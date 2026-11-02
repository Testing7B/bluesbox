/* level matching, generated on PC by calib3.c with the pedal's libm (px_math.c):
 * amps (amp + cab, gain 55) to 0.16 RMS on a plucked string; drives (all knobs at noon) to unity. */
static const float AMP_TRIM[PX_AMP_COUNT] = {
    1.839f, 0.697f, 0.757f, 0.325f, 0.348f, 0.264f, 0.354f, 0.288f, 
    0.236f, 0.213f, 0.208f, 0.505f, 0.208f, 0.261f, 0.218f, 0.206f, 
    0.200f, 0.239f, 0.200f, 0.194f, 0.204f, 0.200f, 0.199f, 0.200f, 
    0.178f, 2.121f, 0.265f, 0.626f, 0.581f};
static const float DRV_TRIM[PX_DRV_COUNT] = {
    0.097f, 0.086f, 0.087f, 0.101f, 0.085f, 0.076f, 0.080f, 0.200f, 
    0.143f, 0.209f, 0.130f, 0.129f, 0.080f, 0.169f, 0.173f, 0.160f, 
    0.084f, 0.073f, 0.071f, 0.187f, 0.072f, 0.258f, 0.570f, 0.035f, 
    0.083f, 0.216f, 0.054f};
