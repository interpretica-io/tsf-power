/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief SCPI over a raw TCP socket
 *
 * @defgroup tapi_power_scpi SCPI transport (tapi_power_scpi)
 * @ingroup tapi_power
 * @{
 *
 * Almost every bench power instrument speaks SCPI (Standard Commands for
 * Programmable Instruments) over a raw TCP socket - port 5025 by the
 * LXI convention - with commands and responses terminated by a newline.
 * This is that transport: open a connection, send a command, send a query
 * and read the one-line answer. The brands differ in which SCPI commands
 * they understand, not in how the bytes move, so everything above this is
 * command strings.
 *
 * The connection is opened from the host running the library (typically
 * the engine, which is on the lab bench network with the instruments).
 *
 * @note This is a line transport for the query/response instruments this
 *       library drives; it is not a full VXI-11 or USBTMC stack.
 */

#ifndef __TSF_TAPI_POWER_SCPI_H__
#define __TSF_TAPI_POWER_SCPI_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Default SCPI-over-TCP port (LXI raw socket). */
#define TAPI_POWER_SCPI_PORT        5025

/** Default timeout of a SCPI exchange, ms. */
#define TAPI_POWER_SCPI_TIMEOUT_MS  5000

/** An open SCPI connection. */
typedef struct tapi_power_scpi {
    /** Socket file descriptor, or -1. */
    int fd;
    /** Exchange timeout, ms. */
    int timeout_ms;
} tapi_power_scpi;

/**
 * Open a SCPI connection to an instrument.
 *
 * @param[in]  host     Instrument host or address.
 * @param[in]  port     TCP port, or 0 for #TAPI_POWER_SCPI_PORT.
 * @param[in]  timeout_ms   Exchange timeout, ms; 0 for the default.
 * @param[out] scpi     Connection; close with tapi_power_scpi_close().
 *
 * @return Status code.
 */
extern te_errno tapi_power_scpi_open(const char *host, uint16_t port,
                                     int timeout_ms, tapi_power_scpi *scpi);

/**
 * Send a command that expects no response (e.g. @c "*RST").
 *
 * @param scpi      Connection.
 * @param command   SCPI command, without the trailing newline.
 *
 * @return Status code.
 */
extern te_errno tapi_power_scpi_send(tapi_power_scpi *scpi,
                                     const char *command);

/**
 * Send a query and read its one-line response.
 *
 * @param[in]  scpi     Connection.
 * @param[in]  query    SCPI query, e.g. @c "MEAS:VOLT?".
 * @param[out] response String to append the response to, newline stripped.
 *
 * @return Status code.
 */
extern te_errno tapi_power_scpi_query(tapi_power_scpi *scpi, const char *query,
                                      te_string *response);

/**
 * Send a query and parse its response as a floating-point number, as SCPI
 * instruments return measurements.
 *
 * @param[in]  scpi     Connection.
 * @param[in]  query    SCPI query.
 * @param[out] value    Where to save the number.
 *
 * @return Status code.
 * @retval TE_EINVAL    The response is not a number.
 */
extern te_errno tapi_power_scpi_query_double(tapi_power_scpi *scpi,
                                             const char *query, double *value);

/**
 * Close a SCPI connection.
 *
 * @param scpi      Connection.
 */
extern void tapi_power_scpi_close(tapi_power_scpi *scpi);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_POWER_SCPI_H__ */

/**@} <!-- END tapi_power_scpi --> */
