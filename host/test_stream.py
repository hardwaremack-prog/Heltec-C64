#!/usr/bin/env python3
"""End-to-end test of the C64 Mesh WebSocket stream against ./c64host.
  python3 test_stream.py video   -> types on the keyboard + paste, saves screen PNGs, dumps tile messages
  python3 test_stream.py audio   -> needs c64host --sys disk/A440.prg; measures the streamed pitch
"""
import asyncio, struct, sys, time, json
import numpy as np
import websockets
from PIL import Image

PAL = [0x000000, 0xFFFFFF, 0x813338, 0x75CEC8, 0x8E3C97, 0x56AC4D, 0x2E2C9B, 0xEDF171,
       0x8E5029, 0x553800, 0xC46C71, 0x4A4A4A, 0x7B7B7B, 0xA9FF9F, 0x706DEB, 0xB2B2B2]
W, H = 384, 272

def apply_tiles(m, scr):
    n = m[1] | (m[2] << 8); p = 3
    for _ in range(n):
        tx, ty, mode = m[p], m[p + 1], m[p + 2]; p += 3
        x0, y0 = tx * 16, ty * 16
        if mode == 0:
            scr[y0:y0 + 16, x0:x0 + 16] = m[p]; p += 1
        elif mode == 1:
            c0, c1 = m[p] & 15, m[p] >> 4; p += 1
            for y in range(16):
                bits = (m[p] << 8) | m[p + 1]; p += 2
                for x in range(16):
                    scr[y0 + y, x0 + x] = c1 if bits & (0x8000 >> x) else c0
        else:
            for y in range(16):
                for x in range(8):
                    b = m[p]; p += 1
                    scr[y0 + y, x0 + 2 * x] = b & 15; scr[y0 + y, x0 + 2 * x + 1] = b >> 4
    assert p == len(m), (p, len(m))

def save(scr, path):
    rgb = np.zeros((H, W, 3), np.uint8)
    for i, c in enumerate(PAL):
        rgb[scr == i] = ((c >> 16) & 255, (c >> 8) & 255, c & 255)
    Image.fromarray(rgb).resize((W * 2, H * 2), Image.NEAREST).save(path)

def key(down, sym): return bytes([0x10, 1 if down else 0]) + struct.pack('<I', sym)

async def pump(ws, scr, seconds, log=None, audio=None, stats=None):
    end = time.time() + seconds
    while time.time() < end:
        try:
            m = await asyncio.wait_for(ws.recv(), timeout=max(0.01, end - time.time()))
        except asyncio.TimeoutError:
            break
        if m[0] == 1:
            apply_tiles(m, scr)
            if log is not None: log.append(m.hex())
            if stats is not None: stats['bytes'] += len(m)
        elif m[0] == 3:
            await ws.send(bytes([0x13]))
            if stats is not None: stats['frames'] += 1
        elif m[0] == 2 and audio is not None:
            audio.extend(struct.unpack('<%dh' % ((len(m) - 1) // 2), m[1:]))

async def video():
    scr = np.zeros((H, W), np.uint8)
    async with websockets.connect('ws://127.0.0.1:8181/') as ws:
        hello = await ws.recv()
        w, h, rate = struct.unpack('<HHI', hello[1:9])
        print('hello', hello[0], w, h, rate)
        log, stats = [], {'bytes': 0, 'frames': 0}
        await pump(ws, scr, 3.5, log, stats=stats)
        print('first screen: %d frames, %d bytes of tiles' % (stats['frames'], stats['bytes']))
        json.dump(log, open('tiles.json', 'w'))
        save(scr, 'ws_boot.png')
        np.save('ws_boot.npy', scr)
        # type on the keyboard: load"$",8 <return> list <return>
        for ch in 'load"$",8':
            await ws.send(key(True, ord(ch))); await ws.send(key(False, ord(ch)))
        await ws.send(key(True, 0xff0d)); await ws.send(key(False, 0xff0d))
        await pump(ws, scr, 1.5)
        for ch in 'list':
            await ws.send(key(True, ord(ch))); await ws.send(key(False, ord(ch)))
        await ws.send(key(True, 0xff0d)); await ws.send(key(False, 0xff0d))
        # on-screen keyboard style raw matrix keys: SHIFT(1,7) held + Q(7,6) -> graphics char
        sh, q = 0xC64000 + 1 * 8 + 7, 0xC64000 + 7 * 8 + 6
        await ws.send(key(True, sh)); await ws.send(key(True, q)); await ws.send(key(False, q)); await ws.send(key(False, sh))
        await pump(ws, scr, 1.5)
        await ws.send(bytes([0x11]) + b'\nPRINT "PASTED OK"\n')
        stats = {'bytes': 0, 'frames': 0}
        await pump(ws, scr, 3, stats=stats)
        print('typing phase: %d frames, %d bytes' % (stats['frames'], stats['bytes']))
        save(scr, 'ws_typed.png')

async def audio():
    scr = np.zeros((H, W), np.uint8)
    async with websockets.connect('ws://127.0.0.1:8181/') as ws:
        await ws.recv()
        await ws.send(bytes([0x14, 1]))
        await pump(ws, scr, 4)          # let the test program start (frame 150 = 3 s)
        pcm = []
        await pump(ws, scr, 2, audio=pcm)
        d = np.array(pcm, float)
        print('audio samples in 2 s:', len(d), ' peak', int(abs(d).max()))
        sp = abs(np.fft.rfft(d * np.hanning(len(d)))); fr = np.fft.rfftfreq(len(d), 1 / 22050)
        print('dominant %.1f Hz' % fr[sp[5:].argmax() + 5])
        await ws.send(bytes([0x14, 0]))
        await pump(ws, scr, 0.5)
        pcm2 = []
        await pump(ws, scr, 1, audio=pcm2)
        print('after sound off:', len(pcm2), 'samples')

asyncio.run(video() if sys.argv[1] == 'video' else audio())
