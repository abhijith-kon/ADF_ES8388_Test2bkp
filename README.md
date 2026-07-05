# Retro Console OS (ESP32-S3 N16R8)

## Purpose
Building a high-performance retro console based on the Retro-Go architecture, currently focusing on audio (ES8388), 4-bit SDMMC, ILI9341 display, and custom inputs.

## Hardware Pinout
| Peripheral | Signal | GPIO Pin | Notes |
|------------|--------|----------|-------|
| **I2C**    | SDA    | 4        | ES8388, DS3231, TEA5657 |
|            | SCL    | 5        | Shared Bus |
| **I2S**    | MCLK   | 47       | ES8388 Master Clock |
|            | BCLK   | 15       | |
|            | WS     | 16       | |
|            | DOUT   | 17       | Corrected for Hardware Swap |
|            | DIN    | 18       | Corrected for Hardware Swap |
| **Display**| CS     | 10       | 10k External Pull-up |
| (ILI9341   | DC     | 9        | GoldenMorning 2.8" TFT |
| 240x320)   | RST    | 14       | 10k External Pull-up |
|            | MOSI   | 13       | SPI2_HOST @ 16MHz |
|            | SCK    | 12       | |
|            | LED    | 3.3V     | Hardwired On |
| **SDMMC**  | CLK    | 38       | 4-bit Mode |
|            | CMD    | 39       | |
|            | D0     | 40       | |
|            | D1     | 41       | |
|            | D2     | 42       | |
|            | D3     | 21       | |
| **Inputs** | EN_CLK | 1        | KY-040 Encoder |
|            | EN_DT  | 2        | |
|            | SR_CLK | 3        | 74HC165 Clock (CP) |
|            | SR_QH  | 6        | 74HC165 Serial Data Out (Q7) |
|            | SR_PL  | 7        | 74HC165 Parallel Load (PL) |

## Button Mapping (74HC165 Shift Register)
Buttons are active LOW (0 = pressed). Bit order is MSB-first from Q7 output.

| Bit | Mask (hex) | Binary State   | Button    | Function          |
|-----|-----------|----------------|-----------|-------------------|
| D7  | 0x7F      | `01111111`     | BTN_A     | Play / Pause      |
| D6  | 0xBF      | `10111111`     | BTN_UP    | Next Track        |
| D5  | 0xDF      | `11011111`     | BTN_DOWN  | Previous Track    |
| D4  | 0xEF      | `11101111`     | BTN_LEFT  | Previous Track    |
| D3  | 0xF7      | `11110111`     | BTN_B     | Stop              |
| D2  | 0xFB      | `11111011`     | BTN_ESCAPE| Stop              |
| D1  | 0xFD      | `11111101`     | BTN_ENTER | (Reserved: Menu)  |
| D0  | 0xFE      | `11111110`     | BTN_RIGHT | Next Track        |

**KY-040 Rotary Encoder:** CW = Volume Up (+5), CCW = Volume Down (-5)

## Project Status & Memory
### Tried and Successful
- **I2S/I2C Conflict Bypass:** Enabled `CONFIG_I2S_SKIP_LEGACY_CONFLICT_CHECK=y` and `CONFIG_I2C_SKIP_LEGACY_CONFLICT_CHECK=y`. Removed all references to new `esp_driver_i2s` headers to allow ADF's legacy drivers to work seamlessly. A `fullclean` was required to apply these changes globally.
- **SDMMC 4-Bit:** Mounted SD card successfully on pins 38-42 in `SD_MODE_4_LINE`.
- **Audio HAL & Pipeline:** Initialized ES8388 codec (PCB Artists module) and ADF I2S stream. MP3 decoding and playback from SD card is verified and working (music audible). Root cause of original static/jitter was `board_i2s_pin_t` struct field order not matching IDF5's `i2s_std_gpio_config_t` — the ADF's `i2s_stream_idf5.c` does a raw `memcpy` of the pin struct into `gpio_cfg`, so `mck_io_num` MUST be the first field. Reordered from `{bck, ws, dout, din, mck}` to `{mck, bck, ws, dout, din}`. Also removed unnecessary hardcoded I2S overrides; the MP3 decoder auto-negotiates sample rate and bit width from file headers.
- **ES8388 Mixer Fix (Static Noise):** Root cause identified: ADF's `es8388_init()` hardcodes DACCONTROL17=0x90 and DACCONTROL20=0x90, which route analog input pins (LIN1/RIN1) into the DAC output mixer at 0dB. On the PCB Artists module (no mic connected), floating inputs inject noise into the output path. Fixed by writing DACCONTROL17=0x80 and DACCONTROL20=0x80 after codec init to disconnect analog inputs from the mixer. ADC also powered down (ADCPOWER=0xFF) since only playback (DECODE mode) is needed.
- **I2S Buffer Tuning:** Increased DMA descriptors (3→6), DMA frames per descriptor (312→480), and ring buffer (8KB→32KB) to reduce audio underrun pops.
- **Track Switching:** Fixed pipeline state management — `play_track()` now stops/terminates/resets the pipeline before starting a new track, regardless of play/pause state. Uses a `pipeline_has_run` guard to skip cleanup on first call (avoids harmless "Without stop" warnings).
- **Pipeline Event Listener:** Added `audio_event_iface` to consume ADF pipeline events and auto-advance to the next track when the current one finishes. Resolves "no space in external queue" warnings.
- **Dynamic Volume Control:** Volume is tracked locally (eliminates I2C read via `audio_hal_get_volume`). Only `audio_hal_set_volume` (I2C write) is called on encoder rotation (±5 per step). This halves the I2C traffic vs the previous read+write approach.
- **Display Output:** ILI9341 display initialized via `rg_display` driver. SPI bus at 16MHz, BGR element order, landscape orientation via `swap_xy(true)` and `mirror(false, false)`. UI overlay drawn via `rg_gui` (menu system from `ui.c`).
- **74HC165 Shift Register Buttons:** Restored 8-button input via 74HC165 on GPIO 3/6/7. Button protocol verified with Arduino IDE test sketch. 250ms debounce via `esp_timer_get_time()`. Mapped to play/pause, next/prev track, stop, and menu enter.
- **Build System Fix:** Converted `my_board` component from legacy `register_component()` to modern `idf_component_register()` for IDF 5.x compatibility. Also used explicit relative include path for `board.h` to prevent ADF's opaque forward declaration from shadowing the project's full struct definition.
- **Audio Sample Rate Fix:** Resolved the "slowed down" audio issue by adding an `AEL_MSG_CMD_REPORT_MUSIC_INFO` event handler. The I2S clock now dynamically updates via `i2s_stream_set_clk()` to match the MP3 file's exact sample rate instead of defaulting to 44.1kHz.
- **Button Remapping:** Remapped the 74HC165 shift register inputs to match the user's expected layout: UP/DOWN act as volume controls, ENTER is play/pause, and LEFT/RIGHT handle track navigation.
- **UI Redraw & Flickering Fixes:** Stopped full screen redraws on every frame by restricting `rg_gui_clear()` to the first frame. Implemented double-buffering for individual app icons using a local 72x72 buffer array, entirely avoiding `memset()` overhead by filling the pixels procedurally. Limited icon redraws to a maximum of 2 icons per frame (previous and current) to prevent visual artifacting.
- **DMA Tearing & Flashing Fixes:** Resolved severe screen flashing and line-drawing artifacts by adding a 50 FPS (20ms) redraw throttle via `esp_timer_get_time()`. This ensures the SPI DMA queue empties out before static buffers are mutated on the next frame. Upgraded the drawing buffers to a quad-buffer system (`node_bufs[4]`) for rock-solid stability during rapid navigation.
- **Infinite Redraw Loop Fix:** Stopped a bug where the display was continuously redrawing the same 2 apps forever by implementing an `icon_dirty` flag array. The UI now precisely redraws only icons that are actively animating and stops executing completely once they reach IDLE.
- **UI Ghosting & Clipping Fixes:** Eliminated text ghosting on app names by restoring a precisely sized `104x20` black bounding box that perfectly clears old characters without clipping the radii of neighboring IDLE icons. Fixed a bug where the system clock triggered massive 240x80 redraws every single frame by properly validating the minute change via `localtime()`.
- **Portrait UI Clipping Fix:** Fixed an issue where new screens (like the music player) only covered 3/4ths of the home menu by updating `rg_gui_clear()` to cover a full 320x320 area, properly clearing portrait mode orientations.
- **Audio Clicking Noise Fix:** Fixed a frequent clicking noise during music playback caused by I2S starvation/SD card latency. Resolved by increasing the `out_rb_size` of the `fatfs_stream_reader` from the default 8KB to 32KB to match the I2S writer's buffer size.
- **Horizontal Line Artifacts Fix:** Root cause: the "clear-then-draw" pattern in `home_ui_draw()` drew a 240×80 black rect (10 separate DMA bands) before drawing text on top, creating visible horizontal flash artifacts. Fixed by introducing `rg_gui_draw_text_box()` which renders text + background into a single buffer and blits once — zero flicker, single DMA transaction per text area. Eliminated all `rg_gui_draw_rect` clearing calls from the home UI render path.
- **RGB565 Byte-Swap Fix:** `rg_gui_draw_rect` and `rg_gui_draw_text` were writing raw (un-swapped) RGB565 values, but ILI9341 expects big-endian while ESP32-S3 is little-endian. This was invisible for BLACK/WHITE but corrupted any other color (e.g., music app background). All GUI primitives now consistently byte-swap.
- **DMA Buffer Safety:** `rg_gui_draw_rect` previously used dynamic malloc that could be freed/reallocated while DMA was referencing the old buffer. Replaced with a pre-allocated static 4KB array that is never freed. `rg_gui_draw_text` replaced malloc/free-per-call with a persistent static buffer.
- **Portrait-First Display Init:** Display was initializing in landscape mode, clearing, then switching to portrait via MADCTL — the transition could leave edge pixels in an undefined state (right-edge brightness). Now initializes directly in portrait mode with explicit `esp_lcd_panel_set_gap(0,0)`.
- **Date Text Color Bug:** `draw_centered_text()` helper hardcoded `RG_COLOR_WHITE`, overriding the gray (`0xAA`) color intended for the date text. Removed the helper; date now renders correctly in gray via direct `rg_gui_draw_text_box` calls.
- **SPI DMA Queue Corruption (Root Cause of Display Lines):** Reading the ESP-IDF `esp_lcd_panel_io_spi.c` source revealed that `trans_queue_depth = 10` caused pixel DMA to be non-blocking (`spi_device_queue_trans`). The last band's DMA was still reading from the buffer when the function returned, and subsequent draw calls corrupted the buffer mid-transfer — causing random horizontal lines, dancing artifacts, and a persistent vertical line. Fixed by setting `trans_queue_depth = 1` (fully blocking SPI) and adding `rg_display_drain()` calls after each draw operation for belt-and-suspenders DMA safety. Also added `__attribute__((aligned(4)))` to all static DMA buffers.
- **Music App Rendering Fix:** Replaced `rg_gui_clear()` + `rg_gui_draw_text_center()` pattern with `rg_gui_draw_text_box()` using matching dark-blue background color. Eliminated visible black rectangles around text (from `draw_text_center`'s hardcoded 0x0000 background). Added partial `draw_music_status()` for play/pause toggle — only redraws the status text box instead of the entire screen.
- **Music App Partial Redraw on Track Change:** Split `draw_music_ui()` into `draw_music_full_ui()` (entry only, full 240×320 clear) and `draw_music_track_info()` (track changes, redraws only the track text box + status). Eliminates the unnecessary full-screen clear on every track skip that caused horizontal line artifacts bleeding into other screens.
- **DMA Drain on App Transitions:** Added `rg_display_drain()` calls in `main.c` before clearing the screen when returning from Music or Games apps to Home. Ensures all in-flight DMA transfers from the previous app complete before the home screen is drawn, preventing stale pixel data from bleeding through.
- **Games Page Clock Redraw Optimization:** `ui_update()` now only redraws the top-bar clock text when the time string actually changes (string comparison), instead of unconditionally every second. Eliminates unnecessary SPI/DMA churn on the games page.
- **FATFS Long Filename (LFN) Support:** Enabled `CONFIG_FATFS_LFN_HEAP=y` with `CONFIG_FATFS_MAX_LFN=255` in sdkconfig. FATFS now successfully returns and displays full song titles instead of abbreviated 8.3 short names.
- **SD Card Song Loading & Lazy Init:** Playlist scanning moved from boot (`app_music_init`) to when the app opens (`app_music_start`), and capacity increased from 100 to 1000 tracks via heap-allocated `calloc`. Successfully tested loading all 900+ songs without crashing.
- **Button Navigation Mapping:** Perfected controls so UP/DOWN scroll through the song list and LEFT/RIGHT directly skip to and play the previous/next track.

### Known Issues / In-Progress
- **UI/UX Aesthetics & Design:** The current UI layout is functional but basic ("mid"); a comprehensive visual and UX redesign will be planned and implemented later based on user instructions.
- **Home Menu Icon Ghosting / Outline Artifacts:** While the black box artifact was resolved by dynamic circle sizing, deselecting an icon now leaves behind ring/line artifacts (ghost outlines) around the last selected icon showing its intermediate growth/shrink states.
- **Screen Flashing on Redraws:** Brief white screen flashes / flickering on redraws still persist on both the Home Page and the Music App scrollable list, despite partial slot dirty tracking.
- **Slow SD Card Scanning / Background Loading Needed:** Scanning 900+ songs synchronously upon opening the music app causes a noticeable loading delay. This needs to be modified to load in the background (asynchronously), opening the UI as soon as the first 100 songs are found while continuing to scan the rest.
- **Slow & Choppy Text Scrolling:** The auto-scrolling for long song titles on the selected list item is slow and choppy. Furthermore, scrolling needs to be implemented for the mini player bar ("now playing" song title) at the bottom as well.
- **I2C Volume Control Bug & Stereo Imbalance:** `I2C transaction unexpected nack detected` errors continue during encoder volume adjustments. The 3-attempt retry loop did not resolve this hardware/bus-level issue, causing erratic earphone stereo behavior (one earphone volume changing independently or one channel randomly going deaf/mute).
- **Pipeline File Abort Warnings:** When skipping tracks rapidly or stopping playback, warnings like `AUDIO_ELEMENT: OUT-[file] AEL_IO_ABORT` and `MP3_DECODER: Output aborted, -3` appear in the log. This is due to the audio pipeline being stopped abruptly while elements are still processing or blocking on I/O. The system handles it gracefully by resetting for the next track, but a cleaner tear-down sequence might eliminate these warnings.

## ES8388 Module Setup (PCB Artists)
### Key Configuration
- **I2C Address:** 0x20 (8-bit format, CE=LOW). The ADF `es8388.h` hardcodes `ES8388_ADDR 0x20`. Arduino I2C scanners may show 0x10 (7-bit representation) — this is the same address.
- **Codec Mode:** `AUDIO_HAL_CODEC_MODE_DECODE` (DAC only), ES8388 in slave mode, ESP32-S3 is I2S master. Changed from BOTH→DECODE since ADC is not needed for playback.
- **I2S Format:** Standard Philips I2S, 16-bit, sample rate auto-negotiated by decoder.
- **MCLK:** GPIO 47 — ESP32-S3 allows any GPIO for MCLK (unlike original ESP32). MCLK/LRCK ratio = 256 (single speed mode).
- **DAC Output:** `AUDIO_HAL_DAC_OUTPUT_ALL` (LOUT1/ROUT1/LOUT2/ROUT2 all enabled).
- **PA:** No external PA — `PA_ENABLE_GPIO = -1`.
- **Mixer Fix:** After codec init, DACCONTROL17 and DACCONTROL20 are patched from 0x90→0x80 to disconnect floating analog inputs from the output mixer. ADCPOWER set to 0xFF to fully power down the ADC path.

### PCB Artists vs LyraT Differences
| Aspect | LyraT v4.3 | PCB Artists Module |
|--------|-----------|-------------------|
| CE Pin | Pulled LOW (0x20) | Typically LOW (0x20) — verify schematic |
| I2C Pull-ups | Onboard | May need external 4.7kΩ or internal pull-ups |
| PA | NS4150 on GPIO 21 | No onboard PA |
| MCLK | GPIO 0 | GPIO 47 (our config) |
| MICBIAS | Available | Not needed — ADC powered down |
| Mixer | ADC inputs routed to output (mic passthrough) | Analog inputs disconnected from mixer (no mic) |

### Dead Code Note
`components/my_board/my_codec_driver/new_codec.c` and `AUDIO_NEW_CODEC_DEFAULT_HANDLE` are **unused dead code**. The board init (`board.c`) correctly uses `AUDIO_CODEC_ES8388_DEFAULT_HANDLE` (the real ADF ES8388 driver).

## Software Architecture (Current Focus)
- **Home Menu (`rg_gui`):** The primary home screen will be built natively using `rg_gui` (avoiding the overhead of LVGL). It will feature a grid or list layout for apps.
- **Game Sub-Menu:** A dedicated "Game App" launcher from the home menu that will host:
  - `Retro-OS` (the media player and retro-go launcher)
  - `Tetris` (custom built via `rg_gui`)
  - `2048` (custom built via `rg_gui`)
  - `Pong` (custom built via `rg_gui`)
- **`main/play_mp3_control_example.c`**: Audio orchestration, SD card init, and input management. Will be adapted to hand off control to the `rg_gui` home menu upon boot.
- **`components/retro-go/rg_display.c`**: Core ILI9341 SPI LCD driver. Renders the `rg_gui` primitives (rectangles, text, etc).
- **`components/input_manager`**: Handles 74HC165 shift register and KY-040 encoder. Will feed inputs directly into the `rg_gui` menu state machine.

## Game Session Architecture (from retro-go research)
The retro-go project uses a multi-binary architecture:
- **Launcher binary**: App selection menu with ROM browser, favorites, cover art
- **Emulator binaries**: Separate binaries per console (NES, GB/GBC, SMS, Genesis, SNES, PC Engine, DOOM, etc.)
- **Game session lifecycle**: `rg_emu_start()` → `rg_emu_loop()` → save state on exit
- **Save system**: SRAM saves (.srm) + full emulator state snapshots with preview thumbnails
- **Input**: `rg_input` API with `RG_KEY_*` constants, Core 1 I/O polling, Core 0 emulation
- **Display**: Frame buffer management with scaling/filtering, in-game menu overlay via `rg_gui_dialog()`

## Reference Documentation (MCP Servers)
- `retro-go Docs`: Core OS architecture, game sessions, emulator framework.
- `s3-node-repo Docs`: LVGL-based OS with radial app menu, DOOM integration, display/input patterns.
- `esp-adf Docs`: Audio pipeline management, ES8388 driver, codec HAL integration.

## Environment Paths
- **ADF:** `C:\Users\Dell\esp\esp-adf`
- **IDF:** `C:\Users\Dell\esp\v5.3.4\esp-idf`




next prompt to fix:
The home launcher currently has two rendering artifacts that need to be fixed. Please analyze the rendering pipeline and modify the code, not just explain it.

## Issue 1 – Black square overlapping highlighted icon

When navigating between apps, the previously selected icon is redrawn before the newly selected icon.

Each icon is rendered into a fixed off-screen buffer (currently 68x68 or 72x72) and uploaded with `rg_display_write()`.

The previous icon redraw clears its entire buffer to black. Since the upload rectangle overlaps the neighbouring highlighted icon, the black background temporarily overwrites part of the highlighted circle, producing a square-corner artifact.

This is visible when moving from the Music icon to the Wi-Fi icon.

### Fix requirements

Do NOT simply redraw everything.

Instead implement one of these approaches (preferred order):

1. Compute a single dirty rectangle that contains BOTH the previous and current icon.

   * Clear the dirty rectangle once.
   * Draw both icons into the same temporary buffer.
   * Upload one rectangle.
   * Never allow an intermediate state to reach the display.

OR

2. If keeping per-icon rendering,

   * erase the previous icon,
   * redraw the previous icon,
   * redraw the current highlighted icon,
   * flush only after both have been rendered.

The highlighted icon must never be partially covered by the previous icon's background.

---

## Issue 2 – Right edge brightness during redraw

The right edge of the display becomes brighter while partial or full redraws occur.

This does NOT happen when using another ILI9341 library on the same hardware, so assume this is a software issue.

Investigate:

* SPI transaction sequencing
* DMA completion
* address window updates
* rg_display_write()
* esp_lcd_panel_draw_bitmap()
* rg_display_drain()
* partial update timing
* display flush order

Determine whether multiple overlapping bitmap writes or incomplete DMA synchronization could produce temporary brightness changes.

Do not assume this is a hardware issue.

---

## Rendering constraints

* Keep the current UI.
* Keep partial redraws.
* Do not redraw the full screen.
* Do not remove the animation.
* Do not change public APIs.
* Do not change icon positions.
* Preserve existing colors.

---

## Deliverables

1. Identify the exact cause of both artifacts.
2. Modify the rendering code to eliminate them.
3. Explain why the fix works.
4. If another rendering architecture (dirty rectangle, double buffering, or compositing) would be more appropriate, implement it only if it improves correctness without increasing redraw area unnecessarily.
