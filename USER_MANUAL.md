# Retro Console S3 - Dual Firmware User Manual

This manual explains how to compile, flash, and operate the dual-firmware architecture on the ESP32-S3 (N16R8) Retro Console. 

The console runs two independent systems that share the same hardware:
1. **Console OS (`ADF_ES8388_Test2bkp`)**: The primary operating system featuring the Music Player, FM Radio, File Browser, and Native Games.
2. **Retro-Go (`retro-go`)**: A dedicated gaming environment for retro emulators (NES, SNES, Gameboy, PC Engine) and Doom, ported specifically to this hardware.

---

## 1. Prerequisites

- **Framework**: ESP-IDF v5.3.4
- **Hardware**: ESP32-S3 (N16R8), ILI9341 2.8" SPI Display, ES8388 Audio Codec, SD Card (4-bit SDMMC), KY-040 Rotary Encoder, and 74HC165 Shift Register.
- **Port**: Change `COM7` in the commands below to match your actual ESP32-S3 serial port if it differs.

---

## 2. Flashing Phase 1: The Console OS

The Console OS manages the partition table, bootloader, and primary features. It must be flashed using standard ESP-IDF commands.

1. Open an ESP-IDF PowerShell terminal.
2. Navigate to the Console OS directory:
   ```powershell
   cd C:\Users\Dell\esp\projects\ADF_ES8388_Test2bkp
   ```
3. Build and flash the firmware:
   ```powershell
   idf.py -p COM7 flash monitor
   ```
   *Note: This command will automatically compile the bootloader, apply the custom `partitions.csv` layout, and write the Console OS to the `factory` partition (`0x20000`).*

---

## 3. Flashing Phase 2: Retro-Go Apps

**CRITICAL WARNING:** Never run `idf.py flash` inside the `retro-go` directory! Doing so will overwrite the bootloader and Console OS partitions with dummy layouts, causing endless bootloops.

Instead, Retro-Go applications are compiled individually and flashed manually to their designated OTA partition offsets using `esptool.py`.

1. Navigate to the `retro-go` directory:
   ```powershell
   cd C:\Users\Dell\esp\projects\retro-go
   ```
2. Build the applications (we have created helper scripts for this):
   ```powershell
   .\retro-core\build_retro_core.bat
   .\prboom-go\build_prboom_go.bat
   ```
   *(Alternatively, you can `cd` into those directories and run `idf.py build` manually).*
3. Flash the compiled binaries directly into the flash memory using `esptool.py`:
   ```powershell
   python C:\Users\Dell\esp\v5.3.4\esp-idf\components\esptool_py\esptool\esptool.py -p COM7 -b 460800 --chip esp32s3 write_flash 0x320000 launcher\build\launcher.bin 0x420000 retro-core\build\retro-core.bin 0x720000 prboom-go\build\prboom-go.bin
   ```
   * Partition Map Reference:
     * `0x320000`: Launcher (OTA 0)
     * `0x420000`: Emulators (OTA 1 - `retro-core`)
     * `0x720000`: Doom (OTA 4 - `prboom-go`)

---

## 4. Operating the Console

### Switching Between Environments
- **Entering Retro-Go:** From the Console OS Home Menu, navigate to **Games**, select **RETRO-GO**, and press ENTER. The ESP32-S3 will seamlessly reboot into the Retro-Go launcher.
- **Returning to Console OS:** 
  - From the Retro-Go Launcher: Press the Menu/Option button, select **About/Options**, then select **Quit to Console OS**.
  - From a Game: Open the emulator/game menu and select the Exit option. 

### Core Controls & Keybindings

#### Global Hardware
- **Rotary Encoder (KY-040):** Rotate CW to increase volume, CCW to decrease volume.
- **Audio Output:** All sound is routed through the ES8388 hardware codec to the connected speakers/headphones.

#### Console OS - Music Player
- **Play / Pause:** Press `ENTER`.
- **Next / Previous Track:** Press `RIGHT` / `LEFT`.
- **Volume:** Use the rotary encoder or `UP` / `DOWN` buttons. (Global software volume scaling allows independent adjustments without affecting hardware codec baselines).
- **Shuffle Mode:** Long-press `ENTER` (hold for >600ms).
- **Thumbnail / Visualizer Toggle:** Press `A`. (Includes optimized JPEG caching and independent FFT visualization logic).
- **Exit to List:** Press `ESCAPE`. (Returning to the list highlights the currently playing track).

#### Console OS - Audio FX
- Adjust 10-band Equalizer and Pitch/Speed shift using `UP`/`DOWN` to change frequency bands, and `LEFT`/`RIGHT` to modify gains/values.

#### Console OS - RSVP (Text Reader)
- Smooth text scrolling: Hover over long `.TXT` files in the menu to automatically scroll the filename horizontally.
- Read files: Press `ENTER` to open.
- Delete files: Press `B` over any file in the list to trigger a deletion confirmation popup.

#### Console OS - Download (File Transfer)
- WAP File Transfer: Press `ENTER` on WAP to launch the Wi-Fi Access Point (`RetroConsole`). Connect your phone to it and go to `192.168.4.1` to upload files directly to the SD card.
- Automatic Time Sync: The moment you open the WAP upload page on your phone, your console's RTC clock automatically syncs to your phone's exact local time!

#### Retro-Go - DOOM (PrBoom-Go)
We custom-mapped the controls to make Doom fully playable on the console without a keyboard:
- **Movement / Turning:** D-Pad (`UP`, `DOWN`, `LEFT`, `RIGHT`)
- **Fire Weapon:** `START` / `ENTER`
- **Use / Open Doors:** `START` / `ENTER`
- **Weapon Cycle:** `A`
- **Strafe Left:** `MENU`
- **Strafe Right:** `B`
- **Game Menu / Escape:** Press **`MENU` + `B` simultaneously**.
- **Exit Game:** Inside menus, press `ENTER` to confirm "y" when asked to quit.
