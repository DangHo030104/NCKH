# Irrigation status on the TFT

The row at y=103..113 displays `V1:ON`, `V2:OFF`, and one shared
`AUTO` / `MANUAL` irrigation mode. It replaces the command/MQTT row.

The receiver accepts these optional, case-sensitive fields in a valid DATA
response whose SEQ matches the pending request:

```text
<DATA,SEQ=25,T=28.5,H=75,SM1=62,SM2=48,V1=ON,V2=OFF,MODE=AUTO>
```

- `V1`, `V2`: `ON` or `OFF`.
- `MODE`: `AUTO` or `MANUAL`, shared by both zones.
- Missing, duplicate, or invalid status fields display `--` independently.
- Sensor-only DATA remains supported; valve and mode values then display `--`.
- Status becomes unknown on screen when sensor DATA reaches
  `SENSOR_STALE_TIMEOUT_MS`; a fresh DATA response restores reported values.
- ACK and outgoing commands do not set valve state or irrigation mode.

STM32 firmware must populate these fields from its current state. This receiver
change does not implement automatic irrigation or change the transmitter.
Relay output state is not proof of physical valve movement or water flow.

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
