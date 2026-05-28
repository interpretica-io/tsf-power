/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Embedded power profilers
 *
 * @defgroup tapi_power_profiler Embedded power profilers (tapi_power_profiler)
 * @ingroup tapi_power
 * @{
 *
 * The other kind of instrument: the USB power profilers that measure what
 * a low-power embedded device draws, down to nanoamps, and that are driven
 * not by SCPI over the network but by a vendor tool over USB or serial. A
 * BLE beacon or a sensor node spends almost all its life asleep at
 * microamps and wakes for milliamps in bursts; a bench supply cannot see
 * that, and these can.
 *
 * The instruments driven:
 *
 * | Kind | Instrument | Tool on the agent |
 * |---|---|---|
 * | #TAPI_POWER_PROFILER_NORDIC_PPK2 | Nordic Power Profiler Kit II | @c ppk2-api |
 * | #TAPI_POWER_PROFILER_JOULESCOPE | Joulescope JS110/JS220 | @c joulescope |
 * | #TAPI_POWER_PROFILER_QOITECH_OTII | Qoitech Otii Arc/Ace | @c otii |
 * | #TAPI_POWER_PROFILER_MONSOON | Monsoon HV Power Monitor | @c monsoon |
 * | #TAPI_POWER_PROFILER_GENERIC | any tool you give a command line for | (yours) |
 *
 * Because these run on the agent the profiler is plugged into, this part
 * of the library uses the agent - unlike the SCPI part, which reaches a
 * bench instrument over the network. A measurement runs the tool for a
 * duration and reads back the average current, the voltage and the energy.
 *
 * @code
 * tapi_power_profiler prof = {
 *     .kind = TAPI_POWER_PROFILER_NORDIC_PPK2,
 *     .device = "/dev/ttyACM0", .supply_mv = 3300,
 * };
 * tapi_power_profile result;
 *
 * CHECK_RC(tapi_power_profiler_measure(factory, &prof, 10000, &result));
 * RING("Sleeping node: %.3f uA average, %.3f uWh over 10 s",
 *      result.avg_current * 1e6, result.milliwatt_hours * 1000);
 * @endcode
 *
 * @note The vendor tools and their exact command lines and output vary by
 *       version. Each kind carries a default command template that a lab
 *       adjusts through @a command; the numbers are read from the tool's
 *       output by label, so a template that prints average current,
 *       voltage and energy is what a new tool needs.
 */

#ifndef __TSF_TAPI_POWER_PROFILER_H__
#define __TSF_TAPI_POWER_PROFILER_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "tapi_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default timeout headroom over the measurement duration, ms. */
#define TAPI_POWER_PROFILER_OVERHEAD_MS  30000

/** A kind of embedded power profiler. */
typedef enum tapi_power_profiler_kind {
    /** A tool you supply the command line for. */
    TAPI_POWER_PROFILER_GENERIC = 0,
    /** Nordic Power Profiler Kit II (PPK2). */
    TAPI_POWER_PROFILER_NORDIC_PPK2,
    /** Joulescope JS110 / JS220. */
    TAPI_POWER_PROFILER_JOULESCOPE,
    /** Qoitech Otii Arc / Ace. */
    TAPI_POWER_PROFILER_QOITECH_OTII,
    /** Monsoon High Voltage Power Monitor. */
    TAPI_POWER_PROFILER_MONSOON,
} tapi_power_profiler_kind;

/**
 * The name of a profiler kind.
 *
 * @param kind      Kind.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_power_profiler_kind2str(tapi_power_profiler_kind kind);

/** An embedded power profiler and how it is reached on the agent. */
typedef struct tapi_power_profiler {
    /** Kind, which selects the default tool and command. */
    tapi_power_profiler_kind kind;
    /**
     * Device on the agent: a serial port (@c "/dev/ttyACM0"), or a serial
     * number the tool selects by. Passed to the command as @c $DEV.
     */
    const char *device;
    /**
     * Supply voltage to source, millivolts, for a profiler that powers the
     * device under test itself (PPK2 source mode, Otii). 0 to measure only.
     */
    unsigned int supply_mv;
    /**
     * Command template to run on the agent, overriding the kind's default.
     * It measures for @c $DURATION seconds on @c $DEV at @c $VMV millivolts
     * and prints the average current, voltage and energy. May be @c NULL to
     * use the built-in default for the kind (required for GENERIC).
     */
    const char *command;
} tapi_power_profiler;

/** The result of a profiler measurement over a window. */
typedef struct tapi_power_profile {
    /** Average current, amperes. */
    double avg_current;
    /** Average voltage, volts (the supply, or as measured). */
    double avg_voltage;
    /** Average power, watts. */
    double avg_power;
    /** Peak current, amperes (0 if the tool did not report it). */
    double peak_current;
    /** Energy over the window, joules. */
    double joules;
    /** The same in milliwatt-hours. */
    double milliwatt_hours;
    /** Duration of the window, seconds. */
    double seconds;
} tapi_power_profile;

/**
 * Check whether the tool for a profiler kind is on the agent.
 *
 * @param[in]  factory  Job factory.
 * @param[in]  prof     Profiler.
 * @param[out] present  Where to save the answer.
 *
 * @return Status code.
 */
extern te_errno tapi_power_profiler_available(tapi_job_factory_t *factory,
                                              const tapi_power_profiler *prof,
                                              bool *present);

/**
 * Measure with a profiler over a fixed window.
 *
 * @param[in]  factory      Job factory (the agent the profiler is on).
 * @param[in]  prof         Profiler.
 * @param[in]  duration_ms  Length of the measurement, ms.
 * @param[out] result       Where to save the result.
 *
 * @return Status code.
 */
extern te_errno tapi_power_profiler_measure(tapi_job_factory_t *factory,
                                            const tapi_power_profiler *prof,
                                            unsigned int duration_ms,
                                            tapi_power_profile *result);

/**
 * Parse a profiler tool's output into a result. Separate from running it
 * so it can be exercised on captured output.
 *
 * @param[in]  kind         Profiler kind (selects the labels to read).
 * @param[in]  text         The tool's output.
 * @param[in]  seconds      Duration of the window, to derive energy when
 *                          the tool reported only average power.
 * @param[out] result       Where to save the result.
 *
 * @return Status code.
 * @retval TE_EINVAL    No current or power figure could be read.
 */
extern te_errno tapi_power_profiler_parse(tapi_power_profiler_kind kind,
                                          const char *text, double seconds,
                                          tapi_power_profile *result);

/**
 * Write a profiler result into the log.
 *
 * @param result    Result.
 * @param what      What the window was, for the message.
 */
extern void tapi_power_profile_log(const tapi_power_profile *result,
                                   const char *what);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_POWER_PROFILER_H__ */

/**@} <!-- END tapi_power_profiler --> */
