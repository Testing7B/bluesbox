/* SPDX-License-Identifier: GPL-3.0-or-later
 * Blues Lab factory bank. Included by px_dsp.c only for PX_BLUES_LAB. */

enum { BL_OFF = 255, BL_TS808 = PX_DRV_V33, BL_BREAKER = PX_DRV_V33 + 9,
       BL_FACE = PX_DRV_V33 + 16, BL_OCTAFUZZ = PX_DRV_V33 + 19 };

typedef struct {
    uint8_t model, cab, bass, mid, treble, body, bright, mic;
} blues_amp;

enum { BL_BLACKFACE, BL_FULL, BL_HEADROOM, BL_DRY, BL_TWO_ROCK };

static const blues_amp BLUES_AMPS[] = {
    /* model       cabinet        bass mid treble   body bright mic */
    { M_TWIN,       PX_CAB_112JN,    47, 49, 53,     51, 48, 44 },
    { M_SUPER59,    PX_CAB_112CREAM,  47, 53, 52,     52, 49, 49 },
    { M_SHIRLCL,    PX_CAB_212BLUE,   48, 53, 49,     50, 46, 43 },
    { M_TWIN,       PX_CAB_112JN,    41, 60, 60,     42, 57, 59 },
#ifdef PX_BLUES_V45
    { PX_NAM_N,     PX_CAB_212BLUE,   48, 53, 49,     50, 46, 43 },
#endif
};

typedef struct {
    uint8_t type, a, b, c, d, e;
} blues_block;

#define BL_NONE { BL_OFF, 0, 0, 0, 0, 0 }
#define BL_DRIVE(type, gain, tone, level) { type, gain, tone, level, 0, 0 }
#define BL_OPTO(type, reduction, gain, mix) { type, reduction, gain, mix, 0, 0 }

enum { BL_MOD_NONE, BL_ROT_SLOW, BL_ROT_FAST, BL_VIBE, BL_TREM, BL_DIM };
static const blues_block BLUES_MOD[] = {
    BL_NONE,
    { PX_MOD_ROTARY,     28, 54, 53, 0, 0 },
    { PX_MOD_ROTARY,     70, 51, 54, 0, 0 },
    { PX_MOD_UNIVIBE,    24, 40, 38, 0, 0 },
    { PX_MOD_OPTOTREM,  27, 23,  0, 0, 0 },
    { PX_MOD_DIMENSION, 20, 22,  0, 0, 0 },
};

enum { BL_DLY_NONE, BL_SLAP, BL_BBD, BL_TAPE, BL_LONG };
static const blues_block BLUES_DELAY[] = {
    BL_NONE,
    { PX_DLY_TAPE,      7, 14, 12, 44, 12 },
    { PX_DLY_ANALOG,   18, 22, 14, 45,  0 },
    { PX_DLY_TAPE,     27, 27, 16, 40, 18 },
    { PX_DLY_ANALOG,   35, 28, 18, 43,  0 },
};

enum { BL_REV_NONE, BL_ROOM, BL_SPRING, BL_WET_SPRING, BL_PLATE, BL_HALL };
static const blues_block BLUES_REVERB[] = {
    BL_NONE,
    { PX_REV_ROOM,   33, 48, 12,  0, 0 },
    { PX_REV_SPRING, 34, 51, 16, 34, 0 },
    { PX_REV_SPRING, 44, 47, 25, 43, 0 },
    { PX_REV_PLATE,  37, 46, 16,  0, 0 },
    { PX_REV_HALL,   42, 45, 20,  0, 0 },
};

typedef struct {
    char name[14];
    uint8_t voice, gain, level, mod, delay, reverb;
    blues_block drive, comp;
} blues_preset;

/* Song titles are listening references. Shared amp/cab voices are proxies for the original rigs.
 * Preset levels were set on the bundled direct guitar loops with room for transient peaks. */
static const blues_preset BLUES_PRESETS[] = {
    /* name          voice, gain, level, modulation, delay, reverb     drive, compressor */
    /* SRV: 1–20 */
    { "LENNY",       BL_BLACKFACE, 42, 56, BL_MOD_NONE, BL_DLY_NONE, BL_WET_SPRING, BL_NONE, BL_NONE },
    { "PRIDE & JOY", BL_FULL,      53, 45, BL_MOD_NONE, BL_DLY_NONE, BL_SPRING,     BL_DRIVE(PX_DRV_TS9, 25, 47, 53), BL_NONE },
    { "TEXAS FLOOD", BL_FULL,      58, 45, BL_MOD_NONE, BL_DLY_NONE, BL_SPRING,     BL_DRIVE(PX_DRV_BLUES_808, 39, 46, 55), BL_OPTO(PX_CMP_OPTO_T4, 22, 29, 47) },
    { "COLD SHOT",   BL_BLACKFACE, 53, 54, BL_ROT_SLOW, BL_DLY_NONE, BL_ROOM,       BL_DRIVE(PX_DRV_TS9, 18, 46, 56), BL_NONE },
    { "RIVIERA",     BL_HEADROOM,  43, 60, BL_MOD_NONE, BL_TAPE,     BL_HALL,       BL_NONE, BL_OPTO(PX_CMP_OPTO_T4, 16, 28, 38) },
    { "TIN PAN ALY", BL_BLACKFACE, 45, 52, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_NONE },
    { "RUDE MOOD",   BL_FULL,      55, 42, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_DRIVE(PX_DRV_TS9, 21, 54, 55), BL_NONE },
    { "LOVE STRUCK", BL_FULL,      56, 44, BL_MOD_NONE, BL_SLAP,     BL_SPRING,     BL_DRIVE(BL_TS808, 28, 53, 56), BL_NONE },
    { "SCUTTLE",     BL_FULL,      63, 32, BL_MOD_NONE, BL_DLY_NONE, BL_SPRING,     BL_DRIVE(PX_DRV_TS9, 31, 58, 57), BL_NONE },
    { "STANG SWANG", BL_HEADROOM,  42, 58, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_OPTO(PX_CMP_OPTO_T4, 13, 28, 34) },
    { "LITTLE WING", BL_BLACKFACE, 48, 53, BL_MOD_NONE, BL_TAPE,     BL_PLATE,      BL_NONE, BL_NONE },
    { "VOODOO CHL",  BL_FULL,      58, 47, BL_VIBE,     BL_DLY_NONE, BL_ROOM,       BL_DRIVE(BL_FACE, 29, 47, 47), BL_NONE },
    { "CROSSFIRE",   BL_HEADROOM,  53, 58, BL_MOD_NONE, BL_BBD,      BL_PLATE,      BL_DRIVE(PX_DRV_TS9, 28, 53, 59), BL_OPTO(PX_CMP_OPTO_T4, 18, 29, 43) },
    { "TIGHTROPE",   BL_HEADROOM,  59, 47, BL_MOD_NONE, BL_SLAP,     BL_ROOM,       BL_DRIVE(BL_TS808, 31, 51, 56), BL_NONE },
    { "WALL DENIAL", BL_FULL,      58, 39, BL_MOD_NONE, BL_DLY_NONE, BL_PLATE,      BL_DRIVE(PX_DRV_TS9, 33, 47, 52), BL_NONE },
    { "THE SKY CRY", BL_BLACKFACE, 52, 74, BL_MOD_NONE, BL_DLY_NONE, BL_WET_SPRING, BL_DRIVE(BL_TS808, 24, 43, 56), BL_OPTO(PX_CMP_OPTO_T4, 17, 28, 43) },
    { "EMPTY ARMS",  BL_FULL,      53, 41, BL_MOD_NONE, BL_SLAP,     BL_ROOM,       BL_DRIVE(PX_DRV_BLUES_808, 20, 54, 54), BL_NONE },
    { "CANT STAND",  BL_BLACKFACE, 56, 50, BL_ROT_FAST, BL_DLY_NONE, BL_ROOM,       BL_DRIVE(PX_DRV_TS9, 19, 53, 56), BL_NONE },
    { "TESTIFY",     BL_FULL,      60, 38, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_DRIVE(BL_TS808, 29, 55, 54), BL_NONE },
    { "THIRD STONE", BL_FULL,      52, 44, BL_VIBE,     BL_DLY_NONE, BL_PLATE,      BL_DRIVE(BL_OCTAFUZZ, 23, 45, 48), BL_NONE },
    /* John Mayer: 21–30 */
#ifdef PX_BLUES_V45
    { "GRAVITY",     BL_TWO_ROCK,  44, 51, BL_TREM,     BL_DLY_NONE, BL_SPRING,     BL_NONE, BL_OPTO(PX_CMP_OPTO_T4, 14, 28, 35) },
    { "SLOW DANCE",  BL_TWO_ROCK,  49, 58, BL_MOD_NONE, BL_BBD,      BL_PLATE,      BL_DRIVE(PX_DRV_TS808_JP, 50, 50, 40), BL_OPTO(PX_CMP_OPTO_T4, 14, 28, 35) },
#else
    { "GRAVITY",     BL_HEADROOM,  44, 51, BL_TREM,     BL_DLY_NONE, BL_SPRING,     BL_NONE, BL_OPTO(PX_CMP_OPTO_T4, 14, 28, 35) },
    { "SLOW DANCE",  BL_HEADROOM,  49, 58, BL_MOD_NONE, BL_BBD,      BL_PLATE,      BL_DRIVE(BL_BREAKER, 25, 47, 51), BL_NONE },
#endif
    { "VULTURES",    BL_HEADROOM,  56, 51, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_DRIVE(PX_DRV_KLON, 21, 51, 50), BL_NONE },
    { "NO DOCTOR",   BL_FULL,      58, 36, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_DRIVE(BL_BREAKER, 31, 54, 52), BL_NONE },
    { "DONT TRUST",  BL_BLACKFACE, 46, 56, BL_ROT_SLOW, BL_BBD,      BL_PLATE,      BL_NONE, BL_NONE },
    { "BELIEF",      BL_HEADROOM,  53, 53, BL_MOD_NONE, BL_TAPE,     BL_PLATE,      BL_DRIVE(PX_DRV_KLON, 25, 53, 52), BL_NONE },
    { "BOLD LOVE",   BL_FULL,      52, 59, BL_VIBE,     BL_DLY_NONE, BL_SPRING,     BL_DRIVE(BL_FACE, 23, 51, 47), BL_NONE },
    { "I GUESS",     BL_HEADROOM,  54, 61, BL_MOD_NONE, BL_LONG,     BL_HALL,       BL_DRIVE(BL_BREAKER, 27, 49, 56), BL_OPTO(PX_CMP_OPTO_T4, 18, 30, 42) },
    { "SOMETHING",   BL_BLACKFACE, 47, 44, BL_DIM,      BL_BBD,      BL_PLATE,      BL_DRIVE(PX_DRV_BLUES_BOOST, 35, 0, 0), BL_NONE },
    { "GOOD LOVE",   BL_BLACKFACE, 55, 55, BL_MOD_NONE, BL_SLAP,     BL_ROOM,       BL_DRIVE(PX_DRV_TS9, 17, 54, 55), BL_NONE },
    /* Albert King: 31–40 */
    { "BAD SIGN",    BL_DRY,       49, 47, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_NONE },
    { "CROSSCUT",    BL_DRY,       54, 48, BL_MOD_NONE, BL_DLY_NONE, BL_REV_NONE,   BL_NONE, BL_NONE },
    { "THE HUNTER",  BL_FULL,      54, 38, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_NONE },
    { "MANAGER",     BL_DRY,       58, 46, BL_MOD_NONE, BL_DLY_NONE, BL_REV_NONE,   BL_NONE, BL_NONE },
    { "PLAY BLUES",  BL_BLACKFACE, 45, 53, BL_MOD_NONE, BL_DLY_NONE, BL_PLATE,      BL_NONE, BL_NONE },
    { "BLUES POWER", BL_DRY,       60, 40, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_NONE },
    { "BLUES SUNUP", BL_DRY,       51, 50, BL_MOD_NONE, BL_DLY_NONE, BL_REV_NONE,   BL_NONE, BL_NONE },
    { "MATCH BOX",   BL_DRY,       56, 45, BL_MOD_NONE, BL_SLAP,     BL_ROOM,       BL_NONE, BL_NONE },
    { "STORMY MON",  BL_BLACKFACE, 51, 48, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_NONE },
    { "OH PRETTY",   BL_FULL,      57, 35, BL_MOD_NONE, BL_DLY_NONE, BL_ROOM,       BL_NONE, BL_NONE },
};

static const blues_preset BLUES_OPTO_AB = {
    "", BL_BLACKFACE, 51, 50, BL_MOD_NONE, BL_DLY_NONE, BL_REV_NONE,
    BL_NONE, BL_OPTO(PX_CMP_OPTO, 36, 32, 75)
};

typedef struct {
    char name[14];
    uint8_t drive, level;
} blues_drive_test;

static const blues_drive_test BLUES_DRIVE_AB[] = {
    { "AB TS9 V40",   PX_DRV_TS9, 49 },
    { "AB TS808 LAB", PX_DRV_BLUES_808, 46 },
    { "AB TS808 OLD", BL_TS808, 56 },
    { "AB KLON",      PX_DRV_KLON, 61 },
    { "AB BREAKER",   BL_BREAKER, 58 },
    { "AB CLEAN BST", PX_DRV_BLUES_BOOST, 42 },
    { "AB NO DRIVE",  BL_OFF, 44 },
};

enum { BLUES_COUNT = sizeof(BLUES_PRESETS) / sizeof(BLUES_PRESETS[0]),
       BLUES_V40_COUNT = 3, BLUES_OPTO_COUNT = 2,
       BLUES_DRIVE_COUNT = sizeof(BLUES_DRIVE_AB) / sizeof(BLUES_DRIVE_AB[0]) };
_Static_assert(BLUES_COUNT == 40, "the song bank fills slots 1 through 40");
_Static_assert(BLUES_COUNT + BLUES_V40_COUNT + BLUES_OPTO_COUNT + BLUES_DRIVE_COUNT == NPRESETS,
               "the Blues Lab keeps the 52-slot preset ABI");

static void blues_apply_block(px_preset_t *p, int block, const blues_block *settings)
{
    if (settings->type != BL_OFF)
        blk(p, block, 1, settings->type, settings->a, settings->b, settings->c, settings->d, settings->e);
}

static void blues_apply_preset(px_preset_t *p, const blues_preset *preset)
{
    const blues_amp *amp = &BLUES_AMPS[preset->voice];
    static const uint8_t order[PX_PRESET_ROWS] = {
        PX_B_GATE, PX_B_DRIVE, PX_B_AMP, PX_B_CAB, PX_B_COMP, PX_B_MOD, PX_B_DELAY, PX_B_REVERB,
        PX_B_PITCH, PX_B_SFX, PX_B_EQ, PX_NBLK
    };

    setname(p, preset->name);
    memcpy(p->order, order, sizeof order);
    blk(p, PX_B_GATE, 1, 0, 13, 53, preset->level, 0, 0);
    blues_apply_block(p, PX_B_DRIVE, &preset->drive);
#ifdef PX_BLUES_V45
    if (preset->voice == BL_TWO_ROCK)
        blk(p, PX_B_AMP, 1, PX_AMP_TS1, preset->gain, amp->bass, amp->mid, amp->treble, 0);
    else
#endif
        NAM(amp->model, preset->gain, amp->bass, amp->mid, amp->treble);
    blk(p, PX_B_CAB, 1, amp->cab, amp->body, amp->bright, amp->mic, 0, 0);
    blues_apply_block(p, PX_B_COMP, &preset->comp);
    blues_apply_block(p, PX_B_MOD, &BLUES_MOD[preset->mod]);
    blues_apply_block(p, PX_B_DELAY, &BLUES_DELAY[preset->delay]);
    blues_apply_block(p, PX_B_REVERB, &BLUES_REVERB[preset->reverb]);
}

/* Return 1 when this slot is a Lab preset. The three V40 slots use the unchanged factory switch. */
static int blues_preset_factory(px_preset_t *p, int *idx)
{
    if (*idx < BLUES_COUNT) {
        blues_apply_preset(p, &BLUES_PRESETS[*idx]);
        return 1;
    }

    int slot = *idx - BLUES_COUNT;
    if (slot < BLUES_V40_COUNT) {
        static const uint8_t v40_refs[] = { 0, 6, 8 };   /* Twin Clean, TS Blues, Klon Crunch */
        *idx = v40_refs[slot];
        return 0;
    }

    slot -= BLUES_V40_COUNT;
    if (slot < BLUES_OPTO_COUNT) {
        blues_preset preset = BLUES_OPTO_AB;
        static const uint8_t level[] = { 59, 55 };
        preset.comp.type = slot == 0 ? PX_CMP_OPTO : PX_CMP_OPTO_T4;
        preset.level = level[slot];
        blues_apply_preset(p, &preset);
        setname(p, slot == 0 ? "AB OPTO OLD" : "AB OPTO T4");
        return 1;
    }

    const blues_drive_test *test = &BLUES_DRIVE_AB[slot - BLUES_OPTO_COUNT];
    blues_preset preset = {
        "", BL_BLACKFACE, 51, test->level, BL_MOD_NONE, BL_DLY_NONE, BL_REV_NONE,
        BL_DRIVE(test->drive, 24, 50, 53), BL_NONE
    };
    blues_apply_preset(p, &preset);
    setname(p, test->name);
    return 1;
}

#undef BL_NONE
#undef BL_DRIVE
#undef BL_OPTO
