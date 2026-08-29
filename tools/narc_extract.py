#!/usr/bin/env python3
"""Minimal Nitro NARC unpacker for extracted HeartGold resources."""
from __future__ import annotations
import argparse, struct
from pathlib import Path

def unpack_narc(path: Path, out: Path):
    b=path.read_bytes()
    if b[:4] != b'NARC': raise ValueError('not a NARC')
    block_count=struct.unpack_from('<H',b,0x0E)[0]; p=0x10; fat=None; img=None
    for _ in range(block_count):
        sig=b[p:p+4]; size=struct.unpack_from('<I',b,p+4)[0]
        if sig==b'BTAF': fat=(p,size)
        elif sig==b'GMIF': img=(p,size)
        p+=size
    if not fat or not img: raise ValueError('NARC missing BTAF or GMIF')
    fp,_=fat; ip,_=img; count=struct.unpack_from('<H',b,fp+8)[0]; data0=ip+8
    out.mkdir(parents=True,exist_ok=True)
    for i in range(count):
        s,e=struct.unpack_from('<II',b,fp+12+i*8)
        (out/f'{i:05d}.bin').write_bytes(b[data0+s:data0+e])
    return count

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('narc',type=Path); ap.add_argument('out',type=Path); a=ap.parse_args()
    print('entries:',unpack_narc(a.narc,a.out))
if __name__=='__main__': main()
