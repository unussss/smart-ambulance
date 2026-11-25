"""
SMART AMBULANCE DETECTION & TRAFFIC SIGNAL PRIORITY SYSTEM
===========================================================
Console / Terminal Logger for Raspberry Pi
Reads real-time USB Serial events and telemetry from Traffic ESP32.
"""

import serial
import time
import sys
import glob

def find_serial_port():
    # Common ports on Linux / Raspberry Pi (including GPIO /dev/serial0) and Windows
    ports = ['/dev/serial0'] + glob.glob('/dev/ttyUSB*') + glob.glob('/dev/ttyACM*') + ['COM3', 'COM4', 'COM5', 'COM6', 'COM7']
    for p in ports:
        try:
            s = serial.Serial(p, 115200, timeout=0.5)
            s.close()
            return p
        except Exception:
            continue
    return '/dev/serial0'

PORT = find_serial_port()
BAUD = 115200

print("="*65)
print("🚑 SMART AMBULANCE TRAFFIC PRIORITY - RASPBERRY PI LOGGER")
print("="*65)
print(f"Connecting to ESP32 on: {PORT} (Baud: {BAUD})...")

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print(f"✅ Successfully Connected to {PORT}!")
    print("Listening for 2-Way Traffic Telemetry & Emergency Events...\n")
    print("-" * 65)

    while True:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            current_time = time.strftime("%Y-%m-%d %H:%M:%S")

            if line.startswith("EVENT:"):
                event_name = line.split("EVENT:")[1].strip()
                if "EMERGENCY_REQUEST_RECEIVED" in event_name:
                    print(f"\n[{current_time}] 🚨🚨 EMERGENCY CORRIDOR ACTIVE! LANE 1 GREEN, LANE 2 RED!")
                elif "DETECTED_AT_JUNCTION" in event_name:
                    print(f"[{current_time}] 📏 {event_name}")
                elif "TIMEOUT" in event_name or "NORMAL" in event_name:
                    print(f"[{current_time}] 🟢 {event_name}\n")
                else:
                    print(f"[{current_time}] ℹ️  {event_name}")

            elif line.startswith("STATUS:"):
                # Clean, single-line in-place telemetry display
                status_data = line.split("STATUS:")[1].strip()
                sys.stdout.write(f"\r[{current_time}] 📊 {status_data}   ")
                sys.stdout.flush()

except serial.SerialException as e:
    print(f"\n❌ Error: Could not connect to {PORT}.")
    print("Ensure the ESP32 is plugged securely into the USB port and permissions are set.")
    print("Tip: Run 'ls /dev/ttyUSB*' or check 'dmesg | grep tty' on Raspberry Pi.")
except KeyboardInterrupt:
    print("\n\n🛑 Logger stopped by user.")
