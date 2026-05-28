/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Power TAPI: internal helpers for agent-side tools
 *
 * Internal to tsf-power; not installed.
 *
 * The SCPI part of this library talks to a bench instrument over the
 * network and needs no agent. The profiler part (@ref tapi_power_profiler)
 * drives a USB or serial power profiler through its vendor tool, which
 * runs on the agent the profiler is plugged into; that is what these
 * helpers are for, built on tsf-devtool.
 */

#ifndef __TSF_TAPI_POWER_INTERNAL_H__
#define __TSF_TAPI_POWER_INTERNAL_H__

#include "te_defs.h"
#include "te_errno.h"
#include "te_string.h"
#include "tapi_job.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run a shell command line on the agent, wait for it and capture its
 * output. The profiler tools are varied enough that a command line is the
 * honest interface; the value substitutions are done by the caller with
 * shell-safe positional parameters.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  name         Name for log messages.
 * @param[in]  script       Shell script text.
 * @param[in]  args         Positional parameters (may be @c NULL).
 * @param[in]  n_args       Number of @p args.
 * @param[in]  timeout_ms   Timeout, ms.
 * @param[out] out          String to append stdout to (may be @c NULL).
 * @param[out] status       Exit status, or @c -1 (may be @c NULL).
 *
 * @return Status code of running the command, not of the command.
 */
extern te_errno tapi_power_agent_sh(tapi_job_factory_t *factory,
                                    const char *name, const char *script,
                                    const char **args, size_t n_args,
                                    int timeout_ms, te_string *out,
                                    int *status);

/**
 * Check whether a program is on the agent, i.e. @c command @c -v.
 *
 * @param[in]  factory      Job factory.
 * @param[in]  program      Program name.
 * @param[out] present      Where to save the answer.
 *
 * @return Status code.
 */
extern te_errno tapi_power_agent_have(tapi_job_factory_t *factory,
                                      const char *program, bool *present);

/**
 * Parse a number with an optional SI prefix and unit out of text, e.g.
 * @c "12.3 uA", @c "1.5mA", @c "3.30 V", @c "42 mWh". The result is in
 * the base SI unit (A, V, W, J; Wh is converted to J, C is left as is).
 *
 * The search is for the first occurrence of @p label (case-insensitive),
 * after which the next number and its unit are read.
 *
 * @param[in]  text     Text to search.
 * @param[in]  label    Label to find the value after (may be @c NULL to
 *                      read from the start).
 * @param[out] value    Where to save the value in base SI units.
 *
 * @return @c true if a number was found.
 */
extern bool tapi_power_parse_quantity(const char *text, const char *label,
                                      double *value);

#ifdef __cplusplus
} /* extern "C" */
#endif
#endif /* !__TSF_TAPI_POWER_INTERNAL_H__ */
