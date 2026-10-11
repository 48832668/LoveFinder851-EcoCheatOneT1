/**
  ******************************************************************************
  * @file    app.cpp
  * @brief   PD_PPS_Req —??FUSB302 ??SINK「PD 3.0 PPS 诱骗?? 上下双进度条 UI
  *
  * ============================================================================
  *  这个例程做什??  * ============================================================================
  *  1. 开机：点亮????配好 FUSB302 ??toggle 探出源端挂在哪根 CC ??钉死 CC 路由
  *     ??补一??PD **Soft Reset** 清掉上一轮电源合????收到 Source_Capabilities
  *     ??**同一轮内**??caps 里找 **PPS APDO** ????APDO 量程取默??5.2V / 2A
  *     ??立刻回一??**Programmable RDO**（Request）→ ACCEPT ??PS_RDY??  *  2. 谈成后屏上画**上下两条 38px 大带**??  *        · 上带 = 电压：量??= APDO ??minV..maxV，步??20mV
  *        · 下带 = 电流：量??= 0..maxI??    步进 50mA
  *     每带布局??*左侧 70px 进度??* + **右侧大号实时数??*（scale2），
  *              数??*正下??*是该进度条的**上限**??  *  3. 按键（板上单键）??  *        · **单击** = 切换选中带（选中带提亮底??+ 高亮边框）；
  *        · **长按 ??00ms** = 判定成立??*立即步进一??*，之??*??200ms 再步进一??*??  *        · 步进到顶**回绕到最小??*??  *     每次改值都**重发一??Programmable RDO** —??PPS ??可编程电????  *     改电??电流本身就是"再发一??Request ??ACCEPT ??PS_RDY"??  *
  *  ==========================================================================
  *  ★★ ??PD_PDO_FIX_Req 的关??★★
  *  ==========================================================================
  *  本文??*整体沿用** FIX 工程那套已经上板跑通的骨架（开机流程、AUTO_CRC 常开??  *  同轮??Request、MessageID 不因 Soft Reset 归零、CC 终结拔出快通道、事件日志环??  *  OpenOCD 快照）??*唯一改动的两??*??  *      ??**挑档??*：FIX ????5V 最近的 Fixed PDO"，这里改????PPS APDO"??  *         并把 Request ??Fixed RDO 换成 **Programmable RDO**??  *      ??**界面**：从"6 ??PDO 文本列表"换成"上下两条进度条大????  *  其余时序铁律一条都没变，改动风险因此被压到最小??  *
  *  ==========================================================================
  *  ★★ 四条最容易写错的铁律（PD_PDO_List_Det / FIX 上全踩过）★??  *  ==========================================================================
  *  ??**Fixed RDO 里没??电压"字段**（只能表??我要第几??PDO"）；??**PPS ??  *     Programmable RDO ??* —??它直接带 20mV/bit 的电压和 50mA/bit 的电流??  *     所??PPS 这边"选档"只是??*哪个 APDO**（Object Position），
  *     真正的电??电流写进 RDO 的位域里。见 buildPpsRdo()??  *
  *  ??**收到 caps 之后必须??24ms 内回 Request**。源端发??Source_Capabilities
  *     并拿到我们回??GoodCRC 后，会启??tSenderResponse 定时器（规范 24~30ms??  *     实测 SW3526S ??8.7ms）；超时没等??Request ??源端直接 Hard Reset??  *     ??所??Request 必须??*收到 caps 的同一??*里发出去（见主循??3 号块 +
  *       5.5 号块），绝不能等 UI 刷新那一路??  *
  *  ??**确认 ACCEPT 必须用硬件（AUTO_CRC??*。软??sendGoodCrc ??0.6~0.7ms??  *     远超规范 tTransmit(??95µs)；实测源端把它当"没收????重发 ACCEPT×3
  *     ??Hard Reset。只有硬件自??GoodCRC 做得到。本工程 AUTO_CRC **常开**??  *
  *  ??**Soft Reset 不重??MessageID；Hard Reset 才重??*。发??SR 千万别调
  *     `pd.resetMsgId()` —??那会让随后的 Request ??msgId 0，而源端期望的??  *     上一??+1 ??判乱????**不回 GoodCRC**（库??AUTO_RETRY 关，中断??  *     一条线索都没有），源端静等超时??Hard Reset。实测证据见 FIX ??app.cpp??  *
  *  ==========================================================================
  *  ??FUSB302B ??PD2.0 PHY，PPS ??PD3.0 特??—??这条到底成不成立??  *  ==========================================================================
  *  成立的部分：FUSB302B ??`Switches1.SPECREV`(bits[6:5]) 数据手册只允??  *      `00`=Rev1.0 / `01`=Rev2.0，`10`/`11` 明写 "Do Not Use"??  *  **不成立的部分**：那??*只管芯片自动回的 GoodCRC 报头**（手册原文：
  *     "Bit used for constructing the GoodCRC acknowledge packet"），
  *     **不参与我们自己写??TX FIFO 的报??* —??我们自己组的报头原样发出??  *  所??SINK 完全可以按源端的 Rev3.0 组包。共享库已经为此把两个值分开存：
  *        m_rev    ??Switches1.SPECREV，钳??0..1（喂自动 GoodCRC??  *        m_hdrRev ??TX 报头 SpecRev，允??0..2（喂我们发出去的报文??  *      `pd.setSpecRev(msg.specRev)` ??*同时**更新这两个（??FUSB302.cpp）??  *  因此本工程只要在收到 caps 后照??FIX 那句 `pd.setSpecRev(msg.specRev)`??  *  随后 sendRequest() 出去??Request 报头自然就是 Rev3.0 —??PPS 可发??  * ============================================================================
  */

/* Includes ------------------------------------------------------------------*/
#include "main.hpp"      /* 桥接：PY32Studio ??main.h + irq_flags.h（BSP_GetTick??*/
#include "gpio.hpp"      /* 桥接：LCD_* / KEY_INT_* 板级引脚??*/
#include "app.h"         /* extern "C" void App_Run(void) —????main.c 调用 */
#include "st7735.hpp"    /* 共享 C++ 屏驱库：DMA + Font_7x10 */
#include "FUSB302.hpp"   /* 共享 C++ PD ??*/
#include <cstdio>        /* snprintf */
#include <cstring>       /* strcmp / strlen */
#include <cstddef>       /* size_t */

/* Studio_I2C1_Init()：I²C 总线自救后把引脚/外设重新配好（C 函数，显??extern "C"??*/
extern "C" {
#include "i2c.h"
}

/* Private define ------------------------------------------------------------*/
/* ---- FUSB302 配置重试 ---- */
#define PD_CFG_RETRY_MS       500u  /* init 失败重试间隔 */
/* 开机探 attach（toggle）的最长等待。开机那??Soft Reset 必须发到源端真正
 * 挂着的那??CC 上，所以要??toggle 探一下；没插线时也不阻塞??*/
#define PD_BOOT_ATTACH_MS     800u

/* ---- 心跳兜底（★ 不是轮询周期??---
 *
 * 三条 EXTI 都会在事件一到就置标志，主循环当轮就服务，实际延??~100µs?? * 心跳??*兜底**：INT_N 是电平型闩锁，边沿有可能整体丢失（上拉没贴、走线问题?? * 芯片异常）。没有心跳，程序会安静地卡死；有心跳只是"慢一点但还活着"?? *
 * ⚠️ 协商期间（还没拿??caps）的心跳必须明显小于 24ms —??这里 5ms??*/
#define PD_ATTACH_POLL_MS      10u  /* attach(TOGSS) 兜底心跳 */
#define PD_RX_POLL_MS           5u  /* RX 兜底心跳：协商中 */
#define PD_RX_IDLE_MS         100u  /* RX 兜底心跳：已谈成 / 已放??*/
#define PD_INT_POLL_MS         20u  /* 中断闩锁兜底心跳 */

/* ---- 屏刷新节??---- */
/* 一??整屏重画"= 两块 160×38 的带 ??30ms；单????15ms?? * 这里限??80ms ??*限??*不是周期：步进（长按 200ms 一拍）绝不会被它吃掉?? * ??已删??500ms 看门狗：它会??内容没变"时也周期重画一条带 —???? *   "不变就不??冲突。现在只??scrKey 变化或按带值真的变了才会写屏??*/
#define PD_UI_MIN_MS           80u
/* ??应答敏感期长度：attach 后这么久之内（还在等 caps）绝不做整屏重画 —?? *   一次整????7ms，会吃掉源端 24~30ms ??caps 应答窗口 ??Hard Reset?? *   超过这个时长说明源端根本不给 caps，那时才允许??WAIT CAPS"画出来??*/
#define PD_UI_CAPS_HOLD_MS   1500u

/* ---- 拔出 / 恢复 ---- */
/* ⚠️ 不能太短：源端响??Soft Reset 时会主动??VBUS 拉低??0.9s，短去抖?? *    把它误判??拔出"??*/
#define PD_DETACH_MS         1500u
/* ??CC 终结丢失 ??快速判拔出??*快通道**）?? *   实测：拔 SRC ??CC 终结??2025ms 就没了（STATUS0.BC_LVL 3??），??VBUS
 *   直到 3450ms 才掉（源/漏两侧体电容??VBUS 拖住 ~1.4s）。再叠上
 *   PD_DETACH_MS ??VBUS 去抖，屏要等??~4.9s 才更新（晚了 2.9s）?? *   去抖??**20ms**?? 2 ??10ms 采样）：误判面已被四道守卫收干净
 *   （ccSeenOk + vbusOk + ACTIVITY==0 + 去抖），而屏刷新本身??80ms 限速，
 *   检测只??< 80ms 就不会成为瓶颈??*/
#define PD_DETACH_CC_MS        20u
#define PD_DETACH_HOLD_MS     300u  /* 判拔出后静置这么久才允许重挂 */
#define PD_HR_RECOVER_MS     2500u  /* 我方主动 Hard Reset 后的恢复窗口 */
#define PD_RECOVER_RETRY_MS   300u  /* Hard Reset ??recover() 的重复调用间??*/
#define PD_HR_RECOVER_MAX       3u  /* 恢复窗口??recover() 最多补几次（含首调之后??*/

/* ---- 久等不到 caps ??主动叫醒源端 ---- */
#define PD_SOFTRST_FIRST_MS   250u  /* attach 后先短听这么久：源端在播就直接读 */
#define PD_SOFTRST_RETRY_MS  3000u  /* 之后每隔这么久补一??Soft Reset */
#define PD_SOFTRST_MAX          3u  /* 最多催这么多枪；还不来????PD 源端"处理 */

/* ---- 按键 ---- */
/* 机械按键抖动典型 1~20ms??~10 个边沿），去抖窗口取 5ms 足够压住接触抖动??*/
/* 按键手势时序（与参考工程 PPS_Req/pps_key.cpp 一致） */
#define KEY_SAMPLE_MS          10u  /* 电平采样节拍 */
#define KEY_DEBOUNCE_MS        30u  /* 去抖 */
#define KEY_LONG_MS           200u  /* 长按判定 / 连发周期 */
#define KEY_REPEAT_MS         200u  /* 长按连发间隔 */
#define KEY_DOUBLE_MS         400u  /* 双击窗口 */
/* ??长按门限 = 重复周期。按住到 200ms 判定成立 ??立即步进一次；
 *   之后**??200ms** 再步进一次（??UI_exple.html ??PRESS_MS 一致）?? *   松手时若还没??200ms ????*单击**（切换选中带）??*/
#define PPS_PRESS_MS          200u

/* ---- PPS 取??---- */
#define PPS_STEP_MV            20u  /* 电压步进（PD 3.0 规定 PPS 电压分辨??20mV??*/
#define PPS_STEP_MA            50u  /* 电流步进（PD 3.0 规定 PPS 电流分辨??50mA??*/
#define PPS_DEF_MV           5200u  /* PPS 出现后的默认电压 */
#define PPS_DEF_MA           2000u  /* PPS 出现后的默认电流 */
/* 发出 Request 之后等应答的上限。超时就??在??标记清掉，允许重??—?? * 否则一次丢包会把后续所有步进全部憋死??*/
#define PPS_RDO_TIMEOUT_MS    600u

/* ★★ PPS 保活（心跳）周期 ★★
 *
 * 【为什么必须有 —??上板实测?? *   合约生效（收??PS_RDY）之后，如果 SINK **什么都不再??*，源端（SW3526S?? *   会在 **~13.2s** 后自己发一??Hard Reset?? *       60s 观测??4 次，间隔 13218 / 13219 / 13222 ms —??极其规律?? *       HRST ??VBUS ??~0.9s，然后源端重??caps，我们又重谈一遍（每轮??1.2s）?? *   对照实验：同一块源端、同一个骨架跑 **Fixed PDO** 合约 50s，`hrTries` ??0?? *   一??HRST 都没有。也就是说这个看门狗**只在 PPS 合约下存??*?? *   业主在逻辑分析仪上的观察与之吻合：真正??PPS 会话??CC 总线上有一?? *   **周期性的通信（心跳）**，而我们的会话??PS_RDY 之后彻底安静 —?? *   源端屏幕上也就一直显??"PD" 而进不了 "PPS"?? *
 * 【结论】源端要 SINK **周期性重??*同一??Programmable RDO ??按住"合约?? *   1s 远小??13.2s 的看门狗（约 13 倍余量），既??心跳"又不至于把总线刷爆?? *   ⚠️ 若现场发现源端对重复 Request 敏感（例??VBUS 抖动），把这里调?? *      ??~8s 仍安全）；若发现源端要求更勤，再往小调??*/
#define PPS_KEEPALIVE_MS     1000u

/* Private typedef -----------------------------------------------------------*/
/* 协商状态（屏上/日志用） */
enum e_St : uint8_t {
  ST_WAIT_ATTACH = 0,  /* 还没探到源端 */
  ST_WAIT_CAP    = 1,  /* ??attach，正在听 Source_Capabilities */
  ST_REQ_SENT    = 2,  /* 已发??Programmable RDO，等应答 */
  ST_ACCEPTED    = 3,  /* 收到 ACCEPT，等 PS_RDY */
  ST_PSRDY       = 4,  /* ??收到 PS_RDY = PPS 合约生效（该电压/电流已上线） */
  ST_REJECTED    = 5,  /* 收到 REJECT / WAIT */
  ST_NO_PPS      = 6,  /* 源端 caps 里没??PPS APDO */
};

/* 按键事件（KeyService 的返回值） */
enum e_Key : uint8_t {
  KEY_NONE   = 0,
  KEY_SELECT = 1,  /* 单击：切换选中行（电压 ↔ 电流） */
  KEY_STEP   = 2,  /* 长按：步进一次（首拍 200ms，之后每 200ms 一拍） */
  KEY_CYCLE  = 3,  /* 双击：轮换选中行的步长 */
};

/* 事件日志（环形，固定 8 字节/条）—????OpenOCD 直接?? *   [0..3] t，毫秒，小端??*相对开??*（g_log_t0?? *   [4]    事件码（??e_Log?? [5] arg0  [6] arg1  [7] 备用
 * 读法：条??= g_log_head，第 n 条在 g_log + (n & 63) * 8??*/
enum e_Log : uint8_t {
  L_BOOT    = 1,   /* 进入 App_Run */
  L_LCD     = 2,   /* 屏驱初始化完??*/
  L_INIT    = 3,   /* FUSB302 init，arg0=ok */
  L_DEVID   = 4,   /* arg0=DEVICE_ID */
  L_TOGGLE  = 5,   /* startSinkToggle，arg0=ok */
  L_ATTACH  = 6,   /* arg0=cc(1/2)??=没探??*/
  L_SELCC   = 7,   /* selectCC，arg0=ok，arg1=specRev */
  L_CAP     = 8,   /* 收到 Source_Cap，arg0=PDO 数，arg1=specRev */
  L_RDO     = 9,   /* ??Request，arg0=ok，arg1=Object Position */
  L_TGT     = 10,  /* arg0=目标 mV 低字节，arg1=选中??Object Position */
  L_PSRDY   = 11,  /* arg0=1 PS_RDY / 0 REJECT，arg1=实际 msgType */
  L_ACCEPT  = 12,  /* 收到 Accept（arg1=msgType??*/
  L_HRST    = 13,  /* 收到对端 Hard Reset */
  L_HRSND   = 14,  /* 我方主动??Hard Reset（长按），arg0=ok */
  L_RECOVER = 15,  /* pd.recover()，arg0=ok，arg1=CC */
  L_DETACH  = 16,  /* 判拔出。arg0: 1 = CC 终结丢失**快通道** / 0 = VBUS 慢通道 */
  L_BTN     = 17,  /* arg0=1 单击(切区) / 2 长按(步进)，arg1=当时的选中??0/1) */
  L_INTA    = 18,  /* arg0=INTERRUPTA 快照（R/C，读即清??*/
  L_INT0    = 19,  /* arg0=Interrupt(0x42) 快照（已剔除 I_CRC_CHK??*/
  L_ARM     = 20,  /* 开??PD（挂 Rd / 显示??*/
  L_SOFTRST = 21,  /* 我方主动??Soft Reset（arg0=ok，arg1=第几枪；0=开机那一枪） */
  L_CRC     = 22,  /* 硬件自动 GoodCRC 开关（arg0=1 开 / 0 关） */
  L_ACK     = 23,  /* boot SR 后手动确认：arg0=成功发出??GoodCRC ??*/
  L_DBG     = 24,  /* 收到??*每一??*报文。arg0=msgType，arg1=(isSourceCap?0x80:0)|msgId */
  L_INTB    = 25,  /* arg0=INTERRUPTB 快照（R/C）。IB_GCRCSENT(0x01) 置位 =
                    *   芯片刚回过一??GoodCRC = **刚收到一条有效报??*??*/
  L_SRC     = 26,  /* ??源端插拔 / VBUS 状态变化（**中断驱动**，见主循??4 号块）??*/
  L_CC      = 27,  /* ??CC 终结（BC_LVL）变??—??**拔出快通道**的证据??                    *   arg0 = ccOk?? = CC 有终??/ 0 = 终结丢了）、arg1 = STATUS0(0x40)??*/
  L_PPS     = 28,  /* ????caps 里找到了 PPS APDO??                    *   arg0 = Object Position，arg1 = 源端 specRev?? = Rev3.0）??*/
  L_PSET    = 29,  /* ??发出 Programmable RDO??                    *   arg0 = 目标电压 / 100mV，arg1 = 目标电流 / 100mA??*/
  L_PERR    = 30,  /* PPS 异常：arg0 = 1 REJECT / 2 WAIT / 3 超时，arg1 = msgType */
  L_PEER    = 31,  /* ??取证??*每一??*??pollSourceCapabilities() 消费掉的对端报文??                    *   arg0 = msgType（含我们不认识的类型 —??用来??源端到底发了什??）??                    *   arg1 = 1 已识别处??/ 0 未识别（只是丢弃）??*/
  L_RDOB    = 32,  /* ??取证：Programmable RDO 的原始字节??                    *   arg0 = 字节下标 0..3，arg1 = 该字节。只在??*变化**时记??*/
  L_PDOH    = 33,  /* ??取证：caps 里第 i ??PDO ??*最高字??*（含 bits[31:30] 类型）??                    *   arg0 = PDO 下标 0..3，arg1 = raw >> 24??                    *   0xC0..0xFF ??bits[31:30]=11 = APDO??x00..0x3F ??Fixed??*/
  L_KA      = 34,  /* ??PPS 保活心跳触发一次（arg0 = 累计第几次，??8 位）??*/
  L_FIXED   = 35,  /* ????5V Fixed Request（PPS 前置）。arg0=ok，arg1=Object Position??*/
  L_DRAW    = 36,  /* ??渲染耗时实测（毫秒，16 位）。arg0=低字节，arg1=高字节??                    *   一??DrawMessage（整??160×80）或 DrawBand??60×38）一条??*/
};

/* Private variables ---------------------------------------------------------*/
extern "C" {
/* 事件日志 —??OpenOCD: mdw <g_log_head> 1 ; mdb <g_log> 512 */
volatile uint32_t g_log_head = 0;
volatile uint8_t  g_log[512] = {0};
volatile uint32_t g_log_t0   = 0;

/* 诊断快照（固定布局，LTO 下也稳定）—??OpenOCD: mdw <g_snap> 8
 *   [0] tried(??)  [1] ready   [2] devId   [3] st | (armed?0x80:0)
 *   [4] attached    [5] vbusOk | ccOk<<1    [6] hrstCnt [7] specRev
 *   [8] ppsPos      [9] pdoCount
 *   [10..11] tgtV(小端)         [12..13] tgtI(小端)
 *   [14] selBand    [15] ppsOk(=收到??PS_RDY)
 *   [16] ppsMinV/100  [17] ppsMaxV/100  [18] ppsMaxI/100
 *   [19] (单击次数&0xF)<<4 | (步进次数&0xF)
 *   [20..23] 主循环轮??小端)
 *   [24] g_pdIrqCnt 低字??     [25] g_keyIrqCnt 低字?? *   [26] g_clkIrqCnt 低字??    [27] 最??STATUS1
 *   [28] 最??STATUS0A          [29] 最??Interrupt(0x42)
 *   [30] hbHits 低字节（心跳顶事次数 ??正常应恒??0?? *   [31] intaAcc | int0Acc（已置位过的中断源集合） */
volatile uint8_t  g_snap[32] = {0};
}

/* USER CODE BEGIN 0 */

/*============================================================================
 * 屏幕布局??60×80?? *
 *   ┌────────────────────────────────────────────??y=0
 *   ??┌──────??             5200  mV             ??  上带 = 电压
 *   ??│██████│              MAX 11000mV          ??  y=0..37
 *   ??└──────??                                  ?? *   ├────────────────────────────────────────────??y=38..41 空档
 *   ??┌──────??             2000  mA             ??  下带 = 电流
 *   ??│███   ??             MAX 3000mA           ??  y=42..79
 *   ??└──────??                                  ?? *   └────────────────────────────────────────────??y=79
 *
 *   每带：左??70px 进度条（x=3..72），右侧 x=74 起是 scale2 大号数值，
 *         数值右下方??scale1 单位，数值正下方一行是 scale1 上限?? *   选中带：底色提亮（C_ROW_BG_SEL?? 边框换高亮色（C_BORDER_SEL）?? *
 *   ⚠️ 为什么大号数值用 **scale2 而不??11x18 字体**：本工程的字库清?? *      （font_config.hpp）只编译??Font_7x10 一套（RO ??1.9KB）。用 scale2
 *      ??7x10 放大??14x20，视觉上等价于大字体，且**一个字节的额外 RO 都不??*?? *      ⚠️ 字模**必须**??font_get_glyph() 查，不能??`Font_7x10.data[...]`?? *        本字库是"字符级编??（Font_7x10 = {7,10,nullptr}），FontDef.data
 *        **恒为 nullptr**。直接解引用是空指针解引用；更坑的是 LTO 看得?? *        data 是常??nullptr，会把整张字模表判定??不可??直接删掉
 *        （实??RO-data ??2.2KB），而编译期 0 error 0 warning —??完全静默?? *============================================================================*/
namespace {

using LoveFinderLib::FUSB302;
using LoveFinderLib::FUSB302_Message;
using LoveFinderLib::FUSB302_PDO;
using LoveFinderLib::e_FUSB302_CC;
using LoveFinderLib::e_FUSB302_PDOType;
namespace Reg = LoveFinderLib::FUSB302_Reg;
namespace Bit = LoveFinderLib::FUSB302_Bit;

/* ---- 屏幕几何（对齐参考工程 PPS_Req 的两行版式；本板不画横幅，两行铺满 160×80） ---- */
constexpr uint16_t SCR_W  = 160;
constexpr uint16_t SCR_H  = 80;
constexpr uint16_t ROW_H  = 40;                     /* 电/流每行 40px */
constexpr uint16_t ROW_Y[2] = { 0u, 40u };          /* 两行顶 */

/* 行内布局（与参考工程 pps_ui.cpp 一一对应；因无横幅，纵向偏移整体下移居中） */
constexpr uint16_t BAR_X  = 2;                      /* 进度条内框 */
constexpr uint16_t BAR_W  = 64;
constexpr uint16_t BAR_DY = 7;                      /* 相对行顶 */
constexpr uint16_t BAR_H  = 25;
constexpr uint16_t NUM_X  = 68;                     /* 右侧数值区左边界 */
constexpr uint16_t NUM_DY = 8;                      /* 数值行（相对行顶） */
constexpr uint16_t LBL_DY = 22;                     /* 标签行（相对行顶） */

/* Font_7x10 度量 */
constexpr uint16_t FW = 7;                          /* 字宽（也作步进） */
constexpr uint16_t FH = 10;                         /* 字高 */

/* ---- 调色板（RGB565，取参考工程 pps_ui.cpp 的调色板） ---- */
constexpr uint16_t C_BG         = ST7735::Color565( 10,  14,  20);
constexpr uint16_t C_ROW_BG     = ST7735::Color565( 16,  22,  30);
constexpr uint16_t C_ROW_BG_SEL = ST7735::Color565( 20,  31,  43);
constexpr uint16_t C_BORDER     = ST7735::Color565( 30,  41,  56);
constexpr uint16_t C_BORDER_SEL = ST7735::Color565( 56, 189, 248);
constexpr uint16_t C_TRACK      = ST7735::Color565( 23,  32,  43);
constexpr uint16_t C_TRACK_EDGE = ST7735::Color565( 37,  49,  65);
constexpr uint16_t C_VOLT_FILL  = ST7735::Color565( 56, 189, 248);
constexpr uint16_t C_CURR_FILL  = ST7735::Color565(251, 191,  36);
constexpr uint16_t C_VOLT_TEXT  = ST7735::Color565(125, 211, 252);
constexpr uint16_t C_CURR_TEXT  = ST7735::Color565(252, 211,  77);
constexpr uint16_t C_BAR_SCALE  = ST7735::Color565(235, 244, 255);
constexpr uint16_t C_BAR_STEP   = ST7735::Color565(200, 215, 235);
constexpr uint16_t C_NUM_LABEL  = ST7735::Color565(130, 145, 165);
constexpr uint16_t C_REAL_TEXT  = ST7735::Color565(226, 238, 250);   /* 实测值：偏白，与 req 的行色区分 */

/* Status0(0x40) 位掩码（旧 UI 段里定义，随版式重写一并搬来这里）：
 *   bits[1:0] = BC_LVL（CC 电平），0 = 源端 Rp 没了 = 线拔出 / 源端撤了终结；
 *   bit6 = ACTIVITY（CC 上正有 BMC 活动）—— PD 突发期间 BC_LVL 会假读 0，
 *          所以"CC 终结丢失"判据里必须要求 ACTIVITY == 0。 */
constexpr uint8_t S0_BC_LVL_MASK = 0x03u;
constexpr uint8_t S0_ACTIVITY    = 0x40u;

/* 每行切两块差分刷新：左 x=0..67（进度条），右 x=68..159（req | real 数值） */
constexpr uint16_t REG_LX0 = 0,  REG_LW = 68;
constexpr uint16_t REG_RX0 = 68, REG_RW = 92;

/* 步长表（双击轮换，与参考工程一致） */
constexpr uint16_t kStepV[4] = { 20u, 50u, 100u, 500u };
constexpr uint16_t kStepI[4] = { 50u, 100u, 200u, 500u };

/*--------------------------------------------------------------------------*/
/* 文字：字形查表 + 透明点阵落笔                                              */
/*--------------------------------------------------------------------------*/
/* 把字符串的每个字符预先查好字模指针（每帧一次，而不是每像素一次）。 */
void LoadGlyphs(const char* s, uint8_t n, const uint16_t** out, uint8_t cap)
{
  for (uint8_t i = 0; i < cap; ++i) {
    if (i < n) {
      const uint8_t ch = static_cast<uint8_t>(s[i]);
      out[i] = ((ch >= 32u) && (ch <= 126u))
                   ? font_get_glyph(Font_7x10, ch)
                   : nullptr;
    } else {
      out[i] = nullptr;
    }
  }
}

/* 事件日志的前向声明（渲染打点要用；本体在下方 UI 段之后）。 */
void Log(uint8_t code, uint8_t a0, uint8_t a1);

/* 7x10 字符串像素宽（按 font.width=7 步进，与绘制一致）。 */
uint16_t TextW(const char* s)
{
  uint16_t n = 0u;
  while (s[n] != '\0') { ++n; }
  return static_cast<uint16_t>(n * FW);
}

/* 毫单位 → **定宽** "XX.YYV"/"XX.YYA"（整数两位补零，两位小数）。
 * 目的是无论数值多少，req / real 的字符数恒定 → 区域大小永不变。 */
void FmtFixed(char* out, size_t n, uint16_t milli, char unit)
{
  const unsigned v = milli / 10u;          /* 以 0.01V / 0.01A 为单位 */
  snprintf(out, n, "%02u.%02u%c",
           static_cast<unsigned>(v / 100u),
           static_cast<unsigned>(v % 100u), unit);
}

/* 毫单位 → 去尾零（进度条内 min/max 用："21" / "3.3" / "19.62"）。 */
void FmtTrim(char* out, size_t n, uint16_t milli)
{
  const unsigned whole = milli / 1000u;
  const unsigned fr    = (milli % 1000u) / 10u;   /* 2 位小数 */
  if (fr == 0u) {
    snprintf(out, n, "%u", whole);
  } else if ((fr % 10u) == 0u) {
    snprintf(out, n, "%u.%u", whole, fr / 10u);
  } else {
    snprintf(out, n, "%u.%02u", whole, fr);
  }
}

/* 毫单位 → 毫值+单位（进度条内步长："20mV" / "50mA"）。 */
void FmtMilli(char* out, size_t n, uint16_t milli, char unit)
{
  snprintf(out, n, "%um%c", static_cast<unsigned>(milli), unit);
}

/*--------------------------------------------------------------------------*/
/* 透明点阵文字：只写字形亮点（不刷背景），让底色/轨道透过来。               */
/*   x0 = 目标区段的屏幕左边界；dst 的 x 相对该区段起点。                     */
/*--------------------------------------------------------------------------*/
void PaintText(uint16_t y, uint8_t* dst, uint16_t w, uint16_t x0,
               uint16_t ox, uint16_t oy,
               const uint16_t* const* glyphs, uint8_t n,
               uint16_t s, uint16_t color)
{
  if (n == 0u) { return; }
  const uint16_t ch = static_cast<uint16_t>(FH * s);   /* 每字符占的像素高 */
  if (y < oy || y >= static_cast<uint16_t>(oy + ch)) { return; }

  const uint16_t cw = static_cast<uint16_t>(FW * s);   /* 每字符占的像素宽 */
  const uint16_t ln = static_cast<uint16_t>((y - oy) / s);   /* 0..FH-1 */
  const uint8_t  hi = static_cast<uint8_t>(color >> 8);
  const uint8_t  lo = static_cast<uint8_t>(color & 0xFFu);

  for (uint8_t i = 0u; i < n; ++i) {
    const uint16_t* g = glyphs[i];
    if (g == nullptr) { continue; }
    const uint16_t bits = g[ln];
    const uint16_t xs0  = static_cast<uint16_t>(ox + static_cast<uint16_t>(i) * cw);
    for (uint8_t j = 0u; j < FW; ++j) {
      if (((bits << j) & 0x8000u) == 0u) { continue; }
      const uint16_t xs = static_cast<uint16_t>(xs0 + static_cast<uint16_t>(j) * s);
      for (uint16_t k = 0u; k < s; ++k) {
        const uint16_t x = static_cast<uint16_t>(xs + k);
        if (x >= x0 && x < static_cast<uint16_t>(x0 + w)) {
          const uint16_t idx = static_cast<uint16_t>((x - x0) * 2u);
          dst[idx] = hi; dst[idx + 1u] = lo;
        }
      }
    }
  }
}

/*--------------------------------------------------------------------------*/
/* 行渲染上下文 + 逐行回调                                                    */
/*--------------------------------------------------------------------------*/
struct RowCtx {
  uint16_t ry;                                   /* 行顶绝对 y */
  uint16_t bg, border, fill, numCol, edgeCol;
  uint16_t fillW;                                /* 进度条填充宽 0..BAR_W */
  /* 进度条内文字 */
  char minS[8];   uint8_t minN;   uint16_t minW;   const uint16_t* minG[8];
  char maxS[8];   uint8_t maxN;   uint16_t maxW;   const uint16_t* maxG[8];
  char stepS[10]; uint8_t stepN;  uint16_t stepW;  const uint16_t* stepG[10];
  /* 右侧 req | real */
  char reqS[10];  uint8_t reqN;   const uint16_t* reqG[10];
  char realS[10]; uint8_t realN;  const uint16_t* realG[10];
  uint16_t yNum, yLbl;
  uint16_t startX, realX, reqW, realW, reqLblX, realLblX;
};
RowCtx g_row;

const uint16_t* g_reqLblG[3];
const uint16_t* g_realLblG[4];
bool g_lblLoaded = false;

void LoadLabelsOnce()
{
  if (g_lblLoaded) { return; }
  LoadGlyphs("req", 3u, g_reqLblG, 3u);
  LoadGlyphs("real", 4u, g_realLblG, 4u);
  g_lblLoaded = true;
}

/* 把一行要画的内容全部算好，塞进 g_row。
 * realOk=false 时右侧 real 列显示占位 "--.--V" / "--.--A"。 */
void RowPrepare(uint8_t row, uint16_t req, uint16_t mn, uint16_t mx,
                uint16_t step, bool sel, uint16_t real, bool realOk)
{
  const bool isV = (row == 0u);
  RowCtx& b = g_row;

  b.ry      = ROW_Y[row];
  b.bg      = sel ? C_ROW_BG_SEL : C_ROW_BG;
  b.border  = sel ? C_BORDER_SEL : C_BORDER;
  b.fill    = isV ? C_VOLT_FILL  : C_CURR_FILL;
  b.numCol  = isV ? C_VOLT_TEXT  : C_CURR_TEXT;
  b.edgeCol = sel ? C_BORDER_SEL : C_TRACK_EDGE;

  /* 进度条填充宽度：t = (req-mn)/(mx-mn)，四舍五入 */
  uint32_t fw = 0u;
  if (mx > mn) {
    const uint32_t v    = (req > mn) ? static_cast<uint32_t>(req - mn) : 0u;
    const uint32_t span = static_cast<uint32_t>(mx - mn);
    fw = (v * BAR_W + span / 2u) / span;
    if (fw > BAR_W) { fw = BAR_W; }
  }
  b.fillW = static_cast<uint16_t>(fw);

  /* 三串进度条内文字 */
  FmtTrim(b.minS, sizeof(b.minS), mn);
  FmtTrim(b.maxS, sizeof(b.maxS), mx);
  FmtMilli(b.stepS, sizeof(b.stepS), step, isV ? 'V' : 'A');
  b.minN  = static_cast<uint8_t>(strlen(b.minS));
  b.maxN  = static_cast<uint8_t>(strlen(b.maxS));
  b.stepN = static_cast<uint8_t>(strlen(b.stepS));
  b.minW  = TextW(b.minS);
  b.maxW  = TextW(b.maxS);
  b.stepW = TextW(b.stepS);

  /* 右侧 req 与 real：都固定 "XX.YYV" / "XX.YYA"（定宽，区域大小恒定） */
  const char unit = isV ? 'V' : 'A';
  FmtFixed(b.reqS, sizeof(b.reqS), req, unit);
  if (realOk) {
    FmtFixed(b.realS, sizeof(b.realS), real, unit);
  } else {
    snprintf(b.realS, sizeof(b.realS), "--.--%c", unit);
  }
  b.reqN  = static_cast<uint8_t>(strlen(b.reqS));
  b.realN = static_cast<uint8_t>(strlen(b.realS));
  b.reqW  = TextW(b.reqS);
  b.realW = TextW(b.realS);

  const uint16_t gap = 8u;   /* 两列留白，不再用 "|" 隔断 */
  const uint16_t totalW = static_cast<uint16_t>(b.reqW + gap + b.realW);
  const uint16_t avail  = static_cast<uint16_t>(SCR_W - NUM_X);
  b.startX = static_cast<uint16_t>(NUM_X + ((avail > totalW) ? ((avail - totalW) / 2u) : 0u));
  b.realX  = static_cast<uint16_t>(b.startX + b.reqW + gap);

  b.yNum = static_cast<uint16_t>(b.ry + NUM_DY);
  b.yLbl = static_cast<uint16_t>(b.ry + LBL_DY);
  b.reqLblX  = static_cast<uint16_t>(b.startX + (b.reqW  - TextW("req"))  / 2u);
  b.realLblX = static_cast<uint16_t>(b.realX  + (b.realW - TextW("real")) / 2u);

  LoadGlyphs(b.minS,  b.minN,  b.minG,  8u);
  LoadGlyphs(b.maxS,  b.maxN,  b.maxG,  8u);
  LoadGlyphs(b.stepS, b.stepN, b.stepG, 10u);
  LoadGlyphs(b.reqS,  b.reqN,  b.reqG,  10u);
  LoadGlyphs(b.realS, b.realN, b.realG, 10u);
  LoadLabelsOnce();
}

/* 左半区（进度条）逐行回调：每个像素行调一次。 */
void RowRenderL(uint16_t y, uint8_t* dst, uint16_t w)
{
  const RowCtx& b = g_row;
  const uint16_t ry = static_cast<uint16_t>(y - b.ry);
  const uint16_t x0 = REG_LX0;

  const bool edgeTB = (ry == 0u) || (ry == static_cast<uint16_t>(ROW_H - 1u));
  const uint16_t base = edgeTB ? b.border : b.bg;
  const uint8_t bhi = static_cast<uint8_t>(base >> 8);
  const uint8_t blo = static_cast<uint8_t>(base & 0xFFu);
  for (uint16_t i = 0u; i < w; ++i) { dst[i * 2u] = bhi; dst[i * 2u + 1u] = blo; }
  if (!edgeTB) {   /* 左竖边框（x=0） */
    dst[0] = static_cast<uint8_t>(b.border >> 8);
    dst[1] = static_cast<uint8_t>(b.border & 0xFFu);
  }

  /* 进度条：轨道 + 填充 + 1px 内外描边 */
  const uint16_t by0 = static_cast<uint16_t>(BAR_DY - 1u);
  const uint16_t by1 = static_cast<uint16_t>(BAR_DY + BAR_H);
  if (ry >= by0 && ry <= by1) {
    const bool edgeLR = (ry == by0) || (ry == by1);
    const uint16_t fillEnd = static_cast<uint16_t>(BAR_X + b.fillW);
    for (uint16_t x = static_cast<uint16_t>(BAR_X - 1u);
         (x <= static_cast<uint16_t>(BAR_X + BAR_W)) && (x < static_cast<uint16_t>(x0 + w));
         ++x) {
      uint16_t c;
      if (edgeLR || (x == static_cast<uint16_t>(BAR_X - 1u)) ||
          (x == static_cast<uint16_t>(BAR_X + BAR_W))) {
        c = b.edgeCol;
      } else {
        c = (x < fillEnd) ? b.fill : C_TRACK;
      }
      const uint16_t idx = static_cast<uint16_t>((x - x0) * 2u);
      dst[idx] = static_cast<uint8_t>(c >> 8);
      dst[idx + 1u] = static_cast<uint8_t>(c & 0xFFu);
    }
  }

  /* 进度条内文字（透明）：左上 min / 右上 max / 底部居中 步长 */
  PaintText(y, dst, w, x0, static_cast<uint16_t>(BAR_X + 2u),
            static_cast<uint16_t>(b.ry + BAR_DY + 2u), b.minG, b.minN, 1u, C_BAR_SCALE);
  PaintText(y, dst, w, x0, static_cast<uint16_t>(BAR_X + BAR_W - 2u - b.maxW),
            static_cast<uint16_t>(b.ry + BAR_DY + 2u), b.maxG, b.maxN, 1u, C_BAR_SCALE);
  PaintText(y, dst, w, x0, static_cast<uint16_t>(BAR_X + (BAR_W - b.stepW) / 2u),
            static_cast<uint16_t>(b.ry + BAR_DY + BAR_H - 12u), b.stepG, b.stepN, 1u, C_BAR_STEP);
}

/* 右半区（req | real）逐行回调。 */
void RowRenderR(uint16_t y, uint8_t* dst, uint16_t w)
{
  const RowCtx& b = g_row;
  const uint16_t ry = static_cast<uint16_t>(y - b.ry);
  const uint16_t x0 = REG_RX0;

  const bool edgeTB = (ry == 0u) || (ry == static_cast<uint16_t>(ROW_H - 1u));
  const uint16_t base = edgeTB ? b.border : b.bg;
  const uint8_t bhi = static_cast<uint8_t>(base >> 8);
  const uint8_t blo = static_cast<uint8_t>(base & 0xFFu);
  for (uint16_t i = 0u; i < w; ++i) { dst[i * 2u] = bhi; dst[i * 2u + 1u] = blo; }
  if (!edgeTB) {   /* 右竖边框（x=159） */
    const uint16_t xe = static_cast<uint16_t>((w - 1u) * 2u);
    dst[xe] = static_cast<uint8_t>(b.border >> 8);
    dst[xe + 1u] = static_cast<uint8_t>(b.border & 0xFFu);
  }

  /* req（行色） | real（偏白）+ 下方标签 —— 无隔断竖条 */
  PaintText(y, dst, w, x0, b.startX, b.yNum, b.reqG,  b.reqN,  1u, b.numCol);
  PaintText(y, dst, w, x0, b.realX,  b.yNum, b.realG, b.realN, 1u, C_REAL_TEXT);
  PaintText(y, dst, w, x0, b.reqLblX,  b.yLbl, g_reqLblG,  3u, 1u, C_NUM_LABEL);
  PaintText(y, dst, w, x0, b.realLblX, b.yLbl, g_realLblG, 4u, 1u, C_NUM_LABEL);
}

/*--------------------------------------------------------------------------*/
/* 差分缓存（内容没变就完全不碰屏幕）                                         */
/*--------------------------------------------------------------------------*/
struct RowCache {
  char     minS[8], maxS[8], stepS[10], reqS[10], realS[10];
  uint16_t fillW;
  bool     sel;
  bool     valid;
};
RowCache g_rc[2] = {};

void UiReset() { g_rc[0].valid = false; g_rc[1].valid = false; }

/* 画一行（差分区段刷新：进度条区 / 数值区各一块）。 */
void DrawRow(uint8_t row, uint16_t req, uint16_t mn, uint16_t mx,
             uint16_t step, bool sel, uint16_t real, bool realOk)
{
  RowPrepare(row, req, mn, mx, step, sel, real, realOk);

  RowCache& c = g_rc[row];
  const bool full   = (!c.valid) || (c.sel != sel);
  const bool barChg = full ||
      (strcmp(c.minS,  g_row.minS)  != 0) ||
      (strcmp(c.maxS,  g_row.maxS)  != 0) ||
      (strcmp(c.stepS, g_row.stepS) != 0) ||
      (c.fillW != g_row.fillW);
  const bool numChg = full ||
      (strcmp(c.reqS,  g_row.reqS)  != 0) ||
      (strcmp(c.realS, g_row.realS) != 0);

  const uint32_t t0 = BSP_GetTick();
  if (barChg) { ST7735_DrawFrameRect(REG_LX0, g_row.ry, REG_LW, ROW_H, RowRenderL); }
  if (numChg) { ST7735_DrawFrameRect(REG_RX0, g_row.ry, REG_RW, ROW_H, RowRenderR); }
  if (full) {   /* 只在整行重画时打点，避免实时数值把日志环刷爆 */
    const uint32_t d = BSP_GetTick() - t0;
    Log(L_DRAW, static_cast<uint8_t>(d & 0xFFu), static_cast<uint8_t>((d >> 8) & 0xFFu));
  }

  snprintf(c.minS,  sizeof(c.minS),  "%s", g_row.minS);
  snprintf(c.maxS,  sizeof(c.maxS),  "%s", g_row.maxS);
  snprintf(c.stepS, sizeof(c.stepS), "%s", g_row.stepS);
  snprintf(c.reqS,  sizeof(c.reqS),  "%s", g_row.reqS);
  snprintf(c.realS, sizeof(c.realS), "%s", g_row.realS);
  c.fillW = g_row.fillW;
  c.sel   = sel;
  c.valid = true;
}

/* 首屏：铺底 + 失效两行缓存，随后由 DrawRow 全画一遍。 */
void UiInitScreen()
{
  ST7735_FillScreen(C_BG);
  UiReset();
}

/*--------------------------------------------------------------------------*/
/* INA226 实时测量（"real" 列）                                              */
/*   与 FUSB302 共用 I2C1（PF0=SDA/PF1=SCL）。低频轮询、短事务，             */
/*   只取母线电压(0x02, 1.25mV/LSB) 与电流(0x04, 1mA/LSB, 10mΩ shunt)。      */
/*--------------------------------------------------------------------------*/
constexpr uint8_t  INA226_ADDRS[4]   = { 0x40u, 0x41u, 0x44u, 0x45u };
constexpr uint8_t  INA226_REG_CONFIG = 0x00u;
constexpr uint8_t  INA226_REG_BUSV   = 0x02u;
constexpr uint8_t  INA226_REG_CURR   = 0x04u;
constexpr uint8_t  INA226_REG_CAL    = 0x05u;
constexpr uint8_t  INA226_REG_MFG    = 0xFEu;
constexpr float    INA226_SHUNT_MOHM = 10.0f;   /* 板载检流电阻 10mΩ */
constexpr float    INA226_MAX_A      = 3.0f;    /* 最大期望电流 3A（照参考库 setCalibration(3.0)） */
constexpr uint32_t INA_POLL_MS       = 200u;

uint8_t  g_inaAddr  = 0u;      /* 0 = 没探到 */
bool     g_inaOk    = false;   /* 最近一次读数有效 */
uint16_t g_inaBusMv = 0u;      /* 母线电压 mV */
int16_t  g_inaCurMa = 0;       /* 电流 mA（有符号） */
float    g_inaLsbMa = 0.0f;    /* 电流 LSB（mA/LSB，校准后） */
uint32_t g_inaAt    = 0u;
volatile uint16_t g_inaRawBus   = 0u;   /* 诊断：母线电压寄存器原值 */
volatile uint16_t g_inaRawShunt = 0u;   /* 诊断：分流电压寄存器原值 */
uint16_t g_inaDieId    = 0u;   /* 诊断：DIE_ID(0xFF)，INA226=0x2260 / INA219=0x0219 */

bool InaWaitFlag(uint32_t (*fn)(I2C_TypeDef*), uint32_t to)
{
  const uint32_t t0 = BSP_GetTick();
  while (fn(I2C1) == 0u) {
    if ((uint32_t)(BSP_GetTick() - t0) >= to) { return false; }
  }
  return true;
}

bool InaBusFree()
{
  const uint32_t t0 = BSP_GetTick();
  while (LL_I2C_IsActiveFlag_BUSY(I2C1) != 0u) {
    if ((uint32_t)(BSP_GetTick() - t0) >= 3u) { return false; }
  }
  return true;
}

bool InaWriteReg(uint8_t addr, uint8_t reg, uint16_t val)
{
  if (!InaBusFree()) { return false; }

  LL_I2C_GenerateStartCondition(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_SB, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, static_cast<uint8_t>((addr << 1) | 0u));
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_ADDR, 3u)) {
    if (LL_I2C_IsActiveFlag_AF(I2C1) != 0u) { LL_I2C_ClearFlag_AF(I2C1); }
    LL_I2C_GenerateStopCondition(I2C1);
    return false;
  }
  LL_I2C_ClearFlag_ADDR(I2C1);

  if (!InaWaitFlag(LL_I2C_IsActiveFlag_TXE, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, reg);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_TXE, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, static_cast<uint8_t>(val >> 8));
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_TXE, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, static_cast<uint8_t>(val & 0xFFu));
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_BTF, 3u)) { return false; }

  LL_I2C_GenerateStopCondition(I2C1);
  return true;
}

bool InaReadReg(uint8_t addr, uint8_t reg, uint16_t& out)
{
  if (!InaBusFree()) { return false; }

  LL_I2C_GenerateStartCondition(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_SB, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, static_cast<uint8_t>((addr << 1) | 0u));
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_ADDR, 3u)) {
    if (LL_I2C_IsActiveFlag_AF(I2C1) != 0u) { LL_I2C_ClearFlag_AF(I2C1); }
    LL_I2C_GenerateStopCondition(I2C1);
    return false;
  }
  LL_I2C_ClearFlag_ADDR(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_TXE, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, reg);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_BTF, 3u)) { return false; }

  LL_I2C_GenerateStartCondition(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_SB, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, static_cast<uint8_t>((addr << 1) | 1u));
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_ADDR, 3u)) {
    if (LL_I2C_IsActiveFlag_AF(I2C1) != 0u) { LL_I2C_ClearFlag_AF(I2C1); }
    LL_I2C_GenerateStopCondition(I2C1);
    return false;
  }
  LL_I2C_AcknowledgeNextData(I2C1, LL_I2C_ACK);
  LL_I2C_ClearFlag_ADDR(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_RXNE, 3u)) { return false; }
  LL_I2C_AcknowledgeNextData(I2C1, LL_I2C_NACK);
  const uint8_t hi = LL_I2C_ReceiveData8(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_RXNE, 3u)) { return false; }
  LL_I2C_GenerateStopCondition(I2C1);
  const uint8_t lo = LL_I2C_ReceiveData8(I2C1);

  out = static_cast<uint16_t>((static_cast<uint16_t>(hi) << 8) | lo);
  return true;
}

/* 探地址：读 MFG_ID 是否为 0x5449 ('TI')。 */
bool InaProbeAt(uint8_t addr)
{
  uint16_t v = 0u;
  return InaReadReg(addr, INA226_REG_MFG, v) && (v == 0x5449u);
}

/* ---- I²C 总线体检（开机做一次）：0x08..0x77 逐地址写寻址，谁 ACK 谁在 ---- */
uint8_t g_i2cMap[14] = {0};   /* bit(a-0x08) = 地址 a 有 ACK */
bool    g_i2cScanned = false;

bool InaAddrAck(uint8_t addr)
{
  if (!InaBusFree()) { return false; }
  LL_I2C_GenerateStartCondition(I2C1);
  if (!InaWaitFlag(LL_I2C_IsActiveFlag_SB, 3u)) { return false; }
  LL_I2C_TransmitData8(I2C1, static_cast<uint8_t>((addr << 1) | 0u));
  const bool ack = InaWaitFlag(LL_I2C_IsActiveFlag_ADDR, 3u);
  if (!ack && (LL_I2C_IsActiveFlag_AF(I2C1) != 0u)) { LL_I2C_ClearFlag_AF(I2C1); }
  if (ack) { LL_I2C_ClearFlag_ADDR(I2C1); }
  LL_I2C_GenerateStopCondition(I2C1);
  return ack;
}

void InaI2cScan()
{
  for (uint8_t i = 0u; i < 14u; ++i) { g_i2cMap[i] = 0u; }
  for (uint16_t a = 0x08u; a <= 0x77u; ++a) {
    if (InaAddrAck(static_cast<uint8_t>(a))) {
      const uint8_t b = static_cast<uint8_t>(a - 0x08u);
      g_i2cMap[b >> 3] |= static_cast<uint8_t>(1u << (b & 7u));
    }
  }
  g_i2cScanned = true;
}

/* 初始化：扫地址 → 写 CONFIG + CAL。 */
bool InaInit()
{
  if (!g_i2cScanned) { InaI2cScan(); }
  g_inaAddr = 0u;
  g_inaOk   = false;
  for (uint8_t i = 0u; i < 4u; ++i) {
    const uint8_t a = INA226_ADDRS[i];
    if (!InaProbeAt(a)) { continue; }
    /* 配置：AVG_16 + Vbus/Vsh 各 1.1ms + 分流&总线连续（= 参考库默认）= 0x0527 */
    if (!InaWriteReg(a, INA226_REG_CONFIG, 0x0527u)) { continue; }
    /* 校准（照参考库 setCalibration）：Current_LSB = max/32768；CAL = 0.00512/(LSB·R) */
    const float lsbA = INA226_MAX_A / 32768.0f;
    const float rOhm = INA226_SHUNT_MOHM / 1000.0f;
    const uint16_t cal = static_cast<uint16_t>(0.00512f / (lsbA * rOhm));
    if (!InaWriteReg(a, INA226_REG_CAL, cal)) { continue; }
    g_inaLsbMa = (0.00512f / (static_cast<float>(cal) * rOhm)) * 1000.0f;  /* A/LSB → mA/LSB */
    (void)InaReadReg(a, 0xFFu, g_inaDieId);   /* 诊断：DIE_ID */
    g_inaAddr = a;
    return true;
  }
  return false;
}

/* 周期读母线电压/电流；未探到芯片时每 500ms 重试一次。 */
void InaIoPoll(uint32_t now)
{
  if (g_inaAddr == 0u) {
    if ((now - g_inaAt) >= 500u) { g_inaAt = now; (void)InaInit(); }
    return;
  }
  if ((now - g_inaAt) < INA_POLL_MS) { return; }
  g_inaAt = now;

  uint16_t bus = 0u, cur = 0u, sh = 0u;
  const bool okSh  = InaReadReg(g_inaAddr, 0x01u, sh);
  const bool okBus = InaReadReg(g_inaAddr, INA226_REG_BUSV, bus);
  const bool okCur = InaReadReg(g_inaAddr, INA226_REG_CURR, cur);
  if (okSh)  { g_inaRawShunt = sh; }
  if (okBus) { g_inaRawBus   = bus; }
  if (okSh && okBus && okCur) {
    g_inaBusMv = static_cast<uint16_t>((static_cast<uint32_t>(bus) * 5u) / 4u);  /* 1.25mV/LSB */
    /* 本板 INA226 电流方向为反（IN+/IN- 接反）→ 取负还原；仍按有符号处理 */
    const float ma = -static_cast<float>(static_cast<int16_t>(cur)) * g_inaLsbMa;
    g_inaCurMa = static_cast<int16_t>(ma);                                       /* mA */
    g_inaOk    = true;
  } else {
    g_inaOk = false;
  }
}

/*--------------------------------------------------------------------------*/
/* 事件日志                                                                  */
/*--------------------------------------------------------------------------*/
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
/* PD：在 Source_Capabilities 里找 PPS APDO                                   */
/*--------------------------------------------------------------------------*/
/* 返回 Object Position??..7），0 = 没有 PPS APDO?? *
 * APDO 的判定（PD 3.0 §6.4.1）：
 *   bits[31:30] == 0b11  ??这是 APDO
 *   bits[29:28] == 0b00  ??APDO 子类??= PPS
 *   （bits[29:28] == 0b01 ??可调电压"AVS??b10/0b11 保留?? *
 * ??位域解析**交给共享库的 decodePdo()**，本文件不自己抠位：
 *   PPS APDO ??Min Voltage 到底落在 B16..B9 还是 B15..B8，业界文档与实现
 *   存在分歧（规范表??B16..B9，而实??SW3526S 的解码在 `>>8` 下才??3.3V）?? *   库里??decodePdo() ??*已对着本台源端验证??*的（解出 3.3-11.0V 3.00A），
 *   直接消费它的 minVoltage_mV / voltage_mV(=maxV) / ppsCurrent_mA(=maxI)
 *   最稳，也把"位域之争"关在一个文件里，将来只需改库??*/
uint8_t findPps(const FUSB302_Message& m, uint16_t* outMinV, uint16_t* outMaxV,
                uint16_t* outMaxI)
{
  for (uint8_t i = 0; i < m.pdoCount && i < 7u; ++i) {
    const uint32_t raw = m.pdo[i];
    if (((raw >> 30) & 0x3u) != 0x3u) { continue; }   /* 只要 APDO */
    if (((raw >> 28) & 0x3u) != 0x0u) { continue; }   /* 只要 PPS 子类??*/

    FUSB302_PDO p;
    if (!FUSB302::decodePdo(raw, p)) { continue; }
    if (outMinV != nullptr) { *outMinV = p.minVoltage_mV; }
    if (outMaxV != nullptr) { *outMaxV = p.voltage_mV; }      /* APDO: maxV */
    if (outMaxI != nullptr) { *outMaxI = p.ppsCurrent_mA; }   /* APDO: maxI */
    return static_cast<uint8_t>(i + 1u);
  }
  return 0u;
}

/* 默认电压：优??5.2V；APDO 上限**不足 5.2V** 时取上限的一半?? * 之后对齐??20mV 步进，并夹进 [minV, maxV]??*/
uint16_t ppsDefaultV(uint16_t minV, uint16_t maxV)
{
  uint32_t v = PPS_DEF_MV;
  if (v > maxV) { v = static_cast<uint32_t>(maxV) / 2u; }
  v = (v / PPS_STEP_MV) * PPS_STEP_MV;
  if (v < minV) { v = minV; }
  if (v > maxV) { v = maxV; }
  return static_cast<uint16_t>(v);
}

/* 默认电流：优??2A；APDO 上限**不足 2A** 时取上限的一半?? * 之后对齐??50mA 步进，并夹进 [0, maxI]??*/
uint16_t ppsDefaultI(uint16_t maxI)
{
  uint32_t i = PPS_DEF_MA;
  if (i > maxI) { i = static_cast<uint32_t>(maxI) / 2u; }
  i = (i / PPS_STEP_MA) * PPS_STEP_MA;
  if (i > maxI) { i = maxI; }
  return static_cast<uint16_t>(i);
}

/* 组一??**Programmable RDO**（PD 3.0 Table 6-14）：
 *
 *   bits[31:28] Object Position        ??要第几个 PDO??..7，这里就是那??PPS APDO?? *   bits[27:26] 保留???? *   bit [25]    USB Communications Capable   ??本板不做 USB 数据，填 0
 *   bit [24]    No USB Suspend               ????0（允许源端按需挂起?? *   bit [23]    Unchunked Extended Messages  ????0
 *   bit [22]    Capability Mismatch          ????0（我们是"能力??才提这个请求?? *   bits[21:20] 保留???? *   bits[19:9]  Output Voltage   ??0mV ??bit??1 bit ??0..40.94V?? *   bits[ 8:0]  Operating Current??0mA ??bit??9 bit ??0..25.55A?? *
 * ⚠️ ??Fixed RDO 最大的不同??*这里真的带电??*。所??PPS 改电压不需?? *    "换档??，只要把新的 mV/mA 重新塞进这两个位域、再发一??Request?? * ⚠️ 四个 flag 位保??0，与已验证的 FIX 路径（Fixed RDO 也只??objpos+电流?? *    保持最小差异；将来若遇到挑剔的源端，可在这里加 RDO_USB_COMMS /
 *    RDO_NO_USB_SUSPEND??*/
uint32_t buildPpsRdo(uint8_t pos, uint16_t mv, uint16_t ma)
{
  const uint32_t v = (static_cast<uint32_t>(mv) / PPS_STEP_MV) & 0x7FFu;
  const uint32_t c = (static_cast<uint32_t>(ma) / PPS_STEP_MA) & 0x1FFu;
  return (static_cast<uint32_t>(pos & 0xFu) << 28) | (v << 9) | c;
}

/* 发一??PPS Request?? * ⚠️ 报头 SpecRev 由库??m_hdrRev 决定 —??收到 caps 时已按源端的 specRev
 *    更新过（Rev3.0 ??m_hdrRev=2），所以这??Request 的报头就??Rev3.0??*/
bool sendPpsRequest(FUSB302& pd, uint8_t pos, uint16_t mv, uint16_t ma)
{
  if (pos == 0u) { return false; }
  /* 新一轮协商：先清??上次申请成功"的结论和应答记录??     这样随后读到??msgType 一定是针对**这一??* RDO 的应答??*/
  pd.clearLastPeerMsgType();
  return pd.sendRequest(buildPpsRdo(pos, mv, ma));
}

/*--------------------------------------------------------------------------*/
/* PD：在 Source_Capabilities 里找 **5V Fixed** PDO（PPS 前置用）             */
/*--------------------------------------------------------------------------*/
/* 返回 Object Position??..7），0 = 没找到?? *
 * Fixed PDO 的判定（PD 3.0 §6.4.1）：
 *   bits[31:30] == 0b00 ??Fixed
 *   bits[29:20] = 电压??0mV ??bit??0 bit）→ 5V 就是 100
 *   bits[ 9: 0] = 最大电流（10mA ??bit??0 bit?? *
 * 【为什么要它】源端发??caps 只给 24~30ms ??Request，超时就 Hard Reset
 *   （VBUS 掉到 vSafe0V）—??????PD 源供??的板子等于掉电。所以拿??caps
 *   后第一件事??*先要一??5V Fixed 合约**（不改变电压、必然被接受），把合?? *   先立住；随后再走 PPS 的电压切换。详见主循环 5.5 号块??*/
uint8_t find5vFixed(const FUSB302_Message& m, uint16_t* outMa)
{
  for (uint8_t i = 0; i < m.pdoCount && i < 7u; ++i) {
    const uint32_t raw = m.pdo[i];
    if (((raw >> 30) & 0x3u) != 0x0u) { continue; }              /* 只要 Fixed */
    const uint16_t mv = static_cast<uint16_t>(((raw >> 10) & 0x3FFu) * 50u);
    if (mv != 5000u) { continue; }                               /* 只要 5V ??*/
    if (outMa != nullptr) {
      *outMa = static_cast<uint16_t>((raw & 0x3FFu) * 10u);
    }
    return static_cast<uint8_t>(i + 1u);
  }
  return 0u;
}

/* 组一??**Fixed RDO**（PD 3.0 Table 6-12）：
 *
 *   bits[31:28] Object Position
 *   bits[27:26] 保留???? *   bit [25]    USB Communications Capable   ??0
 *   bit [24]    No USB Suspend               ??0
 *   bit [23]    Unchunked Extended Messages  ??0
 *   bit [22]    Capability Mismatch          ??0
 *   bits[21:20] 保留???? *   bits[19:10] Max Operating Current??0mA ??bit?? *   bits[ 9: 0] Max Current          ??0mA ??bit?? *
 * ⚠️ ??PPS RDO 最大的不同：Fixed **不带电压**（电压由 PDO 档位决定），
 *    所以这里只??要第几个档位 + 电流"??*/
uint32_t buildFixedRdo(uint8_t pos, uint16_t ma)
{
  const uint32_t c = (static_cast<uint32_t>(ma) / 10u) & 0x3FFu;
  return (static_cast<uint32_t>(pos & 0xFu) << 28) | (c << 10) | c;
}

bool sendFixedRequest(FUSB302& pd, uint8_t pos, uint16_t ma)
{
  if (pos == 0u) { return false; }
  pd.clearLastPeerMsgType();
  return pd.sendRequest(buildFixedRdo(pos, ma));
}

/*--------------------------------------------------------------------------*/
/* 按键：ISR 只置标志；采样 / 去抖 / 单-长-双击判定全部在这里（主循环）    */
/*--------------------------------------------------------------------------*/
/*==========================================================================
 * 按键手势（单键三手势，与参考工程 PPS_Req/pps_key.cpp 一致）
 *   单击          → 切换选中行（电压 ↔ 电流）
 *   长按 ≥200ms   → 判定成立立即步进一次，之后每 200ms 连发
 *   双击          → 轮换选中行的步长
 * 单击/双击靠"抬起后再等 400ms"消歧 —— 单击最多 400ms 延迟（可接受）。
 * 长按与双击都会置"本次按下已消费"，抬起时不再补发单击。
 *==========================================================================*/
struct KeyState {
  uint8_t  phase     = 0u;    /* 0=Idle 1=Down 2=Wait2nd */
  uint8_t  cand      = 0u;    /* 去抖候选电平（1=按下） */
  uint8_t  level     = 0u;    /* 去抖后稳定电平（1=按下） */
  uint8_t  prev      = 0u;
  uint32_t sampleAt  = 0u;
  uint32_t candAt    = 0u;
  uint32_t downAt    = 0u;
  uint32_t repeatAt  = 0u;
  uint32_t upAt      = 0u;
  bool     longFired = false; /* 本次按下已触发过长按 */
  bool     consumed  = false; /* 本次按下已被消费（长按/双击）→ 抬起不再补单击 */
};

e_Key KeyService(KeyState& s, uint32_t now)
{
  /* ---- 10ms 采样 + 30ms 去抖 → s.level（低有效：按下 = 1） ---- */
  if ((now - s.sampleAt) >= KEY_SAMPLE_MS) {
    s.sampleAt = now;
    const uint8_t raw =
        (LL_GPIO_IsInputPinSet(KEY_INT_Port, KEY_INT_Pin) == 0u) ? 1u : 0u;
    if (raw != s.cand) { s.cand = raw; s.candAt = now; }
    else if ((now - s.candAt) >= KEY_DEBOUNCE_MS) { s.level = s.cand; }
  }

  /* ---- 按下 / 抬起沿 ---- */
  if ((s.level != 0u) && (s.prev == 0u)) {
    if (s.phase == 2u) {
      /* 第二下 → 双击：切步长；本次按下不再产生单击 */
      s.consumed  = true;
      s.phase     = 1u;
      s.downAt    = now;
      s.longFired = false;
      s.prev      = s.level;
      return KEY_CYCLE;
    }
    s.consumed  = false;
    s.phase     = 1u;
    s.downAt    = now;
    s.longFired = false;
  } else if ((s.level == 0u) && (s.prev != 0u)) {
    if (s.phase == 1u) {
      if (s.consumed || s.longFired) { s.phase = 0u; }   /* 长按/双击已处理 */
      else                           { s.phase = 2u; s.upAt = now; }
    }
  }
  s.prev = s.level;

  /* ---- 长按：判定成立立即步进一次，之后每 KEY_REPEAT_MS 再步进一次 ---- */
  if ((s.phase == 1u) && (s.level != 0u)) {
    if (!s.longFired && ((now - s.downAt) >= KEY_LONG_MS)) {
      s.longFired = true;
      s.repeatAt  = now;
      return KEY_STEP;
    }
    if (s.longFired && ((now - s.repeatAt) >= KEY_REPEAT_MS)) {
      s.repeatAt = now;
      return KEY_STEP;
    }
  }

  /* ---- 单击挂起超时 → 判定为单击（切行） ---- */
  if ((s.phase == 2u) && ((now - s.upAt) > KEY_DOUBLE_MS)) {
    s.phase = 0u;
    return KEY_SELECT;
  }
  return KEY_NONE;
}

/*==========================================================================
 * ??I²C 总线自救：解开 FUSB302 卡住 SDA 的死?? *
 * 【症??/ 为什么会卡?? *   FUSB302 若在"某个 bit ??被打断（最典型：MCU 正在读它时被复位 / 重烧固件），
 *   它会一直把 SDA 拉低等下一??SCL。同??I2C 外设 SR2.BUSY 也锁死，库里?? *   waitBusFree()/waitFlag() 全部超时 ??init() 永远失败 ??pd.isReady() ??0?? *
 * 【怎么救?? *   ??SCL 当普??GPIO 敲最??9 个时钟，把从设备??卡住??bit"顶上总线?? *   再补一??STOP，然后把引脚/外设还给 I2C1?? *   · 只在真的卡住时动手（SDA ????BUSY）—??否则无害早退?? *   · 不会和正常事务撞车：I²C 事务是同步的，主循环在跑就说明没有事务在途?? *==========================================================================*/
void FusbBusRecover()
{
  /* 早退：总线空闲（SDA ??????BUSY）就什么都不做 */
  if ((LL_GPIO_IsInputPinSet(GPIOF, LL_GPIO_PIN_0) != 0u) &&
      (LL_I2C_IsActiveFlag_BUSY(I2C1) == 0u)) {
    return;
  }

  LL_I2C_Disable(I2C1);

  LL_GPIO_InitTypeDef gi = {0};

  /* SDA(PF0)：输??+ 上拉，只观察 */
  gi.Pin        = LL_GPIO_PIN_0;
  gi.Mode       = LL_GPIO_MODE_INPUT;
  gi.Pull       = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOF, &gi);

  /* SCL(PF1)：通用开漏输出，先释放为??*/
  gi.Pin        = LL_GPIO_PIN_1;
  gi.Mode       = LL_GPIO_MODE_OUTPUT;
  gi.Pull       = LL_GPIO_PULL_UP;
  gi.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
  gi.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  LL_GPIO_Init(GPIOF, &gi);
  LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_1);

  /* 敲最??9 ??SCL??4MHz 下每个沿停几??µs，够慢） */
  for (uint8_t i = 0u; i < 9u; ++i) {
    LL_GPIO_ResetOutputPin(GPIOF, LL_GPIO_PIN_1);
    for (volatile uint32_t d = 0u; d < 300u; ++d) { }
    LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_1);
    for (volatile uint32_t d = 0u; d < 300u; ++d) { }
    if (LL_GPIO_IsInputPinSet(GPIOF, LL_GPIO_PIN_0) != 0u) { break; }  /* SDA 已释??*/
  }

  /* 补一??STOP：SCL 高时 SDA 低→??*/
  gi.Pin        = LL_GPIO_PIN_0;
  gi.Mode       = LL_GPIO_MODE_OUTPUT;
  gi.Pull       = LL_GPIO_PULL_UP;
  gi.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  LL_GPIO_Init(GPIOF, &gi);
  LL_GPIO_ResetOutputPin(GPIOF, LL_GPIO_PIN_0);      /* SDA ??*/
  LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_1);        /* SCL ??*/
  for (volatile uint32_t d = 0u; d < 300u; ++d) { }
  LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_0);        /* SDA ??= STOP */
  for (volatile uint32_t d = 0u; d < 300u; ++d) { }

  /* 引脚 + 外设还给 I2C1（Studio_I2C1_Init 会重置外设并??PF0/PF1 重配??AF12??*/
  Studio_I2C1_Init();
}

} // namespace
/* USER CODE END 0 */

/*==========================================================================*/
/* 应用主循环（永不返回??                                                   */
/*==========================================================================*/
/* USER CODE BEGIN ExternalFunctions */

/* ??必须显式 extern "C"：main.c ??C，App_Run ??C++ TU 里的定义?? *   少了它，两边名字修饰不一????链接??"Undefined symbol App_Run"??*/
extern "C" void App_Run(void)
{
  using namespace LoveFinderLib;

  FUSB302         pd;
  FUSB302_Message msg;
  FUSB302_Message lastMsg = {};

  bool haveCap  = false;
  bool attached = false;
  bool doReset  = true;      /* 只有开机那??init 允许软复??*/
  uint8_t devId = 0u;        /* ??缓存：deviceId() 内部要读一??I²C??                              *   绝不能每轮主循环都调（会白烧一条事??轮）??*/

  bool     armed    = false;                 /* 已进??PD 业务 */
  bool     toggleOn = false;                 /* startSinkToggle 是否已调??*/

  /* ---- PPS 合约状??---- */
  bool     havePps  = false;                 /* caps 里有 PPS APDO */
  uint8_t  ppsPos   = 0u;                    /* ??APDO ??Object Position */
  uint16_t ppsMinV  = 0u;                    /* APDO 最小电压（mV??*/
  uint16_t ppsMaxV  = 0u;                    /* APDO 最大电压（mV??*/
  uint16_t ppsMaxI  = 0u;                    /* APDO 最大电流（mA??*/
  uint16_t tgtV     = 0u;                    /* 目标电压（用户可步进??*/
  uint16_t tgtI     = 0u;                    /* 目标电流（用户可步进??*/
  uint16_t sentV    = 0u;                    /* 最近一??Request 里的电压 */
  uint16_t sentI    = 0u;                    /* 最近一??Request 里的电流 */
  uint16_t okV      = 0u;                    /* 最近一??PS_RDY 确认的电??*/
  uint16_t okI      = 0u;                    /* 最近一??PS_RDY 确认的电??*/
  bool     needRdo  = false;                 /* 目标值变??/ 收到 caps ??该发 Request */
  bool     rdoPend  = false;                 /* 有一??Request 在途，等应??*/
  uint32_t tRdoSent = 0u;                    /* 发出那条 Request 的时刻（超时用） */
  uint32_t tKeepAlive = 0u;                  /* 上一次发??RDO 的时刻（保活心跳计时基准??*/
  uint32_t keepAliveCnt = 0u;                /* 保活心跳累计次数（诊断） */
  bool     ppsOk    = false;                 /* 至少收到过一??PS_RDY */
  uint8_t  selBand  = 0u;                    /* 当前选中带：0=电压 / 1=电流 */

  /* ---- ??PPS 前置：先建立一??5V Fixed 合约（防??悬空"被源??HRST??---
   *   拿到 caps 后先??Fixed 5V Request ??收到 PS_RDY（fixedOk）→ 再发 PPS Request??   *   这样源端??tSenderResponse 窗口??*永远有一条我方报??*，不??Hard Reset??*/
  uint8_t  fixedPos  = 0u;                   /* caps ??5V Fixed PDO ??Object Position */
  uint16_t fixedMa   = 0u;                   /* ??PDO 的最大电流（mA），??Fixed RDO */
  bool     needFixed = false;                /* 该发一??Fixed Request */
  bool     fixedPend = false;                /* 有一??Fixed Request 在途，等应??*/
  bool     fixedOk   = false;                /* 已收??5V Fixed 合约??PS_RDY */
  uint32_t tFixedSent = 0u;                  /* 发出那条 Fixed Request 的时刻（超时用） */

  uint8_t  st      = ST_WAIT_ATTACH;

  uint32_t tAttach = 0u, tRx = 0u, tRetry = 0u, tUi = 0u, tInt = 0u;
  uint32_t tVbusLow = 0u, tDetach = 0u, tHrRecover = 0u, tRecover = 0u, tCcGone = 0u;
  bool     ccSeenOk = false;                 /* 挂上后是否见??BC_LVL != 0（快通道自检??*/
  uint32_t loops = 0u;
  uint32_t tCapWait = 0u;                    /* 进入 ST_WAIT_CAP 的时刻（Soft Reset 计时基准??*/
  uint8_t  softRstSent = 0u;                 /* 本轮已补过几??Soft Reset */
  uint8_t  prevSt = ST_WAIT_ATTACH;          /* 上一轮状态：用来捕获"刚进??ST_WAIT_CAP"这个??*/
  uint8_t  hrTries = 0u;                     /* Hard Reset 恢复窗口内已补过几次 recover */
  uint32_t hrstCnt = 0u;                     /* ??累计收到??Hard Reset 次数（长窗口看门狗判据） */
  uint32_t tCapSeen = 0u;                    /* 待机期间最近一次读??caps 的时刻（0=还没读到??*/

  KeyState key;

  /* ---- UI 差分缓存由 DrawRow()/UiReset() 内部维护 ---- */
  uint8_t  stepVIdx = 0u;                    /* 电压步长索引（双击轮换） */
  uint8_t  stepIIdx = 0u;                    /* 电流步长索引（双击轮换） */

  /* 诊断累加??*/
  uint8_t  intaAcc = 0u, int0Acc = 0u;
  uint8_t  lastS1 = 0u, lastS0a = 0u, lastI0 = 0u, lastVbusOk = 0u, lastCcOk = 0u;
  uint32_t keySelect = 0u, keyStep = 0u;
  uint32_t hbHits = 0u;                      /* 心跳顶事次数（正常应恒为 0??*/

  /* ??开??-------------------------------------------------------------- */
  g_log_t0 = BSP_GetTick();
  Log(L_BOOT, 0, 0);

  /* 先把屏点亮：整板由 DAPLink 3V3 供（不受 PD VBUS 影响），屏驱可放在 PD 之前。 */
  ST7735_Init();
  /* 首屏：无横幅，两行铺满 160×80（与参考工程 PPS_Req 的两行版式一致）。
   * 此时还没 caps，先按默认量程画一帧；收到 caps 后差分刷新自动更新。 */
  UiInitScreen();
  DrawRow(0u, PPS_DEF_MV, 3300u, 21000u, kStepV[0], true,  0u, false);
  DrawRow(1u, PPS_DEF_MA, 100u,  5000u,  kStepI[0], false, 0u, false);
  Log(L_LCD, 0, 0);

  /* ??配好 FUSB302，并**立刻补一??Soft Reset** 清掉上一轮电源合约??   *
   * 【为什么必须发】FUSB302 由板??3V3 常供 —??**MCU 复位并不会让它掉??*??   *   Switches0 里的 Rd、Switches1 ??TXCC、以??上一轮谈成的合约"全部原样保留??   *   源端那边同样不知道我们复位过，于是它认为"SINK 还挂着、旧合约仍然有效"??   *   不会再广??Source_Capabilities —??结果输出一直停在上一轮协商的电压上??   *   发一??Soft Reset，让源端把协议层和电源合约一起清零，退??vSafe5V
   *   并重新广??caps??*/
  FusbBusRecover();             /* ??先解 I²C 死锁，否则下??init 必失??*/
  if (pd.init(I2C1)) {
    Log(L_INIT, 1, 0);
    devId = pd.deviceId();
    Log(L_DEVID, devId, 0);
  } else {
    Log(L_INIT, 0, 0);
  }
  doReset = false;              /* 之后的重试绝不再软复位（会撤 Rd ??变成拔出??*/
  tRetry  = BSP_GetTick();

  if (pd.isReady()) {
    /* ★★ AUTO_CRC 直接常开 ★★
     *   硬件在规??tTransmit(??95µs) 内确??ACCEPT ??Source_Capabilities??     *   主循??3 号块一收到 caps ??*立刻**??Request??4~30ms 窗口绰绰有余??*/
    (void)pd.setAutoCrc(true);
    Log(L_CRC, 1, 0);

    toggleOn = pd.startSinkToggle();
    Log(L_TOGGLE, toggleOn ? 1u : 0u, 0);

    const uint32_t tBoot = BSP_GetTick();
    e_FUSB302_CC   cc    = e_FUSB302_CC::NONE;
    while ((BSP_GetTick() - tBoot) < PD_BOOT_ATTACH_MS) {
      cc = pd.pollAttach();
      if (cc != e_FUSB302_CC::NONE) { break; }
    }

    if (cc != e_FUSB302_CC::NONE) {
      const bool sel = pd.selectCC(cc);
      attached = true;              /* ??attach ??主循环不必再挂一??*/
      Log(L_ATTACH, static_cast<uint8_t>(cc), 0);
      Log(L_SELCC, sel ? 1u : 0u, pd.specRev());

      /* ??Soft Reset 清旧合约（AUTO_CRC 已常开，源端回??ACCEPT 由硬件确认）??       * ★★ 这里**绝对不能** resetMsgId() ★★
       *   sendControl(SR) 已经用掉 msgId 0 并把计数推到 1；再清零会让随后??       *   Request ??msgId 0，而源端刚收过 msgId 0 ??SR，期望的??**1**
       *   ??判为乱序 ??**不回 GoodCRC** ??源端静等 tSenderResponse 超时
       *   ??Hard Reset。实测证据见 FIX ??app.cpp 的文件头铁律⑤??*/
      (void)pd.rxFlush();           /* 先清掉复位前残留??caps，别顶掉新一轮的 */
      const bool srOk = pd.sendControl(Bit::PD_CTRL_SOFT_RESET);
      Log(L_SOFTRST, srOk ? 1u : 0u, 0);

      /* 源端收到 Soft Reset 会先??VBUS 撤低再重建，开恢复窗口别当拔出??       * 顺带：这个窗口也压住 3.5 号块，别在开??SR 还没走完时就抢着补枪??*/
      tHrRecover = BSP_GetTick();
      tRecover   = tHrRecover;
      hrTries    = 0u;
    } else {
      Log(L_ATTACH, 0, 0);          /* arg0=0：开机探测没等到 attach（没插线？） */
    }
  }

  /* ??不等按键 —??开机这??SR 之后**立刻把这一轮协商谈??*??   *     attach ??收到 caps ????PPS APDO ????Programmable RDO ??ACCEPT ??PS_RDY??   *     屏上随即显示双进度条??*/
  armed       = true;
  st          = attached ? ST_WAIT_CAP : ST_WAIT_ATTACH;
  tCapWait    = BSP_GetTick();
  softRstSent = 0u;
  Log(L_ARM, 0, 0);

  /* ??主循??------------------------------------------------------------ */
  for (;;) {
    const uint32_t now = BSP_GetTick();
    loops++;

    /* ??取走中断标志 —??必须在所有服务分支之前??     * 顺序??先清再服??：服务期??ISR 又置起来的那一次不会被抹掉??     * 下一轮立刻再跑一遍??     * g_pdIrq 只在 ISR 里置 1、只在主循环里清 0，是标准的单写??单读者握手；
     * Cortex-M0+ ??8 ??volatile ??写本身原子，不用关中断??*/
    const bool irqSet = (g_pdIrq != 0u);
    if (irqSet) { g_pdIrq = 0u; }
    if (g_keyIrq != 0u) { g_keyIrq = 0u; }

    /* ---------------- 1. FUSB302 初始化（失败则周期重试） ---------------- */
    if (!pd.isReady() && ((now - tRetry) >= PD_CFG_RETRY_MS)) {
      tRetry = now;
      FusbBusRecover();         /* ??重试前先??I²C 死锁（空闲时无害早退??*/
      if (pd.init(I2C1, FUSB302_I2C_ADDR, doReset)) {
        devId = pd.deviceId();
        Log(L_INIT, 1, 0);
        (void)pd.setAutoCrc(true);
      } else {
        Log(L_INIT, 0, 0);
        haveCap = false;
      }
      UiReset();         /* 强制重画 */
    }

    /* ---------------- 2. attach + VBUS 时间型去??----------------
     * ??触发源：中断（irqSet）或 PD_ATTACH_POLL_MS 心跳，两者取或??*/
    if (armed && pd.isReady() && (irqSet || ((now - tAttach) >= PD_ATTACH_POLL_MS))) {
      tAttach = now;

      /* 第一次进来：??Rd（toggle）。源端看??sink attach 才会广播 caps??*/
      if (!toggleOn) {
        toggleOn = pd.startSinkToggle();   /* 失败则下轮重??*/
        Log(L_TOGGLE, toggleOn ? 1u : 0u, 0);
      }

      uint8_t s0 = 0u;
      const bool busOk = pd.readReg8(Reg::STATUS0, s0);
      const bool vbusOk = busOk && ((s0 & Bit::S0_VBUSOK) != 0u);
      const bool vbusLowNow = busOk && !vbusOk;
      if (busOk) { lastVbusOk = vbusOk ? 1u : 0u; }

      /* ??CC 终结（BC_LVL）跟??—??拔出快通道的输入??       *   ⚠️ **PD 突发期间（ACTIVITY=1）整段跳??*：测量块被收发占用，
       *      BC_LVL 会假??0（实??853ms STATUS0=0xd0??ms 后恢??0x93）??*/
      const bool ccMeasurable = ((s0 & S0_ACTIVITY) == 0u);
      if (busOk && ccMeasurable) {
        const uint8_t ccOk = ((s0 & S0_BC_LVL_MASK) != 0u) ? 1u : 0u;
        if (ccOk != 0u) { ccSeenOk = true; }   /* 快通道自检：见??有终??才敢??没终?? */
        if (ccOk != lastCcOk) {
          lastCcOk = ccOk;
          Log(L_CC, ccOk, s0);
        }
      }

      /* Hard Reset 恢复窗口：这??VBUS 低是**预期**的（源端要先撤到 vSafe0V
       * 再重??vSafe5V，实??~0.9s），绝不能判??拔出"??*/
      const bool inHrRecover =
          (tHrRecover != 0u) && ((now - tHrRecover) < PD_HR_RECOVER_MS);

      /* ??拔出快通道的计时：CC 终结丢了 = 源端 Rp 没了 = 真拔出??       *   **四道守卫，缺一不可**??       *   ??vbusOk —??HR 会把 VBUS 拉低，那种情况留给下面的 VBUS 路径??       *   ??ccSeenOk —??selectCC 刚生效时 BC_LVL 还没稳定，防插上瞬间误判??       *   ??ccMeasurable（ACTIVITY == 0）—??PD 突发期间 BC_LVL 会假??0??       *   ??PD_DETACH_CC_MS(20ms) 去抖 —??滤掉单次采样抖动??       *   ??BC_LVL 全程??0（该配置下测不出终结），快通道自动失效 ??退??VBUS 路径??*/
      const bool ccGone = busOk && vbusOk && ccSeenOk && ccMeasurable &&
                          ((s0 & S0_BC_LVL_MASK) == 0u);
      if (ccGone) {
        if (tCcGone == 0u) { tCcGone = now; }
      } else {
        tCcGone = 0u;
      }

      if (vbusLowNow) {
        if (tVbusLow == 0u) { tVbusLow = now; }
      } else if (tVbusLow != 0u) {
        /* 掉过又回??= 源端重启过（Soft/Hard Reset 都会这样）→ PD_RESET 复活??         * 实测 FUSB302 收到 Hard Reset ??Status1.RX_EMPTY 恒为 1??         * 不复位就永远收不到后面的 caps??*/
        tVbusLow = 0u;
        if (attached) {
          const bool ok = pd.recover();
          Log(L_RECOVER, ok ? 1u : 0u, static_cast<uint8_t>(pd.specRev()));
          tRecover = now;
          if (!haveCap) { st = ST_WAIT_CAP; }
          UiReset();
        }
      }

      if (!attached) {
        /* 拔出后静??PD_DETACH_HOLD_MS 才允许重挂，压住自激 */
        const bool holdOver =
            (tDetach == 0u) || ((now - tDetach) >= PD_DETACH_HOLD_MS);
        if (holdOver) {
          const e_FUSB302_CC cc = pd.pollAttach();
          if (cc != e_FUSB302_CC::NONE) {
            const bool sel = pd.selectCC(cc);
            /* ★★ 运行时挂载也必须打开硬件 AUTO_CRC ★★
             *   若源端是**开机之??*才插上的，开??init 留下的配置可能不是常开??             *   芯片就会"只收不回" ??源端每条报文都等不到 GoodCRC ??             *   ACCEPT×3 ??SOFT RESET×3 ??HRST，每 ~1s 一轮死循环，屏上永??0 PDO??*/
            (void)pd.setAutoCrc(true);
            Log(L_CRC, 1, 0);
            attached = true;
            tDetach  = 0u;
            tVbusLow = 0u;
            tCcGone  = 0u;
            ccSeenOk = false;   /* 快通道自检：重新等一??BC_LVL != 0 */
            st       = ST_WAIT_CAP;
            haveCap  = false;
            havePps  = false;
            ppsPos   = 0u;
            ppsOk    = false;
            needRdo  = false;
            rdoPend  = false;
            tKeepAlive = 0u;
            selBand  = 0u;
            fixedPos = 0u; fixedMa = 0u;
            needFixed = false; fixedPend = false; fixedOk = false;
            UiReset();
            Log(L_ATTACH, static_cast<uint8_t>(cc), 0);
            Log(L_SELCC, sel ? 1u : 0u, pd.specRev());
          }
        }
      } else if (!inHrRecover &&
                 (((tVbusLow != 0u) && ((now - tVbusLow) >= PD_DETACH_MS)) ||
                  ((tCcGone  != 0u) && ((now - tCcGone)  >= PD_DETACH_CC_MS)))) {
        /* 真拔出，两条路任取其一??         *   ??CC 终结丢失 PD_DETACH_CC_MS —??**快通道**（实测比 VBUS ??~2.9s）；
         *   ??VBUS 连续??PD_DETACH_MS   —??慢通道，兜底（例如源端只给 VBUS）??*/
        const bool fast = (tCcGone != 0u) && ((now - tCcGone) >= PD_DETACH_CC_MS);
        tVbusLow = 0u;
        tCcGone  = 0u;
        attached = false;
        haveCap  = false;
        havePps  = false;
        ppsPos   = 0u;
        ppsOk    = false;
        needRdo  = false;
        rdoPend  = false;
        tKeepAlive = 0u;
        selBand  = 0u;
        fixedPos = 0u; fixedMa = 0u;
        needFixed = false; fixedPend = false; fixedOk = false;
        st       = ST_WAIT_ATTACH;
        tDetach  = now;
        (void)pd.startSinkToggle();
        UiReset();
        Log(L_DETACH, fast ? 1u : 0u, s0);   /* arg0: 1=CC 快通道 / 0=VBUS 慢通道 */
      }

      /* ??Hard Reset 之后 FUSB302 ??PD 接收逻辑可能锁死（RX_EMPTY 恒为 1）??       *   pd.recover() 等价于写一??Reset.PD_RESET??       *   ⚠️ 实测有时一次不够，所以在恢复窗口内按 PD_RECOVER_RETRY_MS 重复??—??       *      ??*必须封顶**（这??3 次）：recover() 会重??CC 路由 + 重配接收前端??       *      ??2.5s 窗口里无脑重发反而会把刚建立的接收状态反复打断??       *   ??而且**一旦重新拿??caps（haveCap）就立刻??*??*/
      if (attached && inHrRecover && !haveCap && (hrTries < PD_HR_RECOVER_MAX) &&
          ((now - tRecover) >= PD_RECOVER_RETRY_MS)) {
        tRecover = now;
        hrTries++;
        const bool ok = pd.recover();
        Log(L_RECOVER, ok ? 1u : 0u, hrTries);
      }
    }

    /* ---------------- 3. RX FIFO —????Source_Capabilities ----------------
     * ??触发源：中断（irqSet）或心跳，两者取或??     *   协商期间（还??caps）心跳保??5ms —??那段时间源端只等 24~30ms 就要
     *   Request；拿??caps 之后放宽??100ms??*/
    const uint32_t rxHb = haveCap ? PD_RX_IDLE_MS : PD_RX_POLL_MS;
    if (armed && pd.isReady() && (irqSet || ((now - tRx) >= rxHb))) {
      tRx = now;
      if (!irqSet) { hbHits++; }

      uint8_t    s1r  = 0u;
      const bool s1Ok = pd.readReg8(Reg::STATUS1, s1r);
      if (s1Ok) { lastS1 = s1r; }

      /* RX_EMPTY(bit5)=1 表示 FIFO 空。读失败时按"??处理 —??一??I²C 抖动
       * 不该让我们去啃一个可能不完整??FIFO??*/
      const bool rxEmptyNow = (!s1Ok) || ((s1r & Bit::S1_RX_EMPTY) != 0u);

      if (!rxEmptyNow) {
        /* ??true：上面那??STATUS1 读已确认 FIFO 非空，而且就在**同一次循??         * 里、只隔了几十微秒**，让库跳过它内部那次重复??rxEmpty()??*/
        const bool gotCap = pd.pollSourceCapabilities(msg, true);

        if (gotCap) {
          /* ★★??收到 Source_Capabilities ★★??           *   【为什么必须立刻回 Request】源端发??caps、并收到我们硬件自动回的
           *   GoodCRC 之后，会启动 tSenderResponse（规??24~30ms，实??SW3526S
           *   ??8.7ms）等 SINK ??Request；超时没等到 ??源端直接 Hard Reset??           *   ??VBUS 拉低重启??           *   【为什么不??内容签名去重"】源??*重播同一??caps** 恰恰??上一??           *   Request 没被受理"的信号，此时必须重发；被签名闸门挡住就等于永??           *   不回话，只能等它 HR。所以这??*收到 caps 就回 Request**??*/
          lastMsg = msg;
          haveCap = true;
          /* ⚠️ msg.specRev ??FUSB302_Message ??*字段**；pd.specRev() 才是方法??           *   这一句同时更??m_rev(钳到 2.0，喂自动 GoodCRC) ??           *   m_hdrRev(允许 3.0，喂我们发出去的报文) —??PPS ??Request 报头
           *   就靠它变??Rev3.0。见 FUSB302.hpp 的说明??*/
          pd.setSpecRev(msg.specRev);
          Log(L_CAP, msg.pdoCount, msg.specRev);
          Log(L_DBG, msg.msgType, (uint8_t)(0x80u | (msg.msgId & 0x07u)));
          /* ??取证：把??4 ??PDO 的最高字节记下来 —??bits[31:30] 就是 PDO 类型
           *   ??1 = APDO/PPS??0 = Fixed）。用来确??我们到底在向第几??PDO 要电"??*/
          for (uint8_t k = 0u; k < msg.pdoCount && k < 4u; ++k) {
            Log(L_PDOH, k, static_cast<uint8_t>(msg.pdo[k] >> 24));
          }

          /* ---- ??先找 5V Fixed PDO（PPS 前置??---
           *   拿到 caps 的第一件事不是??PPS，而是先要一??5V Fixed 合约??           *   不改变电压、必然被接受，等??先把合约焊死"，源端就不会悬空??*/
          uint16_t fma = 0u;
          const uint8_t fpos = find5vFixed(msg, &fma);
          if (fpos != 0u) {
            fixedPos = fpos;
            fixedMa  = fma;
            if (!fixedOk) { needFixed = true; fixedPend = false; }
          } else {
            /* caps 里居然没??5V Fixed（极罕见）→ 跳过前置，直接走 PPS??             * 免得把自己憋死在"永远等不??Fixed PS_RDY"上??*/
            fixedOk = true;
          }

          /* ---- ??caps 里找 PPS APDO ---- */
          uint16_t mn = 0u, mx = 0u, mi = 0u;
          const uint8_t pos = findPps(msg, &mn, &mx, &mi);
          if (pos != 0u) {
            const bool first = (!havePps) || (pos != ppsPos);
            havePps = true;
            ppsPos  = pos;
            ppsMinV = mn;
            ppsMaxV = mx;
            ppsMaxI = mi;
            if (first) {
              /* ??PPS 出现后默??5.2V / 2A；上限不足则取上限一半（??ppsDefaultV/I）??*/
              tgtV = ppsDefaultV(mn, mx);
              tgtI = ppsDefaultI(mi);
              okV  = tgtV;
              okI  = tgtI;
              selBand = 0u;
              ppsOk   = false;
              Log(L_PPS, pos, msg.specRev);
            } else {
              /* 源端重播 caps：范围可能变了，把已选值夹回新量程??*保留用户已调的??*??*/
              if (tgtV < ppsMinV) { tgtV = ppsMinV; }
              if (tgtV > ppsMaxV) { tgtV = ppsMaxV; }
              if (tgtI > ppsMaxI) { tgtI = ppsMaxI; }
            }
            /* 收到 caps = 源端在等 Request：无论新旧，都补一条??             * ★★ 但顺序是**??5V Fixed、后 PPS** ★★ —??只有 5V 合约已经立住
             *    （fixedOk）时才置 needRdo；否则先??5.5 号块??Fixed Request??             *    等它??PS_RDY 到了再置 needRdo 开??PPS??*/
            if (fixedOk) { needRdo = true; }
            rdoPend = false;
          } else {
            havePps = false;
            ppsPos  = 0u;
            ppsOk   = false;
            needRdo = false;
            rdoPend = false;
            st      = ST_NO_PPS;
          }
          /* ??不在这里强制整屏重画：屏态键（scrKey）变了会在第 6 块里??整屏重来"??           *   量程/目标值变了也有逐带比对兜底。稳态下重播 caps 不该闪屏??*/
        } else {
          /* 不是 caps：看看是不是对端对我??Request 的应答??           * ACCEPT(0x03) / PS_RDY(0x06) / REJECT(0x04) / WAIT(0x0C) 都是控制报文??           * pollSourceCapabilities() 为了??caps 会把它们取出来丢掉，
           * 所以库??最后一条被跳过??msgType"记在 lastPeerMsgType() 里??           * 读到??clear??消费"掉），否则同一条应答会在后面每一轮里被反复处理??*/
          const uint8_t peer = pd.lastPeerMsgType();
          if (peer != 0u) {
            pd.clearLastPeerMsgType();
            /* ??取证??*任何**被消费掉的对端报文都记一??—??包括我们不认识??             *   只丢弃的类型。用它回??源端到底在发什??（例如它??PPS ??             *   周期性发的到底是 caps、还是别的控??数据报文）??*/
            const bool known = (peer == Bit::PD_CTRL_PS_RDY) ||
                               (peer == Bit::PD_CTRL_ACCEPT) ||
                               (peer == Bit::PD_CTRL_REJECT) ||
                               (peer == Bit::PD_CTRL_WAIT);
            Log(L_PEER, peer, known ? 1u : 0u);
            if (peer == Bit::PD_CTRL_PS_RDY) {
              if (fixedPend) {
                /* ??5V Fixed 合约立住了（这是"安全??）→ 现在才允许发 PPS Request??*/
                fixedPend = false;
                fixedOk   = true;
                if (havePps) { needRdo = true; }
                rdoPend = false;
                st      = ST_PSRDY;
                Log(L_PSRDY, 1, peer);
              } else {
                /* ??电源已切到申请的电压/电流 = PPS 合约生效??                 *   ⚠️ 保活心跳??Hz）会**反复**收到 PS_RDY —??这里**绝不??*??                 *      scrDrawn = 0xFFu，否则每秒整屏刷一次（屏上显示??tgtV/tgtI
                 *      在用户按键那一刻就已经画好了，PS_RDY 只是"确认"，无需重画）??*/
                rdoPend = false;
                ppsOk   = true;
                okV     = sentV;
                okI     = sentI;
                st      = ST_PSRDY;
                Log(L_PSRDY, 1, peer);
              }
            } else if (peer == Bit::PD_CTRL_ACCEPT) {
              /* 只是受理了，电源还没??—??继续??PS_RDY??*/
              st = ST_ACCEPTED;
              Log(L_ACCEPT, 0, peer);
            } else if (peer == Bit::PD_CTRL_REJECT) {
              if (fixedPend) {
                /* 5V Fixed 被拒（理论上不该发生）→ 放弃前置、直接走 PPS??                 * 免得卡在"永远等不??Fixed PS_RDY"??*/
                fixedPend = false;
                fixedOk   = true;
                needRdo   = havePps;
                rdoPend   = false;
                st        = ST_REJECTED;
                Log(L_FIXED, 0, fixedPos);
              } else {
                /* PPS Request 被拒（例如电压越界）：退回上一次成功的值??                 * 目标值回退后，逐带比对会自然发??tgtV/tgtI 变了并重画那一条带??                 * 所以这里也不需要强制整屏重画??*/
                rdoPend = false;
                needRdo = false;
                tgtV    = okV;
                tgtI    = okI;
                st      = ST_REJECTED;
              }
              Log(L_PSRDY, 0, peer);
              Log(L_PERR, 1, peer);
            } else if (peer == Bit::PD_CTRL_WAIT) {
              /* Wait = 源端还要点时间，随后它会??PS_RDY ??REJECT??               * 保持 rdoPend 不变，继续等??*/
              st = ST_REJECTED;
              Log(L_PSRDY, 0, peer);
              Log(L_PERR, 2, peer);
            }
          }
        }
      }
    }

    /* ---------------- 3.5 ??久等不到 caps ??主动??Soft Reset 叫醒源端 ----------------
     *   源端（SW3526S）看??Rd 之后只广播约 5s ??Source_Capabilities。一旦这??     *   SINK 因为 toggle/attach 慢了一拍错过窗口，源端就彻底安??—??唯一出路??     *   SINK **主动发一??Soft Reset**：源端回 GoodCRC + ACCEPT，随后重??caps??*/
    if (st != prevSt) {
      if (st == ST_WAIT_CAP) { tCapWait = now; softRstSent = 0u; }
      prevSt = st;
    }
    {
      const bool inHrRecover =
          (tHrRecover != 0u) && ((now - tHrRecover) < PD_HR_RECOVER_MS);
      if (armed && pd.isReady() && attached && !haveCap && !inHrRecover &&
          (st == ST_WAIT_CAP) && (tCapWait != 0u) && (softRstSent < PD_SOFTRST_MAX)) {
        const bool srcBroadcasting =
            (tCapSeen != 0u) && ((now - tCapSeen) < 400u);
        const uint32_t waitMs =
            (softRstSent != 0u) ? PD_SOFTRST_RETRY_MS
          : srcBroadcasting     ? PD_SOFTRST_FIRST_MS   /* 还在广播：先??*/
          :                       0u;                   /* 已安静：立刻补枪 */
        if ((now - tCapWait) >= waitMs) {
          tCapWait = now;
          softRstSent++;
          const bool ok = pd.sendControl(Bit::PD_CTRL_SOFT_RESET);
          /* ??同理：Soft Reset **不重??* MessageID，这里什么都不做??*/
          Log(L_SOFTRST, ok ? 1u : 0u, softRstSent);
        }
      }
    }

    /* ---------------- 4. 中断闩锁 + Hard Reset 复活 ----------------
     *   0x3E INTERRUPTA.bit0 I_HARDRST  = **收到 Hard Reset ordered set**
     *                    bit2 I_TXSENT   = 我方报文拿到了对??GoodCRC
     *                    bit4 I_RETRYFAIL= 自动重发 ×3 耗尽
     *                    bit6 I_TOGDONE  = toggle 因检测到设备而停??     *   0x3F INTERRUPTB.bit0 I_GCRCSENT = 芯片回过 GoodCRC
     *   0x42 INTERRUPT  bit4 I_CRC_CHK  = 收到一??CRC 正确的报??     *
     * ★★??0x42 **必须??* ★★??     *   INT_N 是三??R/C 寄存器按 Mask 门控之后????。Mask1 ??M_CRC_CHK ??     *   放开的，也就??I_CRC_CHK(0x42.bit4) 能拉??INT_N。只??0x3E/0x3F 的话??     *   第一包有效报文一到，0x42 置位、INT_N 拉低，然??*永远清不??* —??     *   引脚就永久趴在低电平上，表现??只来一次边????*/
    if (pd.isReady() && (irqSet || ((now - tInt) >= PD_INT_POLL_MS))) {
      tInt = now;

      /* 一??7 字节突发读，替掉??4 次读（数据手??Figure 14 支持连续读）??       *   st[0] 0x3C STATUS0A  st[1] 0x3D STATUS1A  st[2] 0x3E INTERRUPTA
       *   st[3] 0x3F INTERRUPTB st[4] 0x40 STATUS0  st[5] 0x41 STATUS1
       *   st[6] 0x42 INTERRUPT
       * 0x3E/0x3F/0x42 ??R/C —??突发读里每读到一个字节就等于清掉它??*/
      uint8_t st7[7] = {0, 0, 0, 0, 0, 0, 0};
      (void)pd.readBytes(Reg::STATUS0A, st7, 7u);

      const uint8_t ia = st7[2];   /* INTERRUPTA */
      const uint8_t ib = st7[3];   /* INTERRUPTB */
      const uint8_t s0 = st7[4];   /* STATUS0 */
      const uint8_t i0 = st7[6];   /* INTERRUPT (0x42) */
      lastS0a = st7[0];
      lastI0  = i0;

      if (ia != 0u) {
        intaAcc |= ia;
        Log(L_INTA, ia, 0);

        /* ??收到对端 Hard Reset ??必须??Reset.PD_RESET 复活 FUSB302??         * ??INTERRUPTA.bit0（I_HARDRST）而不??Status0a.HARDRST??         * 前者是 R/C ??**天然就是边沿检??*??*/
        if (attached && ((ia & Bit::IA_HARDRST) != 0u)) {
          hrstCnt++;
          Log(L_HRST, 1, 0);
          tHrRecover = now;
          tRecover   = now;
          hrTries    = 0u;
          (void)pd.recover();
          if (!haveCap) { st = ST_WAIT_CAP; }
          haveCap = false;
          havePps = false;
          ppsPos  = 0u;
          ppsOk   = false;
          needRdo = false;
          rdoPend = false;
          tKeepAlive = 0u;
          fixedPos = 0u; fixedMa = 0u;
          needFixed = false; fixedPend = false; fixedOk = false;
          /* 清掉 haveCap/havePps 后屏态键必然变化????），??6 块会走整屏重来，
           * 这里无需再强制重画??*/
        }
      }

      /* ★★ 源端插拔检??—??**中断驱动** ★★
       *   I_VBUSOK(0x42.bit7) 就是"源端????的权威事件??       *   ⚠️ 源端 Soft/Hard Reset ??*主动**??VBUS 拉低??0.9s 再重??—??       *      那是预期行为，绝不能当成拔出??真拔????2 号块做时间型去抖判定??*/
      if ((i0 & Bit::I_VBUSOK) != 0u) {
        const uint8_t vbus = ((s0 & Bit::S0_VBUSOK) != 0u) ? 1u : 0u;
        if (vbus != lastVbusOk) {
          lastVbusOk = vbus;
          const uint8_t flags = static_cast<uint8_t>(
              (attached ? 0x01u : 0u) | (vbus << 1) |
              ((attached || (vbus != 0u)) ? 0x04u : 0u));
          Log(L_SRC, flags, s0);
        }
      }

      if (ib != 0u) { Log(L_INTB, ib, 0); }
      /* 0x42 的记录策略：先剔??I_CRC_CHK 再入日志（它每收到一包有效报文就
       * 置位一次，日志环形区只??64 条，记它会把真正有用的记录全冲掉）??*/
      const uint8_t i0log = static_cast<uint8_t>(i0 & static_cast<uint8_t>(~Bit::I_CRC_CHK));
      if (i0log != 0u) {
        int0Acc |= i0log;
        Log(L_INT0, i0log, 0);
      }
    }

    /* ---------------- 5. 按键（单击切行 / 长按步进 / 双击切步长） ----------------
     *   · 单击 = 切换选中行（只改高亮，**不发** Request）；
     *   · 长按 = 按当前步长步进选中行的值（首拍 200ms，之后每 200ms 一拍）；
     *   · 双击 = 轮换选中行的步长（V:20/50/100/500mV，A:50/100/200/500mA）。
     *   改完由 5.5 号块下发新的 Programmable RDO。 */
    const e_Key ev = KeyService(key, now);
    if (ev == KEY_SELECT) {
      keySelect++;
      selBand = static_cast<uint8_t>(selBand ^ 1u);
      Log(L_BTN, 1, selBand);
    } else if (ev == KEY_STEP) {
      keyStep++;
      if (havePps) {
        if (selBand == 0u) {
          /* 电压：按步长抬一格；越过上限 → 回绕到最小值 */
          uint32_t v = static_cast<uint32_t>(tgtV) + kStepV[stepVIdx & 3u];
          if (v > ppsMaxV) { v = ppsMinV; }
          if (v < ppsMinV) { v = ppsMinV; }
          tgtV = static_cast<uint16_t>(v);
        } else {
          /* 电流：按步长抬一格；越过上限 → 回绕到下限（100mA） */
          uint32_t i = static_cast<uint32_t>(tgtI) + kStepI[stepIIdx & 3u];
          if (i > ppsMaxI) { i = 100u; }
          tgtI = static_cast<uint16_t>(i);
        }
        needRdo = true;
      }
      Log(L_BTN, 2, selBand);
    } else if (ev == KEY_CYCLE) {
      if (selBand == 0u) { stepVIdx = static_cast<uint8_t>((stepVIdx + 1u) & 3u); }
      else               { stepIIdx = static_cast<uint8_t>((stepIIdx + 1u) & 3u); }
      Log(L_BTN, 3, selBand);
    }

    /* ---------------- 5.5 ??PPS 下发：把目标电压/电流发成 Programmable RDO ----------------
     *
     * 这一块就??PPS ??Fixed ??*本质区别**：Fixed 只有"换档??才需??Request??     * ??PPS 每改一次电??电流都要再发一??Request ??ACCEPT ??PS_RDY??     *
     * 触发条件：needRdo（目标变??/ 刚收??caps）且**没有在途的 Request**??     *   ⚠️ 不抢发：上一轮还没谈完（??ACCEPT / PS_RDY）时先让它走完，
     *      否则会把源端拖进"应答还没发完又来一??Request"的乱序??     *   ⚠️ 超时兜底：发出后 PPS_RDO_TIMEOUT_MS 内没等到任何应答，就把在??     *      标记清掉并重??needRdo —??否则一次丢包会把后续所有步进全憋死??     *
     * ??这一块与 3 号块??*同一次循环迭??*里，所??收到 caps ????Request"
     *   的延迟是微秒级，远小??tSenderResponse(24~30ms)??*/
    if (armed && pd.isReady()) {
      /* ===== ??5.5a PPS 前置：先??5V Fixed 合约 =====
       *   拿到 caps ??*第一件事**是发一??5V Fixed Request（不改变电压??       *   必然被接受），把合约先立住；随后才走 PPS 的电压切换??       *   目的：源端的 tSenderResponse(24~30ms) 窗口里永远有我方报文??       *   不会因为"悬空"??Hard Reset（HRST 会把 VBUS 拉到 vSafe0V??       *   ??整机??PD 源供??的产??= 掉电重启，致命）??*/
      if (fixedPend && ((now - tFixedSent) >= PPS_RDO_TIMEOUT_MS)) {
        fixedPend = false;                       /* 超时 ??允许重发 */
        needFixed = true;
      }
      if (needFixed && !fixedPend && (fixedPos != 0u)) {
        const bool ok = sendFixedRequest(pd, fixedPos, fixedMa);
        if (ok) {
          fixedPend  = true;
          needFixed  = false;
          tFixedSent = now;
          st         = ST_REQ_SENT;
        }
        Log(L_FIXED, ok ? 1u : 0u, fixedPos);
      }

      /* ===== ??5.5b PPS 主体：等 5V 合约立住（fixedOk）之后才??===== */
      if (fixedOk) {
      /* 超时兜底：发出后 PPS_RDO_TIMEOUT_MS 内没等到任何应答，就把在途标??       * 清掉并重??needRdo —??否则一次丢包会把后续所有步进全憋死??*/
      if (rdoPend && ((now - tRdoSent) >= PPS_RDO_TIMEOUT_MS)) {
        rdoPend = false;
        needRdo = true;
        Log(L_PERR, 3, 0);
      }

      /* ★★ PPS 保活心跳 ★★
       *   合约生效后，??PPS_KEEPALIVE_MS 重发一??*同样**??Programmable RDO??       *   把合??按住"。不发的话源端（SW3526S）~13.2s 后就会自??Hard Reset
       *   （实测见 PPS_KEEPALIVE_MS 上方那段记录），屏上也进不了 "PPS"??       *   逻辑分析仪上看到的就??CC 总线那条周期性的通信??*/
      if (havePps && ppsOk && !rdoPend && !needRdo &&
          ((now - tKeepAlive) >= PPS_KEEPALIVE_MS)) {
        needRdo = true;
        keepAliveCnt++;
        Log(L_KA, static_cast<uint8_t>(keepAliveCnt & 0xFFu), 0);
      }

      if (needRdo && !rdoPend && havePps) {
        const uint32_t rdo = buildPpsRdo(ppsPos, tgtV, tgtI);
        const bool ok = sendPpsRequest(pd, ppsPos, tgtV, tgtI);
        if (ok) {
          const bool valueChanged = (tgtV != sentV) || (tgtI != sentI);
          sentV      = tgtV;
          sentI      = tgtI;
          rdoPend    = true;
          tRdoSent   = now;
          tKeepAlive = now;        /* 心跳计时??真正发出??那一刻起??*/
          needRdo    = false;
          st         = ST_REQ_SENT;
          if (valueChanged) {
            /* ??只在??*变化**时记详细日志 —??1Hz 的保活会??64 条的日志环刷爆，
             *   那样就没法用它判"长窗口里到底有没??HRST"了??             *   arg1 = 库将写进报头??SpecRev（m_hdrRev）：2 = Rev3.0（PPS 必须）??*/
            Log(L_RDO, 1, pd.headerRev());
            Log(L_PSET, static_cast<uint8_t>(tgtV / 100u),
                        static_cast<uint8_t>(tgtI / 100u));
            Log(L_RDOB, 0, static_cast<uint8_t>(rdo));
            Log(L_RDOB, 1, static_cast<uint8_t>(rdo >> 8));
            Log(L_RDOB, 2, static_cast<uint8_t>(rdo >> 16));
            Log(L_RDOB, 3, static_cast<uint8_t>(rdo >> 24));
          }
        } else {
          /* 发失败就留在原地，下一轮再试（而不是沉默地卡住）??*/
          Log(L_RDO, 0, pd.headerRev());
        }
      }
      }   /* if (fixedOk) */
    }

    /* ---------------- 6. 界面刷新（屏态指??+ 按带缓存??----------------
     * ??严格"不变就不????*只有内容真的变了才动??*，且只重画变了的那一条带
     *   （单????5ms）。稳态下（含 1Hz 保活心跳）这里一个像素都不会??—??屏面
     *   完全静止。允许整屏重画的**唯一**路径??屏态键 scrKey 发生变化"??     *   ??PD_UI_MIN_MS 限速挡下时不更新指纹，所以下一轮只要过了下限就一定会
     *   补画 —??不会丢帧，只会晚几十毫秒??     *
     * ★★ 关键窗口保护（uiSafe）★??     *   实测（IICAP/PDCAP）：一次整??DrawMessage ??**67ms**，而源端发??caps
     *   只给 24~30ms 的应答窗口。两者一旦重叠，源端就会因为"没等??Request"
     *   直接 Hard Reset??     *       2916.42ms 源端??caps ??2946.09ms ??HRST??9.7ms，卡在窗口边缘）??     *       ??MCU 恰好卡在一??67ms 整屏重画里（2909.2??976.36ms），
     *       直到 2976ms 才去??RX FIFO —??晚了整整 60ms，caps 一直躺??FIFO 里??     *   HRST 会把 VBUS 拉到 vSafe0V，对"整机??PD 源供??的产??= 掉电重启??     *   ??所以在"??attach 但合约还没立????*应答敏感??*??*不做整屏重画**??     *     只让主循环空转、随时应答；屏面保持上一帧不动。合约立住（ppsOk??     *     或判定无 PPS 后立刻恢复刷新??*/
    {
      /* 应答敏感期：刚 attach、合约未立住，且正处"等 caps / 等 ACCEPT/PS_RDY" ——
       * 这段**不做**整块重画（一次重画可能吃掉源端 24~30ms 的 caps 应答窗口 → HRST）。 */
      const bool capsHot = (tCapWait != 0u) && ((now - tCapWait) < PD_UI_CAPS_HOLD_MS);
      const bool uiSafe  = !(attached && !ppsOk &&
                             ((st == ST_REQ_SENT) || (st == ST_ACCEPTED) ||
                              ((st == ST_WAIT_CAP) && capsHot)));

      if (uiSafe && ((now - tUi) >= PD_UI_MIN_MS)) {
        tUi = now;

        /* INA226 实时测量（"real" 列）—— 与 UI 同节拍，低频短事务 */
        InaIoPoll(now);

        const uint16_t mn0 = havePps ? ppsMinV : 3300u;
        const uint16_t mx0 = havePps ? ppsMaxV : 21000u;
        const uint16_t mx1 = havePps ? ppsMaxI : 5000u;
        const uint16_t dv  = (tgtV != 0u) ? tgtV : static_cast<uint16_t>(PPS_DEF_MV);
        const uint16_t di  = (tgtI != 0u) ? tgtI : static_cast<uint16_t>(PPS_DEF_MA);
        DrawRow(0u, dv, mn0, mx0, kStepV[stepVIdx & 3u], selBand == 0u,
                g_inaBusMv, g_inaOk);
        const uint16_t cma = (g_inaCurMa > 0) ? static_cast<uint16_t>(g_inaCurMa) : 0u;
        DrawRow(1u, di, 100u, mx1, kStepI[stepIIdx & 3u], selBand == 1u,
                cma, g_inaOk);
      }
    }

    /* ---------------- 7. ??重新武装：把"服务期间漏掉的边??补回??----------------
     * INT_N ??*电平型闩??*，不是脉冲。芯片一旦把引脚拉低就会一直保持低??     * 直到所有被放开的中断位都被读走。于是服务期间如果又来了新报??—??     * 引脚**本来就是低的** —??不会产生新的下降沿，EXTI 也就不会再进 ISR??     * 而源端广??Source_Capabilities 正是**一??3 条连??*（组内间??~2ms），
     * 几乎必然踩到这个窗口。所以这一步不是锦上添花，是必需品??*/
    if (pd.isReady() &&
        (LL_GPIO_IsInputPinSet(FUSB_INT_Port, FUSB_INT_Pin) == 0u)) {
      g_pdIrq = 1u;
    }

    /* ---------------- 8. 诊断快照（每轮都写，??OpenOCD 实时体检??----------------
     *   mdw &g_snap 8
     * 判读：g_snap[30]（心跳顶事次数）正常??*恒为 0** —??中断总是先到??     *       g_snap[31] ??曾经置位过的中断源集??，正常应该非 0??*/
    g_snap[0]  = 1u;                                  /* tried */
    g_snap[1]  = pd.isReady() ? 1u : 0u;
    g_snap[2]  = devId;
    g_snap[3]  = static_cast<uint8_t>(st | (armed ? 0x80u : 0u));
    g_snap[4]  = static_cast<uint8_t>(attached ? 1u : 0u);
    g_snap[5]  = static_cast<uint8_t>(lastVbusOk | (lastCcOk << 1));  /* bit0 vbusOk, bit1 ccOk */
    g_snap[6]  = static_cast<uint8_t>(hrstCnt & 0xFFu);   /* ??累计收到??Hard Reset 次数 */
    g_snap[7]  = pd.specRev();
    g_snap[8]  = ppsPos;
    g_snap[9]  = haveCap ? lastMsg.pdoCount : 0u;
    g_snap[10] = static_cast<uint8_t>(tgtV & 0xFFu);
    g_snap[11] = static_cast<uint8_t>(tgtV >> 8);
    g_snap[12] = static_cast<uint8_t>(tgtI & 0xFFu);
    g_snap[13] = static_cast<uint8_t>(tgtI >> 8);
    g_snap[14] = selBand;
    g_snap[15] = ppsOk ? 1u : 0u;
    g_snap[16] = static_cast<uint8_t>(ppsMinV / 100u);
    g_snap[17] = static_cast<uint8_t>(ppsMaxV / 100u);
    g_snap[18] = static_cast<uint8_t>(ppsMaxI / 100u);
    g_snap[19] = static_cast<uint8_t>(((keySelect & 0x0Fu) << 4) | (keyStep & 0x0Fu));
    g_snap[20] = static_cast<uint8_t>(loops & 0xFFu);
    g_snap[21] = static_cast<uint8_t>((loops >> 8) & 0xFFu);
    g_snap[22] = static_cast<uint8_t>((loops >> 16) & 0xFFu);
    g_snap[23] = static_cast<uint8_t>((loops >> 24) & 0xFFu);
    g_snap[24] = static_cast<uint8_t>(g_pdIrqCnt & 0xFFu);
    g_snap[25] = static_cast<uint8_t>(g_keyIrqCnt & 0xFFu);
    g_snap[26] = static_cast<uint8_t>(g_clkIrqCnt & 0xFFu);
    g_snap[27] = lastS1;
    g_snap[28] = lastS0a;
    g_snap[29] = lastI0;
    g_snap[30] = static_cast<uint8_t>(hbHits & 0xFFu);
    g_snap[31] = static_cast<uint8_t>(intaAcc | int0Acc);
  }
}

/* USER CODE END ExternalFunctions */
