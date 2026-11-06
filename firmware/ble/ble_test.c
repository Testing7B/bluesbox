/* SPDX-License-Identifier: GPL-3.0-only OR MPL-2.0 */
/* Advertise-only Bluetooth test (build target "ble", docs/BLE-TEST.md): the controller from the JieLi SDK library
 * (third_party/jieli-ble) is started, and one scannable, non-connectable advertisement is sent on the three
 * advertising channels:
 *   advertising data   flags, the BLE-MIDI service UUID 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 (128 bit)
 *   scan response      the complete local name "OpenPocket-BLE-TEST"
 * No connection is ever accepted (the type is ADV_SCAN_IND), no pairing, nothing is sent over Bluetooth but this. */
#include <stdint.h>
#include <stdbool.h>
#include "fm1_irq.h"
#include "ble_os.h"

/* the controller library's entry points (include_lib/btctrler/btctrler_task.h, ble/hci_ll.h; hci_transport.h) */
extern int btctrler_task_init(const void *transport, const void *config);
extern int btctrler_task_ready(void);        /* blocks until the controller task answers, 1 when it did */
extern const void *hci_transport_h4_controller_instance(void);
extern void ll_hci_adv_set_params(uint16_t adv_int_min, uint16_t adv_int_max, uint8_t adv_type, uint8_t direct_address_type,
                                  uint8_t *direct_address, uint8_t channel_map, uint8_t filter_policy);
extern void ll_hci_adv_set_data(uint8_t len, uint8_t *data);
extern void ll_hci_adv_scan_response_set_data(uint8_t len, uint8_t *data);
extern int ll_hci_adv_enable(bool enable);
extern int task_create(void (*fn)(void *), void *arg, const char *name);   /* ble_os.c */

enum { ADV_SCAN_IND = 2 };
enum { S_IDLE, S_STARTING, S_ADV, S_FAILED };

static volatile uint32_t state;
static uint32_t t_start;
static uint8_t cfg[64];                                         /* the controller reads its host-side settings here: none */

static uint8_t adv_data[] = {
    2, 0x01, 0x06,                                              /* flags: LE general discoverable, no BR/EDR */
    17, 0x07,                                                   /* complete list of 128-bit service UUIDs, little endian */
    0x00, 0xC7, 0xC4, 0x4E, 0xE3, 0x6C, 0x51, 0xA7, 0x33, 0x4B, 0xE8, 0xED, 0x5A, 0x0E, 0xB8, 0x03,
};
static uint8_t scan_rsp[] = { 20, 0x09, 'O','p','e','n','P','o','c','k','e','t','-','B','L','E','-','T','E','S','T' };

/* btctrler_task_ready waits (on a semaphore) until the controller's own task has started, so this runs as a task of
 * its own: the main loop must never block there. */
static void test_task(void *arg)
{
    uint8_t chmap = 7;                                          /* channels 37, 38, 39 */
    (void)arg;
    if (btctrler_task_init(hci_transport_h4_controller_instance(), cfg) || !btctrler_task_ready()) {
        state = S_FAILED;
        return;
    }
    ll_hci_adv_set_params(0x00A0, 0x00A0, ADV_SCAN_IND, 0, 0, chmap, 0);   /* 100 ms */
    ll_hci_adv_set_data(sizeof(adv_data), adv_data);
    ll_hci_adv_scan_response_set_data(sizeof(scan_rsp), scan_rsp);
    state = ll_hci_adv_enable(true) ? S_FAILED : S_ADV;
}

void ble_test_start(void)
{
    state = S_STARTING;
    t_start = fm1_uptime_ms();
    if (task_create(test_task, 0, "bletest"))
        state = S_FAILED;
}

void ble_test_poll(void)
{
    ble_poll();
    if (state == S_STARTING && fm1_uptime_ms() - t_start > 5000u)
        state = S_FAILED;                                       /* (the task, if it is still waiting, is left alone) */
}

const char *ble_test_state(void)
{
    return state == S_ADV ? "ADVERTISING" : state == S_STARTING ? "STARTING" : state == S_FAILED ? "FAILED" : "OFF";
}
