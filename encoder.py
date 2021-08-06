from PIL import Image, ImageSequence
import numpy as np
import os
import sys
from io import BytesIO
import struct
from subprocess import run, PIPE
from hashlib import md5

typedef = '''

'''

def to_hex(a):
  return '0x{:02x}'.format(a)

def adpcm_encode(content):
  proc = run(['./adpcm-xq', '-b8', '-e', '/dev/stdin', '/dev/stdout'], capture_output=True, input=content)
  # print('subprocess.stderr', proc.stderr)
  return proc.stdout


def encode_file(filename):
  name = os.path.basename(filename).split('.')[0]
  with open(filename, 'rb') as file:
    enc_data = adpcm_encode(file.read())
    content = struct.pack('<I', len(enc_data)) + enc_data
    t = np.array2string(np.frombuffer(content, dtype=np.uint8), max_line_width=81, separator=',', formatter={'int': to_hex}, threshold=2**20)
    declare = 'const uint8_t adpcm_%s[] PROGMEM ={\n ' % name + t[1:-1] +'\n};'
    return declare, name, len(content)


if __name__ == "__main__":
  declare, name, length = encode_file('./one.wav')
  print(declare)
  print(name)
  print(length)





