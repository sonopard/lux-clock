#!/usr/bin/env python

from struct import unpack
import socket
from array import array
from itertools import chain
import colorsys
from sys import argv

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

num_leds = int(argv[1])

#while True:
stickdata = [64 for i in range(num_leds * 3)]
#sock.sendto(bytes([int(len(stickdata) / 256), len(stickdata) % 256]), ("olp_bigclock", 2342))
#sock.sendto(bytes([2, 6]), ("olp_bigclock", 2342))

data = bytes([int(len(stickdata) / 256), len(stickdata) % 256] + stickdata)

data_send = 0
while len(stickdata) - data_send > 0:
    data_send += sock.sendto(data[data_send:], ("olp_bigclock", 2342))
    print(data_send)
