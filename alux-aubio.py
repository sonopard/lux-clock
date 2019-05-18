#! /usr/bin/env python

import sys
import time
import alsaaudio
import numpy as np
import aubio
from array import array
import socket

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
num_leds=120
leds = [0] * num_leds * 3
leds_full = array('B',[0xaf] * num_leds * 3)
leds_off = array('B',[0x00] * num_leds * 3)

# constants
samplerate = 44100
win_s = 2048
hop_s = win_s // 2
framesize = hop_s

# set up audio input
recorder = alsaaudio.PCM(type=alsaaudio.PCM_CAPTURE)
recorder.setperiodsize(framesize)
recorder.setrate(samplerate)
recorder.setformat(alsaaudio.PCM_FORMAT_FLOAT_LE)
recorder.setchannels(1)

# create aubio tempo detection
a_tempo = aubio.tempo("default", win_s, hop_s, samplerate)
# create aubio pitch detection (first argument is method, "default" is
# "yinfft", can also be "yin", "mcomb", fcomb", "schmitt").
pitcher = aubio.pitch("default", win_s, hop_s, samplerate)
# set output unit (can be 'midi', 'cent', 'Hz', ...)
pitcher.set_unit("Hz")
# ignore frames under this level (dB)
pitcher.set_silence(-40)

print("Starting to listen, press Ctrl+C to stop")

while True:
    try:
        # read data from audio input
        _, data = recorder.read()
        # convert data to aubio float samples
        samples = np.frombuffer(data, dtype=aubio.float_type)
        # pitch of current frame
        freq = pitcher(samples)[0]
        is_beat = a_tempo(samples)
        if is_beat:
            leds = [(int)(min(255, v + 0xaa)) for v in leds]
        # compute energy of current block
        energy = np.sum(samples**2)/len(samples)
        # do something with the results
        f_scaled = (int)((freq))%360
        if f_scaled > (num_leds*3)-1:
            f_scaled = (num_leds*3)-1
        e_scaled = (int)(energy * 255 * 24)
        if e_scaled > 255:
            e_scaled = 255
        leds[f_scaled] = e_scaled
        sock.sendto(bytearray(leds),("10.23.42.103", 2342))
        leds = [(int)(max(0, v - (v * v * 0.0015))) for v in leds]
        print("{:10.4f} {:10.4f}".format(freq,energy))
    except KeyboardInterrupt:
        print("Ctrl+C pressed, exiting")
        break
