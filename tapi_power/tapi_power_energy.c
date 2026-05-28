/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Energy over a test window
 *
 * Software integration of power over time: sample the instrument, add the
 * trapezoidal area between samples, report joules and derived figures.
 */

#define TE_LGR_USER "TAPI POWER ENERGY"

#include "te_config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_string.h"
#include "te_sleep.h"

#include "tapi_power_energy.h"

/** Seconds between two timevals. */
static double
tv_diff(const struct timeval *a, const struct timeval *b)
{
    return (b->tv_sec - a->tv_sec) + (b->tv_usec - a->tv_usec) / 1e6;
}

/* See description in tapi_power_energy.h */
te_errno
tapi_power_energy_start(tapi_power_instrument *inst,
                        tapi_power_energy_session *session)
{
    double power;
    te_errno rc;

    memset(session, 0, sizeof(*session));
    session->inst = inst;
    session->peak_power = -INFINITY;
    session->min_power = INFINITY;

    rc = tapi_power_measure_power(inst, &power);
    if (rc != 0)
        return rc;

    gettimeofday(&session->start, NULL);
    session->last = session->start;
    session->last_power = power;
    session->peak_power = power;
    session->min_power = power;
    session->samples = 1;

    return 0;
}

/* See description in tapi_power_energy.h */
te_errno
tapi_power_energy_sample(tapi_power_energy_session *session)
{
    struct timeval now;
    double power;
    double dt;
    te_errno rc;

    rc = tapi_power_measure_power(session->inst, &power);
    if (rc != 0)
        return rc;

    gettimeofday(&now, NULL);
    dt = tv_diff(&session->last, &now);

    /* Trapezoidal area between the previous sample and this one. */
    session->joules += (session->last_power + power) / 2.0 * dt;

    session->last = now;
    session->last_power = power;
    if (power > session->peak_power)
        session->peak_power = power;
    if (power < session->min_power)
        session->min_power = power;
    session->samples++;

    return 0;
}

/** Fill a result from a session's accumulated state. */
static void
finish(const tapi_power_energy_session *session, const struct timeval *end,
       tapi_power_energy *result)
{
    memset(result, 0, sizeof(*result));
    result->joules = session->joules;
    result->milliwatt_hours = session->joules / 3.6;   /* J -> mWh */
    result->seconds = tv_diff(&session->start, end);
    result->avg_power = (result->seconds > 0) ?
                        session->joules / result->seconds : 0.0;
    result->peak_power = session->peak_power;
    result->min_power = session->min_power;
    result->samples = session->samples;
}

/* See description in tapi_power_energy.h */
te_errno
tapi_power_energy_stop(tapi_power_energy_session *session,
                       tapi_power_energy *result)
{
    struct timeval end;
    te_errno rc;

    rc = tapi_power_energy_sample(session);
    if (rc != 0)
        return rc;

    gettimeofday(&end, NULL);
    finish(session, &end, result);

    return 0;
}

/* See description in tapi_power_energy.h */
te_errno
tapi_power_energy_measure(tapi_power_instrument *inst,
                          unsigned int duration_ms, unsigned int interval_ms,
                          tapi_power_energy *result)
{
    tapi_power_energy_session session;
    struct timeval deadline;
    struct timeval now;
    te_errno rc;

    if (interval_ms == 0)
        interval_ms = 50;

    rc = tapi_power_energy_start(inst, &session);
    if (rc != 0)
        return rc;

    gettimeofday(&deadline, NULL);
    deadline.tv_sec += duration_ms / 1000;
    deadline.tv_usec += (duration_ms % 1000) * 1000;
    deadline.tv_sec += deadline.tv_usec / 1000000;
    deadline.tv_usec %= 1000000;

    for (;;)
    {
        te_msleep(interval_ms);

        rc = tapi_power_energy_sample(&session);
        if (rc != 0)
            return rc;

        gettimeofday(&now, NULL);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_usec >= deadline.tv_usec))
            break;
    }

    gettimeofday(&now, NULL);
    finish(&session, &now, result);

    return 0;
}

/* See description in tapi_power_energy.h */
void
tapi_power_energy_log(const tapi_power_energy *result, const char *what)
{
    RING("Energy over %s: %.4f J (%.3f mWh) in %.2f s; "
         "power avg %.4f W, min %.4f W, peak %.4f W; %u samples",
         what != NULL ? what : "the window", result->joules,
         result->milliwatt_hours, result->seconds, result->avg_power,
         result->min_power, result->peak_power, result->samples);
}
