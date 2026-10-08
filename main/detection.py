import socket
import struct
import csv
import time
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.cm as cm
import ast
from scipy.signal import butter, filtfilt
UDP_IP = "0.0.0.0"      # bind to all interfaces, 
UDP_PORT = 5005
WINDOW = 15
NumSUB = 54 #Number of subcarriers
# Matches: typedef struct { int8_t buf[384]; int64_t timestamp_us; uint16_t len; } __attribute__((packed)) csiData;
CSI_STRUCT_FMT = "<384sqH"
CSI_STRUCT_SIZE = struct.calcsize(CSI_STRUCT_FMT)  # should be 394

motion = False
csi_matrix = np.zeros((15, 54)) 
threshhold_matrix = np.zeros((15, 54)) 
matrix_rows = 0
threshhold_rows = 0
mask = np.ones(NumSUB, dtype=bool)
mask[0:2] = False
mask[28:36] = False

def processing(input: list):
    global matrix_rows, motion, csi_matrix, threshhold_matrix

    clean_input = input[2:28] + input[36:]   # already masked — nothing further needed
    amps = np.array(clean_input, dtype=np.float64)

    row_median = np.median(amps)
    norm_amps = amps / row_median             
    if (matrix_rows < 15):
        csi_matrix[matrix_rows] = norm_amps   
        matrix_rows += 1
        if matrix_rows == 15:
            threshhold_matrix = csi_matrix.copy()  
        return

    # buffer full — slide window
    csi_matrix[:-1] = csi_matrix[1:]
    csi_matrix[-1] = norm_amps

    # --- threshold comparison against frozen baseline ---
    eps = 1e-6
    baseline_median = np.median(threshhold_matrix, axis=0)   # one value per subcarrier column
    baseline_mad = np.median(np.abs(threshhold_matrix - baseline_median), axis=0)

    deviation = np.abs(norm_amps - baseline_median)
    scaled = deviation / (baseline_mad + eps)
    score = np.median(scaled)

    print(f"score={score:.2f}  buffer={matrix_rows}/15")

    if score > 3.0:
        motion = True
        print(">>> MOTION DETECTED 🤬🤬🤬🤬🤬🤬"*10)
        motion = False
        matrix_rows = 0
        threshhold_matrix = None
        time.sleep(2)
        return
def parse_csi_packet(data: bytes):
    if len(data) != CSI_STRUCT_SIZE:
        raise ValueError(f"bad packet size: got {len(data)}, expected {CSI_STRUCT_SIZE}")

    buf_raw, timestamp_us, length = struct.unpack(CSI_STRUCT_FMT, data)
    csi_bytes = buf_raw[:length]
    csi_values = struct.unpack(f"<{length}b", csi_bytes)  # signed int8 values, interleaved imag/real

    # split into (real, imag) pairs and compute amplitude per subcarrier
    subcarriers = []
    for i in range(0, length, 2):
        imag = csi_values[i]
        real = csi_values[i + 1]
        amp = (real ** 2 + imag ** 2) ** 0.5
        subcarriers.append((real, imag, amp))

    return {
        "timestamp_us": timestamp_us,
        "len": length,
        "subcarriers": subcarriers,
    }


def main():
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((UDP_IP, UDP_PORT))
    sock.settimeout(1.0) 
    print(f"Listening on {UDP_IP}:{UDP_PORT}, expecting {CSI_STRUCT_SIZE}-byte packets")




    try:
        while True:
            try:
                data, addr = sock.recvfrom(1024)
                print(f"[{addr}] RAW bytes received: {len(data)}")
            except socket.timeout:
                time.sleep(0.1)
                continue   # ← loop back, checks for Ctrl+C, then waits again

            try:
                frame = parse_csi_packet(data)
            except ValueError as e:
                print(f"[{addr}] {e}")
                continue

            amps = [round(sc[2], 2) for sc in frame["subcarriers"]]
            print(f"t={frame['timestamp_us']}us len={frame['len']} "
                  f"first_amps={amps[:6]}")

            processing(amps)

    except KeyboardInterrupt:
        print("\nStopped.")
        
    finally:

         print(f"Saved log")


if __name__ == "__main__":
    main()