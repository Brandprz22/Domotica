// ============================================================================
//  Casa C-137 · Conexión WebSocket y sincronización de la interfaz
//  Sin dependencias externas. Render quirúrgico sobre nodos existentes.
// ============================================================================

const statusBox = document.getElementById('wsStatus');
const statusText = document.getElementById('wsText');

const tempValue = document.getElementById('tempValue');
const ldrValue = document.getElementById('ldrValue');
const ldrBar = document.getElementById('ldrBar');

const fanSlider = document.getElementById('fanSlider');
const fanValue = document.getElementById('fanValue');
const setpointValue = document.getElementById('setpointValue');
const spDown = document.getElementById('spDown');
const spUp = document.getElementById('spUp');

const led1Switch = document.getElementById('led1Switch');
const led1Lamp = document.getElementById('led1Lamp');
const led1State = document.getElementById('led1State');
const led2Lamp = document.getElementById('led2Lamp');
const led2State = document.getElementById('led2State');

const parasolSwitch = document.getElementById('parasolSwitch');
const parasolState = document.getElementById('parasolState');
const parasolAngle = document.getElementById('parasolAngle');

const gasValue = document.getElementById('gasValue');
const gasBar = document.getElementById('gasBar');
const gasBanner = document.getElementById('gasBanner');
const gasCard = gasValue.closest('.card');

const babyCard = document.getElementById('babyCard');
const babyBanner = document.getElementById('babyBanner');
const babyClear = document.getElementById('babyClear');
const soundValue = document.getElementById('soundValue');
const soundMeter = document.getElementById('soundMeter');

const RAW_MAX = 4095;
const SOUND_MAX = 100; // Nivel digital del KY-038 (D0) enviado por el firmware
const SETPOINT_MIN = 10;
const SETPOINT_MAX = 40;
const FAN_HOLD_MS = 900;
const PARASOL_HOLD_MS = 1500;

let ws = null;
let reconnectTimer = null;
let reconnectDelay = 1000;

let fanSendTimer = null;
let fanHoldUntil = 0;
let setpointHoldUntil = 0;
let parasolHoldUntil = 0;
let currentSetpoint = 26;

const alerts = { gas: false, baby: false };

// ------------------------------- Conexión -----------------------------------

// Producción: ws://<host>/ws · Simulación local: ws://<host>:8081
function buildWsUrl() {
  if (window.location.protocol === 'file:') {
    return 'ws://localhost:8081';
  }

  const host = window.location.hostname || 'localhost';
  const scheme = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
  const isMock = window.location.port === '8080';
  return scheme + '//' + host + (isMock ? ':8081' : '/ws');
}

function setConnected(connected) {
  statusBox.classList.toggle('is-live', connected);
  statusText.textContent = connected ? 'En vivo' : 'Sin conexión';
}

function connect() {
  const wsUrl = buildWsUrl();
  ws = new WebSocket(wsUrl); // El socket debe existir antes de los handlers
  const socket = ws;

  socket.onopen = function () {
    if (socket !== ws) return;
    reconnectDelay = 1000;
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    setConnected(true);
  };

  socket.onmessage = function (event) {
    if (socket !== ws) return;
    let data;
    try {
      data = JSON.parse(event.data);
    } catch (error) {
      return; // Mensaje no válido: se ignora
    }
    if (data && typeof data === 'object') updateTelemetry(data);
  };

  socket.onerror = function () {
    socket.close();
  };

  socket.onclose = function () {
    if (socket !== ws) return;
    setConnected(false);
    if (reconnectTimer) return;
    reconnectTimer = setTimeout(function () {
      reconnectTimer = null;
      connect();
    }, reconnectDelay);
    reconnectDelay = Math.min(reconnectDelay * 1.5, 5000);
  };
}

function send(payload) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  }
}

// ------------------------------- Telemetría ---------------------------------

function isNumber(value) {
  return typeof value === 'number' && Number.isFinite(value);
}

function clamp(value, min, max) {
  return Math.min(max, Math.max(min, value));
}

function setBar(fill, value) {
  fill.style.width = clamp((value / RAW_MAX) * 100, 0, 100).toFixed(1) + '%';
}

function updateTelemetry(data) {
  if (isNumber(data.temp)) {
    tempValue.textContent = data.temp.toFixed(1);
  }

  if (isNumber(data.ldr)) {
    ldrValue.textContent = String(Math.round(data.ldr));
    setBar(ldrBar, data.ldr);
  }

  if (isNumber(data.gas)) {
    gasValue.textContent = String(Math.round(data.gas));
    setBar(gasBar, data.gas);
  }

  if (isNumber(data.sound)) {
    const level = clamp(data.sound, 0, SOUND_MAX);
    soundValue.textContent = String(Math.round(level));
    soundMeter.style.setProperty('--level', ((level / SOUND_MAX) * 100).toFixed(1) + '%');
    soundMeter.setAttribute('aria-valuenow', String(Math.round(level)));
  }

  if (isNumber(data.fan) && Date.now() > fanHoldUntil) {
    const fan = clamp(Math.round(data.fan), 0, 100);
    fanSlider.value = String(fan);
    fanValue.textContent = String(fan);
  }

  if (isNumber(data.temp_setpoint) && Date.now() > setpointHoldUntil) {
    currentSetpoint = clamp(data.temp_setpoint, SETPOINT_MIN, SETPOINT_MAX);
    setpointValue.textContent = currentSetpoint.toFixed(1);
  }

  if (data.led1 !== undefined) {
    const on = data.led1 === 1 || data.led1 === true;
    led1Switch.checked = on;
    led1Lamp.classList.toggle('is-on', on);
    led1State.textContent = on ? 'Encendido' : 'Apagado';
  }

  if (data.led2 !== undefined) {
    const on = data.led2 === 1 || data.led2 === true;
    led2Lamp.classList.toggle('is-on', on);
    led2State.textContent = on ? 'Encendido por oscuridad' : 'Apagado';
  }

  if (isNumber(data.parasol)) {
    const angle = clamp(Math.round(data.parasol), 0, 180);
    parasolAngle.textContent = String(angle);
    if (Date.now() > parasolHoldUntil) {
      parasolSwitch.checked = angle >= 90;
    }
    parasolState.textContent = angle >= 90 ? 'Abierto' : 'Cerrado';
  }

  if (data.gas_alert !== undefined) {
    setBanner('gas', data.gas_alert === true, gasBanner);
    gasCard.classList.toggle('is-alert', data.gas_alert === true);
  }

  if (data.baby_alert !== undefined) {
    const active = data.baby_alert === true;
    setBanner('baby', active, babyBanner);
    babyCard.classList.toggle('is-alert', active);
  }
}

// -------------------------------- Alertas -----------------------------------

function setBanner(kind, active, node) {
  node.hidden = !active;
  if (active && !alerts[kind]) {
    announce(kind);
  }
  alerts[kind] = active;
}

let audioCtx = null;

function unlockAudio() {
  const AudioCtor = window.AudioContext || window.webkitAudioContext;
  if (!AudioCtor) return;
  if (!audioCtx) {
    audioCtx = new AudioCtor();
  }
  if (audioCtx.state === 'suspended') {
    audioCtx.resume();
  }
}

function beep(frequency) {
  if (!audioCtx || audioCtx.state !== 'running') return;

  const now = audioCtx.currentTime;
  for (let i = 0; i < 3; i++) {
    const start = now + i * 0.3;
    const osc = audioCtx.createOscillator();
    const gain = audioCtx.createGain();
    osc.type = 'square';
    osc.frequency.setValueAtTime(frequency, start);
    gain.gain.setValueAtTime(0.0001, start);
    gain.gain.exponentialRampToValueAtTime(0.08, start + 0.02);
    gain.gain.exponentialRampToValueAtTime(0.0001, start + 0.2);
    osc.connect(gain).connect(audioCtx.destination);
    osc.start(start);
    osc.stop(start + 0.22);
  }
}

function announce(kind) {
  if (navigator.vibrate) {
    navigator.vibrate(kind === 'gas' ? [300, 120, 300] : [200, 100, 200]);
  }
  beep(kind === 'gas' ? 740 : 560);
}

window.addEventListener('pointerdown', unlockAudio);
window.addEventListener('keydown', unlockAudio);

// -------------------------------- Controles ---------------------------------

led1Switch.addEventListener('change', function () {
  send({ action: 'toggle_led1' });
});

fanSlider.addEventListener('input', function () {
  const value = clamp(Math.round(Number(fanSlider.value)), 0, 100);
  fanValue.textContent = String(value);
  fanHoldUntil = Date.now() + FAN_HOLD_MS;

  if (fanSendTimer) clearTimeout(fanSendTimer);
  fanSendTimer = setTimeout(function () {
    fanSendTimer = null;
    send({ action: 'set_fan', value: value });
  }, 120);
});

fanSlider.addEventListener('change', function () {
  if (fanSendTimer) {
    clearTimeout(fanSendTimer);
    fanSendTimer = null;
  }
  const value = clamp(Math.round(Number(fanSlider.value)), 0, 100);
  fanHoldUntil = Date.now() + FAN_HOLD_MS;
  send({ action: 'set_fan', value: value });
});

function changeSetpoint(delta) {
  const next = clamp(
    Math.round((currentSetpoint + delta) * 2) / 2,
    SETPOINT_MIN,
    SETPOINT_MAX
  );
  currentSetpoint = next;
  setpointValue.textContent = next.toFixed(1);
  setpointHoldUntil = Date.now() + FAN_HOLD_MS;
  send({ action: 'set_temp_setpoint', value: next });
}

spDown.addEventListener('click', function () {
  changeSetpoint(-0.5);
});

spUp.addEventListener('click', function () {
  changeSetpoint(0.5);
});

parasolSwitch.addEventListener('change', function () {
  parasolHoldUntil = Date.now() + PARASOL_HOLD_MS;
  send({ action: 'toggle_parasol' });
});

babyClear.addEventListener('click', function () {
  send({ action: 'clear_baby_alert' });
  babyBanner.hidden = true;
  babyCard.classList.remove('is-alert');
  alerts.baby = false; // Permite que una nueva alerta vuelva a sonar
});

connect();
