# Updating Custom INAV Firmware

This repo is a fork of [iNavFlight/inav](https://github.com/iNavFlight/inav) with a custom dual LED strip patch for the CoreWing F405 Wing V2.

## What's customized

Branch `corewing-dual-led-strip` adds a second WS2812 LED strip output on PB15 (S11 pad) using TIM1_CH3N, in addition to the existing LED pad (PA8/TIM1_CH1). Both strips are independently controllable via the `led_strip_2_offset` CLI setting.

Modified files:
- `src/main/drivers/light_ws2811strip.c` — driver refactored for multi-strip
- `src/main/drivers/light_ws2811strip.h` — indexed API, LED_STRIP_COUNT
- `src/main/fc/settings.yaml` — `led_strip_2_offset` setting
- `src/main/io/ledstrip.c` — strip 2 update logic
- `src/main/target/COREWINGF405WINGV2/target.c` — PB15 reassigned to TIM1_CH3N
- `src/main/target/COREWINGF405WINGV2/target.h` — USE_LED_STRIP_2, WS2811_PIN_2

## Quick update

Run from the repo root:

```bash
./update-and-build.sh
```

This will:
1. Check if a new INAV version is available upstream
2. Show you what changed (version, commit count)
3. Rebase your custom changes on top of the new version
4. Build the firmware
5. Give you the flash command

## Flashing

After building, put the board in DFU mode and flash:

```bash
# Hold BOOT button on the board, plug in USB, then:
dfu-util -a 0 -s 0x08000000:leave -D build/bin/COREWINGF405WINGV2.bin
```

Note: flashing erases settings. After flashing, reconfigure via CLI:

```
set led_strip_2_offset = 4
led 0 0,0::C:2
led 1 1,0::C:2
led 2 2,0::C:2
led 3 3,0::C:2
led 4 0,1::C:10
led 5 1,1::C:10
led 6 2,1::C:10
led 7 3,1::C:10
save
```

## Manual update (if the script fails)

If the rebase has conflicts:

```bash
# 1. The script will stop and show conflicting files
# 2. Edit each conflicting file, resolve the markers (<<<< ==== >>>>)
# 3. Stage resolved files
git add <resolved-file>

# 4. Continue rebase
git rebase --continue

# 5. Push and build
git push origin corewing-dual-led-strip --force-with-lease
cd build && make COREWINGF405WINGV2
```

## Git setup

| Remote     | Points to                           |
|------------|-------------------------------------|
| `origin`   | `roandegraaf/inav` (your fork)      |
| `upstream` | `iNavFlight/inav` (official repo)   |

| Branch                      | Purpose                        |
|-----------------------------|--------------------------------|
| `master`                    | Tracks upstream master         |
| `corewing-dual-led-strip`   | Your customizations (work here)|
