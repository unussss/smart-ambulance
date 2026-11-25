/**
 * SMART AMBULANCE TRAFFIC PRIORITY SYSTEM - FRONTEND ENGINE
 * Real-time state synchronizer and UI animator
 */

let isConnected = false;

// 1. Clock Display
function updateClock() {
    const now = new Date();
    const timeStr = now.toTimeString().split(' ')[0];
    document.getElementById('clockDisplay').textContent = timeStr;
}
setInterval(updateClock, 1000);
updateClock();

// 2. Fetch Available Serial Ports
async function loadPorts() {
    try {
        const res = await fetch('/api/ports');
        const data = await res.json();
        const select = document.getElementById('portSelect');
        const btn = document.getElementById('btnConnect');

        select.innerHTML = '';
        if (data.ports && data.ports.length > 0) {
            data.ports.forEach(p => {
                const opt = document.createElement('option');
                opt.value = p;
                opt.textContent = p;
                if (p === data.current) opt.selected = true;
                select.appendChild(opt);
            });
        } else {
            const opt = document.createElement('option');
            opt.value = '';
            opt.textContent = 'Demo Mode (Offline)';
            select.appendChild(opt);
        }

        isConnected = data.connected;
        if (isConnected) {
            btn.innerHTML = '<span>🔌</span> Disconnect';
            btn.className = 'btn btn-outline';
        } else {
            btn.innerHTML = '<span>🔌</span> Connect';
            btn.className = 'btn btn-primary';
        }
    } catch (e) {
        console.warn('Port detection error:', e);
    }
}
loadPorts();

// 3. Connect / Disconnect Toggle
async function toggleConnect() {
    const select = document.getElementById('portSelect');
    const port = select.value;

    if (isConnected) {
        await fetch('/api/disconnect', { method: 'POST' });
    } else {
        if (!port) {
            alert('No serial port selected. Operating in offline simulation mode.');
            return;
        }
        await fetch('/api/connect', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ port: port })
        });
    }
    loadPorts();
}

// 4. Send Demo Command
async function sendCommand(cmd) {
    try {
        await fetch('/api/command', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ cmd: cmd })
        });
        fetchStatus();
        fetchEvents();
    } catch (e) {
        console.error('Command failed:', e);
    }
}

// 5. Real-Time Status Telemetry Synchronizer
async function fetchStatus() {
    try {
        const res = await fetch('/api/status');
        const s = await res.json();

        // A. Hero Banner
        const heroPill = document.getElementById('modeHeroPill');
        const modeText = document.getElementById('modeText');
        const modeDot  = document.getElementById('modeDot');

        if (s.mode === 'EMERGENCY') {
            heroPill.className = 'hero-pill pill-emergency';
            modeDot.textContent = '🚨';
            modeText.textContent = 'EMERGENCY CORRIDOR ACTIVE — AMBULANCE APPROACHING';
        } else if (s.mode === 'PASSED_3S') {
            heroPill.className = 'hero-pill pill-passed';
            modeDot.textContent = '🚗';
            modeText.textContent = 'AMBULANCE PASSED JUNCTION — 3-SECOND CLEARANCE COUNTDOWN';
        } else {
            heroPill.className = 'hero-pill pill-normal';
            modeDot.textContent = '🟢';
            modeText.textContent = 'NORMAL 2-WAY TRAFFIC CYCLE ACTIVE (5s PER PHASE)';
        }

        // B. Traffic Light 1 (Lane 1)
        const l1R = document.getElementById('bulbL1Red');
        const l1Y = document.getElementById('bulbL1Yellow');
        const l1G = document.getElementById('bulbL1Green');
        const l1Text = document.getElementById('statusL1Text');

        l1R.classList.toggle('active', s.l1 === 'RED');
        l1Y.classList.toggle('active', s.l1 === 'YELLOW');
        l1G.classList.toggle('active', s.l1 === 'GREEN');

        if (s.l1 === 'GREEN') {
            l1Text.textContent = 'SIGNAL: GREEN (GO)';
            l1Text.style.color = 'var(--success)';
        } else if (s.l1 === 'YELLOW') {
            l1Text.textContent = 'SIGNAL: YELLOW (CAUTION)';
            l1Text.style.color = 'var(--warning)';
        } else {
            l1Text.textContent = 'SIGNAL: RED (HALT)';
            l1Text.style.color = 'var(--danger)';
        }

        // C. Traffic Light 2 (Lane 2)
        const l2R = document.getElementById('bulbL2Red');
        const l2Y = document.getElementById('bulbL2Yellow');
        const l2G = document.getElementById('bulbL2Green');
        const l2Text = document.getElementById('statusL2Text');

        l2R.classList.toggle('active', s.l2 === 'RED');
        l2Y.classList.toggle('active', s.l2 === 'YELLOW');
        l2G.classList.toggle('active', s.l2 === 'GREEN');

        if (s.l2 === 'GREEN') {
            l2Text.textContent = 'SIGNAL: GREEN (GO)';
            l2Text.style.color = 'var(--success)';
        } else if (s.l2 === 'YELLOW') {
            l2Text.textContent = 'SIGNAL: YELLOW (CAUTION)';
            l2Text.style.color = 'var(--warning)';
        } else {
            l2Text.textContent = 'SIGNAL: RED (HALT)';
            l2Text.style.color = 'var(--danger)';
        }

        // D. Roadside 16x2 LCD Mirror
        document.getElementById('lcdLine1').textContent = s.lcd1 || 'TRAFFIC: NORMAL';
        document.getElementById('lcdLine2').textContent = s.lcd2 || 'L1: GO | L2: STOP';

        // E. IR Vehicle Passing Sensor Display
        const irState = s.ir || 'CLEAR';
        const irLabel = document.getElementById('irSensorState');
        const irBar   = document.getElementById('irProgressBar');
        const proxText = document.getElementById('proximityText');

        if (irLabel && irBar) {
            if (irState === 'DETECTED') {
                irLabel.textContent = 'DETECTED';
                irLabel.style.color = 'var(--danger)';
                irBar.style.width = '100%';
                irBar.style.backgroundColor = 'var(--danger)';
                proxText.textContent = '🎯 VEHICLE DETECTED AT JUNCTION (BEAM CUT)!';
                proxText.style.color = 'var(--danger)';
                proxText.style.fontWeight = '700';
            } else {
                irLabel.textContent = 'CLEAR';
                irLabel.style.color = 'var(--success)';
                irBar.style.width = '10%';
                irBar.style.backgroundColor = 'var(--success)';
                proxText.textContent = 'Awaiting Vehicle Crossing Beam...';
                proxText.style.color = 'var(--text-muted)';
                proxText.style.fontWeight = 'normal';
            }
        }

        // F. Counter
        document.getElementById('emgClearedCount').textContent = `🚨 Emergencies Cleared: ${s.emergency_count}`;

    } catch (e) {
        console.warn('Telemetry polling error:', e);
    }
}

// 6. Real-Time Event Audit Log Fetcher
async function fetchEvents() {
    try {
        const res = await fetch('/api/events');
        const events = await res.json();
        const tbody = document.getElementById('eventsTableBody');

        if (events.length === 0) return;

        let rowsHtml = '';
        events.slice(0, 15).forEach(ev => {
            rowsHtml += `
                <tr>
                    <td style="font-family: var(--font-mono);">${ev.time}</td>
                    <td><span class="tag tag-${ev.type}">${ev.type}</span></td>
                    <td>${ev.message}</td>
                </tr>
            `;
        });
        tbody.innerHTML = rowsHtml;
    } catch (e) {
        console.warn('Events polling error:', e);
    }
}

// Poll status every 250ms for smooth live lamp changes
setInterval(fetchStatus, 250);

// Poll event table every 1000ms
setInterval(fetchEvents, 1000);

fetchStatus();
fetchEvents();
