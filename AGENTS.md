# IoT Project — Agent & Environment Guide

## 1. ESP-IDF Build Environment

### Environment Variables

| Variable              | Value                                  |
| --------------------- | -------------------------------------- |
| `IDF_PATH`            | `C:\esp\v6.0\esp-idf`                 |
| `IDF_TOOLS_PATH`      | `C:\Espressif\tools`                   |
| `IDF_PYTHON_ENV_PATH` | `C:\Espressif\tools\python\v6.0\venv` |

### Available Commands

| Command          | Purpose                                  |
| ---------------- | ---------------------------------------- |
| `idf.py build`   | Compile the project                      |
| `idf.py flash`   | Flash firmware to device                 |
| `idf.py monitor` | Open serial monitor                      |
| `idf.py menuconfig` | Open configuration menu               |
| `esptool.py`     | Low-level ESP flash operations           |
| `espefuse.py`    | eFuse management                         |
| `espsecure.py`   | Security & signing tools                 |
| `otatool.py`     | OTA partition operations                 |
| `parttool.py`    | Partition table operations               |

### Workflow Rules

- **Always flash to device after making code changes.** The device is on **COM7**.
- Use `idf.py build && idf.py -p COM7 flash` for a quick build-and-flash cycle.

---

## 2. Target Hardware — Waveshare ESP32-S3 LCD 3.49

### 2.1 SoC — ESP32-S3R8

| Spec       | Detail                                           |
| ---------- | ------------------------------------------------ |
| Core       | Xtensa LX7 dual-core, up to 240 MHz             |
| Wi-Fi      | 2.4 GHz 802.11 b/g/n                            |
| Bluetooth  | Bluetooth 5 (LE)                                |
| Antenna    | Onboard patch (default); IPEX Gen-1 connector for external antenna (remove resistor to switch) |
| SRAM       | 512 KB                                           |
| ROM        | 384 KB                                           |
| PSRAM      | 8 MB (octal, stacked)                            |
| Flash      | 16 MB — W25Q128JVSI (SPI NOR)                    |

### 2.2 I/O Expander — TCA9554PWR

- 8-bit I2C GPIO expander for additional I/O lines.

### 2.3 LCD Display

| Parameter     | Value               | Parameter   | Value              |
| ------------- | -------------------- | ----------- | ------------------ |
| Panel type    | LCD                  | Size        | 3.49 inch          |
| Resolution    | 172 × 640            | Colors      | 16.7 M             |
| Brightness    | 350 cd/m²            | Contrast    | 1200:1             |
| Display IF    | QSPI                 | Driver IC   | AXS15231B          |
| Touch IF      | I2C                  | Touch type  | Capacitive         |

### 2.4 Audio

| IC      | Role                  | Description                                                       |
| ------- | --------------------- | ----------------------------------------------------------------- |
| ES8311  | DAC (playback)        | High-performance, low-power audio codec (DAC)                     |
| ES7210  | ADC (recording)       | High-performance, low-power audio codec (ADC), multi-mic input    |

- Dual digital microphone array — supports noise reduction, AEC, voice recognition, and wake-word detection.
- Speaker output via **MX1.25 2-pin connector** (back side) for external speaker.

### 2.5 Sensors

| Sensor  | IC          | Interface | Capability                                      |
| ------- | ----------- | --------- | ------------------------------------------------ |
| 6-axis IMU | QMI8658  | I2C       | 3-axis accelerometer + 3-axis gyroscope; posture sensing, motion recognition, step counting |
| RTC     | PCF85063    | I2C       | Real-time clock with time-keeping function       |

### 2.6 Storage

- **Micro SD card slot** — supports FAT32, for data expansion, logging, and media playback.

### 2.7 Buttons

| Button    | Location  | Function                                                        |
| --------- | --------- | --------------------------------------------------------------- |
| BOOT      | Back side | Hold BOOT + press RESET to enter download mode                  |
| RESET     | Back side | System reset; combined with BOOT for download mode              |
| PWR       | Back side | Power control (software-configurable for Li-po battery control) |

### 2.8 Connectors & Interfaces

| Interface                | Description                                                    |
| ------------------------ | -------------------------------------------------------------- |
| USB Type-C               | Programming and serial log output                              |
| IPEX Gen-1               | External antenna connector (switch from patch via resistor)    |
| MX1.25 2-pin (back)      | Speaker output                                                 |
| MX1.25 2-pin (back)      | Li-po battery input (3.7 V, with onboard charging)             |
| 22-pin 2.54 mm header    | Through-hole pads for external modules and expansion           |
| Micro SD slot            | FAT32 SD card for storage expansion                            |

---

## 3. Project Conventions

### Directory Layout

```
time/
├── main/            # Application source code
├── components/      # Reusable ESP-IDF components
├── managed_components/ # IDF component manager dependencies
├── CMakeLists.txt   # Top-level build config
├── sdkconfig        # Project configuration
└── AGENTS.md        # This file
```

### Code Style

- Language: **C** (ESP-IDF standard).
- Follow ESP-IDF coding conventions and naming (`snake_case` functions, `CamelCase` types).
- Keep component headers in `include/` subdirectories.
- Use `ESP_LOGI`, `ESP_LOGW`, `ESP_LOGE` for logging — never `printf` in production code.

### Commit Messages

- Use conventional commits: `feat(display): ...`, `fix(wifi): ...`, `chore: ...`.

---

## 4. Pin & Peripheral Quick Reference

| Function        | GPIO        | Notes                        |
| --------------- | ----------- | ---------------------------- |
| I2C0 SDA        | GPIO 47     | RTC (PCF85063), IMU (QMI8658)|
| I2C0 SCL        | GPIO 48     | RTC (PCF85063), IMU (QMI8658)|
| I2C1 SDA        | GPIO 17     | Touch (AXS15231B)            |
| I2C1 SCL        | GPIO 18     | Touch (AXS15231B)            |
| LCD CS          | GPIO 9      | QSPI Display                 |
| LCD PCLK        | GPIO 10     | QSPI Display                 |
| LCD DATA0-3     | GPIO 11-14  | QSPI Display                 |
| LCD RST         | GPIO 21     | Display hardware reset       |
| Backlight PWM   | GPIO 8      | LCD backlight                |
| BOOT button     | GPIO 0      | Also enters download mode    |

---

## 5. Common Tasks

### Build & Flash

```bash
idf.py build
idf.py -p COM7 flash
```

### Monitor Serial Output

```bash
idf.py -p COM7 monitor
```

### Clean Build

```bash
idf.py fullclean
idf.py build
```

### Add an IDF Component

```bash
idf.py add-dependency "espressif/button>=3.0"
```

---

## 6. Known Pitfalls & Solutions

### 6.1 AXS15231B QSPI Display — Rotation / Orientation

**Problem:** Physical panel is 172×640 (portrait). Using in landscape (640×172) requires correct data layout.

**What does NOT work:**
- `esp_lcd_panel_swap_xy(panel, true)` — only swaps the window coordinate parameters in `draw_bitmap`, does NOT rearrange pixel data. Results in content repeating 3-4 times across the screen.
- LVGL software rotation (`lv_display_set_rotation`) with a single full-screen buffer — flush callback always sends the full buffer, rotation is ignored.
- Adding a MADCTL (0x36) command to init_cmds — driver already handles MADCTL internally via `swap_xy`/`mirror` API; duplicate commands conflict.

**Root cause:** In QSPI mode, `panel_axs15231b_draw_bitmap` **only sends CASET (column address), never RASET (row address)**. Pixel data must always be laid out as portrait (172-wide rows), regardless of logical orientation.

**Solution:** Keep LVGL display as landscape 640×172. In the flush callback, **manually transpose** the framebuffer from landscape to portrait format before DMA transfer:

```c
// portrait[py][col] = landscape[(lh-1-col)][py]  (with Y-flip to avoid mirror)
for (int row = py; row < py + lines; row++) {
    for (int col = 0; col < pw; col++) {
        *dst++ = landscape[(lh - 1 - col) * lw + row];
    }
}
esp_lcd_panel_draw_bitmap(panel, 0, py, pw, py + lines, trans_buf_1);
```

- DMA buffer (`trans_buf_1`) size = `172 × N_lines × 2` bytes (portrait strip width = 172).
- Use `taskYIELD()` after each DMA strip to prevent WDT starvation (full-screen transpose is CPU-intensive).
- `lv_draw_sw_rgb565_swap()` the entire landscape buffer first (sequential access = cache-friendly), then transpose.

---

### 6.2 SNTP Sync Status Race Condition

**Problem:** `esp_sntp_get_sync_status()` returns `SNTP_SYNC_STATUS_COMPLETED` **only once** — on the next call it returns `SNTP_SYNC_STATUS_RESET`. Polling loops that check the status twice (once in `while` condition, once in `if` after) will always see `RESET` the second time.

**Solution:** Set a `volatile bool s_sntp_synced = false` flag in the sync notification callback, and check the flag instead of calling `get_sync_status()` repeatedly.

```c
static void sntp_sync_cb(struct timeval *tv) {
    s_sntp_synced = true;
}
void sntp_wait_sync(void) {
    int retry = 0;
    while (!s_sntp_synced && retry++ < 30)
        vTaskDelay(pdMS_TO_TICKS(1000));
}
```

---

### 6.3 NTP Sync Task — WiFi Not Connected at Startup

**Problem:** If the NTP task starts before WiFi connects, `wifi_is_connected()` returns false. If the task then sleeps for the refresh interval (e.g. 1 hour), the main UI never loads.

**Solution:** Add a wait loop at task start before the main sync loop:

```c
while (!wifi_is_connected())
    vTaskDelay(pdMS_TO_TICKS(500));
```

---

### 6.4 FontAwesome Symbols in CJK Font Labels

**Problem:** LVGL symbols like `LV_SYMBOL_IMAGE` (U+F03E, FontAwesome private use area) are **not included** in CJK fonts (`lv_font_source_han_sans_sc_*`). They render as empty boxes (□).

**Rule:** Never mix `LV_SYMBOL_*` macros into labels that use a CJK font. Either:
- Use `lv_font_montserrat_*` for symbol-only labels.
- Or omit the symbol and use plain text.

---

### 6.5 Task Stack Sizes

| Task          | Minimum Stack | Notes                                    |
| ------------- | ------------- | ---------------------------------------- |
| `app_main`    | 8192 bytes    | Increased from default 3840; WiFi init is heavy |
| `LVGL`        | 8192 bytes    | Handles flush + UI rendering             |
| `NTP`         | 4096 bytes    | sntp + RTC set + LVGL lock              |
| `Weather`     | 6144 bytes    | HTTP client + JSON parsing              |
| `Clock`       | 4096 bytes    | UI label updates only                   |
| `Button`      | 2048 bytes    | GPIO polling only                       |

---

### 6.6 DMA Buffer Constraints

- DMA buffer must be allocated with `MALLOC_CAP_DMA` (internal SRAM, not PSRAM).
- Max single DMA transfer on ESP32-S3 is limited; keep `max_transfer_sz` in `spi_bus_config_t` equal to `LVGL_DMA_BUFF_LEN`.
- For QSPI portrait strips: `LVGL_DMA_BUFF_LEN = LCD_V_RES × N_lines × 2` (e.g. 172 × 40 × 2 = 13760 bytes).

---

### 6.7 BOOT Button (GPIO 0)

- GPIO 0 is the BOOT button. It has an internal pull-up; reads LOW when pressed.
- Safe to use as a general-purpose input after boot.
- Do **not** drive it LOW during reset or it enters download mode.
- Useful for runtime controls (e.g. brightness cycling) without needing extra hardware.

### 6.8 Battery Power Latch (TCA9554 IO6)

**Problem:** When powered from battery (no USB), releasing the PWR button cuts power immediately.

**Root cause:** The board uses a hardware power latch circuit controlled via TCA9554 I/O expander **IO6** (address 0x20, on I2C bus 0). Pressing PWR temporarily enables power, but the MCU must drive IO6 HIGH to keep the latch active.

**Solution:** Call `power_latch_enable()` in `app_main()` immediately after `i2c_master_init()`:

```c
/* Set TCA9554 IO6 as output, drive HIGH to keep battery power latched */
cfg_val &= ~(1 << 6);  // IO6 = output
out_val |= (1 << 6);   // IO6 = HIGH
```

**Notes:**
- Same TCA9554 device (0x20) is used for PA enable (IO7) — operations are independent.
- To power off programmatically: set IO6 LOW via TCA9554.
- GPIO 16 can be read to detect if board is running on battery power.
