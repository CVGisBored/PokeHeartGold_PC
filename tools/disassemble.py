#!/usr/bin/env python3
"""Generate ARM and Thumb reference disassemblies for ARM9 and decoded overlays."""
from __future__ import annotations
import argparse, json, shutil, subprocess, tempfile
from pathlib import Path

def disasm(binpath: Path, addr: int, out: Path, triple: str):
    with tempfile.TemporaryDirectory() as td:
        obj=Path(td)/'blob.o'
        subprocess.run(['llvm-objcopy','-I','binary','-O','elf32-littlearm','-B','arm',str(binpath),str(obj)],check=True)
        cmd=['llvm-objdump','-D','-j','.data',f'--triple={triple}',f'--adjust-vma={addr}',str(obj)]
        p=subprocess.run(cmd,check=True,stdout=subprocess.PIPE,text=True)
        out.write_text(p.stdout)

def main():
    ap=argparse.ArgumentParser(); ap.add_argument('extracted',type=Path); ap.add_argument('out',type=Path); args=ap.parse_args()
    if not shutil.which('llvm-objcopy') or not shutil.which('llvm-objdump'):
        raise SystemExit('llvm-objcopy and llvm-objdump are required')
    m=json.loads((args.extracted/'manifest.json').read_text()); args.out.mkdir(parents=True,exist_ok=True)
    h=m['header']
    disasm(args.extracted/'bin/arm9.bin',h['arm9_ram'],args.out/'arm9.arm.s','armv5te-none-eabi')
    disasm(args.extracted/'bin/arm9.bin',h['arm9_ram'],args.out/'arm9.thumb.s','thumbv5te-none-eabi')
    for o in m['arm9_overlays']:
        p=args.extracted/'overlays/arm9'/f"overlay_{o['id']:04d}.bin"
        disasm(p,o['ram_address'],args.out/f"overlay_{o['id']:04d}.arm.s",'armv5te-none-eabi')
        disasm(p,o['ram_address'],args.out/f"overlay_{o['id']:04d}.thumb.s",'thumbv5te-none-eabi')
        print('overlay',o['id'])
if __name__=='__main__': main()
