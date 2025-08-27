import socket
import json
import time

def subscribe(topic, callback):
    # Koneksi ke broker
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 8123))

    # Buat pesan subscribe sesuai protokol broker
    message = {
        "type": "subscribe",
        "topic": topic
    }
    s.sendall(json.dumps(message).encode('utf-8'))

    print(f"Berlangganan topik '{topic}'...")

    # Loop untuk terus menerima data
    while True:
        try:
            data = s.recv(1024)
            if data:
                print(data)
                message = json.loads(data.decode('utf-8'))
                if message['type'] == 'publish':
                    # Panggil fungsi callback saat pesan diterima
                    callback(message['topic'], message['payload'])
        except (socket.error, json.JSONDecodeError) as e:
            print(f"Error: {e}")
            break

def on_message(topic, payload):
    print(f"Pesan diterima di topik '{topic}': {payload}")

if __name__ == "__main__":
    try:
        # Berlangganan ke topik 'task.hello'
        subscribe("task.hello", on_message)
    except KeyboardInterrupt:
        print("Berhenti berlangganan.")