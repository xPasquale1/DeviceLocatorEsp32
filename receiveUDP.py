import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

sock.bind(("0.0.0.0", 4984))

print("Server gestartet")

while True:
    data, addr = sock.recvfrom(1024)
    print("Empfangen: ", addr, ":", data.decode("utf-8"))
