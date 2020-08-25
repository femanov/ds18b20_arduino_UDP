#!/usr/bin/env python3

import socket
import struct
import numpy as np

HOST = '192.168.11.2'    # The remote host
PORT = 12333              # The same port as used by the server
MESSAGE = b"hello"

is_first_packet = True
ndevs = None
pack_count = 0

def verefy_checksum(data_in):
    a = np.frombuffer(data_in, dtype=np.uint8)
    b = np.zeros(1, dtype=np.uint16)
    for i in range(len(a)-1):
        b[0] += a[i]
    c = np.bitwise_and(b[0], 0xFF)
    return c == a[-1]

with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    print("UDP target IP: %s" % HOST)
    print("UDP target port: %s" % PORT)
    print("message: %s" % MESSAGE)
    sock.sendto(MESSAGE, (HOST, PORT))

    while True:
        data, addr = sock.recvfrom(1024)  # buffer size is 1024 bytes
        print("pack_len=", len(data))
        if not verefy_checksum(data):
            print("bad checksum, dropping packet")
            continue
        pack_type, ndevs = struct.unpack('bb', data[:2])

        print("pack_type=", pack_type, " ndevs=", ndevs)

        if pack_type == 1:
            fmt = "Q" * ndevs + "I"
            d = struct.unpack(fmt, data[2:-1])
            print("ids, lastconv_time", d)

        elif pack_type == 2:
            fmt = "h" * ndevs
            d = struct.unpack(fmt, data[2:-5])
            for x in d:
                print(x/128)
            m = struct.unpack("I", data[-5:-1])
            print("conv_millis=", m[0])

        pack_count += 1
        if pack_count > 5:
            pack_count = 0
            # sending keep-alive
            sock.sendto(MESSAGE, (HOST, PORT))

        #print("data: %s" % data)
        #print(len(data), type(data))


        # if is_first_packet:
        #     is_first_packet = False
        # else:
        #     a = np.frombuffer(data, dtype=np.int16)
        #     print(a/128)

