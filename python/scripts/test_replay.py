#!/usr/bin/env python3

# Quick integration test: start engine , snd to USP market messages to set top-of-book,
# then send an ORDER to the gateway and assert a FILL is returned


import os
import socket
import subprocess
import sys
import time

ENGINE_BIN = os.environ.get("ENGINE_BIN", "./cpp/build/engine")
UDP_HOST = "127.0.0.1"
UDP_PORT = 9000
GW_HOST = "127.0.0.1"
GW_PORT = 9999
CLI_ID = "cli-test-1"

def start_engine():
    args = [ENGINE_BIN, str(UDP_PORT), "1", str(GW_PORT), "100000", "9100"]
    print("Starting engine : ", " ".join(args))
    p = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    return p

def wait_for_port(host, port, timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=1.0)
            s.close()
            return True
        except Exception:
            time.sleep(0.2)
    return False
    
def send_udp_msg(seq, msg_ts_ms, symbol, price, size):
    msg = f"{seq}|{msg_ts_ms}|{symbol}|{price:.2f}|{size}\n".encode("utf-8")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(msg, (UDP_HOST, UDP_PORT))
    sock.close()
    print("sent udp : ", msg.decode().strip())


def test_flow():
    # start engine
    p = start_engine()
    try:
        # wait for gateway port to be available
        print("waiting for gateway ", GW_HOST, GW_PORT)
        if not wait_for_port(GW_HOST, GW_PORT, timeout=8.0):
            print("Gateway not ready (timeout). engine stderr: ")
            print(p.stderr.read())
            return 2
        
        # send two market messages that create an offer at price 100.00 for AAPL
        now_ms = int(time.time() * 1000)
        send_udp_msg(1, now_ms, "AAPL", 100.00, 100)
        time.sleep(0.05)
        send_udp_msg(2, now_ms + 50, "AAPL", 101.00, 50) # bid or other level
        
        time.sleep(0.2) # let orderbook apply

        # connect to gateway and submit BUY order that should match offer at 100.00
        s = socket.create_connection((GW_HOST, GW_PORT), timeout=5.0)
        s.settimeout(5.0)
        order_line = f"ORDER|{CLI_ID}|BUY|AAPL|102.00|10\n"
        s.sendall(order_line.encode("utf-8"))
        print("sent order:  ", order_line.strip())

        # read lines until we get ACK and FILL or timeout
        data = b""
        got_ack = False
        got_fill = False
        deadline = time.time() + 5.0
        while time.time() < deadline:
            try:
                chunk = s.recv(4096)
                if not chunk:
                    break
                data += chunk
                lines = data.split(b'\n')
                for ln in lines:
                    if not ln:
                        continue
                    txt = ln.decode('utf-8', errors='ignore').strip()
                    print("recv : ", txt)
                    if txt.startswith("ACK|") and CLI_ID in txt:
                        got_ack = True
                    if txt.startswith("FILL|") and CLI_ID in txt:
                        got_fill = True
                if got_ack and got_fill:
                    break
            except socket.timeout:
                break
        s.close()

        if not got_ack:
            print("Did not receive ACK, Response : ", data.decode(errors='ignore'))
            return 3
        if not got_fill:
            print("Did not receive FILL, Response : ", data.decode(errors='ignore'))
            return 4
        
        print("Test succeeded : got ACK and FILL")
        return 0
    
    finally:
        # terminate engine
        p.terminate()
        try:
            p.wait(timeout=2.0)
        except subprocess.TimeoutExpired:
            p.kill()


if __name__ == "__main__":
    rc = test_flow()
    sys.exit(rc)