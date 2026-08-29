#include "assets/nitro2d.hpp"
#include <algorithm>
#include <cstddef>

namespace {
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){return p+2<=b.size()?std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8):0;}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){return p+4<=b.size()?std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24):0;}
bool tag(const std::vector<unsigned char>& b,const char* s){return b.size()>=4&&b[0]==s[0]&&b[1]==s[1]&&b[2]==s[2]&&b[3]==s[3];}
void bgr555(std::uint16_t c,std::uint8_t& r,std::uint8_t& g,std::uint8_t& bl){
    auto cv=[](unsigned v){return std::uint8_t((v<<3)|(v>>2));};
    r=cv(c&31);g=cv((c>>5)&31);bl=cv((c>>10)&31);
}
}

NitroRgbaImage decode_nitro_bg(const std::vector<unsigned char>& ncgr,
                               const std::vector<unsigned char>& nscr,
                               const std::vector<unsigned char>& nclr,
                               bool paletteIndexZeroTransparent){
    NitroRgbaImage out;
    if(!tag(ncgr,"RGCN")||!tag(nscr,"RCSN")||!tag(nclr,"RLCN")){out.error="NCGR/NSCR/NCLR signature mismatch";return out;}
    if(ncgr.size()<0x30||nscr.size()<0x24||nclr.size()<0x28){out.error="2D resource truncated";return out;}

    const int charH=u16(ncgr,0x18),charW=u16(ncgr,0x1a);
    const std::uint32_t fmt=u32(ncgr,0x1c); // HG resources use 3=4bpp, 4=8bpp.
    const std::size_t charBytes=u32(ncgr,0x28),charOff=0x30;
    const int screenW=u16(nscr,0x18),screenH=u16(nscr,0x1a);
    const std::size_t mapBytes=u32(nscr,0x20),mapOff=0x24;
    const std::size_t palBytes=u32(nclr,0x20),palOff=0x28;
    if(charW<=0||charH<=0||screenW<=0||screenH<=0||screenW%8||screenH%8){out.error="invalid Nitro 2D dimensions";return out;}
    if(charOff+charBytes>ncgr.size()||mapOff+mapBytes>nscr.size()||palOff+palBytes>nclr.size()){out.error="2D block size exceeds member";return out;}
    if(fmt!=3&&fmt!=4){out.error="unsupported NCGR color format";return out;}

    const int tilesX=screenW/8,tilesY=screenH/8;
    if(mapBytes<std::size_t(tilesX*tilesY*2)){out.error="NSCR tilemap too small";return out;}
    const std::size_t tileBytes=fmt==4?64:32;
    const std::size_t tileCount=charBytes/tileBytes;
    const std::size_t paletteCount=palBytes/2;
    if(tileCount==0||paletteCount==0){out.error="empty tile/palette data";return out;}

    out.width=screenW;out.height=screenH;out.rgba.assign(std::size_t(screenW)*screenH*4,0);
    for(int my=0;my<tilesY;my++)for(int mx=0;mx<tilesX;mx++){
        std::uint16_t ent=u16(nscr,mapOff+std::size_t(my*tilesX+mx)*2);
        std::size_t ti=ent&0x3ffu;if(ti>=tileCount)continue;
        bool hf=(ent&0x0400u)!=0,vf=(ent&0x0800u)!=0;unsigned palBank=(ent>>12)&0xf;
        for(int py=0;py<8;py++)for(int px=0;px<8;px++){
            int sx=hf?7-px:px,sy=vf?7-py:py;std::size_t pixel=std::size_t(sy*8+sx),pi=0;
            if(fmt==4) pi=ncgr[charOff+ti*64+pixel];
            else {auto q=ncgr[charOff+ti*32+pixel/2];pi=(pixel&1)?(q>>4):(q&15);pi+=palBank*16u;}
            if(pi>=paletteCount)continue;
            std::uint16_t c=u16(nclr,palOff+pi*2);std::uint8_t r,g,b;bgr555(c,r,g,b);
            std::size_t dst=(std::size_t(my*8+py)*screenW+std::size_t(mx*8+px))*4;
            out.rgba[dst]=r;out.rgba[dst+1]=g;out.rgba[dst+2]=b;out.rgba[dst+3]=(paletteIndexZeroTransparent&&pi==0)?0:255;
        }
    }
    out.valid=true;return out;
}

NitroRgbaImage decode_nitro_char_sheet(const std::vector<unsigned char>& ncgr,
                                       const std::vector<unsigned char>& nclr,
                                       bool transparent, int paletteBank){
    NitroRgbaImage out;
    if(!tag(ncgr,"RGCN")||!tag(nclr,"RLCN")||ncgr.size()<0x30||nclr.size()<0x28){out.error="NCGR/NCLR signature mismatch";return out;}
    int tilesY=u16(ncgr,0x18),tilesX=u16(ncgr,0x1a);const std::uint32_t fmt=u32(ncgr,0x1c);
    const std::size_t charBytes=u32(ncgr,0x28),charOff=0x30,palBytes=u32(nclr,0x20),palOff=0x28;
    if((fmt!=3&&fmt!=4)||charOff+charBytes>ncgr.size()||palOff+palBytes>nclr.size()){out.error="invalid Nitro char sheet";return out;}
    const std::size_t tileBytes=fmt==4?64:32;
    if((tilesX==0xffff||tilesY==0xffff)&&tileBytes&&charBytes%tileBytes==0){auto tiles=int(charBytes/tileBytes);tilesX=4;tilesY=(tiles+3)/4;}
    if(tilesX<=0||tilesY<=0){out.error="invalid Nitro char dimensions";return out;}
    const std::size_t bankColors=fmt==4?256:16,palBase=palOff+std::size_t(std::max(0,paletteBank))*bankColors*2,paletteCount=palBytes/2;
    if(std::size_t(tilesX*tilesY)*tileBytes>charBytes||paletteCount==0||palBase>=palOff+palBytes){out.error="Nitro char sheet truncated";return out;}
    out.width=tilesX*8;out.height=tilesY*8;out.rgba.assign(std::size_t(out.width)*out.height*4,0);
    for(int ty=0;ty<tilesY;ty++)for(int tx=0;tx<tilesX;tx++){std::size_t ti=std::size_t(ty*tilesX+tx);for(int py=0;py<8;py++)for(int px=0;px<8;px++){
        std::size_t pix=std::size_t(py*8+px),pi=0;if(fmt==4)pi=ncgr[charOff+ti*64+pix];else{auto q=ncgr[charOff+ti*32+pix/2];pi=(pix&1)?(q>>4):(q&15);}if(pi>=paletteCount)continue;
        std::size_t po=palBase+pi*2;if(po+1>=nclr.size())continue;std::uint8_t r,g,b;bgr555(u16(nclr,po),r,g,b);auto di=(std::size_t(ty*8+py)*out.width+std::size_t(tx*8+px))*4;out.rgba[di]=r;out.rgba[di+1]=g;out.rgba[di+2]=b;out.rgba[di+3]=(transparent&&pi==0)?0:255;
    }}out.valid=true;return out;
}


NitroRgbaImage decode_hg_pokepic(const std::vector<unsigned char>& ncgr,
                                 const std::vector<unsigned char>& nclr,
                                 bool transparent, int frame){
    NitroRgbaImage out;
    if(!tag(ncgr,"RGCN")||!tag(nclr,"RLCN")||ncgr.size()<0x30||nclr.size()<0x28){out.error="HG pokepic NCGR/NCLR signature mismatch";return out;}
    const std::uint32_t fmt=u32(ncgr,0x1c);
    const std::size_t charBytes=u32(ncgr,0x28),charOff=0x30,palBytes=u32(nclr,0x20),palOff=0x28;
    // Retail PokepicManager decrypts 3200 u16 values = 6400 bytes. These are
    // two 80x80 4bpp frames arranged as 80 scanlines of 80 bytes each.
    if(fmt!=3||charBytes<6400||charOff+6400>ncgr.size()||palBytes<32||palOff+32>nclr.size()){
        out.error="HG pokepic data is not the expected 6400-byte 4bpp layout";return out;
    }
    std::vector<unsigned char> raw(ncgr.begin()+std::ptrdiff_t(charOff),ncgr.begin()+std::ptrdiff_t(charOff+6400));
    auto get16=[&](std::size_t i){return std::uint16_t(raw[i])|(std::uint16_t(raw[i+1])<<8);};
    auto put16=[&](std::size_t i,std::uint16_t v){raw[i]=std::uint8_t(v&0xff);raw[i+1]=std::uint8_t(v>>8);};
    std::uint32_t seed=get16(0);
    for(std::size_t i=0;i<3200;i++){
        std::size_t pos=i*2;std::uint16_t v=std::uint16_t(get16(pos)^std::uint16_t(seed));put16(pos,v);
        seed=seed*1103515245u+24691u;
    }
    frame=std::clamp(frame,0,1);
    out.width=80;out.height=80;out.rgba.assign(std::size_t(out.width)*out.height*4,0);
    const std::size_t byteBase=std::size_t(frame)*40u;
    for(int y=0;y<80;y++)for(int x=0;x<80;x++){
        const unsigned char q=raw[std::size_t(y)*80u+byteBase+std::size_t(x/2)];
        const std::size_t pi=(x&1)?std::size_t(q>>4):std::size_t(q&0x0f);
        std::uint8_t r,g,b;bgr555(u16(nclr,palOff+pi*2),r,g,b);
        const std::size_t di=(std::size_t(y)*80u+std::size_t(x))*4u;
        out.rgba[di]=r;out.rgba[di+1]=g;out.rgba[di+2]=b;out.rgba[di+3]=(transparent&&pi==0)?0:255;
    }
    out.valid=true;return out;
}


std::vector<NitroRgbaImage> decode_nitro_cells(const std::vector<unsigned char>& ncgr,
                                                const std::vector<unsigned char>& ncer,
                                                const std::vector<unsigned char>& nclr,
                                                bool transparent){
    std::vector<NitroRgbaImage> out;
    if(!tag(ncgr,"RGCN")||!tag(ncer,"RECN")||!tag(nclr,"RLCN")||ncgr.size()<0x30||ncer.size()<0x30||nclr.size()<0x28)return out;
    const std::uint32_t fmt=u32(ncgr,0x1c);if(fmt!=3&&fmt!=4)return out;
    const std::size_t charBytes=u32(ncgr,0x28),charOff=0x30,palBytes=u32(nclr,0x20),palOff=0x28;
    if(charOff+charBytes>ncgr.size()||palOff+palBytes>nclr.size())return out;
    // CEBK is the first NCER block. HG's cell bank offsets are relative to the
    // CEBK payload (the byte immediately after the 8-byte block header).
    const std::size_t cb=16,payload=cb+8;
    if(ncer.size()<payload+0x18)return out;
    const std::uint16_t cellCount=u16(ncer,payload+0),bankType=u16(ncer,payload+2);
    const std::uint32_t cellOff=u32(ncer,payload+4);
    // CEBK mapping mode controls how the 10-bit OAM character name maps into
    // OBJ VRAM. In 1D modes the character-name address unit expands from
    // 32 bytes through 64/128/256 bytes. Trainer-back trbgra uses 1D 64K, so
    // treating its tile numbers as raw 32-byte indices assembles unrelated
    // character blocks and produces the broken/static player seen in v0.37.
    const std::uint32_t mappingMode=u32(ncer,payload+8);
    const std::uint32_t vramTransferOff=u32(ncer,payload+12);
    std::size_t transferEntries=0;
    if(vramTransferOff){
        const std::size_t tp=payload+std::size_t(vramTransferOff);
        if(tp+8<=ncer.size()){
            const std::uint32_t entriesOff=u32(ncer,tp+4);
            if(tp+std::size_t(entriesOff)+std::size_t(cellCount)*8<=ncer.size())transferEntries=tp+std::size_t(entriesOff);
        }
    }
    const std::size_t entryBytes=bankType?16u:8u,cellBase=payload+cellOff,oamBase=cellBase+std::size_t(cellCount)*entryBytes;
    if(!cellCount||cellBase>=ncer.size()||oamBase>ncer.size())return out;
    static constexpr int dims[3][4][2]={{{8,8},{16,16},{32,32},{64,64}},{{16,8},{32,8},{32,16},{64,32}},{{8,16},{8,32},{16,32},{32,64}}};
    const std::size_t paletteCount=palBytes/2;
    auto signedX=[](int v){return v>=256?v-512:v;};auto signedY=[](int v){return v>=128?v-256:v;};
    for(std::uint16_t ci=0;ci<cellCount;ci++){
        const std::size_t ep=cellBase+std::size_t(ci)*entryBytes;if(ep+8>ncer.size())break;
        const std::uint16_t objects=u16(ncer,ep);const std::uint32_t oo=u32(ncer,ep+4);
        // VRAM-transfer cell banks (including HG/SS trbgra) reuse the same OAM
        // character names for every animation cell and DMA a different character
        // slice into that OBJ address range for each cell. Without applying this
        // table every NANR frame samples cell 0's character data and looks static.
        std::size_t cellCharSource=0,cellCharBytes=charBytes;
        if(transferEntries){
            const std::size_t te=transferEntries+std::size_t(ci)*8;
            cellCharSource=u32(ncer,te+0);cellCharBytes=u32(ncer,te+4);
            if(cellCharSource>=charBytes){cellCharSource=0;cellCharBytes=charBytes;}
            else cellCharBytes=std::min<std::size_t>(cellCharBytes,charBytes-cellCharSource);
        }
        NitroRgbaImage im;im.valid=true;im.width=256;im.height=192;im.rgba.assign(std::size_t(im.width)*im.height*4,0);
        for(std::uint16_t oi=0;oi<objects;oi++){
            const std::size_t op=oamBase+oo+std::size_t(oi)*6;if(op+6>ncer.size())break;
            const std::uint16_t a0=u16(ncer,op),a1=u16(ncer,op+2),a2=u16(ncer,op+4);
            const int shape=(a0>>14)&3,size=(a1>>14)&3;if(shape>=3)continue;
            const int w=dims[shape][size][0],h=dims[shape][size][1];
            const int ox=128+signedX(a1&0x1ff),oy=96+signedY(a0&0xff);
            const bool affine=(a0&0x0100)!=0,hf=!affine&&(a1&0x1000),vf=!affine&&(a1&0x2000),color8=(a0&0x2000)!=0;
            const std::size_t tile0=a2&0x3ffu;const unsigned palBank=(a2>>12)&0xf;
            const int tilesPerRow=std::max(1,w/8);
            const std::size_t bytesPerTile=color8?64u:32u;
            for(int py=0;py<h;py++)for(int px=0;px<w;px++){
                const int sx=hf?w-1-px:px,sy=vf?h-1-py:py;
                const std::size_t tx=std::size_t(sx/8),ty=std::size_t(sy/8),pix=std::size_t(sy%8)*8+std::size_t(sx%8);
                std::size_t byteOffset=0;
                if(mappingMode<=3){
                    // OAM character names are expressed in 32-byte blocks; the
                    // boundary setting shifts the base address only. Tiles inside
                    // a 1D object remain tightly packed in character data.
                    const std::size_t baseBlock=tile0<<mappingMode;
                    const std::size_t localTile=ty*std::size_t(tilesPerRow)+tx;
                    byteOffset=baseBlock*32u+localTile*bytesPerTile;
                } else {
                    // Preserve the established 2D-object interpretation for banks
                    // that use it. HG trainer backs exercise the 1D path above.
                    const std::size_t localTile=ty*std::size_t(tilesPerRow)+tx;
                    byteOffset=(tile0+localTile)*32u;
                }
                std::size_t pi=0;
                if(color8){const std::size_t local=byteOffset+pix;if(local>=cellCharBytes)continue;const std::size_t pos=charOff+cellCharSource+local;pi=ncgr[pos];}
                else {const std::size_t local=byteOffset+pix/2;if(local>=cellCharBytes)continue;const std::size_t pos=charOff+cellCharSource+local;auto q=ncgr[pos];pi=((pix&1)?(q>>4):(q&15))+palBank*16u;}
                if(pi>=paletteCount||(transparent&&(color8?pi==0:(pi&15u)==0)))continue;
                const int dx=ox+px,dy=oy+py;if(dx<0||dy<0||dx>=im.width||dy>=im.height)continue;
                std::uint8_t r,g,b;bgr555(u16(nclr,palOff+pi*2),r,g,b);const std::size_t di=(std::size_t(dy)*im.width+std::size_t(dx))*4;
                im.rgba[di]=r;im.rgba[di+1]=g;im.rgba[di+2]=b;im.rgba[di+3]=255;
            }
        }
        out.push_back(std::move(im));
    }
    return out;
}

std::vector<unsigned char> nitro_lz10_decompress(const std::vector<unsigned char>& in){
    if(in.size()<4||in[0]!=0x10)return {};
    std::size_t outSize=std::size_t(in[1])|(std::size_t(in[2])<<8)|(std::size_t(in[3])<<16);
    if(outSize==0||outSize>128u*1024u*1024u)return {};
    std::vector<unsigned char> out;out.reserve(outSize);std::size_t p=4;
    while(out.size()<outSize&&p<in.size()){
        unsigned flags=in[p++];
        for(int bit=7;bit>=0&&out.size()<outSize;--bit){
            if((flags&(1u<<bit))==0){if(p>=in.size())return {};out.push_back(in[p++]);}
            else{
                if(p+1>=in.size())return {};
                unsigned a=in[p++],b=in[p++];
                std::size_t len=(a>>4)+3,disp=((std::size_t(a&0x0f)<<8)|b)+1;
                if(disp>out.size())return {};
                for(std::size_t i=0;i<len&&out.size()<outSize;i++)out.push_back(out[out.size()-disp]);
            }
        }
    }
    if(out.size()!=outSize)return {};
    return out;
}

NitroNanrBank decode_nitro_nanr(const std::vector<unsigned char>& nanr){
    NitroNanrBank out;
    if(!tag(nanr,"RNAN")||nanr.size()<0x30){out.error="NANR signature/truncation";return out;}
    std::size_t block=16;
    if(nanr.size()<block+8||!(nanr[block]=='K'&&nanr[block+1]=='N'&&nanr[block+2]=='B'&&nanr[block+3]=='A')){out.error="NANR ABNK block missing";return out;}
    const std::size_t payload=block+8;
    const std::uint16_t sequenceCount=u16(nanr,payload+0),frameCount=u16(nanr,payload+2);
    const std::size_t sequenceBase=payload+u32(nanr,payload+4);
    const std::size_t frameBase=payload+u32(nanr,payload+8);
    const std::size_t elementBase=payload+u32(nanr,payload+12);
    if(!sequenceCount||sequenceCount>1024||frameCount>16384||sequenceBase+std::size_t(sequenceCount)*16>nanr.size()||frameBase+std::size_t(frameCount)*8>nanr.size()||elementBase>=nanr.size()){
        out.error="NANR table bounds invalid";return out;
    }
    out.sequences.reserve(sequenceCount);
    for(std::uint16_t si=0;si<sequenceCount;si++){
        const std::size_t sp=sequenceBase+std::size_t(si)*16;
        const std::uint16_t count=u16(nanr,sp+0),startFrame=u16(nanr,sp+2);
        const std::uint16_t elementType=u16(nanr,sp+4),animationType=u16(nanr,sp+6);
        const std::uint32_t playbackMode=u32(nanr,sp+8);
        const std::size_t firstOffset=u32(nanr,sp+12);
        NitroNanrSequence seq;seq.startFrame=startFrame;seq.elementType=elementType;seq.animationType=animationType;seq.playbackMode=playbackMode;
        if(firstOffset%8!=0){out.sequences.push_back(std::move(seq));continue;}
        const std::size_t first=firstOffset/8;
        for(std::size_t fi=0;fi<count&&first+fi<frameCount;fi++){
            const std::size_t fp=frameBase+(first+fi)*8;
            const std::size_t elementOffset=u32(nanr,fp+0);
            const std::uint16_t duration=std::max<std::uint16_t>(1,u16(nanr,fp+4));
            if(elementBase+elementOffset+2>nanr.size())continue;
            // Index, Index+Translation and Index+SRT records all begin with the
            // cell index. The renderer currently consumes authored cell geometry;
            // transform fields can be layered on without corrupting frame choice.
            seq.frames.push_back({u16(nanr,elementBase+elementOffset),duration});
        }
        out.sequences.push_back(std::move(seq));
    }
    out.valid=std::any_of(out.sequences.begin(),out.sequences.end(),[](auto const& s){return !s.frames.empty();});
    if(!out.valid)out.error="NANR contained no playable frames";
    return out;
}

std::size_t sample_nitro_nanr_cell(const NitroNanrBank& bank,std::size_t sequence,double seconds,double framesPerSecond){
    if(!bank.valid||sequence>=bank.sequences.size()||bank.sequences[sequence].frames.empty())return 0;
    auto const& seq=bank.sequences[sequence];
    const std::size_t n=seq.frames.size();
    const std::size_t start=std::min<std::size_t>(seq.startFrame,n-1);
    std::uint64_t tick=std::uint64_t(std::max(0.0,seconds)*std::max(1.0,framesPerSecond));
    auto durationAt=[&](std::size_t i){return std::uint64_t(std::max<std::uint16_t>(1,seq.frames[i].duration));};

    std::vector<std::size_t> order;order.reserve(n*2);
    for(std::size_t i=start;i<n;i++)order.push_back(i);
    if(seq.playbackMode==2||seq.playbackMode==3){
        if(n>start+1)for(std::size_t i=n-1;i>start;i--)order.push_back(i-1);
    }
    if(order.empty())order.push_back(start);
    std::uint64_t total=0;for(auto i:order)total+=durationAt(i);
    if(total==0)return seq.frames[start].cellIndex;
    const bool loops=(seq.playbackMode==1||seq.playbackMode==3);
    if(tick>=total){if(loops)tick%=total;else tick=total-1;}
    std::uint64_t at=0;for(auto i:order){at+=durationAt(i);if(tick<at)return seq.frames[i].cellIndex;}
    return seq.frames[order.back()].cellIndex;
}

