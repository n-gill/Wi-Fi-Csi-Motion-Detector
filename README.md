# Wi-Fi CSI Motion Detection (ESP32)

Detects motion in a room using Wi-Fi Channel State Information (CSI). An ESP32 connects to your Wi-Fi network, captures CSI from received frames, and streams it over UDP to a PC. Python scripts on the PC either log the data to CSV or run a simple real-time motion detector.

```
 ESP32 (ESP-IDF firmware)                         PC (Python)
┌──────────────────────────┐   UDP :5005   ┌──────────────────────────┐
│ CSI callback             │  394-byte     │ data_manager.py  -> CSV  │
│  -> FreeRTOS queue (32)  │ ────────────> │ detection.py     -> alert│
│  -> csi_manager sendto() │   packets     └──────────────────────────┘
└──────────────────────────┘
```

## Files

| File | Runs on | Purpose |
|---|---|---|
| `motion_sensor.c` / `.h` | ESP32 | `app_main`, serial Wi-Fi credential entry, UDP sender task |
| `wifi.c` / `wifi.h` | ESP32 | Wi-Fi init, event handlers, CSI configuration and callback, `csiData` struct |
| `data_manager.py` | PC | Receives packets, prints amplitudes, logs to `csi_log.csv` |
| `detection.py` | PC | Receives packets and runs the motion detector |

## How it works

### Firmware

1. `app_main` creates the CSI queue, initializes Wi-Fi in station mode (20 MHz bandwidth), and starts two tasks.
2. `keyboard_input_task` prompts on the serial console for the SSID, then the password, and connects. After connecting it enables CSI (HT-LTF only, LTF merge and channel filter on).
3. `csi_callback` runs for every CSI report. It ignores non-HT frames (`sig_mode != 1`) and anything longer than 384 bytes, stamps the rest with `esp_timer_get_time()`, and pushes it onto the queue.
4. `csi_manager` drains the queue and sends each entry as one UDP datagram to `UDP_SERVER_IP:UDP_SERVER_PORT`.

### Packet format

Little-endian, packed, 394 bytes total (matches `csiData` in `wifi.h` and `CSI_STRUCT_FMT = "<384sqH"` in the Python scripts):

| Offset | Size | Field | Type |
|---|---|---|---|
| 0 | 384 | `buf` (raw CSI) | `int8_t[384]` |
| 384 | 8 | `timestamp_us` | `int64_t` |
| 392 | 2 | `len` (valid bytes in `buf`) | `uint16_t` |

CSI bytes are interleaved `(imag, real)` signed int8 pairs, one pair per subcarrier. Amplitude is `sqrt(real² + imag²)`.

### Detection algorithm (`detection.py`)

1. Per packet, drop unusable subcarriers (indices 0–1 and 28–35 of the 64 received), leaving 54. Divide the amplitudes by their median to remove overall gain changes.
2. The first 15 packets are stored as a **baseline** and frozen. The room should be still during this time.
3. For each packet after that, compute how far each subcarrier is from its baseline median, scaled by that subcarrier's baseline MAD (median absolute deviation). The packet's **score** is the median of those scaled deviations across subcarriers.
4. If `score > 3.0`, motion is reported, the baseline is discarded, and the detector pauses for 2 seconds and then recalibrates from the next 15 packets.

## Requirements

**Firmware**
- ESP32 dev board (the code uses GPIO 2 as the status LED)
- ESP-IDF 5.x

**PC**
- Python 3.8+
- `pip install numpy pandas matplotlib scipy` (`detection.py` currently imports all four, though only `numpy` is used in the detection logic)

## Setup

### 1. Configure the firmware

In `motion_sensor.c`, set the PC's IP address on the same Wi-Fi network as the ESP32:

```c
#define UDP_SERVER_IP "192.168.1.50"   // your PC's IP
#define UDP_SERVER_PORT 5005
```

`motion_sensor.c` includes `wifi.c` directly (`#include "wifi.c"`), so `wifi.c` should **not** also be listed as a separate source in your component's `CMakeLists.txt`, or you will get duplicate-symbol link errors.

### 2. Build and flash

```bash
idf.py build
idf.py -p <PORT> flash monitor
```

### 3. Enter Wi-Fi credentials

In the serial monitor, type the SSID and press Enter, then the password and press Enter. Credentials are not saved, so you will need to re-enter them after every reset. The LED turns on if the ESP32 has an IP address after connecting.

### 4. Start the PC side

Allow inbound UDP on port 5005 in your firewall, then run one of:

```bash
python data_manager.py   # logs to csi_log.csv (set LOG_TO_CSV = False to disable)
python detection.py      # prints score per packet and flags motion
```

Stop either script with Ctrl+C.

### CSV output

`csi_log.csv` has one row per packet with columns `timestamp_us`, `len`, `amplitudes` (a Python-style list of per-subcarrier amplitudes).

## Tuning

| Setting | Location | Effect |
|---|---|---|
| `score > 3.0` | `detection.py` | Motion threshold. Lower is more sensitive, higher is fewer false alarms |
| Baseline length (`15`) | `detection.py` | Packets used to build the baseline |
| Subcarrier slicing `[2:28] + [36:]` | `detection.py` | Which subcarriers are used. Must match the packet length you receive |
| `time.sleep(2)` | `detection.py` | Pause after a detection |

## Limitations

- CSI is only produced when the ESP32 receives frames, and this firmware only keeps HT (802.11n) frames. Packet rate depends on how much traffic the access point sends to the ESP32. If the rate is low or uneven, generate traffic (for example, ping the ESP32 from the PC).
- The detector assumes 64 subcarriers per packet. Different bandwidth or STBC settings change the packet length and break the hardcoded slicing.
- The baseline is a single 15-packet snapshot, so a person standing in the room during calibration will be treated as the "still" state.
- Detection is single-link and the threshold is not calibrated for any specific environment.
- UDP is unauthenticated and unencrypted. Use on a trusted network only.
