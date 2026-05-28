/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Controlling power through programmable switches
 *
 * @defgroup tapi_power_switch Programmable power switches (tapi_power_switch)
 * @ingroup tapi_power
 * @{
 *
 * Turning the power to a device under test on and off, and cycling it, with
 * a programmable outlet - a smart PDU, a web relay, or a switchable USB
 * hub. This is how a test recovers a device that has wedged, measures cold
 * boot, or proves a device comes back after power is pulled: the outlet is
 * the one thing that always answers even when the device does not.
 *
 * The switches driven:
 *
 * | Kind | What it is | Tool on the agent |
 * |---|---|---|
 * | #TAPI_POWER_SWITCH_SNMP_APC | APC / PowerNet rack PDU | @c snmpset / @c snmpget |
 * | #TAPI_POWER_SWITCH_SNMP | any PDU with control and status OIDs you give | @c snmpset / @c snmpget |
 * | #TAPI_POWER_SWITCH_DLI | Digital Loggers Web Power Switch | @c curl |
 * | #TAPI_POWER_SWITCH_NETIO | NETIO PowerBox / PowerCable | @c curl |
 * | #TAPI_POWER_SWITCH_TASMOTA | Tasmota relay (Sonoff and the like) | @c curl |
 * | #TAPI_POWER_SWITCH_SHELLY | Shelly relay | @c curl |
 * | #TAPI_POWER_SWITCH_UHUBCTL | per-port power of a USB hub | @c uhubctl |
 * | #TAPI_POWER_SWITCH_GENERIC | any switch you give commands for | (yours) |
 *
 * The switch is controlled by a tool on the agent it is reachable from, so
 * this uses the agent - the outlet is on the lab network or on the agent's
 * USB.
 *
 * @code
 * tapi_power_switch sw = {
 *     .kind = TAPI_POWER_SWITCH_SNMP_APC, .host = "192.0.2.20",
 *     .outlet = 3, .snmp_community = "private",
 * };
 *
 * CHECK_RC(tapi_power_switch_cycle(factory, &sw, 5000));  // off, 5 s, on
 * @endcode
 *
 * @note Cutting power is destructive to whatever the device was doing.
 *       The outlet controlled is the one the suite's own configuration
 *       names; that is the engagement it belongs to.
 */

#ifndef __TSF_TAPI_POWER_SWITCH_H__
#define __TSF_TAPI_POWER_SWITCH_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "tapi_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default timeout of a switch operation, ms. */
#define TAPI_POWER_SWITCH_TIMEOUT_MS    30000

/** Default off-time of a power cycle, ms. */
#define TAPI_POWER_SWITCH_CYCLE_MS      5000

/** A kind of programmable power switch. */
typedef enum tapi_power_switch_kind {
    /** A switch you supply the on/off/status commands for. */
    TAPI_POWER_SWITCH_GENERIC = 0,
    /** APC / PowerNet rack PDU, over SNMP. */
    TAPI_POWER_SWITCH_SNMP_APC,
    /** Any PDU over SNMP, with the control and status OIDs supplied. */
    TAPI_POWER_SWITCH_SNMP,
    /** Digital Loggers Web Power Switch, over HTTP. */
    TAPI_POWER_SWITCH_DLI,
    /** NETIO PowerBox / PowerCable, over HTTP. */
    TAPI_POWER_SWITCH_NETIO,
    /** Tasmota relay, over HTTP. */
    TAPI_POWER_SWITCH_TASMOTA,
    /** Shelly relay, over HTTP. */
    TAPI_POWER_SWITCH_SHELLY,
    /** Per-port power of a USB hub, over @c uhubctl. */
    TAPI_POWER_SWITCH_UHUBCTL,
} tapi_power_switch_kind;

/**
 * The name of a switch kind.
 *
 * @param kind      Kind.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_power_switch_kind2str(tapi_power_switch_kind kind);

/** The state of an outlet. */
typedef enum tapi_power_switch_state {
    /** Could not be determined. */
    TAPI_POWER_SWITCH_UNKNOWN = 0,
    /** Powered on. */
    TAPI_POWER_SWITCH_ON,
    /** Powered off. */
    TAPI_POWER_SWITCH_OFF,
} tapi_power_switch_state;

/**
 * Spell out a state.
 *
 * @param state     State.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_power_switch_state2str(tapi_power_switch_state state);

/** A programmable power switch and how it is reached. */
typedef struct tapi_power_switch {
    /** Kind, which selects the control commands. */
    tapi_power_switch_kind kind;
    /**
     * Host or address of the switch; for #TAPI_POWER_SWITCH_UHUBCTL, the
     * hub location (@c uhubctl @c -l value). Passed to commands as @c $HOST.
     */
    const char *host;
    /** Outlet or USB port number (1-based). Passed as @c $OUTLET. */
    unsigned int outlet;
    /** Username for HTTP switches (may be @c NULL). @c $USER. */
    const char *user;
    /** Password for HTTP switches (may be @c NULL). @c $PASS. */
    const char *password;
    /** SNMP community for SNMP switches (may be @c NULL). @c $COMMUNITY. */
    const char *snmp_community;
    /**
     * For #TAPI_POWER_SWITCH_SNMP: the base control OID; the outlet number
     * is appended. Set to on/off values by tapi_power_switch_*. @c $OID.
     */
    const char *control_oid;
    /** For #TAPI_POWER_SWITCH_SNMP: the base status OID. @c $STATUS_OID. */
    const char *status_oid;
    /**
     * For an SNMP switch: the integer written to the control OID for on and
     * for off, and read back from the status OID. Left both zero (the
     * default) to use the kind's values (1 = on, 2 = off, as APC does);
     * set either to use these instead, so on = 1 / off = 0 is expressed by
     * setting @a snmp_on_value to 1 and leaving @a snmp_off_value 0.
     */
    int snmp_on_value;
    int snmp_off_value;
    /** Override on command (required for GENERIC). */
    const char *cmd_on;
    /** Override off command. */
    const char *cmd_off;
    /** Override status command; its output is matched for on/off. */
    const char *cmd_status;
} tapi_power_switch;

/**
 * Check whether the tool for a switch kind is on the agent.
 *
 * @param[in]  factory  Job factory.
 * @param[in]  sw       Switch.
 * @param[out] present  Where to save the answer.
 *
 * @return Status code.
 */
extern te_errno tapi_power_switch_available(tapi_job_factory_t *factory,
                                            const tapi_power_switch *sw,
                                            bool *present);

/**
 * Turn the outlet on.
 *
 * @param factory   Job factory.
 * @param sw        Switch.
 *
 * @return Status code.
 */
extern te_errno tapi_power_switch_on(tapi_job_factory_t *factory,
                                     const tapi_power_switch *sw);

/**
 * Turn the outlet off.
 *
 * @param factory   Job factory.
 * @param sw        Switch.
 *
 * @return Status code.
 */
extern te_errno tapi_power_switch_off(tapi_job_factory_t *factory,
                                      const tapi_power_switch *sw);

/**
 * Power-cycle the outlet: turn it off, wait, turn it on. The wait is done
 * on the engine, so the off-time does not depend on the switch's own
 * cycle timer.
 *
 * @param factory   Job factory.
 * @param sw        Switch.
 * @param off_ms    How long to stay off, ms; 0 for the default.
 *
 * @return Status code.
 */
extern te_errno tapi_power_switch_cycle(tapi_job_factory_t *factory,
                                        const tapi_power_switch *sw,
                                        unsigned int off_ms);

/**
 * Read the outlet's state.
 *
 * @param[in]  factory  Job factory.
 * @param[in]  sw       Switch.
 * @param[out] state    Where to save the state.
 *
 * @return Status code.
 */
extern te_errno tapi_power_switch_get(tapi_job_factory_t *factory,
                                      const tapi_power_switch *sw,
                                      tapi_power_switch_state *state);

/**
 * Set the outlet to a state (on or off) and confirm it took, by reading
 * the state back where the switch supports it.
 *
 * @param factory   Job factory.
 * @param sw        Switch.
 * @param on        @c true for on, @c false for off.
 *
 * @return Status code.
 * @retval TE_EFAIL The state read back does not match what was set.
 */
extern te_errno tapi_power_switch_set_checked(tapi_job_factory_t *factory,
                                              const tapi_power_switch *sw,
                                              bool on);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_POWER_SWITCH_H__ */

/**@} <!-- END tapi_power_switch --> */
