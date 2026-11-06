/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* What the Bluetooth test image shows on the screen (docs/BLE-TEST.md, "Reading the status text"): where start-up has got
 * to, and, when it failed, which step and which return value. No hardware access, so tests/test_ble_status.c compiles it
 * on the PC.
 *
 *   STARTING <stage>      the last stage reached (BLE_STAGE_*): a timeout names the stage it stalled in
 *   FAILED <step> <code>  the step that failed (BLE_FAIL_*) and its return value; for BLE_FAIL_TIMEOUT the code is the stage
 *   ADVERTISING           the advertisement is on air
 * and the marquee that scrolls the full failure line across the small status area. */
#pragma once
#include <stdint.h>
#include <string.h>

enum { BLE_FAIL_NONE, BLE_FAIL_TASK_CREATE, BLE_FAIL_INIT, BLE_FAIL_READY, BLE_FAIL_ADV_ENABLE, BLE_FAIL_TIMEOUT };
enum { BLE_STAGE_IDLE, BLE_STAGE_TASK, BLE_STAGE_INIT, BLE_STAGE_RF, BLE_STAGE_READY, BLE_STAGE_ADV, BLE_STAGE_ENABLE,
       BLE_STAGE_RF_DONE, BLE_STAGE_BB };
enum { BLE_OFF, BLE_STARTING, BLE_ADV, BLE_FAILED };

static volatile uint32_t ble_state;
static volatile uint32_t ble_stage;
static volatile uint32_t ble_fail_step;
static volatile int32_t ble_fail_code;
static volatile uint32_t ble_wait;                 /* at a timeout: what the controller task is blocked on (ble_task_waiting) */

static void ble_stage_set(uint32_t stage) { ble_stage = stage; }

/* The stall watch. The scheduler is cooperative, so library code that never returns (a failed assertion parks in
 * P33_SYSTEM_RESET, a wait on a register bit) holds the main loop until the watchdog resets, and that leaves no trace.
 * The 10 kHz tick (px_usb.h) therefore counts while a ble_poll runs; one that has not returned after BLE_STALL_TICKS
 * makes the next tick the crash screen, whose PC and RETS name the code (VEC 63). 0: no poll running, or held off. */
enum { BLE_STALL_TICKS = 40000 };                  /* 4 s: before the watchdog (8 s) */
static volatile uint32_t ble_busy_ticks;
static void ble_busy(int on) { ble_busy_ticks = on ? 1u : 0u; }
static int ble_stall_tick(void) { return ble_busy_ticks && ++ble_busy_ticks > BLE_STALL_TICKS; }

static void ble_fail(uint32_t step, int32_t code)
{
    ble_fail_step = step;
    ble_fail_code = code;
    ble_state = BLE_FAILED;
}

/* the results of btctrler_task_init and btctrler_task_ready (1 when the controller task answered) -> the failing step, or 0 */
static uint32_t ble_judge_start(int init_rc, int ready_rc)
{
    if (init_rc) { ble_fail(BLE_FAIL_INIT, init_rc); return BLE_FAIL_INIT; }
    if (!ready_rc) { ble_fail(BLE_FAIL_READY, ready_rc); return BLE_FAIL_READY; }
    return 0;
}

static char *ble__put(char *o, const char *s) { while (*s) *o++ = *s++; return o; }

static char *ble__num(char *o, int32_t v)
{
    char t[12]; int n = 0;
    uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
    if (v < 0) *o++ = '-';
    do { t[n++] = (char)('0' + u % 10u); u /= 10u; } while (u);
    while (n) *o++ = t[--n];
    return o;
}

/* the status text into out (at least 24 bytes) */
static void ble_status_text(char *out)
{
    char *o = out;
    if (ble_state == BLE_ADV) o = ble__put(o, "ADVERTISING");
    else if (ble_state == BLE_STARTING) { o = ble__put(o, "STARTING "); o = ble__num(o, (int32_t)ble_stage); }
    else if (ble_state == BLE_FAILED) {
        o = ble__put(o, "FAILED "); o = ble__num(o, (int32_t)ble_fail_step);
        *o++ = ' '; o = ble__num(o, ble_fail_code);
    } else o = ble__put(o, "OFF");
    *o = 0;
}

/* The full failure line, for the marquee: the short code first, then the step and the stage in words (out: 96 bytes) */
static void ble_status_full(char *out)
{
    static const char *const step[] = { "none", "task_create", "btctrler_task_init", "btctrler_task_ready", "ll_hci_adv_enable", "timeout" };
    static const char *const stage[] = { "idle", "task", "init", "radio-init", "ready-wait", "adv-setup", "enable", "radio-done", "baseband-irq" };
    char *o;
    ble_status_text(out);
    if (ble_state != BLE_FAILED) return;
    o = out + strlen(out);
    o = ble__put(o, "  step ");
    o = ble__put(o, ble_fail_step < 6 ? step[ble_fail_step] : "?");
    o = ble__put(o, " stage ");
    o = ble__put(o, ble_stage < 9 ? stage[ble_stage] : "?");
    if (ble_wait) {                                              /* look the address up in firmware/build-ble/symbols.txt */
        int i;
        o = ble__put(o, " wait ");
        for (i = 28; i >= 0; i -= 4) *o++ = "0123456789ABCDEF"[(ble_wait >> i) & 15u];
    }
    *o = 0;
}

/* A marquee: the text, then a gap, looping; tick counts the 500 ms redraws. It rests for BLE_MARQUEE_PAUSE ticks on the
 * start, then moves one character per tick (2 a second). win characters go to out, which gets a terminating 0. */
enum { BLE_MARQUEE_PAUSE = 4, BLE_MARQUEE_GAP = 3 };
static void ble_marquee(const char *text, uint32_t tick, int win, char *out)
{
    uint32_t len = (uint32_t)strlen(text), period = len + BLE_MARQUEE_GAP, phase, off;
    int i;
    if (len <= (uint32_t)win) { memcpy(out, text, len + 1u); return; }      /* it fits: no scroll */
    phase = tick % (BLE_MARQUEE_PAUSE + period);
    off = phase < BLE_MARQUEE_PAUSE ? 0u : phase - BLE_MARQUEE_PAUSE;
    for (i = 0; i < win; i++) {
        uint32_t k = (off + (uint32_t)i) % period;
        out[i] = k < len ? text[k] : ' ';
    }
    out[win] = 0;
}
