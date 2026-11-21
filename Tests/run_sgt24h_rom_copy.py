#!/usr/bin/env python3
"""Static audit of the installed SGT ROM copier. No emulator or ROM output files."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import zipfile
import zlib
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('rom',type=Path);a=p.parse_args()
with zipfile.ZipFile(a.rom) as z:
    lo=z.read('epr-19155.15');hi=z.read('epr-19156.16')
assert len(lo)==len(hi)==0x80000
assert zlib.crc32(lo)==0x593952fd and zlib.crc32(hi)==0xa91fc4ee
rom=bytearray(0x100000)
rom[0::4]=lo[0::2];rom[1::4]=lo[1::2];rom[2::4]=hi[0::2];rom[3::4]=hi[1::2]
assert zlib.crc32(rom)==0x22d76fe3
# Keep only the copier code for disassembly; no extracted ROM is persisted.
code=rom[0xbb550:0xbb740]
source=r'''
#include "CPU/I960/I960Disasm.h"
#include <cstdio>
#include <cstdint>
#include <cstdlib>
int main(int argc,char**argv){unsigned base=argc>1?std::strtoul(argv[1],nullptr,0):0xbb550;uint32_t a[124];if(fread(a,4,124,stdin)!=124)return 2;
for(unsigned i=0;i<124;){unsigned n=4;
auto s=CI960Disassembler::Disassemble(base+i*4,a[i],i+1<124?a[i+1]:0,&n);
printf("%s\n",s.c_str());i+=n/4;}}
'''
with tempfile.TemporaryDirectory(prefix='sgt-static-copy-') as d:
    d=Path(d);(d/'audit.cpp').write_text(source)
    subprocess.run(['g++','-std=c++17','-ISrc',str(d/'audit.cpp'),'Src/CPU/I960/I960Disasm.cpp','-o',str(d/'audit')],cwd=ROOT,check=True)
    lines=subprocess.check_output([str(d/'audit')],input=code).decode().splitlines()
    def decode_at(offset):
        # Preserve the real PC so branch targets can also be asserted.
        raw=subprocess.check_output([str(d/'audit'),hex(offset)],input=rom[offset:offset+len(code)])
        return [' '.join(line.split()) for line in raw.decode().splitlines()]
    assert decode_at(0xbad00)[:6]==[
        'ld 0x7e0(g4),g13','st g13,0x2c(g6)',
        'ld 0x7e4(g4),g13','st g13,0x30(g6)',
        'ld 0x7e8(g4),g13','st g13,0x34(g6)']
    assert decode_at(0xbaaa0)[:6]==[
        'ld 0x2c(g5),r4','st r4,0xf98(g3)',
        'ld 0x30(g5),r4','st r4,0xf9c(g3)',
        'ld 0x34(g5),r4','st r4,0xfa0(g3)']
    assert decode_at(0x45464)[:4]==[
        'subr g1,g0,g0','ld 0xfa0(g5),g5',
        'subr g2,g4,g4','subr g3,g7,g7']
    print('PASS actual-ROM first spatial triple: serializer, deserializer and floating-point delta consumers')
    assert decode_at(0xbb0ec)[:10]==[
        'ld 0x7e0(g5),g2','st g2,0x19c(g6)',
        'ld 0x7e4(g5),g4','lda 0x42c80000,g7',
        'mulr g4,g7,g4','cvtzri g4,g4','stos g4,0x1a0(g6)',
        'ld 0x7e8(g5),g2','st g2,0x1a4(g6)','ldos 0x804(g5),g4']
    assert decode_at(0xbaf50)[:16]==[
        'shlo 5,g1,g5','addo g0,g5,g5','ldis 0x1a0(g5),g4',
        'lda 0x1728,g3','cvtir g4,g6','mulo g1,g3,g4',
        'lda 0x517c50(g4),g2','ld 0x19c(g5),g3','st g3,0xf98(g2)',
        'ldis 0x1b4(g5),g4','cvtir g4,g7','lda 0x42c80000,g4',
        'divr g4,g6,g6','divr g4,g7,g7','ldis 0x1a8(g5),g4',
        'st g6,0xf9c(g2)']
    print('PASS actual-ROM compact stride and float/signed-centiunit spatial conversions')
    assert decode_at(0xba8f8)[:9]==[
        'ld 0x51095c,g0','ld 0x510dd0,g4','cmpibne g0,g4,0xba92c',
        'mulo g0,r8,g0','ld 0x5346c8,g4','mov r6,g1','mov r4,g2',
        'lda 0x511230(g0),g0','callx (g4)']
    assert decode_at(0xba954)[:7]==[
        'ld 0x510dd0,g0','mulo r8,g0,g0','ld 0x5346c4,g4',
        'mov r6,g1','mov r4,g2','lda 0x512420(g0),g0','callx (g4)']
    assert decode_at(0xba98c)[:8]==[
        'ld 0x51095c,g0','ld 0x510dd0,g4','cmpibne g0,g4,0xba9bc',
        'mulo g0,r8,g0','ld 0x5346d0,g4','mov r4,g1',
        'lda 0x511230(g0),g0','callx (g4)']
    assert decode_at(0xba9bc)[:6]==[
        'ld 0x510dd0,g0','mulo r8,g0,g0','ld 0x5346cc,g4',
        'mov r4,g1','lda 0x512420(g0),g0','callx (g4)']
    assert decode_at(0xba8d0)[:2]==['ld (r7),g4','cmpibe 2,g4,0xba98c']
    print('PASS actual-ROM full/compact selection, local-only serialization gates and selected-cabinet RX source')
    assert decode_at(0x28444)[:4]==[
        'ld 0x510dd0,g5','ld 0x51095c,g4',
        'ld (r8),r6','cmpibne g5,g4,0x286c0']
    assert decode_at(0x284a0)[:3]==[
        'lda 0x518408(r4),r4','mov r4,g0','call 0x0002ee00']
    assert decode_at(0x2ee00)[:2]==['ld 0x278(g0),g1','mov g0,r5']
    assert decode_at(0x2eed0)[:6]==[
        'ld 0x8(r4),g2','st g2,0x28(r5)',
        'ld 0xc(r4),g3','st g3,0x2c(r5)',
        'ld 0x10(r4),g2','st g2,0x30(r5)']
    assert 0x518408+0x28==0x517c50+0x7e0
    assert 0x518408+0x2c==0x517c50+0x7e4
    assert 0x518408+0x30==0x517c50+0x7e8
    assert decode_at(0x286c0)[:4]==[
        'ld 0x510f40,g4','addo r8,4,r8','lda 0x1(r7),r7',
        'cmpibl r7,g4,0x28444']
    print('PASS actual-ROM selected-cabinet update-loop gate and reachable spatial-store routine')
    assert decode_at(0x56b04)[:5]==[
        'lda 0x217c00,g3','lda 0x2cfd0,g1','lda 0x217c00,g0',
        'mov 0,g2','st g3,0x52ef3c']
    assert decode_at(0x56bc8)[:5]==[
        'lda 0x216400,g3','lda 0x2ba30,g1','lda 0x216400,g0',
        'mov 0,g2','st g3,0x52ef40']
    assert decode_at(0x2e51c)[:11]==[
        'ld 0x28(r5),g4','ld 0x5110c0,g5','addr g4,g5,g4','st g4,0x28(r5)',
        'ld 0x2c(r5),g4','ld 0x5110c4,g5','addr g4,g5,g4','st g4,0x2c(r5)',
        'ld 0x30(r5),g5','ld 0x5110c8,g4','addr g5,g4,g5']
    assert decode_at(0x2e56c)[:7]==[
        'ld 0x50(r5),g4','ld 0x5c(r5),g6','lda 0xffff,r13',
        'clrbit 15,r13,r14','subo g6,g4,g5','and r13,g5,g4',
        'cmpible g4,r14,0x2e5a4']
    assert decode_at(0xbad20)[:2]==['ld 0x808(g4),g5','st g5,0x3c(g6)']
    assert decode_at(0xbb11c)[:2]==['ldos 0x808(g5),g4','stos g4,0x1aa(g6)']
    assert 0x518408+0x50==0x517c50+0x808
    print('PASS actual-ROM indirect update targets, spatial integration and modular orientation field')
    assert decode_at(0x56604)[:5]==[
        'lda 0x21bc80,g3','lda 0x21180,g1','lda 0x21bc80,g0',
        'mov 0,g2','st g3,0x52ef08']
    assert decode_at(0x21298)[:4]==[
        'st g4,0x5110c8','st g5,0x5110c4','st g0,0x5110c0','ret']
    assert decode_at(0x2e338)[:7]==[
        'ld 0x4c(r5),g3','notbit 31,g5,g0','ld 0x5c(r5),g4',
        'ld 0x54(r5),g5','ld 0x52ef08,g6','mov 0,g1','callx (g6)']
    assert decode_at(0x2e358)[:3]==[
        'ld 0x28(r5),g4','ld 0x5110c0,g5','addr g4,g5,g4']
    assert decode_at(0x2e3b0)[:15]==[
        'ld 0xa0(r5),g2','ld 0x58(r5),g3','movrl r14,fp1',
        'movr fp1,g5','addr g6,g4,g6','ld 0xa4(r5),g4',
        'st g5,0xac(r5)','ld 0x60(r5),g5','st g6,0x30(r5)','ld 0x52ef08,g6',
        'mov 0,g0','mov 0,g1','callx (g6)',
        'ld 0xa0(r5),r15','ld 0xa4(r5),g4']
    print('PASS actual-ROM shared transform scratch producer and successive actor consumers')
    assert decode_at(0x25f60)[:9]==[
        'lda 0x518bc0,r6','lda 0x518408,r4','lda 0x517c50,r5',
        'mov r5,g0','mov r4,g1','call 0x00016040',
        'mov r4,g0','mov r6,g1','call 0x00016040']
    assert decode_at(0x164f4)[:6]==[
        'ld 0x228(g0),g4','st g4,0x228(g1)',
        'ld 0x22c(g0),g4','st g4,0x22c(g1)',
        'ld 0x230(g0),g4','st g4,0x230(g1)']
    print('PASS actual-ROM three-state actor initialization and copied control-field offsets')
    assert decode_at(0x28404)[:6]==[
        'ld 0x510e60,g1','lda 0x1728,g13','mulo g1,g13,g1',
        'lda 0x517c50(g1),g0','lda 0x518bc0(g1),g1','call 0x0002b8f0']
    assert decode_at(0x2b91c)[:4]==[
        'ld 0x510eb4,g4','lda 0xbc(g0),g6','ld (g6),g5','st g4,0xc0(g0)']
    assert decode_at(0x2b938)[0]=='ld 0x510e9c,g4'
    assert decode_at(0x2b984)[:2]==['st g5,(g6)','st g5,0xbc(g1)']
    assert decode_at(0x2b9a4)[:2]==['st g0,0xe4(r4)','st g0,0xe4(r5)']
    assert decode_at(0x5f114)[0]=='st g2,0x510e9c'
    assert decode_at(0x5f168)[0]=='ld 0xf0500[g0*4],g4'
    assert decode_at(0x5f188)[0]=='st g4,0x510eb4'
    print('PASS actual-ROM processed-input stores and pre-authority-gate actor input application')
    assert decode_at(0xbaa40)[:12]==[
        'lda 0x1728,g5','mulo g1,g5,g1','lda 0x517c50(g1),g1',
        'ldob 0xc0(g1),g4','stob g4,0x28(g0)',
        'ldos 0xbc(g1),g4','stos g4,0x26(g0)',
        'ldob 0xe4(g1),g4','stob g4,0x29(g0)',
        'ldob 0xec(g1),g4','stob g4,0x2a(g0)','ret']
    assert decode_at(0xbaa00)[:12]==[
        'lda 0x1728,g6','mulo g1,g6,g1','ldob 0x28(g0),g4',
        'ldis 0x26(g0),g5','lda 0x517c50(g1),g1',
        'st g4,0x878(g1)','st g5,0x874(g1)',
        'ldob 0x29(g0),g4','st g4,0x89c(g1)',
        'ldob 0x2a(g0),g4','st g4,0x8a4(g1)','ret']
    assert decode_at(0xba8d8)[:6]==[
        'cmpibne 0,g4,0xba8f8','ld 0x51095c,g0','mulo r8,g0,g0',
        'mov r4,g1','lda 0x511230(g0),g0','call 0x000baa40']
    assert decode_at(0xba92c)[:7]==[
        'ld 0x51095c,g5','ld 0x510dd0,g4','cmpibne g5,g4,0xba954',
        'mulo r6,r8,g0','mov r4,g1','lda 0x512420(g0),g0','call 0x000baa00']
    assert decode_at(0x287f4)[0]=='call 0x000ba830'
    assert all(0x7b8+a==b for a,b in [(0xbc,0x874),(0xc0,0x878),(0xe4,0x89c),(0xec,0x8a4)])
    print('PASS actual-ROM input wire codec, signedness and selected-cabinet RX-to-simulation handoff')
    assert decode_at(0x2ef2c)[:11]==[
        'ld 0x290(g0),g4','cmpibne 0,g4,0x2ef48','call 0x0002b0f0',
        'ld 0x52ef40,g4','mov r5,g0','callx (g4)',
        'ld 0x52ef3c,g4','mov r5,g0','callx (g4)','ret','? 00000000']
    assert decode_at(0x2b0f0)[:10]==[
        'addo g0,4,g7','mov 0,g1','ld (g7),g4','lda (g4)[g4*4],g5',
        'subo g1,3,g4','lda 0x52f350[g5*4],g5','ld (g5)[g4*4],g6',
        'subo g1,4,g4','st g6,(g5)[g4*4]','ld (g7),g4']
    assert decode_at(0x2b160)[:10]==[
        'ld 0xbc(g0),g5','lda 0xd0,g2','cmpible g5,g2,0x2b170',
        'lda 0xd0,g5','addo 31,16,g2','cmpibg g5,g2,0x2b17c',
        'addo 31,17,g5','ld 0x4(g0),g4','lda (g4)[g4*4],g4',
        'st g5,0x52f350[g4*4]']
    assert decode_at(0x2b1bc)[:10]==[
        'ld 0xbc(g0),g4','setbit 7,0,g2','subo g4,g2,g4',
        'lda 0x445,g2','mulo g4,g2,g4','lda 0x50,g2','divi g2,g4,g5',
        'ld 0x2aa70[g6*4],g4','lda 0x22c(g0),g0','mulo g5,g4,g4']
    assert decode_at(0x2b1f8)[:8]==[
        'cmpible g5,g2,0x2b200','shlo 8,3,g5','lda 0xfffffd00,g2',
        'cmpibge g5,g2,0x2b210','mov g2,g5','addo g6,g5,g6','st g6,(g0)','ret']
    for pc,ins in [(0x2ba4c,'ld 0xc0(g0),g4'),(0x2ba7c,'ld 0xec(g0),g4'),
                   (0x2bb30,'ld 0xe4(g0),r4'),(0x2bbb8,'st g6,0xb0(g0)'),
                   (0x2bee4,'st g4,0x24c(r5)'),(0x2d178,'ldos 0x22c(g0),g4')]:
        assert decode_at(pc)[0]==ins
    print('PASS actual-ROM simulation input consumers, shared actor-ID history and rate-limited control state')
    assert decode_at(0x27db0)[:2]==['lda 0x518bc0(g0),g0','call 0x000053b0']
    assert decode_at(0x27dd0)[:4]==[
        'ld 0x518e3c(g0),g4','ld 0xec7e0[g4*4],g4',
        'lda 0x518bc0(g0),g0','callx (g4)']
    assert decode_at(0x280fc)[:5]==[
        'ld 0x52ef30,g4','lda 0x20(g12),g12','st g12,0x517a9c',
        'lda 0x518bc0(r4),g0','callx (g4)']
    assert decode_at(0x56910)[:7]==[
        'ld 0x510948,g4','cmpi g4,3','lda 0x21a400,g3',
        'be 0x00056930','lda 0x21dd0,g3','st g3,0x52ef30','lda 0x21dd0,g1']
    assert decode_at(0x21df0)[:9]==[
        'ld 0x28(g0),g6','ld 0x5110d8,g1','ld 0x30(g0),g4',
        'ld 0x5110e0,g5','subr g1,g6,g6','subr g5,g4,g4',
        'mulr g6,g6,g6','mulr g4,g4,g4','addr g6,g4,g6']
    assert decode_at(0x21e80)[:5]==[
        'ld 0x4(g0),g4','lda 0x1728,g12','mulo g4,g12,g4',
        'mov 1,g13','st g13,0x5186a4(g4)']
    assert decode_at(0x287a0)[:5]==[
        'ld 0x510e60,g0','lda 0x1728,g12','mulo g0,g12,g0',
        'lda 0x518bc0(g0),g0','call 0x00023b20']
    print('PASS actual-ROM received-state consumers and cross-state distance-path write')
    assert decode_at(0xbace0)[:6]==[
        'lda 0x5c,g13','mulo g1,g13,g6','lda 0x1728,g13',
        'mulo g2,g13,g4','addo g6,g0,g6','lda 0x517c50(g4),g4']
    assert decode_at(0xbaa80)[:6]==[
        'lda 0x1728,r4','mulo g2,r4,g4','lda 0x5c,r4',
        'mulo g1,r4,g5','lda 0x517c50(g4),g3','addo g5,g0,g5']
    assert decode_at(0xbaff4)[:5]==[
        'bbc 4,g7,0xbb00c','mov 10,g3','st g3,0x105c(g2)',
        'st g3,0x8a4(g2)','b 0x000bb02c']
    assert decode_at(0xbb00c)[:5]==[
        'lda 0x1728,g3','mulo g1,g3,g4','lda 0x517c50(g4),g4',
        'st g14,0x105c(g4)','st g14,0x8a4(g4)']
    assert 0x2c+4*0x5c==0x19c
    assert 0xf70+0xec==0x105c and 0x7b8+0xec==0x8a4
    print('PASS actual-ROM full slot indexing and compact RX cross-state control writes')
    assert decode_at(0x5ef60)[:7]==[
        'stos g14,0x1c0001e','ldos 0x1c0001e,g4','stob g4,0x510a10',
        'ldos 0x1c0001e,g4','stob g4,0x510a11',
        'ldos 0x1c0001e,g4','stob g4,0x510a12']
    assert decode_at(0x5f038)[0]=='ldob 0x510a10,g2'
    assert decode_at(0x5f100)[0]=='ldob 0x510a11,g4'
    assert decode_at(0x5f14c)[0]=='ldob 0x510a12,g1'
    assert decode_at(0x5f244)[0]=='ld 0xf0900[g0*4],g4'
    assert decode_at(0x5f270)[0]=='st g4,0x510ec8'
    assert decode_at(0x2ba00)[:2]==['ld 0x510ec8,g4','st g4,0xec(r4)']
    assert int.from_bytes(rom[0xed610:0xed614],'little')==0x2b260
    assert int.from_bytes(rom[0xed614:0xed618],'little')==0x2b6f0
    assert decode_at(0x2b260)[:2]==['ld 0xe4(g0),g6','st g6,0xe8(g0)']
    assert decode_at(0x2b6f0)[:2]==['ld 0xe4(g0),g6','st g6,0xe8(g0)']
    print('PASS actual-ROM analog sampling, processed brake path and input-handler dispatch table')
    assert decode_at(0x24ecc)[:6]==[
        'ldob 0x512423(g7),g5','ldob 0x512423(g4),g4',
        'cmpibne g5,g4,0x24f18','ldob 0x512422(g7),g4',
        'cmpibe 3,g4,0x24ef0','cmpibne 8,g4,0x24f18']
    assert decode_at(0x25130)[:3]==[
        'st r15,0x517c54(r5)','mov 2,g9','st g9,(r6)']
    assert decode_at(0x2518c)[:10]==[
        'lda 0x512420(g5),g6','ldob 0x3(g6),g5','ldob 0x512423(g4),g4',
        'cmpibne g5,g4,0x251e8','ldob 0x2(g6),g5','cmpibe 3,g5,0x251b0',
        'cmpibne 8,g5,0x251e8','ld 0x51095c,g4','cmpibne r4,g4,0x251cc',
        'b 0x000251e4']
    assert decode_at(0x251cc)[:3]==['mov 1,g9','st g9,(r6)','b 0x000251e8']
    assert decode_at(0x251e4)[0]=='st g14,(r6)'
    assert decode_at(0x25e70)[:6]==[
        'ld (r5),g4','cmpibne 0,g4,0x25ec0','st g14,0x5110e4',
        'ld 0x50(r5),g4','st g14,0x5110ec','st r15,0x510e60']
    # Runtime uses 3/4, not the initializer's 3/8; preserve phase distinction.
    assert decode_at(0xba890)[:2]==['cmpibe 3,g4,0xba898','cmpibne 4,g4,0xba8a8']
    print('PASS actual-ROM linked actor role assignments, local actor selection and phase-specific membership')
    assert decode_at(0xba5f0)[:4]==[
        'cmpi g5,2','stob g7,0x24(g1)','stob g6,0x25(g1)','bne 0x000ba6d4']
    assert decode_at(0xba618)[:5]==[
        'ld 0x510dd4,g13','mov 0,g7','ld 0x510dcc,r4','mov 0,g5','ld 0x510dd0,r5']
    assert decode_at(0xba638)[:13]==[
        'ldob (g2),g4','cmpibne 2,g4,0xba66c','ldob (g3),g4',
        'bbc 0,g4,0xba66c','cmpibne 0,g7,0xba65c',
        'mov g5,r5','mov g5,r4','mov g5,g0','mov 1,g7',
        'addo g6,1,g6','stob g0,0x3(g1)','mov g6,g13','stob g6,0x4(g1)']
    assert decode_at(0xba66c)[:14]==[
        'ldob 0x47c(g2),g4','addo g5,1,g5','cmpibne 2,g4,0xba6a4',
        'ldob 0x47c(g3),g4','bbc 0,g4,0xba6a4','cmpibne 0,g7,0xba694',
        'mov g5,r5','mov g5,r4','mov g5,g0','mov 1,g7',
        'addo g6,1,g6','stob g0,0x3(g1)','mov g6,g13','stob g6,0x4(g1)']
    assert decode_at(0xba6a4)[:9]==[
        'addo g5,1,g5','cmpi g5,3','lda 0x8f8,g4',
        'addo g3,g4,g3','addo g2,g4,g2','ble 0x000ba638',
        'st r5,0x510dd0','st r4,0x510dcc','st g13,0x510dd4']
    print('PASS actual-ROM state-2 four-slot authority election and zero-match retention')
    assert decode_at(0x21f18)[:9]==[
        'ld 0x59f40,g1','ld 0x28(g0),g5','ld 0x2c(g0),g6','ld 0x30(g0),g7',
        'lda 0xd0d(g12),g13','st g13,0x880120',
        'st g5,(g1)','st g6,(g1)','st g7,(g1)']
    assert decode_at(0x56938)[:8]==[
        'lda 0x21dd0,g1','lda 0x21a400,g0','mov 0,g2',
        'addo 31,9,g5','addo 31,17,g6','lda 0x2ff,g3',
        'addo 31,5,g13','addo 31,13,r4']
    assert decode_at(0x569bc)[:6]==[
        'addo g1,g6,g1','lda 0xc(g2),g2','cmpi g2,g3',
        'st g7,(g0)[r4]','addo g0,g6,g0','ble 0x00056960']
    assert 0x21a400+(0x21f20-0x21dd0)==0x21a550
    assert 0x21a400+(0x21f24-0x21dd0)==0x21a554
    assert 0x21a400+(0x21f28-0x21dd0)==0x21a558
    assert 0x21f28-0x21dd0<0xc00
    print('PASS actual-ROM candidate geometry position loads, FIFO writes and RAM relocation')
    assert decode_at(0x21f44)[0]=='ld (g0),g5'
    assert decode_at(0x21ff0)[0]=='ld 0x59f44,g6'
    assert decode_at(0x21ecc)[:2]==['lda 0x72c90,g12','st g12,0x52ef10']
    assert decode_at(0x21fc4)[:3]==[
        'ld 0x52ef10,g4','lda 0x2a0,g0','callx (g4)']
    assert decode_at(0x72c90)[:19]==[
        'ld 0x59f40,g4','lda 0x1111,g6','st g6,0x880110',
        'lda 0xb0b,g6','st g6,0x8000b0','ld 0x59f48,g5',
        'ldq (g4),r4','stq r4,(g5)','ldq (g4),r4','stq r4,(g5)',
        'ldq (g4),r4','stq r4,(g5)','lda 0x101,g6','st g6,0x800010',
        'lda 0x280f480[g0*16],g0','ldq (g0),g0','stq g0,(g5)','ret',
        'ldob 0x1b7,pfp']
    assert decode_at(0x22044)[:3]==[
        'cmpi r8,2','st g4,(g6)','bg 0x000224c4']
    assert decode_at(0x221b0)[:2]==['ld (g5),g7','ld 0x4(g5),g6']
    assert decode_at(0x224dc)[0]=='ld 0x59f40,g7'
    print('PASS actual-ROM geometry register overwrite sites and model-emission callee')
    assert decode_at(0xba968)[:4]==[
        'mov r6,g1','mov r4,g2','lda 0x512420(g0),g0','callx (g4)']
    assert decode_at(0xbacc4)[:4]==[
        'ldob 0x6d(g5),g4','st g1,0xf6c(g2)','st g4,0x1720(g2)','ret']
    assert decode_at(0x283a8)[:6]==[
        'ld (g7),g4','cmpibe 2,g4,0x283d0',
        'ld 0x518bbc(g6),g4','ld 0x518664(g6),g5',
        'st g4,0x519374(g6)','st g5,0x518e1c(g6)']
    assert decode_at(0x55934)[:8]==[
        'ld 0x7b4(g0),g5','lda 0x47c,g3','mulo g5,g3,g4',
        'ldob 0x512422(g4),g4','cmpibe 4,g4,0x55964',
        'shlo 12,g5,g4','lda 0x204(g4),g4','st g4,0x510960']
    assert 0x517c50+0xf6c==0x518bbc
    assert 0x517c50+0xf70+0x7b4==0x519374
    print('PASS actual-ROM full-slot binding metadata, delayed propagation and cabinet-index consumer')
    assert decode_at(0xba888)[:6]==[
        'ldob 0x512422(g6),g4','cmpibe 3,g4,0xba898','cmpibne 4,g4,0xba8a8',
        'ldob 0x3(g0),g5','ldob 0x512423(g6),g4','cmpibe g5,g4,0xba980']
    assert decode_at(0xba8a8)[:5]==[
        'addo g7,1,g7','cmpi g7,g1','lda 0x1(r5),r5',
        'lda 0x47c(g6),g6','bl 0x000ba888']
    assert decode_at(0xba980)[:3]==['mov g7,r6','lda 0x1(r5),r5','b 0x000ba8bc']
    print('PASS actual-ROM full-slot scan eligibility, cursor advance and fresh-match assignment')
    assert decode_at(0xb9634)[:4]==[
        'lda 0x47c,g6','mulo r5,g6,g4','lda 0x1a12380,r6','lda 0x512420(g4),r4']
    assert decode_at(0xb964c)[:8]==[
        'ld 0x510dc8,g4','cmpibl r5,g4,0xb9664','lda 0x512420,r4','mov 0,r5',
        'ld 0x5346d4,g4','lda 0x0(r6),g0','mov r4,g1','callx (g4)']
    assert decode_at(0xb9678)[:6]==[
        'ld 0x510dc8,g4','lda 0x47c(r4),r4','addo r5,1,r5',
        'lda 0x380(r6),r6','addo r7,1,r7','cmpibl r7,g4,0xb964c']
    print('PASS actual-ROM comm-RX to cabinet-record copy pointers, wrap and distinct strides')
    assert decode_at(0xb9580)[:4]==[
        'ld 0x510958,g4','lda 0x21be00,g6','st g6,0x5346d4','cmpibne 1,g4,0xb9614']
    assert decode_at(0xbb720)[:3]==[
        'ldq 0x370(g9),g4','stq g0,0x360(g10)','stq g4,0x370(g11)']
    assert decode_at(0xb9674)[:2]==['callx (g4)','ld 0x510dc8,g4']
    print('PASS actual-ROM RX-copy caller and final-load context (not completion or freshness)')
    assert decode_at(0xbac14)[:3]==['lda 0x1728,r4','mulo g2,r4,g2','shri 25,g13,g4']
    assert decode_at(0xbac30)[0]=='lda 0x517c50(g2),g2'
    assert decode_at(0xbacc8)[:3]==['st g1,0xf6c(g2)','st g4,0x1720(g2)','ret']
    assert 0x216000+(0xbaccc-0xbaa80)==0x21624c
    assert decode_at(0xba978)[:2]==['callx (g4)','b 0x000ba9e0']
    print('PASS actual-ROM full decode observation PC, caller and final actor-base register')






















lines=[' '.join(x.split()) for x in lines]
assert lines[:7]==['stq g0,(sp)','stq g4,0x10(sp)','stq g8,0x20(sp)','lda 0x0(g0),g8','lda 0x0(g0),g9','lda 0x0(g1),g10','lda 0x0(g1),g11']
expected=[]
for offset in range(0,0x380,0x20):
    expected += [f'ldq 0x{offset:x}(g8),g0',f'ldq 0x{offset+16:x}(g9),g4',f'stq g0,0x{offset:x}(g10)',f'stq g4,0x{offset+16:x}(g11)']
assert lines[7:119]==expected
assert lines[119:123]==['ldq 0x20(sp),g8','ldq 0x10(sp),g4','ldq (sp),g0','ret'],lines[119:]
print('PASS verified SGT ROM: 56 matched 16-byte load/store chunks copy exactly 0x380 bytes; registers restored; no codec or field conversion')
