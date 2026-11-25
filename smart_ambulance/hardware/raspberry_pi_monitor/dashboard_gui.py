"""
SMART AMBULANCE DETECTION & TRAFFIC SIGNAL PRIORITY SYSTEM
===========================================================
Master Presentation Dashboard (Tkinter GUI with Virtual LCD & Ultrasonic Pass Trigger)
Features:
  - 2-Way Traffic Signal Control (Lane 1 & Lane 2)
  - Virtual 16x2 I2C LCD Display matching the physical roadside LCD
  - Ultrasonic Physical Verification (<30cm triggers 3-second clearance countdown)
  - Out of range disconnect handling
  - Live Audit Log with CSV Export
  - Works on Raspberry Pi & Windows/Mac Laptops

Author: Final Year ECE Major Project Batch
"""

import sys
import os
import time
import threading
import queue
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

try:
    import serial
    import serial.tools.list_ports
    HAS_SERIAL = True
except ImportError:
    HAS_SERIAL = False

# Color Palette
COLOR_BG_DARK       = "#101620"
COLOR_PANEL_BG      = "#192231"
COLOR_HEADER_BG     = "#0c1119"
COLOR_TEXT_WHITE    = "#ffffff"
COLOR_TEXT_MUTED    = "#8e9bb0"
COLOR_ACCENT_BLUE   = "#00a8ff"
COLOR_BORDER        = "#27354a"

# Lamp Colors
COLOR_RED_ON        = "#ff3838"
COLOR_RED_OFF       = "#4a1212"
COLOR_YELLOW_ON     = "#ffb142"
COLOR_YELLOW_OFF    = "#4a3312"
COLOR_GREEN_ON      = "#2ed573"
COLOR_GREEN_OFF     = "#0e3d21"

# LCD Screen Aesthetic (Classic Blue Backlight with Bright White-Cyan Text)
COLOR_LCD_BG        = "#002b80"
COLOR_LCD_TEXT      = "#e0f7ff"

COLOR_EMERGENCY_BG  = "#c0392b"
COLOR_PASSED_BG     = "#d35400"
COLOR_NORMAL_BG     = "#27ae60"

class SmartTrafficDashboard:
    def __init__(self, root):
        self.root = root
        self.root.title("SMART AMBULANCE TRAFFIC PRIORITY SYSTEM - COMMAND CENTER")
        self.root.geometry("1180x820")
        self.root.minsize(1020, 720)
        self.root.configure(bg=COLOR_BG_DARK)

        # State Variables
        self.ser = None
        self.serial_thread = None
        self.is_connected = False
        self.serial_queue = queue.Queue()
        self.running = True

        self.current_mode = "NORMAL"
        self.l1_color = "GREEN"
        self.l2_color = "RED"
        self.current_distance = 999
        self.emergency_count = 0
        self.lcd_line1 = "TRAFFIC: NORMAL"
        self.lcd_line2 = "L1: GO | L2: STOP"

        # Build UI
        self.build_header()
        self.build_main_display()
        self.build_log_and_controls()

        # Timers
        self.update_clock()
        self.process_serial_queue()
        self.refresh_ports()

    # ==========================================================
    # 1. HEADER
    # ==========================================================
    def build_header(self):
        header_frame = tk.Frame(self.root, bg=COLOR_HEADER_BG, height=75, highlightbackground=COLOR_BORDER, highlightthickness=1)
        header_frame.pack(side=tk.TOP, fill=tk.X)

        title_box = tk.Frame(header_frame, bg=COLOR_HEADER_BG)
        title_box.pack(side=tk.LEFT, padx=20, pady=10)

        title_lbl = tk.Label(title_box, text="🚑 SMART AMBULANCE TRAFFIC PRIORITY SYSTEM", 
                             font=("Segoe UI", 16, "bold"), fg=COLOR_TEXT_WHITE, bg=COLOR_HEADER_BG)
        title_lbl.pack(anchor="w")

        sub_lbl = tk.Label(title_box, text="ECE Major Project • 2-Way Smart Intersection, Ultrasonic Pass Verification & LCD", 
                           font=("Segoe UI", 9), fg=COLOR_TEXT_MUTED, bg=COLOR_HEADER_BG)
        sub_lbl.pack(anchor="w")

        conn_box = tk.Frame(header_frame, bg=COLOR_HEADER_BG)
        conn_box.pack(side=tk.RIGHT, padx=20, pady=12)

        self.lbl_clock = tk.Label(conn_box, text="00:00:00", font=("Consolas", 12, "bold"), 
                                  fg=COLOR_ACCENT_BLUE, bg=COLOR_HEADER_BG)
        self.lbl_clock.pack(side=tk.RIGHT, padx=15)

        self.btn_connect = tk.Button(conn_box, text="🔌 Connect", font=("Segoe UI", 9, "bold"), 
                                     bg="#2980b9", fg="white", activebackground="#3498db", 
                                     activeforeground="white", relief=tk.FLAT, padx=12, pady=4, 
                                     command=self.toggle_connection)
        self.btn_connect.pack(side=tk.RIGHT, padx=5)

        self.port_combobox = ttk.Combobox(conn_box, width=14, state="readonly", font=("Segoe UI", 9))
        self.port_combobox.pack(side=tk.RIGHT, padx=5)

        lbl_port = tk.Label(conn_box, text="Port:", font=("Segoe UI", 9), fg=COLOR_TEXT_MUTED, bg=COLOR_HEADER_BG)
        lbl_port.pack(side=tk.RIGHT, padx=2)

    # ==========================================================
    # 2. MAIN DISPLAY (TRAFFIC LIGHTS + VIRTUAL LCD + RADAR)
    # ==========================================================
    def build_main_display(self):
        # Mode Status Banner
        self.banner_frame = tk.Frame(self.root, bg=COLOR_NORMAL_BG, height=40)
        self.banner_frame.pack(fill=tk.X, padx=15, pady=(10, 5))

        self.lbl_banner = tk.Label(self.banner_frame, text="🟢 NORMAL 2-WAY TRAFFIC CYCLE IN PROGRESS (5s PER PHASE)", 
                                   font=("Segoe UI", 12, "bold"), fg="white", bg=COLOR_NORMAL_BG)
        self.lbl_banner.pack(pady=8)

        # 3 Main Columns
        display_frame = tk.Frame(self.root, bg=COLOR_BG_DARK)
        display_frame.pack(fill=tk.BOTH, expand=False, padx=15, pady=5)

        # --- COLUMN 1: LANE 1 (AMBULANCE CORRIDOR) ---
        self.col_l1 = tk.Frame(display_frame, bg=COLOR_PANEL_BG, highlightbackground=COLOR_BORDER, highlightthickness=1)
        self.col_l1.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        tk.Label(self.col_l1, text="🚦 LANE 1 (AMBULANCE)", font=("Segoe UI", 12, "bold"), 
                 fg=COLOR_TEXT_WHITE, bg=COLOR_PANEL_BG).pack(pady=(12, 2))
        tk.Label(self.col_l1, text="Priority Corridor (GPIO 25, 26, 27)", font=("Segoe UI", 8), 
                 fg=COLOR_TEXT_MUTED, bg=COLOR_PANEL_BG).pack(pady=(0, 10))

        self.canvas_l1 = tk.Canvas(self.col_l1, width=110, height=200, bg="#0d1117", 
                                   highlightthickness=2, highlightbackground="#30363d")
        self.canvas_l1.pack(pady=5)
        self.draw_traffic_housing(self.canvas_l1)
        self.l1_red_bulb    = self.canvas_l1.create_oval(25, 18, 85, 78, fill=COLOR_RED_OFF, outline="#222", width=2)
        self.l1_yellow_bulb = self.canvas_l1.create_oval(25, 80, 85, 140, fill=COLOR_YELLOW_OFF, outline="#222", width=2)
        self.l1_green_bulb  = self.canvas_l1.create_oval(25, 142, 85, 202, fill=COLOR_GREEN_ON, outline="#222", width=2)

        self.lbl_l1_status = tk.Label(self.col_l1, text="SIGNAL: GREEN", font=("Segoe UI", 11, "bold"), 
                                      fg=COLOR_GREEN_ON, bg=COLOR_PANEL_BG)
        self.lbl_l1_status.pack(pady=(10, 12))


        # --- COLUMN 2: VIRTUAL 16x2 LCD & ULTRASONIC PASS DETECTOR ---
        self.col_center = tk.Frame(display_frame, bg=COLOR_PANEL_BG, highlightbackground=COLOR_BORDER, highlightthickness=1)
        self.col_center.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        tk.Label(self.col_center, text="📺 ROADSIDE 16x2 LCD DISPLAY", font=("Segoe UI", 12, "bold"), 
                 fg=COLOR_TEXT_WHITE, bg=COLOR_PANEL_BG).pack(pady=(10, 2))
        tk.Label(self.col_center, text="Live Hardware I2C Mirror (Address 0x27)", font=("Segoe UI", 8), 
                 fg=COLOR_TEXT_MUTED, bg=COLOR_PANEL_BG).pack(pady=(0, 6))

        # Virtual Blue LCD Screen Housing
        lcd_housing = tk.Frame(self.col_center, bg="#050a12", padx=6, pady=6, 
                               highlightbackground="#34495e", highlightthickness=2)
        lcd_housing.pack(padx=15, pady=4, fill=tk.X)

        self.lcd_screen = tk.Frame(lcd_housing, bg=COLOR_LCD_BG, padx=12, pady=10)
        self.lcd_screen.pack(fill=tk.BOTH)

        self.lbl_lcd_l1 = tk.Label(self.lcd_screen, text="TRAFFIC: NORMAL", font=("Consolas", 13, "bold"), 
                                   fg=COLOR_LCD_TEXT, bg=COLOR_LCD_BG, anchor="w")
        self.lbl_lcd_l1.pack(fill=tk.X)

        self.lbl_lcd_l2 = tk.Label(self.lcd_screen, text="L1: GO | L2: STOP", font=("Consolas", 13, "bold"), 
                                   fg=COLOR_LCD_TEXT, bg=COLOR_LCD_BG, anchor="w")
        self.lbl_lcd_l2.pack(fill=tk.X, pady=(4, 0))

        # Ultrasonic Physical Passing Detector Display
        dist_box = tk.Frame(self.col_center, bg="#0d1117", highlightbackground="#30363d", highlightthickness=1)
        dist_box.pack(padx=15, pady=(10, 4), fill=tk.X)

        tk.Label(dist_box, text="HC-SR04 PHYSICAL PASS DETECTOR", font=("Segoe UI", 8, "bold"), 
                 fg=COLOR_TEXT_MUTED, bg="#0d1117").pack(pady=(4, 1))

        self.lbl_dist_value = tk.Label(dist_box, text="-- cm", font=("Consolas", 18, "bold"), 
                                       fg=COLOR_ACCENT_BLUE, bg="#0d1117")
        self.lbl_dist_value.pack(pady=1)

        self.lbl_proximity = tk.Label(dist_box, text="Awaiting Vehicle...", font=("Segoe UI", 9, "bold"), 
                                      fg=COLOR_TEXT_MUTED, bg="#0d1117")
        self.lbl_proximity.pack(pady=(0, 4))

        self.progress_dist = ttk.Progressbar(self.col_center, orient="horizontal", length=160, mode="determinate")
        self.progress_dist.pack(pady=4)

        self.lbl_emg_count = tk.Label(self.col_center, text="🚨 Emergencies Cleared: 0", font=("Segoe UI", 9, "bold"), 
                                      fg=COLOR_TEXT_WHITE, bg=COLOR_PANEL_BG)
        self.lbl_emg_count.pack(pady=(4, 8))


        # --- COLUMN 3: LANE 2 (CROSS TRAFFIC) ---
        self.col_l2 = tk.Frame(display_frame, bg=COLOR_PANEL_BG, highlightbackground=COLOR_BORDER, highlightthickness=1)
        self.col_l2.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=5)

        tk.Label(self.col_l2, text="🚦 LANE 2 (CROSS TRAFFIC)", font=("Segoe UI", 12, "bold"), 
                 fg=COLOR_TEXT_WHITE, bg=COLOR_PANEL_BG).pack(pady=(12, 2))
        tk.Label(self.col_l2, text="General Corridor (GPIO 32, 33, 19)", font=("Segoe UI", 8), 
                 fg=COLOR_TEXT_MUTED, bg=COLOR_PANEL_BG).pack(pady=(0, 10))

        self.canvas_l2 = tk.Canvas(self.col_l2, width=110, height=200, bg="#0d1117", 
                                   highlightthickness=2, highlightbackground="#30363d")
        self.canvas_l2.pack(pady=5)
        self.draw_traffic_housing(self.canvas_l2)
        self.l2_red_bulb    = self.canvas_l2.create_oval(25, 18, 85, 78, fill=COLOR_RED_ON, outline="#222", width=2)
        self.l2_yellow_bulb = self.canvas_l2.create_oval(25, 80, 85, 140, fill=COLOR_YELLOW_OFF, outline="#222", width=2)
        self.l2_green_bulb  = self.canvas_l2.create_oval(25, 142, 85, 202, fill=COLOR_GREEN_OFF, outline="#222", width=2)

        self.lbl_l2_status = tk.Label(self.col_l2, text="SIGNAL: RED", font=("Segoe UI", 11, "bold"), 
                                      fg=COLOR_RED_ON, bg=COLOR_PANEL_BG)
        self.lbl_l2_status.pack(pady=(10, 12))

    def draw_traffic_housing(self, canvas):
        canvas.create_arc(20, 10, 90, 42, start=0, extent=180, fill="#21262d", outline="#30363d")
        canvas.create_arc(20, 72, 90, 104, start=0, extent=180, fill="#21262d", outline="#30363d")
        canvas.create_arc(20, 134, 90, 166, start=0, extent=180, fill="#21262d", outline="#30363d")

    # ==========================================================
    # 3. LIVE EVENT AUDIT LOG & CONTROLS
    # ==========================================================
    def build_log_and_controls(self):
        bottom_frame = tk.Frame(self.root, bg=COLOR_BG_DARK)
        bottom_frame.pack(fill=tk.BOTH, expand=True, padx=15, pady=(5, 15))

        log_panel = tk.Frame(bottom_frame, bg=COLOR_PANEL_BG, highlightbackground=COLOR_BORDER, highlightthickness=1)
        log_panel.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))

        log_head = tk.Frame(log_panel, bg=COLOR_PANEL_BG)
        log_head.pack(fill=tk.X, padx=10, pady=(8, 4))

        tk.Label(log_head, text="📜 LIVE EVENT AUDIT LOG (PI USB SERIAL STREAM)", 
                 font=("Segoe UI", 10, "bold"), fg=COLOR_TEXT_WHITE, bg=COLOR_PANEL_BG).pack(side=tk.LEFT)

        tk.Button(log_head, text="Clear", font=("Segoe UI", 8), bg="#34495e", fg="white", 
                  relief=tk.FLAT, padx=8, pady=2, command=self.clear_log).pack(side=tk.RIGHT, padx=4)
        tk.Button(log_head, text="Export...", font=("Segoe UI", 8), bg="#34495e", fg="white", 
                  relief=tk.FLAT, padx=8, pady=2, command=self.export_log).pack(side=tk.RIGHT)

        self.log_text = tk.Text(log_panel, font=("Consolas", 9), bg="#0d1117", fg="#c9d1d9", 
                                insertbackground="white", relief=tk.FLAT, height=9)
        self.log_text.pack(fill=tk.BOTH, expand=True, padx=10, pady=(0, 10))

        self.log_text.tag_config("EMERGENCY", foreground="#ff4757", font=("Consolas", 9, "bold"))
        self.log_text.tag_config("SENSOR",    foreground="#eccc68", font=("Consolas", 9, "bold"))
        self.log_text.tag_config("PASSED",    foreground="#ffa502", font=("Consolas", 9, "bold"))
        self.log_text.tag_config("NORMAL",    foreground="#2ed573")
        self.log_text.tag_config("INFO",      foreground="#70a1ff")

        # Demo Controls Pad
        demo_panel = tk.Frame(bottom_frame, bg=COLOR_PANEL_BG, width=240, highlightbackground=COLOR_BORDER, highlightthickness=1)
        demo_panel.pack(side=tk.RIGHT, fill=tk.Y, padx=(5, 0))

        tk.Label(demo_panel, text="🎯 PRESENTATION CONTROLS", font=("Segoe UI", 10, "bold"), 
                 fg=COLOR_TEXT_WHITE, bg=COLOR_PANEL_BG).pack(pady=(6, 1))
        tk.Label(demo_panel, text="Test triggers for examiners:", font=("Segoe UI", 8), 
                 fg=COLOR_TEXT_MUTED, bg=COLOR_PANEL_BG).pack(pady=(0, 4))

        tk.Button(demo_panel, text="🚨 1. TRIGGER EMERGENCY\n(Simulate Approach)", 
                  font=("Segoe UI", 8, "bold"), bg="#c0392b", fg="white", activebackground="#e74c3c", 
                  relief=tk.FLAT, padx=6, pady=4, width=23, command=self.send_demo_emergency).pack(pady=2)

        tk.Button(demo_panel, text="🚗 2. PASS SENSOR (<30cm)\n(Triggers 3s Shutdown)", 
                  font=("Segoe UI", 8, "bold"), bg="#d35400", fg="white", activebackground="#e67e22", 
                  relief=tk.FLAT, padx=6, pady=4, width=23, command=self.send_demo_passed).pack(pady=2)

        tk.Button(demo_panel, text="🟢 3. RESTORE NORMAL\n(Force 5s Cycle)", 
                  font=("Segoe UI", 8, "bold"), bg="#27ae60", fg="white", activebackground="#2ecc71", 
                  relief=tk.FLAT, padx=6, pady=4, width=23, command=self.send_demo_normal).pack(pady=2)

        self.lbl_sim_mode = tk.Label(demo_panel, text="Simulation Mode: Active", font=("Segoe UI", 8, "italic"), 
                                     fg=COLOR_YELLOW_ON, bg=COLOR_PANEL_BG)
        self.lbl_sim_mode.pack(side=tk.BOTTOM, pady=6)

    # ==========================================================
    # 4. SERIAL WORKER & PARSER
    # ==========================================================
    def refresh_ports(self):
        if not HAS_SERIAL:
            self.port_combobox["values"] = ["No PySerial"]
            self.port_combobox.current(0)
            return

        ports = [port.device for port in serial.tools.list_ports.comports()]
        for p in ['/dev/ttyUSB0', '/dev/ttyUSB1', '/dev/ttyACM0']:
            if os.path.exists(p) and p not in ports:
                ports.append(p)

        if ports:
            self.port_combobox["values"] = ports
            self.port_combobox.current(0)
        else:
            self.port_combobox["values"] = ["Simulation Only"]
            self.port_combobox.current(0)

    def toggle_connection(self):
        if self.is_connected:
            self.disconnect_serial()
        else:
            port = self.port_combobox.get()
            if not port or port in ["No PySerial", "Simulation Only"]:
                self.log_event("Operating in Demo/Simulation Mode (No hardware required).", "INFO")
                return
            self.connect_serial(port)

    def connect_serial(self, port):
        try:
            self.ser = serial.Serial(port, 115200, timeout=1)
            self.is_connected = True
            self.btn_connect.config(text="🔌 Disconnect", bg="#e74c3c")
            self.lbl_sim_mode.config(text="Hardware Connected", fg=COLOR_GREEN_ON)
            self.log_event(f"Successfully connected to ESP32 on {port}", "INFO")

            self.serial_thread = threading.Thread(target=self.read_serial_worker, daemon=True)
            self.serial_thread.start()
        except Exception as e:
            messagebox.showerror("Serial Error", f"Could not open {port}:\n{str(e)}")
            self.log_event(f"Connection failed: {str(e)}", "EMERGENCY")

    def disconnect_serial(self):
        self.is_connected = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        self.btn_connect.config(text="🔌 Connect", bg="#2980b9")
        self.lbl_sim_mode.config(text="Simulation Mode: Active", fg=COLOR_YELLOW_ON)
        self.log_event("Disconnected from serial port.", "INFO")

    def read_serial_worker(self):
        while self.is_connected and self.ser and self.ser.is_open:
            try:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    self.serial_queue.put(line)
            except Exception:
                break
        self.is_connected = False

    def process_serial_queue(self):
        while not self.serial_queue.empty():
            line = self.serial_queue.get_nowait()
            self.parse_incoming_line(line)
        self.root.after(50, self.process_serial_queue)

    def parse_incoming_line(self, line):
        # Format: STATUS:MODE=NORMAL,L1=GREEN,L2=RED,DIST=45,LCD1=...,LCD2=...
        if line.startswith("STATUS:"):
            data_str = line.split("STATUS:")[1].strip()
            pairs = data_str.split(",")
            telemetry = {}
            for pair in pairs:
                if "=" in pair:
                    k, v = pair.split("=", 1)
                    telemetry[k.strip()] = v.strip()

            mode = telemetry.get("MODE", "NORMAL")
            l1   = telemetry.get("L1", "GREEN")
            l2   = telemetry.get("L2", "RED")
            dist = telemetry.get("DIST", "999")
            lcd1 = telemetry.get("LCD1", self.lcd_line1)
            lcd2 = telemetry.get("LCD2", self.lcd_line2)

            try:
                distance_val = int(dist)
            except ValueError:
                distance_val = 999

            self.update_traffic_state(mode, l1, l2, distance_val, lcd1, lcd2)

        elif line.startswith("EVENT:"):
            event_name = line.split("EVENT:")[1].strip()
            tag = "INFO"
            if "EMERGENCY_REQUEST_RECEIVED" in event_name:
                tag = "EMERGENCY"
                self.emergency_count += 1
                self.lbl_emg_count.config(text=f"🚨 Emergencies Cleared: {self.emergency_count}")
            elif "ULTRASONIC_VERIFIED" in event_name or "AMBULANCE_PASSED" in event_name:
                tag = "PASSED"
            elif "OUT_OF_RANGE" in event_name:
                tag = "SENSOR"
            elif "TIMEOUT" in event_name or "NORMAL" in event_name:
                tag = "NORMAL"

            self.log_event(event_name, tag)

    # ==========================================================
    # 5. UI UPDATE LOGIC
    # ==========================================================
    def update_traffic_state(self, mode, l1, l2, dist, lcd1, lcd2):
        self.current_mode = mode
        self.l1_color = l1
        self.l2_color = l2
        self.current_distance = dist
        self.lcd_line1 = lcd1
        self.lcd_line2 = lcd2

        # Virtual LCD Update
        self.lbl_lcd_l1.config(text=lcd1)
        self.lbl_lcd_l2.config(text=lcd2)

        # Mode Banner Update
        if mode == "PASSED_3S":
            self.banner_frame.config(bg=COLOR_PASSED_BG)
            self.lbl_banner.config(text="🚑 AMBULANCE PASSED JUNCTION — RETURNING TO NORMAL IN 3s", bg=COLOR_PASSED_BG)
            self.lbl_proximity.config(text="✅ PHYSICAL CROSSING VERIFIED (<30cm)", fg=COLOR_YELLOW_ON)
        elif mode == "EMERGENCY":
            self.banner_frame.config(bg=COLOR_EMERGENCY_BG)
            self.lbl_banner.config(text="🚨 AMBULANCE APPROACHING! PLEASE CLEAR THE LANE FAST!", bg=COLOR_EMERGENCY_BG)
        else:
            self.banner_frame.config(bg=COLOR_NORMAL_BG)
            self.lbl_banner.config(text="🟢 NORMAL 2-WAY TRAFFIC CYCLE IN PROGRESS (5s PER PHASE)", bg=COLOR_NORMAL_BG)

        # Bulbs L1
        self.canvas_l1.itemconfig(self.l1_red_bulb,    fill=COLOR_RED_ON    if l1 == "RED"    else COLOR_RED_OFF)
        self.canvas_l1.itemconfig(self.l1_yellow_bulb, fill=COLOR_YELLOW_ON if l1 == "YELLOW" else COLOR_YELLOW_OFF)
        self.canvas_l1.itemconfig(self.l1_green_bulb,  fill=COLOR_GREEN_ON  if l1 == "GREEN"  else COLOR_GREEN_OFF)

        color_map = {"RED": COLOR_RED_ON, "YELLOW": COLOR_YELLOW_ON, "GREEN": COLOR_GREEN_ON}
        self.lbl_l1_status.config(text=f"SIGNAL: {l1}", fg=color_map.get(l1, "white"))

        # Bulbs L2
        self.canvas_l2.itemconfig(self.l2_red_bulb,    fill=COLOR_RED_ON    if l2 == "RED"    else COLOR_RED_OFF)
        self.canvas_l2.itemconfig(self.l2_yellow_bulb, fill=COLOR_YELLOW_ON if l2 == "YELLOW" else COLOR_YELLOW_OFF)
        self.canvas_l2.itemconfig(self.l2_green_bulb,  fill=COLOR_GREEN_ON  if l2 == "GREEN"  else COLOR_GREEN_OFF)

        self.lbl_l2_status.config(text=f"SIGNAL: {l2}", fg=color_map.get(l2, "white"))

        # Distance Gauge
        if dist > 0 and dist < 200:
            self.lbl_dist_value.config(text=f"{dist} cm")
            self.progress_dist["value"] = max(0, min(100, int((dist / 100.0) * 100)))

            if dist <= 30 and mode != "PASSED_3S":
                self.lbl_proximity.config(text="🎯 PHYSICAL PASS DETECTED (<30cm)!", fg="#ff4757")
            elif mode != "PASSED_3S":
                self.lbl_proximity.config(text="Monitoring Crossing Zone...", fg=COLOR_TEXT_MUTED)
        else:
            self.lbl_dist_value.config(text="-- cm")
            if mode != "PASSED_3S":
                self.lbl_proximity.config(text="Awaiting Vehicle...", fg=COLOR_TEXT_MUTED)
            self.progress_dist["value"] = 100

    def log_event(self, message, tag="INFO"):
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert(tk.END, f"[{timestamp}] {message}\n", tag)
        self.log_text.see(tk.END)

    def clear_log(self):
        self.log_text.delete("1.0", tk.END)

    def export_log(self):
        f = filedialog.asksaveasfilename(defaultextension=".txt", filetypes=[("Text File", "*.txt")])
        if f:
            try:
                with open(f, "w", encoding="utf-8") as file:
                    file.write(self.log_text.get("1.0", tk.END))
                messagebox.showinfo("Export Successful", "Audit log saved successfully.")
            except Exception as e:
                messagebox.showerror("Export Failed", str(e))

    # ==========================================================
    # 6. DEMO BUTTONS
    # ==========================================================
    def send_demo_emergency(self):
        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b"CMD:DEMO_EMERGENCY\n")
            except Exception as e:
                self.log_event(f"Serial write error: {e}", "EMERGENCY")
        else:
            self.log_event("Simulated Emergency: Approach Corridor & LCD Alert Activated", "EMERGENCY")
            self.emergency_count += 1
            self.lbl_emg_count.config(text=f"🚨 Emergencies Cleared: {self.emergency_count}")
            self.update_traffic_state("EMERGENCY", "GREEN", "RED", 65, "!AMBULANCE NEAR!", "CLEAR LANE FAST!")

    def send_demo_passed(self):
        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b"CMD:DEMO_VEHICLE_PASSED\n")
            except Exception as e:
                self.log_event(f"Serial write error: {e}", "EMERGENCY")
        else:
            self.log_event("Simulated Ultrasonic Trigger: Vehicle Passed Junction (<30cm)", "PASSED")
            self.log_event("3-Second Clearance Countdown Started", "PASSED")
            self.update_traffic_state("PASSED_3S", "GREEN", "RED", 18, "AMBULANCE PASSED", "NORMAL IN 3 SEC")
            self.root.after(3000, lambda: self.update_traffic_state("NORMAL", "GREEN", "RED", 999, "TRAFFIC: NORMAL", "L1: GO | L2: STOP"))

    def send_demo_normal(self):
        if self.is_connected and self.ser and self.ser.is_open:
            try:
                self.ser.write(b"CMD:DEMO_NORMAL\n")
            except Exception as e:
                self.log_event(f"Serial write error: {e}", "EMERGENCY")
        else:
            self.log_event("Simulated Normal Cycle Restored (5s timer)", "NORMAL")
            self.update_traffic_state("NORMAL", "GREEN", "RED", 999, "TRAFFIC: NORMAL", "L1: GO | L2: STOP")

    def update_clock(self):
        self.lbl_clock.config(text=time.strftime("%H:%M:%S"))
        self.root.after(1000, self.update_clock)


if __name__ == "__main__":
    root = tk.Tk()
    app = SmartTrafficDashboard(root)
    root.mainloop()
