# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2026 Interpretica Unipessoal Lda
#
# External TE library (see TE_EXT_REPO): engine-side TAPIs for measuring
# the power and energy a device under test draws, with a bench instrument
# on the lab network over SCPI - Keysight, Rohde & Schwarz, Yokogawa,
# Keithley/Tektronix, Rigol and Chroma. Built as a shared library on the
# engine platform and as a static library when agents or agent apps are
# built.

if get_option('engine')
    build_lib_shared = true
endif
build_lib_static = not get_option('engine') \
    or get_option('agents').split().length() != 0 \
    or get_option('agent-apps').split().length() != 0
install_lib = install_dev

headers += files(
    'tapi_power.h',
    'tapi_power_energy.h',
    'tapi_power_profiler.h',
    'tapi_power_scpi.h',
    'tapi_power_switch.h',
)

# tapi_power_internal.h is internal to this library and is not installed.
sources += files(
    'tapi_power.c',
    'tapi_power_agent.c',
    'tapi_power_energy.c',
    'tapi_power_profiler.c',
    'tapi_power_scpi.c',
    'tapi_power_switch.c',
)

# The SCPI part reaches a bench instrument over the network and needs only
# tapi; the profiler part drives a USB tool on the agent, hence tapi_job and
# tapi_devtool.
te_libs += [
    'tapi',
    'tapi_job',
    'tapi_devtool',
]
