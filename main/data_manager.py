import socket
import struct
import csv
import time
UDP_IP = "0.0.0.0"      # bind to all interfaces, NOT 127.0.0.1
UDP_PORT = 5005

# Matches: typedef struct { int8_t buf[384]; int64_t timestamp_us; uint16_t len; } __attribute__((packed)) csiData;
CSI_STRUCT_FMT = "<384sqH"
CSI_STRUCT_SIZE = struct.calcsize(CSI_STRUCT_FMT)  # should be 394

LOG_TO_CSV = True
CSV_PATH = "csi_log.csv"


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

    csv_file = None
    csv_writer = None
    if LOG_TO_CSV:
        csv_file = open(CSV_PATH, "w", newline="")
        csv_writer = csv.writer(csv_file)
        csv_writer.writerow(["timestamp_us", "len", "amplitudes"])

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

            if csv_writer:
                csv_writer.writerow([frame["timestamp_us"], frame["len"], amps])
                print("WRITTEN")
                csv_file.flush()

    except KeyboardInterrupt:
        print("\nStopped.")
        
    finally:
        if csv_file:
            csv_file.close()
            print(f"Saved log to {CSV_PATH}")


if __name__ == "__main__":
    main()