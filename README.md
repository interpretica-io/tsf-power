# tsf-power

Measuring the power and energy a device under test draws, with a bench
instrument on the lab network — packaged as an external Test Environment
(TE) repository and consumed with the `TE_EXT_REPO` builder directive.

Library:

- `tapi_power` — engine-side TAPIs, built as a shared library:
  - `tapi_power` — one interface over the measurement instruments of the
    major brands: identify, then read voltage, current and power;
  - `tapi_power_energy` — the energy consumed over a test window, by
    integrating power across it: joules, milliwatt-hours, average and peak;
  - `tapi_power_profiler` — the USB energy profilers for embedded work
    (Nordic PPK2, Joulescope, Qoitech Otii, Monsoon), driven through their
    vendor tool on the agent;
  - `tapi_power_switch` — turning power on, off and cycling it through
    programmable outlets: SNMP PDUs, HTTP relays and switchable USB hubs;
  - `tapi_power_scpi` — the SCPI-over-TCP transport underneath.

The brands driven, all over SCPI on a raw TCP socket:

| Brand | Typical instruments |
|---|---|
| Keysight | N6705 source/analyzer, 34461A DMM, N7900 |
| Rohde & Schwarz | NGM/NGU source-measure, HMC8015 analyzer |
| Yokogawa | WT310/WT500/WT3000 power analyzers |
| Keithley / Tektronix | DMM6500, 2280S supply, 2450 SMU |
| Rigol | DP800 supplies, DM-series DMM |
| Chroma | 66200 power meters |
| generic | any instrument answering the SCPI `MEASure` tree |

The SCPI part builds only on `tapi`: the bench instrument is reached over
the network directly, no agent needed. The profiler and switch parts
additionally use `tapi_job` and `tsf-devtool`, because a USB profiler or a
programmable outlet is driven by a tool (its vendor tool, `curl`, `snmpset`
or `uhubctl`) running on the agent.

## Usage

Declare the repository in an external libraries catalog (e.g.
`conf/external.yml` in the test suite) and pass it to
`dispatcher.sh --external=external.yml`:

```yaml
repositories:
  - name: tsf_power
    url: https://github.com/interpretica-io/tsf-power.git
    ref: <tag>
    libs:
      - tapi_power
```

Bind it to the engine platform in `builder.conf`:

```
TE_EXT_REPO_USE([tsf_power], [], [tapi_power])
```

and add `tapi_power` to the `te_libs` list of the suite's `meson.build`.

## Measuring

Name the brand, the host and the channel; the brand selects the SCPI
command set, so the calls above it are the same whichever instrument
answers:

```c
tapi_power_instrument inst = {
    .brand = TAPI_POWER_KEYSIGHT, .host = "192.0.2.50", .channel = 1,
};
tapi_power_reading r;

CHECK_RC(tapi_power_open(&inst));                 /* connects, reads *IDN? */
CHECK_RC(tapi_power_measure(&inst, &r));
RING("DUT draws %.3f W (%.3f V, %.3f A)", r.power, r.voltage, r.current);
tapi_power_close(&inst);
```

When an instrument does not report power directly, it is computed from
voltage times current, so `r.power` is always filled. `tapi_power_command`
and `tapi_power_query` send raw SCPI for anything not wrapped — ranging,
averaging, output control.

## Energy over a window

The headline figure of a battery-life or efficiency test: the energy used
while a workload ran. It is measured by sampling the instrument across the
window and integrating, which works with every brand and does not rely on
the instrument having its own accumulator.

```c
tapi_power_energy result;

/* Sample every 50 ms for 10 s while the DUT runs the workload. */
CHECK_RC(tapi_power_energy_measure(&inst, 10000, 50, &result));
tapi_power_energy_log(&result, "the workload");
if (result.milliwatt_hours > budget_mwh)
    TEST_VERDICT("The workload used %.1f mWh, over the %.1f mWh budget",
                 result.milliwatt_hours, budget_mwh);
```

For a window whose end the test decides at run time, open a session with
`tapi_power_energy_start()`, call `tapi_power_energy_sample()` in a loop
while the work runs, and `tapi_power_energy_stop()` when it finishes.

The integral is a trapezoidal sum over the samples taken, so sample fast
enough that power does not swing far between two samples.

## Embedded profilers

Bench meters cannot see a sleeping BLE node that lives at microamps and
wakes for milliamps in bursts. The USB power profilers can, and they are
driven not by SCPI but by a vendor tool on the agent the profiler is
plugged into:

| Kind | Instrument | Tool |
|---|---|---|
| `TAPI_POWER_PROFILER_NORDIC_PPK2` | Nordic Power Profiler Kit II | `ppk2-api` |
| `TAPI_POWER_PROFILER_JOULESCOPE` | Joulescope JS110/JS220 | `joulescope` |
| `TAPI_POWER_PROFILER_QOITECH_OTII` | Qoitech Otii Arc/Ace | `otii` |
| `TAPI_POWER_PROFILER_MONSOON` | Monsoon HV Power Monitor | `monsoon` |
| `TAPI_POWER_PROFILER_GENERIC` | any tool you give a command for | yours |

```c
tapi_power_profiler prof = {
    .kind = TAPI_POWER_PROFILER_NORDIC_PPK2,
    .device = "/dev/ttyACM0", .supply_mv = 3300,
};
tapi_power_profile result;

CHECK_RC(tapi_power_profiler_measure(factory, &prof, 10000, &result));
tapi_power_profile_log(&result, "sleep current");
if (result.avg_current > 20e-6)
    TEST_VERDICT("Sleep current %.1f uA over the 20 uA budget",
                 result.avg_current * 1e6);
```

A measurement runs the tool for the duration and reads back the average
current, the voltage, the peak and the energy. The tools differ by vendor
and version, so each kind carries a default command template that a lab
adjusts through the `command` field (`$DEV`, `$DURATION`, `$VMV` are
exported to it); the numbers are read from the output by label, so what a
new tool needs is a command that prints average current, voltage and
energy. Energy the tool does not report is derived from average power and
the window length.

## Switching power

Turning the power to a device on and off, and cycling it — to recover a
wedged device, measure cold boot, or prove it comes back after power is
pulled. The outlet is the one thing that answers when the device does not.

| Kind | What it is | Tool |
|---|---|---|
| `TAPI_POWER_SWITCH_SNMP_APC` | APC / PowerNet rack PDU | `snmpset`/`snmpget` |
| `TAPI_POWER_SWITCH_SNMP` | any PDU with the OIDs you give | `snmpset`/`snmpget` |
| `TAPI_POWER_SWITCH_DLI` | Digital Loggers Web Power Switch | `curl` |
| `TAPI_POWER_SWITCH_NETIO` | NETIO PowerBox / PowerCable | `curl` |
| `TAPI_POWER_SWITCH_TASMOTA` | Tasmota relay (Sonoff etc.) | `curl` |
| `TAPI_POWER_SWITCH_SHELLY` | Shelly relay | `curl` |
| `TAPI_POWER_SWITCH_UHUBCTL` | per-port power of a USB hub | `uhubctl` |
| `TAPI_POWER_SWITCH_GENERIC` | any switch you give commands for | yours |

```c
tapi_power_switch sw = {
    .kind = TAPI_POWER_SWITCH_SNMP_APC, .host = "192.0.2.20",
    .outlet = 3, .snmp_community = "private",
};

CHECK_RC(tapi_power_switch_cycle(factory, &sw, 5000));   /* off, 5 s, on */
```

`tapi_power_switch_on`/`_off` set the outlet; `_cycle` turns it off, waits
on the engine and turns it on again (so the off-time does not depend on the
PDU's own cycle timer); `_get` reads the state where the switch reports it;
`_set_checked` sets and reads back to confirm. The switch is driven by a
tool on the agent it is reachable from, so this half uses the agent, like
the profilers.

For an SNMP PDU that is not APC, use `TAPI_POWER_SWITCH_SNMP` and give the
control and status OID bases (the outlet number is appended) and, if the
vendor does not use 1 = on / 2 = off, the values it does use.

## Reaching the instrument

The connection is opened from the host running the library — the engine,
on the bench network with the instruments — to the instrument's SCPI port
(5025 by the LXI convention). An instrument on GPIB or USB is reached by
putting a LAN/GPIB or LAN/USB gateway in front of it, which is how such
instruments are shared on a lab network.
