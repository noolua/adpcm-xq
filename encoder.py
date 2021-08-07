from PIL import Image, ImageSequence
import numpy as np
import os
import sys
from io import BytesIO
import struct
from subprocess import run, PIPE
from hashlib import md5

typedef = '''
#ifndef __sndeffects_h__
#define __sndeffects_h__
#include <stdint.h>

typedef struct adpcm_progm_s{
  const uint32_t size;
  const uint8_t content[0];
}adpcm_progm_t;

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

def process_snd(filepath):
  filelist = os.listdir(filepath)
  for fichier in filelist[:]:
    if not(fichier.endswith(".wav")):
        filelist.remove(fichier)
  files = ['%s%s' % (filepath, name) for name in filelist]
  files.sort()
  # files = ['./data/ccc/spr_plant.png']
  # print(files)
  all_name = []
  all_declare = []
  all_size = []
  total_bytes = 0
  for f in files:
    declare, name, length = encode_file(f)
    all_name.append(name)
    all_declare.append(declare)
    all_size.append(length)
    total_bytes += length
  # define emnu
  print(typedef)
  print('typedef enum {')
  for name in all_name:
    print('  snd_%s,' % name)
  print('};\n\n')

  for idx in range(len(all_name)):
    de = all_declare[idx]
    sz = all_size[idx]
    name = all_name[idx]
    print(de)
    print('//----ADPCM-%s-----//bytes=%d (%.2fKB)\n' % (name, sz, sz/1024.0))

  print('const adpcm_progm_t* SNDEFFECTS[] = {')
  for idx in range(len(all_name)):
    name = all_name[idx]
    print('  (const adpcm_progm_t*)adpcm_%s,' % name)
  print('  NULL')
  print('};\n\n')
  print('#endif //__sndeffects_h__ bytes = %d (%.2fKB)' % (total_bytes, total_bytes/1024))

if __name__ == "__main__":
  # declare, name, length = encode_file('./one.wav')
  process_snd('data/snd/')
  # print(declare)
  # print(name)
  # print(length)





