/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Power and energy measurement from a Test Agent
 *
 * @defgroup tapi_power Power and energy measurement (tapi_power)
 * @{
 *
 * Measuring what a device under test draws, with a bench instrument on
 * the lab network: the voltage across it, the current into it, the power
 * now, and the energy over a test window. The instruments differ by
 * brand, but they answer the same questions, so a test asks them the same
 * way and the brand is a field.
 *
 * The brands driven, all over SCPI on a raw TCP socket:
 *
 * | Brand | Typical instruments |
 * |---|---|
 * | #TAPI_POWER_KEYSIGHT | N6705 source/analyzer, 34461A DMM, N7900 |
 * | #TAPI_POWER_ROHDE_SCHWARZ | NGM/NGU source-measure, HMC8015 analyzer |
 * | #TAPI_POWER_YOKOGAWA | WT310/WT500/WT3000 power analyzers |
 * | #TAPI_POWER_KEITHLEY | DMM6500, 2280S supply, 2450 SMU (Tektronix) |
 * | #TAPI_POWER_RIGOL | DP800 supplies, DM3068 DMM |
 * | #TAPI_POWER_CHROMA | 66200 power meters |
 * | #TAPI_POWER_GENERIC | any instrument that answers the SCPI MEASure tree |
 *
 * @code
 * tapi_power_instrument inst = {
 *     .brand = TAPI_POWER_KEYSIGHT, .host = "192.0.2.50", .channel = 1,
 * };
 * tapi_power_reading r;
 *
 * CHECK_RC(tapi_power_open(&inst));
 * CHECK_RC(tapi_power_measure(&inst, &r));
 * RING("DUT draws %.3f W (%.3f V, %.3f A)", r.power, r.voltage, r.current);
 * tapi_power_close(&inst);
 * @endcode
 *
 * For the energy a test consumes over its run - the headline number for a
 * battery or an efficiency test - see @ref tapi_power_energy.
 *
 * @note The connection is opened from the host running the library, which
 *       is on the bench network with the instrument. No agent tool is
 *       needed; the instrument is reached over the network directly.
 */

#ifndef __TSF_TAPI_POWER_H__
#define __TSF_TAPI_POWER_H__

#include <stdint.h>

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"

#include "tapi_power_scpi.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A brand of measurement instrument. */
typedef enum tapi_power_brand {
    /** Any instrument answering the standard SCPI MEASure tree. */
    TAPI_POWER_GENERIC = 0,
    /** Keysight (Agilent): N6705, 34400-series DMM, N7900. */
    TAPI_POWER_KEYSIGHT,
    /** Rohde & Schwarz: NGM/NGU source-measure, HMC8015 analyzer. */
    TAPI_POWER_ROHDE_SCHWARZ,
    /** Yokogawa: WT-series power analyzers. */
    TAPI_POWER_YOKOGAWA,
    /** Keithley / Tektronix: DMM6500, 2280S, 2450 SMU. */
    TAPI_POWER_KEITHLEY,
    /** Rigol: DP800 supplies, DM-series DMM. */
    TAPI_POWER_RIGOL,
    /** Chroma: power meters. */
    TAPI_POWER_CHROMA,
} tapi_power_brand;

/**
 * The name of a brand.
 *
 * @param brand     Brand.
 *
 * @return A static string, never @c NULL.
 */
extern const char *tapi_power_brand2str(tapi_power_brand brand);

/** An instrument and how it is reached. */
typedef struct tapi_power_instrument {
    /** Brand, which selects the SCPI command set. */
    tapi_power_brand brand;
    /** Host or address on the bench network. */
    const char *host;
    /** TCP port, or 0 for the SCPI default (5025). */
    uint16_t port;
    /**
     * Measurement channel (1-based), for multi-channel instruments; 0 or 1
     * for a single-channel one.
     */
    unsigned int channel;
    /** Exchange timeout, ms; 0 for the default. */
    int timeout_ms;
    /** The open connection; filled by tapi_power_open(). */
    tapi_power_scpi scpi;
    /** The instrument's @c *IDN? string; filled by tapi_power_open(). */
    char idn[256];
} tapi_power_instrument;

/** One instantaneous reading. */
typedef struct tapi_power_reading {
    /** Voltage, volts. NAN if the instrument did not report it. */
    double voltage;
    /** Current, amperes. NAN if not reported. */
    double current;
    /** Power, watts. Measured if the instrument reports it, else V*I. */
    double power;
} tapi_power_reading;

/**
 * Open a connection to an instrument and read its identity (@c *IDN?).
 *
 * @param[in,out] inst   Instrument; its @a scpi and @a idn are filled.
 *
 * @return Status code.
 */
extern te_errno tapi_power_open(tapi_power_instrument *inst);

/**
 * Close the connection to an instrument.
 *
 * @param inst      Instrument.
 */
extern void tapi_power_close(tapi_power_instrument *inst);

/**
 * Measure voltage, current and power once.
 *
 * @param[in]  inst     Instrument (open).
 * @param[out] reading  Where to save the reading.
 *
 * @return Status code.
 */
extern te_errno tapi_power_measure(tapi_power_instrument *inst,
                                   tapi_power_reading *reading);

/**
 * Measure power alone, watts.
 *
 * @param[in]  inst     Instrument (open).
 * @param[out] watts    Where to save the power.
 *
 * @return Status code.
 */
extern te_errno tapi_power_measure_power(tapi_power_instrument *inst,
                                         double *watts);

/**
 * Measure current alone, amperes.
 *
 * @param[in]  inst     Instrument (open).
 * @param[out] amps     Where to save the current.
 *
 * @return Status code.
 */
extern te_errno tapi_power_measure_current(tapi_power_instrument *inst,
                                           double *amps);

/**
 * Measure voltage alone, volts.
 *
 * @param[in]  inst     Instrument (open).
 * @param[out] volts    Where to save the voltage.
 *
 * @return Status code.
 */
extern te_errno tapi_power_measure_voltage(tapi_power_instrument *inst,
                                           double *volts);

/**
 * Send a raw SCPI command to the instrument, for anything this library
 * does not wrap (ranging, averaging, output control).
 *
 * @param inst      Instrument (open).
 * @param command   SCPI command, without a trailing newline.
 *
 * @return Status code.
 */
extern te_errno tapi_power_command(tapi_power_instrument *inst,
                                   const char *command);

/**
 * Send a raw SCPI query and read the response.
 *
 * @param[in]  inst     Instrument (open).
 * @param[in]  query    SCPI query.
 * @param[out] response String to append the response to.
 *
 * @return Status code.
 */
extern te_errno tapi_power_query(tapi_power_instrument *inst,
                                 const char *query, te_string *response);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_POWER_H__ */

/**@} <!-- END tapi_power --> */
