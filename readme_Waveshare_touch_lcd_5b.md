# Battery-Emulator on Waveshare ESP32-S3-Touch-LCD-5B

**A community-developed 1024 x 600 touchscreen port, with local battery controls and an experimental RS485 coolant-pump link.**

This guide describes the Waveshare work in the [casaantoes-eng/Battery-Emulator fork](https://github.com/casaantoes-eng/Battery-Emulator), based on [Dala's Battery-Emulator](https://github.com/dalathegreat/Battery-Emulator).

The fork adds a dedicated board definition, RGB display and touch driver, an English dashboard, a single-page 98-cell monitor, local current/SOC controls, and a Waveshare-side pump controller. The latest source reviewed for this guide contains **UI200** and the **PDS199 RS485 RX fix**.

This is **community support in this fork**, not a claim of upstream acceptance, official Waveshare certification or production readiness. Do not assume the upstream web installer provides this variant. Use the source-build instructions below.

> **Safety and scope**
> Repurposed EV batteries contain hazardous voltage and stored energy. Follow the battery/inverter commissioning instructions and applicable electrical requirements. This touchscreen is not a safety controller, an emergency stop or a substitute for BMS protections. A visible CAN status or a working display does not establish that the installation is safe.
>
> The pump can run in an OEM fallback mode when CAN control messages disappear. Removing power from the LilyGo or disconnecting pump CAN is **not** a reliable way to stop it.

## Contents

- [Hardware and tested setup](#hardware-and-tested-setup)
- [Features](#features)
- [SOC window and rescaling](#soc-window-and-rescaling)
- [Coolant-pump integration](#coolant-pump-integration)
- [Build and upload](#build-and-upload)
- [Implementation and pin assignments](#implementation-and-pin-assignments)
- [What has been tested](#what-has-been-tested)
- [Troubleshooting](#troubleshooting)
- [Files added or adapted](#files-added-or-adapted)
- [Next work and contributions](#next-work-and-contributions)

## Hardware and tested setup

| Item | Setup used during development |
|---|---|
| Display board | **Waveshare ESP32-S3-Touch-LCD-5B**, SKU 28151 |
| Screen | Capacitive touch, **1024 x 600**, RGB interface |
| MCU / memory | ESP32-S3, 16 MB flash, 8 MB PSRAM |
| Battery | Repurposed Kia Soul battery, 98 cells reported |
| Inverter | GoodWe **GW10K-ET PLUS+** |
| Optional pump bridge | Original **LilyGo T-CAN485**, ESP32; not T-2CAN |
| Pump used in bench tests | Hyundai/Kia rear battery coolant pump **375W5-K4000 OS EV BAT** |

The 800 x 480 LCD-5, other screen sizes and T-2CAN must not be assumed pin- or firmware-compatible. The underlying Battery-Emulator supports other protocols, but this particular display layout and commissioning evidence concern the setup above.

Hardware documentation: [Waveshare LCD-5 family](https://docs.waveshare.com/ESP32-S3-Touch-LCD-5) and [published schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-5/ESP32-S3-Touch-LCD-5-Sch.pdf).

## Features

### Main dashboard

Nine consistently sized cards provide:

- **State of charge:** real battery SOC, not the rescaled value sent to the inverter.
- **State of health**, when available.
- **Maximum battery temperature**.
- **Pack voltage**.
- **Signed current** and **signed power**.
- **User charge-current limit** and the BMS-reported available charge current.
- **User discharge-current limit** and the BMS-reported available discharge current.
- **Coolant-pump status**, including manual/automatic command when ready and current feedback.

The header shows Wi-Fi status, SSID, signal-strength bars, CAN-data availability, **Rescale SOC**, and **Cell Monitor** navigation. The footer distinguishes charging, discharging, idle and no-data states. Battery-Emulator's sign convention is retained: positive power means charging.

Missing battery data is shown as unavailable, not filled with demonstration readings. `CAN: RECEIVING` uses the base firmware's battery detection/alive indication; it is not an independently timestamped validity check for every displayed value.

### Charge and discharge limits

The arrow controls adjust user limits from **0 to 40 A**, in **5 A steps**. They update the existing Battery-Emulator settings rather than introducing an independent current-control protocol. The displayed BMS availability is separate from the selected user limit.

Arrow presses do not directly trigger a nonvolatile-memory save. However, another action using the shared `store_settings()` routine, including SOC Save, can persist those current settings as well.

### Single-page cell monitor

All **98 positions** are visible on one page:

- Top plot: **C1 to C49**.
- Bottom plot: **C50 to C98**.
- Fixed **3.5 to 4.2 V** scale on both plots.
- Numbered cell positions, minimum/maximum with cell IDs, and pack delta.
- Counts of readings below the scale, above the scale and missing.
- Warning when the reported BMS cell count differs from 98.

Values outside the plot are not replaced with the scale boundary: minimum/maximum and out-of-range counts retain the actual readings. A reading exactly at 3.5 V lies at the baseline and may have no visible bar height. Use the reading count to distinguish that from missing data.

**This scale is a display choice, not a new battery protection limit.** The UI currently targets 98 cells; it is not a generic paginated monitor for every battery supported by the upstream project.

### Touch handling and responsiveness

The interface retains the input-first scheduling developed in UI188:

- One action per contact, with a confirmed release before another action.
- No automatic long-press repetition or drag-through commands.
- An 80 ms debounce applies to repeated touches on the same control.
- Local menu actions do not force a full data/Wi-Fi refresh first.
- Immediate refresh of invalidated regions for recognized actions.
- No animated menu transitions or additional full-screen framebuffer.

The user reported a substantial improvement with UI188. Later, heavier views still produced slow frames in diagnostic logs, so this is not a promise of constant frame rate or zero latency. Menu sizing and visual refinement remain ongoing work.

## SOC window and rescaling

Tap **STATE OF CHARGE** to open **SOC WINDOW**.

| Control | Behavior |
|---|---|
| Min SOC | Adjustable from **-10% to 50%** |
| Max SOC | Adjustable from **50% to 100%** |
| Arrows | **10 percentage points** per press, clamped at field bounds |
| `10-100%` | Loads the chosen preset into the editor |
| Save | Applies a valid window and requests persistence |
| Cancel | Discards the editor's unsaved changes |

**Min must be strictly below Max.** The negative minimum range mirrors the advanced web setting; it is not a recommendation to discharge a battery below its usable range. BMS and cell-voltage protections remain essential.

Opening the menu reads the current settings. It does not silently replace them. To use the development setup's selected window, press **10-100%**, then **Save**. The values are not reset to that preset on every boot.

Existing decimal settings from the web are retained: for example, incrementing 12.5% produces 22.5%, not 20%. A best-effort conflict check rejects a Save if the SOC settings changed elsewhere while the menu was open. Avoid simultaneous edits from the touchscreen and web interface.

### What Rescale SOC actually does

The header button shows **ON/OFF**. Pressing it sets the existing scaling setting to **1 = Yes** and requests persistence. Pressing it again while ON does not disable it. To disable scaling, use the existing web setting.

This is **not a one-shot BMS calibration**. With scaling enabled:

```text
Reported SOC = 100 x (real SOC - minimum) / (maximum - minimum)
```

The input is clamped to the selected window. For **10%-100%**:

| Real SOC | Rescaled SOC |
|---:|---:|
| 10% | 0% |
| 55% | 50% |
| 100% | 100% |

The base algorithm also adjusts reported usable capacity. The touchscreen's main SOC card continues to show **real SOC**.

Save of the window does not automatically turn scaling on. If scaling is already ON, changing the window affects the next normal calculation cycle. This mechanism changes what the inverter is told; it does not add a separate physical charge/discharge cutoff.

### Persistence limitations

SOC Save and Rescale use the same `store_settings()` mechanism as the web UI. That routine stores several current settings, not just SOC. Flash writes are explicit and infrequent but synchronous, and may briefly affect responsiveness. The UI's storage acknowledgment is not an independent read-back verification; check the settings after a reboot during commissioning.

## Coolant-pump integration

### Architecture

```text
Battery / GoodWe CAN
        |
   Waveshare LCD-5B
        |
   Dedicated RS485 (PB1)
        |
   LilyGo T-CAN485
        |
   Separate pump-only CAN
        |
   375W5-K4000 battery coolant pump
```

**Never merge pump CAN with the battery/inverter CAN bus.** The Waveshare pump module sends RS485 requests; it does not transmit the pump's OEM CAN commands on the Battery-Emulator CAN bus.

**Receiver availability:** the reviewed fork archives contain the Waveshare-side module, but do **not yet contain the standalone LilyGo receiver project**. A compatible receiver was developed and used during testing (PDS193). Its source and installation guide still need to be published alongside this feature. Do not assume selecting a normal LilyGo Battery-Emulator build installs that receiver.

### Menu and command range

Tap **COOLANT PUMP** to open the menu. It displays the manual command as a percentage, pump-current feedback in amperes, mode/link state and the last sent command when available.

- **0% = raw `0x00`**.
- **100% = raw `0x99`**, decimal 153.
- This is a percentage of the chosen command range, **not calibrated RPM or flow**.
- The original raw steps are retained: 0, 5, 10, ... 150, 153. Displayed percentage increments are therefore roughly 3-4 points, not 5% or 10% pump steps.
- Current uses the observed OEM feedback encoding, byte 5 divided by 10.
- `-- A` means current feedback is unavailable/invalid, not measured zero current.
- Closing the menu does not stop the pump.
- **Manual zero does not disable automatic cooling.**

Arrows remain grey without a valid link and healthy pump feedback. At 0%, the down arrow is also grey normally; at the maximum, the up arrow is grey. This is intentional gating, not a UI fault.

### Automatic mode and communication loss

The current Waveshare control rules are:

- Maximum battery temperature **above 30 C** sets automatic demand to raw 153.
- Temperature **at or below 25 C** clears the automatic latch.
- Between those thresholds, the latch retains its previous state.
- Automatic demand takes precedence over manual zero.
- Invalid temperature, unavailable link or unavailable pump readiness clears the automatic latch.
- Nonzero manual requests require valid receiver readiness. Settings are not automatically stored for the pump.

Temperature validity uses the base battery-alive indication and a plausibility range, not an independent temperature-sample timestamp. Autonomous thermal operation needs further validation.

The developed LilyGo receiver has a **750 ms local request watchdog** and attempts to keep sending zero on pump CAN when valid RS485 requests cease. This is a software request, not a guaranteed physical stop. CAN wiring failure, loss of LilyGo power or reboot may trigger the pump's observed fallback operation.

### PB1 protocol summary

PB1 is a custom, dedicated point-to-point binary protocol, **not Modbus**.

| Parameter | Value |
|---|---|
| RS485 | 19200 baud, 8 data bits, no parity, 1 stop bit |
| Packet | 20 bytes, starts `A5 5A`, version 1 |
| Types | Request 1, reply 2 |
| Matching | Session nonce, sequence, echoed demand |
| Integrity | CRC16/MODBUS over the first 18 bytes, little-endian CRC |
| Request period | 250 ms |
| Waveshare reply deadline | 200 ms |
| Waveshare link timeout | 1000 ms |
| Feedback age required on acceptance | At most 500 ms, with healthy flags/alarm bytes |
| Tested receiver turnaround | Nonblocking 20 ms guard before reply |

Reply byte 10 echoes the requested demand, not measured speed. Feedback bytes and status are separate. The developed receiver starts/rearms through a zero-command handshake. CRC and sequence handling are not authentication or permanent replay protection; keep the bus dedicated.

The rear pump bench protocol uses standard CAN `0x4DE` for demand, `0x523` with purge off, and `0x4E4` feedback at 500 kbit/s. These reverse-engineered commands must not be generalized to other pumps: the front `36910-0E650 OS/DE EV PE` is different.

### The RX issue and the working configuration

During development, the LilyGo accepted requests and an Ethernet tap captured valid replies, while the Waveshare read **zero bytes**. Both UART1 and UART2 showed the symptom in a minimal program. A 20 ms reply delay and moving the IDF console to USB did not resolve it alone.

In the controlled PDS198 test, reception began after the following grouped configuration sequence. PDS199 integrated it into the pump module, **after `Serial2.begin()` and HAL pin reservation, before starting the worker**:

```cpp
gpio_set_direction(GPIO_NUM_43, GPIO_MODE_INPUT);
uart_set_pin(UART_NUM_2, 44, 43, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
Serial2.setRxFIFOFull(1);
Serial2.setRxTimeout(2);
```

The implementation checks the UART driver and return values and leaves the pump link disabled on setup failure. The user subsequently confirmed that this resolved reception in the full integration.

**This validates the sequence, not which individual call was solely responsible.** Do not replace it with a pin swap based on instructions for the 7-inch Waveshare boards, and do not patch the generic RS485 layer for every board.

## Build and upload

### Prerequisites

- Git installed and available to build tools.
- PlatformIO Core; development used **6.1.19**.
- Network access for dependency downloads.
- The correct **LCD-5B** board and a USB data cable.
- A backup of the currently working source/configuration before changing versions.

Get this fork and run commands from its root:

```sh
git clone https://github.com/casaantoes-eng/Battery-Emulator.git
cd Battery-Emulator
```

Cloning preserves commit metadata for the build. A source ZIP can also be used; messages that Git cannot describe a non-Git directory do not, by themselves, identify a fatal compiler error. Git must still be installed for the current version-generation script.

### Pinned configuration

| Setting | Current fork configuration |
|---|---|
| Environment | **`waveshare_LCD_5B`**, case-sensitive |
| Platform | pioarduino **55.03.311** release ZIP |
| Framework in reviewed build | Arduino-ESP32 3.3.11 / ESP-IDF 5.5.5 |
| Board profile | `esp32s3_flash_16MB` |
| PSRAM memory type | `qio_opi` |
| LovyanGFX | **1.2.26** |
| LVGL | **9.3.0** |
| Package directory | `.pio/packages` in the current distribution configuration |
| Specific SDK input | `sdkconfig.waveshare.defaults` |

Do not replace the environment with `waveshare_330`: that is a different board. Local package storage avoids a user-specific path, but pioarduino also manages bootstrap tools in its core directory; this is not complete isolation of every installed tool.

### Compile

```sh
pio run -e waveshare_LCD_5B
```

If `pio` is not available in a Windows PowerShell terminal, this was the executable location in the development installation:

```powershell
& "$env:USERPROFILE/.platformio/penv/Scripts/platformio.exe" run -e waveshare_LCD_5B
```

Use your actual PlatformIO executable location if it differs. Run one complete command at a time; do not paste terminal prefixes such as `PS ...>` or continuation prompts `>>`.

### Windows tool-path workaround

The development installation required two fixes in the installed pioarduino `builder/frameworks/espidf.py`: the GCC path for linker-script preprocessing and the `objdump` path. The script expected a flat `bin` directory, while the package used a nested `xtensa-esp-elf/bin` directory.

This repository includes [`tools/fix_pioarduino_tool_paths.py`](tools/fix_pioarduino_tool_paths.py) to package that workaround. It is **not run automatically** by the firmware build or by the current CI workflow.

For the known path error, stop other builds, preview first:

```powershell
& "$env:USERPROFILE/.platformio/python3/python.exe" ./tools/fix_pioarduino_tool_paths.py
```

Read the identified platform and proposed changes. To apply explicitly:

```powershell
& "$env:USERPROFILE/.platformio/python3/python.exe" ./tools/fix_pioarduino_tool_paths.py --apply
```

Alternatively, use an installed Python 3 interpreter, such as `py -3`, if the PlatformIO Python path differs.

The installer recognizes only the supported platform version and known expressions, accepts the earlier manual fixes, creates a timestamped backup, checks syntax and is idempotent. Multiple matching platforms require an explicit `--platform-dir`. It modifies a shared installed platform file, so do not run it during another build. Restore the printed backup to roll back.

The manual fix enabled a successful Windows build during development. The distributable installer passed host tests, but **its application on a fresh machine still needs validation**. Do not use it as a general repair for missing manifests, locked files or unrelated compiler errors. Do not reinstall all packages or edit generated `build.ninja` files as a first response.

### Upload and monitor

After a successful build, identify the Waveshare USB port with `pio device list`. Example for a board currently on COM4:

```sh
pio run -e waveshare_LCD_5B -t upload --upload-port COM4
pio device monitor -p COM4 -b 115200
```

COM4 was used for Waveshare and COM5 for LilyGo during development; **these are not fixed assignments**. Close any monitor holding the upload port first. No full-flash erase is required for the documented source updates.

Verify the UI200 banner and, when using the pump module, the PDS199 RX setup result. Existing `P194` diagnostics remain in the pump source intentionally; their label does not mean the working RX fix is absent.

### Configuration files and generated files

Keep these source inputs:

- [`sdkconfig.be_size.defaults`](sdkconfig.be_size.defaults): shared size/build settings.
- [`sdkconfig.waveshare.defaults`](sdkconfig.waveshare.defaults): LCD-5B-specific copy of base settings plus the USB console selection.
- [`include/lv_conf.h`](include/lv_conf.h): LVGL configuration.

The `.gitignore` must allow both intentional `.defaults` files. In the workflow used here, `sdkconfig.defaults` and `sdkconfig.waveshare_LCD_5B` are generated working configuration, not the place to maintain permanent board changes. Ignoring a path does not remove it if already tracked.

The board-specific defaults currently duplicate the base contents; keep them synchronized deliberately if shared settings change. Do not blindly delete generated files from a working installation while troubleshooting.

**Do not use `cmake_clean.bat` to build this port.** It is a separate legacy CMake helper and requires review. It removes a relative `build` directory and does not provide the PlatformIO installation flow above.

### GitHub Actions and OTA

The updated `compile-common-image.yml` includes `waveshare_LCD_5B` alongside the original environments. Keep its workflow name consistent with the listener in `add-comment-to-pr.yml`. Inclusion in the matrix is not proof that the first run has passed, and CI compilation is not a hardware test.

The fork retains the base OTA mechanisms and two OTA application partitions of `0x640000` bytes each. **An end-to-end OTA test on this board remains pending.** Do not confuse an OTA application image with a factory/full-flash image. Retaining OTA code does not establish safe updates or recovery under every failure condition.

The existing release workflow also expects the fork's web-installer repository and token configuration; publishing source in Show and tell does not require running that deployment workflow.

## Implementation and pin assignments

| Function | LCD-5B mapping |
|---|---|
| Battery/inverter CAN TX / RX | **GPIO15 / GPIO16** |
| RS485 UART TX / RX | **GPIO44 / GPIO43** |
| Display/touch I2C SDA / SCL | **GPIO8 / GPIO9** |
| GT911 interrupt | **GPIO4** |
| Touch controller | GT911, address 0x5D with 0x14 fallback |
| I/O expander | CH422G; touch reset EXIO1, backlight EXIO2, LCD reset EXIO3 |

RS485 direction switching is provided by the board's automatic circuit. Do not copy the GPIO21 DE control from the separate Waveshare RS485-CAN board: GPIO21 is part of the LCD RGB interface here.

GPIO0 is also an RGB signal and is excluded from the runtime BOOT-button handling in this port. The LCD-specific HAL does **not implement external contactor, precharge or equipment-stop GPIO outputs**. A battery/protocol requiring those physical outputs needs its own verified implementation; compatibility cannot be inferred from the touchscreen alone.

The display uses LovyanGFX's PSRAM framebuffer and one **20,480-byte internal partial LVGL draw buffer**. The LVGL pool is configured to 48 KiB. The connectivity task gets **16,384 bytes of stack for this board only**, following a stack-canary failure and successful bench correction.

The retained RGB pixel clock is 14 MHz, with 40/20/40 timing intervals in both axes. The configured nominal panel scan rate is approximately **17.8 Hz**; this is not a measured UI frame rate. Increasing clocks or memory allocation requires separate validation.

### Bus wiring and termination

Use distinct twisted pairs for RS485 and CAN, appropriate reference/grounding and termination for the actual segment. Do not use HV battery terminals as a communication ground. RS485 A/B naming can vary by device; in the development setup, A-to-A and B-to-B carried valid requests and replies.

The battery/inverter CAN fault was resolved by enabling the Waveshare's onboard CAN termination in that setup. This is a commissioning observation, not a universal topology prescription. RS485 termination was also changed during diagnostics; removing it alone did not resolve reception. Verify final termination at the cable endpoints and do not add a third terminator merely because an analyser is connected.

## What has been tested

| Item | Evidence / current status |
|---|---|
| Display and touch | User-operated on the physical LCD-5B |
| Kia Soul / GoodWe communication | User confirmed inverter recognition, 98 cells and disappearance of the earlier native CAN error after termination adjustment |
| UI188 responsiveness | User reported substantially smoother interaction |
| RS485 transport | Independent Ethernet capture validated 80 request/reply pairs with CRC, session, sequence and demand echo matching |
| GPIO/UART diagnostic | PDS198 phase 2 produced valid matched replies after explicit RX reconfiguration |
| Full RX integration | User confirmed success after PDS199 was integrated |
| Pump raw control | Separate bench experiments demonstrated demand response; selected upper command is 0x99 |
| Complete revised SOC UI | Source and host logic checks performed; reboot persistence and inverter behavior need confirmation in the final assembled setup |
| PDS203 build helper | Host fixture tests performed; fresh-machine installation/build validation pending |
| Automated LCD-5B CI | Workflow entry added; first successful run not established by the reviewed files |
| OTA, thermal automation and long-duration fault tests | Further end-to-end validation required |

Host tests covered packet CRC/framing, session/sequence matching, watchdog boundaries, corrupted inputs, UI actions, SOC ranges, preset/Cancel/Save behavior and selected layout bounds with simulated peripherals. They do not replace an ESP32 build, actual LVGL rendering, electrical measurements or full-system safety testing.

The source archives do not include the contents of device NVS or prove which binary is installed. Record the commit, platform versions, board revision and logs with each meaningful test.

## Troubleshooting

### Pump arrows grey

Check Waveshare `PUMP485 online=... ready=...` first. Do not bypass the gate. The down arrow is expected to be grey at a zero manual setting. Without the receiver, the pump feature remains unavailable while the main display can still operate.

### `rx=0` despite LilyGo replies

Confirm that the **PDS199** setup sequence is present and reports `direction=0 pins=0 fifo=1 timeout=1`. `replies_rejected=0` alone does not prove a clean input: it excludes malformed frames and total absence of bytes. Use the `P194 rx`, `valid`, `crc_bad`, `accepted` and sample diagnostics.

If the symptom returns, inspect runtime pin configuration and the physical return path. Do not infer an A/B reversal, a dead transceiver or a CRC mismatch solely from zero software bytes.

### Slow frames, communication pauses or heap warnings

`UI191` timing lines remain in the UI200 source; the startup banner identifies the actual revision. Timing values are microseconds and maxima need not come from the same frame. Action timing excludes time before touch detection and physical LCD scanout.

The pump task still contains periodic `Serial.printf` logging and shares processor resources with other tasks. Earlier logs showed long task gaps. This remains a robustness concern, not something the RX fix automatically repaired. Do not treat a burst of host timestamps as proof that every line was generated simultaneously.

Low-memory warnings were observed during development. Do not suppress them or increase buffers without measuring internal heap, stack headroom and long-run behavior.

### Security and feature limitations inherited from size settings

The current size defaults limit the TLS trust bundle to a placeholder certificate and disable some TLS features. They are not a validated configuration for arbitrary secure MQTT/TLS endpoints. Restore an appropriate trusted CA bundle and test the relevant features before relying on on-device TLS.

Keep web credentials and firmware access protected. Review generated logs and configuration before sharing them; source archives do not include every credential stored on the device.

## Files added or adapted

Paths below describe the implementation, not an exhaustive diff against every upstream revision.

| File | Role |
|---|---|
| [`platformio.ini`](platformio.ini) | LCD-5B build environment, pinned dependencies and SDK input |
| [`Software/Software.cpp`](Software/Software.cpp) | Board-specific task stack, GPIO0 exclusion and pump startup |
| [`Software/src/devboard/hal/hal.cpp`](Software/src/devboard/hal/hal.cpp) | Selects the LCD-5B HAL |
| [`Software/src/devboard/hal/hw_waveshare_LCD_5B.h`](Software/src/devboard/hal/hw_waveshare_LCD_5B.h) | Hardware identity and interfaces |
| [`Software/src/devboard/display/display.cpp`](Software/src/devboard/display/display.cpp) | Excludes the generic OLED implementation on this board |
| [`Software/src/devboard/display/display_waveshare_LCD_5B.cpp`](Software/src/devboard/display/display_waveshare_LCD_5B.cpp) | RGB/touch driver, dashboard, cells, pump and SOC menus |
| [`Software/src/devboard/display/waveshare_ui_text.h`](Software/src/devboard/display/waveshare_ui_text.h) | English UI text catalogue |
| [`include/lv_conf.h`](include/lv_conf.h) | LVGL options and fonts |
| [`Software/src/communication/pump/pump_link.h`](Software/src/communication/pump/pump_link.h) | Pump state and API |
| [`Software/src/communication/pump/pump_link.cpp`](Software/src/communication/pump/pump_link.cpp) | PB1 sender/control and PDS199 RX initialization |
| [`sdkconfig.waveshare.defaults`](sdkconfig.waveshare.defaults) | LCD-specific SDK defaults |
| [`boards/esp32s3_flash_16MB.json`](boards/esp32s3_flash_16MB.json) | Existing 16 MB ESP32-S3 profile used by this port |
| [`default_16MB.csv`](default_16MB.csv) | Flash/OTA partition layout |
| [`tools/fix_pioarduino_tool_paths.py`](tools/fix_pioarduino_tool_paths.py) | Explicit installed-platform path workaround |
| [`.github/workflows/compile-common-image.yml`](.github/workflows/compile-common-image.yml) | Build matrix and cache updates |
| [`.gitignore`](.gitignore) | Keeps generated files out while allowing intentional defaults |

The generic RS485 and NVM layers are reused. The working RX fix is local to the Waveshare pump module, not a global change for all hardware.

## Next work and contributions

Planned or pending work includes:

- Publish the compatible standalone LilyGo receiver and its wiring/test guide.
- Refine menu layout and control sizes without regressing responsiveness.
- Add a local **Portuguese (Portugal)** text catalogue; English is currently implemented. Other languages on demand
- Reduce logging-related stalls and validate task/heap behavior under sustained load.
- Validate pump control, current feedback, RS485 loss/recovery and thermal hysteresis end to end.
- Verify SOC persistence and inverter behavior for the selected window.
- Run clean-machine builds, the LCD-5B CI job and real OTA tests.
- Review the separate legacy `cmake_clean.bat` helper rather than using it as the firmware build command.

To help reproduce a problem, include the board revision, commit, environment name, PlatformIO/platform versions, active firmware on each board, wiring/termination description and relevant logs. Avoid changing wiring, UART selection, protocol timing and firmware simultaneously; controlled comparisons were essential to finding the RX configuration that worked.

Read the repository's [contribution guidelines](CONTRIBUTING.md) and [AI policy](AI_POLICY.md) before posting or opening a pull request. Explain and verify the changes you submit. Please distinguish proposals, host tests and physical hardware results.

### Credits and licensing

This work builds on Dala's Battery-Emulator and its contributors, Waveshare's hardware documentation, Espressif's Arduino/ESP-IDF stack, [LovyanGFX](https://github.com/lovyan03/LovyanGFX), [LVGL](https://github.com/lvgl/lvgl), and the [OpenInverter pump documentation](https://openinverter.org/wiki/Hyundai_Kona_EV_Coolant_Pumps).

Development and hardware testing for this fork were carried out by its owner with AI-assisted code and documentation work. That assistance is not independent verification of correctness or safety.

The repository's [LICENSE](LICENSE) and the respective third-party licenses continue to apply. Preserve the original credits, notices and applicable source-distribution requirements.
