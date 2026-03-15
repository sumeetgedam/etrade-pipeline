#!/usr/bin/env python3
"""
Simple UDP feed sender for testing udp_receiver.

Each message format (text for now):
    <seq>|<iso-ts-ms>|<SYM>|<price>|<size>

Example:
    1|16788800000000|AAPL|173.42|100

To be ran while udp_receiver is running
"""
import socket
import time
import argparse

def make_msg(seq, symbol="AAPL", price=174.23, size=100):
    ts_ms = int(time.time() * 1000)
    return f"{seq}|{ts_ms}|{symbol}|{price:.2f}|{size}".encode()


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9000)
    p.add_argument("--count", type=int, default=20)
    p.add_argument("--rate", type=float, default=100.0, help="messages per second")
    args = p.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    interval = 1.0 / max(args.rate, 1.0)

    for i in range(1, args.count + 1):
        msg = make_msg(i)
        sock.sendto(msg, (args.host, args.port))
        print("sent: ", msg)
        time.sleep(interval)

if __name__ == "__main__":
    main()