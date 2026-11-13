"""Generate a small original sine-wave SF2; no third-party samples."""
import math
from pathlib import Path
import struct
import sys

def chunk(tag, data):
    return tag + struct.pack('<I', len(data)) + data + b'\0' * (len(data) % 2)

def generate(path, bank_offset=0, octave_program=1, pressure=False):
    def name(s): return s.encode().ljust(20, b'\0')
    # Exactly 100 periods at 440 Hz in 44100 samples; loop boundaries are integral.
    pcm = b''.join(struct.pack('<h', round(10000 * math.sin(2 * math.pi * 440 * n / 44100))) for n in range(44100))
    info = chunk(b'LIST', b'INFO' + chunk(b'ifil', struct.pack('<HH', 2, 1)) +
                 chunk(b'isng', b'EMU8000\0') + chunk(b'INAM', b'Juicy test sine\0'))
    sdta = chunk(b'LIST', b'sdta' + chunk(b'smpl', pcm + b'\0' * 92))
    # Bank 8 is an octave higher, so tests can verify the selected bank by audio.
    presets = [('Sine', 0, 0), ('Sine 2', 1, 0), ('Octave', 0, 8), ('Octave 2', octave_program, 8)]
    presets = [(n, pr, bank + bank_offset) for n, pr, bank in presets]
    phdr = b''.join(struct.pack('<20sHHHIII', name(n), pr, bank, i, 0, 0, 0)
                    for i, (n, pr, bank) in enumerate(presets))
    phdr += struct.pack('<20sHHHIII', name('EOP'), 0, 0, len(presets), 0, 0, 0)
    pbag = b''.join(struct.pack('<HH', index, 0) for index in [0, 1, 2, 4, 6])
    pgen = b''.join(struct.pack('<HH', op, val) for op, val in
                    [(41, 0), (41, 0), (51, 12), (41, 0), (51, 12), (41, 0), (0, 0)])
    pdta = chunk(b'phdr', phdr) + chunk(b'pbag', pbag)
    pdta += chunk(b'pmod', b'\0' * 10) + chunk(b'pgen', pgen)
    pdta += chunk(b'inst', struct.pack('<20sH20sH', name('Sine'), 0, name('EOI'), 1))
    # Optional channel-pressure -> attenuation modulator lives in the bank,
    # so a newly created engine must restore pressure for the audio test to pass.
    imod = struct.pack('<HHhHH', 13, 48, 960, 0, 0) if pressure else b''
    pdta += chunk(b'ibag', struct.pack('<HHHH', 0, 0, 2, int(pressure)))
    pdta += chunk(b'imod', imod + b'\0' * 10)
    pdta += chunk(b'igen', struct.pack('<HHHHHH', 54, 1, 53, 0, 0, 0))
    pdta += chunk(b'shdr', struct.pack('<20sIIIIIBbHH', name('Sine'), 0, 44100, 0, 44100, 44100, 69, 0, 0, 1) + struct.pack('<20sIIIIIBbHH', name('EOS'), 44100, 44100, 44100, 44100, 44100, 0, 0, 0, 1))
    Path(path).write_bytes(chunk(b'RIFF', b'sfbk' + info + sdta + chunk(b'LIST', b'pdta' + pdta)))

if __name__ == '__main__':
    generate(sys.argv[1])
    generate(Path(sys.argv[1]).with_name("test-shifted.sf2"), bank_offset=5)
    generate(Path(sys.argv[1]).with_name("test-sparse.sf2"), octave_program=2)

    generate(Path(sys.argv[1]).with_name("test-pressure.sf2"), pressure=True)
