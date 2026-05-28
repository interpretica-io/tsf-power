/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Embedded power profilers
 *
 * Driving Nordic PPK2, Joulescope, Qoitech Otii and Monsoon through their
 * vendor tools on the agent, and reading average current, voltage and
 * energy back from what they print.
 */

#define TE_LGR_USER "TAPI POWER PROFILER"

#include "te_config.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"

#include "tapi_power_profiler.h"
#include "tapi_power_internal.h"

/** Per-kind tool, command template and the labels to read from output. */
typedef struct profiler_backend {
    tapi_power_profiler_kind kind;
    const char *name;
    /** The tool binary, to check availability. */
    const char *tool;
    /**
     * Default command. $DEV, $DURATION (seconds) and $VMV (millivolts) are
     * exported into the environment before it runs, so the template refers
     * to them as shell variables.
     */
    const char *command;
    /** Label before the average current in the output. */
    const char *label_current;
    /** Label before the voltage (may be @c NULL). */
    const char *label_voltage;
    /** Label before the energy (may be @c NULL). */
    const char *label_energy;
    /** Label before the peak current (may be @c NULL). */
    const char *label_peak;
} profiler_backend;

static const profiler_backend backends[] = {
    {
        .kind = TAPI_POWER_PROFILER_NORDIC_PPK2, .name = "Nordic PPK2",
        .tool = "ppk2-api",
        .command = "ppk2-api --serial \"$DEV\" --source \"$VMV\" "
                   "--measure \"$DURATION\"",
        .label_current = "average current",
        .label_voltage = "voltage",
        .label_energy = "energy",
        .label_peak = "max current",
    },
    {
        .kind = TAPI_POWER_PROFILER_JOULESCOPE, .name = "Joulescope",
        .tool = "joulescope",
        .command = "joulescope statistics --duration \"$DURATION\"",
        /* joulescope prints "current", "voltage", "power", "energy". */
        .label_current = "current",
        .label_voltage = "voltage",
        .label_energy = "energy",
        .label_peak = NULL,
    },
    {
        .kind = TAPI_POWER_PROFILER_QOITECH_OTII, .name = "Qoitech Otii",
        .tool = "otii",
        .command = "otii measure --device \"$DEV\" --voltage-mv \"$VMV\" "
                   "--duration \"$DURATION\"",
        .label_current = "average current",
        .label_voltage = "main voltage",
        .label_energy = "energy",
        .label_peak = "max current",
    },
    {
        .kind = TAPI_POWER_PROFILER_MONSOON, .name = "Monsoon",
        .tool = "monsoon",
        .command = "monsoon --serialno \"$DEV\" --voltage "
                   "\"$(awk \"BEGIN{print $VMV/1000}\")\" "
                   "--samples \"$DURATION\" --timestamp",
        .label_current = "average current",
        .label_voltage = "voltage",
        .label_energy = "energy",
        .label_peak = "max current",
    },
};

/* See description in tapi_power_profiler.h */
const char *
tapi_power_profiler_kind2str(tapi_power_profiler_kind kind)
{
    size_t i;

    if (kind == TAPI_POWER_PROFILER_GENERIC)
        return "generic";
    for (i = 0; i < TE_ARRAY_LEN(backends); i++)
    {
        if (backends[i].kind == kind)
            return backends[i].name;
    }

    return "unknown";
}

/** Get the backend for a kind, or @c NULL for GENERIC. */
static const profiler_backend *
backend_of(tapi_power_profiler_kind kind)
{
    size_t i;

    for (i = 0; i < TE_ARRAY_LEN(backends); i++)
    {
        if (backends[i].kind == kind)
            return &backends[i];
    }

    return NULL;
}

/** Multiply for an SI prefix character; 1.0 for none/unknown. */
static double
si_prefix(char c)
{
    switch (c)
    {
        case 'p': return 1e-12;
        case 'n': return 1e-9;
        case 'u': return 1e-6;   /* ASCII micro */
        case 'm': return 1e-3;
        case 'k': case 'K': return 1e3;
        case 'M': return 1e6;
        default:  return 1.0;
    }
}

/* See description in tapi_power_internal.h */
bool
tapi_power_parse_quantity(const char *text, const char *label, double *value)
{
    const char *p = text;
    const char *num;
    char *end = NULL;
    double v;
    double mult = 1.0;
    bool wh = false;

    if (label != NULL)
    {
        /* Case-insensitive search for the label. */
        size_t llen = strlen(label);

        for (; *p != '\0'; p++)
        {
            if (strncasecmp(p, label, llen) == 0)
            {
                p += llen;
                break;
            }
        }
        if (*p == '\0')
            return false;
    }

    /* Skip to the next number (optionally signed, with a decimal point). */
    while (*p != '\0' && !(isdigit((unsigned char)*p) ||
           ((*p == '-' || *p == '+' || *p == '.') &&
            isdigit((unsigned char)p[1]))))
        p++;
    if (*p == '\0')
        return false;

    num = p;
    v = strtod(num, &end);
    if (end == num)
        return false;

    /* Skip spaces, then read an optional prefix + unit. */
    while (*end == ' ' || *end == '\t')
        end++;

    /* Handle the UTF-8 micro sign (0xC2 0xB5) and Greek mu (0xCE 0xBC). */
    if ((unsigned char)end[0] == 0xC2 && (unsigned char)end[1] == 0xB5)
    {
        mult = 1e-6;
        end += 2;
    }
    else if ((unsigned char)end[0] == 0xCE && (unsigned char)end[1] == 0xBC)
    {
        mult = 1e-6;
        end += 2;
    }
    else if (*end != '\0' && strchr("pnumkKM", *end) != NULL)
    {
        /* A prefix only if a base unit or end follows (not e.g. "min"). */
        char base = end[1];

        if (base == 'A' || base == 'V' || base == 'W' || base == 'J' ||
            base == 'C' || base == 'h' || base == '\0' || base == ' ' ||
            base == '\n' || base == '\r')
        {
            mult = si_prefix(*end);
            end++;
        }
    }

    /* Wh -> J conversion (3600 J per Wh); "mWh" already took the m prefix. */
    if ((end[0] == 'W' && end[1] == 'h') || (end[0] == 'h'))
        wh = true;

    *value = v * mult;
    if (wh)
        *value *= 3600.0;

    return true;
}

/* See description in tapi_power_profiler.h */
te_errno
tapi_power_profiler_parse(tapi_power_profiler_kind kind, const char *text,
                          double seconds, tapi_power_profile *result)
{
    const profiler_backend *b = backend_of(kind);
    const char *lc = (b != NULL) ? b->label_current : "current";
    const char *lv = (b != NULL) ? b->label_voltage : "voltage";
    const char *le = (b != NULL) ? b->label_energy : "energy";
    const char *lp = (b != NULL) ? b->label_peak : NULL;
    bool have_current;
    bool have_power = false;
    double power = 0;

    memset(result, 0, sizeof(*result));
    result->seconds = seconds;

    have_current = tapi_power_parse_quantity(text, lc, &result->avg_current);
    if (lv != NULL)
        tapi_power_parse_quantity(text, lv, &result->avg_voltage);
    if (lp != NULL)
        tapi_power_parse_quantity(text, lp, &result->peak_current);
    have_power = tapi_power_parse_quantity(text, "power", &power);

    if (!have_current && !have_power)
    {
        ERROR("No current or power figure in the profiler output:\n%s", text);
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    /* Average power: measured, or current times voltage. */
    if (have_power)
        result->avg_power = power;
    else if (result->avg_voltage > 0)
        result->avg_power = result->avg_current * result->avg_voltage;

    /* Energy: from the tool if it gave one, else power times time. */
    if (le != NULL && tapi_power_parse_quantity(text, le, &result->joules))
    {
        /* joules already in J (Wh converted by the quantity parser). */
    }
    else if (result->avg_power > 0 && seconds > 0)
    {
        result->joules = result->avg_power * seconds;
    }

    result->milliwatt_hours = result->joules / 3.6;

    return 0;
}

/* See description in tapi_power_profiler.h */
te_errno
tapi_power_profiler_available(tapi_job_factory_t *factory,
                             const tapi_power_profiler *prof, bool *present)
{
    const profiler_backend *b = backend_of(prof->kind);

    if (b == NULL)
    {
        /* GENERIC: the command is the caller's; assume it is there. */
        *present = (prof->command != NULL);
        return 0;
    }

    return tapi_power_agent_have(factory, b->tool, present);
}

/* See description in tapi_power_profiler.h */
te_errno
tapi_power_profiler_measure(tapi_job_factory_t *factory,
                            const tapi_power_profiler *prof,
                            unsigned int duration_ms,
                            tapi_power_profile *result)
{
    const profiler_backend *b = backend_of(prof->kind);
    const char *command = (prof->command != NULL) ? prof->command :
                          (b != NULL ? b->command : NULL);
    te_string script = TE_STRING_INIT;
    te_string out = TE_STRING_INIT;
    const char *args[3];
    double seconds = duration_ms / 1000.0;
    int timeout = duration_ms + TAPI_POWER_PROFILER_OVERHEAD_MS;
    int status;
    te_errno rc;

    if (command == NULL)
    {
        ERROR("No command for profiler kind %s",
              tapi_power_profiler_kind2str(prof->kind));
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    /*
     * Export DEV, DURATION and VMV as environment variables from positional
     * parameters (so nothing in them is interpreted), then run the template.
     */
    te_string_append(&script,
        "DEV=\"$1\"; DURATION=\"$2\"; VMV=\"$3\"; "
        "export DEV DURATION VMV; %s", command);

    args[0] = (prof->device != NULL) ? prof->device : "";
    {
        char dur[32];
        char vmv[32];

        snprintf(dur, sizeof(dur), "%.3f", seconds);
        snprintf(vmv, sizeof(vmv), "%u", prof->supply_mv);
        args[1] = dur;
        args[2] = vmv;

        rc = tapi_power_agent_sh(factory, "profiler", script.ptr, args, 3,
                                 timeout, &out, &status);
    }
    te_string_free(&script);

    if (rc == 0)
    {
        if (status != 0)
        {
            ERROR("Profiler tool failed (exit %d):\n%s", status,
                  out.ptr != NULL ? out.ptr : "");
            rc = TE_RC(TE_TAPI, TE_EFAIL);
        }
        else
        {
            rc = tapi_power_profiler_parse(prof->kind,
                                           out.ptr != NULL ? out.ptr : "",
                                           seconds, result);
        }
    }

    te_string_free(&out);

    return rc;
}

/* See description in tapi_power_profiler.h */
void
tapi_power_profile_log(const tapi_power_profile *result, const char *what)
{
    RING("Profile over %s: avg %.6g A (%.3f uA), %.4g V, %.6g W; "
         "peak %.6g A; energy %.4f J (%.4f mWh) in %.2f s",
         what != NULL ? what : "the window", result->avg_current,
         result->avg_current * 1e6, result->avg_voltage, result->avg_power,
         result->peak_current, result->joules, result->milliwatt_hours,
         result->seconds);
}
