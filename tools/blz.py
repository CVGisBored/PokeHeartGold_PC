#!/usr/bin/env python3
import struct

def decompress(data: bytes) -> bytes:
    # Nintendo DS backwards-LZ (BLZ/code compression), independently implemented.
    appended_amt = None
    for a in range(0, 0x20, 4):
        if len(data) < a + 8:
            break
        w, extra = struct.unpack_from('<II', data, len(data)-a-8)
        hlen, clen = w >> 24, w & 0xFFFFFF
        if hlen < 8 or clen > len(data)-a or hlen > len(data)-a:
            continue
        pad = data[len(data)-a-hlen:len(data)-a-8]
        if any(x != 0xFF for x in pad):
            continue
        appended_amt = a
        break
    if appended_amt is None:
        return data
    appended = data[-appended_amt:] if appended_amt else b''
    core = data[:-appended_amt] if appended_amt else data
    if len(core) < 8 or core[-4:] == b'\0\0\0\0':
        return data
    w, extra = struct.unpack_from('<II', core, len(core)-8)
    hlen, clen = w >> 24, w & 0xFFFFFF
    if clen >= len(core): clen = len(core)
    passthrough_len = len(core)-clen
    passthrough = core[:passthrough_len]
    comp = core[passthrough_len:passthrough_len+clen-hlen]
    out_len = len(core)+extra-passthrough_len
    out = bytearray(out_len)
    written=0; read=0; flags=0; mask=1
    while written < out_len:
        if mask == 1:
            if read >= len(comp): raise RuntimeError('BLZ flags out of input')
            flags=comp[-1-read]; read+=1; mask=0x80
        else:
            mask >>= 1
        if flags & mask:
            if read+2 > len(comp): raise RuntimeError('BLZ backref out of input')
            b1=comp[-1-read]; read+=1
            b2=comp[-1-read]; read+=1
            length=(b1>>4)+3
            disp=(((b1&0xF)<<8)|b2)+3
            if disp>written:
                if written<2: raise RuntimeError('BLZ invalid displacement')
                disp=2
            src=written-disp
            for _ in range(length):
                if written>=out_len: break
                out[-1-written]=out[-1-src]
                src+=1; written+=1
        else:
            if read>=len(comp): raise RuntimeError('BLZ literal out of input')
            out[-1-written]=comp[-1-read]; read+=1; written+=1
    return bytes(passthrough+out+appended)
