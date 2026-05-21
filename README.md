# SunsetSwitch — Etekcity ESWL01 Sunset/Sunrise Automation

Custom firmware for the **Etekcity ESWL01 wireless outlet switch**, replacing the stock firmware with an automatic sunset-to-sunrise controller. The switch turns ON at sunset and OFF at sunrise, calculated daily for a fixed location using NTP time — no cloud service required.

---

## Hardware

### Etekcity ESWL01
The ESWL01 is a 433 MHz RF-controlled outlet switch that contains an **ESP8266 ESP-12E** module on its internal control board. This makes it flashable with custom Arduino/PlatformIO firmware via its UART pins.

### Pin Mapping (ESWL01 Internal Board)

| Function        | ESP8266 GPIO | Notes                          |
|-----------------|-------------|--------------------------------|
| Relay control   | GPIO13      | HIGH = relay ON, LOW = relay OFF |
| Status LED      | GPIO5       | Mirrors relay state             |
| UART TX         | GPIO1 (TX)  | For flashing / serial monitor   |
| UART RX         | GPIO3 (RX)  | For flashing / serial monitor   |
| Flash mode      | GPIO0       | Pull LOW at boot to enter flash mode |

---

## Flashing the Device

### Requirements
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-to-TTL serial adapter (3.3V logic — **do NOT use 5V**)
- Soldering iron (to access internal UART pads)

### Wiring the Serial Adapter

```
USB-TTL Adapter      ESWL01 PCB
─────────────        ──────────
GND          ──────  GND
3.3V         ──────  3.3V (do NOT use 5V)
TX           ──────  RX  (GPIO3)
RX           ──────  TX  (GPIO1)
             ──────  GPIO0 → GND  (hold LOW during boot to enter flash mode)
```

> ⚠️ **Safety Warning:** The ESWL01 is a mains-connected device. **Disconnect it completely from mains power** before opening the enclosure or connecting any serial adapter. Never connect a serial adapter while the device is plugged in.

### Flash Steps

1. Open the ESWL01 enclosure and locate the ESP-12E module's UART pads on the PCB.
2. Solder wires to `TX`, `RX`, `GND`, `3.3V`, and `GPIO0`.
3. Connect your USB-TTL adapter as shown above.
4. Hold `GPIO0` LOW (connect to GND) **before** powering the board to enter bootloader mode.
5. In PlatformIO, run:
   ```
   pio run --target upload
   ```
6. After flashing, disconnect `GPIO0` from GND and reboot the device.

---

## First-Time WiFi Setup

WiFi credentials are **never stored in the firmware**. On first boot, the device uses [WiFiManager](https://github.com/tzapu/WiFiManager) to provision credentials securely:

1. Power on the device after flashing.
2. The ESP8266 broadcasts a WiFi access point: **`SunsetSwitch-Setup`**
3. Connect your phone or laptop to that network.
4. A captive portal opens automatically (or navigate to `http://192.168.4.1`).
5. Select your home WiFi network and enter the password.
6. The device connects, saves credentials to flash, and begins normal operation.

On all subsequent boots the saved credentials are used automatically — the setup portal does not appear again unless credentials become invalid.

### Re-configuring WiFi
To reset WiFi credentials (e.g. after changing your router), erase the flash and reflash:
```
pio run --target erase
pio run --target upload
```

---

## How It Works

```
Boot
 └─ WiFiManager → connect to WiFi
     └─ NTP sync (pool.ntp.org / time.nist.gov)
         └─ Calculate today's sunrise & sunset for Sammamish, WA
             └─ Every 60 seconds: evaluate current time
                 ├─ After sunset / before sunrise → Relay ON, LED ON
                 └─ After sunrise / before sunset → Relay OFF, LED OFF
```

### Sunrise/Sunset Calculation
Uses the **NOAA simplified solar algorithm** implemented directly in firmware (no external library). Sunrise and sunset are computed daily for:

- **Location:** Sammamish, WA, USA
- **Latitude:** 47.6163° N
- **Longitude:** 122.0356° W
- **Timezone:** PST (UTC−8) / PDT (UTC−7), with automatic DST transitions

### NTP Time Sync
- Syncs on boot via `pool.ntp.org` and `time.nist.gov`
- Re-syncs every **1 hour** to stay accurate
- PST/PDT daylight saving transitions are handled automatically via the POSIX TZ string `PST8PDT,M3.2.0,M11.1.0`

### Relay Evaluation
- Relay state is re-evaluated every **60 seconds**
- Serial monitor (115200 baud) logs current time, sunrise/sunset times, and relay state on each evaluation

---

## Customizing the Location

To use a different location, update these constants in `src/main.cpp`:

```cpp
const float LAT        =  47.6163f;   // Your latitude
const float LON        = -122.0356f;  // Your longitude (negative = West)
const int   UTC_OFFSET = -8;          // Your UTC base offset (without DST)
```

Also update the TZ string in `syncNTP()` if you're outside the US Pacific timezone:
```cpp
configTime("PST8PDT,M3.2.0,M11.1.0", "pool.ntp.org", "time.nist.gov");
```
See [POSIX TZ format](https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html) for other timezone strings.

---

## Project Structure

```
SunsetSwitchESWL01/
├── src/
│   └── main.cpp        # All firmware logic
├── platformio.ini       # PlatformIO build config
└── README.md
```

### Dependencies (auto-installed by PlatformIO)
| Library | Purpose |
|---------|---------|
| `ESP8266WiFi` | WiFi connectivity (built-in) |
| `tzapu/WiFiManager` | Captive portal WiFi provisioning |
| `DNSServer` | Required by WiFiManager (built-in) |
| `ESP8266WebServer` | Required by WiFiManager (built-in) |

---

## Serial Monitor Output Example

```
WiFi connected — IP: 192.168.1.42
Syncing NTP.... done
[2025-11-15 17:02 DST=0] sunrise=07:23  sunset=16:28
Relay/LED -> ON (night)
[2025-11-15 17:03 DST=0] sunrise=07:23  sunset=16:28
Relay/LED -> ON (night)
```
