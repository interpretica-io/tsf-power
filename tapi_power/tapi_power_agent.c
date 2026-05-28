/* SPDX-License-Identifier: MIT */
/* Copyright (C) 2026 Interpretica Unipessoal Lda */
/** @file
 * @brief Power TAPI: running vendor tools on the agent
 *
 * The agent-side runner the profiler part is built on, over tsf-devtool.
 */

#define TE_LGR_USER "TAPI POWER AGENT"

#include "te_config.h"

#include <stdlib.h>
#include <string.h>

#include "logger_api.h"
#include "te_alloc.h"
#include "te_string.h"
#include "tapi_job_opt.h"

#include "tapi_devtool_run.h"

#include "tapi_power_internal.h"

/** Arguments of a command, as a plain vector of strings. */
typedef struct power_cmd_opt {
    size_t n_args;
    const char **args;
} power_cmd_opt;

static const tapi_job_opt_bind power_cmd_binds[] = TAPI_JOB_OPT_SET(
    TAPI_JOB_OPT_ARRAY_PTR(power_cmd_opt, n_args, args,
        TAPI_JOB_OPT_CONTENT(TAPI_JOB_OPT_STRING, NULL, false))
);

/** Run a program on the agent and capture stdout+stderr. */
static te_errno
agent_run(tapi_job_factory_t *factory, const char *name, const char *program,
          const char **args, size_t n_args, int timeout_ms, te_string *out,
          int *status)
{
    power_cmd_opt opt = { .n_args = n_args, .args = args };
    tapi_devtool_run run = TAPI_DEVTOOL_RUN_INIT;
    tapi_devtool_output output;
    te_errno rc;

    rc = tapi_devtool_run_init(&run, factory, name, program, power_cmd_binds,
                               &opt, NULL);
    if (rc != 0)
        return rc;

    rc = tapi_devtool_run_start(&run);
    if (rc == 0)
        rc = tapi_devtool_run_wait(&run, timeout_ms);

    if (rc == 0)
    {
        tapi_devtool_run_get_output(&run, &output);
        if (out != NULL)
        {
            te_string_append(out, "%s", output.out);
            te_string_append(out, "%s", output.err);
        }
        if (status != NULL)
            *status = (output.status.type == TAPI_JOB_STATUS_EXITED) ?
                      output.status.value : -1;
    }

    tapi_devtool_run_fini(&run);

    return rc;
}

/* See description in tapi_power_internal.h */
te_errno
tapi_power_agent_sh(tapi_job_factory_t *factory, const char *name,
                    const char *script, const char **args, size_t n_args,
                    int timeout_ms, te_string *out, int *status)
{
    const char **argv;
    size_t i;
    te_errno rc;

    argv = TE_ALLOC((n_args + 3) * sizeof(*argv));
    argv[0] = "-c";
    argv[1] = script;
    argv[2] = "sh";
    for (i = 0; i < n_args; i++)
        argv[i + 3] = args[i];

    rc = agent_run(factory, name, "sh", argv, n_args + 3, timeout_ms, out,
                   status);

    free(argv);

    return rc;
}

/* See description in tapi_power_internal.h */
te_errno
tapi_power_agent_have(tapi_job_factory_t *factory, const char *program,
                      bool *present)
{
    const char *args[] = { program };
    int status;
    te_errno rc;

    rc = tapi_power_agent_sh(factory, "which", "command -v \"$1\" >/dev/null",
                             args, 1, 30000, NULL, &status);
    if (rc == 0)
        *present = (status == 0);

    return rc;
}
