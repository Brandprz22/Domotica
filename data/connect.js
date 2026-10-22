const wsStatus = document.getElementById('wsStatus');
const wsText = document.getElementById('wsText');
const tempValue = document.getElementById('tempValue');
const ldrValue = document.getElementById('ldrValue');
const led1Indicator = document.getElementById('led1Indicator');
const led1State = document.getElementById('led1State');
const led1Btn = document.getElementById('led1Btn');
const led2Indicator = document.getElementById('led2Indicator');
const led2State = document.getElementById('led2State');
const fanSlider = document.getElementById('fanSlider');
const fanValue = document.getElementById('fanValue');
const servoValue = document.getElementById('servoValue');
const presetButtons = document.querySelectorAll('.btn--preset');

let ws = null;
let reconnectTimer = null;
let fanSendTimer = null;
let fanDragging = false;
let lastFanSent = null;

function setConnected(connected) {
  if (connected) {
    wsStatus.classList.add('status--connected');
    wsText.textContent = 'Conectado';
  } else {
    wsStatus.classList.remove('status--connected');
    wsText.textContent = 'Desconectado';
  }
}

function send(payload) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify(payload));
  }
}

function updateLed(indicator, state, button, on) {
  indicator.classList.toggle('indicator--on', on);
  state.textContent = on ? 'Encendido' : 'Apagado';

  if (button) {
    button.setAttribute('aria-pressed', String(on));
    button.classList.toggle('btn--active', on);
  }
}

function updateServoPresets(angle) {
  presetButtons.forEach(function (button) {
    const active = Number(button.dataset.angle) === angle;
    button.classList.toggle('btn--active', active);
    button.setAttribute('aria-pressed', String(active));
  });
}

function updateTelemetry(data) {
  if (typeof data.temp === 'number' && Number.isFinite(data.temp)) {
    tempValue.textContent = data.temp.toFixed(1);
  }

  if (typeof data.ldr === 'number' && Number.isFinite(data.ldr)) {
    ldrValue.textContent = String(Math.round(data.ldr));
  }

  if (data.led1 !== undefined) {
    updateLed(led1Indicator, led1State, led1Btn, data.led1 === 1 || data.led1 === true);
  }

  if (data.led2 !== undefined) {
    updateLed(led2Indicator, led2State, null, data.led2 === 1 || data.led2 === true);
  }

  if (typeof data.fan === 'number' && Number.isFinite(data.fan)) {
    const fan = Math.round(data.fan);
    if (!fanDragging) {
      fanSlider.value = String(fan);
      fanValue.textContent = String(fan);
    }
  }

  if (typeof data.servo === 'number' && Number.isFinite(data.servo)) {
    const angle = Math.round(data.servo);
    servoValue.textContent = String(angle);
    updateServoPresets(angle);
  }
}

function commitFan(value) {
  if (lastFanSent === value) return;
  lastFanSent = value;
  send({ action: 'fan', speed: value });
}

function connect() {
  const host = window.location.hostname || '192.168.4.1';
  ws = new WebSocket('ws://' + host + '/ws');

  ws.onopen = function () {
    if (reconnectTimer) {
      clearTimeout(reconnectTimer);
      reconnectTimer = null;
    }
    setConnected(true);
  };

  ws.onclose = function () {
    setConnected(false);
    if (reconnectTimer) clearTimeout(reconnectTimer);
    reconnectTimer = setTimeout(connect, 1000);
  };

  ws.onerror = function () {
    if (ws) ws.close();
  };

  ws.onmessage = function (event) {
    try {
      const data = JSON.parse(event.data);
      updateTelemetry(data);
    } catch (e) {
      // Ignorar mensajes no válidos
    }
  };
}

led1Btn.addEventListener('click', function () {
  send({ action: 'toggle', target: 'led1' });
});

fanSlider.addEventListener('pointerdown', function () {
  fanDragging = true;
});

fanSlider.addEventListener('pointerup', function () {
  fanDragging = false;
});

fanSlider.addEventListener('pointercancel', function () {
  fanDragging = false;
});

fanSlider.addEventListener('keydown', function () {
  fanDragging = true;
});

fanSlider.addEventListener('input', function () {
  const value = Number(fanSlider.value);
  fanDragging = true;
  fanValue.textContent = String(value);

  if (fanSendTimer) clearTimeout(fanSendTimer);
  fanSendTimer = setTimeout(function () {
    commitFan(value);
    fanDragging = false;
  }, 80);
});

fanSlider.addEventListener('change', function () {
  if (fanSendTimer) clearTimeout(fanSendTimer);
  commitFan(Number(fanSlider.value));
  fanDragging = false;
});

fanSlider.addEventListener('blur', function () {
  fanDragging = false;
});

presetButtons.forEach(function (button) {
  button.addEventListener('click', function () {
    const angle = Number(button.dataset.angle);
    servoValue.textContent = String(angle);
    updateServoPresets(angle);
    send({ action: 'servo', angle: angle });
  });
});

connect();
