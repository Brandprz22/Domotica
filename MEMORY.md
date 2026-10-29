# MEMORY.md — Domótica ESP32 (Maqueta C-137)
Memoria del proyecto entre sesiones. Máximo ~50 líneas: resume o elimina lo que ya no aporte.

## Estado actual (29 oct 2026)
- 5 archivos: `src/main.cpp`, `mock_esp32.py`, `data/{index.html,style.css,connect.js}`.
- Firmware no bloqueante: LM35 (34), LDR (35), MQ-2 (32), KY-038 D0 (33), servo parasol (13), fan PWM (18) + DIR (19), LED 1 (17), LED 2 (16).
- Telemetría 1 s: `{temp, ldr, gas, sound, led1, led2, fan, parasol, temp_setpoint, baby_alert, gas_alert}`.
- Comandos: `toggle_led1`, `set_fan`, `toggle_parasol`, `set_temp_setpoint`, `clear_baby_alert`.
- UI de 6 tarjetas blancas simétricas (grid 2 col, máx. 900 px): ambiental, iluminación, climatización, parasol, gas y bebé; títulos #003366 y lecturas #005BB5.
- Mock: HTTP 8080 + WebSocket 8081, con las mismas máquinas de estado y eventos de humo/llanto.
- `/data` pesa 31.4 KB (límite 40 KB). Compila (`pio run`) y `buildfs` genera la imagen LittleFS.

## Decisiones (y por qué)
- ADC1 (32/34/35) con atenuación 11 dB: no colisiona con el Wi-Fi; el 33 pasa a digital para el D0.
- El servo reserva el timer 0; el ventilador usa el canal 2 (timer 1) para no competir por LEDC.
- LM35 con filtro EMA (α = 0.25) en vez de promedio con `delay()`: lectura estable sin bloquear el bucle.
- KY-038 en D0: se cuentan flancos HIGH→LOW en ventanas de 100 ms; `sound` = pulsos × 10 (tope 100). Alerta con ≥ 5 pulsos por ventana e integrador +100/−50 ms hasta 3 s; rearme con 2 s bajo el umbral.
- Parasol: override manual se libera solo en la banda neutra 1200–2500; el servo barre 5° cada 20 ms (~720 ms).
- Fan: 0 % = duty 0; 1–100 % se mapea a 75–255. `set_fan` (incluido 0) activa `fanManual`; la histéresis de apagado lo libera.
- Gas: 20 s de warm-up y luego histéresis 1500/1300.
- Alertas con banner, `navigator.vibrate` y pitido WebAudio; el audio exige un toque previo (política de autoplay móvil).
- `connect.js` instancia `ws = new WebSocket(url)` antes de los handlers y detecta `:8080` (mock) vs `/ws` (ESP32).
- Contrato JSON y paleta se reemplazaron con autorización explícita del usuario; `AGENTS.md` sigue describiendo el anterior.
- Sin frameworks, CDNs, `@import` ni `innerHTML`; DOM quirúrgico y variables CSS.
- Rediseño del 29 oct: fuera la tarjeta azul marino sobredimensionada; el badge «Ambiente seguro / ¡Alerta de gas!» se alterna solo con `.card.is-alert` en CSS, sin tocar `connect.js`.
- Los IDs reales del DOM son camelCase (`tempValue`, `gasValue`, `fanSlider`, `spDown`, `babyClear`…); los guiones (`temp-val`, `fan-slider`…) de algunas notas son obsoletos.

## Aprendizajes y errores a evitar
- Leer `digitalRead(33)` en cada vuelta del loop infla el conteo: hay que detectar el flanco de bajada y acumular por ventana.
- El mock envía `sound` 0–100 (ambiente 0–20, llanto 60–100) y la UI usa `SOUND_MAX = 100`; el marcador de llanto quedó al 50 %.
- `pkill -f mock_esp32.py` mata la propia shell que ejecuta el comando; usar otro patrón o matar por puerto.
- `ldr` y `gas` son cuentas ADC (0–4095), no unidades físicas: la UI los rotula como «cuentas».
- En móvil, desactivar datos móviles para que `192.168.4.1` no se desvíe.

## Próximos pasos
- `pio run -t upload` y `pio run -t uploadfs`; validar LEDs, servo, motor, gas y llanto en el celular.
- Ajustar el potenciómetro del KY-038: en reposo el LED de la placa debe estar apagado y parpadear con el llanto.
- Actualizar la sección de contrato de `AGENTS.md` si el usuario lo autoriza.
