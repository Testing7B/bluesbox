/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

enum { CFG_INPUT, CFG_HPF, CFG_GUITAR, CFG_USB, CFG_STOMP, CFG_MUTE, CFG_COUNT };
enum { MUTE_GUITAR, MUTE_OFF, MUTE_ALL };
static const uint8_t CFG_MAX[CFG_COUNT] = {8, 5, 7, 7, 1, 2};
static const uint8_t CFG_SHIFT[CFG_COUNT] = {0, 4, 7, 10, 13, 14};
static const uint8_t CFG_WIDTH[CFG_COUNT] = {15, 7, 7, 7, 1, 3};
static const float CFG_DB[] = {1.0f, 0.707946f, 0.501187f, 0.354813f, 0.251189f,
                               0.177828f, 0.125893f, 0.089125f};
static const float CFG_HP_COEF[] = {0.0f, 0.005683f, 0.008512f, 0.011333f, 0.014146f, 0.016951f};
static volatile uint16_t cfg_audio;

static unsigned cfg_read(uint16_t packed, unsigned row)
{
    unsigned value = (packed >> CFG_SHIFT[row]) & CFG_WIDTH[row];
    return value <= CFG_MAX[row] ? value : 0;
}

static void cfg_set(unsigned row, unsigned value)
{
    unsigned mask = CFG_WIDTH[row] << CFG_SHIFT[row];
    cfg_audio = (uint16_t)((cfg_audio & ~mask) | (value << CFG_SHIFT[row]));
}

enum { FOOT_NONE, FOOT_LEFT, FOOT_RIGHT, FOOT_TUNER };
typedef struct { uint32_t both_since; uint8_t last, chord; } foot_gesture_t;

/* A tap commits on release. Once both are down, consume both releases even
 * if the player lets go before the tuner threshold. */
static unsigned foot_gesture(foot_gesture_t *g, unsigned down, uint32_t now)
{
    unsigned event = FOOT_NONE;
    if (down == 3u && g->last != 3u && g->chord != 2u) {
        g->chord = 1u;
        g->both_since = now;
    }
    if (g->chord) {
        if (g->chord == 1u && down == 3u && now - g->both_since >= 700u) {
            g->chord = 2u;
            event = FOOT_TUNER;
        }
        if (!down) g->chord = 0;
    } else if (!down && g->last) event = g->last;
    g->last = down;
    return event;
}
