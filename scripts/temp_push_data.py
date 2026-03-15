import socket, time


s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

for i in range(5):
    msg = f'TEST-{i}'.encode()
    s.sendto(msg, ('127.0.0.1', 9000))
    time.sleep(0.3)