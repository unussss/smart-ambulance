#!/bin/bash
echo "==================================================="
echo "Starting PriorityRoute Web Dashboard Server (Pi)..."
echo "==================================================="
echo "Access from any device on Wi-Fi at: http://$(hostname -I | awk '{print $1}'):5000"
echo "==================================================="
python3 server.py
