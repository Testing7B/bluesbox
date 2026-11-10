/* SPDX-License-Identifier: GPL-3.0-only
 * 808 feedback branch. Independent approximation of the steady-state
 * diode equation in Yeh, Abel and Smith, DAFx-07, eq. 22.
 * Solve x = y + 2 Is R sinh(y / Vt) with Is = 2 nA, R = 200 kOhm,
 * and effective Vt = 52 mV. The drive knob sets branch gain separately.
 * Table inputs: 0..1 V in 1/64 V steps, 1..9 V in 1/4 V steps,
 * 9..129 V in 4 V steps. The negative side is the odd reflection.
 */
static const float px_blues_808_curve[] = {
    0.00000000f, 0.01538484f, 0.03074889f, 0.04606981f, 0.06132220f, 0.07647601f,
    0.09149499f, 0.10633537f, 0.12094488f, 0.13526268f, 0.14922052f, 0.16274546f,
    0.17576433f, 0.18820946f, 0.20002461f, 0.21116990f, 0.22162461f, 0.23138728f,
    0.24047356f, 0.24891252f, 0.25674246f, 0.26400692f, 0.27075150f, 0.27702146f,
    0.28286011f, 0.28830790f, 0.29340196f, 0.29817595f, 0.30266014f, 0.30688165f,
    0.31086465f, 0.31463066f, 0.31819887f, 0.32158633f, 0.32480826f, 0.32787825f,
    0.33080844f, 0.33360971f, 0.33629182f, 0.33886358f, 0.34133289f, 0.34370694f,
    0.34599219f, 0.34819455f, 0.35031937f, 0.35237153f, 0.35435551f, 0.35627538f,
    0.35813489f, 0.35993749f, 0.36168636f, 0.36338441f, 0.36503435f, 0.36663868f,
    0.36819973f, 0.36971965f, 0.37120043f, 0.37264396f, 0.37405196f, 0.37542607f,
    0.37676780f, 0.37807858f, 0.37935975f, 0.38061254f, 0.38183816f, 0.39849162f,
    0.41127097f, 0.42161646f, 0.43029651f, 0.43776749f, 0.44432189f, 0.45015809f,
    0.45541667f, 0.46020081f, 0.46458844f, 0.46863981f, 0.47240248f, 0.47591461f,
    0.47920730f, 0.48230622f, 0.48523278f, 0.48800506f, 0.49063843f, 0.49314609f,
    0.49553945f, 0.49782842f, 0.50002170f, 0.50212693f, 0.50415089f, 0.50609959f,
    0.50797842f, 0.50979219f, 0.51154524f, 0.51324150f, 0.51488451f, 0.51647752f,
    0.51802345f, 0.53802984f, 0.55245954f, 0.56375053f, 0.57302617f, 0.58089759f,
    0.58773431f, 0.59377686f, 0.59919058f, 0.60409405f, 0.60857517f, 0.61270093f,
    0.61652355f, 0.62008453f, 0.62341739f, 0.62654959f, 0.62950393f, 0.63229950f,
    0.63495252f, 0.63747680f, 0.63988427f, 0.64218525f, 0.64438877f, 0.64650274f,
    0.64853416f, 0.65048923f, 0.65237348f, 0.65419187f, 0.65594884f, 0.65764840f,
    0.65929419f,
};

PX_AINL static inline float px_blues_808_clip(float driven)
{
    const float magnitude = driven < 0.0f ? -driven : driven;
    float result = px_blues_808_curve[126];
    if (magnitude < 129.0f) {
        float position;
        if (magnitude < 1.0f)
            position = magnitude * 64.0f;
        else if (magnitude < 9.0f)
            position = 64.0f + (magnitude - 1.0f) * 4.0f;
        else
            position = 96.0f + (magnitude - 9.0f) * 0.25f;
        const unsigned index = (unsigned)position;
        if (index < 126) {
            const float lower = px_blues_808_curve[index];
            result = lower + (position - (float)index) * (px_blues_808_curve[index + 1] - lower);
        }
    }
    return driven < 0.0f ? -result : result;
}
