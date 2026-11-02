/* level matching, generated on PC by calib3.c with the pedal's libm (px_math.c):
 * amps (amp + cab, gain 55) to 0.16 RMS on a plucked string; drives (all knobs at noon) to unity. */
static const float AMP_TRIM[PX_AMP_COUNT] = {
    1.751f, 0.671f, 0.728f, 0.317f, 0.332f, 0.263f, 0.348f, 0.287f, 
    0.240f, 0.217f, 0.224f, 0.479f, 0.209f, 0.263f, 0.228f, 0.216f, 
    0.215f, 0.258f, 0.212f, 0.210f, 0.214f, 0.208f, 0.212f, 0.219f, 
    0.181f, 1.969f, 0.241f, 0.601f, 0.546f};
static const float DRV_TRIM[PX_DRV_COUNT] = {
    0.097f, 0.086f, 0.087f, 0.092f, 0.085f, 0.076f, 0.080f, 0.200f, 
    0.143f, 0.209f, 0.130f, 0.129f, 0.080f, 0.169f, 0.173f, 0.160f, 
    0.084f, 0.073f, 0.071f, 0.187f, 0.072f, 0.258f, 0.570f, 0.035f, 
    0.083f, 0.216f, 0.047f};
