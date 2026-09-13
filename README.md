# CommutateI2C

> Autonomous I2C switch/router for Arduino Nano (ATmega328P / ATmega328PB).
> CRC-8 protected packet routing between multiple boards over a single I2C bus.

[![Version](https://img.shields.io/badge/version-2.2.0-blue)]()
[![Platform](https://img.shields.io/badge/platform-Arduino%20AVR-orange)]()
[![License](https://img.shields.io/badge/license-MIT-green)]()

---

## Table of contents

- [Features](#features)
- [How it works](#how-it-works)
- [Wiring](#wiring)
- [Installation](#installation)
- [Quick start](#quick-start)
- [Library structure](#library-structure)
- [Configuration](#configuration)
- [API reference](#api-reference)
- [Protocol on the wire](#protocol-on-the-wire)
- [LCD status panel](#lcd-status-panel)
- [Serial CLI](#serial-cli)
- [Troubleshooting](#troubleshooting)
- [Notes on ATmega328PB](#notes-on-atmega328pb)
- [License](#license)

---

## Features

- **Autonomous routing** — the master discovers slaves on its own, no hardcoded list.
- **CRC-8 on every packet** — end-to-end integrity check, immune to bus noise.
- **Automatic retries** — up to `N` delivery attempts on transient errors.
- **Bus recovery** — if the bus hangs, the master generates 9 clock pulses and reinitializes.
- **Watchdog** — 2-second WDT keeps the boards alive through firmware freezes.
- **Ring queue on the slave** — up to 6 outgoing messages buffered, none lost.
- **Optional LCD1602 status panel** — two alternating screens (summary / last event).
- **Serial CLI** — live statistics, slave list, counter reset.
- **Single source tree** — one library for both master and slave firmware.

---

## How it works

```
┌─────────────┐         ┌─────────────┐         ┌─────────────┐
│   MASTER    │◄───────►│   SLAVE 8   │◄───────►│   SLAVE 9   │
│  (router)   │   I2C   │  (queue)    │   I2C   │  (queue)    │
└─────────────┘         └─────────────┘         └─────────────┘
       ▲
       │
   ┌───────┐
   │ LCD   │
   └───────┘
```

1. The master scans the bus every `SCAN_INTERVAL_MS` and builds a live list of slaves.
2. In a round-robin loop it sends `CMD_POLL` to each slave.
3. The slave replies with a fixed-length packet `[dst][len][data * MAX_MSG][crc8]`,
   either containing an outgoing message or all zeros.
4. If non-empty, the master delivers the payload to `dst` with `CMD_DELIVER` and
   waits for the hardware I2C ACK.
5. The master then sends `CMD_ACK` back to the source with status `OK` or `NACK`.
6. On `OK` the slave pops the message from its queue. On `NACK` it stays for the next round.

The slave never initiates traffic — I2C is strictly master-driven. This keeps the
protocol deterministic and avoids collisions.

---

## Wiring

| Signal | Master | Slave 8 | Slave 9 | LCD1602 |
|---|---|---|---|---|
| SDA    | A4     | A4      | A4      | SDA     |
| SCL    | A5     | A5      | A5      | SCL     |
| GND    | GND    | GND     | GND     | GND     |
| VCC    | 5V     | 5V      | 5V      | 5V      |

**Rules of thumb:**

- Always connect **GND** between all boards. Missing GND is the #1 cause of intermittent failures.
- Keep SDA/SCL wires **shorter than 30 cm** when using internal pull-ups only.
- For longer buses or noisy environments, add external **4.7 kΩ** pull-ups from SDA and SCL to 5V.
- The LCD1602 I2C backpack usually already contains 4.7 kΩ pull-ups — this improves bus reliability for free.

---

## Installation

### Arduino IDE

1. Download or clone this repository.
2. Compress the `CommutateI2C` folder into a ZIP (the folder must contain `library.properties`).
3. In the IDE: **Sketch → Include Library → Add .ZIP Library…**, select the archive.
4. Restart the IDE. The library will appear under **File → Examples → CommutateI2C**.

Or manually copy the folder into:

```
~/Documents/Arduino/libraries/CommutateI2C/
```

### PlatformIO

Drop the folder into your project's `lib/` directory:

```
project/
├── lib/
│   └── CommutateI2C/
│       ├── library.properties
│       ├── src/
│       └── examples/
├── src/
│   └── main.cpp
└── platformio.ini
```

PlatformIO automatically discovers local libraries in `lib/`, so no `lib_deps` entry is needed.

If you also use an LCD1602 with I2C backpack, add to `platformio.ini`:

```ini
lib_deps =
    marcoschwartz/LiquidCrystal_I2C@^1.1.4
```

---

## Quick start

### Master firmware

```cpp
#include <Wire.h>
#include <CommutateI2C.h>

#if COMMUTATE_I2C_USE_LCD
  #include <LiquidCrystal_I2C.h>
  LiquidCrystal_I2C lcd(COMMUTATE_I2C_LCD_ADDR,
                        COMMUTATE_I2C_LCD_COLS,
                        COMMUTATE_I2C_LCD_ROWS);
#endif

CommutateI2C bus(CommutateI2C::ROLE_MASTER);

void setup() {
  Serial.begin(9600);
#if COMMUTATE_I2C_USE_LCD
  lcd.begin();
  lcd.backlight();
  bus.attachLCD(&lcd);
#endif
  bus.begin();
}

void loop() {
  bus.update();
}
```

### Slave firmware

```cpp
#include <CommutateI2C.h>

#define SLAVE_ADDRESS  8    /* 9 for the second slave */
#define PEER_ADDRESS   9    /* 8 for the second slave */

CommutateI2C bus(CommutateI2C::ROLE_SLAVE, SLAVE_ADDRESS, PEER_ADDRESS);

void onMsg(uint8_t src, const uint8_t *data, uint8_t len) {
  Serial.print("RX from 0x");
  Serial.print(src, HEX);
  Serial.print(": ");
  for (uint8_t i = 0; i < len; i++) Serial.write(data[i]);
  Serial.println();
}

void onDelivered(bool ok) {
  Serial.println(ok ? "ACKed" : "NACKed");
}

void setup() {
  Serial.begin(9600);
  bus.onMessage(onMsg);
  bus.onDelivered(onDelivered);
  bus.begin();
}

void loop() {
  bus.update();
}
```

Flash the master sketch into board #1, the slave sketch into boards #2 and #3.
Change `SLAVE_ADDRESS` and `PEER_ADDRESS` for each slave.

Ready-made examples live in `examples/Master/` and `examples/Slave/`.

---

## Library structure

```
CommutateI2C/
├── library.properties           Arduino library metadata
├── keywords.txt                 IDE syntax highlighting
├── README.md                    this file
├── src/
│   ├── CommutateI2C_Config.h    user configuration — edit this
│   ├── CommutateI2C.h           public API
│   └── CommutateI2C.cpp         implementation
└── examples/
    ├── Master/Master.ino        master demo
    └── Slave/Slave.ino          slave demo
```

Only `src/CommutateI2C_Config.h` is normally edited. Everything else is reused as-is.

---

## Configuration

All knobs live in `src/CommutateI2C_Config.h`.

| Macro | Default | Purpose |
|---|---|---|
| `COMMUTATE_I2C_USE_LCD` | `1` | Enable LCD1602 status panel on the master |
| `COMMUTATE_I2C_USE_WATCHDOG` | `1` | Enable 2-second watchdog timer |
| `COMMUTATE_I2C_USE_LED` | `1` | Enable pin-13 activity LED |
| `COMMUTATE_I2C_USE_SERIAL` | `1` | Enable Serial debug output |
| `COMMUTATE_I2C_MAX_MSG` | `16` | Max payload bytes per packet |
| `COMMUTATE_I2C_MAX_SLAVES` | `16` | Max slaves the master tracks |
| `COMMUTATE_I2C_QUEUE_SIZE` | `6` | Slave outgoing ring buffer depth |
| `COMMUTATE_I2C_SCAN_MS` | `5000` | Bus rescan interval (ms) |
| `COMMUTATE_I2C_POLL_DELAY` | `3` | Delay after POLL before read (ms) |
| `COMMUTATE_I2C_AUTOSEND_MS` | `3000` | Slave autosend demo interval (ms) |
| `COMMUTATE_I2C_RETRIES` | `3` | Delivery attempts on failure |
| `COMMUTATE_I2C_RETRY_PAUSE` | `5` | Pause between attempts (ms) |
| `COMMUTATE_I2C_LCD_ADDR` | `0x27` | LCD I2C address (`0x27` or `0x3F`) |
| `COMMUTATE_I2C_LCD_COLS` | `16` | LCD columns |
| `COMMUTATE_I2C_LCD_ROWS` | `2` | LCD rows |
| `COMMUTATE_I2C_LCD_SWAP_MS` | `4000` | Screen rotation period (ms) |
| `COMMUTATE_I2C_LED_PIN` | `13` | Activity LED pin |
| `COMMUTATE_I2C_BUS_ERR_MAX` | `5` | Consecutive errors before bus recovery |

**Important:** do **not** call `Wire.setClock()` with frequencies below 100 kHz on AVR.
The `TWBR` divider overflows and the bus becomes unstable. The library never changes
the default 100 kHz clock.

---

## API reference

### Constructor

```cpp
CommutateI2C(Role role, uint8_t address = 8, uint8_t peer = 9);
```

- `role` — `CommutateI2C::ROLE_MASTER` or `CommutateI2C::ROLE_SLAVE`
- `address` — own 7-bit address (slave only)
- `peer` — default destination for autosend (slave only)

### Lifecycle

| Method | Returns | Description |
|---|---|---|
| `bool begin()` | `true` on success | Initialize Wire, watchdog, LED |
| `void update()` | — | Call from `loop()`, drives the whole state machine |

### Master

| Method | Returns | Description |
|---|---|---|
| `uint8_t slaveCount()` | number of slaves found | Live count |
| `uint8_t slaveAt(uint8_t i)` | address or 0 | Address of the i-th slave |

### Slave

| Method | Returns | Description |
|---|---|---|
| `bool send(uint8_t dst, const uint8_t *data, uint8_t len)` | queued | Queue a raw packet |
| `bool send(uint8_t dst, const char *str)` | queued | Queue a string |
| `bool sendToPeer(const char *str)` | queued | Queue a string to `peer` |
| `void onMessage(MessageHandler cb)` | — | Callback on incoming delivery |
| `void onDelivered(DeliverHandler cb)` | — | Callback on ACK/NACK for our message |

Handler signatures:

```cpp
typedef void (*MessageHandler)(uint8_t src, const uint8_t *data, uint8_t len);
typedef void (*DeliverHandler)(bool ok);
```

### Statistics

```cpp
const Stats& stats() const;
void resetStats();
void printStats(Stream &s) const;
```

`Stats` fields:

| Field | Meaning |
|---|---|
| `ok` | Successful deliveries (master) |
| `fail` | Failed deliveries (master) |
| `poll` | Total non-empty polls (master) |
| `crcErr` | CRC mismatches (both roles) |
| `retry` | Retry attempts (master) |
| `timeout` | Bus timeouts (master) |
| `busRecovery` | Bus recoveries performed (master) |
| `sent` | Messages queued for sending (slave) |
| `recv` | Messages received (slave) |
| `nack` | NACKs received on our messages (slave) |

### Runtime tuning

```cpp
void setScanInterval(uint16_t ms);
void setPollDelay(uint16_t ms);
void setAutosendInterval(uint16_t ms);
void setMaxRetries(uint8_t n);
void enableAutosend(bool en);
```

### LCD

```cpp
#if COMMUTATE_I2C_USE_LCD
void attachLCD(LiquidCrystal_I2C *lcd);
#endif
```

---

## Protocol on the wire

All commands originate from the master. Slaves only respond.

| Command | Code | Frame | Reply |
|---|---|---|---|
| `POLL` | `0x01` | `[0x01]` | `[dst][len][data * MAX_MSG][crc8]` |
| `DELIVER` | `0x10` | `[0x10][src][len][data...][crc8]` | hardware I2C ACK |
| `ACK` | `0x20` | `[0x20][status][crc8]` | — |

- `status` is `0x01` for OK, `0x00` for NACK.
- `crc8` uses the CRC-8/ATM polynomial `0x07`, init `0x00`.
- `POLL` replies are **fixed length** so both sides can parse without length-prefix sync.
- `DELIVER` success is a hardware ACK from the destination — no separate software ACK round-trip,
  which eliminates the classic race between the slave ISR and the master's read.

---

## LCD status panel

If `COMMUTATE_I2C_USE_LCD = 1` and an LCD is attached to the master, the display
rotates between two screens every `COMMUTATE_I2C_LCD_SWAP_MS` milliseconds.

**Summary screen**

```
S: 08 09
OK:42 F:0
```

- Line 1 — addresses of detected slaves.
- Line 2 — successful and failed deliveries.

**Last event screen**

```
0x08>0x09 OK
msg #42
```

- Line 1 — source, destination, delivery status.
- Line 2 — payload (truncated to 16 characters).

If the display is blank but the backlight is on, adjust the contrast potentiometer
on the I2C backpack. If both are dead, check the I2C address (`0x27` vs `0x3F`).

---

## Serial CLI

Open the serial monitor at **9600 baud** and type a single letter followed by Enter.

### Master commands

| Key | Action |
|---|---|
| `s` | Print statistics |
| `l` | List detected slaves |
| `r` | Reset all counters |
| `t` | Send a test packet to the first slave |
| `d` | Dump configuration |
| `h` | Print help |

### Slave commands

| Key | Action |
|---|---|
| `s` | Print statistics |
| `r` | Reset all counters |
| `h` | Print help |

---

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Slaves:` list empty | No power / no GND / wrong pins | Check 5V and GND on every board, verify A4/A5 |
| Only `0x08` found | Second slave flashed with `NODE_ADDRESS 8` | Change to `9`, reflash |
| `deliver: endTx err=2` | NACK on address — destination not present | Wrong address, dead board, bad contact on SDA/SCL |
| `deliver: endTx err=3` | NACK on data — bus noise | Shorten wires, add 4.7 kΩ pull-ups, lower `MAX_MSG` |
| `deliver: endTx err=5` | Bus timeout | SDA stuck low, bus recovery will fire after 5 errors |
| `OK:0 F:10000` in a loop | All deliveries fail — check above | Watch the `err=N` diagnostic line |
| LCD blank, backlight on | Contrast | Rotate the potentiometer on the backpack |
| LCD blank, no backlight | Power / address | Check 5V, rescan for `0x27` / `0x3F` |
| Random freezes | Power, watchdog | Ensure common GND, keep WDT enabled |
| `Wire.setClock()` locks the bus | AVR TWBR overflow below 100 kHz | Never call it with values below 100000 |

---

## Notes on ATmega328PB

The ATmega328**PB** variant is **not** the same chip as the classic ATmega328P.
If you flash a 328PB board using the plain "Arduino Nano" board profile, the fuse
bits and the TWI peripheral may not be configured correctly — which manifests as
persistent `err=2` even with correct addresses.

**Fix:** install [MiniCore](https://github.com/MCUdude/MiniCore) and select the
correct board:

1. **File → Preferences → Additional Board Manager URLs:**
   ```
   https://mcudude.github.io/MiniCore/package_MCUdude_MiniCore_index.json
   ```
2. **Tools → Board → Boards Manager… → MiniCore → Install.**
3. For 328PB boards, select:
   - Board: **ATmega328PB**
   - Clock: **16 MHz external**
   - BOD: **2.7V**
   - Bootloader: **Yes (UART0)**
4. For 328P boards, keep **Arduino Nano** with the correct bootloader variant.

---

## License

MIT. See `LICENSE` for details.

---

**Author:** HioSW
**Version:** 2.2.0
**Tested on:** Arduino Nano (ATmega328P, ATmega328PB), Arduino IDE 2.x, MiniCore 3.x
