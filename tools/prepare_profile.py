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
# These inputs were independently compared instruction-for-instruction. The
# offsets below are build-time extraction coordinates, never runtime addresses.
profiles = {
    'f2245d1c9d06e02c36624942483913f5120c0d40777fc1bb8703c6f4bda823e4':
        [0x86210,0xd0880,0xcd00,0x41af0,0x41c20,0x9e00,0x15f6e8,0x1177d0,0x150090,0x1501a0,0x159880,0x159c80,0x11fb60,0x1450b,0x81550,0x88c01],
    '6f8844eb7f0380f30a3db2fc3c435e1145f5c450bdd0999133cc75c516ec3c3b':
        [0x86220,0xd0890,0xcd00,0x41b00,0x41c30,0x9e00,0x15f6f8,0x1177e0,0x1500a0,0x1501b0,0x159890,0x159c90,0x11fb70,0x1450b,0x81560,0x88c11],
    'bda769e226d71a43335c105fd6f72ed19af0a3d815a079b367356de0731a0d9a':
        [0x86020,0xd05c0,0xcbe0,0x41900,0x41a30,0x9ce0,0x15e0c8,0x1173c0,0x14ea90,0x14eba0,0x158280,0x158680,0x11f750,0x143ab,0x81360,0x88a11],
}
coordinates = profiles.get(hashlib.sha256(CLIENT.read_bytes()).hexdigest())
if coordinates is None:
    p.error('Unverified extraction source; no contract generated.')
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
sizes=[0x6ee,0x2e,0x11f,0x130,0x30c,0x13,0x6c,0x70f,0x110,0x6d0,0x30,0x15d,0x85,0x4b,0x37,0x2a]
ranges=list(zip(coordinates,sizes))
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
for i,(rva,code,reloc) in ((i,chunks[i]) for i in list(range(6))+list(range(13,16))):out.append(f'{{0x{rva:x},{len(code)},bytes{i},reloc{i},{len(reloc)}}},')
out+=['};','inline constexpr Range cursorRanges[] = {']
for i,(rva,code,reloc) in enumerate(chunks[6:13],6):out.append(f'{{0x{rva:x},{len(code)},bytes{i},reloc{i},{len(reloc)}}},')
out+=['};','}']
(ROOT/'src'/'client_profile.h').write_text('\n'.join(out)+'\n')
print("Generated src/client_profile.h locally for the supported client.")

# Keep instruction/layout bytes exact; only address operands may move.
import re
out=['// Generated local game-contract data. Do not publish.','#pragma once','#include <cstdint>',
     'namespace native_discovery {','struct Pattern { const unsigned char* bytes;const unsigned char* mask;unsigned size,anchor,length; };']
patterns=[]
for i,(rva,code,reloc) in enumerate(chunks):
    mask=bytearray(b'\xff'*len(code))
    for instruction in md.disasm(code,base+rva):
        start=instruction.address-base-rva
        for offset,size in ((instruction.disp_offset,instruction.disp_size),(instruction.imm_offset,instruction.imm_size)):
            if size!=4:continue
            value=int.from_bytes(instruction.bytes[offset:offset+4],'little')
            branch=instruction.group(capstone.CS_GRP_CALL) or instruction.group(capstone.CS_GRP_JUMP)
            external=branch and not base+rva<=instruction.operands[0].imm<base+rva+len(code)
            if base<=value<base+len(image) or external:mask[start+offset:start+offset+4]=b'\0'*4
    skip=7 if i==2 else 5 if i==7 else 0
    mask[:skip]=b'\0'*skip
    anchor,length=max(((m.start(),len(m.group())) for m in re.finditer(b'\xff+',mask)),key=lambda x:x[1])
    for name,data in [('code',code),('mask',mask)]:
        out.append(f'inline constexpr unsigned char {name}{i}[]={{'+','.join(f'0x{x:02x}' for x in data)+'};')
    patterns.append(f'{{code{i},mask{i},{len(code)},{anchor},{length}}},')
out+=['inline constexpr Pattern patterns[]={']+patterns+['};','}']
(ROOT/'src/native_signatures.h').write_text('\n'.join(out)+'\n')
print('Generated src/native_signatures.h for runtime discovery.')
