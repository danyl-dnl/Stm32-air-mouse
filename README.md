# STM32 Air Mouse

A bare-metal air mouse built on the **STM32F103C8T6** ("Blue Pill") and an **MPU-6050** gyroscope. Tilt the board to move the cursor; two push-buttons provide left and right clicks. Gyro data is streamed over UART to a Python companion script that drives the Windows mouse API.

---

## Table of Contents

- [How It Works](#how-it-works)
- [Hardware](#hardware)
  - [Wiring](#wiring)
- [Repository Layout](#repository-layout)
- [Firmware](#firmware)
  - [Peripheral Configuration](#peripheral-configuration)
  - [Startup Sequence](#startup-sequence)
  - [Main Loop](#main-loop)
  - [Error Codes](#error-codes)
  - [UART Packet Format](#uart-packet-format)
- [PC Companion Script](#pc-companion-script)
  - [Dependencies](#dependencies)
  - [Configuration](#configuration)
  - [Hotkeys](#hotkeys)
  - [Running](#running)
- [Calibration](#calibration)
- [Building the Firmware](#building-the-firmware)

---

## How It Works

```
[MPU-6050] ──I2C──► [STM32F103C8T6] ──UART 115200──► [USB-UART adapter] ──► [PC: read_sensor.py] ──► Windows mouse cursor
                          │
                     [PB12] Left-click button
                     [PB13] Right-click button
```

1. The STM32 reads raw gyroscope data from the MPU-6050 at **100 Hz** via I2C burst read.
2. Calibration offsets (computed at boot) are subtracted and the corrected X/Z values are sent over UART as a compact ASCII packet.
3. The Python script on the PC parses every packet, converts angular-rate to pixel displacement, and calls `user32.mouse_event` to move the cursor and fire click events.

---

## Hardware

| Part | Notes |
|---|---|
| STM32F103C8T6 ("Blue Pill") | Main MCU, bare-metal firmware |
| MPU-6050 (or MPU-6500) | 6-axis IMU — only gyro axes used |
| 2 × momentary push-buttons | Left click (PB12) and right click (PB13) |
| USB-to-UART adapter (e.g. CH340) | Connects PA9 TX to the PC |
| 3.3 V power supply | From the Blue Pill onboard regulator |

### Wiring

```
MPU-6050          STM32F103
─────────         ──────────
VCC       ──────► 3.3 V
GND       ──────► GND
SDA       ──────► PB7  (I2C1 SDA, open-drain)
SCL       ──────► PB6  (I2C1 SCL, open-drain)
AD0       ──────► GND  (I2C address = 0x68)

Button 1 (left click)
  One leg ──────► PB12
  Other   ──────► GND   (internal pull-up enabled)

Button 2 (right click)
  One leg ──────► PB13
  Other   ──────► GND   (internal pull-up enabled)

USB-UART adapter
  RX      ──────► PA9   (USART1 TX, 115200 8N1)
  GND     ──────► GND
```

---

## Repository Layout

```
stm32-air-mouse/
├── firmware/
│   └── main.c          # Bare-metal STM32 firmware (single translation unit)
├── tools/
│   └── read_sensor.py  # Windows PC companion script
├── .gitignore
└── README.md
```

---

## Firmware

### Peripheral Configuration

| Peripheral | Settings |
|---|---|
| CPU clock | 8 MHz (internal HSI, no PLL) |
| SysTick | 1 ms tick (`system_ms` counter) |
| USART1 (PA9) | 115200 baud, 8N1, TX only |
| I2C1 (PB6/PB7) | 100 kHz, open-drain, with bus-recovery on errors |
| MPU-6050 | PLL clock source, 100 Hz sample rate, 42 Hz DLPF, ±250 °/s |
| PC13 | Onboard LED — active LOW, used for error blink codes |

### Startup Sequence

1. Enable RCC clocks for GPIOB, GPIOC, I2C1, GPIOA, USART1.
2. Configure PC13 as push-pull output → LED OFF.
3. Initialise buttons on PB12 / PB13 with internal pull-ups.
4. Initialise USART1 at 115200 baud.
5. **I2C bus recovery** — manually toggle SCL/SDA to release a stuck slave, then software-reset the I2C peripheral.
6. Re-initialise I2C1 (100 kHz).
7. Read `WHO_AM_I` register (`0x75`); halt with blink code **8** if unrecognised.
8. Wake the MPU from sleep (`PWR_MGMT_1 = 0x00`).
9. Configure MPU registers (sample rate, DLPF, gyro range).
10. Start SysTick.
11. **Startup calibration** (see [Calibration](#calibration)).
12. Enter main loop.

### Main Loop

Runs at **100 Hz** (one iteration every 10 ms, timed with `system_ms`):

1. **Read gyro** — 6-byte I2C burst from register `0x43` (GYRO_XOUT_H).
2. **Subtract offsets** — remove the calibration bias from X and Z axes.
3. **Read buttons** — sample PB12 (left) and PB13 (right).
4. **Transmit packet** — send `GX=… GZ=… B=… R=…\r\n` over UART.

A heartbeat message (`LOOP ALIVE`) is printed once per second.

**Fault tolerance:** if the I2C burst read fails, the firmware attempts `I2C_RecoverAndRestart()` and skips the packet. After **20 consecutive failures** it halts with blink code **11**.

### Error Codes

The onboard LED blinks a repeated count to indicate errors during init:

| Blink count | Meaning |
|---|---|
| 7 | `WHO_AM_I` register read failed |
| 8 | Unrecognised `WHO_AM_I` value |
| 9 | MPU configuration write failed |
| 11 | Gyro I2C read failed 20 times in a row |
| 12 | I2C read failure during startup calibration |

### UART Packet Format

```
GX=<int16> GZ=<int16> B=<0|1> R=<0|1>\r\n
```

| Field | Description |
|---|---|
| `GX` | Gyro X raw value minus calibration offset |
| `GZ` | Gyro Z raw value minus calibration offset |
| `B` | Left button state (1 = pressed) |
| `R` | Right button state (1 = pressed) |

Example line:
```
GX=-42 GZ=318 B=0 R=0
```

---

## PC Companion Script

[`tools/read_sensor.py`](tools/read_sensor.py) — **Windows only** (uses `ctypes.windll.user32`).

### Dependencies

```bash
pip install pyserial
```

### Configuration

Edit the constants at the top of `read_sensor.py`:

| Constant | Default | Description |
|---|---|---|
| `PORT` | `"COM5"` | Serial port of the USB-UART adapter |
| `BAUD` | `115200` | Must match firmware |
| `SCALE` | `131.0` | Gyro counts per °/s (correct for ±250 °/s range) |
| `X_DIR` / `Y_DIR` | `-1` / `1` | Flip cursor axes if needed |
| `DEADZONE` | `1.5` °/s | Rotation below this is ignored |
| `GAIN_X` / `GAIN_Y` | `0.12` | px per (°/s) per packet — main speed knob |
| `MAX_STEP` | `20.0` px | Hard cap on pixels moved per packet |
| `CAL_PACKETS` | `150` | Packets averaged during PC-side calibration (~1.5 s) |
| `STILL_DPS` | `2.0` | Below this = board is still → bias tracking active |
| `BIAS_RATE` | `0.01` | Low-pass coefficient for runtime drift correction |
| `DEBOUNCE` | `3` packets | ~30 ms button debounce |
| `SEND_HZ` | `125` | Rate at which accumulated pixels are dispatched |

### Hotkeys

These work globally (no need to focus the console window):

| Key | Action |
|---|---|
| **F8** | Toggle mouse movement on/off |
| **F9** | Recalibrate (keep the board still) |
| **F10** | Quit |

### Running

```bash
python tools/read_sensor.py
```

On launch the script calibrates automatically — keep the board completely still for ~1.5 seconds until `Running.` is printed.

---

## Calibration

Calibration happens in **two stages**:

### Stage 1 — Firmware (on every power-up)

- Waits 1 second for the sensor to settle.
- Collects **1000 gyro samples** (one every 2 ms).
- Computes the mean of X and Z axes as the hardware offset.
- If the sample spread exceeds **800 counts** (board was moving), it retries — up to **10 attempts**.
- The computed offsets are stored in RAM and subtracted in the main loop before transmitting.

### Stage 2 — Python script (on every launch / F9)

- Averages **150 packets** (~1.5 s).
- Applies the same spread check; retries if the board moved.
- After initial calibration, a **runtime bias tracker** slowly adjusts the bias while the board is still (exponential low-pass, α = 0.01) to compensate for thermal drift.

---

## Building the Firmware

The firmware is a single C file with no external dependencies beyond a C99 compiler.

**With arm-none-eabi-gcc + a minimal linker script:**

```bash
arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -O2 -std=c99 \
  -Wl,-T,stm32f103.ld \
  firmware/main.c -o airmouse.elf

arm-none-eabi-objcopy -O binary airmouse.elf airmouse.bin
```

Flash `airmouse.bin` with ST-Link / STM32CubeProgrammer / `openocd`, or via the built-in UART bootloader (BOOT0 = 1).

> **Note:** No startup file or CMSIS headers are required. The vector table and SysTick handler are defined inline in `main.c`.
