/* SPDX-License-Identifier: GPL-3.0-only
 * UI-rate gain reduction display helper for DSPL compressors.
 * No log10f, powf, expf, or audio callback work. Must be called by UI tick only.
 * Input: actual pre-makeup *linear gain ratio* from compressor, 1.0 means 0 dB GR.
 * This is a reusable component; it is NOT wired to the current BlackBox firmware.
 */
#ifndef PX_GR_METER_H
#define PX_GR_METER_H
#include <stdint.h>
typedef struct {
    uint8_t enabled;      /* 0: no calculations and UI blank */
    uint8_t db;           /* integer GR 0..30 dB; 30 is >=30 dB */
    uint32_t conversions; /* validation counter; can be omitted in firmware-specific implementation */
} px_gr_meter_t;

static inline void px_gr_meter_enable(px_gr_meter_t *m, int on)
{
    m->enabled = on ? 1u : 0u;
    if (!m->enabled) m->db = 0u;
}
static inline void px_gr_meter_ui_tick(px_gr_meter_t *m, float attenuation_linear)
{
    if (!m->enabled) return;   /* OFF: no conversion work */
    ++m->conversions;
    /* Reject non-finite/nonphysical states without corrupting the display. */
    if (!(attenuation_linear >= 0.0f)) { m->db = 0u; return; }
    if (attenuation_linear >= 1.0f) { m->db = 0u; return; }
    /* Rounded integer dB: thresholds halfway between whole dB values.
     * 10^(-0.5/20) = 0.944060876; 10^(-1/20) = 0.891250938.
     * 30 iterations at <=10 Hz UI tick, never per audio sample/block.
     */
    float threshold = 0.944060876f;
    const float ratio_per_db = 0.891250938f;
    unsigned db = 0;
    while (db < 30u && attenuation_linear < threshold) {
        ++db;
        threshold *= ratio_per_db;
    }
    m->db = (uint8_t)db;
}
#endif
