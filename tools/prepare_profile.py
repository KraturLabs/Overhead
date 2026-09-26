"""Generate local compatibility data from the supported user-owned client.
Reads but never loads, executes or changes FFXiMain.dll. No game bytes are shipped.
"""
import argparse
import hashlib
import struct
from pathlib import Path
import capstone
import pefile

ROOT = Path(__file__).resolve().parents[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('client', type=Path, help='Path to the supported FFXiMain.dll')
a = p.parse_args()
CLIENT = a.client
expected = 'f2245d1c9d06e02c36624942483913f5120c0d40777fc1bb8703c6f4bda823e4'
if hashlib.sha256(CLIENT.read_bytes()).hexdigest() != expected:
    p.error('Unsupported client checksum; no profile generated. Addresses require independent verification.')
pe = pefile.PE(str(CLIENT))
base = pe.OPTIONAL_HEADER.ImageBase
image = bytearray(pe.get_memory_mapped_image())
text_section = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
if not text_section.SizeOfRawData:
    packed = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'POL1').get_data()
    output = bytearray()
    pos = 0
    done = False
    while not done:
        flags = packed[pos]
        pos += 1
        for bit_index in range(7, -1, -1):
            if flags & (1 << bit_index):
                output.append(packed[pos])
                pos += 1
            else:
                first, second = packed[pos:pos + 2]
                pos += 2
                distance = ((first & 15) << 8) | second
                if distance == 0:
                    done = True
                    break
                assert distance <= len(output)
                for _ in range((first >> 4) + 3):
                    output.append(output[-distance])
            assert len(output) <= text_section.Misc_VirtualSize
    assert len(output) == text_section.Misc_VirtualSize
    start = text_section.VirtualAddress
    image[start:start + len(output)] = output

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
ranges=[(0x86210,0x6ee),(0xd0880,0x2e),(0xcd00,0x11f),(0x41af0,0x130),(0x41c20,0x30c),(0x9e00,0x13)]
chunks=[]
for rva,size in ranges:
    code=bytes(image[rva:rva+size])
    reloc=[]
    for instruction in md.disasm(code,base+rva):
        for off,n in ((instruction.disp_offset,instruction.disp_size),(instruction.imm_offset,instruction.imm_size)):
            if n==4:
                at=instruction.address-base-rva+off
                if base<=struct.unpack_from('<I',code,at)[0]<base+len(image):reloc.append(at)
    chunks.append((rva,code,reloc))
out=['// Generated from the installed FFXiMain.dll; local compatibility evidence.','#pragma once','#include <cstdint>','namespace client_profile {','struct Range { std::uint32_t rva, size; const unsigned char* bytes; const unsigned short* relocations; unsigned relocationCount; };']
for i,(rva,code,reloc) in enumerate(chunks):
    out.append(f'inline constexpr unsigned char bytes{i}[] = {{'+','.join(f'0x{b:02x}' for b in code)+'};')
    out.append(f'inline constexpr unsigned short reloc{i}[] = {{'+','.join(map(str,reloc or [0]))+'};')
out.append('inline constexpr Range ranges[] = {')
for i,(rva,code,reloc) in enumerate(chunks):out.append(f'{{0x{rva:x},{len(code)},bytes{i},reloc{i},{len(reloc)}}},')
out+=['};','}']
(ROOT/'src'/'client_profile.h').write_text('\n'.join(out)+'\n')
print("Generated src/client_profile.h locally for the supported client.")
