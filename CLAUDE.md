# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

INAV is navigation-capable flight controller firmware for multirotor, fixed-wing, rovers, and other RC vehicles. Written in C (C99/C11) for STM32 F4/F7/H7 and AT32 microcontrollers. See `AGENT.md` for detailed architecture documentation.

## Build Target

The default build target is **COREWINGF405WINGV2**. Always use this target unless told otherwise.

## Build Commands

```bash
# Build firmware (from repo root)
mkdir -p build && cd build
cmake -DTOOLCHAIN=arm-none-eabi ..
make COREWINGF405WINGV2

# Build using Docker (handles all dependencies)
./build.sh COREWINGF405WINGV2

# Build for unit tests only
mkdir -p build_test && cd build_test
cmake -DTOOLCHAIN=none ..
make
make check                    # run all unit tests

# Run a single unit test (after test build)
./src/test/unit/battery_unittest

# SITL (Software-In-The-Loop) simulation build
mkdir -p build_sitl && cd build_sitl
cmake -DSITL=ON -DTOOLCHAIN=host ..
make

# Regenerate CLI docs after changing settings.yaml
python3 src/utils/update_cli_docs.py
```

CI builds use `-DWARNINGS_AS_ERRORS=ON`; all code must compile cleanly with `-Wall -Wextra`.

## Flashing

```bash
# Convert ELF to binary
tools/arm-gnu-toolchain-13.2.rel1/bin/arm-none-eabi-objcopy -O binary build/bin/COREWINGF405WINGV2.elf build/bin/COREWINGF405WINGV2.bin

# Put board in DFU mode (hold BOOT button, plug USB), then:
dfu-util -l                    # verify DFU device detected
dfu-util -a 0 -s 0x08000000:leave -D build/bin/COREWINGF405WINGV2.bin
```

Note: flashing erases settings — reconfigure via CLI after flash.

## Architecture

**Core loop**: Gyro read (highest priority) → PID calculation → Mixer → Motor output. Tasks scheduled cooperatively via priority-based scheduler defined in `src/main/fc/fc_tasks.c`.

**Key subsystems** (all under `src/main/`):
- `fc/` — Flight controller core, initialization (`fc_init.c`), MSP handlers (`fc_msp.c`)
- `flight/` — PID controllers, mixer, rate profiles
- `navigation/` — Waypoints, RTH, position hold, altitude hold
- `sensors/` — Gyro, accelerometer, barometer, GPS, compass processing
- `drivers/` — Hardware abstraction (SPI, I2C, UART, timers, DMA)
- `rx/` — Receiver protocols (CRSF, SBUS, IBUS, etc.)
- `config/` — Parameter Group (PG) system for persistent configuration
- `target/` — Board-specific pin definitions and features (200+ boards)

**Configuration system**: Uses Parameter Groups (PG) with versioned EEPROM storage. When modifying PG structures, increment the version number in the `PG_REGISTER` macro. Parameter group IDs live in `config/parameter_group_ids.h`. CLI settings are defined in `src/main/fc/settings.yaml`.

**Board targets**: Each board is defined in `src/main/target/BOARDNAME/target.h` with pin assignments, enabled features (`#define USE_*`), and MCU variant. Build target is declared via CMake functions like `target_stm32f722xe(BOARDNAME)`.

## Coding Conventions

- **4 spaces** indentation, K&R braces, `#pragma once` for headers
- Types: `_t` suffix (`gyroConfig_t`). Enums: `_e` suffix (`portMode_e`)
- Functions: camelCase verbs (`gyroInit()`). Booleans: question format (`isOkToArm()`)
- Memory attributes: `FASTRAM` for hot paths, `DMA_RAM` for DMA buffers (H7/AT32), `STATIC_UNIT_TESTED` for testable statics
- No dynamic allocation (`malloc`); all memory is statically allocated
- Feature-gated code uses `#ifdef USE_FEATURE` / `#if defined(STM32F4)` patterns

## Fork & Custom Branch

This is a fork of `iNavFlight/inav` with dual LED strip support.

- `origin` → `roandegraaf/inav` (fork)
- `upstream` → `iNavFlight/inav` (official)
- Branch `corewing-dual-led-strip` contains all customizations
- See `UPDATE.md` for full details on the custom patch and update process

## Updating to New INAV Releases

```bash
./update-and-build.sh
```

Checks for upstream updates, rebases custom changes, builds, and outputs the flash command. If no update is available, it asks before building.

## Branching (upstream)

- PRs target `maintenance-9.x` (current) or `maintenance-10.x` (breaking changes)
- Never target `master` directly; it receives merges from maintenance branches
