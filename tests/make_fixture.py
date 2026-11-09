"""Generate a small original sine-wave SF2; no third-party samples."""
import math
from pathlib import Path
import struct
import sys

def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + b'\0' * (len(data) % 2)

def generate(path):
    def name(s): return s.encode().ljust(20, b'\0')
    # Exactly 100 periods at 440 Hz in 44100 samples; loop boundaries are integral.
    pcm = b''.join(struct.pack('<h', round(10000 * math.sin(2 * math.pi * 440 * n / 44100))) for n in range(44100))
    info = chunk(b'LIST', b'INFO' + chunk(b'ifil', struct.pack('<HH', 2, 1)) +
                 chunk(b'isng', b'EMU8000\0') + chunk(b'INAM', b'Juicy test sine\0'))
    sdta = chunk(b'LIST', b'sdta' + chunk(b'smpl', pcm + b'\0' * 92))
    phdr = struct.pack('<20sHHHIII', name('Sine'), 0, 0, 0, 0, 0, 0) + struct.pack('<20sHHHIII', name('EOP'), 0, 0, 1, 0, 0, 0)
    pdta = chunk(b'phdr', phdr) + chunk(b'pbag', struct.pack('<HHHH', 0, 0, 1, 0))
    pdta += chunk(b'pmod', b'\0' * 10) + chunk(b'pgen', struct.pack('<HHHH', 41, 0, 0, 0))
    pdta += chunk(b'inst', struct.pack('<20sH20sH', name('Sine'), 0, name('EOI'), 1))
    pdta += chunk(b'ibag', struct.pack('<HHHH', 0, 0, 2, 0)) + chunk(b'imod', b'\0' * 10)
    pdta += chunk(b'igen', struct.pack('<HHHHHH', 54, 1, 53, 0, 0, 0))
    pdta += chunk(b'shdr', struct.pack('<20sIIIIIBbHH', name('Sine'), 0, 44100, 0, 44100, 44100, 69, 0, 0, 1) + struct.pack('<20sIIIIIBbHH', name('EOS'), 44100, 44100, 44100, 44100, 44100, 0, 0, 0, 1))
    Path(path).write_bytes(chunk(b'RIFF', b'sfbk' + info + sdta + chunk(b'LIST', b'pdta' + pdta)))

if __name__ == '__main__':
    generate(sys.argv[1])
