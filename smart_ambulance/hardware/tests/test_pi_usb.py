import serial
import time

# Phase E: Raspberry Pi USB Test
PORT = '/dev/ttyUSB0' # Might be /dev/ttyACM0 on some systems
BAUD = 115200

print("Testing USB Serial Connection...")

try:
    ser = serial.Serial(PORT, BAUD, timeout=1)
    print("SUCCESS: Opened serial port!")
    print("Waiting for data from ESP32...")
    
    while True:
        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8').strip()
            print(f"Received from ESP32: {line}")
            
except serial.SerialException:
    print(f"FAIL: Could not open {PORT}. Please check connection.")
except KeyboardInterrupt:
    print("\nTest Stopped.")
