import socket
import json

def publish(topic, payload):
    # koneksi ke broker
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 8123))

    # buat pesan sesuai protokol broker
    message = {
        "type": "publish",
        "topic": topic,
        "payload": payload
    }
    s.sendall(json.dumps(message).encode('utf-8'))
    s.close()

if __name__ == "__main__":
    publish("task.hello", "From Python")
    publish("math.add", "3,5")
