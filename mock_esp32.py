import asyncio
import json
import random
import http.server
import socketserver
import threading
import websockets

HTTP_PORT = 8080
WS_PORT = 8081

# Estado inicial del ESP32 simulado
state = {
    "temp": 24.5,
    "ldr": 650,
    "led1": 0,
    "led2": 0,
    "fan": 0,
    "servo": 90
}

connected_clients = set()

async def broadcast_state():
    """Envía el estado JSON actual a todos los navegadores conectados"""
    if connected_clients:
        message = json.dumps(state)
        await asyncio.gather(*[client.send(message) for client in connected_clients])

async def handle_ws(websocket):
    """Maneja los comandos enviados desde la interfaz web"""
    connected_clients.add(websocket)
    # Envío de sincronización inicial al abrir la página
    await websocket.send(json.dumps(state))
    print("[WS] Cliente web conectado.")

    try:
        async for message in websocket:
            try:
                data = json.loads(message)
                action = data.get("action")

                if action == "toggle" and data.get("target") == "led1":
                    state["led1"] = 1 if state["led1"] == 0 else 0
                    print(f"-> [COMANDO] LED 1 conmutado a: {state['led1']}")

                elif action == "fan":
                    speed = int(data.get("speed", 0))
                    state["fan"] = max(0, min(100, speed))
                    print(f"-> [COMANDO] Ventilador PWM fijado en: {state['fan']}%")

                elif action == "servo":
                    angle = int(data.get("angle", 90))
                    state["servo"] = max(0, min(180, angle))
                    print(f"-> [COMANDO] Servomotor posicionado en: {state['servo']}°")

                await broadcast_state()

            except Exception as e:
                print(f"[ERROR] Procesando comando: {e}")
    finally:
        connected_clients.remove(websocket)
        print("[WS] Cliente web desconectado.")

async def telemetry_loop():
    """Simula la física de los sensores cada 1 segundo"""
    oscilacion_luz = 0
    while True:
        await asyncio.sleep(1.0)

        # Ruido térmico alrededor de 24°C
        state["temp"] = round(24.0 + random.uniform(-0.4, 0.6), 1)

        # Simula oscilación de luz del LDR entre 400 y 950
        oscilacion_luz = (oscilacion_luz + 35) % 600
        state["ldr"] = 400 + oscilacion_luz

        # Lógica reactiva: si LDR < 800 enciende LED 2 automático
        state["led2"] = 1 if state["ldr"] < 800 else 0

        await broadcast_state()

def run_http():
    """Sirve index.html, style.css y connect.js desde la carpeta /data"""
    class Handler(http.server.SimpleHTTPRequestHandler):
        def __init__(self, *args, **kwargs):
            super().__init__(*args, directory="data", **kwargs)

    with socketserver.TCPServer(("", HTTP_PORT), Handler) as httpd:
        print(f"-> Servidor Web disponible en: http://localhost:{HTTP_PORT}")
        httpd.serve_forever()

async def main():
    threading.Thread(target=run_http, daemon=True).start()
    print(f"-> Servidor WebSocket en: ws://localhost:{WS_PORT}")

    server = await websockets.serve(handle_ws, "0.0.0.0", WS_PORT)
    await asyncio.gather(server.wait_closed(), telemetry_loop())

if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nServidor de simulación detenido.")