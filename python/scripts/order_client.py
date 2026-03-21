#!/usr/bin/env python3
"""
simple order client for the execution gateway

Usage:
    python3 python/scripts/order_client.py --host 127.0.0.1 --port 9999 --orders 5 --symbol AAPL

sends a sequence of simple ORDER messages and prints reponses
Protocol :
    ORDER|<cl_ord_od|<side>|<symbol>|<price>|<size>\n

    
"""
import argparse
import socket
import time
import random

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9999)
    parser.add_argument("--orders", type=int, default=5)
    parser.add_argument("--symbol", default="AAPL")
    parser.add_argument("--side", choices=["BUY","SELL"], default=None)
    parser.add_argument("--price", type=float, default=0.0,help="if 0, randomize near 100")
    parser.add_argument("--size", type=int, default=100)
    args = parser.parse_args()

    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((args.host, args.port))
    s_file = s.makefile("rwb")

    for i in range(args.orders):
        clid = f"cli{i+1}"
        side = args.side or random.choice(["BUY", "SELL"])
        if args.price == 0.0:
            price = 100.0 + random.uniform(-0.5, 0.5)
        else:
            price = args.price
        size = args.size
        line = f"ORDER|{clid}|{side}|{args.symbol}|{price:.2f}|{size}\n"
        print("->", line.strip())
        s.send(line.encode("utf-8"))
        # read responses (non-blocking-ish, small sleep)
        time.sleep(0.05)
        # read available responses
        s.settimeout(0.1)
        try:
            while True:
                resp = s.recv(4096)
                if not resp:
                    break
                for ln in resp.decode().splitlines():
                    print("<-", ln)
        except socket.timeout:
            pass

    s.close()


if __name__ == '__main__':
    main()