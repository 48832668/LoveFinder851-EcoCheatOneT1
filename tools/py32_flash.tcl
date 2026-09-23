# py32_flash.tcl -- direct flash programming for PY32F003 on stock OpenOCD 0.12
#
# Stock OpenOCD has no PY32 flash driver: loading the stm32f1x flash bank fails
# with "Cannot identify target as a stm32x" (Cortex-M0+, IDCODE 0x60001000).
# This script therefore drives the PY32 flash controller directly over SWD,
# replicating the vendor HAL sequence (py32f0xx_hal_flash.c) step by step.
#
#   Flash range : 0x08000000 - 0x0800FFFF (64 KB, 128-byte pages)
#   SWD speed   : 200 kHz (set "adapter speed 200" before init; the reset hooks
#                 below keep 200 kHz as well, overriding stm32f0x.cfg 1000/8000)
#
# Intended use (from expProjWrite.ps1):
#   openocd ... -c "adapter speed 200" -c "init" -c "halt" \
#               -c "source {py32_flash.tcl}" -c "source {generated_data.tcl}"
#
# Generated data script calls, in order:
#   py32_flash_begin
#   py32_erase_page   0x0800XX80
#   py32_program_page 0x0800XX80 {w0 w1 ... w31}
#   ...
#   py32_flash_end            -> prints "PY32_FLASH_OK pages=N"
#
# Register map (PY32F003, FLASH_R_BASE = 0x40022000):
#   KEYR +0x08, SR +0x10, CR +0x14, TS0 +0x100 ... PRETPE +0x120
#   SR : EOP bit0, WRPERR bit4, OPTVERR bit15, BSY bit16
#   CR : PG bit0, PER bit1, MER bit2, PGSTRT bit19, LOCK bit31

# mem_helper supplies mrw/mmw (usually already loaded by the target config)
if {![llength [info procs mrw]]} {
    source [find mem_helper.tcl]
}

# --- reset hooks: keep SWD at 200 kHz, skip stm32f0x clock init -------------
# stm32f0x.cfg registers these events; redefining the procs overrides them
# because the event scripts call them by name.
proc stm32f0x_default_reset_start {} {
    adapter speed 200
}
# stm32f0x_default_reset_init() pokes STM32F0 clock/flash registers whose
# meaning differs on PY32F003 -- doing that would corrupt RCC/FLASH state.
proc stm32f0x_default_reset_init {} {
}

set py32_pages 0

# Poll BSY (SR.16), then read-and-clear SR; fail on WRPERR (SR.4).
proc py32_wait_clr {what} {
    set n 0
    while {[expr {[mrw 0x40022010] & 0x10000}] != 0} {
        incr n
        if {$n > 20000} { error "py32: BSY timeout in $what" }
    }
    set sr [mrw 0x40022010]
    mww 0x40022010 0x8011          ;# FLASH_FLAG_SR_CLEAR = OPTVERR|WRPERR|EOP
    if {[expr {$sr & 0x10}]} { error "py32: WRPERR in $what (SR=$sr)" }
}

# __HAL_FLASH_TIMMING_SEQUENCE_CONFIG(): pick OTP timing table via RCC ICSCR.HSI_FS
proc py32_config_timing {} {
    set idx [expr {([mrw 0x40021004] >> 13) & 7}]
    set tbl {0x1FFF0F1C 0x1FFF0F30 0x1FFF0F44 0x1FFF0F58 0x1FFF0F6C 0x1FFF0F1C 0x1FFF0F1C 0x1FFF0F1C}
    set otp [lindex $tbl $idx]
    set w0 [mrw $otp]
    set w1 [mrw [expr {$otp + 4}]]
    set w2 [mrw [expr {$otp + 8}]]
    set w3 [mrw [expr {$otp + 12}]]
    set w4 [mrw [expr {$otp + 16}]]
    mww 0x40022100 [expr  {$w0 & 0xFF}]          ;# TS0
    mww 0x40022104 [expr {($w0 >> 16) & 0x1FF}]  ;# TS1
    mww 0x40022108 [expr  {$w1 & 0xFF}]          ;# TS2P
    mww 0x4002210C [expr {($w1 >> 16) & 0x7FF}]  ;# TPS3
    mww 0x40022110 [expr {($w0 >> 8)  & 0xFF}]   ;# TS3
    mww 0x40022114 [expr  {$w2 & 0x1FFFF}]       ;# PERTPE
    mww 0x40022118 [expr  {$w3 & 0x1FFFF}]       ;# SMERTPE
    mww 0x4002211C [expr  {$w4 & 0xFFFF}]        ;# PRGTPE
    mww 0x40022120 [expr {($w4 >> 16) & 0x3FFF}] ;# PRETPE
    echo "py32: timing table idx $idx (otp [format 0x%08x $otp])"
}

# HAL_FLASH_Unlock + drop leftover op bits + configure flash timing.
proc py32_flash_begin {} {
    py32_wait_clr "begin"
    set cr [mrw 0x40022014]
    if {[expr {$cr & 0x80000000}]} {
        mww 0x40022008 0x45670123               ;# FLASH_KEY1
        mww 0x40022008 0xCDEF89AB               ;# FLASH_KEY2
        set cr [mrw 0x40022014]
        if {[expr {$cr & 0x80000000}]} {
            error "py32: unlock failed (CR=$cr)"
        }
    }
    mmw 0x40022014 0 0x80007                    ;# CR &= ~(PG|PER|MER|PGSTRT)
    py32_config_timing
    echo "py32: flash controller ready (unlocked)"
}

# HAL FLASH_PageErase: CR |= PER, then write 0xFF to the page address.
proc py32_erase_page {addr} {
    if {[expr {$addr % 128}]} { error "py32: erase address not page aligned: $addr" }
    py32_wait_clr "erase pre"
    mmw 0x40022014 0x2 0
    mww $addr 0xFF
    sleep 1
    py32_wait_clr "erase"
    mmw 0x40022014 0 0x2                        ;# CR &= ~PER
}

# HAL FLASH_Program_Page: CR |= PG, write 31 words, CR |= PGSTRT, write word 32.
# Then wait, clear PG|PGSTRT, and read back the whole page for verification.
proc py32_program_page {addr words} {
    global py32_pages
    if {[expr {$addr % 128}]} { error "py32: program address not page aligned: $addr" }
    if {[llength $words] != 32} {
        error "py32: page needs 32 words, got [llength $words] ($addr)"
    }
    py32_wait_clr "program pre"
    mmw 0x40022014 0x1 0                        ;# CR |= PG
    for {set i 0} {$i < 31} {incr i} {
        mww [expr {$addr + 4 * $i}] [lindex $words $i]
    }
    mmw 0x40022014 0x80000 0                    ;# CR |= PGSTRT (after word 31)
    mww [expr {$addr + 124}] [lindex $words 31]
    sleep 1
    py32_wait_clr "program"
    mmw 0x40022014 0 0x80001                    ;# CR &= ~(PG|PGSTRT)
    for {set i 0} {$i < 32} {incr i} {
        set got  [mrw [expr {$addr + 4 * $i}]]
        set want [lindex $words $i]
        if {[expr {$got != $want}]} {
            error "py32: verify mismatch at [format 0x%08x [expr {$addr + 4 * $i}]]: got $got want $want"
        }
    }
    incr py32_pages
    echo "py32: page [format 0x%08x $addr] ok ($py32_pages)"
}

# HAL_FLASH_Lock: CR |= LOCK, then report success marker for the caller.
proc py32_flash_end {} {
    global py32_pages
    mmw 0x40022014 0x80000000 0                 ;# CR |= LOCK
    set cr [mrw 0x40022014]
    if {![expr {$cr & 0x80000000}]} { error "py32: lock failed (CR=$cr)" }
    echo "PY32_FLASH_OK pages=$py32_pages"
}
