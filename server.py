import socket

HOST = "192.168.137.1"
PORT = 4984

server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
server_socket.bind((HOST, PORT))
server_socket.listen(5)

print(f"Server läuft und lauscht auf {PORT}...")

while True:
    client_socket, client_address = server_socket.accept()
    print(f"Neue Verbindung von {client_address}")

    data = client_socket.recv(1024)
    if data:
        print(f"Empfangen: {data.decode()}")

    client_socket.shutdown()
    client_socket.close()
