# Retro Console OS (ESP32-S3 N16R8)

## Purpose
Building a high-performance retro console based on the Retro-Go architecture, currently focusing on audio (ES8388), FM Radio (RDA5807/RDA5657), 4-bit SDMMC, ILI9341 display, and custom inputs.

## Hardware Pinout
| Peripheral | Signal | GPIO Pin | Notes |
|------------|--------|----------|-------|
| **I2C**    | SDA    | 4        | ES8388, DS3231, RDA5807/RDA5657 FM Tuner |
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
| D7  | 0x7F      | `01111111`     | BTN_A     | Toggle Thumbnail / Visualizer |
| D6  | 0xBF      | `10111111`     | BTN_UP    | Next Track        |
| D5  | 0xDF      | `11011111`     | BTN_DOWN  | Previous Track    |
| D4  | 0xEF      | `11101111`     | BTN_LEFT  | Previous Track    |
| D3  | 0xF7      | `11110111`     | BTN_B     | Stop              |
| D2  | 0xFB      | `11111011`     | BTN_ESCAPE| Stop              |
| D1  | 0xFD      | `11111101`     | BTN_ENTER | (Reserved: Menu)  |
| D0  | 0xFE      | `11111110`     | BTN_RIGHT | Next Track        |

**KY-040 Rotary Encoder:** CW = Volume Up (+5), CCW = Volume Down (-5)

## Project Status & Memory

### Version 8 Updates
- **Hardware PCNT Rotary Encoder:** Replaced manual debouncing with ESP32-S3 Pulse Counter (PCNT) peripheral for flawless, zero-CPU scrolling.
- **Screen Dimming (GPIO 11):** Configured hardware LEDC PWM to dim the ILI9341 backlight to 20% after 25s, and 0% after 40s of inactivity (excluding Games and RSVP reader) to preserve battery.
- **Sci-Fi System Diagnostic UI:** Completely redesigned the System Status view into an amber-and-black industrial diagnostic terminal featuring live Core Temperature, Uptime, Memory blocks, and Storage stats.
- **WAP Upload Routing:** The Web UI now features a dropdown menu to dynamically route uploaded files to the `/sdcard/` Root folder (for music/txt) or the `/sdcard/OTA/` folder (for firmware binaries).
- **Smoother Pong:** Added rotary encoder support to the Pong game and smoothed out the paddle movement increments.
### Version 25 (Latest)
- **RTC Hardware Sync Order Fix**: Resolved a critical I2C bus contention issue where the audio driver's repeated ES8388 I2C timeouts caused the DS3231 RTC initialization to fail with `err=259`. By moving the RTC sync sequence *before* the audio board initialization in the boot sequence, the DS3231 now reliably syncs the system time immediately on power-up.
- **I2C Diagnostics & Muting**: Discovered that generic ES8388 breakout boards without proper chip enable (CE) logic silently ignore I2C initialization commands (NACKs), causing the hardware mixer and new hardware muting commands to fail. Playback continues via I2S, but the lack of I2C control results in audible pops during track transitions.

### Version 24

### Version 23 Updates
- **FLAC Support Enhancements**: Added robust native FLAC metadata parsing (`STREAMINFO`, `VORBIS_COMMENT`, `PICTURE`). FLAC files now accurately display Title, Artist, Album, Genre, and Album Art (Baseline JPEG/PNG) natively, alongside a dynamically calculated accurate average bitrate.
- **Sleep Timer & Ambience Playback**: The Alarm app now features a fully functional sleep timer with an integrated Ambience player. You can choose an ambience track, set a timer (e.g., 30 mins, 1 hour), and the background music/ambience will automatically stop playing when the timer expires.
- **Battery Life Estimation**: Upgraded the Settings UI to not only display battery percentage but also intelligently estimate remaining battery life in hours and minutes (`BAT: 80% (9h 36m left)`) dynamically.
- **Neopixel Color Accuracy Fix**: Fixed an underlying bit-shifting issue with the WS2812 Neopixel driver where colors were rendering incorrectly (e.g., green channel overriding others) ensuring true-to-life HSV color wheel representations.
- **Increased Playlist Limit & Background Scanning**: The maximum number of playlist files has been expanded from 1,000 to 5,000 to support larger music libraries, and playlists now generate seamlessly in the background.
- **Memory Optimization**: Moved large tag buffers (e.g. for FLAC comment extraction) from stack allocation to the heap to prevent stack overflows during ESP-ADF audio pipeline creation.

### Version 22 Updates
- **Background Music**: Music app now supports background playback. You can go back to the Home screen and let it play, and it will keep playing if you open Settings. Opening other apps (Games, Files, Radio, etc.) automatically stops the music. You can also manually stop background playback on the Home screen by pressing the `B` button!
- **Moon Phase Indicator**: Added a real-time calculated moon phase visualization on the main Home screen (next to the large clock display).

### Version 20 & 21 Updates
- **Neopixel Precision Speed:** Fine-tuned the Neopixel animation speed utilizing a 10x hardware tick divider to strike the perfect balance—yielding buttery-smooth rainbow cycles and breathing pulses.
- **Game Audio Routing:** Resolved a hardware conflict where the alarm clock app aggressively disabled the LEDC buzzer channel. Game sound algorithms now forcefully commandeer `LEDC_CHANNEL_1` to guarantee Super Mario level-up and game-over melodies always trigger.
- **Global Alarm Override:** The Alarm and Timer buzzing can now be instantly silenced by pressing *any* button on the console, regardless of the active foreground application.
- **Live Hardware Telemetry:** Upgraded the "System Status" screen with real-time diagnostics including CPU Clock Frequency (MHz), Free RAM (KB), High-Water RAM (KB), and dynamic firmware version indicators.
- **Partition Reprovisioning:** Expanded and restructured the ESP32-S3 `partitions.csv` to allocate precisely 2.5MB (0x280000) for all OTA application slots (`updater`, `launcher`, `retro-core`, `prboom-go`), fully maximizing the 16MB flash boundary.
- **RTC Data Sanity Safety Net:** Upgraded the I2C DS3231 driver to detect time-travel corruption (e.g. Year 2136 due to dead coin cells). Invalid readouts now trigger an automatic physical RTC overwrite using the firmware's compile-time timestamp to ensure perpetual system stability.

### Tried and Successful
- **I2S/I2C Conflict Bypass:** Enabled `CONFIG_I2S_SKIP_LEGACY_CONFLICT_CHECK=y` and `CONFIG_I2C_SKIP_LEGACY_CONFLICT_CHECK=y`. Removed all references to new `esp_driver_i2s` headers to allow ADF's legacy drivers to work seamlessly. A `fullclean` was required to apply these changes globally.
- **I2C Timeout Fix (0x107)**: Lowered the Retro-Go I2C clock speed from 400kHz to 100kHz to compensate for the weak internal pull-up resistors on the ESP32-S3, which prevents the ES8388 communication from timing out upon boot.
- **GUI ENTER Button Fix**: Modified `rg_gui.c` to accept `RG_KEY_START` (the physical ENTER button) as an alternative to `RG_KEY_A` for selecting options in the Retro-Go launcher menus.
- **SDMMC 4-Bit:** Mounted SD card successfully on pins 38-42 in `SD_MODE_4_LINE`.
- **Audio HAL & Pipeline:** Initialized ES8388 codec (PCB Artists module) and ADF I2S stream. MP3 decoding and playback from SD card is verified and working (music audible). Root cause of original static/jitter was `board_i2s_pin_t` struct field order not matching IDF5's `i2s_std_gpio_config_t` — the ADF's `i2s_stream_idf5.c` does a raw `memcpy` of the pin struct into `gpio_cfg`, so `mck_io_num` MUST be the first field. Reordered from `{bck, ws, dout, din, mck}` to `{mck, bck, ws, dout, din}`. Also removed unnecessary hardcoded I2S overrides; the MP3 decoder auto-negotiates sample rate and bit width from file headers.
- **ES8388 Mixer Fix (Static Noise):** Root cause identified: ADF's `es8388_init()` hardcodes DACCONTROL17=0x90 and DACCONTROL20=0x90, which route analog input pins (LIN1/RIN1) into the DAC output mixer at 0dB. On the PCB Artists module (no mic connected), floating inputs inject noise into the output path. Fixed by writing DACCONTROL17=0x80 and DACCONTROL20=0x80 after codec init to disconnect analog inputs from the mixer. ADC also powered down (ADCPOWER=0xFF) since only playback (DECODE mode) is needed.
- **I2S Buffer Tuning:** Increased DMA descriptors (3→6), DMA frames per descriptor (312→480), and ring buffer (8KB→32KB) to reduce audio underrun pops.
- **Track Switching:** Fixed pipeline state management — `play_track()` now stops/terminates/resets the pipeline before starting a new track, regardless of play/pause state. Uses a `pipeline_has_run` guard to skip cleanup on first call (avoids harmless "Without stop" warnings).
- **Pipeline Event Listener:** Added `audio_event_iface` to consume ADF pipeline events and auto-advance to the next track when the current one finishes. Resolves "no space in external queue" warnings.
- **Dynamic Volume Control:** Volume is tracked locally (eliminates I2C read via `audio_hal_get_volume`). Only `audio_hal_set_volume` (I2C write) is called on encoder rotation (±5 per step). This halves the I2C traffic vs the previous read+write approach.
- **Display Output:** ILI9341 display initialized via `rg_display` driver for modern 2.8" ILI9341-compatible TFT modules (e.g. GoldenMorning). SPI bus at 16MHz, RGB element order, Profile 2 contrast calibration (GVDD 0x26, VCOM 0x35/0x3E/0xBE, reverse-ladder IPS gamma E0/E1), and Mode 6 un-mirrored orientation via `swap_xy(true)` and `mirror(false, true)` across all application rotation states.
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
- **I2C Volume NACK Fix (Direct Register Write & Clock Stretching):** Root cause of `I2C transaction unexpected nack detected` and pipeline I/O aborts during volume changes: ADF's `audio_hal_set_volume` uses the legacy I2C driver while IDF5's I2S stream initializes the new I2C master driver on the same port, causing hardware-level contention and NACKs when the codec is busy streaming. Fixed by bypassing `audio_hal_set_volume` and writing DACCONTROL4/DACCONTROL5 volume registers directly via `es8388_write_reg()`, combined with `.scl_wait_us = 300000` clock stretching and automatic 5-attempt retry loops in `i2c_bus_v2.c`.
- **Screen Flashing on Redraws Fix:** Eliminated brief white screen flashes and flickering on both the Home Page and Music App list. Root cause: `rg_display_drain()` was sending an SPI NOP command (`0x00`) to the ILI9341 display after every primitive draw call (text lines, boxes, rects) to wait for DMA completion, which glitched the TFT display controller's gate/source drivers. Replaced the NOP command in `rg_display_drain()` with a clean 200us yield (`esp_rom_delay_us(200)`), allowing blocking SPI DMA transfers to finish without sending bogus commands to the panel.
- **Background SD Card Scanning:** Resolved the noticeable loading delay when opening the music app with 900+ songs. Implemented asynchronous background scanning via a FreeRTOS task (`sd_card_scan_task`) and a mutex-protected `total_tracks` counter (`playlist_mutex`). The UI opens as soon as the first 100 tracks are discovered, and the main loop dynamically refreshes the song list as additional tracks are found in the background.
- **Pipeline Cleanup & Abort Warnings Fix:** Resolved `AUDIO_ELEMENT: OUT-[file] AEL_IO_ABORT` and `-3` warnings when re-entering the music app or switching tracks. Added full pipeline tear-down (`audio_pipeline_terminate`, `reset_ringbuffer`, and `reset_elements`) inside `app_music_stop()`, ensuring clean state resets between app transitions.
- **Icon Ghost Outline & Corner Overlap Fix:** Root cause of ghost rings: shrinking icons needed a full 68x68 (`NODE_SIZE`) black blit to erase previous animation states. Root cause of black corner overlap on the selected icon: adjacent 68x68 square bounding boxes overlap by ~15 pixels in the corners. When unselected icons were drawn after the selected icon, their solid black bounding box corners overwrote the highlighted icon's circular edge. Fixed by modifying `home_ui_draw()` to always draw unselected icons first and guarantee the selected/highlighted icon is drawn last on top of all others.
- **Faster List Text Scrolling:** Reduced `SCROLL_INTERVAL_MS` from 400ms to 150ms for much smoother character-by-character scrolling of long song titles in the selected list item.
- **Mini Player Track Name Scrolling:** Added independent scroll state (`mini_scroll_char_offset`, `mini_scroll_timer`) for the "now playing" mini player bar at the bottom. Long track names now scroll at 200ms intervals. Only the track name text line is redrawn for scroll updates (no full mini player redraw).
- **MP3 Thumbnail/Album Art Detection:** Added ID3v2 header + APIC frame detection in `play_track()`. Each track now logs `Thumbnail: ID3=YES/NO, APIC=YES/NO, ID3size=N` to the serial monitor, enabling future album art display.
- **Multi-Format Audio Scanning & Auto-Decoder Pipeline:** Upgraded the SD card background scanner (`sd_card_scan_task`) to detect `.mp3`, `.flac`, `.aac`, `.m4a`, and `.wav` audio files. Replaced the single MP3 decoder in the audio pipeline with an `esp_decoder` auto-detection array configured with `DEFAULT_ESP_MP3_DECODER_CONFIG`, `DEFAULT_ESP_FLAC_DECODER_CONFIG`, `DEFAULT_ESP_AAC_DECODER_CONFIG`, `DEFAULT_ESP_WAV_DECODER_CONFIG`, and `DEFAULT_ESP_M4A_DECODER_CONFIG`. The audio stream format, sampling rate, bit depth, and bitrate are now dynamically identified and updated in real-time.
- **Full-Screen Dedicated Player UI & Radial RGB FFT Visualizer:** Implemented a brand new full-screen Player UI triggered by pressing ENTER over any song in the playlist. The top 2/5 of the display features header text, a smoothly scrolling song title, artist information, real-time audio format metrics (`[Format] | [Sample Rate] kHz | [Bitrate] kbps`), and elapsed playback progression (`MM:SS / MM:SS`). The bottom section features a simulated/procedural radial RGB FFT visualizer centered around a duration progression loop and play/pause status icon.
- **Real-Time 48-Band Radial FFT Audio Visualizer:** Replaced procedural/simulated sine-wave visualizers with authentic real-time audio spectrum processing. Intercepted PCM audio samples directly at the decoder output using a custom `audio_element_set_write_cb` callback (`decoder_write_cb`) attached to `mp3_decoder` immediately after pipeline linking. Resolved a visualizer starvation bug where `rb_bytes_available()` returned 0 (free write space) instead of readable audio data by switching to `rb_bytes_filled()`. This guarantees PCM audio is mirrored to `fft_ringbuf` across all codecs (MP3, FLAC, AAC, WAV, M4A) without starvation or playback blocking, featuring automatic sliding-window sample discard when full. Built a custom in-place radix-2 Cooley-Tukey float FFT (`compute_fft_128`) with Hann windowing inside `app_music.c` that maps 64 frequency bins across the 48 radial RGB bars with frequency-compensated gain. Implemented real-time audio RMS calculations to drive the dancing VU-meter baseline modulation (`vu_meter_val`). Also subtly reduced the breathing core radius from `13 + (int)(11.0f * pulse)` down to `10 + (int)(9.0f * pulse)` for a cleaner aesthetic.
- **Dynamic ID3 & Vorbis Metadata Extraction (Artist & Title):** Implemented robust ID3v2 (`TPE1`, `TIT2`) frame parsing inside `play_track()` using syncsafe header size calculation and exact frame jumping (`Frame Header -> Frame Size -> Skip payload -> Next frame`). This eliminates false positive matches inside binary APIC album art images and reliably extracts metadata across all supported audio formats. Added global `info_title_str` and `info_artist_str` buffers with intelligent filename fallback parsing (`Artist - Title.ext`). The Player UI and mini-player dynamically display the extracted song title (`strlen(info_title_str) ? info_title_str : playlist[current_track]`), real artist name, audio format, sampling rate, and bitrate (`[Format] | [Sample Rate] kHz | [Bitrate] kbps`).
- **Modular Player UI Redraw & Artist Scrolling Optimization:** Refactored top display redraw logic into modular functions (`draw_player_title()`, `draw_player_metadata()`, and `draw_player_timer()`) that explicitly clear individual bounding rectangles (`rg_gui_draw_rect`) before rendering text. Added smooth character-by-character horizontal scrolling for long artist names using `rg_gui_draw_text_line` and an `artist_scroll` timer, matching title scroll behavior and eliminating text clipping. Eliminates SPI bus congestion, ghost glyphs, and character overlap while boosting VU visualizer animation framerate to 40 FPS.
- **Volume Bar Granularity & Visibility Fix:** Adjusted volume step size from 5% to 10% increments across button and encoder controls. Fixed visual cutoff below 70% volume by adding a prominent white outline (`RG_COLOR_WHITE`) and rendering the unfilled track in visible grey (`RG_COLOR_RGB(100, 100, 110)`), ensuring the bar remains fully visible against the dark background. Fixed outline erasure when the bar auto-hides after 3 seconds by clearing the full outer bounding box.
- **Disappearing Volume Bar & Player Navigation:** In the Player UI, volume is controlled via UP/DOWN buttons or encoder, triggering a vertical volume bar on the right edge of the screen that automatically hides after 3 seconds of inactivity. Track navigation (previous/next) is mapped to LEFT/RIGHT, play/pause toggle to ENTER, and pressing ESCAPE returns seamlessly to the scroll list without interrupting playback.
- **Reliable Play/Pause via Pipeline Stop & Byte Position Seeking:** Replaced standard `audio_pipeline_pause()` and `audio_pipeline_resume()` with a robust stop-and-seek mechanism. When paused, the current stream playback byte position is recorded via `audio_element_getinfo(fatfs_stream_reader, &info); saved_byte_pos = info.byte_pos;`, followed by a full pipeline stop and wait. On resume, the pipeline and ringbuffer are cleanly reset, the file URI is re-applied, and the playback seek position is restored using `audio_element_set_byte_pos(fatfs_stream_reader, (int)saved_byte_pos);`. This eliminates I2S DMA lockups and ensures both audio playback and the real-time FFT visualizer restart immediately. Added throttled diagnostic logging inside the FFT visualizer loop (`FFT avail=%d read=%d channels=%d`) every 40 frames (~1 sec intervals).
- **Shuffle Mode (`BTN_B`) & Jump to Active Track on Escape:** Added a random Shuffle Mode toggled by pressing Button B (`BTN_B`), dynamically rendering a sleek purple block containing a black `S` in the top right corner of the header bar (in both the song scroll list and Player UI). Configured automatic song completion and LEFT/RIGHT track skips (`get_next_track_idx()`, `get_prev_track_idx()`) to randomly select unplayed tracks when shuffle is active. When returning from the full-screen Player UI to the track scroll list via Button ESCAPE (`BTN_ESCAPE`), the list cursor automatically jumps to and highlights the currently playing track (`selected_index = current_track; ensure_cursor_visible();`).
- **Embedded Album Art (APIC) Display & Thumbnail Toggle (`BTN_A`):** Upgraded ID3v2 tag parsing in `play_track()` to use direct stream `fseek`/`fread` scanning with a 4KB scan buffer, reliably detecting embedded JPEG SOI (`0xFF 0xD8`) and PNG header (`\x89PNG`) APIC thumbnail images regardless of tag size or marker ordering. When in the full-screen Player UI, pressing Button A (`BTN_A`) instantly toggles between the real-time 48-band radial FFT visualizer and the album art thumbnail. Thumbnail decompression is performed on-the-fly using hardware-accelerated Tiny JPEG Decompressor (`tjpgd.h`) by constructing the full `/sdcard/%s` filesystem path, then scaling and centering the image to fill the 180x180 visualizer box. When thumbnail display is active, PCM audio interception for FFT/VU calculation is automatically disabled in `decoder_write_cb` to conserve CPU cycles. If a track has no embedded album art, a clean centered `NO THUMBNAIL` indicator is displayed.
- **Long Song Title Rendering & Auto-Advance Fixes:** Solved an SPI DMA LCD error (`ESP_ERR_INVALID_ARG`) caused by centering long strings (>14 characters) wider than the screen width by transitioning `draw_player_title()` to `rg_gui_draw_text_line` with 14-character bounding and smooth horizontal scrolling. Upgraded song auto-advance to reliably trigger the next track when any pipeline element (`fatfs_stream_reader`, `mp3_decoder`, or `i2s_stream_writer`) reports `AEL_STATUS_STATE_FINISHED` or `AEL_STATUS_STATE_STOPPED` with a 1500ms debounce timer.
- **Song List UI Sky-Blue Border & Partial Redraw Protection:** Restored the sky-blue rounded border box around the playlist view and updated partial slot redraws (`draw_list_item`) to restrict rectangle background clearing strictly to the inner list width (`228px`), preventing scrolling or item navigation from erasing the vertical borders.
- **DS3231 Hardware RTC Sync:** Added hardware RTC initialization and synchronization (`rtc_sync_from_ds3231`) over I2C on boot in `main.c`, ensuring system timestamps (`settimeofday`) are accurately maintained.
- **SD Card Text Reader & RSVP Speed Reader (Files App):** Created a dedicated Files application (`app_files.c`) accessible from the Home Screen (`selected == 0`). Features SD card `.txt` file scanning with iPod-style wheel navigation, a word-wrapped Page Reader with automatic smart punctuation sanitization and bookmarking (`.BMK`), and a rapid serial visual presentation (RSVP) speed reader mode that flashes words individually at adjustable WPM speeds (100 to 600 WPM) with Optimal Recognition Point (ORP) highlighting.
- **RSVP UI & Continuous ORP Rendering:** Upgraded RSVP word rendering to eliminate character gaps by calculating dynamic font advances (`rg_gui_get_text_width`) and drawing left/right text blocks continuously against the highlighted ORP letter. Increased RSVP word font to size 24 with automatic scaling fallback for long words. Added persistent per-file bookmarking (`.BMK`) with auto-save/auto-load on mode transition and exit, along with high-precision `%.2f%%` reading progression indicators.
- **Visualizer & Thumbnail PSRAM/DMA Chunked Transfer Fix:** Solved `spi transmit (queue) color failed` (`ESP_ERR_INVALID_ARG`) errors when opening the Music App visualizer or thumbnail. Root cause: audio pipeline tasks fragmented internal DRAM, reducing largest free contiguous block below 64KB and forcing `vis_buf` allocation into external PSRAM, which ESP32-S3 SPI DMA rejected for direct transmission. Fixed by explicitly allocating `vis_buf` in PSRAM (`MALLOC_CAP_SPIRAM`) to preserve internal DRAM, and implementing a chunked staging transfer (`send_vis_buf_to_display`) that copies and transmits 10 lines at a time via a small, 4-byte aligned static internal DRAM buffer. Upgraded `rg_display_drain()` to use `esp_lcd_panel_io_tx_param(io_handle, -1, NULL, 0)` for true hardware DMA draining without transmitting bogus NOP commands over SPI.
- **RTC Build-Time Auto-Sync:** Added compile-time timestamp parsing (`parse_compile_time`) in `main.c` using `__DATE__` and `__TIME__` macros. When `rtc_sync_from_ds3231()` executes on boot, it compares the DS3231 hardware RTC time against the firmware compilation timestamp. If the hardware clock is older than the compile time or invalid, it automatically re-initializes the DS3231 over I2C with the fresh compilation date, time, and day of the week (`wday`).
- **Day Alongside Date on Home Page:** Expanded the home screen date display (`home_rg_gui.c`) to include the abbreviated day of the week (e.g., `"MON, JUL 06, 2026"`). Increased `date_text` buffer capacity to 32 bytes and widened `DATE_BOX_W` to 160 pixels to ensure perfectly centered, clip-free rendering.
- **Alarm Clock Application & Buzzer Feature (`GPIO 48`):** Created a dedicated Alarm Clock application (`app_alarm.c`) launched from the Home Screen (`selected == 6`, `APP_ALARM`). Features a sleek UI displaying system time, toggleable Alarm Status (`[ ON ]`/`[ OFF ]`), Alarm Hour, and Alarm Minute with persistent settings storage via NVS (`"alarm_store"`). Implemented a continuous monitoring loop (`app_alarm_tick`) in `main.c` that triggers a pulsing buzzer on **GPIO 48** (200ms BEEP pattern) across all apps when the system time matches the alarm time. Pressing any button immediately silences the alarm.
- **DMA-Capable GUI Text & Box Rendering:** Created `rg_gui_dma_malloc()` in `rg_gui.c` to prioritize allocating display buffers from internal DMA-capable memory (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`).
- **System-Wide Chunked DMA Staging & Thumbnail Fallback Fix:** Solved `spi transmit (queue) color failed` (`ESP_ERR_INVALID_ARG`) errors when encountering audio files with PNG (`\x89PNG`) or progressive JPEG album art tags that hardware TJpgDec cannot decompress (`jd_prepare failed: 8`). Replaced the monolithic `180x180` fallback `"NO THUMBNAIL"` text box with a lightweight `180x20` box at `y=214`, reducing RAM requirements to 7.2KB and guaranteeing internal DRAM placement. To make the entire OS 100% immune to PSRAM DMA transmission errors forever, created `rg_gui_send_dma_chunked()` in `rg_gui.c` and updated all text primitives (`draw_text_center`, `draw_text_box`, `draw_text_line`, `draw_text_scaled`) and rounded boxes (`draw_rounded_box` in `app_music.c` & `app_files.c`) to stage and transmit all display buffers via a 4-byte aligned static internal DRAM buffer in <= 20-line chunks.
- **LEDC Acoustic Buzzer Beep Upgrade (`GPIO 48`):** Solved passive transducer buzzer clicking/ticking issues by replacing direct GPIO toggling in `app_alarm.c` with the ESP32-S3 hardware LED PWM Controller (`driver/ledc.h`). When ringing, the driver generates a clean 2700 Hz square wave (50% duty cycle at the piezo transducer's natural acoustic resonant frequency) on **GPIO 48**, producing an audible acoustic **BEEP-BEEP-BEEP** pattern.
- **Retro Arcade Games Launcher Menu (`ui.c` & `ui.h`):** Created a dedicated Games application accessible from the Home Screen (`selected == 7`, `APP_GAMES`). Displays an iPod-wheel style games menu listing `TETRIS`, `2048`, `PONG`, and `RETRO-GO` with an Arcade Orange (`#F05A14`) rounded highlight box fixed at row 2 (`y=97`). Features inverted wheel navigation (UP button decrements index moving items down on screen; DOWN button increments index moving items up) and full chunked DMA display rendering.
- **Super Mario Game Audio & No-Beep Gameplay Rule:** Removed all generic button beeps (`game_sound_play_blip`) from standard gameplay actions (tile sliding in 2048, piece rotation and movement in Tetris, paddle movement and ball bouncing in Pong, and menu navigation). Configured all significant gameplay milestones (level ups, high score thresholds, and game overs) to trigger customized Super Mario acoustic melodies (Coin sound, 1-UP theme, Power Up jingle, and Game Over theme) via the LEDC PWM buzzer on **GPIO 48**.
- **UI & Game Audio Refinements (s3-node-repo Melodies, Black Menu Background, Clean Shuffle Icon):** Migrated game milestone melodies in `game_sound.c` (Tetris score/gameover, 2048 milestone/gameover, and Pong level up/gameover) to use the exact note sequences and duration timings from `s3-node-repo`. Updated the Games menu background in `ui.c` from grey to solid black (`0x0000`) for consistency across all system list pages. Removed on-screen footer instruction text from the Games menu, File Browser/RSVP reader, and Alarm clock adjustment screens. Fixed a music player bug where activating shuffle mode caused the bitrate and duration lines to disappear by ensuring the global font rendering color is reset to white before drawing text. Upgraded the shuffle indicator from a box-in-box design to a clean, solid purple `"S"` icon in both the music list and player headers.
- **Horizontal White Line Screen Artifact Fix:** Resolved persistent horizontal white lines bleeding from the left edge (covering almost 1/5 of the screen width) across the Alarm page, Games menu, and individual games. Root cause: rapid, unbuffered SPI DMA writes in `rg_gui_draw_rect` and `rg_gui_draw_text` tight loops without calling `rg_display_drain()`, which corrupted the SPI DMA engine when overlapping with screen transitions. Fixed by enforcing explicit `rg_display_drain()` barriers after each line chunk write in `rg_gui.c`, `home_rg_gui.c`, and `ui.c`, and aligning all static node buffers (`__attribute__((aligned(4)))`).
- **ILI9341 Compatible TFT Module Display Driver Upgrade:** Calibrated the `rg_display` driver (`rg_display.c`) to support new 2.8" ILI9341-compatible replacement panels without altering any application, audio, game, or UI code. Solved low contrast / washed-out gray blacks and dull whites by implementing hardware Contrast Profile 2, programming expanded GVDD (`0xC0 = 0x26`, 4.95V) and VCOM Control (`0xC5 = {0x35, 0x3E}`, `0xC7 = 0xBE`) voltage swings along with reverse-ladder IPS/Clone gamma curves (`0xE0`/`0xE1`). Corrected RGB/BGR color mismatch by setting `.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB`. Fixed screen mirroring and reversed glyphs (e.g., Cyrillic 'И' instead of 'N') by determining the panel's scanning axes require Mode 6 (`mirror_x = false, mirror_y = true` when `swap_xy = true`). Fixed a bug where `rg_display_set_config()` hardcoded `(false, false)` mirror settings when application apps booted, ensuring all system rotation states dynamically inherit the un-mirrored calibration.
- **FM Radio Tuner Application (`app_radio.c` & `app_radio.h`):** Created a dedicated FM Radio application accessible from the Home Screen (`selected == 2`, `APP_RADIO`). Supports RDA5807 / RDA5657 FM receiver modules over I2C (SDA: GPIO 4, SCL: GPIO 5, 100kHz). Features direct analog audio routing through the ES8388 audio codec mixer (enabling LIN1/RIN1 and LIN2/RIN2 passthrough to DAC speakers/headphones without CPU decoding overhead, and cleanly restoring DAC-only 0x80 mode on exit to prevent noise in other apps). Includes manual tuning via UP/DOWN buttons (0.1 MHz steps from 87.0 to 108.0 MHz), 6 preset favorite channels via LEFT/RIGHT buttons (e.g., Red FM 93.5, Radio City 91.9, BIG FM 92.7), volume adjust via encoder or VOL+/VOL- buttons, mute toggle via ENTER/B buttons, and a real-time status bar displaying Volume %, RSSI signal strength, and Stereo/Mono indicator.
- **Music Player Shuffle & Play/Pause Keybind Integration:** Mapped `BTN_B` to toggle Shuffle mode (indicated by a sleek purple "S" icon in the UI). Repurposed the `BTN_ENTER` short-press to pause/resume tracks, and long-press (>= 600ms) to toggle Shuffle mode (releasing `BTN_B` from being a dedicated shuffle key, aligning with user navigation flow).
- **KY-040 Encoder Volume Control & Driver Protection in Emulator/Launcher:** Added KY-040 rotary encoder volume adjustments (+10 / -10 volume per step) directly into `components/retro-go/rg_input.c`. Wrapped volume setting API calls with `rg_audio_get_driver()` safety check, eliminating `StoreProhibited` panics when the encoder was rotated before the audio subsystem was fully initialized.
- **Universal Software Volume Control:** Replaced hardware-based ES8388 I2C volume commands with a global software volume scaling architecture (`global_volume_set`). The hardware codec is locked at a baseline of 70 (the new 100% threshold to prevent clipping), and all volume adjustments (via rotary encoder or VOL+/VOL- buttons) scale the PCM byte stream dynamically. This completely eliminates I2C contention crashes during heavy SPI/SD operations and guarantees immediate volume changes across all apps.
- **Audio FX / DSP App & Equalizer (`app_audio_fx.c`):** Created a dedicated AUDIO FX application accessible from the Home Screen (`selected == 4`). Features a real-time ESP-ADF `audio_pipeline` with the `audio_sonic` and `equalizer` elements. Includes a fully functional 10-Band EQ (`31Hz` to `16kHz`), Pitch shifting, and Speed adjustment. The UI provides a retro horizontal slider interface for adjusting all parameters on the fly via UP/DOWN (select parameter) and LEFT/RIGHT (modify value).
- **Independent FFT Visualizer & Volume Decoupling:** Decoupled the Music App's FFT visualizer from the active volume state. Audio PCM data is now copied directly into the FFT ringbuffer *before* the software volume attenuation is applied. This guarantees that the 48-band radial RGB visualizer reacts dynamically at full scale, even when the playback volume is set below 50%, ensuring maximum aesthetic impact at all listening levels.
- **Thumbnail Watchdog Cache & Smooth UI:** Solved `Task watchdog got triggered` and `prvNotifyQueueSetContainer` crashes when toggling shuffle mode with album art active. Implemented a robust `thumbnail_cached` state machine in `app_music.c`. Album art JPEG/PNG files are now only decoded once per track via TJpgDec. Subsequent UI redraws instantly bypass the heavy decoder and blit directly from the cached `vis_buf` pixel array in PSRAM, achieving zero-latency UI interaction and eliminating audio dropouts.
- **Download (WAP File Transfer):** Transformed the scaffolding into a fully functional Wi-Fi Access Point (`RetroConsole`) application. Connecting your phone navigates to a zero-dependency HTML interface (`192.168.4.1`) for direct POST uploads to the SD card. Features robust backend URL decoding for spaces/symbols (`urldecode2`), safe 256-byte buffer limits to prevent format-truncation crashes, and a dedicated ASCII folder UI icon.
- **Smart RTC Time Sync (Hardware Persistent):** Injected a silent Javascript `window.onload` hook into the WAP captive portal. Upon opening the download page, your device seamlessly calculates its local Unix Timestamp (offset for your timezone) and hits a hidden `/sync_time` REST endpoint, automatically aligning the console's internal RTC clock with your exact local time. The time is synchronously pushed directly to the physical DS3231 chip over I2C, ensuring accurate time keeping across system reboots and emulator transitions.
- **RSVP (Files & Rapid Serial Visual Presentation):** Renamed "Files" to "RSVP" and equipped it with a freshly generated ASCII 'overlapping pages' icon.
- **Games App UI:** Designed and integrated a brand new, high-detail full-sized (24x24) ASCII gamepad icon for the Games/Retro-Go launcher on the home screen.
- **Dynamic Text Scrolling & UI File Management:** Replaced full-screen redraws in the RSVP app with targeted partial bounding-box redraws. When the cursor highlights a text file with a name longer than 26 characters, the filename smoothly scrolls horizontally from right to left, pausing at the end before looping. Added in-app file deletion: pressing `BTN_B` triggers a dark-red GUI confirmation box (`[A] YES   [B] NO`), safely invoking `remove()` on the SD card vfs without requiring a PC.
- **Dynamic Boot Splash Screen (Flash Compiled):** Created a Python packager script (`process_boot_images.py`) that scans a `boot_images` folder and combines multiple 240x320 RGB565 text files into a single unified `boot_images.h` array. `main.c` uses `esp_random()` on boot to select one of the arrays and blasts it to the display in real-time, providing instant, randomized startup visuals without SD card latency.
- **Clock Apps Refinement (Smart Redraws & NVS Decoupling):** Upgraded the Clock, Stopwatch, and Timer UI to be completely event-driven, eliminating 1-second full-screen redraw loops. The UI now only updates when time actually changes or buttons are pressed, preventing unnecessary DMA churn. Removed unnecessary NVS saves to preserve flash wear, mapped Timer Start/Stop purely to `BTN_ENTER`, and improved buzzer hardware integration.
- **USB OTG File Transfer:** Transformed the OTG option into a fully functional USB Mass Storage Class (MSC) device. Selecting "OTG" safely suspends the internal ESP-ADF `esp_periph` SD card background task and re-initializes the SDMMC hardware for the TinyUSB driver using exact GPIO Matrix pin mappings (`ESP_SD_PIN_*`). The console enumerates as a USB Flash Drive on the host PC via the native USB port, featuring dynamic ASCII computer UI art while connected. Pressing `BTN_ESCAPE` safely tears down the TinyUSB driver and cleanly resumes the ESP-ADF audio environment without triggering `LoadProhibited` guru meditations.

### Core System & Hardware Fixes
- **Neopixel 2D Color Wheel UI:** Implemented a full 2D HSV circular color wheel for the onboard WS2812 Neopixel (GPIO 48). Generates a true mathematical 120x120 circular pixel buffer using `<math.h>` and allows the user to freely roam a 6x6 target cursor inside the circle to select any Hue and Saturation, before jumping to a dynamic Brightness bar.
- **Micro-Optimized Partial Screen Redraws:** To prevent the color wheel from choking the SPI display bus when the user holds a directional button, the system dynamically tracks the exact 6x6 bounding box of the old cursor, surgically plucks the 36 background pixels out of the wheel's RAM buffer, and stamps them over the old cursor to instantly erase it. This drops the active SPI payload from 14,400 pixels to just 72 pixels, allowing butter-smooth, zero-lag cursor navigation.
- **Audio Conflict Resolution (PWM vs RMT):** Identified and resolved a severe hardware conflict where the alarm buzzer and retro game audio (originally using `LEDC_TIMER_0` on GPIO 48) were physically sharing the same timer and pin as the display backlight and Neopixel driver. Migrated all audio/buzzer PWM signals to `LEDC_TIMER_1` and completely relocated the piezo buzzer physical pin to **GPIO 46**. This completely isolates the systems—alarms no longer dim the screen, and Neopixel RMT data bursts no longer cause the buzzer to tick.
- **Wi-Fi RF Noise Suppression:** Fixed an electrical physics bug where opening the OTA Web interface caused loud static pops and jitters in the ES8388 audio output. Root cause: The ESP32's default Wi-Fi TX power creates massive 400mA current spikes that sag the shared 3.3V power rails, coupling directly into the analog audio chip. Fixed by injecting `esp_wifi_set_max_tx_power(40)` immediately after Wi-Fi init, throttling the transmission to ~10dBm—which eliminates the electrical spikes while preserving plenty of range for a local phone connection.

### Known Issues / In-Progress
- **UI/UX Aesthetics & Design:** The current UI layout is functional and responsive; ongoing visual refinements will continue based on user feedback.

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
- **Home Menu (`rg_gui`):** The primary home screen is built natively using `rg_gui` (avoiding the overhead of LVGL). It features a grid/list layout for apps.
- **Game Sub-Menu:** A dedicated "Game App" launcher from the home menu hosting Retro-Go, Tetris, 2048, and Pong.
- **`main/app_music.c`**: Audio orchestration, SD card background scanning, ID3 parsing, audio pipeline management, and full UI/visualizer rendering.
- **`main/app_radio.c`**: Dedicated FM Radio application managing RDA5807/RDA5657 I2C tuning, ES8388 analog input mixer routing, preset station list, and live RSSI/Stereo status display.
- **`main/app_files.c`**: Files browser, RSVP speed reader, and text viewer application.
- **`main/app_alarm.c`**: Alarm clock application with hardware DS3231 RTC sync and LEDC PWM buzzer alerts.
- **`components/retro-go/rg_display.c`**: Core ILI9341 SPI LCD driver rendering the `rg_gui` primitives.
- **`components/input_manager`**: Handles 74HC165 shift register and KY-040 encoder inputs.

---

## Retro-Go Integration Plan: Hybrid Launcher + Retro-Go Architecture

### Overview

The console is divided into two independent environments that share the same hardware:

1. **Console OS (Factory Partition)** — The primary firmware containing all multimedia and utility applications (Music, Radio, Files, Alarm, native games).
2. **Retro-Go Gaming Environment (OTA Partition)** — A dedicated retro emulation firmware built from the upstream [ducalex/retro-go](https://github.com/ducalex/retro-go) repository, ported to this hardware.

The Console OS acts as the primary operating system presented to the user after power-on. Retro-Go functions as a separate application firmware that is launched only when the user selects "RETRO-GO" from the Games menu. Switching between the two environments is handled via the ESP-IDF OTA partition API and `esp_restart()` — no custom bootloader modifications are required.

### System Layout

```
Console OS (Factory)              Retro-Go (OTA_0)
├── Music Player                  ├── NES (retro-core)
├── FM Radio                      ├── Game Boy
├── File Browser / RSVP Reader    ├── Game Boy Color
├── Alarm Clock                   ├── Sega Master System / Game Gear
└── Games                         ├── PC Engine
    ├── RETRO-GO ──[reboot]──────>├── DOOM
    ├── Tetris                    ├── Save States & Cover Art
    ├── 2048                      └── Exit ──[reboot]──> Console OS
    └── Pong
```

### Boot Flow

```
Power On
    │
    ▼
ESP32 Bootloader
    │
    ▼
Console OS (factory)
    │
    ├── Music / Radio / Files / Alarm
    └── Games
           │
           └── User selects RETRO-GO
                   │
                   ├── Stop audio pipeline & display DMA
                   ├── Set OTA_0 partition as boot target
                   └── esp_restart()
                           │
                           ▼
                   ESP32 Bootloader
                           │
                           ▼
                   Retro-Go (ota_0)
                           │
                           ├── Retro-Go Launcher (ROM browser)
                           ├── Play NES / GB / GBC / SMS / DOOM
                           └── User exits Retro-Go
                                   │
                                   ├── Set factory partition as boot target
                                   └── esp_restart()
                                           │
                                           ▼
                                   Console OS (factory)
```

### Flash Partition Table (16 MB)

The ESP32-S3 N16R8 has 16 MB of flash. The partition table allocates space for both firmware images and shared storage:

```csv
# Name,      Type, SubType,  Offset,    Size,     Flags
nvs,         data, nvs,      0x9000,    0x6000,
otadata,     data, ota,      0xF000,    0x2000,
phy_init,    data, phy,      0x11000,   0x1000,
factory,     app,  factory,  0x20000,   0x300000,   # Console OS (~3 MB)
ota_0,       app,  ota_0,    0x320000,  0x400000,   # Retro-Go   (~4 MB)
storage,     data, fat,      0x720000,  0x8E0000,   # FATFS      (~8.9 MB)
```

**Notes:**
- `factory` is the default boot partition (Console OS). The system always boots here unless explicitly switched.
- `ota_0` holds the Retro-Go firmware. All emulator cores (NES, GB, GBC, SMS, GG, PCE, DOOM) coexist within this single binary via `retro-core`.
- `otadata` is required by `esp_ota_ops.h` to track which partition to boot.
- `storage` provides local FATFS for Retro-Go save states, bookmarks, and Console OS data. ROMs, cover art, BIOS files, and music are stored on the SD card.
- The SD card is mounted at `/sdcard` (Console OS) and `/sd` (Retro-Go's `RG_STORAGE_ROOT`).

### Side A: Console OS Launch Bridge (`main/ui.c`)

When the user selects RETRO-GO from the Games menu (`selected_game == 3`), the Console OS performs a clean handoff:

```c
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static void launch_retro_go(void)
{
    ESP_LOGI("RETRO_BRIDGE", "Launching Retro-Go...");

    // 1. Stop active audio pipeline (if music was playing)
    //    app_music_stop() / app_radio_stop() as needed

    // 2. Drain display DMA and show transition message
    rg_display_drain();
    rg_gui_clear(0x0000);
    rg_gui_draw_text_box(0, 140, 240, 40, 0x0000, "LOADING RETRO-GO...");
    rg_display_drain();

    // 3. Find the Retro-Go partition (ota_0)
    const esp_partition_t *retro_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, NULL
    );

    if (retro_part) {
        esp_ota_set_boot_partition(retro_part);
        vTaskDelay(pdMS_TO_TICKS(100));
        esp_restart();
    } else {
        ESP_LOGE("RETRO_BRIDGE", "Retro-Go partition not found!");
        // Show error on screen and return to Games menu
    }
}
```

### Side B: Retro-Go Return Bridge

In the Retro-Go firmware, modify `rg_system_switch_app()` in `components/retro-go/rg_system.c` so that exiting to "launcher" boots back to the Console OS instead of searching for a Retro-Go launcher partition:

```c
void rg_system_switch_app(const char *app, const char *arg1, ...)
{
    if (app == NULL || strcmp(app, "launcher") == 0)
    {
        // Return to Console OS (factory partition)
        const esp_partition_t *factory = esp_partition_find_first(
            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL
        );
        if (factory) {
            esp_ota_set_boot_partition(factory);
            esp_restart();
        }
    }
    // ... rest of existing Retro-Go app switching logic
}
```

---

### Retro-Go Custom Target: Hardware Port

A new Retro-Go target must be created to map the console's specific hardware. This target resides inside the cloned `ducalex/retro-go` repository at `components/retro-go/targets/retro-console-s3/`.

#### Target Registration (`components/retro-go/config.h`)

Add the new target to the conditional include chain:

```c
#elif defined(RG_TARGET_RETRO_CONSOLE_S3)
#include "targets/retro-console-s3/config.h"
```

#### Target Config (`targets/retro-console-s3/config.h`)

```c
// Target definition
#define RG_TARGET_NAME             "RETRO-CONSOLE-S3"

// --- Storage: SD Card via SDMMC 4-bit ---
#define RG_STORAGE_ROOT             "/sd"
#define RG_STORAGE_SDMMC_HOST       SDMMC_HOST_SLOT_1
#define RG_STORAGE_SDMMC_SPEED      SDMMC_FREQ_DEFAULT
// GPIO Matrix pin assignments (ESP32-S3 supports flexible SDMMC GPIO mapping)
#define RG_GPIO_SDSPI_CLK           38
#define RG_GPIO_SDSPI_CMD           39
#define RG_GPIO_SDSPI_D0            40
#define RG_GPIO_SDSPI_D1            41
#define RG_GPIO_SDSPI_D2            42
#define RG_GPIO_SDSPI_D3            21

// --- Audio: ES8388 External DAC via I2S ---
#define RG_AUDIO_USE_INT_DAC        0   // No internal DAC (ESP32-S3 has none)
#define RG_AUDIO_USE_EXT_DAC        1   // Enable external DAC (ES8388)
#define RG_GPIO_I2S_MCLK            47
#define RG_GPIO_I2S_BCLK            15
#define RG_GPIO_I2S_WS              16
#define RG_GPIO_I2S_DOUT            17  // ESP32-S3 -> ES8388 (corrected for HW swap)
#define RG_GPIO_I2S_DIN             18  // ES8388 -> ESP32-S3 (corrected for HW swap)
// ES8388 I2C control
#define RG_GPIO_I2C_SDA             4
#define RG_GPIO_I2C_SCL             5
#define RG_ES8388_I2C_ADDR          0x20  // 8-bit address (CE=LOW)

// --- Video: ILI9341 240x320 SPI TFT (Portrait) ---
#define RG_SCREEN_DRIVER            0   // 0 = ILI9341/ST7789
#define RG_SCREEN_HOST              SPI2_HOST
#define RG_SCREEN_SPEED             SPI_MASTER_FREQ_16M  // 16 MHz (verified stable)
#define RG_SCREEN_BACKLIGHT         0   // Hardwired to 3.3V (always on)
#define RG_SCREEN_WIDTH             240
#define RG_SCREEN_HEIGHT            320
#define RG_SCREEN_ROTATE            0   // Portrait native
#define RG_SCREEN_VISIBLE_AREA      {0, 0, 0, 0}
#define RG_SCREEN_SAFE_AREA         {0, 0, 0, 0}
#define RG_GPIO_LCD_MOSI            13
#define RG_GPIO_LCD_CLK             12
#define RG_GPIO_LCD_CS              10
#define RG_GPIO_LCD_DC              9
#define RG_GPIO_LCD_RST             14
#define RG_GPIO_LCD_BCKL            -1  // No GPIO control (hardwired)
// ILI9341 init sequence matching Console OS Profile 2 calibration
#define RG_SCREEN_INIT()                                              \
    ILI9341_CMD(0xCF, 0x00, 0xC3, 0x30);                             \
    ILI9341_CMD(0xED, 0x64, 0x03, 0x12, 0x81);                       \
    ILI9341_CMD(0xE8, 0x85, 0x00, 0x78);                             \
    ILI9341_CMD(0xCB, 0x39, 0x2C, 0x00, 0x34, 0x02);                 \
    ILI9341_CMD(0xF7, 0x20);                                         \
    ILI9341_CMD(0xEA, 0x00, 0x00);                                   \
    ILI9341_CMD(0xC0, 0x26);          /* GVDD 4.95V */                \
    ILI9341_CMD(0xC1, 0x11);                                         \
    ILI9341_CMD(0xC5, 0x35, 0x3E);    /* VCOM */                     \
    ILI9341_CMD(0xC7, 0xBE);          /* VCOM offset */              \
    ILI9341_CMD(0x36, 0x68);          /* MADCTL: Mode 6 portrait */  \
    ILI9341_CMD(0x3A, 0x55);          /* 16-bit RGB565 */            \
    ILI9341_CMD(0xB1, 0x00, 0x1B);                                   \
    ILI9341_CMD(0xB6, 0x0A, 0xA2);                                   \

// --- Input: 74HC165 Shift Register + KY-040 Encoder ---
// Buttons are directly read from 74HC165 (GPIO 3/6/7) in a custom input driver.
// Retro-Go's input system requires mapping physical buttons to RG_KEY_* constants.
// A custom input driver (drivers/input/shift_register.h) will be created.
#define RG_GPIO_GAMEPAD_SR_CLK      3   // 74HC165 Clock (CP)
#define RG_GPIO_GAMEPAD_SR_QH       6   // 74HC165 Serial Data Out (Q7)
#define RG_GPIO_GAMEPAD_SR_PL       7   // 74HC165 Parallel Load (PL)
#define RG_GPIO_GAMEPAD_EN_CLK      1   // KY-040 Encoder CLK
#define RG_GPIO_GAMEPAD_EN_DT       2   // KY-040 Encoder DT

// Button-to-Retro-Go key mapping (active LOW, MSB-first):
// D7 = BTN_A     -> RG_KEY_A        (Primary action)
// D6 = BTN_UP    -> RG_KEY_UP       (D-Pad Up)
// D5 = BTN_DOWN  -> RG_KEY_DOWN     (D-Pad Down)
// D4 = BTN_LEFT  -> RG_KEY_LEFT     (D-Pad Left)
// D3 = BTN_B     -> RG_KEY_B        (Secondary action)
// D2 = BTN_ESC   -> RG_KEY_MENU     (In-game menu / Exit)
// D1 = BTN_ENTER -> RG_KEY_START    (Start / Select)
// D0 = BTN_RIGHT -> RG_KEY_RIGHT    (D-Pad Right)
// KY-040 CW      -> RG_KEY_OPTION   (Volume Up / Settings)
// KY-040 CCW      -> RG_KEY_SELECT  (Volume Down)
```

#### Target Environment (`targets/retro-console-s3/env.py`)

```python
IDF_TARGET = "esp32s3"
FW_FORMAT = "none"
```

#### Target sdkconfig (`targets/retro-console-s3/sdkconfig`)

Key sdkconfig overrides for this hardware:

```
CONFIG_IDF_TARGET="esp32s3"
CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_FATFS_LFN_HEAP=y
CONFIG_FATFS_MAX_LFN=255
```

---

### ES8388 Audio Integration in Retro-Go

Retro-Go's audio subsystem outputs raw PCM samples via its `rg_audio_driver_i2s` driver when `RG_AUDIO_USE_EXT_DAC = 1`. The standard I2S external DAC path in Retro-Go sends PCM data over I2S pins (BCLK, WS, DOUT) — the ES8388 receives this data identically to how a PCM5102A or any I2S DAC would.

**What is needed:**
1. **I2S pin configuration**: Defined via `RG_GPIO_I2S_*` macros in the target `config.h` above. Retro-Go's I2S driver initializes I2S with these pins.
2. **ES8388 codec initialization**: Unlike a passive I2S DAC (PCM5102A), the ES8388 requires I2C register configuration before it will output audio. A hardware init function must be added to the Retro-Go target to configure the ES8388 at startup:
   - Set codec to Slave mode, DAC-only (DECODE)
   - Configure DACCONTROL17/20 = 0x80 (disconnect analog inputs from mixer to prevent noise)
   - Set ADCPOWER = 0xFF (power down ADC)
   - Set DAC output to ALL (LOUT1/ROUT1/LOUT2/ROUT2)
   - MCLK/LRCK ratio = 256

This initialization should be placed in a `rg_board_init()` function called from `rg_system_init()`, or as a custom audio driver init hook.

### SDMMC 4-Bit SD Card in Retro-Go

The upstream `rg_storage.c` already supports SDMMC mode when `RG_STORAGE_SDMMC_HOST` is defined. On ESP32-S3 (`SOC_SDMMC_USE_GPIO_MATRIX` is true), the GPIO matrix allows flexible pin assignment.

**Modification required in `rg_storage.c`**: The upstream code defaults to 1-bit SDMMC (`slot_config.width = 1`). For 4-bit mode, the following changes are needed:

```c
#elif defined(RG_STORAGE_SDMMC_HOST)
    sdmmc_host_t host_config = SDMMC_HOST_DEFAULT();
    host_config.flags = SDMMC_HOST_FLAG_4BIT;     // Changed from FLAG_1BIT
    host_config.slot = RG_STORAGE_SDMMC_HOST;
    host_config.max_freq_khz = RG_STORAGE_SDMMC_SPEED;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;                         // Changed from 1
#if SOC_SDMMC_USE_GPIO_MATRIX
    slot_config.clk  = RG_GPIO_SDSPI_CLK;         // GPIO 38
    slot_config.cmd  = RG_GPIO_SDSPI_CMD;          // GPIO 39
    slot_config.d0   = RG_GPIO_SDSPI_D0;           // GPIO 40
    slot_config.d1   = RG_GPIO_SDSPI_D1;           // GPIO 41
    slot_config.d2   = RG_GPIO_SDSPI_D2;           // GPIO 42
    slot_config.d3   = RG_GPIO_SDSPI_D3;           // GPIO 21
#endif
```

### Custom Input Driver (74HC165 Shift Register)

Retro-Go's input system expects a `rg_input_read_gamepad()` function returning a bitmask of `RG_KEY_*` constants. A custom input driver must be created at `components/retro-go/drivers/input/shift_register.h` that:

1. Reads the 74HC165 shift register via GPIO 3 (CLK), 6 (QH), 7 (PL) — same protocol as the Console OS `input_manager`.
2. Reads the KY-040 rotary encoder via GPIO 1 (CLK), 2 (DT) for volume control.
3. Maps the 8-bit shift register output + encoder rotation to `RG_KEY_UP`, `RG_KEY_DOWN`, `RG_KEY_LEFT`, `RG_KEY_RIGHT`, `RG_KEY_A`, `RG_KEY_B`, `RG_KEY_MENU`, `RG_KEY_START`, `RG_KEY_OPTION`, `RG_KEY_SELECT`.

---

### SD Card File Layout

Both firmware environments share the same SD card. ROMs, cover art, BIOS files, save states, music, and text files all coexist:

```
/sdcard (or /sd in Retro-Go)
├── roms/
│   ├── nes/          # .nes ROM files
│   ├── gb/           # .gb ROM files
│   ├── gbc/          # .gbc ROM files
│   ├── sms/          # .sms ROM files
│   ├── gg/           # .gg ROM files
│   ├── pce/          # .pce ROM files
│   └── doom/         # .wad files
├── romart/           # Retro-Go cover art PNGs (160x168)
│   ├── nes/
│   ├── gb/
│   └── ...
├── retro-go/
│   ├── config/       # Retro-Go configuration JSONs
│   ├── saves/        # Save states
│   └── bios/         # GB/GBC/FDS BIOS files
├── music/            # MP3/FLAC/AAC/WAV/M4A files (Console OS)
└── books/            # .txt files (Console OS Files app)
```

### Implementation Progress

#### Phase 1: Partition Table & OTA Infrastructure ✅ DONE
- [x] Created `partitions.csv` — factory (3MB) + launcher/ota_0 (1MB) + retro-core/ota_1 (3MB) + storage (8.9MB)
- [x] Updated `sdkconfig` → `CONFIG_PARTITION_TABLE_CUSTOM=y`, pointing to `partitions.csv`
- [x] Updated `sdkconfig.defaults` with custom partition + 16MB flash settings
- [x] Implemented `launch_retro_go()` in `ui.c` — OTA partition switch via `esp_ota_set_boot_partition()` + `esp_restart()`
- [x] Wired RETRO-GO menu entry (`selected_game == 3`) to `launch_retro_go()`
- [x] Added error handling: "NO RETRO-GO FW!" / "OTA SET FAILED!" screens with 2s timeout fallback to menu
- [x] **HW TESTED:** Verified `esp_restart()` correctly reboots to `ota_0`. Confirmed bootloader failsafe successfully falls back to Console OS if Retro-Go partition is empty.

#### Phase 2: Retro-Go Repository Clone & Target Creation ✅ DONE
- [x] Cloned `ducalex/retro-go` into `C:\Users\Dell\esp\projects\retro-go`
- [x] Created `components/retro-go/targets/retro-console-s3/config.h` with all HW pin mappings
- [x] Created `targets/retro-console-s3/env.py` (IDF target = esp32s3)
- [x] Created `targets/retro-console-s3/sdkconfig` (16MB flash, octal PSRAM, FATFS, 240MHz)
- [x] Registered target in `components/retro-go/config.h`
- [x] Return bridge: set `RG_APP_LAUNCHER = "factory"` so exit reboots to Console OS
- [ ] **PENDING:** Build using `python rg_tool.py --target=retro-console-s3 build-img`

#### Phase 3: Display Driver Verification ✅ DONE
- [x] Confirmed ILI9341 init sequence matches Console OS Profile 2 calibration
- [x] Added `RG_SCREEN_ROTATE 2` (180 degree flip) to correct upside-down rendering issue

#### Phase 4: SD Card SDMMC 4-Bit Integration ✅ DONE
- [x] Modified `rg_storage.c` for 4-bit SDMMC with GPIO matrix pins (38/39/40/41/42/21)
- [x] **HW TESTED:** Verified ROM loading from `/sd/` (Doom loaded successfully)

#### Phase 5: ES8388 Audio Driver ✅ DONE
- [x] Added `RG_AUDIO_INIT_CUSTOM()` to `config.h` and hooked into `rg_system.c` to properly initialize the ES8388 over I2C on boot, fixing loud static/beeping on startup.
- [x] I2S pins (BCK=15, WS=16, DATA=17, MCLK=47) configured in target config.h
- [x] **HW TESTED:** Amplifier enable pin (GPIO 48) correctly driven HIGH to enable speaker output. SD card audio and Retro-Go audio verified working. Minor hardware ticking remains during soft-reboot transitions.

#### Phase 6: 74HC165 Input Driver ✅ DONE
- [x] Used built-in `RG_GAMEPAD_SERIAL_MAP` (74HC165 protocol matches serial driver)
- [x] Mapped 74HC165 bits (GPIO 3/6/7) to RG_KEY_* via serial map in config.h
- [ ] **PENDING:** KY-040 encoder (GPIO 1/2) volume control hook
- [x] **HW TESTED:** Verified D-Pad navigation (Enter/L/R) works in launcher

#### Phase 7: Return Bridge & Polish ✅ DONE
- [x] Added "Quit to Console OS" button explicitly to the main About menu (`rg_gui_about_menu`).
- [x] Modified `update_boot_config` to cleanly erase the `otadata` partition when `RG_APP_FACTORY` is requested.
- [x] **HW TESTED:** Full round-trip Console OS → RETRO-GO → exit → Console OS works perfectly without manual resets.

#### Phase 8: Console OS Polish & Bugfixes ✅ DONE
- [x] **I2C Volume Bug:** Replaced unverified `es8388_write_reg` I2C calls with a robust retry loop to prevent asymmetrical left/right volumes when the I2C bus NACKs due to heavy SD/I2S DMA streaming contention.
- [x] **ADF Core Bugfix:** Fixed a critical bug in ESP-ADF's `i2c_bus_v2.c` `AUDIO_RET_ON_FALSE` macro that silently converted `ESP_ERR_TIMEOUT` into `ESP_OK`.
- [x] **RNG Seed:** Added `srand(esp_random())` at startup to ensure the music shuffle and game spawns are truly randomized instead of using the default deterministic sequence.
- [x] **Game Balancing:** Scaled up Pong base speed (60 FPS) and increased paddle/ball velocity. Sped up Tetris piece dropping and increased the speed scaling per level.

### Advantages

- **Clean separation**: Multimedia applications and emulation firmware are completely independent.
- **Only two firmware images** to maintain (Console OS + Retro-Go).
- **Retro-Go remains largely unchanged** from upstream, simplifying future updates.
- **No duplication** of ROM browsers, save-state systems, or emulator management code.
- **Native games** (Tetris, 2048, Pong) continue to run directly inside the Console OS without rebooting.
- **Full Retro-Go feature set**: ROM browser, favorites, recently played, save states, cover art, scaling/filters, turbo/fast forward, in-game menu — all available without reimplementation.

## Reference Documentation (MCP Servers)
- `retro-go Docs`: Core OS architecture, game sessions, emulator framework, porting guide.
- `s3-node-repo Docs`: LVGL-based OS with radial app menu, DOOM integration, display/input patterns.
- `esp-adf Docs`: Audio pipeline management, ES8388 driver, codec HAL integration.

## Environment Paths
- **ADF:** `C:\Users\Dell\esp\esp-adf`
- **IDF:** `C:\Users\Dell\esp\v5.3.4\esp-idf`
- **Retro-Go:** `C:\Users\Dell\esp\projects\retro-go`


- **Audio FX (DSP) App & Font Fix**: Renamed the Audio FX application to DSP app in the UI and restored its standard fonts.
- **RSVP File Browser Partial Redraws**: Upgraded the .txt file list UI to use optimized partial redraws, eliminating full-screen flickering during file navigation.
- **Retro-Go ESP-IDF v5 Compatibility Fix**: Identified and mitigated a strict GPIO validation panic (assert failed in LEDC and I2S initialization) in ESP-IDF v5 caused by hardcoding LCD backlight and audio amplifier enable pins to -1. Correctly omitting these unassigned pins allows all Retro-Go emulator cores to boot successfully without buzzer static or crash loops.


## Console v6 Updates: Added System Status graph, improved OTA UI, Battery % Icon, Codec optimizations
