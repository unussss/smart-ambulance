"""
=============================================================================
SMART AMBULANCE TRAFFIC PRIORITY SYSTEM - MASTER FLASK SERVER
=============================================================================
Backend engine for the Web Dashboard:
- Connects to ESP32 over USB Serial or Raspberry Pi Hardware UART (/dev/serial0)
- Streams real-time telemetry (L1, L2, Distance, LCD lines, Emergency Mode)
- Provides auto-simulation fallback if no hardware is currently plugged in
- Serves the ultra-premium web dashboard built with HTML5, CSS3 & JavaScript
=============================================================================
"""

import os
import sys
import time
import glob
import json
import threading
from flask import Flask, render_template, jsonify, request

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

app = Flask(__name__)

# System State Cache
state = {
    "mode": "NORMAL",             # NORMAL, EMERGENCY, or PASSED_3S
    "l1": "RED",                  # RED, YELLOW, GREEN
    "l2": "GREEN",                # RED, YELLOW, GREEN
    "ir": "CLEAR",                # CLEAR or DETECTED
    "dist": 999,                  # Distance or legacy
    "lcd1": "TRAFFIC: NORMAL",    # Roadside LCD line 1
    "lcd2": "L1:RED L2:GRN",      # Roadside LCD line 2
    "connected": False,
    "port": "None",
    "emergency_count": 0,
    "uptime_seconds": 0
}

events_log = []
serial_lock = threading.Lock()
ser_instance = None
start_time = time.time()

def log_event(message, event_type="INFO"):
    timestamp = time.strftime("%H:%M:%S")
    entry = {
        "time": timestamp,
        "message": message,
        "type": event_type
    }
    events_log.insert(0, entry)
    if len(events_log) > 50:
        events_log.pop()

def get_available_ports():
    ports = []
    if HAS_SERIAL:
        for p in serial.tools.list_ports.comports():
            ports.append(p.device)
    for p in ['/dev/serial0', '/dev/ttyUSB0', '/dev/ttyUSB1', '/dev/ttyACM0']:
        if os.path.exists(p) and p not in ports:
            ports.append(p)
    return ports

def parse_serial_line(line):
    global state
    if line.startswith("STATUS:"):
        data_str = line.split("STATUS:")[1].strip()
        pairs = data_str.split(",")
        telemetry = {}
        for pair in pairs:
            if "=" in pair:
                k, v = pair.split("=", 1)
                telemetry[k.strip()] = v.strip()

        state["mode"] = telemetry.get("MODE", state["mode"])
        state["l1"]   = telemetry.get("L1", state["l1"])
        state["l2"]   = telemetry.get("L2", state["l2"])
        state["ir"]   = telemetry.get("IR", state.get("ir", "CLEAR"))
        
        try:
            state["dist"] = int(telemetry.get("DIST", 999))
        except ValueError:
            state["dist"] = 999

        state["lcd1"] = telemetry.get("LCD1", state["lcd1"])
        state["lcd2"] = telemetry.get("LCD2", state["lcd2"])

    elif line.startswith("EVENT:"):
        event_name = line.split("EVENT:")[1].strip()
        tag = "INFO"
        if "EMERGENCY_REQUEST_RECEIVED" in event_name or "MANUAL_EMERGENCY" in event_name:
            tag = "EMERGENCY"
            state["emergency_count"] += 1
        elif "IR_SENSOR_VERIFIED" in event_name or "ULTRASONIC_VERIFIED" in event_name or "AMBULANCE_PASSED" in event_name:
            tag = "PASSED"
        elif "OUT_OF_RANGE" in event_name or "DISCONNECT" in event_name:
            tag = "WARNING"
        elif "NORMAL" in event_name or "TIMEOUT" in event_name:
            tag = "NORMAL"

        log_event(event_name, tag)

def serial_worker():
    global ser_instance, state
    while True:
        if state["connected"] and ser_instance and ser_instance.is_open:
            try:
                line = ser_instance.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    parse_serial_line(line)
            except Exception as e:
                state["connected"] = False
                log_event(f"Serial read disconnected: {str(e)}", "WARNING")
        else:
            # Standalone Simulation Logic (Cycles smoothly if no hardware plugged in)
            time.sleep(0.5)

# Background Simulation Thread (Only active when offline so examiners see a working demo!)
def simulation_worker():
    global state
    sim_phase = 0
    while True:
        if not state["connected"]:
            if state["mode"] == "NORMAL":
                if sim_phase == 0:
                    state["l1"] = "RED"
                    state["l2"] = "GREEN"
                    state["lcd1"] = "TL1:RED   TL2:GRN"
                    state["lcd2"] = "NEXT IN 5s"
                    time.sleep(5)
                    sim_phase = 1
                elif sim_phase == 1:
                    state["l1"] = "YELLOW"
                    state["l2"] = "YELLOW"
                    state["lcd1"] = "TL1:YEL   TL2:YEL"
                    state["lcd2"] = "CAUTION / PREP"
                    time.sleep(2)
                    sim_phase = 2
                elif sim_phase == 2:
                    state["l1"] = "GREEN"
                    state["l2"] = "RED"
                    state["lcd1"] = "TL1:GRN   TL2:RED"
                    state["lcd2"] = "NEXT IN 5s"
                    time.sleep(5)
                    sim_phase = 3
                elif sim_phase == 3:
                    state["l1"] = "YELLOW"
                    state["l2"] = "YELLOW"
                    state["lcd1"] = "TL1:YEL   TL2:YEL"
                    state["lcd2"] = "CAUTION / PREP"
                    time.sleep(2)
                    sim_phase = 0
            else:
                time.sleep(0.5)
        else:
            time.sleep(1)

# Start Background Threads
threading.Thread(target=serial_worker, daemon=True).start()
threading.Thread(target=simulation_worker, daemon=True).start()

# =========================================================================
# WEB ROUTES
# =========================================================================

@app.route('/')
def index():
    return render_template('index.html')

@app.route('/api/status')
def get_status():
    state["uptime_seconds"] = int(time.time() - start_time)
    return jsonify(state)

@app.route('/api/events')
def get_events():
    return jsonify(events_log)

@app.route('/api/ports')
def get_ports():
    return jsonify({
        "ports": get_available_ports(),
        "current": state["port"],
        "connected": state["connected"]
    })

@app.route('/api/connect', methods=['POST'])
def connect_port():
    global ser_instance, state
    data = request.json or {}
    target_port = data.get("port")
    
    if not target_port:
        return jsonify({"success": False, "error": "No port specified"})

    if ser_instance and ser_instance.is_open:
        try:
            ser_instance.close()
        except:
            pass
        state["connected"] = False

    try:
        ser_instance = serial.Serial(target_port, 115200, timeout=1)
        state["connected"] = True
        state["port"] = target_port
        log_event(f"Hardware Serial Connected on {target_port}", "INFO")
        return jsonify({"success": True, "port": target_port})
    except Exception as e:
        state["connected"] = False
        log_event(f"Connection to {target_port} failed: {str(e)}", "WARNING")
        return jsonify({"success": False, "error": str(e)})

@app.route('/api/disconnect', methods=['POST'])
def disconnect_port():
    global ser_instance, state
    if ser_instance and ser_instance.is_open:
        ser_instance.close()
    state["connected"] = False
    log_event("Hardware Serial Disconnected", "INFO")
    return jsonify({"success": True})

@app.route('/api/command', methods=['POST'])
def send_command():
    global ser_instance, state
    data = request.json or {}
    cmd = data.get("cmd")

    if not cmd:
        return jsonify({"success": False, "error": "No command specified"})

    # Send directly to hardware if connected
    if state["connected"] and ser_instance and ser_instance.is_open:
        try:
            ser_instance.write(f"CMD:{cmd}\n".encode('utf-8'))
        except Exception as e:
            log_event(f"Serial write error: {str(e)}", "WARNING")

    # Mirror in software state
    if cmd == "DEMO_EMERGENCY":
        state["mode"] = "EMERGENCY"
        state["l1"] = "GREEN"
        state["l2"] = "RED"
        state["dist"] = 65
        state["lcd1"] = "!AMBULANCE NEAR!"
        state["lcd2"] = "CLEAR LANE FAST!"
        state["emergency_count"] += 1
        log_event("🚨 Simulated Emergency Triggered: Priority Lane 1 Activated", "EMERGENCY")

    elif cmd == "DEMO_VEHICLE_PASSED":
        if state["mode"] == "EMERGENCY":
            state["mode"] = "PASSED_3S"
            state["l1"] = "GREEN"
            state["l2"] = "RED"
            state["ir"] = "DETECTED"
            state["lcd1"] = "AMBULANCE PASSED"
            state["lcd2"] = "NORMAL IN 3 SEC"
            log_event("🚗 IR Sensor Verified: Vehicle Passed. 3s Clearance Started", "PASSED")
            
            # Auto-restore in 3s if simulated
            if not state["connected"]:
                def delayed_restore():
                    time.sleep(3)
                    state["mode"] = "NORMAL"
                    state["l1"] = "RED"
                    state["l2"] = "GREEN"
                    state["ir"] = "CLEAR"
                    state["dist"] = 999
                    state["lcd1"] = "TRAFFIC: NORMAL"
                    state["lcd2"] = "L1:RED L2:GRN"
                    log_event("🟢 Normal Traffic Cycle Restored", "NORMAL")
                threading.Thread(target=delayed_restore, daemon=True).start()

    elif cmd == "DEMO_NORMAL":
        state["mode"] = "NORMAL"
        state["l1"] = "RED"
        state["l2"] = "GREEN"
        state["ir"] = "CLEAR"
        state["dist"] = 999
        state["lcd1"] = "TRAFFIC: NORMAL"
        state["lcd2"] = "L1:RED L2:GRN"
        log_event("🟢 Normal Traffic Cycle Restored", "NORMAL")

    return jsonify({"success": True, "state": state})

if __name__ == '__main__':
    log_event("Web Dashboard Server Initialized", "INFO")
    
    # Auto-try default ports at launch
    ports = get_available_ports()
    if ports:
        for p in ports:
            try:
                ser_instance = serial.Serial(p, 115200, timeout=1)
                state["connected"] = True
                state["port"] = p
                log_event(f"Auto-connected to {p}", "INFO")
                break
            except:
                continue

    # Accessible on local machine and whole Wi-Fi network (for Raspberry Pi / phones)
    app.run(host='0.0.0.0', port=5000, debug=False)
