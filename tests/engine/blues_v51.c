/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "px_dsp.h"

/* Two occupied slots with the same label are still distinct choices. */
int px_ir_user(int slot, char *name, float *h)
{
    (void)h;
    if (slot != 1 && slot != 4) return 0;
    if (name) strcpy(name, "TEST");
    return 1;
}

static void check_steps(int b, int t, int k, int count)
{
    for (int start = 0; start <= 100; start++) {
        for (int dir = -1; dir <= 1; dir += 2) {
            char from[10], to[10], endpoint[10];
            int v = px_param_step(b, t, k, start, dir, 1);
            assert(px_param_text(b, t, k, start, from));
            assert(px_param_text(b, t, k, v, to));
            assert(px_param_text(b, t, k, dir < 0 ? 0 : 100, endpoint));
            assert(!strcmp(from, endpoint) ? v == start : strcmp(from, to) != 0);
        }
    }
    int v = 0;
    for (int step = 1; step < count; step++) {
        int next = px_param_step(b, t, k, v, 1, 1);
        assert(next > v);
        v = next;
    }
    assert(px_param_step(b, t, k, v, 1, 0) == v);
    assert(px_param_step(b, t, k, 0, count - 1, 1) == v);
}

static px_engine engine;
static void feed(float value)
{
    float in[64], out[64];
    for (int i = 0; i < 64; i++) in[i] = i & 1 ? -value : value;
    for (int i = 0; i < 300; i++) px_engine_process(&engine, in, out, 64);
}

int main(void)
{
    check_steps(PX_B_PITCH, PX_PIT_HARMONY, 0, 10);
    check_steps(PX_B_SFX, PX_SFX_CRUSH, 0, 15);
    check_steps(PX_B_SFX, PX_SFX_CRUSH, 1, 40);
    check_steps(PX_B_SFX, PX_SFX_SLICER, 2, 8);
    check_steps(PX_B_MOD, PX_MOD_DIMENSION, 0, 4);
    check_steps(PX_B_MOD, PX_MOD_ROTARY, 0, 2);
    check_steps(PX_B_DELAY, PX_DLY_SPACE, 3, 7);
    assert(px_param_step(PX_B_MOD, PX_MOD_DIMENSION, 1, 25, 1, 0) == 26);
    assert(px_param_step(PX_B_MOD, PX_MOD_DIMENSION, 1, 25, 1, 1) == 30);
    assert(px_param_step(PX_B_CAB, PX_CAB_IR, 0, px_ir_knob(1), 1, 0) == px_ir_knob(4));
    assert(px_param_step(PX_B_CAB, PX_CAB_IR, 0, px_ir_knob(4), -1, 1) == px_ir_knob(1));
    assert(px_param_step(PX_B_CAB, PX_CAB_IR, 0, px_ir_knob(4), 1, 0) == px_ir_knob(4));
    for (int m = 0; m < PX_NAM_N; m++) {
        char name[12];
        if (!px_nam_visible(m) || !px_nam_name(m, name)) continue;
        int v = px_nam_knob(m);
        int next = px_param_step(PX_B_AMP, PX_AMP_NAM, 0, v, 1, 0);
        if (next != v) {
            int model = px_nam_model(next);
            assert(model > m && px_nam_visible(model) && px_nam_name(model, name));
        }
    }

    for (int i = 0; i < px_preset_count(); i++) {
        px_preset_t p;
        px_preset_factory(&p, i);
        assert(px_order_valid(p.order));
        assert(px_stomp_mask(&p, 0) == (1u << PX_B_DRIVE));
        assert(px_stomp_mask(&p, 1) == (1u << PX_B_DELAY));
        assert(p.p[PX_B_GATE][PX_GATE_HYST] == 50);
        if (p.type[PX_B_MOD] == PX_MOD_DIMENSION) {
            char label[10];
            assert(px_param_text(PX_B_MOD, PX_MOD_DIMENSION, 0, p.p[PX_B_MOD][0], label));
            assert(!strcmp(label, "3") && p.p[PX_B_MOD][1] == 25);
        }
    }

    px_preset_t p;
    px_preset_factory(&p, 0);
    memset(p.on, 0, sizeof p.on);
    p.on[PX_B_GATE] = 1;
    p.p[PX_B_GATE][0] = 50;
    px_engine_init(&engine, 44100);
    px_engine_set(&engine, &p);
    assert(fabsf(engine.gate_close / engine.gate_thr - 0.501187f) < 0.0001f);
    feed(engine.gate_thr * 0.8f);
    assert(!engine.gate_open);
    feed(engine.gate_thr * 1.1f);
    assert(engine.gate_open);
    for (int i = 0; i < 6; i++) {
        feed(engine.gate_thr * 0.8f);
        assert(engine.gate_open);
        feed(engine.gate_thr * 1.1f);
    }
    feed(engine.gate_thr * 0.4f);
    assert(!engine.gate_open && engine.gate_g < 0.4f);
    feed(engine.gate_thr * 0.8f);
    assert(!engine.gate_open);
    p.p[PX_B_GATE][PX_GATE_HYST] = 0;
    px_engine_knobs(&engine, &p);
    assert(engine.gate_close == engine.gate_thr);
    puts("V51: discrete controls, sparse slots, 52 factory presets and gate hysteresis passed");
}
