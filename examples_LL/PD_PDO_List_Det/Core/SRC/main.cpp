/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.cpp
  * @brief   PD_PDO_List_Det —— FUSB302 纯 SINK，按下按键后读取
  *          Source_Capabilities 并把 PDO 列表显示在板载 160x80 屏上。
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.hpp"
#include "rcc.hpp"
#include "gpio.hpp"
#include "i2c.hpp"
#include "spi.hpp"
#include "tim.hpp"
#include "crc.hpp"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "st7735.hpp"          /* 共享 C++ 屏驱库：DMA + Font_7x10 */
#include "FUSB302.hpp"         /* LoveFinderLibForPY32_LL/FUSB302 */
#include <cstdio>              /* snprintf */
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define PD_CFG_RETRY_MS      500u   /* FUSB302 init 失败重试间隔 */

/* ---- ★ 事件驱动 + 心跳兜底（2026-10-09 起）-------------------------------
 *
 * 下面这几个间隔**不再**是"轮询周期"，而是**心跳兜底**。
 *
 * FUSB302 的 INT_N 接到 PA0（EXTI0 下降沿）。任何被放开的事件一到，ISR 立刻
 * 置 g_pdIrq，主循环当轮就跑对应分支 —— 实际延迟 ~100µs，心跳基本永不生效。
 *
 * 【那为什么还要留心跳？】
 * 因为边沿是会丢的。INT_N 是**电平型闩锁**而非脉冲：
 *   - 服务期间又来一包 → 引脚本来就是低的，不会产生新下降沿。
 *     这一种已经被主循环末尾的"回读引脚重新武装"补掉了；
 *   - 但若芯片进了异常状态、或者板上的上拉/走线出问题，边沿可能整体消失。
 *     没有心跳，程序会**安静地卡死**；有心跳，它只是退化成"慢一点但还活着"。
 *
 * ⚠️ 心跳上限受 tSenderResponse 约束：源端发完 Source_Capabilities 后只等
 *    24~30ms 就要 Request，超时直接 Hard Reset。所以协商期间（还没 PDO）
 *    的心跳必须明显小于 24ms —— 这里保持原来的 5ms 不动；
 *    拿到 PDO 之后没有实时性要求了，放宽到 100ms（20 倍降幅）。
 *
 * 【怎么确认中断真的在干活】
 * 读 g_pdRxIrq / g_pdRxHb 两个计数器对比：正常工作时应是 g_pdRxIrq 涨、
 * g_pdRxHb 几乎不动。若 g_pdRxHb 跟 g_pdRxIrq 一样涨，说明边沿根本没来。 */
#define PD_ATTACH_POLL_MS     10u   /* attach(TOGSS) 兜底心跳 */
#define PD_RX_POLL_MS          5u   /* RX 兜底心跳：协商中（还没 PDO） */
#define PD_RX_IDLE_MS        100u   /* RX 兜底心跳：已拿到 PDO / 已放弃 */
/* ---- 屏刷新节拍（★ 2026-10-09 语义已改）----
 *
 * PD_UI_MIN_MS 以前是"每 80ms 无条件重画一次"的**周期**，现在降级成
 * **两次重画之间的最小间隔**（限速）：只有"内容真的变了"才会走到这里，
 * 它只负责压住"变太频繁"的情况。判据见主循环第 4 块。
 *
 * PD_UI_WATCHDOG_MS 是慢速兜底：即使内容一直不变，也最多隔 500ms 刷新**一行**
 * （轮转 9 行）。目的是保住旧版那条"SPI/DMA 偶发抽风把某行画坏了能自己长回来"
 * 的自愈能力（纯指纹方案有个死角：画坏之后内容再没变过 → 那行就永远错着）。
 * 代价：整屏轮一遍 9×500ms = 4.5s（原来是 2s 一次整屏），但每次只烧 1 行
 * ≈4ms 的 SPI，不再有 35ms 的整屏停顿。 */
#define PD_UI_MIN_MS          80u     /* 两次重画的最小间隔（限速，不是周期） */
/* ★ 2026-10-09 改语义：从"每 2s 整屏重画一次"改成"每 500ms 刷新**一行**"。
 *
 * 原因：整屏重画一次要 35ms（9 个 160×10 矩形），而源端在 tSenderResponse
 * （24~30ms）内等不到 Request 就发 Hard Reset —— 一次 35ms 的整屏重画正好
 * 能把整个协商窗口吃掉（实测就是它把 Request 顶迟了 0.74ms，见 g_rcTxt 那段）。
 * 改成"一次只刷一行"后，任何一次重画都被限死在 1 行 ≈ 4ms 以内。
 *
 * 代价：整屏轮一遍从 2s 变成 9×500ms = 4.5s。看门狗本来只是兜"SPI/DMA
 * 偶发抽风把某行画坏"这种一次性毛刺，4.5s 自愈完全够用。 */
#define PD_UI_WATCHDOG_MS     500u    /* 兜底自愈：每 500ms 刷新一行（轮转） */
#define PD_INT_POLL_MS        20u   /* 中断闩锁兜底心跳 */

/* ---- 按键后的"先听后催"时序 ----
 * 按下按键 → 先听 PD_LISTEN_MS 有没有广播；
 *   有 → 直接读并显示 PDO；
 *   没有 → 发一枪 Soft Reset，再等 PD_SR_WAIT_MS；
 *   最多 PD_SR_MAX 枪，之后放弃。
 *
 * ⚠️ PD_LISTEN_MS 从 2000ms 缩到 250ms：
 *    源端在广播期间是**每 ~150ms 一组** Source_Capabilities（实测抓包
 *    4872.6 / 5022.0 / 5171.4 …，每组 3 条、组内间隔 2ms），所以 250ms
 *    足够判出"有没有广播"，没必要干等 2 秒 —— 业主抱怨的"按下约 2 秒后
 *    才发 Soft Reset"就是这 2000ms 造成的。 */
#define PD_LISTEN_MS         250u
#define PD_SR_WAIT_MS       3000u
#define PD_SR_MAX              3u

/* 刚收到 Hard Reset 之后的静默期。
 * 源端 Hard Reset 后会先拉低 VBUS ~0.9s，再过 ~1s 才开始重播 caps
 * （实测：HRST 3871.7ms → VBUS 掉 3921ms → 回 4786ms → caps 4872.6ms）。
 * 这段时间**不要**发 Soft Reset 去打扰它，安静等它自己重播。 */
#define PD_LISTEN_AFTER_HR_MS 2500u

/* 拔出判据：VBUS 连续掉这么久才算真拔出。
 * ⚠️ 不能太短！源端响应 Soft Reset 时会主动把 VBUS 拉低约 0.9 秒
 *    （实测 3921→4786ms），旧版 3×20ms=60ms 的去抖会把它误判成"拔出"，
 *    于是清掉状态、重开 toggle，源端随后那 5 秒广播就再也收不到了。 */
#define PD_DETACH_MS        1500u

/* ★ 我方主动发 Hard Reset 之后的"恢复窗口"。
 *
 * 这段 VBUS 低是**预期行为**，不是拔出：源端收到 Hard Reset 后必须先撤到
 * vSafe0V 再重建 vSafe5V（与它自己发 HR 时同量级，实测 ~0.9s）。
 * 而 PD_DETACH_MS 只有 1500ms，余量仅 600ms —— 万一这颗源恢复得慢一点
 * 就会被误判成拔出（清状态 + 重开 toggle），所以显式开个窗口屏蔽掉。
 * 取 2500ms 与 PD_LISTEN_AFTER_HR_MS 一致：窗口内我们本来就在安静等 caps。 */
#define PD_HR_RECOVER_MS    2500u

/* ★ 开机阶段探测 attach（toggle）的最长等待。
 *
 * 开机后必须先知道"源端插在哪根 CC 上"，才能把那发 Hard Reset 发到对的线上；
 * 而"插在哪根"只能靠 toggle 探出来（TOGSS）。正常插着线时几十~一两百 ms 就
 * 能落定；这里给 800ms 兜底 —— 超时就先跳过（不阻塞进 PRESS KEY），
 * 由主循环里那套常规的 attach 流程接手。 */
#define PD_BOOT_ATTACH_MS    800u

/* 判拔出后静置这么久才允许重新 attach（防止在抖动的 CC 上反复挂/拔） */
#define PD_DETACH_HOLD_MS    300u

/* 收到 Hard Reset 后做一次 PD_RESET 复活。
 * 用 INTERRUPTA.bit0(I_HARDRST, R/C) 检测，读即清 → 天然边沿检测，
 * 不需要节流，也不会重复复位 FIFO。 */

/* attach 后的阶段 */
enum e_Phase : uint8_t {
  PH_LISTEN      = 0,   /* 按键后短听（250ms） */
  PH_LISTEN_LONG = 1,   /* Hard Reset / VBUS 恢复后长听（2500ms，等源端自己重播） */
  PH_WAIT_SR     = 2,   /* 已发 Soft Reset，等源端重播 */
  PH_GIVEUP      = 3,
};
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* 1 ms tick counter, incremented from SysTick_Handler() */
volatile uint32_t uwTick = 0;
/* USER CODE BEGIN PV */
/* 诊断快照 —— 固定布局，供 OpenOCD 直接读（LTO 会把匿名命名空间里的结构体拆散） */
extern "C" {
volatile uint8_t g_snap[32] = {0};
}

/* ★ RX FIFO 原始字节快照 —— 用来和逻辑分析仪的 I2C / PD 解码逐字节对照，
 *   回答"FIFO 里到底有什么、我们读对没有"。
 *   OpenOCD 读法：
 *     mdb <g_rx_raw> 32       最近一次从 FIFO 读到的原始字节
 *     mdw <g_rx_len> 1        （低字节 len，次字节 tok，再次 ndo）
 *     mdw <g_rx_try> 3        try / ok / cap 三个计数
 *     mdb <g_s1_hist> 8       最近 8 次 STATUS1（看 RX_EMPTY 有没有翻过 0） */
extern "C" {
volatile uint8_t  g_rx_raw[32] = {0};
volatile uint8_t  g_rx_len = 0;
volatile uint8_t  g_rx_tok = 0;
volatile uint16_t g_rx_hdr = 0;
volatile uint8_t  g_rx_ndo = 0;
volatile uint32_t g_rx_try = 0;   /* 进入 RX 分支次数 */
volatile uint32_t g_rx_ok  = 0;   /* fetchMessage 成功次数 */
volatile uint32_t g_rx_cap = 0;   /* 解析成 Source_Cap 的次数 */
volatile uint8_t  g_rx_s1  = 0;   /* 读时的 STATUS1 */
volatile uint8_t  g_s1_hist[8] = {0};
volatile uint8_t  g_s1_idx = 0;
}

/*--------------------------------------------------------------------------
 * 事件日志（环形，固定 8 字节/条）—— 供 OpenOCD 直接读
 *
 * 时序背景：本板 3V3 和 DAPLink 都取自诱骗 VBUS，屏上看不到东西时
 * 只能靠"抓包 + RAM 日志"对齐时间线。所以：
 *   复位 → 放开 CC 上的 Rd → 阻塞等按键 → 按下才开始 PD 业务并记日志。
 *
 * 单条记录（8 字节）：
 *   [0..3] t，毫秒，小端，**相对按键按下时刻**（g_t0）
 *   [4]    事件码（见 e_Log）
 *   [5]    arg0
 *   [6]    arg1
 *   [7]    备用
 *
 * 读法：条数 = g_log_head，第 n 条在 g_log + (n & 63) * 8。
 *   openocd ... -c "init" -c "halt" -c "mdw <g_log_head> 1" -c "mdb <g_log> 512"
 *------------------------------------------------------------------------*/
extern "C" {
volatile uint32_t g_log_head = 0;     /* 已写条数（单调递增，取模 64 定位） */
volatile uint8_t  g_log[512] = {0};   /* 64 条 × 8 字节 */
volatile uint32_t g_log_t0   = 0;     /* 日志时基（按键按下时的 BSP_GetTick） */
}

/*--------------------------------------------------------------------------
 * ★ 中断握手 + 诊断量（extern "C"，供 OpenOCD 直接读）
 *
 * 数据通路是**单向**的，ISR 与主循环之间不共享任何其它状态：
 *
 *   FUSB302 闩锁置位
 *        → INT_N 拉低（开漏，低有效）
 *        → PA0 下降沿
 *        → EXTI0_1_IRQHandler()
 *        → g_pdIrq = 1                       ← ISR 只做这一件事，绝不碰 I²C
 *        → App_Run() 取走 g_pdIrq，做 I²C 服务
 *        → 服务完回读 PA0 电平；若仍是低（服务期间又来了报文）→ 再置一次
 *
 * OpenOCD 读法：
 *   mdw <g_pdIrqCnt> 1     ISR 累计进入次数
 *   mdw <g_pdIrqRearm> 1   服务期间漏掉的边沿，靠回读引脚补触发的次数
 *   mdw <g_pdRxIrq> 2      RX 服务由【中断】/【心跳】触发的次数
 *                          —— 这两个数一对比就知道中断到底有没有在干活
 *   mdb <g_pdIntReg> 1     最近一次 Interrupt(0x42) 快照
 *------------------------------------------------------------------------*/
extern "C" {
volatile uint8_t  g_pdIrq      = 0;   /* ISR 置 1，主循环消费后清 0 */
volatile uint32_t g_pdIrqCnt   = 0;   /* ISR 累计进入次数 */
volatile uint32_t g_pdIrqRearm = 0;   /* 服务期间漏掉的边沿（回读引脚补触发） */
volatile uint32_t g_pdRxIrq    = 0;   /* RX 服务由中断触发 */
volatile uint32_t g_pdRxHb     = 0;   /* RX 服务由心跳兜底触发 */
volatile uint8_t  g_pdIntReg   = 0;   /* 最近一次 Interrupt(0x42) 快照 */
}
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* External functions --------------------------------------------------------*/
/* USER CODE BEGIN ExternalFunctions */
/* USER CODE END ExternalFunctions */

/* USER CODE BEGIN 0 */
/*============================================================================
 * 屏幕布局
 *
 * Font_7x10 每个字符 7px 宽、10px 高 → 160/7 = 22 列，80/10 = 8 行。
 * 两种视图：
 *
 *   [状态视图]  —— 还没拿到 Source_Capabilities 时显示，只报"卡在哪一步"
 *   ┌──────────────────────┐
 *   │PD PDO DETECT         │ 行0  标题（HeaderBar 反白）
 *   │                      │ 行1..2
 *   │WAIT ATTACH           │ 行3  WAIT ATTACH / LISTENING / SOFT RESET n / NO SOURCE
 *   │                      │ 行4..7
 *   └──────────────────────┘
 *   （业主要求：屏上只显示 PDO，所以状态视图只留一行提示。）
 *
 *   [PDO 视图] —— 收到 Source_Capabilities 后显示
 *   ┌──────────────────────┐
 *   │PDO n=4 CC1 VBUS1     │ 行0  摘要
 *   │1 FIX 5.0V 3.00A 15W  │ 行1
 *   │2 FIX 9.0V 3.00A 27W  │ 行2
 *   │3 FIX 12.0V 3.00A 36W │ 行3
 *   │4 PPS 3.3-11.0V 2.00A │ 行4
 *   │...                   │ 行5..7
 *   └──────────────────────┘
 *
 * 注：行7 原来画过一个"心跳方块"（红/绿/蓝/黄循环，80ms 换一次），
 *     用来肉眼确认 MCU 活着。现在有逻辑分析仪，抓 SPI 波形即可判断，已删除。
 *============================================================================*/
namespace {

using LoveFinderLib::FUSB302;
using LoveFinderLib::FUSB302_Message;
using LoveFinderLib::FUSB302_PDO;
using LoveFinderLib::e_FUSB302_CC;
using LoveFinderLib::e_FUSB302_PDOType;

/* LCD 对象（统一版库：一个对象 = 一块屏；硬件配置见 Core/INC/lf_config.h） */
LoveFinderLib::ST7735 g_lcd;

constexpr uint16_t FONT_W = 7;      /* Font_7x10 字宽 */
constexpr uint16_t ROW_H  = 10;     /* Font_7x10 行高 */
constexpr uint8_t  COLS   = 22;     /* 160 / 7 */
constexpr uint8_t  ROWS   = 8;      /* 80 / 10 */

constexpr uint16_t C_BAR = 0x10A2;  /* 标题栏底色：深灰蓝 */
constexpr uint16_t C_DIM = 0x8410;  /* 次要文字：灰 */
constexpr uint16_t C_OK  = ST7735::GREEN;
constexpr uint16_t C_NG  = ST7735::RED;
constexpr uint16_t C_KEY = ST7735::CYAN;
/* 选中档位的反白底色（申请成功那一条 PDO 用它）。
 * RGB565：R=2 G=15 B=19 → RGB888 ≈ (16, 60, 156)，中蓝，配白字对比度高。 */
constexpr uint16_t C_SEL_BG = 0x11F3;

/*--------------------------------------------------------------------------*/
/* 整行输出（★ DMA 版）                                                      */
/*--------------------------------------------------------------------------*/
/* WriteString 只覆盖字符串本身的宽度，不补空格的话上一帧的内容会残留在行尾。
 * 所以这里统一把每行补齐到 22 个字符再输出。 */

/* ---- DMA 文字渲染的"上下文" ----------------------------------------------
 * ST7735_RowRenderFn 的签名是 void(uint16_t y, uint8_t* dst, uint16_t w)，
 * **没有**上下文参数，所以用一组文件级静态量把"这一行要画什么"传进去。
 * 主循环单线程、Row() 同步返回，不存在重入，安全。 */
uint16_t        g_rowFg  = 0;
uint16_t        g_rowBg  = 0;
uint16_t        g_rowTop = 0;              /* 本行在屏上的起始 y（绝对行号） */
const uint16_t* g_rowGlyph[COLS] = {};     /* 每字符的字模行指针，Row() 里预先查好 */

/* ---- ★ 行缓存：这一行的像素不会变，就一个字节 SPI 都不发 ------------------
 *
 * 【为什么必须要有它 —— 2026-10-09 实测的致命时序】
 *
 *   整屏重画 = 标题条 + 8 行 = 9 个 160×10 的矩形。实测**一次要 35ms**
 *   （I²C 抓包 iicbad.csv 里表现为一段 35.4ms 的整条总线静默）。
 *
 *   而这段静默正好压在"我方发 Soft Reset → 等源端重播 Source_Capabilities"
 *   的窗口里，源端重播之后只等 tSenderResponse（PD 规范 24~30ms）就要 Request，
 *   超时直接发 Hard Reset。两次抓包（iicbad.csv + pdbad.csv）对齐后是：
 *
 *     257.164ms  我方 TX Soft Reset（dirty=true → 立刻触发一次整屏重画）
 *     257.8~293.2ms  整屏重画，主循环被堵死 35.4ms
 *     265.000ms  源端 Source_Cap 到达 —— 就堵在这段重画里，读不到
 *     293.482ms  重画结束才读到 caps（迟到 28.5ms）
 *     295.433ms  才把 Request 写进 TX FIFO（迟到 30.4ms）
 *     294.695ms  源端 tSenderResponse 超时 → 发 Hard Reset → 协商失败
 *
 *   我们只差 0.74ms。而这一帧**真正变的只有第 3 行**
 *   （"LISTENING..." → "SOFT RESET 1/3"），其余 8 个矩形逐像素都和上一帧一样。
 *
 *   加行缓存后，同一次更新只花 1 行 ≈ 4ms，Request 能在 ~1ms 内发出去。
 *
 * 【判定依据是"像素会不会变"，不是"文字一不一样"】
 *   Row() 把文字补齐到 COLS 之后才比 —— 因为行尾补空格就是为了**覆盖**上一帧，
 *   补齐后相同 ⇒ 像素完全相同 ⇒ 跳过是安全的。前景/背景色也必须一起比
 *   （PDO 列表的反白高亮只改配色、不改文字）。
 *
 * 【为什么可以存全文而不是哈希】
 *   RW_IRAM1 上限 8KB（见 MDK-ARM/Listings 里的 .map），当前只用 3976B，
 *   这 9×(23+2+2+1)=252B 完全放得下；存全文就不存在哈希碰撞导致的"漏画"。
 *
 * 【失效时机】
 *   ① 开机：g_rcOk 全 0 → 第一帧 9 个矩形全画；
 *   ② 看门狗：每 500ms **只作废一行**（轮转 0..ROWS，见主循环第 4 块）——
 *      不能一次全作废，否则又变成 35ms 的整屏重画，等于把刚修好的坑挖回来。 */
char     g_rcTxt[ROWS + 1][COLS + 1] = {}; /* [8] 留给标题条（它没有文字） */
uint16_t g_rcFg[ROWS + 1]  = {};           /* 画这一行时用的前景色 */
uint16_t g_rcBg[ROWS + 1]  = {};           /* 画这一行时用的背景色 */
bool     g_rcOk[ROWS + 1]  = {};           /* 该行"屏幕上确实是这个内容"？ */

/* 看门狗轮转指针：0..ROWS-1 = 正文行，ROWS = 标题条 */
uint8_t g_rcWatchRow = 0;

/* 让某一行的缓存失效 → 下一次 Row()/HeaderBar() 一定会真的画它。 */
void InvalidateRow(uint8_t idx)
{
  if (idx <= ROWS) { g_rcOk[idx] = false; }
}

/* 行回调：调用频率是**每个像素行一次**（一行文字 10px 高 → 回调 10 次）。
 * 每次把整条 160px 像素行铺进 dst（大端 RGB565）；传输由
 * g_lcd.drawFrameRect() 内部的 DMA 乒乓负责 —— CPU 只管算。
 *
 * ⚠️ 字模**必须**用 font_get_glyph() 查，不能写 `Font_7x10.data[...]`：
 *    本字库是"字符级编译"（font_data.cpp 里 `Font_7x10 = {7,10,nullptr}`），
 *    FontDef.data **恒为 nullptr**，字模由 font_get_glyph() 内部 switch 分派。
 *    直接解引用 data 是空指针解引用；更坑的是 LTO 看得见 data 是常量 nullptr，
 *    会把整张字模表判定为"不可达"直接删掉（实测 RO-data 因此掉了 2.2KB），
 *    而编译期 0 error 0 warning —— 完全静默。 */
void RowRender(uint16_t y, uint8_t* dst, uint16_t w)
{
  const uint16_t line = static_cast<uint16_t>(y - g_rowTop);   /* 0..ROW_H-1 */

  for (uint16_t x = 0; x < w; ++x) {
    const uint16_t ci = static_cast<uint16_t>(x / FONT_W);     /* 第几个字符 */
    const uint16_t cj = static_cast<uint16_t>(x % FONT_W);     /* 字符内第几列 */
    uint16_t color = g_rowBg;

    /* ⚠️⚠️ 必须夹住 ci < COLS —— 这是踩过的血坑，别再删！
     *
     * w = 160 像素、FONT_W = 7 → 22 个字符只占 154 像素，x = 154..159
     * 这 6 个"零头"像素算出 ci = 22，而 g_rowGlyph 只有 COLS = 22 个元素
     * （下标 0..21）→ 越界读一个指针。
     *
     * 越界读到的正好是 .bss 里紧邻 g_rowGlyph 的 **g_diag**（map 实证：
     * g_rowGlyph @0x20000210 + 88B = 0x20000268 = g_diag 起始），
     * 于是取到 g_diag 前 4 字节 {tried, ready, idValid, devId} 拼成的
     * uint32 当作"字模指针"：
     *   开机时 g_diag 全 0 → 指针 = 0 → glyph[line] 读地址 0（被别名到
     *     flash 向量表，读得动）→ 不崩，只是行尾 6 个像素是垃圾色 ——
     *     **所以开机画面看起来完全正常，掩盖了问题**；
     *   pd.init() 成功后 tried/ready/idValid=1、devId=0x91 → 指针 =
     *     0x91010101 → 解引用未映射地址 → BusFault → HardFault_Handler
     *     的 while(1) → 表现为"按下按键后画面冻在 PRESS KEY"。
     *
     * 语义上也应该夹：那 6 个像素不属于任何字符，填背景色即可。 */
    if (ci < COLS) {
      const uint16_t* glyph = g_rowGlyph[ci];
      if (glyph != nullptr) {
        /* glyph[line] = 该字符第 line 行的位图，bit(15-cj) = 该列是否点亮 */
        const uint16_t bits = glyph[line];
        if ((bits << cj) & 0x8000u) { color = g_rowFg; }
      }
    }
    ST7735_PUT565(dst, x, color);
  }
}

void Row(uint8_t r, const char* s, uint16_t fg, uint16_t bg = ST7735::BLACK)
{
  if (r >= ROWS) { return; }

  char buf[COLS + 1];
  uint8_t i = 0;
  while (i < COLS && s[i] != '\0') { buf[i] = s[i]; ++i; }
  while (i < COLS) { buf[i] = ' '; ++i; }
  buf[COLS] = '\0';

  /* ★ 行缓存早退 —— 内容（补齐后的文字 + 前景/背景色）与上一帧逐字节相同，
   *   说明这一行**一个像素都不会变**，直接返回，不发任何 SPI。
   *   一次整屏重画 9 个矩形 ≈ 35ms；缓存命中后典型更新只剩 1 行 ≈ 4ms。
   *   （完整来由见文件上方 g_rcTxt 那段说明。） */
  if (g_rcOk[r] && g_rcFg[r] == fg && g_rcBg[r] == bg) {
    bool same = true;
    for (uint8_t k = 0; k <= COLS; ++k) {
      if (g_rcTxt[r][k] != buf[k]) { same = false; break; }
    }
    if (same) { return; }
  }
  /* 记下"这一行现在应该是什么"，再画 */
  for (uint8_t k = 0; k <= COLS; ++k) { g_rcTxt[r][k] = buf[k]; }
  g_rcFg[r]  = fg;
  g_rcBg[r]  = bg;
  g_rcOk[r]  = true;

  /* ★ 改走 DMA。
   *
   * 旧路径 ST7735_WriteString → ST7735_WriteChar 是**逐像素一次 SPI 传输**
   * （每字符 7×10=70 次，外加每字符一次 SetAddressWindow），整屏 ~14256 次，
   * 实测刷一屏 ≈80ms —— 主循环 ~90ms 的周期几乎全是它，I²C 轮询只是搭车。
   *
   * 现在把这一行 160×10 交给 g_lcd.drawFrameRect 的 DMA 乒乓
   * （DMA 发第 N 行时 CPU 算第 N+1 行，计算与传输重叠）。
   * 整屏 = 160×80×2B = 25600B，12MHz SPI 下 ≈17ms，已经贴着 SPI 带宽上限。
   * DMA 自检失败时驱动自动退化回阻塞发送，功能不受影响。 */
  /* 先把这一行 22 个字符的字模指针查好 —— 每行只查 22 次，
     而不是每个像素查一次（回调会被调用 10 次 × 160 像素）。 */
  for (uint8_t k = 0; k < COLS; ++k) {
    const uint8_t ch = static_cast<uint8_t>(buf[k]);
    g_rowGlyph[k] = ((ch >= 32u) && (ch <= 126u))
                        ? font_get_glyph(Font_7x10, ch)
                        : nullptr;
  }

  g_rowFg  = fg;
  g_rowBg  = bg;
  g_rowTop = static_cast<uint16_t>(r * ROW_H);
  g_lcd.drawFrameRect(0, g_rowTop, ST7735_WIDTH, ROW_H, RowRender);
}

/*--------------------------------------------------------------------------*/
/* 不依赖字库的可见元素                                                      */
/*--------------------------------------------------------------------------*/
/* 标题栏底色：整条实心填充。即使字库一路出问题，这条蓝灰条也一定会出现 ——
 * 用来把"SPI/面板通路"和"文字渲染"两件事分开定位。
 *
 * ★ 2026-10-09：它也进行缓存（槽位 = ROWS = 8）。
 *   它是**常量填充**，画一次之后永远命中缓存，所以实际只在开机第一帧花 2ms。
 *
 * ⚠️ 顺带说明：DrawStatus() 里 HeaderBar() 之后紧跟 Row(0, "PD PDO DETECT",
 *    C_KEY, C_BAR) —— 两者的矩形**完全相同**（y=0..ROW_H-1，160 宽），
 *    而且 Row(0) 的背景色就是 C_BAR。也就是说标题条总是被 Row(0) 原地盖掉，
 *    本身不产生任何可见像素。它保留下来纯粹是"开机第一帧的探路石"，
 *    缓存之后连这 2ms 也只付一次。 */
void HeaderBar()
{
  if (g_rcOk[ROWS] && g_rcBg[ROWS] == C_BAR) { return; }
  g_rcBg[ROWS] = C_BAR;
  g_rcFg[ROWS] = C_BAR;
  g_rcOk[ROWS] = true;
  g_lcd.fillRectangleDma(0, 0, ST7735_WIDTH, ROW_H, C_BAR);
}

/*--------------------------------------------------------------------------*/
/* 事件日志                                                                  */
/*--------------------------------------------------------------------------*/
enum e_Log : uint8_t {
  L_BOOT    = 1,   /* 复位后进入 App_Run（此时已放开 CC Rd） */
  L_RD_OFF  = 2,   /* 放开 Rd 的结果 arg0=ok */
  L_BTN     = 3,   /* 按键按下 = 日志时基零点 */
  L_INIT    = 4,   /* FUSB302 init，arg0=ok */
  L_DEVID   = 5,   /* arg0=DEVICE_ID */
  L_TOGGLE  = 6,   /* startSinkToggle，arg0=ok */
  L_ATTACH  = 7,   /* arg0=cc(1/2) */
  L_SELCC   = 8,   /* selectCC，arg0=ok，arg1=specRev */
  L_CAP     = 9,   /* 收到 Source_Cap，arg0=PDO 数，arg1=specRev */
  L_CAPHDR  = 10,  /* arg0=header 低字节，arg1=高字节 */
  L_ASK     = 11,  /* 发 Get_Source_Cap，arg0=ok，arg1=第几次 */
  L_TX      = 12,  /* arg0=累计 TX 数 */
  L_RXOTHER = 13,  /* arg0=其它报文 msgType */
  L_VBUS    = 14,  /* arg0=vbusOk */
  L_DETACH  = 15,  /* VBUS 掉 */
  L_LCD     = 16,  /* 屏驱初始化完成 */
  L_INTA    = 17,  /* arg0=INTERRUPTA 快照（R/C，读即清）。逐位见 FUSB302_Bit::IA_*：
                    *   0x01 I_HARDRST   收到 Hard Reset
                    *   0x02 I_SOFTRST   收到 Soft Reset
                    *   0x04 I_TXSENT    我方报文拿到 GoodCRC
                    *   0x08 I_HARDSENT  ★ 我方主动发的 Hard Reset 成功上线
                    *   0x10 I_RETRYFAIL 自动重发耗尽
                    *   0x20 I_SOFTFAIL  自动 Soft Reset 也失败
                    *   0x40 I_TOGDONE   toggle 因检测到设备而停止
                    *   0x80 I_OCP_TEMP  OCP / 过温 */
  L_INTB    = 18,  /* arg0=INTERRUPTB 快照（bit0=GCRCSENT） */
  L_BTNIDLE = 19,  /* arg0=按键静止(松开)电平 */
  L_REATTACH= 20,  /* arg0=0 撤 Rd(模拟拔出) / 1 重新挂 Rd(模拟插入) */
  L_SDAC    = 21,  /* arg0=当前 Slice 值（SDAC 门限扫描梯） */
  L_CCSWEEP = 22,  /* arg0=强制 1(CC1)/2(CC2)，arg1=selectCC 是否成功 */
  L_HRST    = 23,  /* 检测到 Status0a.HARDRST=1（源端发过 Hard Reset） */
  L_RECOVER = 24,  /* arg0=pd.recover() 是否成功，arg1=恢复时用的 CC */
  L_RDO     = 25,  /* 发 Request(RDO)，arg0=是否发出，arg1=申请电流低字节(10mA) */
  L_PSRDY   = 26,  /* 对端对我方 Request 的应答：arg0=1 收到 PS_RDY(=申请成功)
                    *                                0 收到 REJECT(=被拒)
                    *                  arg1=实际收到的 msgType */
  L_HRSND   = 27,  /* ★ 我方主动发 Hard Reset（程序复位后的第一枪）：
                    *   arg0=写 Control3 是否成功，arg1=当时用的 CC(1/2)
                    *   注意这只表示"触发写成功"；**真正上线的确认**是随后
                    *   L_INTA 里出现的 0x08（Interrupta.bit3 I_HARDSENT）。 */
  L_INT0    = 28,  /* ★ Interrupt(0x42) 快照，**已剔除 I_CRC_CHK**。
                    *   逐位见 FUSB302_Bit::I_*：
                    *     0x80 I_VBUSOK    0x40 I_ACTIVITY  0x20 I_COMP_CHNG
                    *     0x10 I_CRC_CHK   0x08 I_ALERT     0x04 I_WAKE
                    *     0x02 I_COLLISION 0x01 I_BC_LVL
                    *   ⚠️ 为什么不记 I_CRC_CHK：它每收到一包有效报文就置位
                    *      一次，而日志环形区只有 64 条 —— 记它会把开机和
                    *      协商那段真正有用的记录全冲掉。所以这里只记
                    *      "VBUS 变化 / Alert" 这类低频事件。
                    *   这个日志码的意义：验证 0x42 现在**真的被读了**，
                    *   也就是 INT_N 不会再永久趴在低电平上。 */
};

/* 写一条日志。t 相对 g_log_t0（按键按下）。 */
void Log(uint8_t code, uint8_t a0, uint8_t a1)
{
  const uint32_t n   = g_log_head;
  const uint32_t off = (n & 63u) * 8u;
  const uint32_t t   = BSP_GetTick() - g_log_t0;

  g_log[off + 0] = static_cast<uint8_t>(t);
  g_log[off + 1] = static_cast<uint8_t>(t >> 8);
  g_log[off + 2] = static_cast<uint8_t>(t >> 16);
  g_log[off + 3] = static_cast<uint8_t>(t >> 24);
  g_log[off + 4] = code;
  g_log[off + 5] = a0;
  g_log[off + 6] = a1;
  g_log[off + 7] = 0;
  g_log_head = n + 1u;
}

/*--------------------------------------------------------------------------*/
/* 等按键（PA1 / KEY_INT，外部 3V3 上拉，按下 = 低电平）                      */
/*--------------------------------------------------------------------------*/
/* 复位后到此为止**不做任何 PD 动作**：配合调用方已放开的 CC Rd，源端看到的是
 * "没有 sink"，不会在我们准备好之前把 Source_Capabilities 发完。
 * 业主按下的那一刻同时开始抓包，于是日志时基和抓包时间轴对齐。 */
bool WaitForButton()
{
  /* ⚠️ 关键：Studio_GPIO_Init() 把 PA1 配成【输入 + 无上下拉】(LL_GPIO_PULL_NO)，
   * 完全依赖板上外部 3V3 上拉。实测这里会悬空抖动 —— 于是"等引脚变低"的自旋
   * 时而立刻通过、时而永远卡住（上一轮就是卡死在这里，g_log 只有 BOOT/RD_OFF
   * 两条、背光不亮）。
   * BUTTON_TEST 之所以正常，是 BUTTON 库在 init() 里自己把引脚配成了上拉。
   * 这里显式补上内部上拉，让"松开 = 稳定高"这件事不再依赖外部元件。 */
  LL_GPIO_SetPinPull(KEY_INT_Port, KEY_INT_Pin, LL_GPIO_PULL_UP);

  /* 不假定极性：先记下静止(松开)电平，再等它变化并保持 30ms。
   * 这样高有效/低有效都能工作，而且把实际电平记进日志便于判读。 */
  const uint8_t idle =
      (LL_GPIO_IsInputPinSet(KEY_INT_Port, KEY_INT_Pin) != 0u) ? 1u : 0u;
  Log(L_BTNIDLE, idle, 0);

  for (;;) {
    const uint8_t v = (LL_GPIO_IsInputPinSet(KEY_INT_Port, KEY_INT_Pin) != 0u) ? 1u : 0u;
    if (v != idle) {
      LL_mDelay(30);                                    /* 去抖 */
      const uint8_t v2 = (LL_GPIO_IsInputPinSet(KEY_INT_Port, KEY_INT_Pin) != 0u) ? 1u : 0u;
      if (v2 == v) {
        Log(L_BTN, v, idle);                            /* arg0=按下电平 arg1=静止电平 */
        return true;
      }
    }
  }
}

/*--------------------------------------------------------------------------*/
/* 运行期诊断快照                                                            */
/*--------------------------------------------------------------------------*/
struct Diag {
  bool         tried;        /* 是否已经尝试过 init */
  bool         ready;        /* FUSB302 init 是否成功 */
  bool         idValid;      /* 是否成功读到 DEVICE_ID */
  uint8_t      devId;        /* DEVICE_ID (0x01) */
  bool         toggleOn;     /* startSinkToggle 是否成功 */
  bool         busOk;        /* Status0 是否读成功 */
  uint8_t      vbusRaw;      /* Status0 原始值 */
  bool         vbusOk;       /* Status0.VBUSOK */
  e_FUSB302_CC cc;           /* 当前 CC 通道 */
  bool         ccSelected;   /* selectCC() 是否成功（GoodCRC 路由已钉死） */
  uint8_t      specRev;      /* 对端 specRev（1=PD2.0 2=PD3.0） */
  uint8_t      intaAcc;      /* INTERRUPTA 累计：bit2=TXSENT bit4=RETRYFAIL bit0=HARDRST */
  uint8_t      intbAcc;      /* INTERRUPTB 累计：bit0=GCRCSENT（芯片回过 GoodCRC） */
  uint8_t      int0Acc;      /* Interrupt(0x42) 累计（**不含 I_CRC_CHK**，见 L_INT0） */
  uint8_t      status1;      /* 最近 STATUS1（bit5=RX 空，bit3=TX 空） */
  uint8_t      status0a;     /* 最近 STATUS0A */
  uint8_t      sdac;         /* 当前 Slice.SDAC 门限值 */
  uint8_t      rxMsgCount;   /* 收到的 Source_Capabilities 条数 */
  uint16_t     lastHeader;   /* 最近一条 Source_Cap 的 Header */
  uint32_t     i2cErr;       /* init 失败累计次数 */
};

Diag g_diag = {};

/* SDAC 门限扫描梯（见 3d 段说明）。
 * Slice = SDAC_HYS(01, 保留 85mV 迟滞) | SDAC，门限 ≈ SDAC × 17mV。 */
constexpr uint8_t kSliceLadder[] = { 0x60u, 0x50u, 0x40u, 0x30u, 0x20u, 0x10u };
constexpr uint8_t kSliceLadderLen =
    static_cast<uint8_t>(sizeof(kSliceLadder) / sizeof(kSliceLadder[0]));

/* 名字缩写成 3 个字符，方便单行排版 */
const char* pdoShortName(e_FUSB302_PDOType t)
{
  switch (t) {
    case e_FUSB302_PDOType::FIXED:    return "FIX";
    case e_FUSB302_PDOType::VARIABLE: return "VAR";
    case e_FUSB302_PDOType::BATTERY:  return "BAT";
    case e_FUSB302_PDOType::APDO_PPS: return "PPS";
    default:                          return "???";
  }
}

/*--------------------------------------------------------------------------*/
/* 关于 GoodCRC：为什么这里**不再**用软件补                                   */
/*--------------------------------------------------------------------------*/
/* 上一版观察到"抓包里始终没有 SNK 发出的 GOOD CRC"，于是改成软件轮询到
 * caps 之后自己 sendRaw 一条 GoodCRC。这条路**在原理上就走不通**：
 *
 *   USB PD 规范给接收方的 GoodCRC 窗口是 **几十 µs**（tGoodCRC），
 *   而我们的轮询周期最快也是毫秒级 —— 等我们发出去，源端早就超时重传了。
 *
 * 真正的原因在 FUSB302 这边（现已修复）：Control0 以前写的是 0x0E，
 *   **AUTO_PRE=1**，而数据手册 "PD Automatic Receive GoodCRC" 明确要求
 *   **AUTO_CRC=1 且 AUTO_PRE=0** 才会自动回 GoodCRC。AUTO_PRE=1 时芯片
 *   改去"收到好报文就从 TX FIFO 自动起发"，硬件自动回包被关掉了。
 *
 * 现在 CONTROL0 = 0x0C（AUTO_PRE=0）+ Switches1.AUTO_CRC=1，
 * 由芯片在硬件层回 GoodCRC，才是唯一可行的做法。软件补包已删除。 */


/* mV -> "12.0" 形式 */
void mVToStr(uint16_t mv, char* out, size_t n)
{
  (void)snprintf(out, n, "%u.%u", static_cast<unsigned>(mv / 1000u),
                 static_cast<unsigned>((mv % 1000u) / 100u));
}

/* 单个 PDO -> 一行 22 字符以内的紧凑文本 */
void PdoToRow(uint8_t idx, const FUSB302_PDO& p, char* out, size_t n)
{
  const unsigned i = static_cast<unsigned>(idx) + 1u;
  char vmax[8];
  char vmin[8];

  switch (p.type) {
    case e_FUSB302_PDOType::FIXED: {
      mVToStr(p.voltage_mV, vmax, sizeof(vmax));
      const unsigned mw = (static_cast<unsigned>(p.voltage_mV) *
                           static_cast<unsigned>(p.maxCurrent_mA)) / 1000000u;
      (void)snprintf(out, n, "%u %s %sV %u.%02uA %uW",
                     i, pdoShortName(p.type), vmax,
                     static_cast<unsigned>(p.maxCurrent_mA / 1000u),
                     static_cast<unsigned>((p.maxCurrent_mA % 1000u) / 10u),
                     mw);
      break;
    }
    case e_FUSB302_PDOType::VARIABLE:
      mVToStr(p.minVoltage_mV, vmin, sizeof(vmin));
      mVToStr(p.voltage_mV, vmax, sizeof(vmax));
      (void)snprintf(out, n, "%u %s %s-%sV %u.%02uA",
                     i, pdoShortName(p.type), vmin, vmax,
                     static_cast<unsigned>(p.maxCurrent_mA / 1000u),
                     static_cast<unsigned>((p.maxCurrent_mA % 1000u) / 10u));
      break;
    case e_FUSB302_PDOType::BATTERY:
      mVToStr(p.minVoltage_mV, vmin, sizeof(vmin));
      mVToStr(p.voltage_mV, vmax, sizeof(vmax));
      (void)snprintf(out, n, "%u %s %s-%sV %uW",
                     i, pdoShortName(p.type), vmin, vmax,
                     static_cast<unsigned>(p.maxPower_mW / 1000u));
      break;
    case e_FUSB302_PDOType::APDO_PPS:
      mVToStr(p.minVoltage_mV, vmin, sizeof(vmin));
      mVToStr(p.voltage_mV, vmax, sizeof(vmax));
      (void)snprintf(out, n, "%u %s %s-%sV %u.%02uA",
                     i, pdoShortName(p.type), vmin, vmax,
                     static_cast<unsigned>(p.ppsCurrent_mA / 1000u),
                     static_cast<unsigned>((p.ppsCurrent_mA % 1000u) / 10u));
      break;
    default:
      (void)snprintf(out, n, "%u raw %08lX", i,
                     static_cast<unsigned long>(p.raw));
      break;
  }
}

/*--------------------------------------------------------------------------*/
/* 状态视图 —— 按键后、还没拿到 PDO 时                                      */
/*--------------------------------------------------------------------------*/
/* 业主要求：屏上只显示 PDO，其它内容一律不画。
 * 所以这里只留一行"现在处于哪一步"：
 *   WAIT ATTACH   → 还没检测到源端
 *   LISTENING...  → 已 attach，正在听广播
 *   SOFT RESET n  → 没听到，已发第 n 枪 Soft Reset 催它
 * 一旦收到 caps 就切到 DrawPdoList()，屏上只剩 PDO 列表；
 * 催满 PD_SR_MAX 枪仍无广播 → 也切到 DrawPdoList()（画 "0 PDO"）作终态。
 * 所以 DrawStatus() 只负责"进行中"，不负责终态。 */
/* App_Run 每轮把当前阶段/已发 Soft Reset 次数写进来，供 DrawStatus() 显示。
 * （原来这里读的是 g_diag.capPrompt，但那个字段从没被赋值，永远显示 LISTENING。） */
uint8_t g_uiPhase   = PH_LISTEN;
uint8_t g_uiSrTries = 0;

/* ★ 对端是否已经对我方发出的 Request 回过 PS_RDY（= 5V 档申请成功）。
 *
 * 判定链（PD 抓包实测时序）：
 *   SNK:Request → SRC:Accept(0x03) → SRC:PS_RDY(0x06)   → g_rdoAck = true
 *   SNK:Request → SRC:Reject(0x04)                      → g_rdoAck = false
 * 只回 Accept 不算成功（那只是"受理了"），必须等到 PS_RDY（电源已切到该档）。
 * 清零时机：新一轮 caps、拔出、以及发出新 Request 之前。
 * 屏上用它决定第 1 条 PDO 要不要反白高亮。 */
bool g_rdoAck = false;

void DrawStatus()
{
  HeaderBar();
  Row(0, "PD PDO DETECT", C_KEY, C_BAR);

  /* ★ 2026-10-09：先把用不到的行擦干净，再画状态文字。
   *
   * 【为什么必须擦】
   *   DrawPdoList() 会把 8 行**全部**写满（PDO 行 + 末尾补空行），
   *   而 DrawStatus() 只用第 0 行和第 3 行。
   *   于是"从 PDO 列表切回状态页"时（唯一路径是拔出：havePdo → false），
   *   第 1/2/4/5/6/7 行还挂着**上一帧的 PDO 文字** ——
   *   屏上会同时出现 "WAIT ATTACH" 和旧的 "2 FIX 9.0V 3.00A 27W"。
   *   开机那两行 "PRESS KEY" / "waiting button" 残留也是同一个原因。
   *
   * 【为什么旧版没暴露】
   *   旧版每 80ms 无条件重画，但重画调的也是 DrawStatus() ——
   *   同样只写第 0/3 行，所以**治不了**这个残留，只是没人盯着看而已。
   *   现在改成"变了才画"，一次视图切换必须把上一帧**完整**盖掉，
   *   否则残留会一直停在屏上（没有后续重画去"碰运气"擦掉它）。
   *
   * 第 3 行跳过（下面三个分支必写它），省一次 160×10 的 DMA。 */
  for (uint8_t r = 1; r < ROWS; ++r) {
    if (r != 3u) { Row(r, "", ST7735::BLACK); }
  }

  if (g_diag.cc == e_FUSB302_CC::NONE) {
    Row(3, "WAIT ATTACH", ST7735::YELLOW);
  } else if (g_uiSrTries == 0u) {
    Row(3, (g_uiPhase == PH_LISTEN_LONG) ? "LISTEN (HR)..." : "LISTENING...",
        ST7735::YELLOW);
  } else {
    char buf[COLS + 1];
    (void)snprintf(buf, sizeof(buf), "SOFT RESET %u/%u",
                   static_cast<unsigned>(g_uiSrTries),
                   static_cast<unsigned>(PD_SR_MAX));
    Row(3, buf, ST7735::YELLOW);
  }
  /* 注：**终态**（催满 PD_SR_MAX 次 Soft Reset 仍没等到 caps）不在这里画 ——
   *     交给 DrawPdoList() 输出 "0 PDO"。这样"按下按键后屏上要么 0 PDO、
   *     要么就是 PDO 列表"只有一条出口，不会出现第三种画面。 */
}

/*--------------------------------------------------------------------------*/
/* PDO 视图                                                                  */
/*--------------------------------------------------------------------------*/
/* 屏上就是**纯粹的 PDO 列表**，从第 1 行（行 0）开始排，没有任何其它内容：
 *
 *   拿到 caps：  1 FIX 5.0V 3.00A 15W      ← 反白高亮 = 我们成功申请到的档位
 *               2 FIX 9.0V 3.00A 27W
 *               3 FIX 12.0V 3.00A 36W
 *               4 PPS 3.3-11.0V 2.00A
 *
 *   没拿到 caps：0 PDO
 *
 * 已按要求删掉的两样东西：
 *   ① 行 0 的 "PDO n=4 CC1 VBUS1" 汇总行；
 *   ② 底部的灰字诊断行 "Hdr 43A1  caps x1"（Hdr = 源端报头，caps = 收到几条 caps）。
 *      那两条数据仍然每轮写进 g_snap[11..12] / g_snap[10]，需要时用 OpenOCD 读，
 *      不占屏幕。 */
void DrawPdoList(const FUSB302_Message& msg)
{
  char buf[COLS + 1];
  uint8_t shown = 0;

  for (uint8_t i = 0; i < msg.pdoCount && i < ROWS; ++i) {
    /* ★ 高亮：只高亮**第 1 条**（本程序固定申请第一个 Fixed 档，即 5V 那一档），
     *   而且只有对端真的回了 PS_RDY（g_rdoAck）才反白 ——
     *   被 REJECT 或还没应答就保持普通配色，一眼能看出"没谈成"。 */
    const bool     sel = (i == 0u) && g_rdoAck;
    const uint16_t bg  = sel ? C_SEL_BG : ST7735::BLACK;
    const uint16_t fg  = sel ? ST7735::WHITE : C_KEY;

    FUSB302_PDO p;
    if (!FUSB302::decodePdo(msg.pdo[i], p)) {
      (void)snprintf(buf, sizeof(buf), "%u raw %08lX",
                     static_cast<unsigned>(i) + 1u,
                     static_cast<unsigned long>(msg.pdo[i]));
      Row(static_cast<uint8_t>(i), buf, C_NG, bg);
    } else {
      PdoToRow(i, p, buf, sizeof(buf));
      Row(static_cast<uint8_t>(i), buf, fg, bg);
    }
    shown = static_cast<uint8_t>(i + 1u);
  }

  /* 一条 PDO 都没有（源端一直没广播 / 谈崩了）：明写 "0 PDO"，
     和"有列表但没画出来"区分开。 */
  if (shown == 0u) {
    Row(0, "0 PDO", C_NG);
    shown = 1u;
  }

  /* 末尾补空行，抹掉上一帧可能更长的列表 */
  for (uint8_t r = shown; r < ROWS; ++r) {
    Row(r, "", ST7735::BLACK);
  }
}

/* 终态视图：催满 Soft Reset 也没有 caps → 画 "0 PDO"。
 * 单独包一层是为了让主循环里那一处调用点自解释（传空 msg 就是"没有 PDO"）。 */
void DrawNoPdo()
{
  const FUSB302_Message none = {};
  DrawPdoList(none);
}

/*--------------------------------------------------------------------------*/
/* 应用主循环（不返回）                                                      */
/*--------------------------------------------------------------------------*/
/* ★ 2026-10-09：屏上内容的"指纹"。
 *
 * 主循环第 4 块用"内容变了才重画"代替原来的"每 80ms 无条件重画"，
 * 这个结构体就是"屏上画的是什么"的完整描述 ——
 * 它必须覆盖 DrawStatus() / DrawPdoList() / DrawNoPdo() 的**每一个输入**，
 * 漏掉任何一个都会出现"内容变了但屏上没跟着变"。
 *
 * 对应关系（改画图函数时请同步回来改这里）：
 *   view      选哪个视图      ← havePdo / (phase == PH_GIVEUP)
 *   cc        状态页 WAIT ATTACH ← g_diag.cc
 *   phase     状态页 LISTEN/LISTEN(HR) ← g_uiPhase
 *   srTries   状态页 SOFT RESET n/3    ← g_uiSrTries
 *   rdoAck    PDO 页第 1 条是否反白    ← g_rdoAck
 *   pdoCount  PDO 页画几行             ← msg.pdoCount
 *   pdo[]     PDO 页每行的内容         ← msg.pdo[i]
 *
 * 数组固定 7 项：FUSB302_Message::pdo 最多 7 个数据对象（4 字节报文头
 * 之后的 NDO 上限），DrawPdoList 的循环也止于 ROWS/7。 */
struct UiFp {
  uint8_t  view;       /* 0=状态页  1=PDO 列表  2="0 PDO" 终态页 */
  uint8_t  cc;         /* e_FUSB302_CC，状态页用 */
  uint8_t  phase;      /* g_uiPhase，状态页用 */
  uint8_t  srTries;    /* g_uiSrTries，状态页用 */
  uint8_t  rdoAck;     /* 0/1，PDO 页高亮用 */
  uint8_t  pdoCount;   /* 0..7 */
  uint32_t pdo[7];     /* PDO 原始 32 位字 */
};

void App_Run()
{
  FUSB302         pd;
  FUSB302_Message msg;
  FUSB302_Message lastMsg = {};

  bool     havePdo   = false;
  bool     attached  = false;   /* 已 attach（toggle 已停、CC 路由已钉死） */
  bool     doReset   = true;    /* 只有开机那次 init 允许软复位 */

  uint32_t tAttach   = 0;   /* attach / VBUS 状态机计时 */
  uint32_t tRx       = 0;   /* RX FIFO 轮询计时 */
  uint32_t tRetry    = 0;   /* init 失败重试计时 */
  uint32_t tUi       = 0;   /* 屏刷新计时 */
  uint32_t tInt      = 0;   /* STATUS0A / 中断轮询计时 */

  uint8_t  phase     = PH_LISTEN;  /* 先听后催 阶段 */
  uint32_t tPhase    = 0;          /* 阶段计时 */
  uint8_t  srTries   = 0;          /* 已发出的 Soft Reset 次数 */

  uint32_t tVbusLow  = 0;          /* >0 = VBUS 从该时刻起一直是低的 */
  uint32_t tDetach   = 0;          /* 上次拔出时刻（重挂静置用） */
  uint32_t tHrRecover = 0;         /* >0 = 刚发过 Hard Reset，恢复窗口起点 */

  bool     dirty   = true;

  /* ★ 屏上"上一帧画的是什么"的指纹 + 有没有画过第一帧。
   *   uiFpValid 必须要有：UiFp 全 0 是**合法**状态（PH_LISTEN=0、
   *   e_FUSB302_CC::NONE=0、没有 PDO），光靠"零值"分不出"没画过"和
   *   "画过一帧全 0 的"。dirty 开机就是 true，所以第一帧一定会画，
   *   这里只是把这件事写成显式条件，不依赖"dirty 恰好是 true"。 */
  UiFp     uiFpLast  = {};
  bool     uiFpValid = false;

  /* ⓪ 开机日志。
   *
   * ★ 2026-10-09 改动：开机**不再**"不碰 FUSB302"。
   *   业主要求把 Hard Reset 从"按键之后"提前到"程序跑起来之后立马"，
   *   所以这里必须主动配置并操作 FUSB302 —— 完整理由见下面 ②。
   *   （旧注释担心"写 Switches0=0 撤掉 Rd → 源端以为 sink 拔出"，
   *     现在这恰恰是我们**想要**的效果：让源端把上一轮的合约清掉。） */
  Log(L_BOOT, 0, 0);

  /* ① 先把屏点亮！
   *
   * 现在整板（PY32 / FUSB302 / 屏）与 DAPLink 共用电脑 3V3，不再受 PD VBUS
   * 影响，所以可以把屏驱初始化提到等按键【之前】——
   * 之前屏全黑时根本分不清是"固件没跑"还是"在等按键"，调试非常痛苦。
   * 点亮屏之后，一眼就能看出固件状态。 */
  g_lcd.init();
  g_lcd.fillScreen(ST7735::BLACK);

  /* ★ 2026-10-09 第八轮补：**开机就把整帧铺一遍**，把行缓存填满。
   *
   * 【为什么必须在这里补】
   *   `g_lcd.fillScreen()` 是直接整屏灌像素，**不经过 `Row()`**，
   *   所以它一个缓存槽都不会填。开机只画了第 3/4/6 行，于是第
   *   0/1/2/5/7 行 + 标题条的 `g_rcOk` 全是 0。
   *
   *   按键之后主循环第一帧走 `DrawStatus()`，这 9 个矩形**一次全画** ——
   *   实测 **35.3ms**（见 README §6g）。而这一帧正好落在"先听 250ms"
   *   窗口的**最开头**：
   *     源端每 ~145ms 广播一组 caps（组内 3 条、间隔 2ms）。如果有一组
   *     恰好落在按键后 ~1.7~9ms 之间，它就会被这 35ms 挡住，等我们醒过来
   *     读走再回 Request，已经超过源端 tSenderResponse（24~30ms）→ HRST。
   *     命中概率约 (6.5+4)/145 ≈ **7%**，属于"偶尔按一次就失败"。
   *
   *   先把整帧铺满之后，按键后第一帧**只有第 3、4 行真的变**
   *   （`PD PDO LIST DET` → `LISTENING...`、`PRESS KEY` → 空白），
   *   一帧只花 ~10ms，从根上消掉这个窗口。
   *
   * 【为什么标题条和标题行也要一起铺】
   *   `DrawStatus()` 第一件事就是 `HeaderBar()` + `Row(0, "PD PDO DETECT"…)`。
   *   不预铺的话它俩也会跟着重画，白白多 8ms。
   *
   * 【代价】
   *   开机多烧 ~24ms 的 SPI —— 这一步在等按键之前，没有任何 deadline。 */
  HeaderBar();
  Row(0, "PD PDO DETECT", C_KEY, C_BAR);
  for (uint8_t r = 1u; r < ROWS; ++r) {
    if ((r != 3u) && (r != 4u) && (r != 6u)) { Row(r, "", ST7735::BLACK); }
  }
  Row(3, "PD PDO LIST DET", ST7735::CYAN);
  Row(4, "PD STARTING..",   ST7735::YELLOW);   /* 开机阶段：正在打那发 Hard Reset */
  Row(6, "",                ST7735::BLACK);
  Log(L_LCD, 0, 0);

  /* ② ★★ 开机第一枪：配好 FUSB302 → 探 attach → 发一条 PD Hard Reset ★★
   * ======================================================================
   * 【为什么要发】
   *   FUSB302 由板上 3V3 供电，**MCU 复位它并不掉电** —— Switches0 的 Rd、
   *   Switches1 的 TXCC、以及"上一轮谈成的合约"全部原样保留。
   *   而源端那边同样不知道我们复位过，所以它认为"SINK 还挂着、5V 合约仍然
   *   有效"，不会重播 Source_Capabilities。反复烧录调试时，我们看到的很可能
   *   是**上一个程序**谈好的状态（甚至可能是别的程序谈成的 9V/12V 合约）。
   *   发一条 Hard Reset，让源端把协议层和电源合约**一起清零重来**。
   *
   * 【为什么必须是"开机就发"，而不是"按键再发"】
   *   业主实测后明确要求：这一枪要在"程序跑起来"之后立刻打出去。
   *   放在按键之后，上一轮的合约会一直挂到按键那一刻才被清掉。
   *
   * 【顺序为什么是这样】
   *   1) init()：软复位芯片 + 配好 AUTO_CRC/specRev/中断屏蔽。
   *              （doReset=true 是本次运行**唯一**允许的软复位，之后的重试
   *                绝不再软复位 —— 那会撤掉 Rd，对源端表现为拔出。）
   *   2) startSinkToggle() + pollAttach()：
   *              PD 报文必须从**正确的那根 CC** 发出去，而"源端插在哪根 CC
   *              上"只有 toggle 探测（TOGSS）才知道。早于 selectCC() 发送
   *              有可能发到空线上，等于白发。
   *   3) selectCC()：把 Rd / MEAS_CCx（= RX 选线）/ TXCCx 一起钉到那根 CC。
   *   4) sendHardReset()：Control3(0x09).SEND_HARD_RESET=1，芯片自己组包，
   *              不写 TX FIFO。真正上线的确认是 Interrupta.bit3 I_HARDSENT，
   *              会出现在日志的 L_INTA 里（arg0 = 0x08）。
   *
   * 【时序后果 —— 关键】
   *   源端收到 Hard Reset 后**必须**先把 VBUS 撤到 vSafe0V、再重建 vSafe5V，
   *   实测低约 0.9 秒，之后才开始重播 Source_Capabilities。
   *   → 开 tHrRecover 恢复窗口（PD_HR_RECOVER_MS=2500ms），把这段 VBUS 低
   *     屏蔽掉，别让主循环当成"拔出"。
   *
   * 【重播的 caps 怎么处理】—— 业主明确要求：**忽略**。
   *   PDO 检测从按键按下那一刻才开始（见 ③），所以开机阶段我们只负责把
   *   Hard Reset 打出去、把 CC 路由钉好，之后源端重播什么都不管。
   *
   * 【探不到 attach 怎么办】
   *   没插线 / 源端不在时 pollAttach() 会一直返回 NONE。这里最多等
   *   PD_BOOT_ATTACH_MS 就往下走（不阻塞进 PRESS KEY），
   *   由主循环里那套常规的 attach 流程接手。 */
  g_diag.tried = true;
  if (pd.init(I2C1)) {
    g_diag.ready    = true;
    g_diag.devId    = pd.deviceId();
    g_diag.idValid  = true;
    g_diag.toggleOn = pd.startSinkToggle();
    Log(L_INIT,   1, 0);
    Log(L_DEVID,  g_diag.devId, 0);
    Log(L_TOGGLE, g_diag.toggleOn ? 1u : 0u, 0);

    const uint32_t tBoot = BSP_GetTick();
    e_FUSB302_CC   cc    = e_FUSB302_CC::NONE;
    while ((BSP_GetTick() - tBoot) < PD_BOOT_ATTACH_MS) {
      cc = pd.pollAttach();
      if (cc != e_FUSB302_CC::NONE) { break; }
    }

    if (cc != e_FUSB302_CC::NONE) {
      g_diag.cc         = cc;
      g_diag.ccSelected = pd.selectCC(cc);
      attached          = true;        /* 已 attach → 主循环不必再挂一次 */
      Log(L_ATTACH, static_cast<uint8_t>(cc), 0);
      Log(L_SELCC,  g_diag.ccSelected ? 1u : 0u, g_diag.specRev);

      tHrRecover = BSP_GetTick();
      const bool hrOk = pd.sendHardReset();
      Log(L_HRSND, hrOk ? 1u : 0u, static_cast<uint8_t>(cc));
    } else {
      Log(L_ATTACH, 0, 0);   /* arg0=0：开机探测没等到 attach（没插线？） */
    }
  } else {
    Log(L_INIT, 0, 0);
  }
  doReset = false;              /* 之后的重试绝不再软复位（会撤 Rd -> 变成拔出） */
  tRetry  = BSP_GetTick();

  /* ③ 等按键。按下的那一刻 = 日志时基零点 = 业主开始抓包的时刻。
   *    （WaitForButton 内部记 L_BTNIDLE / L_BTN）
   *
   * ⚠️ 开机那几行日志（L_BOOT/L_LCD/L_INIT/L_DEVID/L_TOGGLE/L_ATTACH/
   *    L_SELCC/L_HRSND）的时间戳是**相对 MCU 上电**的绝对毫秒；
   *    从这里往后才是相对按键的。日志里能明显看到这个断点。 */
  Row(4, "PRESS KEY",      ST7735::YELLOW);
  Row(6, "waiting button", C_DIM);
  WaitForButton();
  g_log_t0 = BSP_GetTick();

  /* ★ 开机那发 Hard Reset 之后，FUSB302 的 PD 接收逻辑可能锁死
   *   （实测：HR 之后 Status1.RX_EMPTY 恒为 1，一条报文都进不了 RX FIFO）。
   *   pdsink 在"发出或收到"Hard Reset 后都会做 hr_cleanup()，
   *   这里等价地补一次 PD_RESET —— 离发出 HR 已经过去很久，不会打断报文。
   *   没有这一步，按键之后可能永远收不到源端重播的 caps。 */
  if (attached) {
    g_diag.ccSelected = pd.recover(g_diag.cc);
    Log(L_RECOVER, g_diag.ccSelected ? 1u : 0u,
                   static_cast<uint8_t>(g_diag.cc));
  }

  /* ★ 短听窗口从**按键这一刻**起算。
   *   开机就 attach 的话，主循环不会再走 attach 分支（那里原本会设 tPhase），
   *   不补这一句 tPhase 就还是 0，`now - tPhase` 会瞬间 >= 250ms ——
   *   于是"先听"这一步被跳过、直接发 Soft Reset。
   *   那正是业主这次要用逻辑分析仪验证的点，绝不能错。 */
  tPhase = g_log_t0;

  for (;;) {
    const uint32_t now = BSP_GetTick();

    /* ★ 取走中断标志 —— 必须在所有服务分支之前。
     *
     * 顺序是"先清再服务"，不是"服务完再清"：如果服务完才清，那么服务期间
     * ISR 又置起来的那一次会被我们抹掉 → 白白丢一包。先清的话，服务期间
     * 的新边沿照样能把标志重新立起来，下一轮立刻再跑一遍。
     *
     * g_pdIrq 只在 ISR 里置 1、只在主循环里清 0，是标准的单写者/单读者
     * 握手。Cortex-M0+ 上 8 位 volatile 读/写本身就是原子的，不用关中断。 */
    const bool irqSet = (g_pdIrq != 0u);
    if (irqSet) { g_pdIrq = 0u; }

    /* ---------------- 1. FUSB302 初始化（失败则周期重试） ---------------- */
    if (!g_diag.ready && (now - tRetry) >= PD_CFG_RETRY_MS) {
      tRetry = now;
      g_diag.tried = true;

      if (pd.init(I2C1, LoveFinderLib::FUSB302_I2C_ADDR, doReset)) {
        g_diag.ready    = true;
        g_diag.devId    = pd.deviceId();
        g_diag.idValid  = true;
        g_diag.toggleOn = pd.startSinkToggle();
      } else {
        g_diag.ready    = false;
        g_diag.toggleOn = false;
        g_diag.idValid  = false;
        g_diag.devId    = 0;
        g_diag.i2cErr++;
        havePdo = false;
      }
      dirty = true;
    }

    /* ---------------- 2. attach + "先听后催" 状态机 ----------------
     * 业主要求：按下按键后先看有没有广播 ——
     *   有广播 → 直接读并显示 PDO；
     *   没广播 → 发一枪 Soft Reset 把源端叫回来，再等广播。
     * 依据（业主实测）：SW3526S 在 Rd 有效后只广播约 5 秒就停，
     *                  只有 Soft Reset 能让它重播（期间必须保持 Rd 有效）。
     *
     * ★ 触发源：中断（irqSet）或 PD_ATTACH_POLL_MS 心跳，两者取或。
     *   VBUS 变化（I_VBUSOK）和 toggle 落定（I_TOGDONE）现在都会立刻
     *   把这一块叫醒，不用再等 10ms 心跳。 */
    if (g_diag.ready && (irqSet || (now - tAttach) >= PD_ATTACH_POLL_MS)) {
      tAttach = now;

      /* ---- VBUS 时间型去抖 + "掉过又回来"的恢复 ----
       * ⚠️ 旧版是"连续 3 次采样(60ms)都掉 = 拔出"。实测源端响应 Soft Reset 时
       *    会主动把 VBUS 拉低约 0.9 秒（3921→4786ms），60ms 去抖必然误判成拔出，
       *    于是清状态、重开 toggle，源端随后那 5 秒 caps 广播就全丢了。
       *    现在改成时间型：连续低 PD_DETACH_MS 才算拔出；
       *    而"掉过又回来"则当成**源端重启过**（Soft/Hard Reset 都会这样），
       *    直接做一次 PD_RESET 复活 —— 实测 FUSB302 收到 Hard Reset 后
       *    Status1.RX_EMPTY 恒为 1，不复位就永远收不到后面的 caps。 */
      const bool vbusLowNow = (g_diag.busOk && !g_diag.vbusOk);

      /* ★ Hard Reset 恢复窗口：这段 VBUS 低是**预期**的（源端要先撤到
       *   vSafe0V 再重建 vSafe5V），绝不能判成"拔出"。窗口过了自动失效。 */
      const bool inHrRecover =
          (tHrRecover != 0u) && ((now - tHrRecover) < PD_HR_RECOVER_MS);

      if (vbusLowNow) {
        if (tVbusLow == 0u) { tVbusLow = now; }
      } else if (tVbusLow != 0u) {
        tVbusLow = 0u;
        if (attached) {
          g_diag.ccSelected = pd.recover(g_diag.cc);
          if (!havePdo) {
            srTries = 0;
            phase   = PH_LISTEN_LONG;   /* 源端还要 ~1s 才重播，安静等 */
            tPhase  = now;
          }
          dirty = true;
          Log(L_RECOVER, g_diag.ccSelected ? 1u : 0u,
                         static_cast<uint8_t>(g_diag.cc));
        }
      }

      if (!attached) {
        /* 拔出后静置 PD_DETACH_HOLD_MS 才允许重挂，进一步压住自激 */
        const bool holdOver = (tDetach == 0u) || ((now - tDetach) >= PD_DETACH_HOLD_MS);
        if (holdOver) {
          /* 建 attach：toggle 用 Rd 探测，TOGSS 落定 = attach */
          const e_FUSB302_CC cc = pd.pollAttach();
          if (cc != e_FUSB302_CC::NONE) {
            g_diag.cc = cc;
            /* ★ attach 时**总是**重钉一次 CC 路由。
             *   旧版"只在 CC 变了才 selectCC"会在"拔出后用同一根 CC 重挂"时跳过，
             *   而 MEAS_CCx 同时是 **RX 选线** —— 不重钉就等于 RX 一直挂在
             *   toggle 交出来的状态上，收不到任何报文。attach 才调一次，
             *   不像每轮都调那样反复复位接收前端。 */
            g_diag.ccSelected = pd.selectCC(cc);
            attached  = true;
            tDetach   = 0u;
            tVbusLow  = 0u;
            srTries   = 0;
            tPhase    = now;
            dirty     = true;
            Log(L_ATTACH, static_cast<uint8_t>(cc), 0);
            Log(L_SELCC,  g_diag.ccSelected ? 1u : 0u, g_diag.specRev);

            /* ★ attach 之后**先短听 250ms**：看源端有没有正在广播。
             *   有 → 直接读并显示；没有 → 由主循环发 Soft Reset 去催。
             *   （开机那一发 Hard Reset 已经提前到 App_Run 开头，见那里的说明；
             *     这里不再发，否则每次 attach 都会重打一枪。） */
            phase = PH_LISTEN;
          }
        }
      } else if (tVbusLow != 0u && (now - tVbusLow) >= PD_DETACH_MS && !inHrRecover) {
        /* VBUS 连续低 PD_DETACH_MS = 真拔出：清状态、重开 toggle 等下次插入
         * （inHrRecover 期间不判 —— 那是我方 Hard Reset 造成的预期 VBUS 低） */
        tVbusLow          = 0u;
        attached          = false;
        g_diag.cc         = e_FUSB302_CC::NONE;
        g_diag.ccSelected = false;
        havePdo           = false;
        g_rdoAck          = false;   /* 拔出 → "申请成功"的结论作废 */
        phase             = PH_LISTEN;
        srTries           = 0;
        tDetach           = now;
        (void)pd.startSinkToggle();
        dirty = true;
        Log(L_DETACH, 0, 0);
      } else if (!havePdo) {
        /* 已 attach 但还没 caps：先听；听不到就催一枪 Soft Reset，再等。 */
        uint32_t win = PD_LISTEN_MS;                          /* 短听：按键后 250ms */
        if (phase == PH_LISTEN_LONG) { win = PD_LISTEN_AFTER_HR_MS; }  /* 刚 Hard Reset */
        if (phase == PH_WAIT_SR)     { win = PD_SR_WAIT_MS; }
        if ((now - tPhase) >= win) {
          if (srTries < PD_SR_MAX) {
            srTries++;
            const bool sent =
                pd.sendControl(LoveFinderLib::FUSB302_Bit::PD_CTRL_SOFT_RESET);
            Log(L_ASK, sent ? 1u : 0u, srTries);
            phase  = PH_WAIT_SR;
            tPhase = now;
            dirty  = true;
          } else {
            phase = PH_GIVEUP;
            dirty = true;
          }
        }
      }
    }


    /* ---------------- 3. VBUS / RX FIFO —— 找 Source_Capabilities ----------------
     *
     * ★ 触发源：中断（irqSet）或心跳，两者取或。
     *   协商期间（还没 PDO）心跳保持 5ms —— 那段时间源端只等 24~30ms 就要
     *   Request，兜底路径必须够快；拿到 PDO 之后没有实时性要求，放宽到
     *   PD_RX_IDLE_MS=100ms。
     *   正常情况下中断总是先到，所以这两个心跳实际上一次都不会触发 ——
     *   用 g_pdRxIrq / g_pdRxHb 两个计数器可以验证这一点。 */
    const uint32_t rxHb = havePdo ? PD_RX_IDLE_MS : PD_RX_POLL_MS;
    if (g_diag.ready && (irqSet || (now - tRx) >= rxHb)) {
      tRx = now;
      if (irqSet) { g_pdRxIrq++; } else { g_pdRxHb++; }

      uint8_t s0 = 0;
      g_diag.busOk = pd.readReg8(LoveFinderLib::FUSB302_Reg::STATUS0, s0);
      if (g_diag.busOk) {
        g_diag.vbusRaw = s0;
        g_diag.vbusOk  = (s0 & LoveFinderLib::FUSB302_Bit::S0_VBUSOK) != 0u;
      }

      /* ★ STATUS1 只读一次，供两个用途。
       *
       * 原来这里读完 STATUS1 存进 g_diag.status1，紧接着又调 pd.rxEmpty()
       * —— 而 rxEmpty() 内部**再读一次 STATUS1**（见 FUSB302.cpp）。
       * 同一个寄存器在同一个 5ms 窗口里被读两遍，纯属浪费；而且两次读之间
       * 状态还可能变，判断依据反而不一致。现在直接用上面这份快照。
       *
       * ★ 最小对照：把 STATUS1 原值也记下来（变化时才记，arg1=0x5A 标记）。
       *   这样日志里能直接看到 RX_EMPTY / TX_EMPTY 的变化过程。 */
      uint8_t    s1r   = 0;
      const bool s1Ok  = pd.readReg8(LoveFinderLib::FUSB302_Reg::STATUS1, s1r);
      if (s1Ok) {
        if (s1r != g_diag.status1) {
          g_diag.status1 = s1r;
          if (g_diag.rxMsgCount < 20u) { Log(L_RXOTHER, s1r, 0x5Au); }
        }
      }

      /* ★ STATUS1 历史采样：看 RX_EMPTY 有没有翻过 0 */
      g_rx_s1 = g_diag.status1;
      g_s1_hist[g_s1_idx & 7u] = g_diag.status1;
      g_s1_idx = static_cast<uint8_t>((g_s1_idx + 1u) & 7u);

      /* RX_EMPTY(bit5)=1 表示 FIFO 空。
       * ⚠️ 读失败时按"空"处理（与 rxEmpty() 原来的行为一致）——
       *    一次 I²C 抖动不该让我们去啃一个可能不完整的 FIFO。 */
      const bool rxEmptyNow =
          (!s1Ok) || ((s1r & LoveFinderLib::FUSB302_Bit::S1_RX_EMPTY) != 0u);
      if (!rxEmptyNow) {
        g_rx_try++;
        /* ★ 传 true：上面那次 STATUS1 读已经确认过 FIFO 非空，而且就在**同一次
         *   循环里、只隔了几十微秒**，中间没有任何别的东西会去弹 FIFO。
         *   让库跳过它内部那次重复的 rxEmpty()（就是那次多余的 0x41 读）。 */
        const bool gotCap = pd.pollSourceCapabilities(msg, true);
        /* ★ 把最近一次从 FIFO 读到的原始字节存进 RAM 快照 */
        for (uint8_t i = 0; i < 32u; ++i) {
          g_rx_raw[i] = (i < msg.rawLen) ? msg.raw[i] : 0u;
        }
        g_rx_len = msg.rawLen;
        g_rx_tok = msg.token;
        g_rx_hdr = msg.header;
        g_rx_ndo = msg.numDataObjects;
        if (msg.rawLen != 0u) { g_rx_ok++; }
        if (gotCap) { g_rx_cap++; }
        if (gotCap) {
          lastMsg            = msg;
          havePdo            = true;
          dirty              = true;
          g_diag.lastHeader  = msg.header;
          g_diag.rxMsgCount++;
          g_diag.specRev     = msg.specRev;
          /* 让 FUSB302 自动 GoodCRC 的报头 specRev 落在硬件支持的范围内。
             FUSB302B 只能 Rev1.0/Rev2.0，源端报 Rev3.0 时 setSpecRev 会钳到
             Rev2.0（详见 FUSB302.hpp 里 SPECREV_* 的说明）。 */
          pd.setSpecRev(msg.specRev);
          Log(L_CAP, msg.pdoCount, msg.specRev);
          Log(L_CAPHDR, static_cast<uint8_t>(msg.header & 0xFFu),
                        static_cast<uint8_t>(msg.header >> 8));
          /* GoodCRC 由 FUSB302 硬件自动回（Control0.AUTO_PRE=0 + Switches1.AUTO_CRC=1）。
             ⚠️ 不要在这里软件补发 —— PD 给 GoodCRC 的窗口只有几十 µs，
                等我们轮询到 caps 再发早就超时了，只会多出一条多余报文。
                （详见上面"关于 GoodCRC"那段说明。） */

          /* ★★★ 收到 Source_Capabilities 后**必须立刻回一条 Request(RDO)** ★★★
           *
           * 源端发完 caps、并收到我们硬件自动回的 GoodCRC 之后，会启动
           * tSenderResponse 定时器（PD 规范 24~30ms）等 SINK 的 Request。
           * **超时没等到 → 源端直接发 Hard Reset，并把 VBUS 拉低重启。**
           *
           * 抓包实测（PD2CAP.csv，同一次运行）：
           *   SRC CAP → SNK GOOD CRC → +28.7ms → HRST
           *   源端重播 caps → 又是 GOOD CRC → 又是 +28.7ms HRST …… 无限循环，
           *   表现就是 VBUS 在 5V / 0V 之间来回跳。
           * I²C 抓包（IICAP2.csv）也印证：整段抓包我们**只发过一条** PD 报文
           *   （1159.4ms 的 Soft Reset，CONTROL0<=0x0D 那一次 TX_START），
           *   之后再没出现过 TX_START —— 从来没回过 RDO。
           *
           * 这里只申请**第 1 个 Fixed 档位（通常就是 5V）**，保持 VBUS 不变，
           * 最省事也最安全；RDO 的电流直接取源端该 PDO 里报的值。
           *
           * ⚠️ 必须赶在 ~24ms 内发出去，所以放在**收到 caps 的同一轮**里，
           *    不能等 UI 刷新那一路。 */
          if (msg.pdoCount > 0u) {
            uint8_t  pos    = 0u;
            uint32_t pdoRaw = 0u;
            for (uint8_t i = 0; i < msg.pdoCount && i < 7u; ++i) {
              if ((msg.pdo[i] >> 30) == 0u) {          /* PDO 类型 0 = Fixed */
                pos    = static_cast<uint8_t>(i + 1u); /* 对象位置从 1 起算 */
                pdoRaw = msg.pdo[i];
                break;
              }
            }
            if (pos == 0u) {                            /* 没有 Fixed 就硬取 #1 */
              pos    = 1u;
              pdoRaw = msg.pdo[0];
            }
            uint16_t cur10 = static_cast<uint16_t>(pdoRaw & 0x3FFu);  /* 10mA 单位 */
            if (cur10 == 0u) { cur10 = 300u; }                        /* 兜底 3A */
            const uint32_t rdo =
                ((static_cast<uint32_t>(pos)   << 28) |               /* 对象位置 */
                 (static_cast<uint32_t>(cur10) << 10) |               /* 工作电流 */
                  static_cast<uint32_t>(cur10));                      /* 最大电流 */
            /* 新一轮协商：先清掉"上次申请成功"的结论和应答记录，
               这样随后读到的 msgType 一定是针对**这一条** RDO 的应答。 */
            g_rdoAck = false;
            pd.clearLastPeerMsgType();

            const bool rdoOk = pd.sendRequest(rdo);
            Log(L_RDO, rdoOk ? 1u : 0u, static_cast<uint8_t>(cur10 & 0xFFu));
            dirty = true;
          }
        }
        /* ★ 这里原本有一句 (void)pd.rxFlush() —— 已删除（2026-10-09）。
         *
         * 原因一（空转）：pollSourceCapabilities() 返回时 FIFO **必然已经空了**。
         *   它内部是个 for(guard<8) 循环，每轮先 rxEmpty() 再 fetchMessage()，
         *   要么弹到空返回 false，要么中途找到 caps 返回 true —— 无论哪条路，
         *   出口时 FIFO 里都不会剩东西。所以这次冲 FIFO 是白写 3 笔 I²C
         *   （读 0x07 + 写 0x07 两次）。IIC3.csv 里 2016.1 ms 和 2055.0 ms
         *   各抓到一次，一共 6 笔无用事务。
         *
         * 原因二（有丢包竞态，更严重）：冲 FIFO 是个**破坏性**动作。万一在
         *   "pollSourceCapabilities 返回"到"rxFlush 写完 CONTROL1"这 ~0.3 ms
         *   窗口里源端又发来一包，它会被连同陈旧字节一起冲掉 —— 而源端广播
         *   Source_Capabilities 正是一组连发，这个窗口踩中的概率并不低。
         *
         * 所以现在"收到非 caps 报文"这条路径**不做任何 FIFO 操作**：
         * 该记的 msgType 已经由库记在 lastPeerMsgType() 里（见下面那段），
         * FIFO 本身也是干净的。 */

        /* ★ 检查对端对我方 Request(RDO) 的应答 —— 决定第 1 条 PDO 要不要高亮。
         *
         * Accept(0x03) / PS_RDY(0x06) / Reject(0x04) 都是**控制报文**，
         * pollSourceCapabilities() 为了找 Source_Capabilities 会把它们取出来丢掉，
         * 所以库把"最后一条被跳过的 msgType"记在 lastPeerMsgType() 里供我们判断：
         *   Accept(0x03) → 只是受理，电源还没切，继续等
         *   PS_RDY(0x06) → 电源已切到申请的档位 = 申请成功 → 反白高亮第 1 条
         *   Reject(0x04) → 被拒 → 不高亮
         *
         * 读到就 clear（"消费"掉），否则同一条应答会在后面每一轮里被反复处理。 */
        if (!gotCap) {
          const uint8_t peer = pd.lastPeerMsgType();
          if (peer != 0u) {
            pd.clearLastPeerMsgType();
            if (peer == LoveFinderLib::FUSB302_Bit::PD_CTRL_PS_RDY) {
              if (!g_rdoAck) { g_rdoAck = true; dirty = true; }
              Log(L_PSRDY, 1, peer);
            } else if (peer == LoveFinderLib::FUSB302_Bit::PD_CTRL_REJECT) {
              if (g_rdoAck) { g_rdoAck = false; dirty = true; }
              Log(L_PSRDY, 0, peer);
            }
          }
        }
      }
    }

    /* ---------------- 3c. 中断闩锁 + ★ Hard Reset 复活 ----------------
     *   Interrupt(0x42) I_CRC_CHK   = 收到一包 CRC 正确的报文  ← 包到达信号
     *                   I_VBUSOK    = VBUS 状态变化
     *   INTERRUPTA.bit0 I_HARDRST   = **收到 Hard Reset ordered set** ← 复活触发点
     *   INTERRUPTA.bit2 I_TXSENT    = 我方报文拿到了对端 GoodCRC
     *   INTERRUPTA.bit4 I_RETRYFAIL = 自动重发 ×3 耗尽，对端没应答
     *   INTERRUPTA.bit6 I_TOGDONE   = toggle 因检测到设备而停止 ← attach 信号
     *   INTERRUPTB.bit0 I_GCRCSENT  = 芯片回过 GoodCRC = 它收到过有效报文
     *   STATUS0A.bit0   HARDRST     = 同上（状态位，纯诊断用）
     *
     * ★ 触发源：中断（irqSet）或 PD_INT_POLL_MS 心跳，两者取或。
     *   0x3E/0x3F 的闩锁**即使被 Mask 屏蔽也照样会置位**（只是不会拉低 INT_N），
     *   所以改成中断驱动之后，这里读到内容的行为和以前完全一致 —— 是纯增量。
     *   ⚠️ 但 0x42 不同：它以前从来没被读过（见下面那段说明）。
     *
     * ★ 为什么这里必须处理 HARDRST：
     *   实测（PD 抓包 + I²C 抓包，同一次运行）——
     *     SINK 发 Soft Reset → 源端回 GoodCRC + ACCEPT×3 → **源端发 Hard Reset**
     *     → VBUS 掉 ~0.9s → 源端重新广播 5 秒 caps。
     *   但 I²C 抓包显示：Hard Reset 之后 Status1.RX_EMPTY **恒为 1**，
     *   一条报文都没进 RX FIFO —— FUSB302 的 PD 收发逻辑锁死了。
     *   不写 Reset.PD_RESET 就永远收不到那 5 秒广播，屏上自然什么都没有。
     *   （Chromium 的 pd_reset()、pdsink 的 hr_cleanup() 都是这么做的。） */
    if (g_diag.ready && (irqSet || (now - tInt) >= PD_INT_POLL_MS)) {
      tInt = now;
      /* ★ 一次 7 字节突发读，替掉原来分 4 次读的 4 条事务（2026-10-09）。
       *
       * 读的是 0x3C~0x42 这**连续 7 个**寄存器。依据：数据手册 p.11 Figure 14
       * 明确支持"从当前寄存器位置连续读"，多字节读时芯片内部地址指针自动 +1。
       *
       *   st[0] 0x3C STATUS0A     st[1] 0x3D STATUS1A
       *   st[2] 0x3E INTERRUPTA   st[3] 0x3F INTERRUPTB
       *   st[4] 0x40 STATUS0      st[5] 0x41 STATUS1
       *   st[6] 0x42 INTERRUPT
       *
       * 收益（IIC3.csv 实测）：这一块原本 181 轮 × 4 条 = 724 条事务，
       * 占全部 980 条事务的 74%。合并后只剩 181 条。
       *
       * 为什么合并是安全的：
       *   · 0x3C/0x3D/0x40/0x41 是**纯只读状态**寄存器，读几次都没有副作用；
       *   · 0x3E/0x3F/0x42 是 R/C（读即清）—— 突发读里每读到一个字节就等于
       *     清掉那个寄存器，这正是我们要的"一次服务把三个闩锁全清干净"。
       *     而且比分 4 次读**更原子**：不会读到一半被 ISR 插进来。
       *   · 顺带拿到的 STATUS0/STATUS1 在这里**不使用**（block 3 有自己
       *     5ms / 100ms 的采样节奏，不能让 20ms 的这里顶替它）。
       *     它们只是搭车，不额外花任何总线时间。 */
      uint8_t st[7] = {0, 0, 0, 0, 0, 0, 0};
      (void)pd.readBytes(LoveFinderLib::FUSB302_Reg::STATUS0A, st, 7u);

      const uint8_t sa = st[0];   /* Status0a  */
      const uint8_t ia = st[2];   /* Interrupta */
      const uint8_t ib = st[3];   /* Interruptb */
      const uint8_t i0 = st[6];   /* Interrupt 0x42 —— 已经在这条突发读里读掉了 */

      /* ★★★ Interrupt(0x42) 必须读 —— 这一步以前是**漏掉的**，而且后果很重 ★★★
       *
       * 0x42 / 0x3E / 0x3F 是三个**互相独立**的 R/C 寄存器，INT_N 是它们
       * 按 Mask 门控之后的"或"：
       *
       *     (0x42 & ~Mask1) | (0x3E & ~Maska) | (0x3F & ~Maskb)  ≠ 0 → INT_N 低
       *
       * 而 Mask1 里 M_CRC_CHK 是放开的，也就是说 **I_CRC_CHK(0x42.bit4)
       * 能拉低 INT_N**。可旧代码只读 0x3E/0x3F，从来不读 0x42 ——
       * 于是第一包有效报文一到，0x42 置位、INT_N 拉低，然后**永远清不掉**，
       * 引脚就永久趴在低电平上。表现在中断脚上就是"只来一次边沿，之后静默"。
       *
       * 所以读 0x42 不是"顺便多读一个寄存器"，它是整条中断通路能不能用的前提。
       * 改动 FUSB302 的 Mask 值时必须回头确认这里仍然把三个寄存器都读了。
       *
       * ★ 2026-10-09：这一句 readReg8(INTERRUPT) 已经**并进上面那条 7 字节
       *   突发读**（st[6]），不再单独发一条事务。判据完全没变 —— 三个 R/C
       *   寄存器照样被读掉，只是从 4 条事务变成 1 条。 */

      g_diag.status0a = sa;
      g_pdIntReg      = i0;      /* 快照，供 OpenOCD 看 0x42 的真实内容 */
      /* ★ 2026-10-09：这里原来各有一句 dirty = true，已删除。
       *
       * 理由：INTERRUPTA / INTERRUPTB 的内容**一个字都不上屏**。
       *   屏上只有 5 个量：视图（havePdo / PH_GIVEUP）、g_diag.cc、g_uiPhase、
       *   g_uiSrTries、g_rdoAck、以及 PDO 内容 —— 全部由 UiFp 指纹覆盖，
       *   而 I_TOGDONE / I_TXSENT / I_GCRCSENT / I_RETRYFAIL 这些闩锁
       *   一个都不在这 5 个量的因果链上。
       *
       * 而这两个位恰恰**置得非常密**：源端每发一条报文、我们每回一次
       * GoodCRC，INTERRUPTB.bit0（I_GCRCSENT）就置一次。旧版于是"每收到
       * 一包就整屏重画一次 35ms"，白烧 SPI 还在协商窗口里顶掉 Request。
       * （iicnew.csv 里 135.3ms / 207.9ms 那两次 37~43ms 的总线静默就是它。）
       *
       * 顺带一提：删掉它还顺手修了一个 UI 迟滞 —— 每次 dirty 都会把 tUi
       * 推到当前时刻，于是真正的状态变化会被 PD_UI_MIN_MS 的 80ms 下限挡住。
       *
       * 状态变化本身**不会丢**：phase / srTries / rdoAck 一变，UiFp 指纹就变，
       * 下一轮照样重画（下面 4c 那段）。 */
      if (ia != 0u) {
        g_diag.intaAcc |= ia;
        Log(L_INTA, ia, 0);
      }
      if (ib != 0u) {
        g_diag.intbAcc |= ib;
        Log(L_INTB, ib, 0);
      }

      /* 0x42 的记录策略：**先把 I_CRC_CHK 剔掉**再入日志。
       *   它每收到一包有效报文就置位一次，日志环形区只有 64 条，
       *   记它会把开机/协商那段真正有用的记录全冲掉。
       *   剔掉之后剩下的（I_VBUSOK / I_ALERT …）都是低频事件，值得留痕。
       *   另外这一块**不置 dirty** —— 0x42 里没有影响屏上内容的事件。 */
      const uint8_t i0log = static_cast<uint8_t>(
          i0 & static_cast<uint8_t>(~LoveFinderLib::FUSB302_Bit::I_CRC_CHK));
      if (i0log != 0u) {
        g_diag.int0Acc |= i0log;
        Log(L_INT0, i0log, 0);
      }

      /* ★ 收到 Hard Reset → 必须写 Reset.PD_RESET 复活 FUSB302。
       *
       * 用 INTERRUPTA.bit0（I_HARDRST）而不是 Status0a.HARDRST：
       * 前者是 R/C（读一次自动清零）→ **天然就是边沿检测**，
       * 不会出现"状态位一直挂着、每轮都重复复位 FIFO"的副作用。
       * 实测这块芯片即使 MASKA.bit0=1（屏蔽 INT 脚）闩锁照样会置位
       * （抓包 4000.484ms 读到 INTERRUPTA=0x01），所以光靠轮询也能拿到 ——
       * 这保证了从"轮询"切到"中断驱动"时这里的判据完全没变。
       *
       * ★ 现在 MASKA.bit0 已经放开，所以这条还会**立刻**把 INT_N 拉低、
       *   进 ISR，recover() 从"最坏晚 20ms"变成"晚 ~100µs"。
       *   对源端 Hard Reset 后那 ~1s 才开始的重播来说，这个提前量很实在。
       *
       * Status0a.HARDRST 仍然每轮读进 g_diag.status0a / g_snap[27] 作诊断。 */
      if (attached && ((ia & LoveFinderLib::FUSB302_Bit::IA_HARDRST) != 0u)) {
        Log(L_HRST, 1, 0);
        g_diag.ccSelected = pd.recover(g_diag.cc);
        if (!havePdo) {
          srTries = 0;
          phase   = PH_LISTEN_LONG;   /* 源端 ~1s 后才重播，安静等 */
          tPhase  = now;
        }
        dirty = true;
        Log(L_RECOVER, g_diag.ccSelected ? 1u : 0u,
                       static_cast<uint8_t>(g_diag.cc));
      }
    }


    /* ---------------- 4. 界面刷新 + 诊断快照 ----------------
     *
     * ★ 2026-10-09 改动：从"每 80ms 无条件重画"改成"内容真的变了才重画"。
     *
     * 【旧行为的问题】
     *   旧条件 `dirty || (now - tUi) >= PD_UI_MIN_MS` 里那个周期性项，
     *   完全不管屏上内容有没有变，每 80ms 就把整屏重画一遍：
     *     HeaderBar（160×10 实心填充）+ 8 行 × 160 像素 ≈ 25.6KB SPI。
     *   IIC3.csv 抓包实测到的后果：拿到 PDO 之后空闲期的 RX 心跳间隔被拉成
     *     ~99.87 / 100.47 / 121.18ms（周期 321ms ≈ 4×80ms）——
     *   每 4 个心跳就有 1 个被那次"无条件重画"顶掉。
     *   而这段时间屏上内容**恒定不变**（PDO 列表 + 高亮），
     *   画 100 次和画 1 次的结果完全一样，纯浪费。
     *
     * 【新行为：指纹比对】
     *   把三个画图函数的**全部输入**打包成 UiFp（定义见 App_Run 上方），
     *   与"上一帧画的是什么"（uiFpLast）逐字段比较：
     *     视图选择 : havePdo / (phase == PH_GIVEUP) / 其余 → 状态页
     *     状态页   : g_diag.cc、g_uiSrTries、g_uiPhase
     *     PDO 页   : lastMsg.pdoCount、lastMsg.pdo[0..n-1]、g_rdoAck
     *   指纹一致且没人置 dirty → 这一轮**一条 SPI 都不发**。
     *
     * 【dirty 的角色变了，但一个都没少】
     *   dirty 现在是"强制重画"开关。各事件点照旧置位（init 成功/失败、
     *   attach、拔出、VBUS 掉过又回来、收到 caps、发出 RDO、PS_RDY、REJECT、
     *   发 Soft Reset、GIVEUP、Hard Reset 恢复、中断闩锁有内容）——
     *   也就是说**事件驱动的刷新一条都没被砍掉**，砍掉的只是"没事也画"。
     *   指纹在这里是**冗余的安全网**：万一以后新加了一处会改屏上内容、
     *   却忘了置 dirty 的地方，指纹会替它兜住（内容一变，指纹就变）。
     *
     * 【为什么还留 80ms 下限】
     *   指纹只回答"要不要画"，不回答"能不能立刻画"。协商期间 dirty 会被
     *   连发报文置得很密（每条报文都可能置一次），有下限才不会把主循环
     *   刷成"只顾画图、顾不上收报文"。
     *   ⚠️ 被下限挡下时**不更新 uiFpLast、也不清 dirty**，所以下一轮只要
     *      过了下限就一定会补画 —— 不会丢帧，只会晚几十毫秒。
     *
     * 【看门狗 PD_UI_WATCHDOG_MS】
     *   纯"指纹 + dirty"有个死角：万一 SPI/DMA 那一路偶发抽风把某行画坏了，
     *   而之后内容再没变过，那行就永远错着（旧版靠 80ms 重画自愈）。
     *   所以再留一条慢速兜底 —— 但**一次只刷一行**（轮转 9 个槽位），
     *   不是整屏重画：整屏 35ms 正好能吃掉源端等 Request 的那 24~30ms 窗口。
     *   详见上面 g_rcTxt 那段和下面 4c 里 InvalidateRow() 的注释。 */

    /* ---- 4a. 诊断快照 —— 每轮都写 ----
     * ★ 已从"重画条件"里**移出来**。理由：
     *   ① 成本可忽略：几十字节 RAM 写入 + 一个 now 时间戳；
     *   ② 它本来就是给 OpenOCD 看的**实时体检表**（mdw &g_snap 8），
     *      尤其 [30]/[31] 那两个中断/心跳计数器 —— 判"中断到底有没有在
     *      干活"全靠它们。要是跟着重画一起变成"几十秒才更新一次"，
     *      这套诊断就废了。 */
    g_uiPhase   = phase;
    g_uiSrTries = srTries;

    /* 快照（OpenOCD: mdw &g_snap 6）—— 屏幕看不出来时靠它 */
    g_snap[0]  = g_diag.tried     ? 1u : 0u;
    g_snap[1]  = g_diag.ready     ? 1u : 0u;
    g_snap[2]  = g_diag.idValid   ? 1u : 0u;
    g_snap[3]  = g_diag.devId;
    g_snap[4]  = g_diag.toggleOn  ? 1u : 0u;
    g_snap[5]  = g_diag.busOk     ? 1u : 0u;
    g_snap[6]  = g_diag.vbusOk    ? 1u : 0u;
    g_snap[7]  = g_diag.vbusRaw;
    g_snap[8]  = static_cast<uint8_t>(g_diag.cc);
    g_snap[9]  = g_diag.ccSelected ? 1u : 0u;
    g_snap[10] = g_diag.rxMsgCount;
    g_snap[11] = static_cast<uint8_t>(g_diag.lastHeader & 0xFFu);
    g_snap[12] = static_cast<uint8_t>((g_diag.lastHeader >> 8) & 0xFFu);
    g_snap[13] = static_cast<uint8_t>(g_diag.i2cErr & 0xFFu);
    g_snap[14] = attached ? 1u : 0u;
    g_snap[15] = havePdo  ? 1u : 0u;
    g_snap[16] = static_cast<uint8_t>(now & 0xFFu);
    g_snap[17] = static_cast<uint8_t>((now >> 8) & 0xFFu);
    g_snap[18] = static_cast<uint8_t>((now >> 16) & 0xFFu);
    g_snap[19] = static_cast<uint8_t>((now >> 24) & 0xFFu);
    g_snap[20] = g_rdoAck ? 1u : 0u;  /* 原为"心跳计数"（心跳方块已删）→ 改记 PS_RDY 是否到 */
    g_snap[21] = g_diag.specRev;
    g_snap[22] = g_uiSrTries;        /* 已发 Soft Reset 次数 */
    g_snap[23] = static_cast<uint8_t>(pd.txCount() & 0xFFu);
    g_snap[24] = g_diag.intaAcc;     /* bit2=TXSENT bit4=RETRYFAIL */
    g_snap[25] = g_diag.intbAcc;     /* bit0=GCRCSENT */
    g_snap[26] = g_diag.status1;     /* bit5=RX空 bit3=TX空 */
    g_snap[27] = g_diag.status0a;    /* bit0=HARDRST */
    g_snap[28] = g_uiPhase;
    g_snap[29] = g_diag.status0a & 0x01u;   /* Status0a.HARDRST（诊断） */
    /* ★ 中断通路的"体检"两字节（读法：mdw &g_snap 8）——
     *   [30] 中断进入次数低字节（滚动）
     *   [31] 心跳顶事次数低字节
     * 判读：正常情况 [31] 应该一直是 0（中断先到，心跳根本轮不上）。
     *       若 [31] 跟着 [30] 一起涨 → 说明边沿没来，心跳在硬扛，
     *       那就回头查 PA0 上拉 / EXTI 触发沿 / 0x42 有没有被读。 */
    g_snap[30] = static_cast<uint8_t>(g_pdIrqCnt & 0xFFu);
    g_snap[31] = static_cast<uint8_t>(g_pdRxHb  & 0xFFu);

    /* ---- 4b. 算"这一轮屏上应该是什么"的指纹 ---- */
    UiFp fp = {};
    fp.view = havePdo ? 1u : ((phase == PH_GIVEUP) ? 2u : 0u);
    fp.cc      = static_cast<uint8_t>(g_diag.cc);
    fp.phase   = g_uiPhase;
    fp.srTries = g_uiSrTries;
    fp.rdoAck  = g_rdoAck ? 1u : 0u;
    if (havePdo) {
      /* 只比前 7 个 —— FUSB302_Message::pdo 上限就是 7 个数据对象，
       * DrawPdoList() 的循环也止于 ROWS/7，两边必须一致。 */
      const uint8_t n = (lastMsg.pdoCount < 7u) ? lastMsg.pdoCount : 7u;
      fp.pdoCount = n;
      for (uint8_t i = 0; i < n; ++i) { fp.pdo[i] = lastMsg.pdo[i]; }
    }

    /* ---- 4c. 逐字段比对 → 决定这一轮画不画 ---- */
    bool changed = dirty || !uiFpValid ||
                   (fp.view     != uiFpLast.view)    ||
                   (fp.cc       != uiFpLast.cc)      ||
                   (fp.phase    != uiFpLast.phase)   ||
                   (fp.srTries  != uiFpLast.srTries) ||
                   (fp.rdoAck   != uiFpLast.rdoAck)  ||
                   (fp.pdoCount != uiFpLast.pdoCount);
    if (!changed) {
      /* pdo[] 单独用循环比：手写 7 个 != 太容易漏项/写错下标 */
      for (uint8_t i = 0; i < 7u; ++i) {
        if (fp.pdo[i] != uiFpLast.pdo[i]) { changed = true; break; }
      }
    }

    const bool minGapOk = (now - tUi) >= PD_UI_MIN_MS;
    const bool watchdog = (now - tUi) >= PD_UI_WATCHDOG_MS;

    if ((changed && minGapOk) || watchdog) {
      tUi       = now;
      dirty     = false;
      uiFpLast  = fp;          /* 记下"这一帧画的是什么"，供下一轮比对 */
      uiFpValid = true;

      /* ★ 看门狗只作废**一行**（轮转 0..ROWS，含标题条槽位）。
       *
       * 为什么不能一次全作废：那样又变成 35ms 的整屏重画 —— 正是刚修好的
       * 那个坑（源端 tSenderResponse 只有 24~30ms）。轮转之后每一次重画都
       * 被限死在 1 行 ≈ 4ms，看门狗在任何时刻触发都不会再顶掉一次 Request。
       * 整屏轮一遍 = (ROWS+1) × PD_UI_WATCHDOG_MS = 9 × 500ms = 4.5s。 */
      if (watchdog) {
        InvalidateRow(g_rcWatchRow);
        g_rcWatchRow = static_cast<uint8_t>((g_rcWatchRow + 1u) % (ROWS + 1u));
      }

      if (havePdo) {
        DrawPdoList(lastMsg);
      } else if (phase == PH_GIVEUP) {
        /* 终态：催满 Soft Reset 还是没等到 caps → "0 PDO"
         * （这样"按下按键后屏上要么 0 PDO、要么 PDO 列表"只有两个出口） */
        DrawNoPdo();
      } else {
        DrawStatus();
      }
    }

    /* ---------------- 5. ★ 重新武装：把"服务期间漏掉的边沿"补回来 ------------
     *
     * 为什么必须要有这一步：INT_N 是**电平型闩锁**，不是脉冲。芯片一旦把
     * 引脚拉低，就会一直保持低，直到所有被放开的中断位都被读走。
     * 于是服务期间如果又来了新报文 —— 引脚**本来就是低的** —— 不会产生
     * 新的下降沿，EXTI 也就不会再进 ISR。那一包就只能烂在 FIFO 里等心跳。
     *
     * 而源端广播 Source_Capabilities 时正是**一组 3 条连发**（实测组内间隔
     * ~2ms），几乎必然踩到这个窗口。所以这一步不是"锦上添花"，是必需品 ——
     * 少了它，"先听后催"里那个"听"会随机丢包。
     *
     * 做法：服务完直接回读 PA0 电平。还是低，就说明闩锁没清干净、或者又来了
     * 新的，把标志再立一次，下一轮立刻再服务一遍。
     *
     * ⚠️ 会不会死循环？
     *   不会。被放开的位一共 7 个，**全部**落在主循环会读的那三个寄存器里
     *   （0x42 的 bit4/bit7、0x3E 的 bit0/2/3/4/6 —— 见 FUSB302.hpp 里的
     *   覆盖性自检），所以一次完整服务之后引脚必然抬回高电平。
     *   真出现"引脚恒低"，最坏也只是退化成忙轮询 —— 而改动前那套主循环
     *   本来就是**无延时死循环**，所以不会比原来更糟，更不会卡死状态机。
     *   g_pdIrqRearm 计数器会把这个情况暴露出来。 */
    if (g_diag.ready &&
        (LL_GPIO_IsInputPinSet(FUSB_INT_Port, FUSB_INT_Pin) == 0u)) {
      if (g_pdIrq == 0u) { g_pdIrqRearm++; }
      g_pdIrq = 1u;
    }
  }
}

} // namespace

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{
  /* USER CODE BEGIN 1 */
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/
  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  LL_APB1_GRP2_EnableClock(LL_APB1_GRP2_PERIPH_SYSCFG);
  LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_PWR);
  LL_PWR_EnableBkUpAccess();
  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock (HSI 24 MHz) and the 1 ms SysTick time base */
  Studio_RCC_Init();
  /* USER CODE BEGIN SysInit */
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals
     （GPIO / I2C1 / SPI1 / TIM3 / CRC）
     SPI1_TX 的 DMA 配置在 Studio_SPI1_Init() 内部完成。

     ⚠️ 本工程**不使用串口**：有意不调用 Studio_USART1_Init()，
        PDO 列表全部显示在板载 160x80 屏上。 */
  Studio_GPIO_Init();
  Studio_I2C1_Init();
  Studio_SPI1_Init();
  Studio_TIM3_Init();
  Studio_CRC_Init();
  /* USER CODE BEGIN 2 */
  /* USER CODE END 2 */

  /* USER CODE BEGIN WHILE */
  /* ⚠️ 屏驱初始化刻意搬进了 App_Run()，排在 FUSB302 配置【之后】。
   *
   * 原因：本板的 3V3 和板载 DAPLink 都由 FUSB302 诱骗出来的 VBUS 供电。
   * Source 收不到 GoodCRC 就会 Hard Reset 撤 VBUS，整块板子（含调试器）
   * 一起断电。所以开机路径上不能有任何"先画个开机动画"的延时 ——
   * PD 必须先上线。
   *
   * 注意：不返回，所以不要指望 main 之后的代码。 */
  App_Run();
  /* USER CODE END WHILE */

  /* USER CODE BEGIN 3 */
  /* USER CODE END 3 */
}

/**
  * @brief  Return the 1 ms tick counter maintained by SysTick_Handler().
  * @note   LL_mDelay() only polls the SysTick COUNTFLAG and keeps no counter,
  *         so this is the millisecond time base for application code.
  * @retval Milliseconds since reset
  */
uint32_t BSP_GetTick() noexcept
{
  return uwTick;
}

/* USER CODE BEGIN 4 */
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
[[noreturn]] void Error_Handler()
{
  /* USER CODE BEGIN Error_Handler_Debug */
  __disable_irq();
  while (1) {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  (void)file;
  (void)line;
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
