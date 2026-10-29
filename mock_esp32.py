#!/usr/bin/env python3
"""
mock_esp32.py — Gemelo digital de la maqueta C-137.

Sirve /data en http://localhost:8080 y expone el WebSocket en ws://localhost:8081,
replicando la misma máquina de estados del firmware: setpoint, histéresis de gas
(1500/1300), warm-up de 20 s, llanto sostenido 3 s con rearme tras 2 s de
silencio, barrido del servo y arranque automático del ventilador al 50 %.
"""

import asyncio
import http.server
import json
import math
import random
import socketserver
import sys
import threading
import time
from pathlib import Path

try:
    import websockets
except ImportError:  # pragma: no cover
    sys.exit("Falta el paquete 'websockets'. Instálalo con: pip install websockets")

HTTP_PORT = 8080
WS_PORT = 8081
TICK = 0.1  # Resolución interna de la simulación

# Umbrales idénticos al firmware
LDR_LED2_THRESHOLD = 800
LDR_SUN_OPEN = 2500
LDR_SUN_CLOSE = 1200
GAS_ALERT_ON = 1500
GAS_ALERT_OFF = 1300
SOUND_ALERT_ON = 50
SOUND_ALERT_CALM = 30
BABY_TRIGGER_S = 3.0
BABY_REARM_S = 2.0
GAS_WARMUP_S = 20.0
TEMP_HYSTERESIS = 1.0
TEMP_SETPOINT_MIN = 10.0
TEMP_SETPOINT_MAX = 40.0
FAN_AUTO_UI = 50
SERVO_STEP_DEG = 25  # Equivale a 5° cada 20 ms

CLIENTS = set()
DATA_DIR = Path(__file__).resolve().parent / "data"

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(line_buffering=True)  # Consola útil aun redirigiendo la salida


class Esp32Sim:
    """Estado y física del ESP32 simulado."""

    def __init__(self):
        self.started = time.monotonic()
        self.temp = 24.5
        self.ldr = 1800
        self.gas = 420
        self.sound = 8
        self.led1 = 0
        self.led2 = 0
        self.fan = 0
        self.parasol = 0
        self.parasol_target = 0
        self.setpoint = 26.0
        self.gas_alert = False
        self.baby_alert = False
        self.baby_armed = True
        self.fan_manual = False
        self.parasol_manual = False
        self._loud_time = 0.0
        self._quiet_time = 0.0
        self._gas_boost = 0.0
        self._cry_until = 0.0
        self._next_cry = 45.0  # Primer llanto de demostración
        self._elapsed = 0.0

    # ----------------------------- Sensores -----------------------------
    def _update_sensors(self, dt):
        self._elapsed += dt

        # Temperatura: onda lenta + ruido gaussiano (atraviesa el setpoint)
        base = 24.5 + 2.6 * math.sin(2 * math.pi * self._elapsed / 240.0)
        self.temp += (base - self.temp) * 0.05 + random.gauss(0, 0.05)
        self.temp = max(15.0, min(38.0, self.temp))

        # LDR: ciclo día/noche que cruza 800, 1200 y 2500
        self.ldr = int(
            max(
                0,
                min(
                    4095,
                    1800 + 1350 * math.sin(2 * math.pi * self._elapsed / 120.0)
                    + random.gauss(0, 30),
                ),
            )
        )

        # MQ-2: línea base con ruido + picos de humo esporádicos
        if random.random() < 0.0015 and self._gas_boost < 200:
            self._gas_boost = 1500.0
            print("-> [SIM] Pico de humo simulado en el MQ-2")
        self._gas_boost *= 0.97
        self.gas = int(
            max(0, min(4095, 380 + self._gas_boost + random.gauss(0, 40)))
        )

        # KY-038: ruido ambiental y episodios de llanto
        if self._elapsed >= self._next_cry and not self.baby_alert:
            self._cry_until = self._elapsed + 8.0
            self._next_cry = self._elapsed + random.uniform(90, 150)
            print("-> [SIM] Episodio de llanto simulado")
        if self._elapsed < self._cry_until:
            # Llanto sostenido: nivel digital alto (0–100) durante todo el episodio
            self.sound = int(max(60, min(100, 88 + random.gauss(0, 6))))
        else:
            self.sound = int(max(0, min(20, 6 + abs(random.gauss(0, 5)))))

    # -------------------------- Automatización --------------------------
    def _update_light(self):
        self.led2 = 1 if self.ldr < LDR_LED2_THRESHOLD else 0

    def _update_gas(self):
        if self._elapsed < GAS_WARMUP_S:
            self.gas_alert = False
        elif not self.gas_alert and self.gas > GAS_ALERT_ON:
            self.gas_alert = True
            print("-> [SIM] Alerta de gas activada")
        elif self.gas_alert and self.gas < GAS_ALERT_OFF:
            self.gas_alert = False
            print("-> [SIM] Alerta de gas despejada")

    def _update_baby(self, dt):
        if self.baby_alert:
            return

        if not self.baby_armed:
            if self.sound < SOUND_ALERT_CALM:
                self._quiet_time += dt
                if self._quiet_time >= BABY_REARM_S:
                    self.baby_armed = True
                    self._quiet_time = 0.0
                    print("-> [SIM] Detección de llanto rearmada")
            else:
                self._quiet_time = 0.0
            return

        if self.sound > SOUND_ALERT_ON:
            self._loud_time += dt
            if self._loud_time >= BABY_TRIGGER_S:
                self.baby_alert = True
                self.baby_armed = False
                self._loud_time = 0.0
                self._quiet_time = 0.0
                print("-> [SIM] Alerta de llanto activada (enclavada)")
        else:
            self._loud_time = 0.0

    def _update_parasol(self):
        in_neutral = LDR_SUN_CLOSE <= self.ldr <= LDR_SUN_OPEN

        if self.parasol_manual:
            if in_neutral:
                self.parasol_manual = False
                print("-> [SIM] Override del parasol liberado (banda neutra)")
        elif self.ldr > LDR_SUN_OPEN:
            self.parasol_target = 180
        elif self.ldr < LDR_SUN_CLOSE:
            self.parasol_target = 0

        if self.parasol < self.parasol_target:
            self.parasol = min(self.parasol_target, self.parasol + SERVO_STEP_DEG)
        elif self.parasol > self.parasol_target:
            self.parasol = max(self.parasol_target, self.parasol - SERVO_STEP_DEG)

    def _update_fan(self):
        if self.temp > self.setpoint and self.fan == 0 and not self.fan_manual:
            self.fan = FAN_AUTO_UI
            print(f"-> [SIM] Ventilador automático al {FAN_AUTO_UI}%")
        if self.temp < (self.setpoint - TEMP_HYSTERESIS):
            self.fan_manual = False
            if self.fan != 0:
                self.fan = 0
                print("-> [SIM] Ventilador detenido por histéresis")

    # ------------------------------ Tick --------------------------------
    def tick(self, dt):
        self._update_sensors(dt)
        self._update_light()
        self._update_gas()
        self._update_baby(dt)
        self._update_parasol()
        self._update_fan()

    def snapshot(self):
        return {
            "temp": round(self.temp, 1),
            "ldr": self.ldr,
            "gas": self.gas,
            "sound": self.sound,
            "led1": self.led1,
            "led2": self.led2,
            "fan": self.fan,
            "parasol": self.parasol,
            "temp_setpoint": round(self.setpoint, 1),
            "baby_alert": self.baby_alert,
            "gas_alert": self.gas_alert,
        }

    # ---------------------------- Comandos ------------------------------
    def handle_command(self, data):
        action = data.get("action")

        if action == "toggle_led1":
            self.led1 = 0 if self.led1 else 1
            print(f"-> [CMD] LED 1: {'encendido' if self.led1 else 'apagado'}")

        elif action == "set_fan":
            value = int(max(0, min(100, int(data.get("value", 0)))))
            self.fan = value
            self.fan_manual = True
            print(f"-> [CMD] Ventilador manual al {value}%")

        elif action == "toggle_parasol":
            self.parasol_manual = True
            self.parasol_target = 0 if self.parasol_target >= 180 else 180
            print(f"-> [CMD] Parasol manual hacia {self.parasol_target}°")

        elif action == "set_temp_setpoint":
            value = float(data.get("value", self.setpoint))
            value = max(TEMP_SETPOINT_MIN, min(TEMP_SETPOINT_MAX, value))
            self.setpoint = round(value * 2) / 2
            print(f"-> [CMD] SetPoint: {self.setpoint:.1f} °C")

        elif action == "clear_baby_alert":
            self.baby_alert = False
            self.baby_armed = False
            self._quiet_time = 0.0
            print("-> [CMD] Alerta de llanto descartada (rearme tras 2 s de silencio)")

        else:
            print(f"-> [CMD] Acción desconocida: {action!r}")


sim = Esp32Sim()


# ------------------------------ WebSocket -----------------------------------
async def broadcast():
    if not CLIENTS:
        return
    message = json.dumps(sim.snapshot())
    await asyncio.gather(
        *(client.send(message) for client in list(CLIENTS)),
        return_exceptions=True,
    )


async def handle_ws(connection):
    CLIENTS.add(connection)
    print(f"[WS] Cliente conectado ({len(CLIENTS)} activo/s)")
    try:
        await connection.send(json.dumps(sim.snapshot()))
        async for raw in connection:
            try:
                data = json.loads(raw)
            except (TypeError, ValueError):
                continue
            if isinstance(data, dict):
                sim.handle_command(data)
                await broadcast()
    except websockets.ConnectionClosed:
        pass
    finally:
        CLIENTS.discard(connection)
        print(f"[WS] Cliente desconectado ({len(CLIENTS)} activo/s)")


async def simulation_loop():
    last = time.monotonic()
    next_telemetry = last + 1.0
    while True:
        await asyncio.sleep(TICK)
        now = time.monotonic()
        sim.tick(now - last)
        last = now
        if now >= next_telemetry:
            next_telemetry = now + 1.0
            await broadcast()


# -------------------------------- HTTP --------------------------------------
class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(DATA_DIR), **kwargs)

    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, fmt, *args):
        pass  # Consola limpia: el interés está en los eventos del WebSocket


def run_http():
    socketserver.TCPServer.allow_reuse_address = True
    with socketserver.TCPServer(("", HTTP_PORT), QuietHandler) as httpd:
        print(f"-> Interfaz web:  http://localhost:{HTTP_PORT}")
        httpd.serve_forever()


async def main():
    threading.Thread(target=run_http, daemon=True).start()
    print(f"-> WebSocket:     ws://localhost:{WS_PORT}")
    async with websockets.serve(handle_ws, "0.0.0.0", WS_PORT, ping_interval=20):
        await asyncio.gather(simulation_loop())


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nSimulación detenida.")
