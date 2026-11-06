/* The Bluetooth test image's status text and failure codes (firmware/ble/ble_status.h): which step failed, with which
 * return value, and the stage a timeout stalled in. The header has no hardware access, so it is compiled as is. */
#include <stdio.h>
#include <string.h>
#include "../firmware/ble/ble_status.h"

static int bad;
static void is(const char *what, const char *want)
{
    char b[48];
    ble_status_text(b);
    if (strcmp(b, want)) { printf("FAIL %s: \"%s\", wanted \"%s\"\n", what, b, want); bad++; }
    if (strlen(b) >= 24) { printf("FAIL %s: \"%s\" does not fit 24 bytes\n", what, b); bad++; }
}
static void reset(void) { ble_state = BLE_OFF; ble_stage = 0; ble_fail_step = 0; ble_fail_code = 0; }

int main(void)
{
    reset(); is("off", "OFF");
    ble_state = BLE_STARTING; ble_stage_set(BLE_STAGE_TASK); is("starting, task", "STARTING 1");
    ble_stage_set(BLE_STAGE_RF); is("starting, radio init", "STARTING 3");
    ble_stage_set(BLE_STAGE_READY); is("starting, waiting for the controller", "STARTING 4");

    reset(); ble_state = BLE_STARTING;
    if (ble_judge_start(0, 1) != 0 || ble_state != BLE_STARTING) { puts("FAIL: a good start was judged failed"); bad++; }
    if (ble_judge_start(-7, 0) != BLE_FAIL_INIT) { puts("FAIL: init error not judged as step 2"); bad++; }
    is("btctrler_task_init returned -7", "FAILED 2 -7");

    reset(); ble_state = BLE_STARTING;
    if (ble_judge_start(0, 0) != BLE_FAIL_READY) { puts("FAIL: not-ready not judged as step 3"); bad++; }
    is("btctrler_task_ready returned 0", "FAILED 3 0");

    reset(); ble_state = BLE_STARTING; ble_fail(BLE_FAIL_TASK_CREATE, -1); is("task_create failed", "FAILED 1 -1");
    reset(); ble_state = BLE_STARTING; ble_fail(BLE_FAIL_ADV_ENABLE, 12); is("ll_hci_adv_enable returned 12", "FAILED 4 12");

    reset(); ble_state = BLE_STARTING; ble_stage_set(BLE_STAGE_RF);
    ble_fail(BLE_FAIL_TIMEOUT, (int32_t)ble_stage); is("timeout in the radio init", "FAILED 5 3");

    reset(); ble_fail(BLE_FAIL_INIT, -2147483647 - 1); is("most negative code", "FAILED 2 -2147483648");
    reset(); ble_state = BLE_ADV; is("advertising", "ADVERTISING");
    /* the full line */
    reset(); ble_state = BLE_STARTING; ble_stage_set(BLE_STAGE_READY); ble_judge_start(-7, 0);
    { char f[96]; ble_status_full(f);
      if (strcmp(f, "FAILED 2 -7  step btctrler_task_init stage ready-wait")) { printf("FAIL full line: \"%s\"\n", f); bad++; }
      if (strlen(f) >= 96) { puts("FAIL: full line too long"); bad++; } }
    reset(); ble_state = BLE_STARTING; ble_stage_set(BLE_STAGE_RF); ble_fail(BLE_FAIL_TIMEOUT, (int32_t)ble_stage);
    { char f[96]; ble_status_full(f);
      if (strcmp(f, "FAILED 5 3  step timeout stage radio-init")) { printf("FAIL timeout line: \"%s\"\n", f); bad++; } }
    /* the marquee: "ABCDEFGH" in a window of 4, gap 3, rest 4 ticks */
    { static const struct { uint32_t tick; const char *want; } m[] = {
        {0, "ABCD"}, {3, "ABCD"},            /* the rest on the start */
        {4, "ABCD"}, {5, "BCDE"}, {8, "EFGH"},
        {9, "FGH "}, {11, "H   "}, {12, "   A"},   /* through the gap and round */
        {14, " ABC"},                        /* the last step of the cycle: one gap column left */
        {15, "ABCD"}, {19, "ABCD"},          /* a whole cycle (rest 4 + period 11 = 15 ticks) is back at the start */
      };
      int i; char o[8];
      for (i = 0; i < (int)(sizeof m / sizeof m[0]); i++) {
          ble_marquee("ABCDEFGH", m[i].tick, 4, o);
          if (strcmp(o, m[i].want)) { printf("FAIL marquee tick %u: \"%s\", wanted \"%s\"\n", m[i].tick, o, m[i].want); bad++; }
      }
      ble_marquee("ABCD", 7, 4, o);          /* fits: never moves */
      if (strcmp(o, "ABCD")) { puts("FAIL: a text that fits was scrolled"); bad++; } }
    puts(bad ? "ble status: FAILED" : "ble status: ok");
    return bad != 0;
}
