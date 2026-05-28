/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Energy over a test window
 *
 * @defgroup tapi_power_energy Energy measurement (tapi_power_energy)
 * @ingroup tapi_power
 * @{
 *
 * The headline number of a battery-life or efficiency test: the energy a
 * device consumed while something ran. It is the integral of power over
 * time, and it is measured here by sampling the instrument across the
 * window and integrating - which works with any instrument that reads
 * power or current, whatever the brand, and does not depend on the
 * instrument having its own accumulator.
 *
 * @code
 * tapi_power_energy result;
 *
 * // Sample every 50 ms while the device runs the workload, for 10 s.
 * CHECK_RC(tapi_power_energy_measure(&inst, 10000, 50, &result));
 * RING("Workload used %.3f J (%.1f mWh), avg %.3f W, peak %.3f W",
 *      result.joules, result.milliwatt_hours, result.avg_power,
 *      result.peak_power);
 * @endcode
 *
 * For a window whose end the test decides at run time - stop when the
 * device signals done - open a session, sample in a loop, and read the
 * result when the work finishes:
 *
 * @code
 * tapi_power_energy_session s;
 *
 * CHECK_RC(tapi_power_energy_start(&inst, &s));
 * while (!workload_done())
 * {
 *     CHECK_RC(tapi_power_energy_sample(&s));
 *     usleep(50000);
 * }
 * CHECK_RC(tapi_power_energy_stop(&s, &result));
 * @endcode
 *
 * @note The integral is a trapezoidal sum over the samples taken, so its
 *       accuracy follows the sampling interval against how fast the load
 *       changes: sample fast enough that power does not swing far between
 *       two samples.
 */

#ifndef __TSF_TAPI_POWER_ENERGY_H__
#define __TSF_TAPI_POWER_ENERGY_H__

#include <sys/time.h>

#include "tapi_power.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The result of an energy measurement over a window. */
typedef struct tapi_power_energy {
    /** Energy consumed, joules (watt-seconds). */
    double joules;
    /** The same in milliwatt-hours, the usual battery unit. */
    double milliwatt_hours;
    /** Duration of the window, seconds. */
    double seconds;
    /** Mean power over the window, watts. */
    double avg_power;
    /** Highest instantaneous power seen, watts. */
    double peak_power;
    /** Lowest instantaneous power seen, watts. */
    double min_power;
    /** Number of samples taken. */
    unsigned int samples;
} tapi_power_energy;

/** An in-progress energy integration. */
typedef struct tapi_power_energy_session {
    /** The instrument being sampled. */
    tapi_power_instrument *inst;
    /** When the session started. */
    struct timeval start;
    /** When the last sample was taken. */
    struct timeval last;
    /** Power at the last sample, watts. */
    double last_power;
    /** Accumulated energy, joules. */
    double joules;
    /** Peak power, watts. */
    double peak_power;
    /** Minimum power, watts. */
    double min_power;
    /** Number of samples taken. */
    unsigned int samples;
} tapi_power_energy_session;

/**
 * Start an energy integration: record the start time and take the first
 * power sample.
 *
 * @param[in]  inst     Instrument (open).
 * @param[out] session  Session; end it with tapi_power_energy_stop().
 *
 * @return Status code.
 */
extern te_errno tapi_power_energy_start(tapi_power_instrument *inst,
                                        tapi_power_energy_session *session);

/**
 * Take one power sample and add the energy since the previous sample to
 * the running total (trapezoidal). Call it repeatedly across the window.
 *
 * @param session   Session.
 *
 * @return Status code.
 */
extern te_errno tapi_power_energy_sample(tapi_power_energy_session *session);

/**
 * Take a final sample and finish the integration.
 *
 * @param[in]  session  Session.
 * @param[out] result   Where to save the result.
 *
 * @return Status code.
 */
extern te_errno tapi_power_energy_stop(tapi_power_energy_session *session,
                                       tapi_power_energy *result);

/**
 * Measure energy over a fixed window: sample every @p interval_ms for
 * @p duration_ms and integrate.
 *
 * @param[in]  inst         Instrument (open).
 * @param[in]  duration_ms  Length of the window, ms.
 * @param[in]  interval_ms  Sampling interval, ms.
 * @param[out] result       Where to save the result.
 *
 * @return Status code.
 */
extern te_errno tapi_power_energy_measure(tapi_power_instrument *inst,
                                          unsigned int duration_ms,
                                          unsigned int interval_ms,
                                          tapi_power_energy *result);

/**
 * Write an energy result into the log.
 *
 * @param result    Result.
 * @param what      What the window was, for the message.
 */
extern void tapi_power_energy_log(const tapi_power_energy *result,
                                  const char *what);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_POWER_ENERGY_H__ */

/**@} <!-- END tapi_power_energy --> */
