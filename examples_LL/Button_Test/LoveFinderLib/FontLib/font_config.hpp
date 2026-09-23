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
 *
 *  Button_Test 屏幕文案用到的字符 (38 个 / 库中 95 个):
 *
 *      "Button_Test"                  -> B u t t o n _ T e s
 *      "CLICK :" "DOUBLE:" "LONG  :"  -> C L I K D O U B E N G
 *      "LP:" "ms" "DC:"               -> P m D C
 *      "CHG:"                         -> H G
 *      "2x:LP+"                       -> 2 x +
 *      "%" ":" "=" 以及数字 0-9
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

/* Font_7x10 —— 本工程编译 38 个字符 / 库中 95 个 */
#define FONT_7X10_CHARS_STR  " %+0123456789:=BCDEGHIKLNOPTU_emnostux"
#define FONT_7X10_CHARS(X)                                                       \
    X(32) X(37) X(43)                                                            \
    X(48) X(49) X(50) X(51) X(52) X(53) X(54) X(55) X(56) X(57)                  \
    X(58) X(61)                                                                  \
    X(66) X(67) X(68) X(69) X(71) X(72) X(73) X(75) X(76) X(78) X(79) X(80)      \
    X(84) X(85) X(95) X(101) X(109) X(110) X(111) X(115) X(116) X(117) X(120)

/* Font_11x18 —— 未启用 */
/* #define FONT_11X18_CHARS(X)  X(...) */

/* Font_16x26 —— 未启用 */
/* #define FONT_16X26_CHARS(X)  X(...) */

/* Font_ZH_16x16 —— 未启用 */
/* #define FONT_ZH_16X16_CHARS(X)  X(...) */

#endif /* FONT_CONFIG_HPP */
