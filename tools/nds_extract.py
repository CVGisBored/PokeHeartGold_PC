#!/usr/bin/env python3
"""Extract Nintendo DS binaries, NitroFS, overlays, and a machine-readable manifest.

Designed for native-port reverse engineering. It does not emulate the DS.
"""
from __future__ import annotations
import argparse, hashlib, json, struct
from pathlib import Path
from blz import decompress as blz_decompress

def u16(b,o): return struct.unpack_from('<H', b, o)[0]
def u32(b,o): return struct.unpack_from('<I', b, o)[0]

def parse_fnt(b: bytes, off: int, fat_count: int):
    count=u16(b,off+6)
    dirs={}
    for i in range(count):
        p=off+i*8
        dirs[0xF000+i]={'sub':u32(b,p),'first':u16(b,p+4),'parent':u16(b,p+6),'items':[]}
    for did,d in dirs.items():
        p=off+d['sub']; fid=d['first']
        while p < len(b):
            tag=b[p]; p+=1
            if tag==0: break
            is_dir=bool(tag&0x80); n=tag&0x7F
            name=b[p:p+n].decode('ascii','replace'); p+=n
            if is_dir:
                child=u16(b,p); p+=2; d['items'].append(('dir',name,child))
            else:
                if fid < fat_count: d['items'].append(('file',name,fid))
                fid+=1
    paths={}
    def walk(did,prefix=''):
        d=dirs.get(did)
        if not d: return
        for typ,name,val in d['items']:
            path=f'{prefix}/{name}' if prefix else name
            if typ=='file': paths[val]=path
            else: walk(val,path)
    walk(0xF000)
    return paths

def parse_overlays(b, off, size, cpu, paths):
    out=[]
    for p in range(off, off+size, 32):
        vals=struct.unpack_from('<8I',b,p)
        oid,ram,ram_size,bss,s0,s1,file_id,flags=vals
        out.append({
            'cpu':cpu,'id':oid,'ram_address':ram,'ram_size':ram_size,'bss_size':bss,
            'static_init_start':s0,'static_init_end':s1,'file_id':file_id,'flags':flags,
            'compressed':bool(flags & 0x01000000),'compressed_size':flags & 0xFFFFFF,
            'path':paths.get(file_id)
        })
    return out

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('rom',type=Path)
    ap.add_argument('out',type=Path)
    ap.add_argument('--no-nitrofs',action='store_true')
    args=ap.parse_args()
    b=args.rom.read_bytes(); out=args.out
    h={
        'title':b[:12].split(b'\0',1)[0].decode('ascii','replace'),
        'gamecode':b[12:16].decode('ascii','replace'),'maker':b[16:18].decode('ascii','replace'),
        'unit_code':b[18],'revision':b[30],
        'arm9_offset':u32(b,0x20),'arm9_entry':u32(b,0x24),'arm9_ram':u32(b,0x28),'arm9_size':u32(b,0x2C),
        'arm7_offset':u32(b,0x30),'arm7_entry':u32(b,0x34),'arm7_ram':u32(b,0x38),'arm7_size':u32(b,0x3C),
        'fnt_offset':u32(b,0x40),'fnt_size':u32(b,0x44),'fat_offset':u32(b,0x48),'fat_size':u32(b,0x4C),
        'arm9_overlay_offset':u32(b,0x50),'arm9_overlay_size':u32(b,0x54),
        'arm7_overlay_offset':u32(b,0x58),'arm7_overlay_size':u32(b,0x5C),
        'banner_offset':u32(b,0x68),'rom_size':len(b),'sha1':hashlib.sha1(b).hexdigest()
    }
    fat=[(u32(b,p),u32(b,p+4)) for p in range(h['fat_offset'],h['fat_offset']+h['fat_size'],8)]
    paths=parse_fnt(b,h['fnt_offset'],len(fat))
    ov9=parse_overlays(b,h['arm9_overlay_offset'],h['arm9_overlay_size'],'arm9',paths)
    ov7=parse_overlays(b,h['arm7_overlay_offset'],h['arm7_overlay_size'],'arm7',paths) if h['arm7_overlay_size'] else []
    (out/'bin').mkdir(parents=True,exist_ok=True)
    arm9_raw=b[h['arm9_offset']:h['arm9_offset']+h['arm9_size']]
    (out/'bin/arm9.bin').write_bytes(arm9_raw)
    arm9_dec=blz_decompress(arm9_raw)
    (out/'bin/arm9_dec.bin').write_bytes(arm9_dec)
    h['arm9_decoded_size']=len(arm9_dec)
    (out/'bin/arm7.bin').write_bytes(b[h['arm7_offset']:h['arm7_offset']+h['arm7_size']])
    magic_counts={}; total=0
    if not args.no_nitrofs:
        fs=out/'nitrofs'; fs.mkdir(parents=True,exist_ok=True)
    for fid,(s,e) in enumerate(fat):
        data=b[s:e]; total+=len(data)
        magic=data[:4].decode('ascii','replace') if len(data)>=4 else ''
        magic_counts[magic]=magic_counts.get(magic,0)+1
        if not args.no_nitrofs:
            rel=paths.get(fid,f'_unnamed/file_{fid:05d}.bin')
            dst=fs/rel; dst.parent.mkdir(parents=True,exist_ok=True); dst.write_bytes(data)
    for ov in ov9+ov7:
        fid=ov['file_id']; s,e=fat[fid]; raw=b[s:e]
        od=out/'overlays'/ov['cpu']; od.mkdir(parents=True,exist_ok=True)
        rawp=od/f"overlay_{ov['id']:04d}.blz"
        decp=od/f"overlay_{ov['id']:04d}.bin"
        rawp.write_bytes(raw)
        dec=blz_decompress(raw) if ov['compressed'] else raw
        decp.write_bytes(dec)
        ov['decoded_size']=len(dec)
        ov['decoded_matches_ram_size']=(len(dec)==ov['ram_size'])
    manifest={
        'header':h,'fat_file_count':len(fat),'named_file_count':len(paths),'nitrofs_bytes':total,
        'arm9_overlays':ov9,'arm7_overlays':ov7,
        'magic_counts':dict(sorted(magic_counts.items(),key=lambda kv:(-kv[1],kv[0])))
    }
    out.mkdir(parents=True,exist_ok=True)
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    print(f"{h['title']} {h['gamecode']} SHA1={h['sha1']}")
    print(f"Files: {len(fat)} ({len(paths)} named), ARM9 overlays: {len(ov9)}, ARM7 overlays: {len(ov7)}")
    print(f"NitroFS payload: {total/1024/1024:.2f} MiB; NARC files: {magic_counts.get('NARC',0)}")
    bad=[o['id'] for o in ov9 if not o.get('decoded_matches_ram_size')]
    print('Overlay decode mismatches:', bad if bad else 'none')

if __name__=='__main__': main()
