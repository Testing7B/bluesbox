/* SPDX-License-Identifier: GPL-3.0-only
 * Pure C verification of the UI-rate GR conversion. Does not validate DSP wiring.
 */
#include <stdio.h>
#include <stdint.h>
#include "px_gr_meter.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr,"GR meter test failed line %d: %s\n",__LINE__,#c); return 1; } } while (0)
int main(void)
{
    px_gr_meter_t m = {0};
    for (unsigned i=0;i<10000;i++) px_gr_meter_ui_tick(&m, 0.1f);
    CHECK(m.enabled == 0u && m.conversions == 0u && m.db == 0u);
    px_gr_meter_enable(&m,1);
    px_gr_meter_ui_tick(&m,1.0f);
    CHECK(m.db == 0u);
    px_gr_meter_ui_tick(&m,0.5011872f);
    CHECK(m.db == 6u);
    px_gr_meter_ui_tick(&m,0.1f);
    CHECK(m.db == 20u);
    px_gr_meter_ui_tick(&m,0.001f);
    CHECK(m.db == 30u);
    px_gr_meter_ui_tick(&m,-1.0f);
    CHECK(m.db == 0u);
    const uint32_t before = m.conversions;
    px_gr_meter_enable(&m,0);
    CHECK(m.db == 0u);
    for (unsigned i=0;i<10000;i++) px_gr_meter_ui_tick(&m,0.01f);
    CHECK(m.conversions == before && m.db == 0u);
    puts("GR UI-rate conversion: tested OFF fast-path, 0/6/20/30 dB thresholds and invalid input");
    return 0;
}
