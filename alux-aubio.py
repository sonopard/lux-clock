#! /usr/bin/env python

import sys
import time
import alsaaudio
import numpy as np
import aubio
from array import array
import socket
from itertools import chain
import colorsys

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
num_leds=120
leds = [(0,0,0)] * num_leds

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
pitcher_2 = aubio.pitch("default", win_s, hop_s, samplerate)
# set output unit (can be 'midi', 'cent', 'Hz', ...)
pitcher.set_unit("Hz")
pitcher_2.set_unit("Hz")
# ignore frames under this level (dB)
pitcher.set_silence(-40)
pitcher_2.set_silence(-40)

print("Starting to listen, press Ctrl+C to stop")

while True:
    try:
        # read data from audio input
        _, data = recorder.read()
        # convert data to aubio float samples
        samples = np.frombuffer(data, dtype=aubio.float_type)
        # pitch of current frame
        freq = pitcher(samples)[0]
        note = pitcher_2(samples)[0]
        is_beat = a_tempo(samples)
        # compute energy of current block
        energy = np.sum(samples**2)/len(samples)
        # do something with the results
        f_scaled = (int)((freq))%360
        if f_scaled > (num_leds)-1:
            f_scaled = f_scaled % num_leds
        e_scaled = (int)(energy * 255 * 24)
        if e_scaled > 255:
            e_scaled = 255
        
        col = (note, 1, e_scaled/255)
        col = colorsys.hsv_to_rgb(*col)
        col = ((int)(col[0]*255),(int)(col[1]*255),(int)(col[2]*255))
        leds[f_scaled] = col
        
        if is_beat:
            for i, l in enumerate(leds):
                if np.sum(l) < 10:
                    leds[i] = (0x30,0x30,0x30)
        sock.sendto(bytearray(list(chain.from_iterable(leds))),("10.23.42.103", 2342))
        v = chain.from_iterable(leds)
        v =  [(int)(max(0, x - (x * x * 0.0015))) for x in v]
        v_it = iter(v)
        leds = list(zip(v_it, v_it, v_it))
        print("{:10.4f} {:10.4f}".format(freq,energy))
    except KeyboardInterrupt:
        print("Ctrl+C pressed, exiting")
        break
