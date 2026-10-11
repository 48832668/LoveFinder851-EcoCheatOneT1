/**
  ******************************************************************************
  * @file    app.cpp
  * @brief   PD_PDO_FIX_Req —— FUSB302 纯 SINK「固定 PDO 诱骗」
  *
  * ============================================================================
  *  这个例程做什么
  * ============================================================================
 *  1. 开机：点亮屏；立刻清掉复位前的旧合约，并**把这一轮协商谈完**：
 *     toggle 探出源端挂在哪根 CC → 钉死 CC 路由 → 补一枪 PD **Soft Reset**
 *     （AUTO_CRC 常开，硬件在规范 195µs 内确认 ACCEPT / caps）→ 主循环 3 号块
 *     一收到 Source_Capabilities 就**立刻**回 Request 申请第一个 Fixed PDO(5V)，
 *     再等 ACCEPT → PS_RDY。屏上随即显示 PDO 列表并默认激活第 1 档。
 *     ⚠️ 为什么必须"谈完"：实测（OpenOCD 读固件日志环）SW3526S 在收到 SR 之后
 *        会进入"主动协商"态；若 SINK 既不确认 caps、也不回 Request，它 ~28ms 内
 *        就会 **caps×3 → Soft Reset×3 → Hard Reset**，把 VBUS 拉低 ~0.86s。
 *        即"发 SR + 什么都不回 + 干等按键"这条路，源端自己会以 HRST 收场。
 *        详见 ②.6。
 *  2. 源端没广播 PD（非 PD 充电器）→ 屏上显示「0 PDO」；
 *     久等不到 caps → 3.5 号块再补一枪 Soft Reset 催它。
  *  3. 收到 caps 就按**第一个 Fixed PDO（5V）**的 Object Position 组一条
  *     Request(RDO) 发出去 —— 这就是「诱骗」；屏上随即显示 PDO 列表并默认激活第 1 档。
  *  4. 等对端 Accept → PS_RDY，屏上把谈成的那个档位反白高亮。
  *  5. 之后：**单击**把高亮移到下一个 Fixed 档位（不协商，高亮行加 `*` 前缀表示
  *     「选中但未协商」，到最后一个 Fixed 后回绕到第一个，永不落到 PPS）；
  *     **双击**协商当前高亮的档位（谈成后 `*` 消失）；**长按**发 Hard Reset 重来。
  *
  *  ==========================================================================
  *  ★★ 四条最容易写错的铁律（PD_PDO_List_Det 上全踩过）★★
  *  ==========================================================================
  *  ① **RDO 里没有"电压"字段**。RDO 只能表达"我要第几个 PDO"（Object Position，
  *     bits[31:28]，从 1 起算）。所以必须**先在 caps 里扫出目标电压落在哪个
  *     PDO 上**，再把那个下标填进去。盲目写死下标 = 申请到别的档位（甚至 Reject）。
  *     本文件里这件事由 findFixedByVoltage() 做。
  *
  *  ② **收到 caps 之后必须在 24ms 内回 Request**。源端发完 Source_Capabilities
  *     并拿到我们回的 GoodCRC 后，会启动 tSenderResponse 定时器（PD 规范 24~30ms）。
  *     超时没等到 Request → 源端直接发 Hard Reset 并把 VBUS 拉低重启，协商失败。
  *     → 所以 Request 必须在**收到 caps 的同一轮**里发出去（见下面 3 号块），
  *       绝不能等 UI 刷新那一路。
  *     → 屏刷新必须有行缓存（Row() 的早退），否则一次整屏重画 35ms 正好吃掉
  *       整个协商窗口（实测差 0.74ms 就失败）。
  *
 *  ③ **确认 ACCEPT 必须用硬件（AUTO_CRC），别用软件 sendGoodCrc**。
 *     软件补一条要 0.6~0.7ms，远超规范 tTransmit(≤195µs)；实测源端把它当
 *     "没收到"→ 重发 ACCEPT×3 → Hard Reset。只有硬件自动 GoodCRC 做得到。
 *     → 选项 2 里 AUTO_CRC **常开**：硬件确认 ACCEPT 和 caps，3 号块收到
 *       caps 立刻回 Request（24~30ms 窗口绰绰有余）。见 ②.6。
  *
  *  ④ **Reject / Wait 也要处理**。Accept 只表示"受理了"，电源还没切；
  *     必须等到 PS_RDY 才算谈成。Reject 则要回到"重新选档"的状态。
  *
  *  ⑤ ★★ **Soft Reset 不重置 MessageID；Hard Reset 才重置** ★★
  *     发完 SR 千万别调 `pd.resetMsgId()`（曾经照抄库里那句注释，踩了大坑）：
  *     sendControl(SR) 已经用掉 msgId 0 并把计数推到 1，再清零就会让随后的
  *     Request 带 msgId 0 —— 而源端刚收过 msgId 0 的 SR，按"上一包 +1"
  *     它期望的是 **1**，于是判为乱序 → **不回 GoodCRC**。
  *     又因为库里 Control3=0（AUTO_RETRY 关），我方既不重发也不置 I_RETRYFAIL，
  *     中断里**一条线索都没有**，源端则静等 tSenderResponse 超时 → Hard Reset。
  *     实测证据（OpenOCD 读日志环，同一次运行）：
  *       · 我方 SR 之后，源端重播的 caps 带 **msgId = 1**（L_DBG a1=0x81）
  *         → 源端自己也没归零，接着 SR 往下数；
  *       · 源端自己发 Hard Reset 之后，caps 带 **msgId = 0**（a1=0x80）
  *         → 这才是"归零"。
  *     修好之后：SR(839ms) → caps(854) → Request(855) → ACCEPT(858)
  *     → PS_RDY(909)，全程无 HRST、VBUS 不掉。见 ②.6。
  * ============================================================================
  */

/* Includes ------------------------------------------------------------------*/
#include "main.hpp"      /* 桥接：PY32Studio 的 main.h + irq_flags.h（BSP_GetTick） */
#include "gpio.hpp"      /* 桥接：LCD_* / KEY_INT_* 板级引脚宏 */
#include "app.h"         /* extern "C" void App_Run(void) —— 供 main.c 调用 */
#include "st7735.hpp"    /* 共享 C++ 屏驱库：DMA + Font_7x10 */
#include "FUSB302.hpp"   /* 共享 C++ PD 库 */
#include <cstdio>        /* snprintf */

/* Studio_I2C1_Init()：I²C 总线自救后把引脚/外设重新配好（C 函数，显式 extern "C"） */
extern "C" {
#include "i2c.h"
}

/* Private define ------------------------------------------------------------*/
/* ---- FUSB302 配置重试 ---- */
#define PD_CFG_RETRY_MS       500u  /* init 失败重试间隔 */
/* 开机探 attach（toggle）的最长等待。开机那枪 Soft Reset 必须发到源端真正
 * 挂着的那根 CC 上，所以要先 toggle 探一下；没插线时也不阻塞进 "PRESS KEY"。
 * 取值与参考工程 PD_PDO_List_Det 的 PD_BOOT_ATTACH_MS 一致。 */
#define PD_BOOT_ATTACH_MS     800u

/* 开机那枪 Soft Reset 之后，等"源端对我方 SR 的 ACCEPT"并把它确认掉的最长等待。
 * 实测（PDCAP_1.csv）：SR → SRC GoodCRC（+0.6ms）→ SRC ACCEPT（+6.3ms），
 * 全程 < 10ms；留 200ms 纯属兜底（源端忙的时候会晚一点）。
 * ⚠️ 这里**不发 Request、不等 PS_RDY** —— 真正的协商留到按键之后。 */
#define PD_BOOT_ACK_MS        200u

/* 待机（未按键）期间排空 RX FIFO 的节拍。源端在广播窗口内约每 146ms 重播一组
 * Source_Capabilities（实测 PDCAP_1.csv #8 之后），我们不确认它们，但必须把
 * FIFO 读走 —— 否则 RX FIFO 会打满（Status1.RX_FULL），按键之后源端的应答
 * 就进不来了。读/冲 FIFO 都是纯 I²C 动作，不会往 CC 上发任何东西。 */
#define PD_STANDBY_DRAIN_MS   120u

/* "源端还在广播"的判据窗口：待机排空时最近一次读到 caps 距现在 < 这个值，
 * 就认为源端仍在周期性广播（实测广播间隔 ~146ms）。
 * 按键时用它区分业主定义的两种情形：还在广播 → 先听；已安静 → 立刻补 SR。 */
#define PD_BROADCAST_MS       400u

/* ---- 心跳兜底（★ 不是轮询周期）----
 *
 * 三条 EXTI 都会在事件一到就置标志，主循环当轮就服务，实际延迟 ~100µs。
 * 心跳是**兜底**：INT_N 是电平型闩锁，边沿有可能整体丢失（上拉没贴、走线问题、
 * 芯片异常）。没有心跳，程序会安静地卡死；有心跳只是"慢一点但还活着"。
 *
 * ⚠️ 协商期间（还没拿到 caps）的心跳必须明显小于 24ms —— 这里 5ms。 */
#define PD_ATTACH_POLL_MS      10u  /* attach(TOGSS) 兜底心跳 */
#define PD_RX_POLL_MS           5u  /* RX 兜底心跳：协商中 */
#define PD_RX_IDLE_MS         100u  /* RX 兜底心跳：已谈成 / 已放弃 */
#define PD_INT_POLL_MS         20u  /* 中断闩锁兜底心跳 */

/* ---- 屏刷新节拍 ---- */
#define PD_UI_MIN_MS           80u  /* 两次重画的最小间隔（限速，不是周期） */
#define PD_UI_WATCHDOG_MS     500u  /* 兜底自愈：每 500ms 只作废一行 */

/* ---- 拔出 / 恢复 ---- */
/* ⚠️ 不能太短：源端响应 Soft Reset 时会主动把 VBUS 拉低约 0.9s，短去抖会
 *    把它误判成"拔出"。 */
#define PD_DETACH_MS         1500u
/* ★ CC 终结丢失 → 快速判拔出（**快通道**）。
 *   实测（IIC_OUT.csv，拔 SRC）：CC 终结在 **2025ms** 就没了（STATUS0.BC_LVL 3→0），
 *   而 VBUS 直到 **3450ms** 才掉 —— 源/漏两侧的体电容把 VBUS 拖住了 ~1.4s。
 *   再叠上 PD_DETACH_MS(1.5s) 的 VBUS 去抖，屏要等到 **4957ms** 才更新（晚了 2.9s）。
 *   CC 终结才是"源端在不在"的直接判据（SR/HR 期间 Rp 始终在，只有 VBUS 会掉），
 *   所以用它做快通道。
 *
 * ★ 去抖取 **20ms**（= 2 次 10ms 采样）—— 按"响应最快"取的最小可靠值：
 *   · 误判面已经被**四道守卫**收干净：ccSeenOk（挂上后见过 BC_LVL != 0）、
 *     vbusOk（VBUS 有效）、**ACTIVITY == 0**（PD 突发期间 BC_LVL 会假读 0，
 *     见 2 号块 ccMeasurable）—— 剩下的假 0 只可能来自单次采样抖动，
 *     2 次连续采样足以滤掉。
 *   · 屏刷新本身有 PD_UI_MIN_MS(80ms) 限速，检测只要 < 80ms 就不会成为瓶颈；
 *     20ms 让端到端稳定落在下一个刷新窗口（约 60~80ms）。
 *   · 实测单次 BC_LVL 假 0 最长 9ms，且**带 ACTIVITY=1**，已被 ACTIVITY 守卫拦下。
 *   · 若将来现场出现误判拔出，把这里抬到 30~40ms 即可（仍在 80ms 内，肉眼无差别）。 */
#define PD_DETACH_CC_MS        20u
#define PD_DETACH_HOLD_MS     300u  /* 判拔出后静置这么久才允许重挂 */
#define PD_HR_RECOVER_MS     2500u  /* 我方主动 Hard Reset 后的恢复窗口 */
#define PD_RECOVER_RETRY_MS   300u  /* Hard Reset 后 recover() 的重复调用间隔 */
#define PD_HR_RECOVER_MAX       3u  /* 恢复窗口内 recover() 最多补几次（含首调之后） */

/* ---- 久等不到 caps → 主动叫醒源端 ------------------------------------
 * 源端看到 Rd 后只广播约 5s 的 Source_Capabilities；一旦因为 toggle/attach
 * 慢了一拍错过窗口，它就彻底安静（PD 分析仪上既不见它再发 caps，也不见它发
 * Hard Reset）。唯一出路是 SINK 主动发一条 **Soft Reset**：源端回 GoodCRC +
 * Accept，随后重播 caps。下面两个时间控制"首枪"和"补枪"，取值与参考工程
 * PD_PDO_List_Det 一致（PD_LISTEN_MS / PD_SR_WAIT_MS）。 */
#define PD_SOFTRST_FIRST_MS   250u  /* attach 后先短听这么久：源端在播就直接读 */
#define PD_SOFTRST_RETRY_MS  3000u  /* 之后每隔这么久补一枪 Soft Reset */
#define PD_SOFTRST_MAX          3u  /* 最多催这么多枪；还不来当"非 PD 源端"处理 */

/* ---- 按键 ---- */
/* 机械按键抖动典型 1~20ms（2~10 个边沿），去抖窗口取 5ms 足够压住接触抖动，
 * 又不会把一次干脆的短按（通常 50~200ms）吃掉。 */
#define KEY_DEBOUNCE_MS         5u
/* 双击窗口。两次短按间隔 < 这个值 = 双击；超过 = 第一次单击。
 * 单击此时**不再立即触发**，而是等满这个窗口才确认（否则分不清单击/双击）。 */
#define KEY_DOUBLE_MS         300u
/* 长按门限。达到就在**按住不放**的状态下立刻发事件（不必等松开），
 * 这样用户有即时反馈；同时标记 consumed，松开时不会再补一个短按。 */
#define KEY_LONG_MS          1000u

/* ---- 诱骗目标 ---- */
/* 目标电压。上电按键后默认申请这个电压对应的 Fixed PDO（=第一个 PD 挡位 5V）。
 * 源端没这个档位时会显示 NOPDO，按一下按键切到下一个可用的 Fixed 档位。 */
#define PD_TARGET_MV_DEFAULT  5000u
/* 电压匹配容差：PDO 电压是 50mV 的整数倍，且源端可能报 8.95V 这种值。
 * ±500mV 足以覆盖 5/9/12/15/20V 各档而不会串档（相邻档最小间隔 3V）。 */
#define PD_TGT_TOL_MV          500u
/* 申请电流上限（10mA 单位）。本板实际只吃几十 mA，取源端 PDO 报的值即可，
 * 但为安全再夹一个 3.00A 上限。 */
#define PD_MAX_CUR_10MA        300u
#define PD_FALLBACK_CUR_10MA   300u  /* 源端把电流报成 0 时的兜底（3.00A） */

/* Private typedef -----------------------------------------------------------*/
/* 协商状态（屏上那一行摘要用） */
enum e_St : uint8_t {
  ST_WAIT_ATTACH = 0,  /* 还没探到源端 */
  ST_WAIT_CAP    = 1,  /* 已 attach，正在听 Source_Capabilities */
  ST_REQ_SENT    = 2,  /* 已发出 Request(RDO)，等应答 */
  ST_ACCEPTED    = 3,  /* 收到 Accept，等 PS_RDY */
  ST_PSRDY       = 4,  /* ★ 收到 PS_RDY = 该档位申请成功 */
  ST_REJECTED    = 5,  /* 收到 Reject（或 Wait 超时） */
  ST_NO_PDO      = 6,  /* 目标电压在源端 caps 里找不到 */
  ST_WAIT_KEY    = 7,  /* 上电后还没按键：不显示 PDO、不碰 PD */
};

/* 按键事件（KeyService 的返回值） */
enum e_Key : uint8_t {
  KEY_NONE   = 0,
  KEY_SHORT  = 1,  /* 单击：高亮移到下一个 Fixed 档位（不协商） */
  KEY_DOUBLE = 2,  /* 双击：协商当前高亮的档位 */
  KEY_LONG   = 3,  /* 长按：发 Hard Reset，整套重新协商 */
};

/* 事件日志（环形，固定 8 字节/条）—— 供 OpenOCD 直接读
 *   [0..3] t，毫秒，小端，**相对开机**（g_log_t0）
 *   [4]    事件码（见 e_Log）  [5] arg0  [6] arg1  [7] 备用
 * 读法：条数 = g_log_head，第 n 条在 g_log + (n & 63) * 8。 */
enum e_Log : uint8_t {
  L_BOOT    = 1,   /* 进入 App_Run */
  L_LCD     = 2,   /* 屏驱初始化完成 */
  L_INIT    = 3,   /* FUSB302 init，arg0=ok */
  L_DEVID   = 4,   /* arg0=DEVICE_ID */
  L_TOGGLE  = 5,   /* startSinkToggle，arg0=ok */
  L_ATTACH  = 6,   /* arg0=cc(1/2)，0=没探到 */
  L_SELCC   = 7,   /* selectCC，arg0=ok，arg1=specRev */
  L_CAP     = 8,   /* 收到 Source_Cap，arg0=PDO 数，arg1=specRev */
  L_RDO     = 9,   /* 发 Request，arg0=ok，arg1=Object Position */
  L_TGT     = 10,  /* arg0=目标 mV 低字节，arg1=选中的 Object Position */
  L_PSRDY   = 11,  /* arg0=1 PS_RDY / 0 REJECT，arg1=实际 msgType */
  L_ACCEPT  = 12,  /* 收到 Accept（arg1=msgType）。arg0：0=只记录 /
                    *   1=收到源端对我方 Request 的 Accept /
                    *   2=我方回了一条 ACCEPT 但没发出去 /
                    *   3=★ 待机阶段用软件补出的那条 GoodCRC（确认源端对我方 SR 的 Accept） */
  L_HRST    = 13,  /* 收到对端 Hard Reset */
  L_HRSND   = 14,  /* 我方主动发 Hard Reset（长按），arg0=ok */
  L_RECOVER = 15,  /* pd.recover()，arg0=ok，arg1=CC */
  L_DETACH  = 16,  /* 判拔出。arg0: 1 = CC 终结丢失**快通道** / 0 = VBUS 持续低慢通道；
                    *   arg1 = STATUS0(0x40) 快照。 */
  L_BTN     = 17,  /* arg0=1 单击 / 2 双击 / 3 长按，arg1=当时的去抖电平 */
  L_INTA    = 18,  /* arg0=INTERRUPTA 快照（R/C，读即清） */
  L_INT0    = 19,  /* arg0=Interrupt(0x42) 快照（已剔除 I_CRC_CHK） */
  L_ARM     = 20,  /* 收到"上电唤醒"按键，开始 PD（挂 Rd / 显示 PDO） */
  L_SOFTRST = 21,  /* 我方主动发 Soft Reset（arg0=ok，arg1=第几枪；0=开机那一枪） */
  L_CRC     = 22,  /* 硬件自动 GoodCRC 开关（arg0=1 开 / 0 关） */
  L_ACK     = 23,  /* boot SR 后手动确认：arg0=成功发出的 GoodCRC 数，arg1=首条非 GoodCRC 报文类型 */
  L_DBG     = 24,  /* 收到的**每一条**报文。arg0=msgType，arg1=(isSourceCap?0x80:0)|msgId */
  L_INTB    = 25,  /* arg0=INTERRUPTB 快照（R/C）。IB_GCRCSENT(0x01) 置位 =
                    *   芯片刚回过一条 GoodCRC = **刚收到一条有效报文**。
                    *   用它数"到底收了几包、我们的自动确认有没有发出去"。 */
  L_SRC     = 26,  /* ★ 源端插拔 / VBUS 状态变化（**中断驱动**，见主循环 4 号块）。
                    *   arg0 = 状态位：bit0 attached（CC 探到 Rp）、
                    *          bit1 vbusOk（VBUS 有效）、bit2 srcPresent（上面取或）、
                    *          bit3 ccOk（CC 终结在 = BC_LVL != 0）。
                    *   arg1 = STATUS0(0x40) 快照：bit7 VBUSOK / bit6 ACTIVITY /
                    *          bit5 COMP / bits[1:0] BC_LVL —— CC 终结现场。 */
  L_CC      = 27,  /* ★ CC 终结（BC_LVL）变化 —— **拔出快通道**的证据。
                    *   实测拔 SRC：这一条比 VBUS 掉早约 1.4s（2025ms vs 3450ms），
                    *   屏刷新提速就靠它（见主循环 2 号块的 ccGone / PD_DETACH_CC_MS）。
                    *   arg0 = ccOk（1 = CC 有终结 / 0 = 终结丢了）、arg1 = STATUS0(0x40)。 */
};

/* Private variables ---------------------------------------------------------*/
extern "C" {
/* 事件日志 —— OpenOCD: mdw <g_log_head> 1 ; mdb <g_log> 512 */
volatile uint32_t g_log_head = 0;
volatile uint8_t  g_log[512] = {0};
volatile uint32_t g_log_t0   = 0;

/* 诊断快照（固定布局，LTO 下也稳定）—— OpenOCD: mdw <g_snap> 8
 *   [0] tried(恒1)  [1] ready   [2] devId   [3] negPos | (armed?0x80:0)
 *   [4] attached    [5] vbusOk  [6] hrTries [7] specRev
 *   [8] st          [9] pdoCount
 *   [10..11] tgtMv(小端)        [12..13] tgtCur10(小端)
 *   [14] selPos     [15] ack    [16] pdo[0] 低字节
 *   [17] key.edges 低字节       [18] 单击次数低字节
 *   [19] (双击次数&0xF)<<4 | (长按次数&0xF)
 *   [20..23] 主循环轮次(小端)
 *   [24] g_pdIrqCnt 低字节      [25] g_keyIrqCnt 低字节
 *   [26] g_clkIrqCnt 低字节     [27] 最近 STATUS1
 *   [28] 最近 STATUS0A          [29] 最近 Interrupt(0x42)
 *   [30] hbHits 低字节（心跳顶事次数 → 正常应恒为 0）
 *   [31] intaAcc | int0Acc（已置位过的中断源集合） */
volatile uint8_t  g_snap[32] = {0};
}

/* USER CODE BEGIN 0 */

/*============================================================================
 * 屏幕布局（Font_7x10：7px 宽 / 10px 高 → 22 列 × 8 行）
 *
 *   ┌──────────────────────┐
 *   │PD FIX DECOY          │ 行0  标题栏（HeaderBar 反白，C_BAR 底；常量）
 *   │1 FIX 5.0V 3.00A 15W  │ 行1  PDO 列表（最多 6 行）
 *   │2 FIX 9.0V 3.00A 27W  │ 行2   ← 谈成的那个档位反白高亮（C_SEL_BG）
 *   │3 FIX 12.0V 3.00A 36W │ 行3
 *   │4 PPS 3.3-11.0V 2.00A │ 行4
 *   │                      │ 行5
 *   │                      │ 行6
 *   │1CLK:NEXT 2CLK:SET    │ 行7  按键提示（常量，只画一次）
 *   └──────────────────────┘
 *
 *  ⚠️ 行0 以前是**调试摘要** "5.0V S1 N0 PSRDY"（S/N = 内部选中/协商下标，
 *     PSRDY 等 = 状态机枚举名）—— 已全部移除，现在是纯标题。
 *     要看状态机/下标请读 OpenOCD 的 g_snap，不要往屏上加回调试字段。
 *============================================================================*/
namespace {

using LoveFinderLib::FUSB302;
using LoveFinderLib::FUSB302_Message;
using LoveFinderLib::FUSB302_PDO;
using LoveFinderLib::e_FUSB302_CC;
using LoveFinderLib::e_FUSB302_PDOType;
namespace Reg = LoveFinderLib::FUSB302_Reg;
namespace Bit = LoveFinderLib::FUSB302_Bit;

constexpr uint16_t FONT_W = 7;      /* Font_7x10 字宽 */
constexpr uint16_t ROW_H  = 10;     /* Font_7x10 行高 */
constexpr uint8_t  COLS   = 22;     /* 160 / 7 */
constexpr uint8_t  ROWS   = 8;      /* 80 / 10 */

/* 正文 PDO 行 = 行1..行6（行0 摘要、行7 提示各占一行） */
constexpr uint8_t  PDO_ROW0 = 1u;
constexpr uint8_t  PDO_ROWS = 6u;
constexpr uint8_t  HINT_ROW = 7u;

constexpr uint16_t C_BAR = 0x10A2;  /* 标题栏底色：深灰蓝 */
constexpr uint16_t C_DIM = 0x8410;  /* 次要文字：灰 */
constexpr uint16_t C_NG  = ST7735::RED;
constexpr uint16_t C_KEY = ST7735::CYAN;
constexpr uint16_t C_WAIT = ST7735::YELLOW;  /* "还没插源端" 这类等待提示 */
/* 选中档位的反白底色。RGB565 R=2 G=15 B=19 → RGB888 ≈ (16,60,156)，配白字对比度高。 */
constexpr uint16_t C_SEL_BG = 0x11F3;

/* Status0(0x40) 的 bits[1:0] = BC_LVL（CC 电平）。库只导出了 S0_VBUSOK /
 * S0_CRC_CHK，这里补一个本文件要用的掩码。
 *   BC_LVL == 0 → CC 低于 ~0.2V → **源端的 Rp 没了** → 线拔了 / 源端撤了终结。
 *   ⚠️ 实测（IIC_OUT.csv）：插着时 BC_LVL=3（STATUS0=0x93），拔掉后=0（0x90）。
 *   它是"源端在不在"最直接、也最早可得的判据 —— 比 VBUS 早约 1.4s。 */
constexpr uint8_t S0_BC_LVL_MASK = 0x03u;

/* Status0(0x40) bit6 = ACTIVITY（CC 上正有 BMC 活动）。
 *   ⚠️ **PD 突发期间 FUSB302 的测量块被收发占用，BC_LVL 会假读 0**。
 *   实测（ERRIIC 复测）：协商中 853ms 读到 STATUS0=0xd0 =
 *   VBUSOK=1 & ACTIVITY=1 & BC_LVL=0，9ms 后即恢复 0x93。
 *   → 判"CC 终结丢失"时必须要求 ACTIVITY == 0，否则一次长突发
 *     （caps→Request→Accept→PS_RDY 那串约 56ms）就可能被误判成拔出。 */
constexpr uint8_t S0_ACTIVITY = 0x40u;

/*--------------------------------------------------------------------------*/
/* 整行输出（DMA + 行缓存）                                                  */
/*--------------------------------------------------------------------------*/
/* WriteString 只覆盖字符串本身的宽度，不补空格的话上一帧的内容会残留在行尾，
 * 所以这里统一把每行补齐到 22 个字符再输出。 */

/* ---- DMA 文字渲染的"上下文" ----
 * ST7735_RowRenderFn 的签名是 void(uint16_t y, uint8_t* dst, uint16_t w)，
 * **没有**上下文参数，所以用一组文件级静态量把"这一行要画什么"传进去。
 * 主循环单线程、Row() 同步返回，不存在重入，安全。 */
uint16_t        g_rowFg  = 0;
uint16_t        g_rowBg  = 0;
uint16_t        g_rowTop = 0;              /* 本行在屏上的起始 y（绝对行号） */
const uint16_t* g_rowGlyph[COLS] = {};     /* 每字符的字模行指针，Row() 里预先查好 */

/* ---- ★ 行缓存：这一行的像素不会变，就一个字节 SPI 都不发 ----
 *
 * 【为什么必须要有它 —— 实测的致命时序】
 *   整屏重画 = 标题条 + 8 行 = 9 个 160×10 的矩形，实测**一次要 35ms**。
 *   而这段静默正好压在"收到 caps → 必须 24ms 内回 Request"的窗口里，
 *   一次整屏重画就能把整个协商窗口吃掉（实测差 0.74ms 就失败）。
 *   加行缓存后，同一次更新只花真正变化的那 1~2 行 ≈ 4~8ms。
 *
 * 【判定依据是"像素会不会变"，不是"文字一不一样"】
 *   Row() 把文字补齐到 COLS 之后才比 —— 行尾补空格就是为了**覆盖**上一帧。
 *   前景/背景色也必须一起比（选中档位的反白只改配色、不改文字）。
 *
 * 【失效时机】
 *   ① 开机：g_rcOk 全 0 → 第一帧 9 个矩形全画；
 *   ② 看门狗：每 500ms **只作废一行**（轮转）—— 不能一次全作废，
 *      否则又变成 35ms 的整屏重画，等于把刚修好的坑挖回来。 */
char     g_rcTxt[ROWS + 1][COLS + 1] = {}; /* [ROWS] 留给标题条（它没有文字） */
uint16_t g_rcFg[ROWS + 1]  = {};
uint16_t g_rcBg[ROWS + 1]  = {};
bool     g_rcOk[ROWS + 1]  = {};
uint8_t  g_rcWatchRow = 0;

void InvalidateRow(uint8_t idx)
{
  if (idx <= ROWS) { g_rcOk[idx] = false; }
}

/* 行回调：调用频率是**每个像素行一次**（一行文字 10px 高 → 回调 10 次）。
 * 每次把整条 160px 像素行铺进 dst（大端 RGB565）；传输由
 * ST7735_DrawFrameRect() 内部的 DMA 乒乓负责 —— CPU 只管算。
 *
 * ⚠️ 字模**必须**用 font_get_glyph() 查，不能写 `Font_7x10.data[...]`：
 *    本字库是"字符级编译"（font_data.cpp 里 Font_7x10 = {7,10,nullptr}），
 *    FontDef.data **恒为 nullptr**。直接解引用是空指针解引用；更坑的是
 *    LTO 看得见 data 是常量 nullptr，会把整张字模表判定为"不可达"直接删掉
 *    （实测 RO-data 掉了 2.2KB），而编译期 0 error 0 warning —— 完全静默。 */
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
     * 这 6 个"零头"像素算出 ci = 22，而 g_rowGlyph 只有 22 个元素（下标 0..21）
     * → 越界读一个指针。越界读到的正好是 .bss 里紧邻的别的变量，取到的
     * "字模指针"是个垃圾值：开机时它恰好是 0（读地址 0 不崩，画面看着正常，
     * 掩盖了问题），而一旦那几字节被写成非零，就会解引用未映射地址 →
     * BusFault → HardFault_Handler 的 while(1)。
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
   *   说明这一行**一个像素都不会变**，直接返回，不发任何 SPI。 */
  if (g_rcOk[r] && g_rcFg[r] == fg && g_rcBg[r] == bg) {
    bool same = true;
    for (uint8_t k = 0; k <= COLS; ++k) {
      if (g_rcTxt[r][k] != buf[k]) { same = false; break; }
    }
    if (same) { return; }
  }
  /* 记下"这一行现在应该是什么"，再画 */
  for (uint8_t k = 0; k <= COLS; ++k) { g_rcTxt[r][k] = buf[k]; }
  g_rcFg[r] = fg;
  g_rcBg[r] = bg;
  g_rcOk[r] = true;

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
  ST7735_DrawFrameRect(0, g_rowTop, ST7735_WIDTH, ROW_H, RowRender);
}

/* 标题栏底色：整条实心填充。即使字库一路出问题，这条蓝灰条也一定会出现 ——
 * 用来把"SPI/面板通路"和"文字渲染"两件事分开定位。
 * 它是常量填充，画一次之后永远命中缓存（槽位 = ROWS）。
 *
 * ★★ 重填底色时**必须同时作废行0 的文字缓存** ★★
 *   【为什么】这一条 `FillRectangle_DMA(0,0,W,ROW_H)` 覆盖的正是**行0 那一整条**，
 *   它会把 Row(0,...) 画上去的 "PD FIX DECOY" 一起抹成纯底色；
 *   可是行缓存并不知道 —— 紧接着 DrawHeader() 里的 Row(0,...) 一看
 *   `g_rcOk[0]==true` 且文字没变，就**早退什么都不画**，
 *   于是标题凭空消失，一直要等到看门狗轮转到槽位 0 才被补回来。
 *
 *   【实测症状】看门狗每 500ms 只作废一行、轮转 0..ROWS（共 9 槽），
 *   所以槽位 8（本条）每 9×500ms = **4.5s** 轮到一次：
 *   标题每 4.5s 消失约 500ms 再回来 —— 就是业主看到的"偶发闪烁"。
 *   （用 %TEMP%/sim_rowcache.py 对行缓存建模复现：修前 27 拍里 3 拍空白，
 *     修后 0 拍空白；代价只是条重填时多画一行，SPI 开销可忽略。）
 *
 *   【修法】重填底色 = 把行0 的像素改掉 ⇒ 行0 的缓存必须作废。 */
void HeaderBar()
{
  if (g_rcOk[ROWS] && g_rcBg[ROWS] == C_BAR) { return; }

  InvalidateRow(0);        /* ★ 见上：底色覆盖行0，必须让 Row(0,...) 重画 */

  g_rcBg[ROWS] = C_BAR;
  g_rcFg[ROWS] = C_BAR;
  g_rcOk[ROWS] = true;
  ST7735_FillRectangle_DMA(0, 0, ST7735_WIDTH, ROW_H, C_BAR);
}

/*--------------------------------------------------------------------------*/
/* 文本小工具                                                                */
/*--------------------------------------------------------------------------*/
void mVToStr(uint16_t mv, char* out, size_t n)
{
  (void)snprintf(out, n, "%u.%u", static_cast<unsigned>(mv / 1000u),
                 static_cast<unsigned>((mv % 1000u) / 100u));
}

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
      /* 认不出的 PDO 类型（保留编码）—— 只报"未知"，**不**把裸 32 bit 打上屏。 */
      (void)snprintf(out, n, "%u ???", i);
      break;
  }
}

/* stName() 已删除 —— 它只服务于行0 那条调试摘要（见 DrawHeader）。
 * 状态机名字不再上屏；要看就读 OpenOCD 的 g_snap[8]。 */

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
/* PD：在 Source_Capabilities 里挑目标档位                                   */
/*--------------------------------------------------------------------------*/
/* 返回 Object Position（1..7），0 = 没找到。
 * 只看 **Fixed** PDO（type = bits[31:30] == 0），并取"离目标电压最近"的那个，
 * 且差值必须 <= PD_TGT_TOL_MV —— 避免源端只报 5V 时我们硬把 20V 的目标
 * 匹配到 5V 档上去。
 *
 * ⚠️ 这就是"RDO 没有电压字段"的补偿：电压只能通过**下标**表达。 */
uint8_t findFixedByVoltage(const FUSB302_Message& m, uint16_t targetMv,
                           uint16_t* outMv, uint16_t* outCur10)
{
  uint8_t  best     = 0u;
  uint32_t bestDiff = 0xFFFFFFFFu;

  for (uint8_t i = 0; i < m.pdoCount && i < 7u; ++i) {
    const uint32_t raw = m.pdo[i];
    if (((raw >> 30) & 0x3u) != 0u) { continue; }        /* 只要 Fixed */
    const uint16_t mv = static_cast<uint16_t>(((raw >> 10) & 0x3FFu) * 50u);
    const uint32_t diff = (mv > targetMv) ? (mv - targetMv) : (targetMv - mv);
    if (diff > PD_TGT_TOL_MV) { continue; }
    if (diff < bestDiff) {
      bestDiff = diff;
      best     = static_cast<uint8_t>(i + 1u);
      if (outMv    != nullptr) { *outMv    = mv; }
      if (outCur10 != nullptr) { *outCur10 = static_cast<uint16_t>(raw & 0x3FFu); }
    }
  }
  return best;
}

/* 取"当前选中档位之后的第一个 Fixed 档位"（循环回绕），供短按切档用。
 * afterPos = 0 表示从第 1 个开始。返回 Object Position，0 = 一个 Fixed 都没有。 */
uint8_t nextFixedPos(const FUSB302_Message& m, uint8_t afterPos,
                     uint16_t* outMv, uint16_t* outCur10)
{
  uint8_t first = 0u;
  for (uint8_t i = 0; i < m.pdoCount && i < 7u; ++i) {
    const uint32_t raw = m.pdo[i];
    if (((raw >> 30) & 0x3u) != 0u) { continue; }
    const uint8_t pos = static_cast<uint8_t>(i + 1u);
    if (first == 0u) { first = pos; }
    if (pos > afterPos) {
      if (outMv    != nullptr) { *outMv    = static_cast<uint16_t>(((raw >> 10) & 0x3FFu) * 50u); }
      if (outCur10 != nullptr) { *outCur10 = static_cast<uint16_t>(raw & 0x3FFu); }
      return pos;
    }
  }
  if (first != 0u) {   /* 回绕到第一个 */
    const uint32_t raw = m.pdo[first - 1u];
    if (outMv    != nullptr) { *outMv    = static_cast<uint16_t>(((raw >> 10) & 0x3FFu) * 50u); }
    if (outCur10 != nullptr) { *outCur10 = static_cast<uint16_t>(raw & 0x3FFu); }
  }
  return first;
}

/* 组 RDO 并发出去。
 *
 * Fixed PDO 的 RDO 布局（PD 规范 §6.4.2）：
 *   bits[31:28] Object Position      ← 要第几个 PDO（1..7）★ 唯一能表达"哪一档"的字段
 *   bit [27]    GiveBack
 *   bit [26]    No USB Suspend
 *   bit [25]    USB Communications Capable
 *   bit [24]    Unchunked Extended Messages Supported
 *   bits[23:20] 保留
 *   bits[19:10] Operating Current     (10mA 单位)
 *   bits[9:0]   Maximum Operating Current (10mA 单位)
 *
 * ⚠️ 这里**没有**电压字段 —— 见文件头铁律①。 */
bool sendRequestFor(FUSB302& pd, uint8_t pos, uint16_t cur10)
{
  if (pos == 0u) { return false; }

  uint16_t c = cur10;
  if (c == 0u)                 { c = PD_FALLBACK_CUR_10MA; }
  if (c > PD_MAX_CUR_10MA)     { c = PD_MAX_CUR_10MA; }

  const uint32_t rdo =
      (static_cast<uint32_t>(pos) << Bit::RDO_OBJPOS_SHIFT) |
      (static_cast<uint32_t>(c)   << Bit::RDO_FV_CUR_SHIFT)  |
       static_cast<uint32_t>(c);

  /* 新一轮协商：先清掉"上次申请成功"的结论和应答记录，
     这样随后读到的 msgType 一定是针对**这一条** RDO 的应答。 */
  pd.clearLastPeerMsgType();
  return pd.sendRequest(rdo);
}

/*--------------------------------------------------------------------------*/
/* 按键：ISR 只置标志，去抖 / 判定全部在这里（主循环）                        */
/*--------------------------------------------------------------------------*/
/* ⚠️ 为什么不在 ISR 里做：
 *   · 机械按键一次按下会在 1~20ms 内产生 2~10 个边沿。若 ISR 里"每来一个边沿
 *     算按下一次"，一次物理按键会被当成好几次。
 *   · 去抖需要"距上次边沿多久"这个信息，而 ISR 里既不该算时间（BSP_GetTick
 *     依赖 SysTick，而 SysTick 优先级低于 EXTI → 在 ISR 里等 tick 会死锁），
 *     也不该保存状态机。
 *   → 所以 ISR 只报告"引脚动了一下"（g_keyIrq / g_keyLevel），
 *     本函数**直接采样引脚**并做时间型去抖。
 *
 * 本函数是非阻塞的，每轮主循环调一次。主循环是无 sleep 的忙循环
 * （I²C 服务之间只有几十 µs），所以采样密度远高于 5ms 去抖窗口。 */
struct KeyState {
  uint8_t  raw      = 1u;   /* 最近一次采样到的原始电平（1=松开，0=按下） */
  uint8_t  stable   = 1u;   /* 去抖后的电平 */
  uint32_t rawT     = 0u;   /* raw 最近一次变化的时刻 */
  uint32_t pressT   = 0u;   /* 去抖确认"按下"的时刻 */
  uint32_t releaseT = 0u;   /* 上一次"短按松开"的时刻（双击窗口计时用） */
  bool     consumed = false; /* 本次按下已被消费（长按 / 双击第二下 / 唤醒） */
  bool     waiting2 = false; /* 已收下第一个单击，正在等第二个（双击窗口内） */
  uint32_t edges    = 0u;   /* 采样到的原始跳变次数（诊断：抖动有多严重） */
};

/* 单击 / 双击 / 长按判定（全部在主循环，非阻塞，每轮调一次）。
 *
 * 判别顺序与共享库 BUTTON 一致：
 *   · 按下 → 记 pressT；若按住 ≥ KEY_LONG_MS → 立刻发 KEY_LONG（即时反馈）；
 *   · 松开若 < KEY_LONG_MS：先**不**发单击，挂起 waiting2；
 *   · waiting2 期间第二次"按下→松开" = KEY_DOUBLE；
 *   · waiting2 窗口（KEY_DOUBLE_MS）内没有第二下 → 到时补发一次 KEY_SHORT。
 *
 * ⚠️ 单击因此带 KEY_DOUBLE_MS 的确认延迟 —— 这是分清单击/双击的必然代价。 */
e_Key KeyService(KeyState& k, uint32_t now)
{
  const uint8_t raw =
      (LL_GPIO_IsInputPinSet(KEY_INT_Port, KEY_INT_Pin) != 0u) ? 1u : 0u;
  if (raw != k.raw) { k.raw = raw; k.rawT = now; k.edges++; }

  /* 时间型去抖：raw 必须稳定 KEY_DEBOUNCE_MS 才认账 */
  if (((now - k.rawT) >= KEY_DEBOUNCE_MS) && (k.stable != k.raw)) {
    k.stable = k.raw;
    if (k.stable == 0u) {                 /* 按下沿（低有效） */
      k.pressT   = now;
      k.consumed = false;
    } else if (!k.consumed) {             /* 松开沿 */
      k.consumed = true;
      if ((now - k.pressT) >= KEY_LONG_MS) {
        k.waiting2 = false;
        return KEY_LONG;                  /* 兜底：长按通常已在上面的分支发过 */
      }
      if (k.waiting2) {                   /* 双击的第二下 */
        k.waiting2 = false;
        return KEY_DOUBLE;
      }
      k.waiting2 = true;                  /* 第一下：先挂起，等双击窗口 */
      k.releaseT = now;
    }
  }

  /* 长按：按住到门限立刻发事件，并作废等待中的双击 */
  if ((k.stable == 0u) && !k.consumed && ((now - k.pressT) >= KEY_LONG_MS)) {
    k.consumed = true;
    k.waiting2 = false;
    return KEY_LONG;
  }

  /* 双击窗口到期 → 把挂起的第一个单击确认为单击 */
  if (k.waiting2 && (k.stable == 1u) && ((now - k.releaseT) >= KEY_DOUBLE_MS)) {
    k.waiting2 = false;
    return KEY_SHORT;
  }

  return KEY_NONE;
}

/*--------------------------------------------------------------------------*/
/* 界面                                                                      */
/*--------------------------------------------------------------------------*/
/* 屏上 PDO 区画哪一类内容 —— 由"源端插没插"和"有没有 caps"共同决定。
 *
 * ★ 本轮要区分的关键一件事：**没插源端 ≠ 插了但源端不发 PD**。
 *   前者是"线都没接上"，屏上必须明说 NO SRC PLUG IN；
 *   后者是"接了个非 PD 充电器 / 源端还没广播"，那才是 0 PDO。
 *   旧版把这两种都画成 "0 PDO"，用户根本分不清是没插线还是源端不配合。 */
enum e_Screen : uint8_t {
  SCR_NO_SRC = 0,  /* 没插 Type-C 源端（CC 无终结 **且** VBUS 也没有） */
  SCR_NO_PDO = 1,  /* 插着源端，但一条 Source_Capabilities 都没收到 */
  SCR_PDO    = 2,  /* 收到 caps，画 PDO 列表 */
};

/* 屏上"画的是什么"的完整描述 —— 漏掉任何一个输入都会出现"内容变了屏没变"。
 *   scr / st / tgtMv / pdoCount / selPos / negPos / ack  +  pdo[] 内容 */
struct UiFp {
  uint8_t  armed;
  uint8_t  scr;      /* ★ 新增：NO SRC / 0 PDO / PDO 列表 三选一 */
  uint8_t  st;
  uint8_t  selPos;
  uint8_t  negPos;
  uint8_t  ack;
  uint8_t  pdoCount;
  uint16_t tgtMv;
  uint32_t pdo[PDO_ROWS];
};

/* 行0：标题栏（反白，C_BAR 底）。常量 —— 靠行缓存只真正画一次。
 *
 * ⚠️ 这里**曾经**是开发期的调试摘要，形如 "5.0V S1 N0 PSRDY"：
 *      · S<n> = 内部**选中下标** selPos、N<n> = 内部**已协商下标** negPos；
 *      · PSRDY / REQ / WAITCAP / ATTACH … = 状态机枚举名（原 stName()）。
 *    这些对用户没有任何意义，**已全部移除**（stName() 一并删掉）。
 *    档位信息由下面 PDO 列表的反白高亮表达，不需要在标题里复述。
 *    调试期若还想看状态机 / 下标，读 OpenOCD 的 g_snap：
 *      [3] negPos|armed   [8] st   [14] selPos   [15] ack   [9] pdoCount
 *    （行0 只有 22 列；CC/VBUS 也一直只进 g_snap，不上屏。） */
void DrawHeader()
{
  HeaderBar();
  Row(0, "PD FIX DECOY", ST7735::WHITE, C_BAR);
}

/* 行7：按键提示（常量，只会真正画一次） */
void DrawHint()
{
  Row(HINT_ROW, "1CLK:NEXT 2CLK:SET", C_DIM);
}

/* 行1..行6：PDO 区。
 *   · scr == SCR_PDO    → 画 PDO 列表：第 selPos 个反白高亮 = "当前选中"，
 *                         且 selPos != negPos（选中但还没协商成功）时行首加 '*'；
 *   · scr == SCR_NO_PDO → 明写 "0 PDO"（源端在，但它没广播 PD caps）；
 *   · scr == SCR_NO_SRC → 明写 "NO SRC PLUG IN"（Type-C 上根本没插源端）。 */
void DrawPdoList(const FUSB302_Message& msg, uint8_t selPos, uint8_t negPos,
                 e_Screen scr)
{
  char body[COLS + 1];
  char buf[COLS + 1];
  uint8_t shown = 0u;

  if (scr == SCR_PDO) {
    for (uint8_t i = 0; i < msg.pdoCount && i < PDO_ROWS; ++i) {
      const bool     sel     = (static_cast<uint8_t>(i + 1u) == selPos);
      const bool     pending = sel && (selPos != negPos);   /* 选中但未协商 */
      const uint16_t bg      = sel ? C_SEL_BG : ST7735::BLACK;
      const uint16_t fg      = sel ? ST7735::WHITE : C_KEY;

      FUSB302_PDO p;
      if (!FUSB302::decodePdo(msg.pdo[i], p)) {
        /* 解码失败（保留编码 / 畸形 caps）—— 只报"未知"，**不**把裸 32 bit
         * 十六进制打上屏（那是开发期调试信息，已移除）。 */
        (void)snprintf(body, sizeof(body), "%u ???", static_cast<unsigned>(i) + 1u);
        (void)snprintf(buf, sizeof(buf), "%s%s", pending ? "*" : "", body);
        Row(static_cast<uint8_t>(PDO_ROW0 + i), buf, C_NG, bg);
      } else {
        PdoToRow(i, p, body, sizeof(body));
        (void)snprintf(buf, sizeof(buf), "%s%s", pending ? "*" : "", body);
        Row(static_cast<uint8_t>(PDO_ROW0 + i), buf, fg, bg);
      }
      shown = static_cast<uint8_t>(i + 1u);
    }
  }

  /* 列表是空的：按"源端插没插"给不同的话 ——
   * 别把"线都没插"说成 "0 PDO"，那会让人以为源端不配合。 */
  if (shown == 0u) {
    if (scr == SCR_NO_SRC) {
      Row(PDO_ROW0, "NO SRC PLUG IN", C_WAIT);
    } else {
      Row(PDO_ROW0, "0 PDO", C_NG);
    }
    shown = 1u;
  }

  /* 末尾补空行，抹掉上一帧可能更长的列表 */
  for (uint8_t r = shown; r < PDO_ROWS; ++r) {
    Row(static_cast<uint8_t>(PDO_ROW0 + r), "", ST7735::BLACK);
  }
}

/*==========================================================================
 * ★ I²C 总线自救：解开 FUSB302 卡住 SDA 的死锁
 *
 * 【症状 / 为什么会卡】
 *   FUSB302 若在"某个 bit 上"被打断（最典型：MCU 正在读它时被复位 / 重烧固件），
 *   它会一直把 SDA 拉低等下一个 SCL。同时 I2C 外设 SR2.BUSY 也锁死，库里的
 *   waitBusFree()/waitFlag() 全部超时 → init() 永远失败 → pd.isReady() 恒 0。
 *   现象就是：屏上 "PRESS KEY" 一直重画（1 号块每 500ms 置 dirty），按键却毫无反应
 *   （唤醒条件 `!armed && pd.isReady() && key.stable==0` 卡在 isReady 上）。
 *
 * 【怎么救】
 *   标准做法：把 SCL 当普通 GPIO 敲最多 9 个时钟，把从设备从"卡住的 bit"顶上
 *   总线，再补一个 STOP，然后把引脚/外设还给 I2C1。
 *   · 只在真的卡住时动手（SDA 低 或 BUSY）—— 否则无害早退。
 *   · 不会和正常事务撞车：I²C 事务是同步的，主循环在跑就说明没有事务在途，
 *     此时 SDA 还低 = 一定是卡死。
 *==========================================================================*/
void FusbBusRecover()
{
  /* 早退：总线空闲（SDA 高 且 非 BUSY）就什么都不做 */
  if ((LL_GPIO_IsInputPinSet(GPIOF, LL_GPIO_PIN_0) != 0u) &&
      (LL_I2C_IsActiveFlag_BUSY(I2C1) == 0u)) {
    return;
  }

  LL_I2C_Disable(I2C1);

  LL_GPIO_InitTypeDef gi = {0};

  /* SDA(PF0)：输入 + 上拉，只观察 */
  gi.Pin        = LL_GPIO_PIN_0;
  gi.Mode       = LL_GPIO_MODE_INPUT;
  gi.Pull       = LL_GPIO_PULL_UP;
  LL_GPIO_Init(GPIOF, &gi);

  /* SCL(PF1)：通用开漏输出，先释放为高 */
  gi.Pin        = LL_GPIO_PIN_1;
  gi.Mode       = LL_GPIO_MODE_OUTPUT;
  gi.Pull       = LL_GPIO_PULL_UP;
  gi.Speed      = LL_GPIO_SPEED_FREQ_HIGH;
  gi.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  LL_GPIO_Init(GPIOF, &gi);
  LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_1);

  /* 敲最多 9 个 SCL（24MHz 下每个沿停几十 µs，够慢） */
  for (uint8_t i = 0u; i < 9u; ++i) {
    LL_GPIO_ResetOutputPin(GPIOF, LL_GPIO_PIN_1);
    for (volatile uint32_t d = 0u; d < 300u; ++d) { }
    LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_1);
    for (volatile uint32_t d = 0u; d < 300u; ++d) { }
    if (LL_GPIO_IsInputPinSet(GPIOF, LL_GPIO_PIN_0) != 0u) { break; }  /* SDA 已释放 */
  }

  /* 补一个 STOP：SCL 高时 SDA 低→高 */
  gi.Pin        = LL_GPIO_PIN_0;
  gi.Mode       = LL_GPIO_MODE_OUTPUT;
  gi.Pull       = LL_GPIO_PULL_UP;
  gi.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
  LL_GPIO_Init(GPIOF, &gi);
  LL_GPIO_ResetOutputPin(GPIOF, LL_GPIO_PIN_0);      /* SDA 低 */
  LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_1);        /* SCL 高 */
  for (volatile uint32_t d = 0u; d < 300u; ++d) { }
  LL_GPIO_SetOutputPin(GPIOF, LL_GPIO_PIN_0);        /* SDA 高 = STOP */
  for (volatile uint32_t d = 0u; d < 300u; ++d) { }

  /* 引脚 + 外设还给 I2C1（Studio_I2C1_Init 会重置外设并把 PF0/PF1 重配成 AF12） */
  Studio_I2C1_Init();
}

} // namespace
/* USER CODE END 0 */

/*==========================================================================*/
/* 应用主循环（永不返回）                                                    */
/*==========================================================================*/
/* USER CODE BEGIN ExternalFunctions */

/* ★ 必须显式 extern "C"：main.c 是 C，App_Run 是 C++ TU 里的定义。
 *   少了它，两边名字修饰不一致 → 链接期 "Undefined symbol App_Run"。 */
extern "C" void App_Run(void)
{
  using namespace LoveFinderLib;

  FUSB302         pd;
  FUSB302_Message msg;
  FUSB302_Message lastMsg = {};

  bool haveCap  = false;
  bool attached = false;
  bool doReset  = true;      /* 只有开机那次 init 允许软复位 */
  uint8_t devId = 0u;        /* ★ 缓存：deviceId() 内部要读一次 I²C，
                              *   绝不能每轮主循环都调（会白烧一条事务/轮）。 */

  /* 唤醒 / 目标档位 */
  bool     armed   = false;                  /* 用户已按键唤醒 → 才开始 PD 业务 */
  bool     toggleOn = false;                 /* startSinkToggle 是否已调用（唤醒后一次） */
  uint16_t tgtMv   = PD_TARGET_MV_DEFAULT;   /* 选中档位的电压（屏上摘要用），默认 5V */
  uint16_t tgtCur10 = 0u;                    /* 该 PDO 报的最大电流（10mA） */
  uint8_t  selPos  = 0u;                     /* **高亮选中**的 Object Position（0=无） */
  uint8_t  negPos  = 0u;                     /* **已协商成功**的 Object Position（0=无） */

  uint8_t  st      = ST_WAIT_KEY;            /* 上电默认停在"等按键" */
  bool     rdoAck  = false;                  /* 收到 PS_RDY = 申请成功 */

  uint32_t tAttach = 0u, tRx = 0u, tRetry = 0u, tUi = 0u, tInt = 0u;
  uint32_t tVbusLow = 0u, tDetach = 0u, tHrRecover = 0u, tRecover = 0u, tCcGone = 0u;
  bool     ccSeenOk = false;                 /* 挂上后是否见过 BC_LVL != 0（快通道自检） */
  uint32_t loops = 0u;
  uint32_t tCapWait = 0u;                    /* 进入 ST_WAIT_CAP 的时刻（Soft Reset 计时基准） */
  uint8_t  softRstSent = 0u;                 /* 本轮已补过几枪 Soft Reset */
  uint8_t  prevSt = ST_WAIT_KEY;             /* 上一轮状态：用来捕获"刚进入 ST_WAIT_CAP"这个沿 */
  uint8_t  hrTries = 0u;                     /* Hard Reset 恢复窗口内已补过几次 recover */
  uint32_t tRxIdle = 0u;                     /* 待机（未按键）排空 RX FIFO 的节拍 */
  uint32_t tCapSeen = 0u;                    /* 待机期间最近一次读到 caps 的时刻（0=还没读到）
                                              *   —— 按键时用它区分"源端还在广播" vs "已安静" */

  bool dirty = true;

  UiFp uiFpLast  = {};
  bool uiFpValid = false;

  KeyState key;

  /* 诊断累加器 */
  uint8_t  intaAcc = 0u, int0Acc = 0u, intbAcc = 0u;
  uint8_t  lastS1 = 0u, lastS0a = 0u, lastI0 = 0u, lastVbusOk = 0u, lastCcOk = 0u;
  uint32_t keyShort = 0u, keyDouble = 0u, keyLong = 0u;
  uint32_t hbHits = 0u;                      /* 心跳顶事次数（正常应恒为 0） */

  /* ⓪ 开机 -------------------------------------------------------------- */
  g_log_t0 = BSP_GetTick();
  Log(L_BOOT, 0, 0);

  /* ① 先把屏点亮。
   * 整板与 DAPLink 取自电脑 3V3（不受 PD VBUS 影响），所以屏驱可以放在
   * PD 之前 —— 屏全黑时根本分不清"固件没跑"还是"在等源端"。 */
  ST7735_Init();
  ST7735_FillScreen(ST7735::BLACK);

  /* ★ 开机就把整帧铺一遍，把行缓存填满。
   *
   * 【为什么必须在这里补】
   *   ST7735_FillScreen() 是直接整屏灌像素，**不经过 Row()**，
   *   所以它一个缓存槽都不会填。不预铺的话，第一次 DrawXxx() 会一次画
   *   9 个矩形 ≈ 35ms —— 而这一帧正好可能压在"收到 caps → 24ms 内回
   *   Request"的窗口上（实测差 0.74ms 就失败）。
   *
   * 【画什么】直接画**最终形态**：标题栏 + 空 PDO 区 + 按键提示。
   *   ⚠️ 这里以前写的是 "PRESS KEY"（黄字）—— 那是"上电等按键唤醒"老流程的
   *      遗留。选项 2 开机就自己把协商谈完，根本不会停在那一步，留着只会让
   *      用户看到一下**错的**提示。主循环第一帧随即把 PDO 列表填进来。
   *
   * 【代价】
   *   开机多烧 ~30ms 的 SPI。这一步在 PD 协商之前，没有任何 deadline。 */
  DrawHeader();
  for (uint8_t r = PDO_ROW0; r < HINT_ROW; ++r) { Row(r, "", ST7735::BLACK); }
  DrawHint();
  Log(L_LCD, 0, 0);

  /* ② 配好 FUSB302，并**立刻补一枪 Soft Reset** 清掉上一轮电源合约。
   *
   * ★ 需求：上电后等用户按键，按键之后才显示 PDO、才协商 5V；
   *   但"复位以后必须发一次 Soft Reset"是独立的硬性要求（见 ②.5），
   *   它要在按键**之前**就看到旧电压被清掉。
   *
   * ⚠️ 这里不显示 PDO、不协商 —— PDO 列表 / 默认 5V 申请仍然要等"按键唤醒"，
   *   见主循环 1.5 / 2 / 3 号块。 */
  FusbBusRecover();             /* ★ 先解 I²C 死锁，否则下面 init 必失败 */
  if (pd.init(I2C1)) {
    Log(L_INIT, 1, 0);
    devId = pd.deviceId();
    Log(L_DEVID, devId, 0);
  } else {
    Log(L_INIT, 0, 0);
  }
  doReset = false;              /* 之后的重试绝不再软复位（会撤 Rd → 变成拔出） */
  tRetry  = BSP_GetTick();

  /* ②.5 ★★★ 开机第一枪：给源端补一条 PD Soft Reset ★★★
   * ====================================================================
   * 【为什么必须发】
   *   FUSB302 由板上 3V3 常供 —— **MCU 复位并不会让它掉电**：Switches0 里的
   *   Rd、Switches1 的 TXCC、以及"上一轮谈成的合约"全部原样保留。源端那边
   *   同样不知道我们复位过，于是它认为"SINK 还挂着、旧合约（可能是 9V/12V/
   *   20V）仍然有效"，不会再广播 Source_Capabilities。
   *   结果就是：**复位以后输出一直停在上一轮协商的电压**上。
   *   发一条 Soft Reset，让源端把协议层和电源合约一起清零，退回 vSafe5V
   *   并重新广播 caps。
   *
   * 【为什么必须是"开机就发"，而不是"按键再发"】
   *   业主实测后明确要求：这一枪要在程序跑起来之后立刻打出去。放在按键之后，
   *   旧合约会一直挂到用户按下去的那一刻。
   *
   * 【为什么先 toggle + selectCC】
   *   PD 报文必须从**源端真正挂着的那根 CC** 发出去。init() 默认把 TX/MEAS
   *   指到 CC1，而本板源端实测在 CC2 —— 不先探一下，这一枪会发到空线上。
   *
   * 【探不到 attach 怎么办】
   *   没插线时 pollAttach() 一直返回 NONE，这里最多等 PD_BOOT_ATTACH_MS
   *   就往下走，不阻塞进 "PRESS KEY"；按键之后主循环那套常规流程会接手。 */
  if (pd.isReady()) {
    /* ★★ 待机阶段（按键之前）：AUTO_CRC 全程关闭 ★★
     *
     * 【为什么关】AUTO_CRC=1 时芯片对**每一条 CRC 正确的对端报文**都自动回
     *   GoodCRC —— 它是"一视同仁"的，**没法只挑 ACCEPT 回、放过 caps**。
     *   而开机那枪 Soft Reset 之后源端会立刻重播 Source_Capabilities：
     *   这份 caps 一旦被确认，源端就启动 tSenderResponse（实测 ≈28.7ms）
     *   等 Request；而按键之前我们根本不回 Request —— 于是源端超时 →
     *   Hard Reset → VBUS 撤到 0V → 重播 caps → 再被自动 GoodCRC → 再超时……
     *   就是业主抓到的 VBUS 5V/0V 死循环：
     *       SR → SRC GoodCRC/ACCEPT → SNK GoodCRC → SRC CAP → SNK GoodCRC
     *         → +28.7ms → HRST → VBUS 0V → …（PDCAP_1.csv #5/#6/#7 那一段）
     *
     * 【关掉之后怎么办】芯片"只收不回"（RX 完全不受影响，报文照样进 FIFO）。
     *   ★ 唯一必须确认的是源端对我方 SR 的应答 ACCEPT —— 那条由 ②.6 的
     *     "硬件 AUTO_CRC 小窗口"确认（收到 ACCEPT 立刻关窗口）。**别**用软件
     *     sendGoodCrc 补：~0.6~0.7ms 远超规范 tTransmit(195µs)，实测源端会
     *     把它当没收到 → 重发 ACCEPT×3 → Hard Reset。
     *   ⚠️ 实测补充（本轮 OpenOCD 仪器化）：即使 caps 一条都不确认，SW3526S
     *     也会在 ~28ms 内 **caps×3 → Soft Reset×3 → Hard Reset**。也就是说
     *     "发 SR + 什么都不回 + 干等按键"这条路，源端自己就会以 HRST 收场 ——
     *     与"待机不协商"的目标冲突。见 ②.6 的"已知局限"。
     *
     *   等到"按键唤醒"（1.5 号块）才 setAutoCrc(true) —— 那时我们已经能
     *   在收到 caps 后立刻回 Request，28ms 窗口绰绰有余。 */
    (void)pd.setAutoCrc(false);
    Log(L_CRC, 0, 0);

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
      attached = true;              /* 已 attach → 主循环不必再挂一次 */
      Log(L_ATTACH, static_cast<uint8_t>(cc), 0);
      Log(L_SELCC, sel ? 1u : 0u, pd.specRev());

      /* ②.6 ★ 发 Soft Reset，然后**只**确认源端的 ACCEPT，绝不确认 caps ★
       * ---------------------------------------------------------------
       * 【源端（SW3526S）对我方 SR 的应答序列 —— 实测】
       *   #1 SNK SOFT RESET                       ← 我方
       *   #2 SRC GOOD CRC   (+~1ms)               ← 源端确认我方报文，无需回应
       *   #3 SRC ACCEPT     (+~7ms)               ← ★ 必须被确认（见下）
       *   #4 SRC SOURCE CAP (+~2ms)               ← ★ 绝不能确认
       *   旧固件在 #4 上回了一条 GoodCRC → 源端启动 tSenderResponse(24~30ms)
       *   等 Request → 等不到 → HRST 把 VBUS 拉低。这就是要修的 bug。
       *
       * 【为什么 ACCEPT 必须用"硬件"确认】
       *   PD 规范 tTransmit ≤195µs：收到报文后必须在 195µs 内回 GoodCRC。
       *   实测（本轮 OpenOCD 仪器化）：用软件 sendGoodCrc 补（~0.6~0.7ms）
       *   源端视为"没确认"→ **重发 ACCEPT 三次**（845/847/849ms）→ 仍不满足
       *   → Hard Reset。只有硬件自动 GoodCRC（AUTO_CRC）做得到 195µs。
       *
       * 【但 AUTO_CRC 是"一视同仁"的，不能一直开着】
       *   开着不放，紧接着那组 Source_Capabilities 也会被硬件确认 → 又回到
       *   上面那个"等 Request 超时"的 HRST。所以只开**一小段窗口**：
       *   收到 ACCEPT 就立刻关（实测 ACCEPT→caps 隔 ~1.1ms，关窗口 ~0.2ms，
       *   来得及）。见下面代码。
       *
       * 【为什么这里不发 Request】
       *   业主的要求：复位后只清掉旧合约（SR），**不建新合约**，一直等到按键。
       *   真正的协商留给按键之后 —— 见主循环 1.5 / 3 / 3.5 号块。
       *
       * ⚠️【已知局限 —— 实测】SW3526S 在 SR 之后会进入"主动协商"态：若 SINK
       *   既不确认 caps、也不回 Request，它会在 ~28ms 内 **caps×3 → Soft Reset×3
       *   → Hard Reset**，VBUS 掉 ~0.86s 再回来（实测：838ms 发 SR → 845ms ACCEPT
       *   （硬件确认）→ 846ms 关窗口 → 847/849/851ms caps×3（不确认）→
       *   852/854/855ms 源端 Soft Reset×3 → 868ms HRST → 889ms VBUS 掉 →
       *   1748ms VBUS 回）。反过来，若**开机不发 SR**，源端全程静默、VBUS 稳在
       *   vSafe5V、无 HRST（Rd 在 MCU 复位期间一直有效，源端早就放弃广播了）。
       *   即：只要发了 SR，源端就要"谈完"；"SR + 什么都不回 + 干等按键"这条
       *   路，源端本身会以 HRST 收场。取舍见交付说明。 */
      {
        /* ★★ 选项 2：开机就把这一轮协商谈完 —— AUTO_CRC 直接常开 ★★
         * ---------------------------------------------------------------
         * 【为什么不再用"只确认 ACCEPT 的小窗口"】
         *   上一版为了"只确认 ACCEPT、绝不确认 caps"，把 AUTO_CRC 只开一小段。
         *   但实测（OpenOCD 读固件日志环）发现：SW3526S 收到 SR 之后会进入
         *   "主动协商"态，若 SINK 既不确认 caps、也不回 Request，它会在 ~28ms 内
         *       caps×3 → Soft Reset×3 → Hard Reset
         *   把 VBUS 拉低 ~0.86s 再重建。也就是说"发 SR + 什么都不回 + 干等按键"
         *   这条路，源端自己就会以 HRST 收场 —— 与"待机不协商"的目标冲突。
         *
         * 【所以改成：发完 SR 立刻谈完】
         *   AUTO_CRC 常开 → 硬件在规范 tTransmit(≤195µs) 内确认 ACCEPT，
         *   随后确认 Source_Capabilities；主循环 3 号块一收到 caps 就**立刻**
         *   回 Request（首个 5V Fixed PDO），24~30ms 窗口绰绰有余。
         *   这样既清掉了旧合约（SR），又满足了源端"谈完"的诉求，不掉 VBUS。
         *
         * 【为什么必须用硬件确认 ACCEPT】
         *   PD 规范 tTransmit ≤195µs。软件 sendGoodCrc 要 0.6~0.7ms，实测源端
         *   把它当"没收到"→ 重发 ACCEPT×3 → Hard Reset。只有硬件做得到。 */
        (void)pd.rxFlush();           /* 先清掉复位前残留的 caps，别顶掉新一轮的 */
        pd.setAutoCrc(true);
        Log(L_CRC, 1, 0);

        const bool srOk = pd.sendControl(Bit::PD_CTRL_SOFT_RESET);
        Log(L_SOFTRST, srOk ? 1u : 0u, 0);

        /* ★★ 这里**绝对不能** resetMsgId() ★★（曾经照抄库里那句注释，踩了坑）
         *
         * 【实测证据 —— OpenOCD 读固件日志环，同一次运行】
         *   · 我方 SR 之后：源端重播的 caps 带 **msgId = 1**
         *     （L_DBG a1=0x81）→ 说明源端**没有**把 MessageID 归零，
         *     它接着 SR 的 msgId 0 往下数。
         *   · 源端自己发 Hard Reset 之后：源端重播的 caps 带 **msgId = 0**
         *     （L_DBG a1=0x80）→ 这才是"计数器归零"。
         *
         * 【为什么 resetMsgId() 会让协商失败】
         *   sendControl(SR) 已经用了 msgId 0 并把计数推到 1。若这里再清零，
         *   随后那条 Request 就会带 msgId 0 —— 而源端刚收过 msgId 0 的 SR，
         *   按"上一包 +1"它期望的是 **1** → 判为重复/乱序 → **不回 GoodCRC**。
         *   库里 Control3=0（AUTO_RETRY 关）→ 我方不会重发、也不会置
         *   I_RETRYFAIL，所以中断里**一条线索都没有**；源端那边则静静等
         *   tSenderResponse 超时 → 发 Hard Reset 把 VBUS 拉低。
         *   实测时序完全对上：855ms 发 Request(ok=1) → 877ms 收 HRST
         *   （= 源端 caps +28.7ms 超时）。
         *
         * 【结论】Soft Reset **不重置** MessageID（只有 Hard Reset 才重置，
         *   而 Hard Reset 由库里的 pdReset()/recover() 负责清零）。
         *   所以这里什么都不做，Request 自然带上源端期望的 msgId 1。 */

        /* 这里**不**等待、**不**发 Request —— 立刻交给主循环：
         *   2 号块 维持 attach / VBUS 去抖；
         *   3 号块 收到 caps → 立刻回 Request → 处理 ACCEPT / PS_RDY；
         *   3.5 块 久等不到 caps 就再补 SR；
         *   4 号块 处理 HRST。 */
      }

      /* 源端收到 Soft Reset 会先把 VBUS 撤低再重建，开恢复窗口别当拔出。
       * 顺带：这个窗口也压住 3.5 号块，别在开机 SR 还没走完时就抢着补枪。 */
      tHrRecover = BSP_GetTick();
      tRecover   = tHrRecover;
      hrTries    = 0u;
    } else {
      Log(L_ATTACH, 0, 0);          /* arg0=0：开机探测没等到 attach（没插线？） */
    }
  }

  /* ★★ 选项 2：不等按键 —— 开机这枪 SR 之后**立刻把这一轮协商谈完** ★★
   *   直接置 armed，主循环 2/3/3.5 号块会走完
   *       attach → 收到 caps → 回 Request(首个 5V Fixed PDO) → ACCEPT → PS_RDY
   *   屏上随即显示 PDO 列表并默认激活第 1 档。
   *   ⚠️ 原来的"上电停在 PRESS KEY、等按键唤醒"流程已废弃 —— 见 1.5 号块。 */
  armed       = true;
  st          = attached ? ST_WAIT_CAP : ST_WAIT_ATTACH;
  tCapWait    = BSP_GetTick();
  softRstSent = 0u;

  /* ③ 主循环 ------------------------------------------------------------ */
  for (;;) {
    const uint32_t now = BSP_GetTick();
    loops++;

    /* ★ 取走中断标志 —— 必须在所有服务分支之前。
     * 顺序是"先清再服务"：服务期间 ISR 又置起来的那一次不会被抹掉，
     * 下一轮立刻再跑一遍。
     * g_pdIrq 只在 ISR 里置 1、只在主循环里清 0，是标准的单写者/单读者握手；
     * Cortex-M0+ 上 8 位 volatile 读/写本身原子，不用关中断。 */
    const bool irqSet = (g_pdIrq != 0u);
    if (irqSet) { g_pdIrq = 0u; }

    /* 按键标志同样取走（逻辑上不依赖它 —— KeyService 直接采样引脚 ——
     * 但不清掉的话它会一直挂着，也影响 g_keyIrqCnt 的判读）。 */
    if (g_keyIrq != 0u) { g_keyIrq = 0u; }

    /* ---------------- 1. FUSB302 初始化（失败则周期重试） ---------------- */
    if (!pd.isReady() && ((now - tRetry) >= PD_CFG_RETRY_MS)) {
      tRetry = now;
      FusbBusRecover();         /* ★ 重试前先解 I²C 死锁（空闲时无害早退） */
      if (pd.init(I2C1, FUSB302_I2C_ADDR, doReset)) {
        devId = pd.deviceId();
        Log(L_INIT, 1, 0);
        /* ★ init() 会按 m_autoCrc 重写 Switches1；这里按当前阶段校正一次：
         *   待机（未唤醒）= 关，已唤醒 = 开。见 ②.5 的说明。 */
        (void)pd.setAutoCrc(armed);
      } else {
        Log(L_INIT, 0, 0);
        haveCap = false;
      }
      dirty = true;
    }

    /* ---------------- 1.5 ★ （选项 2 下已废弃）上电唤醒：等按键 ----------------
     * ⚠️ 选项 2 把"等按键唤醒"改成了"开机直接谈完"：armed 在进主循环**之前**
     *    就已经是 true，所以本块的 `!armed` 永远为假 —— **整块不会执行**。
     *    保留只为留痕。若要回到"上电停在 PRESS KEY、等按键才开始"的老流程：
     *    把进主循环前那句 `armed = true;` 改回 `st = ST_WAIT_KEY;` 即可，
     *    本块与 1.6 号块会自动复活（无需其它改动）。 */
    if (!armed && pd.isReady() && (key.stable == 0u)) {
      armed        = true;
      key.consumed = true;
      dirty        = true;
      Log(L_ARM, 0, 0);

      /* ★ 开机那枪 Soft Reset 之后，FUSB302 的 PD 接收逻辑可能被锁死
       *   （实测：源端常以 Hard Reset 回应 Soft Reset，之后 Status1.RX_EMPTY
       *   恒为 1，一条报文都进不了 RX FIFO）。这里等价补一次 PD_RESET ——
       *   离发出 Soft Reset 已经过去很久，不会打断报文。
       *   没有这一步，按键之后可能永远收不到源端重播的 caps。
       *   顺带把待机期间攒下的 caps 重播全部冲掉 —— 那些是"旧的一轮"，
       *   按键之后要以**新到的那一份** caps 为准。 */
      if (attached) {
        const bool ok = pd.recover();
        Log(L_RECOVER, ok ? 1u : 0u, static_cast<uint8_t>(pd.specRev()));
      }

      /* ★★ 按键唤醒：重新打开硬件自动 GoodCRC ★★
       *   从这里开始我们才具备"收到 caps 后 28ms 内回 Request"的能力，
       *   所以要让芯片重新自动确认源端的报文（这一轮 caps 必须被确认 ——
       *   确认完我们**立刻**就回 Request，不再有 28ms 超时的风险）。
       *   ⚠️ 必须排在 recover() **之后** —— recover() 内部 selectCC() 会按
       *      m_autoCrc 重写 Switches1，此时还是 off，正好把 recvr 期间的
       *      Switches1 也保持一致；紧接着这里再把它打开。 */
      (void)pd.setAutoCrc(true);
      Log(L_CRC, 1, 0);

      /* ★ st 在这里才切。业主定义的两种情形，靠"待机期间有没有读到过 caps"
       *   （tCapSeen）区分：
       *
       *   情形 1 —— 源端**还在广播**（tCapSeen 很新）：
       *     直接进 ST_WAIT_CAP，先短听 PD_SOFTRST_FIRST_MS。源端那一组 caps
       *     马上就到，3 号块收到后立刻回 Request（首个 5V Fixed PDO）。
       *     屏上随即显示 PDO 列表并默认激活第 1 档。
       *
       *   情形 2 —— 源端**已经安静**（只有 Rd 撑着的 vSafe5V）：
       *     没有 caps 可听，3.5 号块的"首枪等待"取 0 → **立刻**补一枪
       *     Soft Reset 把它叫醒；它重播 caps（这一份会被硬件自动确认），
       *     3 号块随即回 Request。
       *
       * ⚠️ tCapWait / softRstSent 一并重置：开机那枪 Soft Reset 之后源端
       *    可能回了一记 Hard Reset，4 号块（不看 armed）已经把 st 置成了
       *    ST_WAIT_CAP —— 不重置的话 3.5 号块的"先短听"会被跳过。 */
      tCapWait    = now;
      softRstSent = 0u;
      st = attached ? ST_WAIT_CAP : ST_WAIT_ATTACH;
    }

    /* ---------------- 1.6 ★ （选项 2 下已废弃）待机排空 RX FIFO ----------------
     * ⚠️ 同 1.5 号块：armed 恒为 true，所以 `!armed` 永远为假 —— 整块不执行。
     *    保留只为留痕（以及万一回退到"等按键"流程时直接复活）。
     *    原用途：待机期间源端会周期性重播 caps，我们一条都不确认，但必须把
     *    FIFO 读走，否则打满（Status1.RX_FULL）之后应答就进不来了。 */
    if (pd.isReady() && !armed && ((now - tRxIdle) >= PD_STANDBY_DRAIN_MS)) {
      tRxIdle = now;
      for (uint8_t g = 0u; g < 8u; ++g) {
        if (pd.rxEmpty()) { break; }
        /* ★ 复用主循环那份 msg 缓冲（待机与 3 号块互斥，不会打架）——
         *   省一个 50 字节的栈对象，PY32F003 只有 4KB SRAM。 */
        if (!pd.fetchMessage(msg)) { break; }
        if (msg.isSourceCap) { tCapSeen = now; }   /* 源端还在广播 */
      }
    }

    /* ---------------- 2. attach + VBUS 时间型去抖 ----------------
     * ★ 触发源：中断（irqSet）或 PD_ATTACH_POLL_MS 心跳，两者取或。
     *   VBUS 变化（I_VBUSOK）和 toggle 落定（I_TOGDONE）都会立刻把这一块叫醒。
     *   ⚠️ 未唤醒（!armed）时整块跳过 —— 一个 PD 事务都不发。 */
    if (armed && pd.isReady() && (irqSet || ((now - tAttach) >= PD_ATTACH_POLL_MS))) {
      tAttach = now;

      /* 唤醒后第一次进来：挂 Rd（toggle）。源端看到 sink attach 才会广播 caps。 */
      if (!toggleOn) {
        toggleOn = pd.startSinkToggle();   /* 失败则下轮重试 */
        Log(L_TOGGLE, toggleOn ? 1u : 0u, 0);
      }

      uint8_t s0 = 0u;
      const bool busOk = pd.readReg8(Reg::STATUS0, s0);
      const bool vbusOk = busOk && ((s0 & Bit::S0_VBUSOK) != 0u);
      const bool vbusLowNow = busOk && !vbusOk;
      if (busOk) { lastVbusOk = vbusOk ? 1u : 0u; }

      /* ★ CC 终结（BC_LVL）跟踪 —— 拔出快通道的输入。
       *   一变就记一条 L_CC，并驱动 6 号块的 srcPresent。
       *   ★ 为什么不能只看 VBUS：拔线后 VBUS 会被体电容拖住 ~1.4s
       *     （实测 2025ms 拔、3450ms 才掉），这段时间屏会错显 "0 PDO"。
       *   ⚠️ **PD 突发期间（ACTIVITY=1）整段跳过**：测量块被收发占用，
       *      BC_LVL 会假读 0（实测 853ms STATUS0=0xd0，9ms 后恢复 0x93）。
       *      跳过 = 保持上一次有效值 → 既不误判拔出，屏也不会闪一下。 */
      const bool ccMeasurable = ((s0 & S0_ACTIVITY) == 0u);
      if (busOk && ccMeasurable) {
        const uint8_t ccOk = ((s0 & S0_BC_LVL_MASK) != 0u) ? 1u : 0u;
        if (ccOk != 0u) { ccSeenOk = true; }   /* 快通道自检：见过"有终结"才敢信"没终结" */
        if (ccOk != lastCcOk) {
          lastCcOk = ccOk;
          dirty    = true;
          Log(L_CC, ccOk, s0);
        }
      }

      /* Hard Reset 恢复窗口：这段 VBUS 低是**预期**的（源端要先撤到 vSafe0V
       * 再重建 vSafe5V，实测 ~0.9s），绝不能判成"拔出"。 */
      const bool inHrRecover =
          (tHrRecover != 0u) && ((now - tHrRecover) < PD_HR_RECOVER_MS);

      /* ★ 拔出快通道的计时：CC 终结丢了 = 源端 Rp 没了 = 真拔出。
       *   **四道守卫，缺一不可**：
       *   ① **vbusOk** —— HR 会把 VBUS 拉低，那种情况留给下面的 VBUS 路径，
       *      免得源端在 HR 期间短暂撤 Rp 被误判成拔出；
       *   ② **ccSeenOk** —— selectCC 刚生效时 BC_LVL 还没稳定，
       *      没这个自检就会在插上的瞬间误判成"拔出"；
       *   ③ **ccMeasurable（ACTIVITY == 0）** —— PD 突发期间 BC_LVL 会假读 0，
       *      不加这个，一次长突发（caps→Request→Accept→PS_RDY，实测约 56ms）
       *      就可能被误判成拔出；
       *   ④ **PD_DETACH_CC_MS(20ms) 去抖** —— 滤掉单次采样抖动。
       *   若 BC_LVL 全程为 0（该配置下测不出终结），快通道自动失效 → 退回 VBUS 路径。 */
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
        /* 掉过又回来 = 源端重启过（Soft/Hard Reset 都会这样）→ PD_RESET 复活。
         * 实测 FUSB302 收到 Hard Reset 后 Status1.RX_EMPTY 恒为 1，
         * 不复位就永远收不到后面的 caps。 */
        tVbusLow = 0u;
        if (attached) {
          const bool ok = pd.recover();
          Log(L_RECOVER, ok ? 1u : 0u, static_cast<uint8_t>(pd.specRev()));
          tRecover = now;
          if (!haveCap) { st = ST_WAIT_CAP; }
          dirty = true;
        }
      }

      if (!attached) {
        /* 拔出后静置 PD_DETACH_HOLD_MS 才允许重挂，压住自激 */
        const bool holdOver =
            (tDetach == 0u) || ((now - tDetach) >= PD_DETACH_HOLD_MS);
        if (holdOver) {
          const e_FUSB302_CC cc = pd.pollAttach();
          if (cc != e_FUSB302_CC::NONE) {
            const bool sel = pd.selectCC(cc);
            /* ★★ 运行时挂载也必须打开硬件 AUTO_CRC ★★
             *   ②.6 那句 setAutoCrc(true) 挂在 `cc != NONE` 分支里，**只有开机时
             *   源端已经插好**才会执行。若源端是**开机之后**才插上的，开机 init
             *   留下的 setAutoCrc(false) 就一直没人翻回来 →
             *   芯片"只收不回"（RX 不受影响，报文照样进 FIFO）→
             *   源端每条报文都等不到 GoodCRC → 实测 ACCEPT×3 → SOFT RESET×3
             *   → HRST，每 ~1s 一轮死循环，屏上永远是 0 PDO。
             *
             *   【证据】ERRIIC.csv：
             *     · SWITCHES1 全程 = 0x0B（bit2 SW1_AUTO_CRC = 0）—— 确认没开；
             *     · RX FIFO 里能读到源端重发的 ACCEPT ×3 / SOFT RESET ×3，
             *       三遍 payload 逐字节相同（6F AC FA 5D / CD E0 77 2D）。
             *   与 ②.6 的"选项 2：AUTO_CRC 常开"设计一致 —— 挂上就必须开着。 */
            (void)pd.setAutoCrc(true);
            Log(L_CRC, 1, 0);
            attached = true;
            tDetach  = 0u;
            tVbusLow = 0u;
            tCcGone  = 0u;
            ccSeenOk = false;   /* 快通道自检：重新等一次 BC_LVL != 0 */
            st       = ST_WAIT_CAP;
            haveCap  = false;
            selPos   = 0u;
            negPos   = 0u;
            rdoAck   = false;
            dirty    = true;
            Log(L_ATTACH, static_cast<uint8_t>(cc), 0);
            Log(L_SELCC, sel ? 1u : 0u, pd.specRev());
          }
        }
      } else if (!inHrRecover &&
                 (((tVbusLow != 0u) && ((now - tVbusLow) >= PD_DETACH_MS)) ||
                  ((tCcGone  != 0u) && ((now - tCcGone)  >= PD_DETACH_CC_MS)))) {
        /* 真拔出，两条路任取其一：
         *   ① CC 终结丢失 PD_DETACH_CC_MS —— **快通道**（实测比 VBUS 早 ~2.9s）；
         *   ② VBUS 连续低 PD_DETACH_MS   —— 慢通道，兜底（例如源端只给 VBUS）。
         * 清状态、重开 toggle 等下次插入。 */
        const bool fast = (tCcGone != 0u) && ((now - tCcGone) >= PD_DETACH_CC_MS);
        tVbusLow    = 0u;
        tCcGone     = 0u;
        attached    = false;
        haveCap     = false;
        rdoAck      = false;
        selPos      = 0u;
        negPos      = 0u;
        st          = ST_WAIT_ATTACH;
        tDetach     = now;
        (void)pd.startSinkToggle();
        dirty = true;
        Log(L_DETACH, fast ? 1u : 0u, s0);   /* arg0: 1=CC 快通道 / 0=VBUS 慢通道 */
      }

      /* ★ Hard Reset 之后 FUSB302 的 PD 接收逻辑可能锁死
       *   （实测：HR 之后 Status1.RX_EMPTY 恒为 1，一条报文都进不了 FIFO）。
       *   pd.recover() 等价于写一次 Reset.PD_RESET。
       *   ⚠️ 实测有时一次不够，所以在恢复窗口内按 PD_RECOVER_RETRY_MS 重复补 ——
       *      但**必须封顶**（这里 3 次）：recover() 会重钉 CC 路由 + 重配接收前端，
       *      在 2.5s 窗口里无脑重发 8 次反而会把刚建立的接收状态反复打断。
       *   ★ 而且**一旦重新拿到 caps（haveCap）就立刻停** —— 那说明接收已经复活、
       *      甚至已经重新谈成；再补 recover() 只会把刚建立的合约又打断。 */
      if (attached && inHrRecover && !haveCap && (hrTries < PD_HR_RECOVER_MAX) &&
          ((now - tRecover) >= PD_RECOVER_RETRY_MS)) {
        tRecover = now;
        hrTries++;
        const bool ok = pd.recover();
        Log(L_RECOVER, ok ? 1u : 0u, hrTries);
      }
    }

    /* ---------------- 3. RX FIFO —— 找 Source_Capabilities ----------------
     * ★ 触发源：中断（irqSet）或心跳，两者取或。
     *   协商期间（还没 caps）心跳保持 5ms —— 那段时间源端只等 24~30ms 就要
     *   Request；拿到 caps 之后放宽到 100ms。
     *   正常情况下中断总是先到，所以心跳实际上一次都不会触发 ——
     *   用 g_pdRxIrq / hbHits 两个计数器可以验证这一点。 */
    const uint32_t rxHb = haveCap ? PD_RX_IDLE_MS : PD_RX_POLL_MS;
    if (armed && pd.isReady() && (irqSet || ((now - tRx) >= rxHb))) {
      tRx = now;
      if (!irqSet) { hbHits++; }

      /* STATUS1 只读一次，供两个用途（rxEmpty 判据 + 快照）。
       * ⚠️ 不要再调 pd.rxEmpty() —— 它内部会**再读一次 STATUS1**，
       *    同一个 5ms 窗口里读两遍纯属浪费，而且两次读之间状态还可能变。 */
      uint8_t    s1r  = 0u;
      const bool s1Ok = pd.readReg8(Reg::STATUS1, s1r);
      if (s1Ok) { lastS1 = s1r; }

      /* RX_EMPTY(bit5)=1 表示 FIFO 空。
       * 读失败时按"空"处理 —— 一次 I²C 抖动不该让我们去啃一个可能不完整的 FIFO。 */
      const bool rxEmptyNow = (!s1Ok) || ((s1r & Bit::S1_RX_EMPTY) != 0u);

      if (!rxEmptyNow) {
        /* 传 true：上面那次 STATUS1 读已经确认过 FIFO 非空，而且就在**同一次
         * 循环里、只隔了几十微秒**，让库跳过它内部那次重复的 rxEmpty()。 */
        const bool gotCap = pd.pollSourceCapabilities(msg, true);

        if (gotCap) {
          /* ★★★ 收到 Source_Capabilities：**必须立刻回一条 Request(RDO)** ★★★
           *
           * 【为什么不能省】源端发完 caps、并收到我们硬件自动回的 GoodCRC 之后，
           *   会启动 tSenderResponse 定时器（PD 规范 24~30ms）等 SINK 的 Request；
           *   超时没等到 → 源端直接发 Hard Reset、把 VBUS 拉低重启。
           *   实测抓包就是：SRC CAP → SNK GOOD CRC → +28.7ms → HRST（然后循环）。
           *
           * 【为什么**不**按"内容签名"只申请一次】
           *   源端**重播同一份 caps** 恰恰是"上一枪 Request 没被受理"的信号，
           *   此时必须重发 Request；被签名闸门挡住就等于永远不回话，只能等它 HR。
           *   所以这里与 PD_PDO_List_Det 完全对齐：**收到 caps 就回 Request**，
           *   不做任何"这份 caps 已经申请过"的抑制。
           *
           * 【档位】优先挑离 5V 最近的 Fixed PDO；一个 Fixed 都没有（异常）时
           *   **兜底申请 Object Position = 1** —— 绝不出现"收到 caps 却不回话"
           *   （那必然换来一记 Hard Reset）。 */
          lastMsg = msg;
          haveCap = true;
          /* ⚠️ msg.specRev 是 FUSB302_Message 的**字段**；pd.specRev() 才是方法。 */
          pd.setSpecRev(msg.specRev);
          Log(L_CAP, msg.pdoCount, msg.specRev);
          /* 诊断：把 caps 的 MessageID 也记下来 —— 它是判断"源端在 SR 之后
           * 自己的 msgId 计数有没有归零"的直接证据（arg1 高位 0x80 = caps）。 */
          Log(L_DBG, msg.msgType, (uint8_t)(0x80u | (msg.msgId & 0x07u)));

          /* 这一段**必须**留在收到 caps 的同一轮里，不能等 UI 那一路。 */
          if (msg.pdoCount > 0u) {
            uint16_t mv = 0u, cur = 0u;
            uint8_t  pos = findFixedByVoltage(msg, PD_TARGET_MV_DEFAULT, &mv, &cur);
            if (pos == 0u) {                 /* 没有 Fixed → 兜底第 1 档 */
              pos = 1u;
              mv  = static_cast<uint16_t>(((msg.pdo[0] >> 10) & 0x3FFu) * 50u);
              cur = static_cast<uint16_t>(msg.pdo[0] & 0x3FFu);
              if (cur == 0u) { cur = PD_FALLBACK_CUR_10MA; }
            }
            tgtMv    = mv;
            tgtCur10 = cur;
            selPos   = pos;
            rdoAck   = false;
            const bool ok = sendRequestFor(pd, pos, cur);
            /* ★ 只有真发出去了才进入"已请求"；发失败就留在等 caps，
             *   让 3.5 号块的 Soft Reset 再来一枪，而不是沉默地卡住。 */
            if (ok) { st = ST_REQ_SENT; }
            Log(L_TGT, static_cast<uint8_t>(mv & 0xFFu), pos);
            Log(L_RDO, ok ? 1u : 0u, pos);
          }
          dirty = true;
        } else {
          /* 不是 caps：看看是不是对端对我方 Request 的应答。
           * Accept(0x03) / PS_RDY(0x06) / Reject(0x04) / Wait(0x0C) 都是控制报文，
           * pollSourceCapabilities() 为了找 caps 会把它们取出来丢掉，
           * 所以库把"最后一条被跳过的 msgType"记在 lastPeerMsgType() 里。
           * 读到就 clear（"消费"掉），否则同一条应答会在后面每一轮里被反复处理。 */
          const uint8_t peer = pd.lastPeerMsgType();
          if (peer != 0u) {
            pd.clearLastPeerMsgType();
            if (peer == Bit::PD_CTRL_PS_RDY) {
              /* ★ 电源已切到申请的档位 = 诱骗成功；把它记成"已协商位"，
                 高亮的 '*' 前缀因此消失。 */
              rdoAck = true;
              negPos = selPos;
              st     = ST_PSRDY;
              dirty  = true;
              Log(L_PSRDY, 1, peer);
            } else if (peer == Bit::PD_CTRL_ACCEPT) {
              /* 只是受理了，电源还没切 —— 继续等 PS_RDY */
              st    = ST_ACCEPTED;
              dirty = true;
              Log(L_ACCEPT, 0, peer);
            } else if (peer == Bit::PD_CTRL_REJECT) {
              rdoAck = false;
              st     = ST_REJECTED;
              dirty  = true;
              Log(L_PSRDY, 0, peer);
            } else if (peer == Bit::PD_CTRL_WAIT) {
              st    = ST_REJECTED;
              dirty = true;
              Log(L_PSRDY, 0, peer);
            }
          }
        }
      }
    }

    /* ---------------- 3.5 ★ 久等不到 caps → 主动发 Soft Reset 叫醒源端 ----------------
     *
     * 【现象 / 为什么需要】
     *   源端（SW3526S）看到 Rd 之后只广播约 5s 的 Source_Capabilities。一旦这台
     *   SINK 因为 toggle/attach 慢了一拍错过窗口，源端就彻底安静 —— PD 分析仪上
     *   既不见它再发 caps，也不见它发 Hard Reset（它只是保持 vSafe5V 干等）。
     *   唯一出路是 SINK **主动发一条 Soft Reset**：源端回 GoodCRC + Accept，随后
     *   重播 Source_Capabilities。本块就做这件事。
     *
     * 【策略】
     *   · st 每次变为 ST_WAIT_CAP 都重开计时（相当于"新一轮等 caps"）；
     *   · Hard Reset 恢复窗口内不发（那段时间源端本来就会重播 caps）；
     *   · **首枪等待时间看按键那一刻源端的状态**（业主定义的两种情形）：
     *       - 源端还在广播（tCapSeen 很新）→ 等 PD_SOFTRST_FIRST_MS，
     *         给它一组 caps 的时间，能直接读到就不必打扰它；
     *       - 源端已经安静 → 首枪等待取 **0**，按键落下就**立刻**补一枪
     *         Soft Reset 把它叫醒（此时只有 Rd 撑着的 vSafe5V，等也没用）。
     *   · 之后每 PD_SOFTRST_RETRY_MS 补一枪；最多 PD_SOFTRST_MAX 枪，
     *     还不来就静默成"0 PDO"（非 PD 源端），但仍在**继续听**自动广播。 */
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
            (tCapSeen != 0u) && ((now - tCapSeen) < PD_BROADCAST_MS);
        const uint32_t waitMs =
            (softRstSent != 0u)          ? PD_SOFTRST_RETRY_MS
          : srcBroadcasting              ? PD_SOFTRST_FIRST_MS   /* 还在广播：先听 */
          :                                0u;                   /* 已安静：立刻补枪 */
        if ((now - tCapWait) >= waitMs) {
          tCapWait = now;
          softRstSent++;
          const bool ok = pd.sendControl(Bit::PD_CTRL_SOFT_RESET);
          /* ★ 与 ②.6 同理：Soft Reset **不重置** MessageID。
           *   这里曾经 resetMsgId()，会让随后的 Request 回到 msgId 0，
           *   而源端期望的是 SR 的 msgId + 1 → 被判乱序、不回 GoodCRC。
           *   见 ②.6 里那段实测证据（caps 带 msgId 1 而不是 0）。 */
          Log(L_SOFTRST, ok ? 1u : 0u, softRstSent);
        }
      }
    }

    /* ---------------- 4. 中断闩锁 + Hard Reset 复活 ----------------
     *   0x3E INTERRUPTA.bit0 I_HARDRST  = **收到 Hard Reset ordered set**
     *                    bit2 I_TXSENT   = 我方报文拿到了对端 GoodCRC
     *                    bit4 I_RETRYFAIL= 自动重发 ×3 耗尽
     *                    bit6 I_TOGDONE  = toggle 因检测到设备而停止
     *   0x3F INTERRUPTB.bit0 I_GCRCSENT = 芯片回过 GoodCRC
     *   0x42 INTERRUPT  bit4 I_CRC_CHK  = 收到一包 CRC 正确的报文
     *
     * ★★★ 0x42 **必须读** ★★★
     *   INT_N 是三个 R/C 寄存器按 Mask 门控之后的"或"：
     *       (0x42 & ~Mask1) | (0x3E & ~Maska) | (0x3F & ~Maskb) ≠ 0 → INT_N 低
     *   Mask1 里 M_CRC_CHK 是放开的，也就是 I_CRC_CHK(0x42.bit4) 能拉低 INT_N。
     *   只读 0x3E/0x3F 的话，第一包有效报文一到，0x42 置位、INT_N 拉低，
     *   然后**永远清不掉** —— 引脚就永久趴在低电平上，表现是"只来一次边沿"。 */
    if (pd.isReady() && (irqSet || ((now - tInt) >= PD_INT_POLL_MS))) {
      tInt = now;

      /* 一次 7 字节突发读，替掉分 4 次读（数据手册 Figure 14 支持连续读）：
       *   st[0] 0x3C STATUS0A  st[1] 0x3D STATUS1A  st[2] 0x3E INTERRUPTA
       *   st[3] 0x3F INTERRUPTB st[4] 0x40 STATUS0  st[5] 0x41 STATUS1
       *   st[6] 0x42 INTERRUPT
       * 0x3E/0x3F/0x42 是 R/C —— 突发读里每读到一个字节就等于清掉它，
       * 正好"一次服务把三个闩锁全清干净"，而且比分 4 次读**更原子**。 */
      uint8_t st7[7] = {0, 0, 0, 0, 0, 0, 0};
      (void)pd.readBytes(Reg::STATUS0A, st7, 7u);

      const uint8_t ia = st7[2];   /* INTERRUPTA */
      const uint8_t ib = st7[3];   /* INTERRUPTB */
      /* ★ STATUS0(0x40) 顺手也拿到了 —— 正好是"CC/VBUS 现场"：
       *   bit7 VBUSOK / bit6 ACTIVITY / bit5 COMP / bit4 CRC_CHK / bits[1:0] BC_LVL。
       *   数据手册：BC_LVL+COMP 就是"CC 上是什么终结（Ra/Rd/开路）"的判据。 */
      const uint8_t s0 = st7[4];   /* STATUS0 */
      const uint8_t i0 = st7[6];   /* INTERRUPT (0x42) */
      lastS0a = st7[0];
      lastI0  = i0;

      if (ia != 0u) {
        intaAcc |= ia;
        Log(L_INTA, ia, 0);

        /* ★ 收到对端 Hard Reset → 必须写 Reset.PD_RESET 复活 FUSB302。
         * 用 INTERRUPTA.bit0（I_HARDRST）而不是 Status0a.HARDRST：
         * 前者是 R/C（读一次自动清零）→ **天然就是边沿检测**，
         * 不会出现"状态位一直挂着、每轮都重复复位 FIFO"的副作用。 */
        if (attached && ((ia & Bit::IA_HARDRST) != 0u)) {
          Log(L_HRST, 1, 0);
          tHrRecover = now;
          tRecover   = now;
          hrTries    = 0u;
          (void)pd.recover();
          if (!haveCap) { st = ST_WAIT_CAP; }
          haveCap     = false;
          rdoAck      = false;
          selPos      = 0u;
          negPos      = 0u;
          dirty       = true;
        }
      }

      /* ★★ 源端插拔检测 —— **中断驱动**（本轮新增）★★
       *
       * 数据手册原文："A Type-C device must monitor VBUS to determine if it is
       * attached or detached. The FUSB302 provides this information through the
       * **VBUSOK interrupt**."
       *   → I_VBUSOK(0x42.bit7) 就是"源端插/拔"的权威事件。这里**立刻**把新的
       *     VBUSOK 记下来并置 dirty 刷新屏，不必等 2 号块那 10ms 心跳。
       *
       * attach 那一侧由 I_TOGDONE(0x3E.bit6) 驱动：2 号块同样看 irqSet，
       *   所以同一轮里就会 pollAttach() → selectCC()，把 attached 立起来。
       *   （2 号块排在 4 号块之前，所以这里不用再重复 poll 一次。）
       *
       * ⚠️ 源端 Soft/Hard Reset 会**主动**把 VBUS 拉低约 0.9s 再重建 ——
       *    那是预期行为，绝不能当成拔出。"真拔出"由 2 号块用
       *    PD_DETACH_MS(1.5s) 做时间型去抖后判定。 */
      if ((i0 & Bit::I_VBUSOK) != 0u) {
        const uint8_t vbus = ((s0 & Bit::S0_VBUSOK) != 0u) ? 1u : 0u;
        if (vbus != lastVbusOk) {
          lastVbusOk = vbus;
          dirty      = true;
          const uint8_t flags = static_cast<uint8_t>(
              (attached ? 0x01u : 0u) |
              (vbus << 1)             |
              ((attached || (vbus != 0u)) ? 0x04u : 0u));
          Log(L_SRC, flags, s0);
        }
      }

      /* INTERRUPTB 只做诊断，一个字都不上屏 —— 但**必须读掉**（见上面 0x42 那段：
       * 0x3E/0x3F/0x42 是三个独立的 R/C 闩锁，读走才会让 INT_N 抬回高电平）。
       * ★ 诊断：IB_GCRCSENT(0x01) 置位 = 芯片刚回过 GoodCRC = 刚收到一条有效
       *   报文。把它记进日志就能精确数出"源端到底发了几包、我们回了几条"。 */
      if (ib != 0u) {
        intbAcc |= ib;
        Log(L_INTB, ib, 0);
      }
      /* 0x42 的记录策略：先剔掉 I_CRC_CHK 再入日志（它每收到一包有效报文就
       * 置位一次，日志环形区只有 64 条，记它会把真正有用的记录全冲掉）。
       * ★ "收了几包 / 我们的自动确认有没有发出去"改由 L_INTB(IB_GCRCSENT)
       *   记录 —— 一包一条，信息量相同但只在**确认成功**时记，更省槽位。
       * 另外这一块**不置 dirty** —— 0x42 里没有影响屏上内容的事件。 */
      const uint8_t i0log = static_cast<uint8_t>(i0 & static_cast<uint8_t>(~Bit::I_CRC_CHK));
      if (i0log != 0u) {
        int0Acc |= i0log;
        Log(L_INT0, i0log, 0);
      }
    }

    /* ---------------- 5. 按键（ISR 置标志，这里做去抖 + 单击/双击/长按判定） ----------------
     *   · 单击 = 高亮移到下一个 Fixed 档位（**不协商**）；
     *   · 双击 = 协商当前高亮的档位；
     *   · 长按 = Hard Reset。 */
    const e_Key ev = KeyService(key, now);
    if (ev != KEY_NONE) {
      if (ev == KEY_SHORT) {
        keyShort++;
        Log(L_BTN, 1, key.stable);
        /* 单击：高亮移到下一个 Fixed 档位（回绕；只走 Fixed，永不落到 PPS）。
         * 需求"成功高亮并协商第一个 PDO 后"单击才切 —— 所以要求 negPos!=0。
         * 只移高亮、**不发** Request；新位置与已协商位不同，DrawPdoList 会给它加
         * '*' 前缀表示"选中但未协商"。 */
        if (armed && haveCap && (negPos != 0u) && (lastMsg.pdoCount > 0u)) {
          uint16_t mv = 0u, cur = 0u;
          const uint8_t pos = nextFixedPos(lastMsg, selPos, &mv, &cur);
          if (pos != 0u) {
            tgtMv    = mv;
            tgtCur10 = cur;
            selPos   = pos;
            Log(L_TGT, static_cast<uint8_t>(mv & 0xFFu), pos);
          }
        }
        dirty = true;
      } else if (ev == KEY_DOUBLE) {
        keyDouble++;
        Log(L_BTN, 2, key.stable);
        /* 双击：协商当前高亮的档位。 */
        if (armed && haveCap && (selPos != 0u)) {
          rdoAck = false;
          const bool ok = sendRequestFor(pd, selPos, tgtCur10);
          st = ST_REQ_SENT;
          Log(L_RDO, ok ? 1u : 0u, selPos);
        }
        dirty = true;
      } else {  /* KEY_LONG */
        keyLong++;
        Log(L_BTN, 3, key.stable);
        /* 长按 = 发 Hard Reset，整套重新协商（协议层 + 电源合约一起清零）。
         * ⚠️ 源端收到 HR 后会撤 VBUS ~0.9s 再重建，所以要开恢复窗口，
         *    别让主循环把这段 VBUS 低当成"拔出"。 */
        if (attached) {
          tHrRecover  = now;
          tRecover    = now;
          hrTries     = 0u;
          haveCap     = false;
          rdoAck      = false;
          selPos      = 0u;
          negPos      = 0u;
          st          = ST_WAIT_CAP;
          const bool ok = pd.sendHardReset();
          Log(L_HRSND, ok ? 1u : 0u, static_cast<uint8_t>(pd.specRev()));
        }
        dirty = true;
      }
    }

    /* ★ 原"自动申请兜底"块已删除（2026-10-09，对齐 PD_PDO_List_Det）。
     *   3 号块在收到 caps 的**同一轮**里无条件发 Request 并置 ST_REQ_SENT，
     *   已经没有"屏上有 PDO 却没申请"的窗口；那个兜底块若保留，反而会
     *   在每轮回合里重复补枪。 */

    /* ---------------- 6. 界面刷新（指纹 + 行缓存） ----------------
     * 只有"内容真的变了"才重画，再加 PD_UI_MIN_MS 限速。
     * 被限速挡下时**不更新 uiFpLast、也不清 dirty**，所以下一轮只要过了下限
     * 就一定会补画 —— 不会丢帧，只会晚几十毫秒。 */
    UiFp fp = {};
    fp.armed    = armed ? 1u : 0u;
    fp.st       = st;
    fp.selPos   = selPos;
    fp.negPos   = negPos;
    fp.ack      = rdoAck ? 1u : 0u;
    fp.tgtMv    = tgtMv;
    fp.pdoCount = haveCap ? lastMsg.pdoCount : 0u;
    /* ★★ 源端插着吗？★★ —— 两个**独立证据**取"或"：
     *    · attached  ：CC 上有 Rp 终结（toggle 的 TOGSS 探到，由 I_TOGDONE 驱动）
     *    · lastVbusOk **且** lastCcOk：VBUS 有效 **且** CC 终结还在
     *   两个都成立不了 = Type-C 上根本没插源端 → 屏上给 "NO SRC PLUG IN"。
     *   ⚠️ 第二个证据必须带上 lastCcOk，不能只看 VBUS：
     *      拔线后 VBUS 会被体电容拖住 ~1.4s（实测 2025ms 拔、3450ms 才掉），
     *      只看 VBUS 会让屏在这 1.4s 里错显 "0 PDO"，正是要修的那个"慢"。
     *   ⚠️ 也不能用 `&&` 把两个证据串起来：SR/HR 期间 VBUS 会短暂掉，
     *      但线还插着、attached 还是 1，靠"或"才不会被误判成拔出。 */
    const bool srcPresent = attached || ((lastVbusOk != 0u) && (lastCcOk != 0u));
    fp.scr = static_cast<uint8_t>(haveCap   ? SCR_PDO
                                  : srcPresent ? SCR_NO_PDO
                                  :              SCR_NO_SRC);
    {
      const uint8_t n = (fp.pdoCount < PDO_ROWS) ? fp.pdoCount : PDO_ROWS;
      for (uint8_t i = 0u; i < n; ++i) { fp.pdo[i] = lastMsg.pdo[i]; }
    }

    bool changed = dirty || !uiFpValid ||
                   (fp.armed    != uiFpLast.armed)    ||
                   (fp.scr      != uiFpLast.scr)      ||
                   (fp.st       != uiFpLast.st)       ||
                   (fp.selPos   != uiFpLast.selPos)   ||
                   (fp.negPos   != uiFpLast.negPos)   ||
                   (fp.ack      != uiFpLast.ack)      ||
                   (fp.tgtMv    != uiFpLast.tgtMv)    ||
                   (fp.pdoCount != uiFpLast.pdoCount);
    if (!changed) {
      for (uint8_t i = 0u; i < PDO_ROWS; ++i) {
        if (fp.pdo[i] != uiFpLast.pdo[i]) { changed = true; break; }
      }
    }

    const bool minGapOk  = (now - tUi) >= PD_UI_MIN_MS;
    const bool watchdog  = (now - tUi) >= PD_UI_WATCHDOG_MS;

    if ((changed && minGapOk) || watchdog) {
      tUi       = now;
      dirty     = false;
      uiFpLast  = fp;
      uiFpValid = true;

      /* 看门狗只作废**一行**（轮转 0..ROWS，含标题条槽位）。
       * 一次全作废又变成 35ms 的整屏重画 —— 正是刚修好的那个坑。
       * 整屏轮一遍 = (ROWS+1) × 500ms = 4.5s，足够兜住 SPI/DMA 偶发毛刺。 */
      if (watchdog) {
        InvalidateRow(g_rcWatchRow);
        g_rcWatchRow = static_cast<uint8_t>((g_rcWatchRow + 1u) % (ROWS + 1u));
      }

      if (!armed) {
        /* 还没唤醒：屏上只提示按键，一个 PDO 字都不显示（不碰 PD）。 */
        HeaderBar();
        Row(0, "PD FIX DECOY", ST7735::WHITE, C_BAR);
        for (uint8_t r = PDO_ROW0; r < HINT_ROW; ++r) { Row(r, "", ST7735::BLACK); }
        Row(3, "PRESS KEY", ST7735::YELLOW);
        DrawHint();
      } else {
        DrawHeader();
        DrawPdoList(haveCap ? lastMsg : FUSB302_Message{}, selPos, negPos,
                    static_cast<e_Screen>(fp.scr));
        DrawHint();
      }
    }

    /* ---------------- 7. ★ 重新武装：把"服务期间漏掉的边沿"补回来 ----------------
     * INT_N 是**电平型闩锁**，不是脉冲。芯片一旦把引脚拉低就会一直保持低，
     * 直到所有被放开的中断位都被读走。于是服务期间如果又来了新报文 ——
     * 引脚**本来就是低的** —— 不会产生新的下降沿，EXTI 也就不会再进 ISR。
     * 而源端广播 Source_Capabilities 正是**一组 3 条连发**（组内间隔 ~2ms），
     * 几乎必然踩到这个窗口。所以这一步不是锦上添花，是必需品。
     *
     * 做法：服务完直接回读 PA0 电平。还是低就说明闩锁没清干净、或者又来了新的，
     * 把标志再立一次，下一轮立刻再服务一遍。 */
    if (pd.isReady() &&
        (LL_GPIO_IsInputPinSet(FUSB_INT_Port, FUSB_INT_Pin) == 0u)) {
      g_pdIrq = 1u;
    }

    /* ---------------- 8. 诊断快照（每轮都写，供 OpenOCD 实时体检） ----------------
     *   mdw &g_snap 8
     * 判读：g_snap[30]（心跳顶事次数）正常应**恒为 0** —— 中断总是先到。
     *       若它跟着 g_snap[24]（中断进入次数）一起涨 → 说明边沿没来，
     *       回头查 PA0 上拉 / EXTI 触发沿 / 0x42 有没有被读。
     *       g_snap[31] 是"曾经置位过的中断源集合"，正常应该非 0。 */
    g_snap[0]  = 1u;                                  /* tried */
    g_snap[1]  = pd.isReady() ? 1u : 0u;
    g_snap[2]  = devId;
    g_snap[3]  = static_cast<uint8_t>(negPos | (armed ? 0x80u : 0u));
    g_snap[4]  = static_cast<uint8_t>(attached ? 1u : 0u);
    g_snap[5]  = static_cast<uint8_t>(lastVbusOk | (lastCcOk << 1));  /* bit0 vbusOk, bit1 ccOk */
    g_snap[6]  = static_cast<uint8_t>(hrTries);
    g_snap[7]  = pd.specRev();
    g_snap[8]  = st;
    g_snap[9]  = haveCap ? lastMsg.pdoCount : 0u;
    g_snap[10] = static_cast<uint8_t>(tgtMv & 0xFFu);
    g_snap[11] = static_cast<uint8_t>(tgtMv >> 8);
    g_snap[12] = static_cast<uint8_t>(tgtCur10 & 0xFFu);
    g_snap[13] = static_cast<uint8_t>(tgtCur10 >> 8);
    g_snap[14] = selPos;
    g_snap[15] = rdoAck ? 1u : 0u;
    g_snap[16] = haveCap ? static_cast<uint8_t>(lastMsg.pdo[0] & 0xFFu) : 0u;
    g_snap[17] = static_cast<uint8_t>(key.edges & 0xFFu);
    g_snap[18] = static_cast<uint8_t>(keyShort & 0xFFu);
    g_snap[19] = static_cast<uint8_t>(((keyDouble & 0x0Fu) << 4) |
                                      (keyLong   & 0x0Fu));
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
