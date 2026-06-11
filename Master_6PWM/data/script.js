/* ══════════════════════════════════════════
   MARS ROVER CONTROLLER — script.js v2
══════════════════════════════════════════ */

var gateway = `ws://${window.location.hostname}/ws`;
var websocket;

// ── State ──
let currentMode = "NONE";
let currentX    = 2048;
let currentY    = 2048;
let currentPot  = 0;
let isDragging  = false;
let joyCenter   = { x: 0, y: 0 };
const MAX_DIST  = 45;

// ── Elements ──
const joystick       = document.getElementById('joystick');
const joystickTech   = document.getElementById('joystickTech');
const speedSlider    = document.getElementById('speedSlider');
const speedSliderTech= document.getElementById('speedSliderTech');

// ══════════════════════════
window.addEventListener('load', () => {
  initWebSocket();
  initJoystick(joystick);
  initJoystick(joystickTech);
  initSpeedSlider(speedSlider);
  initSpeedSlider(speedSliderTech);
  setInterval(sendData, 50); // 20 Hz
  updateModeUI();
});

// ══════════════════════════
// WEBSOCKET
// ══════════════════════════
function initWebSocket() {
  websocket = new WebSocket(gateway);
  websocket.onopen  = wsOpen;
  websocket.onclose = wsClose;
  websocket.onmessage = onMessage;
}

function wsOpen() {
  setConnStatus(true);
}
function wsClose() {
  setConnStatus(false);
  setTimeout(initWebSocket, 2000);
}

function setConnStatus(ok) {
  const el = document.getElementById('connectionStatus');
  if (ok) {
    el.textContent = '● ONLINE';
    el.className   = 'badge badge-green';
  } else {
    el.textContent = '● OFFLINE';
    el.className   = 'badge badge-red';
  }
}

// ══════════════════════════
// NHẬN DỮ LIỆU TELEMETRY
// ══════════════════════════
function onMessage(event) {
  try {
    const data = JSON.parse(event.data);
    if (data.type !== "tele") return;

    // 1. Failsafe
    const banner = document.getElementById('failsafeBanner');
    if (data.fs === true) {
      banner.style.display = 'block';
      document.getElementById('fsVal').textContent = 'ON';
      document.getElementById('fsVal').style.color = '#e74c3c';
      if (currentMode === "WEB") stopDrag();
    } else {
      banner.style.display = 'none';
      document.getElementById('fsVal').textContent = 'OFF';
      document.getElementById('fsVal').style.color = 'var(--col-green)';
    }

    // 2. Pin
    const bat = parseFloat(data.bat).toFixed(1);
    const batEl = document.getElementById('batteryStatus');
    batEl.textContent = '🔋 ' + bat + 'V';
    if (data.bat >= 37.0)      { batEl.className = 'badge badge-green'; }
    else if (data.bat >= 34.0) { batEl.className = 'badge badge-yellow'; }
    else                       { batEl.className = 'badge badge-red'; }

    // 3. RPM — tab vận hành (chỉ RPM)
    const rpmIds = ['rpmL1','rpmL2','rpmL3','rpmR1','rpmR2','rpmR3'];
    rpmIds.forEach((id, i) => {
      const el = document.getElementById(id);
      if (el) {
        el.textContent = data.rpm[i];
        // Highlight khi đang quay
        const card = el.closest('.wheel-card');
        if (card) {
          card.classList.toggle('spinning', data.rpm[i] > 5);
        }
      }
    });

    // 4. Tab kỹ thuật — RPM
    const techRpmIds = ['techRpmL1','techRpmL2','techRpmL3','techRpmR1','techRpmR2','techRpmR3'];
    techRpmIds.forEach((id, i) => {
      const el = document.getElementById(id);
      if (el) el.textContent = data.rpm[i];
    });

    // 5. Tab kỹ thuật — PWM
    ['pwmL1','pwmL2','pwmL3'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.textContent = data.pwmL;
    });
    ['pwmR1','pwmR2','pwmR3'].forEach(id => {
      const el = document.getElementById(id);
      if (el) el.textContent = data.pwmR;
    });
    setEl('telePwmL', data.pwmL);
    setEl('telePwmR', data.pwmR);

    // 6. Trạng thái vận hành
    const motionMap = {
      0: { text: '⏸ STOP',       cls: '' },
      1: { text: '▲ FORWARD',     cls: 'moving' },
      2: { text: '▼ BACKWARD',    cls: 'moving' },
      3: { text: '↖ FWD LEFT',    cls: 'moving' },
      4: { text: '↗ FWD RIGHT',   cls: 'moving' },
      5: { text: '↺ SPIN LEFT',   cls: 'spinning' },
      6: { text: '↻ SPIN RIGHT', cls: 'spinning' },
      7: { text: '↙ BCK LEFT',    cls: 'moving' },  // ĐÃ THÊM LÙI TRÁI
      8: { text: '↘ BCK RIGHT',   cls: 'moving' },  // ĐÃ THÊM LÙI PHẢI
      9: { text: '⚠ BRAKING',     cls: '' }
    };
    const m = motionMap[data.motion] || motionMap[0];
    const md = document.getElementById('modeDisplay');
    if (md) {
      md.textContent = m.text;
      md.className   = 'motion-display ' + m.cls;
    }

  } catch(e) {
    // ignore bad packet
  }
}

function setEl(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

// ══════════════════════════
// GỬI DỮ LIỆU (20 Hz)
// ══════════════════════════
function sendData() {
  if (!websocket || websocket.readyState !== WebSocket.OPEN) return;
  if (currentMode === "WEB") {
    websocket.send(`X:${currentX},Y:${currentY},POT:${currentPot}`);
  }
}

// ══════════════════════════
// CHỌN NGUỒN ĐIỀU KHIỂN
// ══════════════════════════
function toggleMode(source) {
  if (currentMode === source) {
    currentMode = "NONE";
  } else {
    currentMode = source;
  }
  updateModeUI();
  if (websocket && websocket.readyState === WebSocket.OPEN) {
    websocket.send("MODE:" + currentMode);
  }
  if (currentMode !== "WEB") stopDrag();
}

function updateModeUI() {
  // Buttons
  document.getElementById('btnRC') .classList.toggle('active-rc',  currentMode === 'RC');
  document.getElementById('btnWEB').classList.toggle('active-web', currentMode === 'WEB');
  document.getElementById('btnESP').classList.toggle('active-esp', currentMode === 'ESPNOW');

  // Text
  const t = document.getElementById('activeModeText');
  const map = {
    'RC':     { text:'🎮 RC ACTIVE',       color:'var(--col-green)' },
    'WEB':    { text:'🌐 WEB ACTIVE',      color:'var(--col-blue)' },
    'ESPNOW': { text:'📡 ESP ACTIVE',      color:'var(--col-yellow)' },
    'NONE':   { text:'⛔ LOCKED',          color:'#e74c3c' },
  };
  const info = map[currentMode] || map['NONE'];
  t.textContent   = info.text;
  t.style.color   = info.color;

  // Joystick & slider lock/unlock
  const webMode = currentMode === 'WEB';
  updateControlLock(webMode);
}

function updateControlLock(unlocked) {
  // Speed sliders
  speedSlider.disabled     = !unlocked;
  speedSliderTech.disabled = !unlocked;

  // Joystick visual
  const jBase  = joystick.parentElement;
  const jBase2 = joystickTech.parentElement;
  if (unlocked) {
    jBase.closest('.ctrl-card').classList.remove('joystick-disabled');
    jBase2.closest('.ctrl-card').classList.remove('joystick-disabled');
    // Remove overlay if any
    removeOverlay(jBase.closest('.ctrl-card'));
    removeOverlay(jBase2.closest('.ctrl-card'));
  } else {
    jBase.closest('.ctrl-card').classList.add('joystick-disabled');
    jBase2.closest('.ctrl-card').classList.add('joystick-disabled');
    addOverlay(jBase.closest('.ctrl-card'));
    addOverlay(jBase2.closest('.ctrl-card'));
  }
}

function addOverlay(card) {
  if (!card.querySelector('.locked-overlay')) {
    const d = document.createElement('div');
    d.className = 'locked-overlay';
    d.textContent = '🔒 BẬT WEB ĐỂ ĐIỀU KHIỂN';
    card.appendChild(d);
  }
}
function removeOverlay(card) {
  const o = card.querySelector('.locked-overlay');
  if (o) o.remove();
}

// ══════════════════════════
// JOYSTICK
// ══════════════════════════
function initJoystick(handle) {
  handle.addEventListener('mousedown',  e => startDrag(e, handle));
  handle.addEventListener('touchstart', e => startDrag(e, handle), { passive: false });
}

document.addEventListener('mousemove',  e => drag(e));
document.addEventListener('touchmove',  e => drag(e), { passive: false });
document.addEventListener('mouseup',    stopDrag);
document.addEventListener('touchend',   stopDrag);

function startDrag(e, handle) {
  if (currentMode !== "WEB") return;
  isDragging = true;
  const rect = handle.parentElement.getBoundingClientRect();
  joyCenter  = { x: rect.left + rect.width / 2, y: rect.top + rect.height / 2 };
  e.preventDefault();
}

function drag(e) {
  if (!isDragging) return;
  e.preventDefault();
  const cx = e.touches ? e.touches[0].clientX : e.clientX;
  const cy = e.touches ? e.touches[0].clientY : e.clientY;

  let dx = cx - joyCenter.x;
  let dy = cy - joyCenter.y;
  const dist = Math.hypot(dx, dy);
  if (dist > MAX_DIST) {
    const ang = Math.atan2(dy, dx);
    dx = Math.cos(ang) * MAX_DIST;
    dy = Math.sin(ang) * MAX_DIST;
  }

  currentX = Math.round(2048 + (dx / MAX_DIST) * 2047);
  currentY = Math.round(2048 + (dy / MAX_DIST) * 2047);
  updateJoyVisual(dx, dy);
  updateDebugXY();
}

function stopDrag() {
  isDragging = false;
  currentX = 2048;
  currentY = 2048;
  updateJoyVisual(0, 0);
  updateDebugXY();
}

function updateJoyVisual(dx, dy) {
  const t = `translate(calc(-50% + ${dx}px), calc(-50% + ${dy}px))`;
  joystick.style.transform     = t;
  joystickTech.style.transform = t;
}

function updateDebugXY() {
  setEl('xValue',   currentX);
  setEl('yValue',   currentY);
  setEl('potValue', currentPot);
}

// ══════════════════════════
// SPEED SLIDER
// ══════════════════════════
function initSpeedSlider(slider) {
  slider.addEventListener('input', function() {
    if (currentMode !== "WEB") { this.value = currentPot; return; }
    currentPot = parseInt(this.value);
    syncSliders();
    updateSpeedDisplay();
  });
}

function syncSliders() {
  speedSlider.value     = currentPot;
  speedSliderTech.value = currentPot;
}

function updateSpeedDisplay() {
  const pct = Math.round((currentPot / 4095) * 100) + '%';
  setEl('speedValue',     pct);
  setEl('speedValueTech', pct);
  setEl('speedPct',       pct);
}

// ══════════════════════════
// TAB SWITCHING
// ══════════════════════════
function switchTab(tab) {
  document.getElementById('panelOps') .classList.toggle('active', tab === 'ops');
  document.getElementById('panelTech').classList.toggle('active', tab === 'tech');
  document.getElementById('tabOps')  .classList.toggle('active', tab === 'ops');
  document.getElementById('tabTech') .classList.toggle('active', tab === 'tech');
}