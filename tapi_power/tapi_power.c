/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Power and energy measurement from a Test Agent
 *
 * The core: the per-brand SCPI command sets, opening an instrument and
 * reading voltage, current and power.
 */

#define TE_LGR_USER "TAPI POWER"

#include "te_config.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"

#include "tapi_power.h"

/** The SCPI command set for one brand. */
typedef struct power_backend {
    tapi_power_brand brand;
    const char *name;
    /** Commands sent once at open (may contain @c %u for the channel). */
    const char *init[4];
    /** Number of @a init commands. */
    size_t n_init;
    /** Channel-select command sent before a measurement, or @c NULL. */
    const char *select;
    /** One query returning @c "V,I,P" (Rigol), or @c NULL. */
    const char *q_all;
    /** Voltage query (may contain @c %u), or @c NULL. */
    const char *q_v;
    /** Current query, or @c NULL. */
    const char *q_i;
    /** Power query, or @c NULL (then power is V*I). */
    const char *q_p;
} power_backend;

static const power_backend backends[] = {
    {
        .brand = TAPI_POWER_GENERIC, .name = "generic",
        .q_v = "MEAS:VOLT?", .q_i = "MEAS:CURR?", .q_p = NULL,
    },
    {
        /* N6705 / N7900: channel list (@n) on each query. */
        .brand = TAPI_POWER_KEYSIGHT, .name = "Keysight",
        .q_v = "MEAS:VOLT? (@%u)", .q_i = "MEAS:CURR? (@%u)",
        .q_p = "MEAS:POW? (@%u)",
    },
    {
        /* NGM/NGU: select the channel, then measure. */
        .brand = TAPI_POWER_ROHDE_SCHWARZ, .name = "Rohde&Schwarz",
        .select = "INST:NSEL %u",
        .q_v = "MEAS:VOLT?", .q_i = "MEAS:CURR?", .q_p = "MEAS:POW?",
    },
    {
        /* WT-series: configure elements U,I,P then read them numerically. */
        .brand = TAPI_POWER_YOKOGAWA, .name = "Yokogawa",
        .init = { ":NUMeric:FORMat ASCii", ":NUMeric:NORMal:NUMber 3",
                  ":NUMeric:NORMal:ITEM1 U,%u", ":NUMeric:NORMal:ITEM2 I,%u" },
        .n_init = 4,
        .q_v = ":NUMeric:NORMal:VALue? 1", .q_i = ":NUMeric:NORMal:VALue? 2",
        .q_p = ":NUMeric:NORMal:VALue? 3",
    },
    {
        /* DMM6500 / 2280S: MEASure tree; power computed from V*I. */
        .brand = TAPI_POWER_KEITHLEY, .name = "Keithley",
        .q_v = "MEAS:VOLT?", .q_i = "MEAS:CURR?", .q_p = NULL,
    },
    {
        /* DP800: one query returns "V,I,P" for a channel. */
        .brand = TAPI_POWER_RIGOL, .name = "Rigol",
        .q_all = "MEAS:ALL? CH%u",
    },
    {
        /* 66200 power meter: FETCh tree. */
        .brand = TAPI_POWER_CHROMA, .name = "Chroma",
        .q_v = "FETCh:VOLTage?", .q_i = "FETCh:CURRent?",
        .q_p = "FETCh:POWer:REAL?",
    },
};

/* See description in tapi_power.h */
const char *
tapi_power_brand2str(tapi_power_brand brand)
{
    size_t i;

    for (i = 0; i < TE_ARRAY_LEN(backends); i++)
    {
        if (backends[i].brand == brand)
            return backends[i].name;
    }

    return "unknown";
}

/** Get the backend for a brand. */
static const power_backend *
backend_of(tapi_power_brand brand)
{
    size_t i;

    for (i = 0; i < TE_ARRAY_LEN(backends); i++)
    {
        if (backends[i].brand == brand)
            return &backends[i];
    }

    return &backends[0];
}

/** Format a command, substituting the channel for a @c %u if present. */
static char *
fmt_cmd(const char *fmt, unsigned int channel)
{
    if (strstr(fmt, "%u") != NULL)
        return te_string_fmt(fmt, channel);

    return TE_STRDUP(fmt);
}

/* See description in tapi_power.h */
te_errno
tapi_power_open(tapi_power_instrument *inst)
{
    const power_backend *b = backend_of(inst->brand);
    te_string idn = TE_STRING_INIT;
    unsigned int chan = (inst->channel != 0) ? inst->channel : 1;
    size_t i;
    te_errno rc;

    inst->idn[0] = '\0';

    rc = tapi_power_scpi_open(inst->host, inst->port, inst->timeout_ms,
                             &inst->scpi);
    if (rc != 0)
        return rc;

    /* Identify; useful in the log and to confirm the link. */
    rc = tapi_power_scpi_query(&inst->scpi, "*IDN?", &idn);
    if (rc == 0 && idn.ptr != NULL)
    {
        te_strlcpy(inst->idn, idn.ptr, sizeof(inst->idn));
        RING("Power instrument at %s: %s (driven as %s)", inst->host,
             inst->idn, b->name);
    }
    te_string_free(&idn);

    /* Per-brand initialisation. */
    for (i = 0; i < b->n_init && rc == 0; i++)
    {
        char *cmd = fmt_cmd(b->init[i], chan);

        rc = tapi_power_scpi_send(&inst->scpi, cmd);
        free(cmd);
    }

    if (rc != 0)
        tapi_power_scpi_close(&inst->scpi);

    return rc;
}

/* See description in tapi_power.h */
void
tapi_power_close(tapi_power_instrument *inst)
{
    tapi_power_scpi_close(&inst->scpi);
}

/** Select the channel, if the backend needs a separate select command. */
static te_errno
select_channel(tapi_power_instrument *inst, const power_backend *b)
{
    unsigned int chan = (inst->channel != 0) ? inst->channel : 1;
    char *cmd;
    te_errno rc;

    if (b->select == NULL)
        return 0;

    cmd = fmt_cmd(b->select, chan);
    rc = tapi_power_scpi_send(&inst->scpi, cmd);
    free(cmd);

    return rc;
}

/** Query one channel-formatted value. */
static te_errno
query_value(tapi_power_instrument *inst, const char *fmt, double *value)
{
    unsigned int chan = (inst->channel != 0) ? inst->channel : 1;
    char *cmd = fmt_cmd(fmt, chan);
    te_errno rc;

    rc = tapi_power_scpi_query_double(&inst->scpi, cmd, value);
    free(cmd);

    return rc;
}

/* See description in tapi_power.h */
te_errno
tapi_power_measure(tapi_power_instrument *inst, tapi_power_reading *reading)
{
    const power_backend *b = backend_of(inst->brand);
    unsigned int chan = (inst->channel != 0) ? inst->channel : 1;
    te_errno rc;

    reading->voltage = NAN;
    reading->current = NAN;
    reading->power = NAN;

    rc = select_channel(inst, b);
    if (rc != 0)
        return rc;

    /* Rigol answers V, I and P in one query. */
    if (b->q_all != NULL)
    {
        te_string resp = TE_STRING_INIT;
        char *cmd = fmt_cmd(b->q_all, chan);

        rc = tapi_power_scpi_query(&inst->scpi, cmd, &resp);
        free(cmd);
        if (rc == 0 && resp.ptr != NULL)
        {
            /* "V,I,P" */
            char *save = NULL;
            char *v = strtok_r(resp.ptr, ",", &save);
            char *i = (v != NULL) ? strtok_r(NULL, ",", &save) : NULL;
            char *p = (i != NULL) ? strtok_r(NULL, ",", &save) : NULL;

            if (v != NULL) reading->voltage = strtod(v, NULL);
            if (i != NULL) reading->current = strtod(i, NULL);
            if (p != NULL) reading->power = strtod(p, NULL);
        }
        te_string_free(&resp);
        if (rc != 0)
            return rc;
    }
    else
    {
        if (b->q_v != NULL)
        {
            rc = query_value(inst, b->q_v, &reading->voltage);
            if (rc != 0)
                return rc;
        }
        if (b->q_i != NULL)
        {
            rc = query_value(inst, b->q_i, &reading->current);
            if (rc != 0)
                return rc;
        }
        if (b->q_p != NULL)
        {
            rc = query_value(inst, b->q_p, &reading->power);
            if (rc != 0)
                return rc;
        }
    }

    /* Fill power from V*I when the instrument does not report it. */
    if (isnan(reading->power) && !isnan(reading->voltage) &&
        !isnan(reading->current))
        reading->power = reading->voltage * reading->current;

    return 0;
}

/* See description in tapi_power.h */
te_errno
tapi_power_measure_power(tapi_power_instrument *inst, double *watts)
{
    tapi_power_reading r;
    te_errno rc;

    rc = tapi_power_measure(inst, &r);
    if (rc == 0)
    {
        if (isnan(r.power))
        {
            ERROR("The instrument did not yield a power reading");
            rc = TE_RC(TE_TAPI, TE_EINVAL);
        }
        else
        {
            *watts = r.power;
        }
    }

    return rc;
}

/* See description in tapi_power.h */
te_errno
tapi_power_measure_current(tapi_power_instrument *inst, double *amps)
{
    const power_backend *b = backend_of(inst->brand);
    te_errno rc;

    rc = select_channel(inst, b);
    if (rc == 0 && b->q_i != NULL)
        rc = query_value(inst, b->q_i, amps);
    else if (rc == 0)
    {
        tapi_power_reading r;

        rc = tapi_power_measure(inst, &r);
        if (rc == 0)
            *amps = r.current;
    }

    return rc;
}

/* See description in tapi_power.h */
te_errno
tapi_power_measure_voltage(tapi_power_instrument *inst, double *volts)
{
    const power_backend *b = backend_of(inst->brand);
    te_errno rc;

    rc = select_channel(inst, b);
    if (rc == 0 && b->q_v != NULL)
        rc = query_value(inst, b->q_v, volts);
    else if (rc == 0)
    {
        tapi_power_reading r;

        rc = tapi_power_measure(inst, &r);
        if (rc == 0)
            *volts = r.voltage;
    }

    return rc;
}

/* See description in tapi_power.h */
te_errno
tapi_power_command(tapi_power_instrument *inst, const char *command)
{
    return tapi_power_scpi_send(&inst->scpi, command);
}

/* See description in tapi_power.h */
te_errno
tapi_power_query(tapi_power_instrument *inst, const char *query,
                 te_string *response)
{
    return tapi_power_scpi_query(&inst->scpi, query, response);
}
