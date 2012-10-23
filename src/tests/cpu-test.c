/***
  This file is part of PulseAudio.

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <check.h>
#include <unistd.h>
#include <math.h>

#include <pulse/rtclock.h>
#include <pulsecore/cpu-x86.h>
#include <pulsecore/cpu-orc.h>
#include <pulsecore/random.h>
#include <pulsecore/macro.h>
#include <pulsecore/endianmacros.h>
#include <pulsecore/sconv.h>
#include <pulsecore/sample-util.h>

#define PA_CPU_TEST_RUN_START(l, t1, t2)                        \
{                                                               \
    int _j, _k;                                                 \
    int _times = (t1), _times2 = (t2);                          \
    pa_usec_t _start, _stop;                                    \
    pa_usec_t _min = INT_MAX, _max = 0;                         \
    double _s1 = 0, _s2 = 0;                                    \
    const char *_label = (l);                                   \
                                                                \
    for (_k = 0; _k < _times2; _k++) {                          \
        _start = pa_rtclock_now();                              \
        for (_j = 0; _j < _times; _j++)

#define PA_CPU_TEST_RUN_STOP                                    \
        _stop = pa_rtclock_now();                               \
                                                                \
        if (_min > (_stop - _start)) _min = _stop - _start;     \
        if (_max < (_stop - _start)) _max = _stop - _start;     \
        _s1 += _stop - _start;                                  \
        _s2 += (_stop - _start) * (_stop - _start);             \
    }                                                           \
    pa_log_debug("%s: %llu usec (min = %llu, max = %llu, stddev = %g).", _label, (long long unsigned int)_s1,           \
            (long long unsigned int)_min, (long long unsigned int)_max, sqrt(_times2 * _s2 - _s1 * _s1) / _times2);     \
}

/* Common defines for svolume tests */
#define CHANNELS 2
#define SAMPLES 1022
#define TIMES 1000
#define TIMES2 100
#define PADDING 16

static void run_volume_test(pa_do_volume_func_t func, pa_do_volume_func_t orig_func) {
    int16_t samples[SAMPLES];
    int16_t samples_ref[SAMPLES];
    int16_t samples_orig[SAMPLES];
    int32_t volumes[CHANNELS + PADDING];
    int i, padding;

    pa_random(samples, sizeof(samples));
    memcpy(samples_ref, samples, sizeof(samples));
    memcpy(samples_orig, samples, sizeof(samples));

    for (i = 0; i < CHANNELS; i++)
        volumes[i] = PA_CLAMP_VOLUME((pa_volume_t)(rand() >> 15));
    for (padding = 0; padding < PADDING; padding++, i++)
        volumes[i] = volumes[padding];

    orig_func(samples_ref, volumes, CHANNELS, sizeof(samples));
    func(samples, volumes, CHANNELS, sizeof(samples));
    for (i = 0; i < SAMPLES; i++) {
        if (samples[i] != samples_ref[i]) {
            printf("%d: %04x != %04x (%04x * %08x)\n", i, samples[i], samples_ref[i],
                  samples_orig[i], volumes[i % CHANNELS]);
            fail();
        }
    }

    PA_CPU_TEST_RUN_START("func", TIMES, TIMES2) {
        memcpy(samples, samples_orig, sizeof(samples));
        func(samples, volumes, CHANNELS, sizeof(samples));
    } PA_CPU_TEST_RUN_STOP

    PA_CPU_TEST_RUN_START("orig", TIMES, TIMES2) {
        memcpy(samples_ref, samples_orig, sizeof(samples));
        orig_func(samples_ref, volumes, CHANNELS, sizeof(samples));
    } PA_CPU_TEST_RUN_STOP

    fail_unless(memcmp(samples_ref, samples, sizeof(samples)) == 0);
}

#if defined (__i386__) || defined (__amd64__)
START_TEST (svolume_mmx_test) {
    pa_do_volume_func_t orig_func, mmx_func;
    pa_cpu_x86_flag_t flags = 0;

    pa_cpu_get_x86_flags(&flags);

    if (!((flags & PA_CPU_X86_MMX) && (flags & PA_CPU_X86_CMOV))) {
        pa_log_info("MMX/CMOV not supported. Skipping");
        return;
    }

    orig_func = pa_get_volume_func(PA_SAMPLE_S16NE);
    pa_volume_func_init_mmx(flags);
    mmx_func = pa_get_volume_func(PA_SAMPLE_S16NE);

    pa_log_debug("Checking MMX svolume");
    run_volume_test(mmx_func, orig_func);
}
END_TEST

START_TEST (svolume_sse_test) {
    pa_do_volume_func_t orig_func, sse_func;
    pa_cpu_x86_flag_t flags = 0;

    pa_cpu_get_x86_flags(&flags);

    if (!(flags & PA_CPU_X86_SSE2)) {
        pa_log_info("SSE2 not supported. Skipping");
        return;
    }

    orig_func = pa_get_volume_func(PA_SAMPLE_S16NE);
    pa_volume_func_init_sse(flags);
    sse_func = pa_get_volume_func(PA_SAMPLE_S16NE);

    pa_log_debug("Checking SSE2 svolume");
    run_volume_test(sse_func, orig_func);
}
END_TEST
#endif /* defined (__i386__) || defined (__amd64__) */

#if defined (__arm__) && defined (__linux__)
START_TEST (svolume_arm_test) {
    pa_do_volume_func_t orig_func, arm_func;
    pa_cpu_arm_flag_t flags = 0;

    pa_cpu_get_arm_flags(&flags);

    if (!(flags & PA_CPU_ARM_V6)) {
        pa_log_info("ARMv6 instructions not supported. Skipping");
        return;
    }

    orig_func = pa_get_volume_func(PA_SAMPLE_S16NE);
    pa_volume_func_init_arm(flags);
    arm_func = pa_get_volume_func(PA_SAMPLE_S16NE);

    pa_log_debug("Checking ARM svolume");
    run_volume_test(arm_func, orig_func);
}
END_TEST
#endif /* defined (__arm__) && defined (__linux__) */

START_TEST (svolume_orc_test) {
    pa_do_volume_func_t orig_func, orc_func;
    pa_cpu_info cpu_info;

#if defined (__i386__) || defined (__amd64__)
    pa_zero(cpu_info);
    cpu_info.cpu_type = PA_CPU_X86;
    pa_cpu_get_x86_flags(&cpu_info.flags.x86);
#endif

    orig_func = pa_get_volume_func(PA_SAMPLE_S16NE);

    if (!pa_cpu_init_orc(cpu_info)) {
        pa_log_info("Orc not supported. Skipping");
        return;
    }

    orc_func = pa_get_volume_func(PA_SAMPLE_S16NE);

    pa_log_debug("Checking Orc svolume");
    run_volume_test(orc_func, orig_func);
}
END_TEST

#undef CHANNELS
#undef SAMPLES
#undef TIMES
#undef TIMES2
#undef PADDING
/* End svolume tests */

/* Start conversion tests */
#define SAMPLES 1022
#define TIMES 1000
#define TIMES2 100

static void run_conv_test_float_to_s16(pa_convert_func_t func, pa_convert_func_t orig_func) {
    int16_t samples[SAMPLES];
    int16_t samples_ref[SAMPLES];
    float floats[SAMPLES];
    int i;

    memset(samples_ref, 0, sizeof(samples_ref));
    memset(samples, 0, sizeof(samples));

    for (i = 0; i < SAMPLES; i++) {
        floats[i] = 2.1f * (rand()/(float) RAND_MAX - 0.5f);
    }

    orig_func(SAMPLES, floats, samples_ref);
    func(SAMPLES, floats, samples);

    for (i = 0; i < SAMPLES; i++) {
        if (samples[i] != samples_ref[i]) {
            printf ("%d: %04x != %04x (%f)\n", i, samples[i], samples_ref[i],
                      floats[i]);
            fail();
        }
    }

    PA_CPU_TEST_RUN_START("func", TIMES, TIMES2) {
        func(SAMPLES, floats, samples);
    } PA_CPU_TEST_RUN_STOP

    PA_CPU_TEST_RUN_START("orig", TIMES, TIMES2) {
        orig_func(SAMPLES, floats, samples_ref);
    } PA_CPU_TEST_RUN_STOP

    fail_unless(memcmp(samples_ref, samples, sizeof(samples)) == 0);
}

#if defined (__i386__) || defined (__amd64__)
START_TEST (sconv_sse_test) {
    pa_cpu_x86_flag_t flags = 0;
    pa_convert_func_t orig_func, sse_func;

    pa_cpu_get_x86_flags(&flags);

    if (!(flags & PA_CPU_X86_SSE2)) {
        pa_log_info("SSE2 not supported. Skipping");
        return;
    }

    orig_func = pa_get_convert_from_float32ne_function(PA_SAMPLE_S16LE);
    pa_convert_func_init_sse(flags);
    sse_func = pa_get_convert_from_float32ne_function(PA_SAMPLE_S16LE);

    pa_log_debug("Checking SSE sconv (s16 -> float)");
    run_conv_test_float_to_s16(sse_func, orig_func);
}
END_TEST
#endif /* defined (__i386__) || defined (__amd64__) */

#undef SAMPLES
#undef TIMES
/* End conversion tests */

int main(int argc, char *argv[]) {
    int failed = 0;
    Suite *s;
    TCase *tc;
    SRunner *sr;

    if (!getenv("MAKE_CHECK"))
        pa_log_set_level(PA_LOG_DEBUG);

    s = suite_create("CPU");

    /* Volume tests */
    tc = tcase_create("svolume");
#if defined (__i386__) || defined (__amd64__)
    tcase_add_test(tc, svolume_mmx_test);
    tcase_add_test(tc, svolume_sse_test);
#endif
#if defined (__arm__) && defined (__linux__)
    tcase_add_test(tc, svolume_arm_test);
#endif
    tcase_add_test(tc, svolume_orc_test);
    suite_add_tcase(s, tc);

    /* Converstion tests */
    tc = tcase_create("sconv");
#if defined (__i386__) || defined (__amd64__)
    tcase_add_test(tc, sconv_sse_test);
#endif
    suite_add_tcase(s, tc);

    sr = srunner_create(s);
    srunner_run_all(sr, CK_NORMAL);
    failed = srunner_ntests_failed(sr);
    srunner_free(sr);

    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
