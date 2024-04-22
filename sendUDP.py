import socket

routers = ["HotspotPascal", "Hotspot1", "Hotspot2"]

def sendUdpMessage(message: str, ip, port) -> int:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ret = sock.sendto(message.encode(), (ip, port)) 
    sock.close()
    return ret

for ssid in routers:
    if sendUdpMessage(chr(3) + ssid + chr(0), "192.168.137.133", 4984) <= 0:
        print("Packet nicht gesendet...")
    else:
        print("Packet gesendet!\n")
