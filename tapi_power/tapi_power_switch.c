/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Controlling power through programmable switches
 *
 * Turning an outlet on, off and cycling it, over SNMP PDUs, HTTP relays
 * and switchable USB hubs, driven by a tool on the agent.
 */

#define TE_LGR_USER "TAPI POWER SWITCH"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_str.h"
#include "te_string.h"
#include "te_sleep.h"

#include "tapi_power_switch.h"
#include "tapi_power_internal.h"

/** Which action a command performs. */
typedef enum switch_action { SW_ON, SW_OFF, SW_STATUS } switch_action;

/** Per-kind commands and how to read status. */
typedef struct switch_backend {
    tapi_power_switch_kind kind;
    const char *name;
    const char *tool;
    /** @c true for the SNMP kinds (status is a numeric compare). */
    bool snmp;
    /** SNMP control/status OID bases for a fixed brand, or @c NULL. */
    const char *ctrl_oid;
    const char *status_oid;
    /** Default SNMP on/off integers. */
    int on_value;
    int off_value;
    /** HTTP/USB command templates (env: HOST OUTLET USER PASS ...). */
    const char *cmd_on;
    const char *cmd_off;
    const char *cmd_status;
    /** Substrings in the status output that mean on / off. */
    const char *on_ind;
    const char *off_ind;
} switch_backend;

static const switch_backend backends[] = {
    {
        .kind = TAPI_POWER_SWITCH_SNMP_APC, .name = "APC PDU", .tool = "snmpset",
        .snmp = true,
        .ctrl_oid = ".1.3.6.1.4.1.318.1.1.12.3.3.1.1.4",
        .status_oid = ".1.3.6.1.4.1.318.1.1.12.3.5.1.1.4",
        .on_value = 1, .off_value = 2,
    },
    {
        .kind = TAPI_POWER_SWITCH_SNMP, .name = "SNMP PDU", .tool = "snmpset",
        .snmp = true, .on_value = 1, .off_value = 2,
        /* ctrl_oid/status_oid come from the switch descriptor. */
    },
    {
        .kind = TAPI_POWER_SWITCH_DLI, .name = "Digital Loggers",
        .tool = "curl",
        .cmd_on  = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/outlet?$OUTLET=ON\"",
        .cmd_off = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/outlet?$OUTLET=OFF\"",
        .cmd_status = "curl -fsS --anyauth ${USER:+-u \"$USER:$PASS\"} "
                      "\"http://$HOST/restapi/relay/outlets/$((OUTLET-1))"
                      "/state/\"",
        .on_ind = "true", .off_ind = "false",
    },
    {
        .kind = TAPI_POWER_SWITCH_NETIO, .name = "NETIO", .tool = "curl",
        .cmd_on  = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/netio.json\" "
                   "-d '{\"Outputs\":[{\"ID\":'\"$OUTLET\"',\"Action\":1}]}'",
        .cmd_off = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/netio.json\" "
                   "-d '{\"Outputs\":[{\"ID\":'\"$OUTLET\"',\"Action\":0}]}'",
        /* Per-outlet state is not cheaply matched from the JSON; leave it. */
        .cmd_status = NULL,
    },
    {
        .kind = TAPI_POWER_SWITCH_TASMOTA, .name = "Tasmota", .tool = "curl",
        .cmd_on  = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/cm?cmnd=Power$OUTLET%20ON\"",
        .cmd_off = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/cm?cmnd=Power$OUTLET%20OFF\"",
        .cmd_status = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                      "\"http://$HOST/cm?cmnd=Power$OUTLET\"",
        .on_ind = "\"ON\"", .off_ind = "\"OFF\"",
    },
    {
        .kind = TAPI_POWER_SWITCH_SHELLY, .name = "Shelly", .tool = "curl",
        .cmd_on  = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/relay/$((OUTLET-1))?turn=on\"",
        .cmd_off = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                   "\"http://$HOST/relay/$((OUTLET-1))?turn=off\"",
        .cmd_status = "curl -fsS ${USER:+-u \"$USER:$PASS\"} "
                      "\"http://$HOST/relay/$((OUTLET-1))\"",
        .on_ind = "\"ison\":true", .off_ind = "\"ison\":false",
    },
    {
        .kind = TAPI_POWER_SWITCH_UHUBCTL, .name = "USB hub (uhubctl)",
        .tool = "uhubctl",
        .cmd_on  = "uhubctl -a on -l \"$HOST\" -p \"$OUTLET\"",
        .cmd_off = "uhubctl -a off -l \"$HOST\" -p \"$OUTLET\"",
        .cmd_status = "uhubctl -l \"$HOST\" -p \"$OUTLET\"",
        .on_ind = " power", .off_ind = " off",
    },
};

/* See description in tapi_power_switch.h */
const char *
tapi_power_switch_kind2str(tapi_power_switch_kind kind)
{
    size_t i;

    if (kind == TAPI_POWER_SWITCH_GENERIC)
        return "generic";
    for (i = 0; i < TE_ARRAY_LEN(backends); i++)
    {
        if (backends[i].kind == kind)
            return backends[i].name;
    }

    return "unknown";
}

/* See description in tapi_power_switch.h */
const char *
tapi_power_switch_state2str(tapi_power_switch_state state)
{
    switch (state)
    {
        case TAPI_POWER_SWITCH_ON:  return "on";
        case TAPI_POWER_SWITCH_OFF: return "off";
        case TAPI_POWER_SWITCH_UNKNOWN: return "unknown";
    }

    return "unknown";
}

/** Get the backend for a kind, or @c NULL for GENERIC. */
static const switch_backend *
backend_of(tapi_power_switch_kind kind)
{
    size_t i;

    for (i = 0; i < TE_ARRAY_LEN(backends); i++)
    {
        if (backends[i].kind == kind)
            return &backends[i];
    }

    return NULL;
}

/** Build an SNMP command line for an action. */
static char *
snmp_command(const switch_backend *b, const tapi_power_switch *sw,
             switch_action action)
{
    const char *ctrl = (b->ctrl_oid != NULL) ? b->ctrl_oid : sw->control_oid;
    const char *stat = (b->status_oid != NULL) ? b->status_oid :
                       sw->status_oid;
    bool custom = (sw->snmp_on_value != 0 || sw->snmp_off_value != 0);
    int on_v = custom ? sw->snmp_on_value : b->on_value;
    int off_v = custom ? sw->snmp_off_value : b->off_value;

    if (action == SW_STATUS)
    {
        if (stat == NULL)
            return NULL;
        return te_string_fmt(
            "snmpget -v1 -c \"$COMMUNITY\" -Oqv \"$HOST\" \"%s.$OUTLET\"",
            stat);
    }

    if (ctrl == NULL)
        return NULL;

    return te_string_fmt(
        "snmpset -v1 -c \"$COMMUNITY\" \"$HOST\" \"%s.$OUTLET\" i %d", ctrl,
        action == SW_ON ? on_v : off_v);
}

/** The command template for an action, SNMP or otherwise. */
static char *
command_for(const switch_backend *b, const tapi_power_switch *sw,
            switch_action action)
{
    /* An explicit override on the switch wins. */
    if (action == SW_ON && sw->cmd_on != NULL)
        return TE_STRDUP(sw->cmd_on);
    if (action == SW_OFF && sw->cmd_off != NULL)
        return TE_STRDUP(sw->cmd_off);
    if (action == SW_STATUS && sw->cmd_status != NULL)
        return TE_STRDUP(sw->cmd_status);

    if (b == NULL)
        return NULL;

    if (b->snmp)
        return snmp_command(b, sw, action);

    switch (action)
    {
        case SW_ON:     return b->cmd_on != NULL ? TE_STRDUP(b->cmd_on) : NULL;
        case SW_OFF:    return b->cmd_off != NULL ? TE_STRDUP(b->cmd_off) : NULL;
        case SW_STATUS: return b->cmd_status != NULL ?
                               TE_STRDUP(b->cmd_status) : NULL;
    }

    return NULL;
}

/** Run one switch command with the descriptor's values in the environment. */
static te_errno
switch_run(tapi_job_factory_t *factory, const tapi_power_switch *sw,
           const char *command, te_string *out, int *status)
{
    te_string script = TE_STRING_INIT;
    char outlet[16];
    const char *args[6];
    te_errno rc;

    snprintf(outlet, sizeof(outlet), "%u", sw->outlet);

    te_string_append(&script,
        "HOST=\"$1\"; OUTLET=\"$2\"; USER=\"$3\"; PASS=\"$4\"; "
        "COMMUNITY=\"$5\"; export HOST OUTLET USER PASS COMMUNITY; %s",
        command);

    args[0] = (sw->host != NULL) ? sw->host : "";
    args[1] = outlet;
    args[2] = (sw->user != NULL) ? sw->user : "";
    args[3] = (sw->password != NULL) ? sw->password : "";
    args[4] = (sw->snmp_community != NULL) ? sw->snmp_community : "";

    rc = tapi_power_agent_sh(factory, "switch", script.ptr, args, 5,
                             TAPI_POWER_SWITCH_TIMEOUT_MS, out, status);
    te_string_free(&script);

    return rc;
}

/** Run an on/off action and check the tool succeeded. */
static te_errno
switch_do(tapi_job_factory_t *factory, const tapi_power_switch *sw,
          switch_action action)
{
    const switch_backend *b = backend_of(sw->kind);
    char *cmd = command_for(b, sw, action);
    te_string out = TE_STRING_INIT;
    int status;
    te_errno rc;

    if (cmd == NULL)
    {
        ERROR("No %s command for switch kind %s",
              action == SW_ON ? "on" : "off",
              tapi_power_switch_kind2str(sw->kind));
        return TE_RC(TE_TAPI, TE_EINVAL);
    }

    rc = switch_run(factory, sw, cmd, &out, &status);
    if (rc == 0 && status != 0)
    {
        ERROR("Switch %s command failed (exit %d): %s",
              action == SW_ON ? "on" : "off", status,
              out.ptr != NULL ? out.ptr : "");
        rc = TE_RC(TE_TAPI, TE_EFAIL);
    }

    free(cmd);
    te_string_free(&out);

    return rc;
}

/* See description in tapi_power_switch.h */
te_errno
tapi_power_switch_on(tapi_job_factory_t *factory, const tapi_power_switch *sw)
{
    RING("Switch %s outlet %u: on", tapi_power_switch_kind2str(sw->kind),
         sw->outlet);
    return switch_do(factory, sw, SW_ON);
}

/* See description in tapi_power_switch.h */
te_errno
tapi_power_switch_off(tapi_job_factory_t *factory, const tapi_power_switch *sw)
{
    RING("Switch %s outlet %u: off", tapi_power_switch_kind2str(sw->kind),
         sw->outlet);
    return switch_do(factory, sw, SW_OFF);
}

/* See description in tapi_power_switch.h */
te_errno
tapi_power_switch_cycle(tapi_job_factory_t *factory,
                        const tapi_power_switch *sw, unsigned int off_ms)
{
    te_errno rc;

    if (off_ms == 0)
        off_ms = TAPI_POWER_SWITCH_CYCLE_MS;

    rc = tapi_power_switch_off(factory, sw);
    if (rc != 0)
        return rc;

    te_motivated_msleep(off_ms, "keep the device under test powered off");

    return tapi_power_switch_on(factory, sw);
}

/* See description in tapi_power_switch.h */
te_errno
tapi_power_switch_get(tapi_job_factory_t *factory, const tapi_power_switch *sw,
                      tapi_power_switch_state *state)
{
    const switch_backend *b = backend_of(sw->kind);
    char *cmd = command_for(b, sw, SW_STATUS);
    te_string out = TE_STRING_INIT;
    int status;
    te_errno rc;

    *state = TAPI_POWER_SWITCH_UNKNOWN;

    if (cmd == NULL)
    {
        RING("Switch kind %s does not report state",
             tapi_power_switch_kind2str(sw->kind));
        return 0;
    }

    rc = switch_run(factory, sw, cmd, &out, &status);
    if (rc != 0)
        goto out;
    if (status != 0)
    {
        ERROR("Switch status command failed (exit %d): %s", status,
              out.ptr != NULL ? out.ptr : "");
        rc = TE_RC(TE_TAPI, TE_EFAIL);
        goto out;
    }

    if (out.ptr == NULL)
        goto out;

    if (b != NULL && b->snmp)
    {
        /* snmpget -Oqv prints the integer value. */
        bool custom = (sw->snmp_on_value != 0 || sw->snmp_off_value != 0);
        int on_v = custom ? sw->snmp_on_value : b->on_value;
        int off_v = custom ? sw->snmp_off_value : b->off_value;
        int v = (int)strtol(out.ptr, NULL, 10);

        if (v == on_v)
            *state = TAPI_POWER_SWITCH_ON;
        else if (v == off_v)
            *state = TAPI_POWER_SWITCH_OFF;
    }
    else if (b != NULL)
    {
        /* Order matters: check the off indicator first, as "off" can be a
         * substring context of other words less often than "on". */
        if (b->off_ind != NULL && strstr(out.ptr, b->off_ind) != NULL)
            *state = TAPI_POWER_SWITCH_OFF;
        else if (b->on_ind != NULL && strstr(out.ptr, b->on_ind) != NULL)
            *state = TAPI_POWER_SWITCH_ON;
    }

out:
    free(cmd);
    te_string_free(&out);

    return rc;
}

/* See description in tapi_power_switch.h */
te_errno
tapi_power_switch_set_checked(tapi_job_factory_t *factory,
                             const tapi_power_switch *sw, bool on)
{
    tapi_power_switch_state want = on ? TAPI_POWER_SWITCH_ON :
                                        TAPI_POWER_SWITCH_OFF;
    tapi_power_switch_state got;
    te_errno rc;

    rc = on ? tapi_power_switch_on(factory, sw) :
              tapi_power_switch_off(factory, sw);
    if (rc != 0)
        return rc;

    rc = tapi_power_switch_get(factory, sw, &got);
    if (rc != 0)
        return rc;

    if (got == TAPI_POWER_SWITCH_UNKNOWN)
    {
        /* The switch does not report state; the command succeeded, trust it. */
        return 0;
    }
    if (got != want)
    {
        ERROR("Outlet did not reach %s: it reads %s",
              tapi_power_switch_state2str(want),
              tapi_power_switch_state2str(got));
        return TE_RC(TE_TAPI, TE_EFAIL);
    }

    return 0;
}

/* See description in tapi_power_switch.h */
te_errno
tapi_power_switch_available(tapi_job_factory_t *factory,
                            const tapi_power_switch *sw, bool *present)
{
    const switch_backend *b = backend_of(sw->kind);

    if (b == NULL)
    {
        *present = (sw->cmd_on != NULL && sw->cmd_off != NULL);
        return 0;
    }

    return tapi_power_agent_have(factory, b->tool, present);
}
