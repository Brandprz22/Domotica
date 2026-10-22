const wsStatus = document.getElementById('wsStatus');
const wsText = document.getElementById('wsText');

let ws = null;
let reconnectTimer = null;

function setConnected(connected) {
  if (connected) {
    wsStatus.classList.add('status--connected');
    wsText.textContent = 'Conectado';
  } else {
    wsStatus.classList.remove('status--connected');
    wsText.textContent = 'Desconectado';
  }
}

function updateLights(lights) {
  if (!lights) return;
  ['led1', 'led2'].forEach(function (key) {
    const on = lights[key] === 1 || lights[key] === true;
    const indicator = document.getElementById(key + 'Indicator');
    const state = document.getElementById(key + 'State');
    const button = document.getElementById(key + 'Btn');

    if (indicator) indicator.classList.toggle('indicator--on', on);
    if (state) state.textContent = on ? 'Encendido' : 'Apagado';
    if (button) button.setAttribute('aria-pressed', String(on));
  });
}

function sendToggle(target) {
  if (ws && ws.readyState === WebSocket.OPEN) {
    ws.send(JSON.stringify({ action: 'toggle', target: target }));
  }
}

function connect() {
  const host = window.location.hostname || '192.168.4.1';
  ws = new WebSocket('ws://' + host + '/ws');

  ws.onopen = function () {
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
      updateLights(data.lights);
    } catch (e) {
      // Ignorar mensajes no válidos
    }
  };
}

document.getElementById('led1Btn').addEventListener('click', function () {
  sendToggle('led1');
});

document.getElementById('led2Btn').addEventListener('click', function () {
  sendToggle('led2');
});

connect();
