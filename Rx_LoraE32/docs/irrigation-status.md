# Irrigation status on the TFT

The row at y=103..113 displays `V1:ON`, `V2:OFF`, `P:ON`, and one shared
`AUTO` / `MANUAL` irrigation mode. It replaces the command/MQTT row.

The receiver accepts this compact DATA response when its sequence matches the
pending request:

```text
<D,25,28.5,75,62,48,1,0,1,1,87>
```

- Fields are `seq,temp,humidity,soil1,soil2,valve1,valve2,pump,mode,battery`.
- `valve1`/`valve2`: `0=OFF`, `1=ON`.
- `pump`: `0=OFF`, `1=ON`.
- `mode`: `0=MANUAL`, `1=AUTO`.
- These are logical protocol states. On the STM32 hardware, the two valve
  relays are active-low while the pump relay is active-high; the ESP32 must
  not invert these received values.
- Status becomes unknown on screen when sensor DATA reaches
  `SENSOR_STALE_TIMEOUT_MS`; a fresh DATA response restores reported values.
- ACK and outgoing commands do not set valve state or irrigation mode.

Relay output state is not proof of physical valve movement or water flow.

## Live irrigation telemetry

While MANUAL or AUTO irrigation is active, the STM32 sends an unsolicited
snapshot once per second and immediately after irrigation state changes:

```text
<T,sseq,temp,hum,soil1,soil2,valve1,valve2,pump,mode,battery,zone,phase,cycle>
```

`phase` is `0=idle`, `1=watering`, `2=soak`, `3=measuring`, `4=failed`.
The ESP32 pauses normal polling while active telemetry is arriving. An idle
final snapshot ends the stream; if the stream disappears, polling resumes
after `TELEMETRY_TIMEOUT_MS`.

## MQTT control commands

The ESP32 accepts both relay and irrigation-mode commands on the configured
control topic:

```json
{"relay":1,"state":"ON"}
{"relay":2,"state":"OFF"}
{"mode":"AUTO"}
{"mode":"MANUAL"}
```

Mode messages are converted to `<CMD,SEQ=x,MODE=AUTO>` or
`<CMD,SEQ=x,MODE=MANUAL>`. They use the same ACK timeout, retry, and sequence
matching flow as relay commands. The displayed mode changes only after the
next DATA frame reports the STM32's current state.

Hardware checks: send AUTO and MANUAL reports, change each valve independently,
send a legacy DATA response, and stop DATA until the stale timeout. Verify the
row updates without flashing and MANUAL fits at the right edge of the TFT.
