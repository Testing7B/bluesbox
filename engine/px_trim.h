/* level matching, generated on PC by calib3.c with the pedal's libm (px_math.c):
 * amps (amp + cab, gain 55) to 0.16 RMS on a plucked string; drives (all knobs at noon) to unity. */
static const float AMP_TRIM[PX_AMP_COUNT] = {
    1.744f, 0.666f, 0.723f, 0.313f, 0.327f, 0.261f, 0.344f, 0.284f, 
    0.238f, 0.215f, 0.222f, 0.475f, 0.206f, 0.261f, 0.225f, 0.214f, 
    0.213f, 0.255f, 0.210f, 0.208f, 0.212f, 0.206f, 0.210f, 0.217f, 
    0.179f, 1.953f, 0.237f, 0.597f, 0.540f};
static const float DRV_TRIM[PX_DRV_COUNT] = {
    0.097f, 0.086f, 0.087f, 0.092f, 0.085f, 0.076f, 0.080f, 0.200f, 
    0.143f, 0.209f, 0.130f, 0.129f, 0.080f, 0.169f, 0.173f, 0.160f, 
    0.084f, 0.073f, 0.071f, 0.187f, 0.072f, 0.258f, 0.570f, 0.035f, 
    0.083f, 0.216f, 0.047f};
