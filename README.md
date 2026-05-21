# SunsetSwitch — Etekcity ESWL01 Sunset/Sunrise Automation

Custom firmware for the **Etekcity ESWL01 wireless outlet switch**, replacing the stock firmware with an automatic sunset-to-sunrise controller. The switch turns ON before sunset and OFF after sunrise, calculated daily for a fixed location using NTP time — no cloud service required.

---

## Hardware

### Etekcity ESWL01
The ESWL01 is a WIFI controlled outlet switch that contains an **ESP8266 ESP-12E** module on its internal control board. This makes it flashable with custom Arduino/PlatformIO firmware via its UART pins.

<img width="1461" height="1294" alt="image" src="https://github.com/user-attachments/assets/2229a1d8-9b64-4f88-9705-80ad3ca7f45b" />

### Pin Mapping (ESWL01 Internal Board)

| Function               | ESP8266 GPIO | Notes                                   |
|------------------------|-------------|-----------------------------------------|
| Relay control          | GPIO13      | HIGH = relay ON, LOW = relay OFF        |
| Status LED             | GPIO5       | Mirrors relay state                     |
| Capacitive touch input | GPIO14      | ADAM02S sensor — HIGH = touched         |
| UART TX                | GPIO1 (TX)  | For flashing / serial monitor           |
| UART RX                | GPIO3 (RX)  | For flashing / serial monitor           |
| Flash mode             | GPIO0       | Pull LOW at boot to enter flash mode    |

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
             └─ Every 60 seconds: evaluate schedule
                 ├─ Nighttime window → Relay ON, LED ON  (clears any manual override)
                 ├─ Daytime + manual override → Relay ON, LED ON
                 └─ Daytime, no override → Relay OFF, LED OFF
```

### Schedule
The relay turns **ON 60 minutes before sunset** and **OFF 60 minutes after sunrise**.  
Both offsets are configurable via constants in `src/main.cpp`:

```cpp
#define MINUTES_BEFORE_SUNSET  60
#define MINUTES_AFTER_SUNRISE  60
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

## Capacitive Touch Override (ADAM02S — GPIO14)

The touch sensor allows manual control without disrupting the automatic schedule.

| Switch state | Touch action | Result |
|---|---|---|
| OFF (daytime) | Tap | Turns **ON**, stays ON until next touch or schedule's OFF time |
| ON (manual or scheduled) | Tap | Turns **OFF** immediately |
| Night schedule activates | — | Turns ON automatically, clears any manual state |
| Sunrise + 1hr (schedule OFF) | — | Turns OFF automatically, ready for next manual use |

This means you can turn the light on manually during the day and it will **stay on until you tap again**, or it will turn off automatically at the next sunrise transition — whichever comes first.

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
[2025-11-15 16:28 DST=0] sunrise=07:23 sunset=16:28  on=15:28 off=08:23
Relay/LED -> ON (scheduled night)
[2025-11-15 16:29 DST=0] sunrise=07:23 sunset=16:28  on=15:28 off=08:23
Relay/LED -> ON (scheduled night)
Touch — relay OFF
Touch — relay ON (manual, held until sunrise)
Relay/LED -> ON (manual daytime override)
```
