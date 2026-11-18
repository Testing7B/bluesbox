/* SPDX-License-Identifier: GPL-3.0-only
 * Stage A pre-limiter DSP profiler, using the firmware's own px_engine_process().
 * Unlike the browser output, engine output here is NOT clipped to [-1, 1].
 * Host timings are NOT measured AC791N cycles.
 *
 * Build (macOS/Linux):
 *   cc -std=c11 -O2 -fno-builtin -ffp-contract=off -Iengine \
 *      tests/lab/raw-output.c engine/px_dsp.c firmware/px_math.c -lm -o build/raw-output
 * Run:
 *   ./build/raw-output 600 > build/raw-output.csv
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include "px_dsp.h"

static px_engine E; /* a single engine; compiler and test harness use same source code */
static float in[64], out[64];
static double now_us(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1.0;
    return (double)ts.tv_sec * 1000000.0 + (double)ts.tv_nsec * 0.001;
}
static float signal_at(uint64_t n)
{
    const double t = (double)n / 44100.0;
    const double impact = (n % 22050u) < 180u ? 1.0 : 0.45;
    return (float)(impact * (0.68 * sin(2.0 * 3.14159265358979323846 * 110.0 * t) +
                              0.24 * sin(2.0 * 3.14159265358979323846 * 659.0 * t) +
                              0.08 * sin(2.0 * 3.14159265358979323846 * 3101.0 * t)));
}
static int compare_double(const void *aa, const void *bb)
{
    const double a = *(const double *)aa, b = *(const double *)bb;
    return (a > b) - (a < b);
}
static double percentile(const double *samples, unsigned n, double fraction)
{
    const double k = (n - 1u) * fraction;
    const unsigned lo = (unsigned)k, hi = lo + (lo + 1u < n);
    return samples[lo] + (samples[hi] - samples[lo]) * (k - (double)lo);
}
int main(int argc, char **argv)
{
    unsigned blocks = 600;
    if (argc > 1) {
        char *end = NULL;
        const unsigned long n = strtoul(argv[1], &end, 10);
        if (!end || *end || n < 2u || n > 200000u) {
            fprintf(stderr, "blocks must be an integer in [2, 200000]\n");
            return 2;
        }
        blocks = (unsigned)n;
    }
    double *times = malloc(sizeof(double) * blocks);
    if (!times) return 2;
    printf("preset,scenario,blocks,preclip_peak,preclip_rms,above_fullscale_samples,nonfinite_samples,host_p99_us,host_p999_us,host_max_us\n");
    for (int preset = 0; preset < 52; ++preset) {
        for (int scenario = 0; scenario < 3; ++scenario) {
            const float level = scenario == 0 ? 0.00015f : scenario == 1 ? 0.15f : 0.8f;
            const char *name = scenario == 0 ? "quiet" : scenario == 1 ? "nominal" : "hard";
            px_preset_t p;
            px_preset_factory(&p, preset);
            px_engine_init(&E, 44100.0f);
            px_engine_set(&E, &p);
            double sum = 0.0, peak = 0.0;
            unsigned long long above = 0, nonfinite = 0;
            for (unsigned b = 0; b < blocks + 12u; ++b) {
                for (unsigned j = 0; j < 64; ++j) {
                    const uint64_t pos = ((uint64_t)b * 64u) + j;
                    in[j] = signal_at(pos) * level;
                }
                const double begin = now_us();
                px_engine_process(&E, in, out, 64);
                const double end = now_us();
                if (begin < 0.0 || end < 0.0) return 3;
                if (b < 12u) continue; /* transient warm-up, separate from timed/report windows */
                times[b - 12u] = end - begin;
                for (unsigned j = 0; j < 64; ++j) {
                    const double v = out[j], a = fabs(v);
                    if (!isfinite(v)) { nonfinite++; continue; }
                    if (a > peak) peak = a;
                    sum += v * v;
                    if (a > 1.0) above++;
                }
            }
            qsort(times, blocks, sizeof(*times), compare_double);
            printf("%d,%s,%u,%.9g,%.9g,%llu,%llu,%.4f,%.4f,%.4f\n",
                   preset + 1, name, blocks, peak,
                   sqrt(sum / (64.0 * (double)blocks)), above, nonfinite,
                   percentile(times, blocks, 0.99),
                   percentile(times, blocks, 0.999),
                   times[blocks - 1u]);
            if (nonfinite) { free(times); return 1; }
        }
    }
    free(times);
    return 0;
}
