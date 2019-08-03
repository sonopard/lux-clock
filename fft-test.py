#!/usr/bin/env python

import numpy as np
import matplotlib.pyplot as plt
import audioop
import alsaaudio as aa
from struct import unpack
import socket
from array import array
from itertools import chain
import colorsys
np.set_printoptions(precision=2)#formatter={'all':lambda x: '{:2d}'.format(x)})

# audio setup
sample_rate = 44100
no_channels = 2
chunk = 1024 # read chunk size, multiple of 8
data_in = aa.PCM(aa.PCM_CAPTURE, aa.PCM_NORMAL)
data_in.setchannels(no_channels)
data_in.setrate(sample_rate)
data_in.setformat(aa.PCM_FORMAT_S16_LE)
data_in.setperiodsize(chunk)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

num_leds=124
max_amp=0
while True:
    l, data = data_in.read()
    data_in.pause(1)
    data = unpack("%dh"%(len(data)/2), data)
    data = np.array(data, dtype='h')
    hann = np.hanning(len(data))
    fourier = np.fft.rfft(data*hann)#, norm="ortho")
    freq = np.fft.rfftfreq(data.size, d=1./sample_rate)
    freq = freq [:-1]
    fourier=np.abs(fourier)
    N = len(fourier)/2
    fourier=fourier[:N]
    fourier=fourier[:-(264+124)]
    print(len(fourier))
##    fourier = np.log2(fourier)
    fourier = np.reshape(fourier, (num_leds, len(fourier)/num_leds))
    fourier = np.average(fourier, axis=1)
    fourier = fourier * np.linspace(1, 8, len(fourier), dtype="float_")
    max_amp = max_amp - 1
    if(max_amp<fourier.max()):
            max_amp=fourier.max()
    #scale = np.average(fourier) / max_amp
    leds = fourier / fourier.max()
    leds = np.abs(leds) #occasional slightly negative value causes conversoin errors FIXME better?
    leds = np.nan_to_num(leds) #all zero input data results in NaN
    #leds = array('B',list(chain.from_iterable(zip(leds, leds, leds))))
    stickdata = array('B',[])
    for h in leds:
        r, g, b = colorsys.hsv_to_rgb(h, 1., max(0,(h/2)-0.2))#scale)
        r, g, b =  255*r,  255*g,  255*b
        stickdata.extend((int(r), int(g), int(b)))
    sock.sendto(stickdata,("olp_bigclock", 2342))
    data_in.pause(0)
