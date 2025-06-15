import asyncio
import websockets
import threading
import http.server
import socketserver
import os

HTTP_PORT = 8000
WS_PORT = 8001
clients = set()

async def ws_handler(websocket):
    # if path != "/ws":
    #     # print(f"Rejected connection on path: {path}")
    #     await websocket.close()
    #     return

    clients.add(websocket)
    try:
        async for message in websocket:
            print(f"Received from form: {message}")
            # Broadcast to all other connected clients except the sender
            await asyncio.gather(*(c.send(message) for c in clients))
    except websockets.exceptions.ConnectionClosed:
        pass
    finally:
        clients.remove(websocket)

def run_http_server():
    os.chdir(os.path.dirname(os.path.abspath(__file__)))
    handler = http.server.SimpleHTTPRequestHandler
    with socketserver.TCPServer(("", HTTP_PORT), handler) as httpd:
        print(f"HTTP server running at http://localhost:{HTTP_PORT}")
        httpd.serve_forever()

async def run_ws_server():
    async with websockets.serve(ws_handler, "0.0.0.0", WS_PORT):
        print(f"WebSocket server listening on ws://localhost:{WS_PORT}/ws")
        await asyncio.Future()  # run forever

if __name__ == "__main__":
    # Start HTTP server in a background thread
    threading.Thread(target=run_http_server, daemon=True).start()

    # Start WebSocket server in main asyncio event loop
    asyncio.run(run_ws_server())