# Elli Charger 2 dynamic load management

ESP32-C3 firmware prototype for reading HomeWizard P1 phase power over the local HTTP API and calculating a dynamic charging-current target.

## Safety status

Elli Modbus output is deliberately disabled (`ELLI_MODBUS_OUTPUT_ENABLED = false`). The supplied register table identifies holding registers 261 (maximum-current command), 262 (failsafe current), and 257 (master watchdog timeout), with FC03 for reads and FC06 for single-register writes. Current commands use 10 register units per amp (`100 = 10 A`); the supplied failsafe value is `60` (6 A), and the watchdog default is `15000 ms` (15 s). These values are recorded in `include/config.h`, but this project does not write them yet.

Before enabling writes, confirm the Modbus TCP Unit ID, whether the listed bus addresses are used as-is in protocol requests, and exactly which successful Modbus exchanges reset the watchdog. The clarified watchdog range is 0–63335 ms. Also verify the official table applies to the installed Charger 2 firmware.

If P1 data is missing, malformed, or older than the configured timeout, firmware reports a fault and does not calculate a new target for the charger. This prototype does not yet provide a Modbus fail-safe; configure and test the charger's own fail-safe locally before using automatic control. Do not rely on this prototype as the only protection against an overloaded installation.

## Status LED

The linked ESP32-C3 USB-C board listing specifies a blue status LED on GPIO8. It turns on for 0.5 seconds after each valid P1 update. The firmware checks whether Elli's Modbus TCP port 502 accepts a TCP connection every five seconds; while unreachable, the same LED turns on for 0.5 seconds once per five-second period. This checks TCP port reachability only, not a Modbus request/response or the charger's watchdog state. The Elli fault indication takes priority over the P1 pulse.

The board listing does not specify the LED's electrical polarity. `STATUS_LED_ACTIVE_LEVEL` defaults to `LOW`; if the LED remains dark or is inverted on the actual board, change it to `HIGH` in `include/config.h`.

## First-time setup

1. Power on the ESP32-C3. On first setup, it creates a password-protected Wi-Fi network named `Elli-Setup-XXXXXX`.
2. Open the serial monitor at 115200 baud to read that setup network's unique password, connect a phone or computer to it, then open `http://192.168.4.1`.
3. Select the same home WLAN that the HomeWizard P1 and Elli charger use, then enter its password. Enter each device's IPv4 address or hostname, such as `wallbox-serial.local`, without an `http://` or `https://` prefix. Set the main fuse current per phase (for example, enter `25` for a 3x25 A supply) and the maximum charging power in kW.
4. Save the form. The ESP32 stores Wi-Fi credentials in its Wi-Fi NVS and load settings in Preferences/NVS flash; after that, it reconnects automatically on power-up. If the saved network cannot be reached, the setup portal opens again.
5. To change the WLAN or any saved addresses later, open the serial monitor at 115200 baud and send `c`; the setup portal reopens with the current values.

Defaults are 25 A per phase and 11 kW. Valid ranges are 6-63 A per phase and 0-11 kW; 0 kW disables charging. The charger phase count is currently configured as three in `include/config.h`; the power limit is converted to an equivalent per-phase current using 230 V and that phase count. The load manager also subtracts the configured reserve and caps the current at the charger's 16 A limit. Export is treated as zero household load, and a target below 6 A becomes 0 A.

The selected WLAN name, assigned ESP32 IP, and configured P1 and Elli hostnames are printed to the serial monitor after connection. Device hosts are stored in Preferences/NVS and can be changed by reopening the setup portal. `.local` names use mDNS and require the devices and ESP32 to share a WLAN where multicast DNS is available; static IPv4 or normal DNS names are alternatives.

## Build and flash

Install PlatformIO Core or the PlatformIO IDE extension, then run from this project directory:

```sh
pio run
pio run -t upload
pio device monitor
```

Select the correct serial port if PlatformIO does not detect it automatically. The build uses the generic ESP32-C3 DevKitC-02 platform profile with native USB CDC enabled, matching the listed chip, 4 MB flash, and USB-C connection; the seller does not identify an exact board-vendor model.

## Current limitations

- Only the HomeWizard P1 local endpoint `/api/v1/data` is read; expected fields are `active_power_l1_w`, `active_power_l2_w`, and `active_power_l3_w`.
- The program reports a calculated target over serial but does not send it to Elli.
- Modbus TCP transport, watchdog configuration, failsafe programming, and charger command writes remain unimplemented until the official register encoding and behavior are validated.
