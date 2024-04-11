import socket

def sendUdpMessage(message: str, ip, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(message.encode(), (ip, port))
    sock.close()

sendUdpMessage("0", "192.168.178.66", 5000)
print("Packet gesendet!\n")
