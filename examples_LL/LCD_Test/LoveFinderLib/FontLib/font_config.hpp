/**
 * @file font_config.hpp
 * @brief 本工程的字体编译清单 (工程专属 — 这是工程里唯一的字库文件)
 * @author LoveFinder
 *
 * ============================================================================
 *  这个文件只声明「要编译哪些字」, 不含任何字形像素数据。
 * ============================================================================
 *  像素数据在共享库里 (唯一副本):
 *      LoveFinderLibForPY32_LL/FontLib/font_manifest.json   唯一真相源
 *      LoveFinderLibForPY32_LL/FontLib/font_data.cpp        全部字形 (生成物)
 *      LoveFinderLibForPY32_LL/FontLib/font.h               固定接口 (生成物)
 *
 *  用 PickSoul 编辑库里的字形, 所有引用该库的例程重新编译后一起更新。
 *  本文件由 PickSoul 生成/维护, 也可手工编辑 (改完重新编译即可, 无需重新生成)。
 *
 *  编译原理: 库的 font_data.cpp 持有该字体【全部】字形的 static 数组,
 *  但查表 switch 只展开下面 *_CHARS(X) 里列出的字符; 未被引用的数组
 *  由编译器直接丢弃, 不占 Flash (armclang -O2 即可, 已实测)。
 */

#ifndef FONT_CONFIG_HPP
#define FONT_CONFIG_HPP

/*============================================================================
 * 1. 启用哪些字体 (库的完整名册见 LoveFinderLibForPY32_LL/FontLib/font.h)
 *============================================================================*/
#define USE_FONT_7X10     1
#define USE_FONT_11X18    0
#define USE_FONT_16X26    0
#define USE_FONT_ZH_16X16 0

/*============================================================================
 * 2. 各字体编译哪些字符
 *
 *    X(<十进制码点>) 形式, 必须按码点升序排列。
 *    未列出的字符不会被编译 -> 渲染时自动跳过 (不显示也不占位)。
 *============================================================================*/

/* Font_7x10 —— 本工程编译 25 个字符 / 库中 95 个 */
#define FONT_7X10_CHARS_STR  " !1CDEHLOTW_acdehlnoprstx"
#define FONT_7X10_CHARS(X) \
    X(32) X(33) X(49) X(67) X(68) X(69) X(72) X(76) X(79) X(84) X(87)                       \
    X(95) X(97) X(99) X(100) X(101) X(104) X(108) X(110) X(111) X(112) X(114)               \
    X(115) X(116) X(120)                                                                   

/* Font_11x18 —— 未启用 */
/* #define FONT_11X18_CHARS(X)  X(...) */

/* Font_16x26 —— 未启用 */
/* #define FONT_16X26_CHARS(X)  X(...) */

/* Font_ZH_16x16 —— 未启用 */
/* #define FONT_ZH_16X16_CHARS(X)  X(...) */

#endif /* FONT_CONFIG_HPP */
