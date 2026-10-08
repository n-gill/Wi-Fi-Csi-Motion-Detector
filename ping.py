from ping3 import ping
import time

ESP_IP = "10.211.116.207"

while True:
    delay = ping(ESP_IP, timeout=1)  # returns round-trip time in seconds, or None on timeout
    print(f"RTT: {delay}")
    time.sleep(0.02)