#include "game/game.hpp"
#include "game/field_coords.hpp"
#include "assets/romfs.hpp"
#include "assets/narc.hpp"
#include "assets/nsbmd.hpp"
#include "assets/land_data.hpp"
#include "assets/overworld_data.hpp"
#include "assets/nitro2d.hpp"
#include "assets/hg_text.hpp"
#include "assets/wild_encounter.hpp"
#include "assets/pokemon_data.hpp"
#include "assets/trainer_data.hpp"
#include "assets/sdat.hpp"
#include "game/hg_state.hpp"
#include "game/retail_mart.hpp"
#include "game/script_header.hpp"
#include "game/hg_script.hpp"
#include "game/rom_world.hpp"
#include "game/overworld_anim.hpp"
#include "game/overworld_sprite_map.hpp"
#include "game/new_game_assets.hpp"
#include "game/battle_layout.hpp"
#include "game/battle_retail.hpp"
#include "game/script_objects.hpp"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <deque>
#include <iomanip>
#include <iostream>
#include <optional>
#include <map>
#include <sstream>
#include <utility>
#include <tuple>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {
constexpr float LW = RenderFrame::LogicalWidth;
constexpr float LH = RenderFrame::LogicalHeight;
constexpr float TILE = 40.0f;

// Retail HeartGold SDAT sequence IDs (include/constants/sndseq.h in the
// decomp). Keeping named IDs here avoids the v0.14 regression where TITLE was
// reused for the opening movie and Oak/new-game sequence.
constexpr std::uint16_t SEQ_GS_TITLE             = 1004;
constexpr std::uint16_t SEQ_GS_OPENING_TITLE_G   = 1006;
constexpr std::uint16_t SEQ_GS_POKEMON_THEME     = 1008;
constexpr std::uint16_t SEQ_GS_STARTING           = 1009;

enum class Tile : unsigned char { Grass, Path, Water, Tree, Wall, Floor, Door, Flower, Counter, Ledge, Sand };
enum class Dir : int { Down=0, Left=1, Right=2, Up=3 };
enum class Mode { Intro, Title, MainMenu, NewGameIntro, SavePrompt, Field, Dialogue, ScriptChoice, BankAmount, Menu, Party, PCStorage, Bag, Pokedex, Pokegear, TownMap, Mart, Naming, Summary, StarterSelect, Battle, SpriteViewer, AssetViewer, TerrainSandbox };
enum class AssetSource { Field, Room, Land };

// Object-event SPRITE_* -> a/0/8/1 MMODEL_* resolution lives in
// overworld_sprite_map.hpp.  It covers the full retail overworld namespace,
// including regular/static follower Pokémon and variable graphics slots.

static std::optional<std::uint16_t> hgFirstCheckedFlag(const std::vector<unsigned char>& bank,std::uint16_t scriptNumber){
    // Script banks begin with 32-bit relative entry pointers.  Elm's first
    // GoToIfSet is deliberately recovered from the retail bytecode instead of
    // hard-coding a regional FLAG_* number.
    if(bank.size()<8||scriptNumber==0)return std::nullopt;
    std::vector<std::size_t> entries;
    for(std::size_t p=0;p+4<=bank.size();p+=4){
        const std::uint16_t marker=std::uint16_t(bank[p])|(std::uint16_t(bank[p+1])<<8);
        if(marker==0xfd13)break;
        const std::uint32_t raw=std::uint32_t(bank[p])|(std::uint32_t(bank[p+1])<<8)|(std::uint32_t(bank[p+2])<<16)|(std::uint32_t(bank[p+3])<<24);
        const std::int32_t rel=static_cast<std::int32_t>(raw);
        const std::int64_t dest=std::int64_t(p)+4+rel;
        if(dest<0||dest>=std::int64_t(bank.size()))return std::nullopt;
        entries.push_back(std::size_t(dest));
        if(entries.size()>4096)return std::nullopt;
    }
    const std::size_t idx=std::size_t(scriptNumber-1);
    if(idx>=entries.size())return std::nullopt;
    const std::size_t begin=entries[idx],end=std::min(bank.size(),begin+96);
    for(std::size_t p=begin;p+4<=end;p++){
        const std::uint16_t op=std::uint16_t(bank[p])|(std::uint16_t(bank[p+1])<<8);
        if(op==32){ // CheckFlag / GoToIfSet condition source
            return std::uint16_t(bank[p+2])|(std::uint16_t(bank[p+3])<<8);
        }
    }
    return std::nullopt;
}

struct Warp { int x=0,y=0,targetMap=0,targetX=0,targetY=0; Dir targetFacing=Dir::Down; };
struct Sign { int x=0,y=0; std::vector<std::string> lines; };
struct Npc { int x=0,y=0; Dir facing=Dir::Down; std::string name; Color color; std::vector<std::string> lines; bool wander=false; };
struct MapDef {
    std::string name;
    int w=0,h=0;
    std::vector<Tile> tiles;
    std::vector<Warp> warps;
    std::vector<Sign> signs;
    std::vector<Npc> npcs;
    Tile get(int x,int y) const { if(x<0||y<0||x>=w||y>=h) return Tile::Tree; return tiles[static_cast<std::size_t>(y*w+x)]; }
    void set(int x,int y,Tile t){ if(x>=0&&y>=0&&x<w&&y<h) tiles[static_cast<std::size_t>(y*w+x)]=t; }
};

Color mul(Color c,float m){ c.r=std::clamp(c.r*m,0.0f,1.0f); c.g=std::clamp(c.g*m,0.0f,1.0f); c.b=std::clamp(c.b*m,0.0f,1.0f); return c; }
void rect(RenderFrame& f,float x,float y,float w,float h,Color c){ if(w>0&&h>0) f.rects.push_back({x,y,w,h,c}); }
void text(RenderFrame& f,float x,float y,int s,std::string t,Color c={1,1,1,1},bool shadow=true,float advance=0.0f,float lineAdvance=0.0f){ f.texts.push_back({x,y,s,std::move(t),c,shadow,advance,lineAdvance}); }

void ensurePixels(RenderFrame& f,Color c={0,0,0,1}){
    if(f.hasPixels())return;
    f.rgba.resize(std::size_t(RenderFrame::PixelWidth)*RenderFrame::PixelHeight*4);
    auto cv=[](float v){return std::uint8_t(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);};
    for(std::size_t i=0;i<f.rgba.size();i+=4){f.rgba[i]=cv(c.r);f.rgba[i+1]=cv(c.g);f.rgba[i+2]=cv(c.b);f.rgba[i+3]=255;}
}

void pixelRect(RenderFrame& f,int x,int y,int w,int h,Color c){
    if(w<=0||h<=0)return;
    ensurePixels(f,f.clear);
    const int x0=std::max(0,x),y0=std::max(0,y),x1=std::min(RenderFrame::PixelWidth,x+w),y1=std::min(RenderFrame::PixelHeight,y+h);
    if(x1<=x0||y1<=y0)return;
    const float a=std::clamp(c.a,0.0f,1.0f);
    const auto cv=[](float v){return std::uint8_t(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);};
    const std::uint8_t r=cv(c.r),g=cv(c.g),b=cv(c.b);
    for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
        const std::size_t i=(std::size_t(yy)*RenderFrame::PixelWidth+std::size_t(xx))*4;
        if(a>=0.999f){f.rgba[i]=r;f.rgba[i+1]=g;f.rgba[i+2]=b;f.rgba[i+3]=255;}
        else if(a>0.0f){
            f.rgba[i]=std::uint8_t(float(r)*a+float(f.rgba[i])*(1.0f-a));
            f.rgba[i+1]=std::uint8_t(float(g)*a+float(f.rgba[i+1])*(1.0f-a));
            f.rgba[i+2]=std::uint8_t(float(b)*a+float(f.rgba[i+2])*(1.0f-a));
            f.rgba[i+3]=255;
        }
    }
}

void blitImage(RenderFrame& f,const NitroRgbaImage& img,int dx,int dy,int dw,int dh,bool alpha=true){
    if(!img.valid||img.width<=0||img.height<=0||dw<=0||dh<=0)return;
    ensurePixels(f,f.clear);
    for(int y=0;y<dh;y++){int yy=dy+y;if(yy<0||yy>=RenderFrame::PixelHeight)continue;int sy=std::clamp(y*img.height/dh,0,img.height-1);
        for(int x=0;x<dw;x++){int xx=dx+x;if(xx<0||xx>=RenderFrame::PixelWidth)continue;int sx=std::clamp(x*img.width/dw,0,img.width-1);
            std::size_t si=(std::size_t(sy)*img.width+std::size_t(sx))*4,di=(std::size_t(yy)*RenderFrame::PixelWidth+std::size_t(xx))*4;
            std::uint8_t a=img.rgba[si+3];if(alpha&&a==0)continue;
            if(!alpha||a==255){f.rgba[di]=img.rgba[si];f.rgba[di+1]=img.rgba[si+1];f.rgba[di+2]=img.rgba[si+2];f.rgba[di+3]=255;}
            else{float af=a/255.0f;for(int c=0;c<3;c++)f.rgba[di+c]=std::uint8_t(img.rgba[si+c]*af+f.rgba[di+c]*(1.0f-af));f.rgba[di+3]=255;}
        }
    }
}


void blitImageCrop(RenderFrame& f,const NitroRgbaImage& img,int sx0,int sy0,int sw,int sh,int dx,int dy,int dw,int dh,bool alpha=true){
    if(!img.valid||sw<=0||sh<=0||dw<=0||dh<=0)return;
    ensurePixels(f,f.clear);
    for(int y=0;y<dh;y++){int yy=dy+y;if(yy<0||yy>=RenderFrame::PixelHeight)continue;int sy=std::clamp(sy0+y*sh/dh,0,img.height-1);
        for(int x=0;x<dw;x++){int xx=dx+x;if(xx<0||xx>=RenderFrame::PixelWidth)continue;int sx=std::clamp(sx0+x*sw/dw,0,img.width-1);std::size_t si=(std::size_t(sy)*img.width+std::size_t(sx))*4,di=(std::size_t(yy)*RenderFrame::PixelWidth+std::size_t(xx))*4;std::uint8_t a=img.rgba[si+3];if(alpha&&a==0)continue;if(!alpha||a==255){for(int c=0;c<3;c++)f.rgba[di+c]=img.rgba[si+c];f.rgba[di+3]=255;}else{float af=a/255.0f;for(int c=0;c<3;c++)f.rgba[di+c]=std::uint8_t(img.rgba[si+c]*af+f.rgba[di+c]*(1.0f-af));f.rgba[di+3]=255;}}
    }
}
void wrappedText(RenderFrame& f,float x,float y,int scale,const std::string& src,std::size_t cols,Color c={1,1,1,1}){
    std::size_t p=0;float yy=y;while(p<src.size()){while(p<src.size()&&(src[p]==' '||src[p]=='\n')){if(src[p]=='\n')yy+=28*scale/2.0f;p++;}if(p>=src.size())break;std::size_t end=std::min(src.size(),p+cols),cut=end;if(end<src.size()){auto sp=src.rfind(' ',end);if(sp!=std::string::npos&&sp>p)cut=sp;}text(f,x,yy,scale,src.substr(p,cut-p),c);yy+=28*scale/2.0f;p=cut;}
}

// opening.narc backgrounds can be larger than one DS screen because retail scrolls
// them while separate BG/OBJ layers fill the rest. Choose the densest 256x192 crop
// and only pan a small distance around it; this avoids exposing unused black/blank
// regions when a still-unimplemented retail sprite layer would normally cover them.
std::pair<int,int> bestNitroCrop(const NitroRgbaImage& img,int cw,int ch){
    if(!img.valid||(img.width<=cw&&img.height<=ch))return {0,0};
    const int maxX=std::max(0,img.width-cw),maxY=std::max(0,img.height-ch);
    auto score=[&](int sx,int sy){
        std::uint64_t v=0;
        for(int y=sy;y<std::min(img.height,sy+ch);y+=4)for(int x=sx;x<std::min(img.width,sx+cw);x+=4){
            const auto i=(std::size_t(y)*img.width+std::size_t(x))*4;
            if(img.rgba[i+3]<16)continue;
            const int r=img.rgba[i],g=img.rgba[i+1],b=img.rgba[i+2];
            const int hi=std::max({r,g,b}),lo=std::min({r,g,b});
            // Reward visible/colorful detail and lightly reward non-black pixels.
            v+=std::uint64_t((r+g+b)>45?2:0)+std::uint64_t(hi-lo>20?2:0)+std::uint64_t(hi>110?1:0);
        }
        return v;
    };
    std::uint64_t best=0;int bx=0,by=0;
    for(int y=0;y<=maxY;y+=8)for(int x=0;x<=maxX;x+=8){auto sc=score(x,y);if(sc>best){best=sc;bx=x;by=y;}}
    // Include the exact far edge when dimensions are not multiples of eight.
    for(auto [x,y]:{std::pair{maxX,by},std::pair{bx,maxY},std::pair{maxX,maxY}}){auto sc=score(x,y);if(sc>best){best=sc;bx=x;by=y;}}
    return {bx,by};
}


NitroRgbaImage compositeNitroLayers(const std::vector<NitroRgbaImage>& layers){
    NitroRgbaImage out;int w=0,h=0;for(auto const& q:layers)if(q.valid){w=std::max(w,q.width);h=std::max(h,q.height);}if(w<=0||h<=0){out.error="no valid layers";return out;}out.width=w;out.height=h;out.rgba.assign(std::size_t(w)*h*4,0);
    for(auto const& q:layers){if(!q.valid)continue;for(int y=0;y<std::min(h,q.height);y++)for(int x=0;x<std::min(w,q.width);x++){auto si=(std::size_t(y)*q.width+x)*4,di=(std::size_t(y)*w+x)*4;auto a=q.rgba[si+3];if(!a)continue;if(a==255){for(int c=0;c<4;c++)out.rgba[di+c]=q.rgba[si+c];}else{float af=a/255.0f;for(int c=0;c<3;c++)out.rgba[di+c]=std::uint8_t(q.rgba[si+c]*af+out.rgba[di+c]*(1-af));out.rgba[di+3]=std::max(out.rgba[di+3],a);}}}
    out.valid=true;return out;
}

NitroRgbaImage fillNitroTransparencyFromArtwork(NitroRgbaImage img){
    if(!img.valid||img.rgba.empty())return img;
    // Pick a dominant non-transparent color from the decoded ROM artwork, quantized
    // to DS-like 5-bit channels. This fills only regions that retail normally covers
    // with another BG/OBJ layer; no external/generated artwork is introduced.
    std::unordered_map<std::uint16_t,std::uint32_t> counts;
    for(std::size_t i=0;i+3<img.rgba.size();i+=16){
        if(img.rgba[i+3]<64)continue;
        const std::uint16_t r=img.rgba[i]>>3,g=img.rgba[i+1]>>3,b=img.rgba[i+2]>>3;
        counts[std::uint16_t(r|(g<<5)|(b<<10))]++;
    }
    std::uint16_t best=0;std::uint32_t bestN=0;for(auto [k,n]:counts)if(n>bestN){best=k;bestN=n;}
    const std::uint8_t br=std::uint8_t((best&31)*255/31),bg=std::uint8_t(((best>>5)&31)*255/31),bb=std::uint8_t(((best>>10)&31)*255/31);
    for(std::size_t i=0;i+3<img.rgba.size();i+=4)if(img.rgba[i+3]<16){img.rgba[i]=br;img.rgba[i+1]=bg;img.rgba[i+2]=bb;img.rgba[i+3]=255;}
    return img;
}


static std::vector<unsigned char> readNitro2dMember(const std::filesystem::path& narc,std::size_t index){
    auto raw=read_narc_member(narc,index);
    auto dec=nitro_lz10_decompress(raw);
    return dec.empty()?raw:dec;
}

struct NitroOpaqueBounds { int x=0,y=0,w=0,h=0; };

NitroOpaqueBounds nitroOpaqueBounds(const NitroRgbaImage& img){
    NitroOpaqueBounds out; if(!img.valid||img.width<=0||img.height<=0||img.rgba.empty())return out;
    int minX=img.width,minY=img.height,maxX=-1,maxY=-1;
    for(int y=0;y<img.height;y++)for(int x=0;x<img.width;x++){
        std::size_t i=(std::size_t(y)*img.width+std::size_t(x))*4;
        if(img.rgba[i+3]<16)continue;
        minX=std::min(minX,x); minY=std::min(minY,y); maxX=std::max(maxX,x); maxY=std::max(maxY,y);
    }
    if(maxX<minX||maxY<minY)return out;
    out.x=minX; out.y=minY; out.w=maxX-minX+1; out.h=maxY-minY+1; return out;
}

void blitCroppedNitro(RenderFrame& f,const NitroRgbaImage& img,int dx,int dy,int dw,int dh,bool alpha=true){
    auto b=nitroOpaqueBounds(img);
    if(!b.w||!b.h) return;
    blitImageCrop(f,img,b.x,b.y,b.w,b.h,dx,dy,dw,dh,alpha);
}

MapDef makeTown(){
    MapDef m{"NEW BARK TOWN - NATIVE DEMO",34,24,{}, {}, {}, {}}; m.tiles.assign(m.w*m.h,Tile::Grass);
    for(int x=0;x<m.w;x++){m.set(x,0,Tile::Tree);m.set(x,m.h-1,Tile::Tree);} for(int y=0;y<m.h;y++){m.set(0,y,Tile::Tree);m.set(m.w-1,y,Tile::Tree);}
    for(int x=1;x<m.w-1;x++) for(int y=10;y<=12;y++) m.set(x,y,Tile::Path);
    for(int y=4;y<m.h-1;y++) for(int x=14;x<=16;x++) m.set(x,y,Tile::Path);
    for(int y=3;y<=7;y++) for(int x=4;x<=10;x++) m.set(x,y,Tile::Wall);
    m.set(7,7,Tile::Door); m.set(7,8,Tile::Path);
    for(int y=4;y<=8;y++) for(int x=20;x<=28;x++) m.set(x,y,Tile::Wall);
    m.set(24,8,Tile::Door); m.set(24,9,Tile::Path);
    for(int y=15;y<=20;y++) for(int x=4;x<=12;x++) m.set(x,y,Tile::Wall);
    m.set(8,20,Tile::Door); m.set(8,21,Tile::Path);
    for(int y=15;y<=20;y++) for(int x=21;x<=28;x++) m.set(x,y,Tile::Water);
    for(int x=2;x<13;x+=2){m.set(x,13,Tile::Flower);} for(int x=18;x<31;x+=3){m.set(x,14,Tile::Flower);}
    m.warps.push_back({7,7,3,8,9,Dir::Up});
    m.warps.push_back({24,8,2,8,9,Dir::Up});
    m.warps.push_back({1,11,1,36,10,Dir::Left});
    m.signs.push_back({17,8,{"NEW BARK TOWN","A quiet town where a native PC port begins."}});
    m.signs.push_back({19,9,{"PROF. ELM POKEMON LAB","Vulkan renderer online. ARM execution layer: none."}});
    m.npcs.push_back({18,12,Dir::Left,"LYRA",{0.92f,0.32f,0.40f,1},{"Hey! Movement and collision are native now.","Try the lab, the house, or Route 29 to the west."},true});
    m.npcs.push_back({12,9,Dir::Down,"TOWNSPERSON",{0.25f,0.55f,0.90f,1},{"F3 toggles the native debug HUD.","F5 saves. F9 loads."},false});
    return m;
}

MapDef makeRoute(){
    MapDef m{"ROUTE 29 - NATIVE DEMO",40,22,{}, {}, {}, {}}; m.tiles.assign(m.w*m.h,Tile::Grass);
    for(int x=0;x<m.w;x++){m.set(x,0,Tile::Tree);m.set(x,m.h-1,Tile::Tree);} for(int y=0;y<m.h;y++){m.set(0,y,Tile::Tree);m.set(m.w-1,y,Tile::Tree);}
    for(int x=1;x<m.w-1;x++) for(int y=9;y<=11;y++) m.set(x,y,Tile::Path);
    for(int x=8;x<=14;x++) for(int y=4;y<=7;y++) m.set(x,y,Tile::Water);
    for(int x=19;x<=26;x++) m.set(x,7,Tile::Ledge);
    for(int x=4;x<35;x+=3){m.set(x,14,Tile::Flower); if(x%2) m.set(x,5,Tile::Flower);}
    for(int x=3;x<37;x+=5){m.set(x,3,Tile::Tree); m.set(x,18,Tile::Tree);}
    m.warps.push_back({38,10,0,2,11,Dir::Right});
    m.signs.push_back({31,8,{"ROUTE 29","Wild battles are not hooked up in the native runtime yet."}});
    m.npcs.push_back({25,10,Dir::Right,"YOUNGSTER",{0.25f,0.70f,0.35f,1},{"This route is rendered from native world data.","The next milestone is decoding the ROM's real field models."},true});
    return m;
}

MapDef makeLab(){
    MapDef m{"PROF. ELM'S LAB - NATIVE DEMO",17,13,{}, {}, {}, {}}; m.tiles.assign(m.w*m.h,Tile::Floor);
    for(int x=0;x<m.w;x++){m.set(x,0,Tile::Wall);m.set(x,m.h-1,Tile::Wall);} for(int y=0;y<m.h;y++){m.set(0,y,Tile::Wall);m.set(m.w-1,y,Tile::Wall);}
    m.set(8,12,Tile::Door);
    for(int x=3;x<=5;x++)m.set(x,3,Tile::Counter);
    for(int x=11;x<=13;x++)m.set(x,3,Tile::Counter);
    for(int x=3;x<=5;x++)m.set(x,7,Tile::Counter);
    for(int x=11;x<=13;x++)m.set(x,7,Tile::Counter);
    m.warps.push_back({8,12,0,24,9,Dir::Down});
    m.npcs.push_back({8,4,Dir::Down,"PROF. ELM",{0.90f,0.86f,0.70f,1},{"Welcome to the native ROM-world build!","This program is x86-64 C++ and Vulkan. It does not execute Nintendo DS ARM code.","Map, terrain, event and sprite systems are now selected from extracted ROM data and replaced natively."},false});
    m.npcs.push_back({4,9,Dir::Right,"AIDE",{0.38f,0.55f,0.90f,1},{"The native loader now follows the ROM map matrix, permissions, buildings, events, warps and asset archives.","Faithful model and animation decoding comes next."},false});
    return m;
}

MapDef makeHouse(){
    MapDef m{"PLAYER HOUSE - NATIVE DEMO",17,13,{}, {}, {}, {}}; m.tiles.assign(m.w*m.h,Tile::Floor);
    for(int x=0;x<m.w;x++){m.set(x,0,Tile::Wall);m.set(x,m.h-1,Tile::Wall);} for(int y=0;y<m.h;y++){m.set(0,y,Tile::Wall);m.set(m.w-1,y,Tile::Wall);}
    m.set(8,12,Tile::Door); for(int x=3;x<=6;x++)m.set(x,4,Tile::Counter); for(int x=11;x<=13;x++)m.set(x,8,Tile::Counter);
    m.warps.push_back({8,12,0,7,8,Dir::Down});
    m.npcs.push_back({10,5,Dir::Left,"MOM",{0.95f,0.50f,0.55f,1},{"The save system works already.","Use F5 or choose SAVE from the pause menu."},false});
    m.signs.push_back({4,3,{"A PC monitor is displaying a Vulkan validation message.","No emulator core detected. Good."}});
    return m;
}

bool passable(Tile t){ return t==Tile::Grass||t==Tile::Path||t==Tile::Floor||t==Tile::Door||t==Tile::Flower||t==Tile::Sand; }
std::pair<int,int> delta(Dir d){ switch(d){case Dir::Down:return{0,1};case Dir::Up:return{0,-1};case Dir::Left:return{-1,0};case Dir::Right:return{1,0};} return{0,0}; }
std::string dirName(Dir d){ switch(d){case Dir::Down:return"DOWN";case Dir::Up:return"UP";case Dir::Left:return"LEFT";case Dir::Right:return"RIGHT";}return"?"; }

static std::string hgPhoneContactName(std::uint16_t id){
    static constexpr std::array<const char*,75> names={
        "MOM","PROF. ELM","PROF. OAK","ETHAN","LYRA","KURT","DAY-CARE MAN","DAY-CARE LADY","BUENA","BILL",
        "JOEY","RALPH","LIZ","WADE","ANTHONY","BIKE SHOP","KENJI","WHITNEY","FALKNER","JACK",
        "CHAD","BRENT","TODD","ARNIE","BAOBA","IRWIN","JANINE","CLAIR","ERIKA","MISTY",
        "BLAINE","BLUE","CHUCK","BROCK","BUGSY","SABRINA","LT. SURGE","MORTY","JASMINE","PRYCE",
        "HUEY","GAVEN","JAMIE","REENA","VANCE","PARRY","ERIN","BEVERLY","JOSE","GINA",
        "ALAN","DANA","DEREK","TULLY","TIFFANY","WILTON","KRISE","IAN","WALT","ALFRED",
        "DOUG","ROB","KYLE","KYLER","TIM & SUE","KENNY","TANNER","JOSH","TORIN","HILLARY",
        "BILLY","KAY & TIA","REESE","AIDEN","ERNEST"
    };
    return id<names.size()?names[id]:("CONTACT #"+std::to_string(id));
}

Color tileBase(Tile t,float light){
    Color c;
    switch(t){
        case Tile::Grass:c={0.24f,0.58f,0.28f,1};break; case Tile::Path:c={0.75f,0.69f,0.48f,1};break; case Tile::Water:c={0.18f,0.48f,0.78f,1};break;
        case Tile::Tree:c={0.08f,0.33f,0.13f,1};break; case Tile::Wall:c={0.55f,0.36f,0.24f,1};break; case Tile::Floor:c={0.77f,0.69f,0.55f,1};break;
        case Tile::Door:c={0.42f,0.25f,0.13f,1};break; case Tile::Flower:c={0.24f,0.58f,0.28f,1};break; case Tile::Counter:c={0.38f,0.24f,0.15f,1};break;
        case Tile::Ledge:c={0.56f,0.48f,0.33f,1};break; case Tile::Sand:c={0.82f,0.74f,0.48f,1};break;
    }
    return mul(c,light);
}

int localHour(){
    auto now=std::chrono::system_clock::now(); std::time_t tt=std::chrono::system_clock::to_time_t(now); std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm,&tt);
#else
    localtime_r(&tt,&tm);
#endif
    return tm.tm_hour;
}
float dayLightFactor(){ int h=localHour(); if(h>=7&&h<18) return 1.0f; if(h>=18&&h<21) return 0.78f; if(h>=5&&h<7) return 0.82f; return 0.60f; }

struct ScreenTri {
    float x[3]{}, y[3]{}, z=0;
    Vec2f uv[3]{};
    Color color{};
    int textureIndex=-1;
    int paletteIndex=-1;
};

static Color rgbaColor(const NsbmdTexture& t,int paletteIndex,int x,int y){
    const std::vector<unsigned char>* pixels=&t.rgba;
    if(paletteIndex>=0&&size_t(paletteIndex)<t.paletteVariants.size()) pixels=&t.paletteVariants[size_t(paletteIndex)];
    if(!t.width||!t.height||pixels->size()!=size_t(t.width)*t.height*4)return {1,0,1,1};
    x%=int(t.width);y%=int(t.height);if(x<0)x+=int(t.width);if(y<0)y+=int(t.height);
    size_t q=(size_t(y)*t.width+size_t(x))*4;
    return {(*pixels)[q]/255.0f,(*pixels)[q+1]/255.0f,(*pixels)[q+2]/255.0f,(*pixels)[q+3]/255.0f};
}


static int wrapTextureCoord(int v,int size,bool repeat,bool mirror){
    if(size<=0)return 0;
    if(!repeat)return std::clamp(v,0,size-1);
    if(!mirror){v%=size;if(v<0)v+=size;return v;}
    const int period=size*2;
    v%=period;if(v<0)v+=period;
    return v<size?v:(period-1-v);
}

static int fieldAnimatedTextureAxis(const NsbmdTexture& t){
    std::string n=t.name;
    for(char& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));
    // HG area records explicitly identify maps with dynamic field textures. The
    // native renderer animates the continuously moving material classes rather
    // than leaving the decoded first frame permanently frozen.
    static constexpr const char* vertical[]={"water","river","wave","fall","foam","fountain","stream"};
    for(auto* k:vertical)if(n.find(k)!=std::string::npos)return 2;
    static constexpr const char* horizontal[]={"sea","pool","light","lamp","neon","screen","flag"};
    for(auto* k:horizontal)if(n.find(k)!=std::string::npos)return 1;
    return 0;
}


// Native software scene raster used by the ROM-world view. Vulkan still owns
// the window/swapchain/presentation; this raster replaces the old coarse
// clear-rectangle approximation so DS texels are nearest-sampled per screen
// pixel and all terrain/buildings/characters share one depth buffer.
struct SoftwareRaster {
    static constexpr int W=RenderFrame::PixelWidth,H=RenderFrame::PixelHeight;
    std::vector<std::uint8_t> rgba;
    std::vector<float> depth;

    explicit SoftwareRaster(Color clear):rgba(std::size_t(W)*H*4),depth(std::size_t(W)*H,std::numeric_limits<float>::infinity()){
        const auto cv=[](float v){return std::uint8_t(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);};
        for(std::size_t i=0;i<std::size_t(W)*H;i++){rgba[i*4]=cv(clear.r);rgba[i*4+1]=cv(clear.g);rgba[i*4+2]=cv(clear.b);rgba[i*4+3]=255;}
    }
    void put(int x,int y,float z,Color c,bool depthWrite=true){
        if(x<0||y<0||x>=W||y>=H||z<=0.0f)return;
        std::size_t i=std::size_t(y)*W+std::size_t(x);
        if(z>depth[i]+0.00005f)return;
        c.r=std::clamp(c.r,0.0f,1.0f);c.g=std::clamp(c.g,0.0f,1.0f);c.b=std::clamp(c.b,0.0f,1.0f);c.a=std::clamp(c.a,0.0f,1.0f);
        if(c.a<0.02f)return;
        std::size_t q=i*4;
        if(c.a<0.995f){
            float ia=1.0f-c.a;
            rgba[q]=std::uint8_t(std::clamp(c.r*c.a+(rgba[q]/255.0f)*ia,0.0f,1.0f)*255.0f+0.5f);
            rgba[q+1]=std::uint8_t(std::clamp(c.g*c.a+(rgba[q+1]/255.0f)*ia,0.0f,1.0f)*255.0f+0.5f);
            rgba[q+2]=std::uint8_t(std::clamp(c.b*c.a+(rgba[q+2]/255.0f)*ia,0.0f,1.0f)*255.0f+0.5f);
        }else{
            rgba[q]=std::uint8_t(c.r*255.0f+0.5f);rgba[q+1]=std::uint8_t(c.g*255.0f+0.5f);rgba[q+2]=std::uint8_t(c.b*255.0f+0.5f);
        }
        rgba[q+3]=255;
        if(depthWrite&&c.a>=0.45f)depth[i]=z;
    }
    bool blockOccluded(int x,int y,int step,float z) const{
        if(z<=0.0f||step<=0)return true;
        const int x0=std::max(0,x),y0=std::max(0,y),x1=std::min(W,x+step),y1=std::min(H,y+step);
        if(x0>=x1||y0>=y1)return true;
        for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
            const std::size_t i=std::size_t(yy)*W+std::size_t(xx);
            if(z<=depth[i]+0.00005f)return false;
        }
        return true;
    }
    void putBlock(int x,int y,int step,float z,Color c,bool depthWrite=true){
        if(z<=0.0f||step<=0)return;
        c.r=std::clamp(c.r,0.0f,1.0f);c.g=std::clamp(c.g,0.0f,1.0f);c.b=std::clamp(c.b,0.0f,1.0f);c.a=std::clamp(c.a,0.0f,1.0f);
        if(c.a<0.02f)return;
        const int x0=std::max(0,x),y0=std::max(0,y),x1=std::min(W,x+step),y1=std::min(H,y+step);
        if(x0>=x1||y0>=y1)return;
        const bool opaque=c.a>=0.995f;
        const bool writeDepth=depthWrite&&c.a>=0.45f;
        const std::uint8_t sr=std::uint8_t(c.r*255.0f+0.5f),sg=std::uint8_t(c.g*255.0f+0.5f),sb=std::uint8_t(c.b*255.0f+0.5f);
        const float ia=1.0f-c.a;
        for(int yy=y0;yy<y1;yy++)for(int xx=x0;xx<x1;xx++){
            const std::size_t i=std::size_t(yy)*W+std::size_t(xx);
            if(z>depth[i]+0.00005f)continue;
            const std::size_t q=i*4;
            if(opaque){rgba[q]=sr;rgba[q+1]=sg;rgba[q+2]=sb;}
            else{
                rgba[q]=std::uint8_t(std::clamp(c.r*c.a+(rgba[q]/255.0f)*ia,0.0f,1.0f)*255.0f+0.5f);
                rgba[q+1]=std::uint8_t(std::clamp(c.g*c.a+(rgba[q+1]/255.0f)*ia,0.0f,1.0f)*255.0f+0.5f);
                rgba[q+2]=std::uint8_t(std::clamp(c.b*c.a+(rgba[q+2]/255.0f)*ia,0.0f,1.0f)*255.0f+0.5f);
            }
            rgba[q+3]=255;if(writeDepth)depth[i]=z;
        }
    }
};

struct PlacedRotation {
    bool x=false,y=false,z=false;
    float cx=1.0f,sx=0.0f,cy=1.0f,sy=0.0f,cz=1.0f,sz=0.0f;
};

static PlacedRotation makePlacedRotation(std::uint16_t xr,std::uint16_t yr,std::uint16_t zr){
    PlacedRotation r;r.x=xr!=0;r.y=yr!=0;r.z=zr!=0;
    if(!(r.x||r.y||r.z))return r;
    constexpr float tau=6.2831853071795864769f;
    if(r.x){const float a=float(xr)*(tau/65536.0f);r.cx=std::cos(a);r.sx=std::sin(a);}
    if(r.y){const float a=float(yr)*(tau/65536.0f);r.cy=std::cos(a);r.sy=std::sin(a);}
    if(r.z){const float a=float(zr)*(tau/65536.0f);r.cz=std::cos(a);r.sz=std::sin(a);}
    return r;
}

static Vec3f rotatePlaced(Vec3f p,const PlacedRotation& r){
    if(r.x){float y=p.y*r.cx-p.z*r.sx,z=p.y*r.sx+p.z*r.cx;p.y=y;p.z=z;}
    if(r.y){float x=p.x*r.cy+p.z*r.sy,z=-p.x*r.sy+p.z*r.cy;p.x=x;p.z=z;}
    if(r.z){float x=p.x*r.cz-p.y*r.sz,y=p.x*r.sz+p.y*r.cz;p.x=x;p.y=y;}
    return p;
}

static void drawTexturePixels(RenderFrame& f,const NsbmdTexture& t,float x,float y,float scale,float light=1.0f,bool flipX=false){
    if(!t.width||!t.height||t.rgba.size()!=size_t(t.width)*t.height*4)return;
    for(unsigned sy=0;sy<t.height;sy++){
        unsigned sx=0;
        while(sx<t.width){
            unsigned srcx=flipX?(t.width-1-sx):sx;size_t q=(size_t(sy)*t.width+srcx)*4;
            unsigned a=t.rgba[q+3];if(a<24){sx++;continue;}
            unsigned r=t.rgba[q],g=t.rgba[q+1],b=t.rgba[q+2];unsigned run=1;
            while(sx+run<t.width){unsigned nx=flipX?(t.width-1-(sx+run)):(sx+run);size_t z=(size_t(sy)*t.width+nx)*4;if(t.rgba[z+3]<24||t.rgba[z]!=r||t.rgba[z+1]!=g||t.rgba[z+2]!=b)break;run++;}
            Color c=mul({r/255.0f,g/255.0f,b/255.0f,a/255.0f},light);rect(f,x+sx*scale,y+sy*scale,run*scale+0.15f,scale+0.15f,c);sx+=run;
        }
    }
}

static void drawNsbmdTexture(RenderFrame& f,const NsbmdTexture& t,float x,float y,float maxW,float maxH){
    if(!t.width||!t.height||t.rgba.size()!=size_t(t.width)*t.height*4)return;
    unsigned step=std::max(1u,std::max((t.width+95)/96,(t.height+95)/96));float dw=float((t.width+step-1)/step),dh=float((t.height+step-1)/step);float cell=std::max(1.0f,std::min(maxW/dw,maxH/dh));float ox=x+(maxW-dw*cell)*0.5f,oy=y+(maxH-dh*cell)*0.5f;
    for(unsigned sy=0,dy=0;sy<t.height;sy+=step,dy++)for(unsigned sx=0,dx=0;sx<t.width;sx+=step,dx++){size_t q=(size_t(sy)*t.width+sx)*4;float a=t.rgba[q+3]/255.0f;Color checker=(((dx^dy)&1)?Color{0.22f,0.24f,0.27f,1}:Color{0.14f,0.16f,0.19f,1});Color c{t.rgba[q]/255.0f,t.rgba[q+1]/255.0f,t.rgba[q+2]/255.0f,1};c={c.r*a+checker.r*(1-a),c.g*a+checker.g*(1-a),c.b*a+checker.b*(1-a),1};rect(f,ox+dx*cell,oy+dy*cell,cell+0.2f,cell+0.2f,c);}
}

static const NsbmdTexture* findNumericTextureFrame(const NsbmdMember& sprite,int wanted){
    std::string suffix="."+std::to_string(wanted);
    for(auto const& t:sprite.textures)if(t.name.size()>=suffix.size()&&t.name.compare(t.name.size()-suffix.size(),suffix.size(),suffix)==0)return &t;
    if(!sprite.textures.empty()) return &sprite.textures.front();
    return nullptr;
}

static const NsbmdTexture* findWalkFrame(const NsbmdMember& sprite,Dir d,bool moving,int phase){
    return findNumericTextureFrame(sprite,hg_walk_frame_number(static_cast<int>(d),moving,phase));
}

static const NsbmdTexture* findFollowerFrame(const NsbmdMember& sprite,Dir d,bool moving,int phase){
    return findNumericTextureFrame(sprite,hg_follower_frame_number(static_cast<int>(d),moving,phase));
}

static void drawOverworldSprite(RenderFrame& f,const NsbmdMember& sprite,float cx,float bottomY,Dir d,bool moving,double animClock,float light=1.0f,float scale=1.55f){
    int phase=hg_walk_phase(animClock);auto* t=findWalkFrame(sprite,d,moving,phase);if(!t)return;
    float w=t->width*scale,h=t->height*scale;drawTexturePixels(f,*t,cx-w*0.5f,bottomY-h,scale,light,false);
}

static void drawBdhcPlates(RenderFrame& f,const BdhcData& bdhc,float x,float y,float w,float h){
    if(!bdhc.valid||bdhc.plates.empty()) return;
    int minX=bdhc.plates.front().x,minY=bdhc.plates.front().y,maxX=minX,maxY=minY;
    for(auto const& p:bdhc.plates){
        int x0=std::min(p.x,p.x+p.width),x1=std::max(p.x,p.x+p.width);
        int y0=std::min(p.y,p.y+p.height),y1=std::max(p.y,p.y+p.height);
        minX=std::min(minX,x0);maxX=std::max(maxX,x1);minY=std::min(minY,y0);maxY=std::max(maxY,y1);
    }
    float ex=std::max(1,maxX-minX),ey=std::max(1,maxY-minY);
    float scale=std::min((w-8)/ex,(h-8)/ey);float ox=x+(w-ex*scale)*0.5f,oy=y+(h-ey*scale)*0.5f;
    const Color colors[7]={{0.30f,0.45f,0.95f,1},{0.20f,0.80f,0.35f,1},{0.30f,0.88f,0.90f,1},{0.85f,0.35f,0.85f,1},{0.90f,0.84f,0.30f,1},{0.85f,0.85f,0.85f,1},{0.95f,0.40f,0.12f,1}};
    rect(f,x,y,w,h,{0.06f,0.075f,0.09f,1});
    for(auto const& p:bdhc.plates){
        float px=ox+(std::min(p.x,p.x+p.width)-minX)*scale;
        float py=oy+(std::min(p.y,p.y+p.height)-minY)*scale;
        float pw=std::max(1.0f,std::abs(float(p.width))*scale),ph=std::max(1.0f,std::abs(float(p.height))*scale);
        Color c=colors[std::clamp(p.type,0,6)];float zLight=std::clamp(0.72f+p.z*0.025f,0.45f,1.18f);c=mul(c,zLight);
        rect(f,px,py,pw,ph,c);
        if(pw>5&&ph>5){rect(f,px,py,pw,1,{0.02f,0.03f,0.04f,1});rect(f,px,py,1,ph,{0.02f,0.03f,0.04f,1});}
    }
}

static std::array<float,3> projectNsbmdPoint(const NsbmdModel& model,Vec3f p,float vx,float vy,float vw,float vh,float yaw,float pitch){
    const float cx=(model.min.x+model.max.x)*0.5f, cy=(model.min.y+model.max.y)*0.5f, cz=(model.min.z+model.max.z)*0.5f;
    const float ex=std::max(0.001f,model.max.x-model.min.x), ey=std::max(0.001f,model.max.y-model.min.y), ez=std::max(0.001f,model.max.z-model.min.z);
    const float scale=0.80f*std::min(vw/std::max(ex,ez),vh/std::max(ey,0.45f*ez));
    const float sy=std::sin(yaw),cyaw=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    float x=p.x-cx,y=p.y-cy,z=p.z-cz;float xr=cyaw*x+sy*z;float zr=-sy*x+cyaw*z;float yr=cp*y-sp*zr;float zd=sp*y+cp*zr;
    return {vx+vw*0.5f+xr*scale,vy+vh*0.55f-yr*scale,zd};
}

static void drawNsbmdModel(RenderFrame& f,const NsbmdModel& model,const std::vector<NsbmdTexture>* textures,float vx,float vy,float vw,float vh,float yaw,float pitch){
    if(model.triangles.empty()) return;
    const float cx=(model.min.x+model.max.x)*0.5f, cy=(model.min.y+model.max.y)*0.5f, cz=(model.min.z+model.max.z)*0.5f;
    const float ex=std::max(0.001f,model.max.x-model.min.x), ey=std::max(0.001f,model.max.y-model.min.y), ez=std::max(0.001f,model.max.z-model.min.z);
    const float scale=0.80f*std::min(vw/std::max(ex,ez),vh/std::max(ey,0.45f*ez));
    const float sy=std::sin(yaw),cyaw=std::cos(yaw),sp=std::sin(pitch),cp=std::cos(pitch);
    auto project=[&](Vec3f p){float x=p.x-cx,y=p.y-cy,z=p.z-cz;float xr=cyaw*x+sy*z;float zr=-sy*x+cyaw*z;float yr=cp*y-sp*zr;float zd=sp*y+cp*zr;return std::array<float,3>{vx+vw*0.5f+xr*scale,vy+vh*0.55f-yr*scale,zd};};
    std::vector<ScreenTri> list; list.reserve(model.triangles.size());
    for(auto const& t:model.triangles){auto a=project(t.a.position),b=project(t.b.position),c=project(t.c.position);float area=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);if(std::fabs(area)<0.01f)continue;
        Vec3f e1{t.b.position.x-t.a.position.x,t.b.position.y-t.a.position.y,t.b.position.z-t.a.position.z},e2{t.c.position.x-t.a.position.x,t.c.position.y-t.a.position.y,t.c.position.z-t.a.position.z};
        Vec3f n{e1.y*e2.z-e1.z*e2.y,e1.z*e2.x-e1.x*e2.z,e1.x*e2.y-e1.y*e2.x};float nl=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);float light=0.86f;if(nl>0.0001f){n.x/=nl;n.y/=nl;n.z/=nl;light=std::clamp(0.58f+0.42f*(0.30f*n.x+0.86f*n.y+0.40f*n.z),0.32f,1.0f);}Color vc{(t.a.color.r+t.b.color.r+t.c.color.r)/3.0f,(t.a.color.g+t.b.color.g+t.c.color.g)/3.0f,(t.a.color.b+t.b.color.b+t.c.color.b)/3.0f,1};Color col=mul({t.materialColor.r*vc.r,t.materialColor.g*vc.g,t.materialColor.b*vc.b,1},light);ScreenTri st{{a[0],b[0],c[0]},{a[1],b[1],c[1]},(a[2]+b[2]+c[2])/3.0f,{t.a.uv,t.b.uv,t.c.uv},col,t.textureIndex,t.paletteIndex};list.push_back(st);}
    std::sort(list.begin(),list.end(),[](auto const& a,auto const& b){return a.z<b.z;});
    float step=(vw>700||vh>450)?5.0f:4.0f;
    for(auto const& t:list){
        float minx=std::max(vx,std::floor(std::min({t.x[0],t.x[1],t.x[2]}))),maxx=std::min(vx+vw,std::ceil(std::max({t.x[0],t.x[1],t.x[2]})));
        float miny=std::max(vy,std::floor(std::min({t.y[0],t.y[1],t.y[2]}))),maxy=std::min(vy+vh,std::ceil(std::max({t.y[0],t.y[1],t.y[2]})));
        float denom=(t.y[1]-t.y[2])*(t.x[0]-t.x[2])+(t.x[2]-t.x[1])*(t.y[0]-t.y[2]);if(std::fabs(denom)<0.0001f)continue;
        const NsbmdTexture* tex=(textures&&t.textureIndex>=0&&size_t(t.textureIndex)<textures->size())?&(*textures)[size_t(t.textureIndex)]:nullptr;
        for(float y=miny;y<=maxy;y+=step)for(float x=minx;x<=maxx;x+=step){float px=x+step*0.5f,py=y+step*0.5f;float w0=((t.y[1]-t.y[2])*(px-t.x[2])+(t.x[2]-t.x[1])*(py-t.y[2]))/denom;float w1=((t.y[2]-t.y[0])*(px-t.x[2])+(t.x[0]-t.x[2])*(py-t.y[2]))/denom;float w2=1.0f-w0-w1;if(w0<-0.001f||w1<-0.001f||w2<-0.001f)continue;Color c=t.color;
            if(tex){float u=w0*t.uv[0].x+w1*t.uv[1].x+w2*t.uv[2].x,v=w0*t.uv[0].y+w1*t.uv[1].y+w2*t.uv[2].y;Color tc=rgbaColor(*tex,t.paletteIndex,int(std::floor(u)),int(std::floor(v)));if(tc.a<0.08f)continue;c={c.r*tc.r,c.g*tc.g,c.b*tc.b,1};}
            rect(f,x,y,step+0.35f,step+0.35f,c);
        }
    }
}

// HeartGold's retail field camera is target-following and is parameterized by a
// map camera type.  The original engine keeps a 4:3 projection and follows the
// player target every frame.  We preserve that *vertical* composition at 720p
// while extending the horizontal field of view for widescreen PCs.
//
// World units here are the decoded HG map-model units: one 32x32 map chunk is
// 8 units wide, so one tile is 0.25 units.  At the retail DS top-screen scale,
// a 16-pixel tile becomes 60 pixels at 720p (720 / 192 = 3.75), i.e. 240 px per
// model unit at the camera target.
struct HgFieldCamera {
    std::uint8_t type=0;
    float pitch=0.6632251f;       // 38 degrees downward
    float fovY=0.3926991f;        // 22.5 degrees
    float targetPixelsPerUnit=240.0f;
    float anchorY=LH*(106.0f/192.0f); // retail-like player target position
    float spriteScale=3.75f;      // 32px DS OW sprite -> 120px at 720p
    float focal=1.0f;
    float distance=1.0f;
    float sinPitch=0.0f,cosPitch=1.0f;
    float targetX=0,targetY=0,targetZ=0;
};

static HgFieldCamera makeHgFieldCamera(std::uint8_t type,float targetX,float targetY,float targetZ){
    HgFieldCamera c;c.type=type;c.targetX=targetX;c.targetY=targetY;c.targetZ=targetZ;

    // Type 0 is the standard outdoor camera and type 4 is the standard indoor
    // camera in HG's map-header table. Rare camera types are special-location
    // variants. They retain the same target-follow model with conservative
    // type-specific framing until their entire retail preset table is named.
    switch(type){
        case 4: // standard interiors
            c.pitch=0.6981317f;          // 40 deg
            c.fovY=0.4188790f;           // 24 deg
            c.targetPixelsPerUnit=252.0f;
            c.anchorY=LH*(105.0f/192.0f);
            c.spriteScale=3.75f;
            break;
        case 3: case 8: case 12: case 13: // elevated/special field views
            c.pitch=0.7330383f;          // 42 deg
            c.fovY=0.4363323f;           // 25 deg
            c.targetPixelsPerUnit=225.0f;
            c.anchorY=LH*(104.0f/192.0f);
            c.spriteScale=3.65f;
            break;
        case 15: case 16: // special interiors / set pieces
            c.pitch=0.7504916f;          // 43 deg
            c.fovY=0.4363323f;
            c.targetPixelsPerUnit=240.0f;
            c.anchorY=LH*(104.0f/192.0f);
            c.spriteScale=3.70f;
            break;
        default:
            break;
    }
    c.focal=(LH*0.5f)/std::tan(c.fovY*0.5f);
    c.distance=c.focal/c.targetPixelsPerUnit;
    c.sinPitch=std::sin(c.pitch);c.cosPitch=std::cos(c.pitch);
    return c;
}

static std::array<float,3> projectWorldPoint(Vec3f p,float translateX,float translateY,float translateZ,const HgFieldCamera& camera){
    const float x=(p.x+translateX)-camera.targetX;
    const float y=(p.y+translateY)-camera.targetY;
    const float z=(p.z+translateZ)-camera.targetZ;

    // Camera is behind (+Z) and above its target, looking down toward it.
    // This mirrors the target/distance/angle construction used by HG's Camera.
    const float up=y*camera.cosPitch-z*camera.sinPitch;
    float depth=camera.distance-y*camera.sinPitch-z*camera.cosPitch;
    depth=std::max(depth,0.10f);
    const float perspective=camera.focal/depth;
    return {LW*0.5f+x*perspective,camera.anchorY-up*perspective,depth};
}

static float spriteScaleAtDepth(const HgFieldCamera& camera,float depth){
    if(depth<=0.10f)return camera.spriteScale;
    return std::clamp(camera.spriteScale*(camera.distance/depth),camera.spriteScale*0.58f,camera.spriteScale*1.55f);
}

struct PreparedFieldTriangle {
    const NsbmdTriangle* source=nullptr;
    std::array<float,3> a{},b{},c{};
    int minx=0,maxx=-1,miny=0,maxy=-1;
    int rasterStep=2,rasterStartX=0,rasterStartY=0;
    float denom=1.0f,ia=1.0f,ib=1.0f,ic=1.0f;
    Color base{};
    const std::vector<unsigned char>* texPixels=nullptr;
    int texW=0,texH=0;
    bool repeatS=false,repeatT=false,mirrorS=false,mirrorT=false;
    float animU=0.0f,animV=0.0f;
};

static void preparePlacedNsbmdModel(std::vector<PreparedFieldTriangle>& prepared,const NsbmdModel& model,const std::vector<NsbmdTexture>* textures,
                                    float translateX,float translateY,float translateZ,const HgFieldCamera& camera,float sceneLight,
                                    double animClock,std::uint16_t dynamicTextureType,
                                    std::uint16_t xr=0,std::uint16_t yr=0,std::uint16_t zr=0){
    if(model.triangles.empty())return;
    const float ms=std::isfinite(model.normalizedScale)&&model.normalizedScale>0.0001f?model.normalizedScale:1.0f;
    const PlacedRotation rotation=makePlacedRotation(xr,yr,zr);
    {
        bool allLeft=true,allRight=true,allAbove=true,allBelow=true;
        const float xs[2]={model.min.x*ms,model.max.x*ms};
        const float ys[2]={model.min.y*ms,model.max.y*ms};
        const float zs[2]={model.min.z*ms,model.max.z*ms};
        for(float xx:xs)for(float yy:ys)for(float zz:zs){
            auto rp=rotatePlaced({xx,yy,zz},rotation);
            auto q=projectWorldPoint(rp,translateX,translateY,translateZ,camera);
            allLeft &= q[0] < -48.0f; allRight &= q[0] > SoftwareRaster::W+48.0f;
            allAbove &= q[1] < -48.0f; allBelow &= q[1] > SoftwareRaster::H+48.0f;
        }
        if(allLeft||allRight||allAbove||allBelow)return;
    }
    prepared.reserve(prepared.size()+model.triangles.size());
    for(auto const& t:model.triangles){
        Vec3f pa=rotatePlaced({t.a.position.x*ms,t.a.position.y*ms,t.a.position.z*ms},rotation);
        Vec3f pb=rotatePlaced({t.b.position.x*ms,t.b.position.y*ms,t.b.position.z*ms},rotation);
        Vec3f pc=rotatePlaced({t.c.position.x*ms,t.c.position.y*ms,t.c.position.z*ms},rotation);
        auto a=projectWorldPoint(pa,translateX,translateY,translateZ,camera);
        auto b=projectWorldPoint(pb,translateX,translateY,translateZ,camera);
        auto c=projectWorldPoint(pc,translateX,translateY,translateZ,camera);
        if(a[2]<=0.10f||b[2]<=0.10f||c[2]<=0.10f)continue;
        float denom=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1]);
        if(std::fabs(denom)<0.001f)continue;
        int minx=std::max(0,int(std::floor(std::min({a[0],b[0],c[0]}))));
        int maxx=std::min(SoftwareRaster::W-1,int(std::ceil(std::max({a[0],b[0],c[0]}))));
        int miny=std::max(0,int(std::floor(std::min({a[1],b[1],c[1]}))));
        int maxy=std::min(SoftwareRaster::H-1,int(std::ceil(std::max({a[1],b[1],c[1]}))));
        if(minx>maxx||miny>maxy)continue;

        PreparedFieldTriangle q;q.source=&t;q.a=a;q.b=b;q.c=c;q.minx=minx;q.maxx=maxx;q.miny=miny;q.maxy=maxy;q.denom=denom;
        q.base=mul(t.rasterBaseColor,sceneLight);
        const NsbmdTexture* tex=(textures&&t.textureIndex>=0&&size_t(t.textureIndex)<textures->size())?&(*textures)[size_t(t.textureIndex)]:nullptr;
        const int animateAxis=(dynamicTextureType!=0&&tex)?fieldAnimatedTextureAxis(*tex):0;
        q.rasterStep=((maxx-minx)<6||(maxy-miny)<6)?1:2;
        q.rasterStartX=(minx/q.rasterStep)*q.rasterStep;
        q.rasterStartY=(miny/q.rasterStep)*q.rasterStep;
        q.ia=1.0f/a[2];q.ib=1.0f/b[2];q.ic=1.0f/c[2];
        if(tex){
            q.texPixels=&tex->rgba;
            if(t.paletteIndex>=0&&size_t(t.paletteIndex)<tex->paletteVariants.size())q.texPixels=&tex->paletteVariants[size_t(t.paletteIndex)];
            q.texW=int(tex->width);q.texH=int(tex->height);
            q.repeatS=(t.texImageParams&(1u<<16))!=0;q.repeatT=(t.texImageParams&(1u<<17))!=0;
            q.mirrorS=(t.texImageParams&(1u<<18))!=0;q.mirrorT=(t.texImageParams&(1u<<19))!=0;
            if(animateAxis){
                const float phase=float(std::fmod(animClock,16.0));
                if(animateAxis==1)q.animU=std::floor(phase*4.0f)*0.25f;
                else q.animV=std::floor(phase*6.0f)*0.25f;
            }
        }
        prepared.push_back(q);
    }
}

static void rasterPreparedFieldBand(SoftwareRaster& out,const std::vector<PreparedFieldTriangle>& prepared,int bandY0,int bandY1){
    for(auto const& q:prepared){
        if(q.maxy<bandY0||q.miny>=bandY1)continue;
        auto const& t=*q.source;const int step=q.rasterStep;
        int startY=q.rasterStartY;
        if(startY<bandY0)startY=((bandY0+step-1)/step)*step;
        const int endY=std::min(q.maxy,bandY1-1);
        if(startY>endY)continue;
        auto const& a=q.a;auto const& b=q.b;auto const& c=q.c;
        for(int y=startY;y<=endY;y+=step)for(int x=q.rasterStartX;x<=q.maxx;x+=step){
            float px=float(x)+0.5f*step,py=float(y)+0.5f*step;
            float w0=((b[1]-c[1])*(px-c[0])+(c[0]-b[0])*(py-c[1]))/q.denom;
            float w1=((c[1]-a[1])*(px-c[0])+(a[0]-c[0])*(py-c[1]))/q.denom;
            float w2=1.0f-w0-w1;
            if(w0<-0.0005f||w1<-0.0005f||w2<-0.0005f)continue;
            float invz=w0*q.ia+w1*q.ib+w2*q.ic;if(invz<=0.0f)continue;float z=1.0f/invz;
            Color cc=q.base;
            if(q.texPixels&&q.texW>0&&q.texH>0&&q.texPixels->size()==std::size_t(q.texW)*std::size_t(q.texH)*4){
                float u=(w0*t.a.uv.x*q.ia+w1*t.b.uv.x*q.ib+w2*t.c.uv.x*q.ic)/invz+q.animU;
                float v=(w0*t.a.uv.y*q.ia+w1*t.b.uv.y*q.ib+w2*t.c.uv.y*q.ic)/invz+q.animV;
                int sx=wrapTextureCoord(int(std::floor(u)),q.texW,q.repeatS,q.mirrorS);
                int sy=wrapTextureCoord(int(std::floor(v)),q.texH,q.repeatT,q.mirrorT);
                std::size_t p=(std::size_t(sy)*std::size_t(q.texW)+std::size_t(sx))*4;
                float ta=(*q.texPixels)[p+3]/255.0f;if(ta<0.08f)continue;
                cc={cc.r*((*q.texPixels)[p]/255.0f),cc.g*((*q.texPixels)[p+1]/255.0f),cc.b*((*q.texPixels)[p+2]/255.0f),cc.a*ta};
            }
            const bool depthWrite=(cc.a>=0.995f)||((t.polygonAttr&(1u<<11))!=0);
            out.putBlock(x,y,step,z,cc,depthWrite);
        }
    }
}

static std::pair<int,int> fieldRasterBand(unsigned worker,unsigned workers){
    const int band=(SoftwareRaster::H+int(workers)-1)/int(workers);
    int y0=int(worker)*band,y1=std::min(SoftwareRaster::H,int(worker+1)*band);
    if(worker>0)y0=(y0+1)&~1;
    if(worker+1<workers)y1&=~1;
    return {y0,y1};
}

class FieldRasterPool {
public:
    FieldRasterPool(){
        workers_=std::clamp(std::thread::hardware_concurrency(),1u,8u);
        for(unsigned i=1;i<workers_;++i)threads_.emplace_back([this,i]{workerLoop(i);});
    }
    ~FieldRasterPool(){
        {std::lock_guard<std::mutex> lock(mutex_);stop_=true;++generation_;}
        start_.notify_all();for(auto& t:threads_)if(t.joinable())t.join();
    }
    void render(SoftwareRaster& out,const std::vector<PreparedFieldTriangle>& prepared){
        if(workers_<=1||prepared.size()<256){rasterPreparedFieldBand(out,prepared,0,SoftwareRaster::H);return;}
        {
            std::lock_guard<std::mutex> lock(mutex_);out_=&out;prepared_=&prepared;pending_=workers_-1;++generation_;
        }
        start_.notify_all();
        auto [y0,y1]=fieldRasterBand(0,workers_);if(y0<y1)rasterPreparedFieldBand(out,prepared,y0,y1);
        std::unique_lock<std::mutex> lock(mutex_);done_.wait(lock,[this]{return pending_==0;});out_=nullptr;prepared_=nullptr;
    }
private:
    void workerLoop(unsigned id){
        std::uint64_t seen=0;
        for(;;){
            SoftwareRaster* out=nullptr;const std::vector<PreparedFieldTriangle>* prepared=nullptr;std::uint64_t gen=0;
            {
                std::unique_lock<std::mutex> lock(mutex_);start_.wait(lock,[&]{return stop_||generation_!=seen;});
                if(stop_)return;
                gen=generation_;out=out_;prepared=prepared_;
            }
            auto [y0,y1]=fieldRasterBand(id,workers_);if(out&&prepared&&y0<y1)rasterPreparedFieldBand(*out,*prepared,y0,y1);
            {
                std::lock_guard<std::mutex> lock(mutex_);seen=gen;if(pending_>0&&--pending_==0)done_.notify_one();
            }
        }
    }
    unsigned workers_=1;std::vector<std::thread> threads_;std::mutex mutex_;std::condition_variable start_,done_;
    bool stop_=false;std::uint64_t generation_=0;unsigned pending_=0;SoftwareRaster* out_=nullptr;const std::vector<PreparedFieldTriangle>* prepared_=nullptr;
};

static void rasterPreparedField(SoftwareRaster& out,const std::vector<PreparedFieldTriangle>& prepared){
    static FieldRasterPool pool;pool.render(out,prepared);
}

static void rasterSpriteTexture(SoftwareRaster& out,const NsbmdTexture* t,float cx,float bottomY,float depth,float light,float scale){
    if(!t||!t->width||!t->height)return;
    const int dw=std::max(1,int(std::round(t->width*scale))),dh=std::max(1,int(std::round(t->height*scale)));
    const int left=int(std::round(cx-dw*0.5f)),top=int(std::round(bottomY-dh));
    for(int dy=0;dy<dh;dy++){
        int sy=std::clamp(int(float(dy)/scale),0,int(t->height)-1);
        for(int dx=0;dx<dw;dx++){
            int sx=std::clamp(int(float(dx)/scale),0,int(t->width)-1);
            std::size_t q=(std::size_t(sy)*t->width+std::size_t(sx))*4;
            if(q+3>=t->rgba.size()||t->rgba[q+3]<24)continue;
            Color c{t->rgba[q]/255.0f,t->rgba[q+1]/255.0f,t->rgba[q+2]/255.0f,t->rgba[q+3]/255.0f};
            c=mul(c,light);c.a=t->rgba[q+3]/255.0f;
            out.put(left+dx,top+dy,std::max(0.101f,depth-0.0025f),c,c.a>=0.45f);
        }
    }
}

static void rasterOverworldSprite(SoftwareRaster& out,const NsbmdMember& sprite,float cx,float bottomY,float depth,Dir d,bool moving,double animClock,float light,float scale){
    int phase=hg_walk_phase(animClock);rasterSpriteTexture(out,findWalkFrame(sprite,d,moving,phase),cx,bottomY,depth,light,scale);
}

static void rasterFollowerSprite(SoftwareRaster& out,const NsbmdMember& sprite,float cx,float bottomY,float depth,Dir d,bool moving,double animClock,float light,float scale){
    int phase=hg_walk_phase(animClock);rasterSpriteTexture(out,findFollowerFrame(sprite,d,moving,phase),cx,bottomY,depth,light,scale);
}


[[maybe_unused]] static void drawPlacedNsbmdModel(RenderFrame& f,const NsbmdModel& model,const std::vector<NsbmdTexture>* textures,
                                 float translateX,float translateY,float translateZ,const HgFieldCamera& camera,float rasterStep=6.0f,float modelScale=1.0f){
    if(model.triangles.empty())return;
    std::vector<ScreenTri> list;list.reserve(model.triangles.size());
    for(auto const& t:model.triangles){
        Vec3f pa{t.a.position.x*modelScale,t.a.position.y*modelScale,t.a.position.z*modelScale},pb{t.b.position.x*modelScale,t.b.position.y*modelScale,t.b.position.z*modelScale},pc{t.c.position.x*modelScale,t.c.position.y*modelScale,t.c.position.z*modelScale};
        auto a=projectWorldPoint(pa,translateX,translateY,translateZ,camera);
        auto b=projectWorldPoint(pb,translateX,translateY,translateZ,camera);
        auto c=projectWorldPoint(pc,translateX,translateY,translateZ,camera);
        float minx=std::min({a[0],b[0],c[0]}),maxx=std::max({a[0],b[0],c[0]}),miny=std::min({a[1],b[1],c[1]}),maxy=std::max({a[1],b[1],c[1]});
        if(maxx<0||minx>LW||maxy<70||miny>LH-34)continue;
        float area=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);if(std::fabs(area)<0.02f)continue;
        Vec3f e1{t.b.position.x-t.a.position.x,t.b.position.y-t.a.position.y,t.b.position.z-t.a.position.z};
        Vec3f e2{t.c.position.x-t.a.position.x,t.c.position.y-t.a.position.y,t.c.position.z-t.a.position.z};
        Vec3f n{e1.y*e2.z-e1.z*e2.y,e1.z*e2.x-e1.x*e2.z,e1.x*e2.y-e1.y*e2.x};float nl=std::sqrt(n.x*n.x+n.y*n.y+n.z*n.z);float light=0.82f;if(nl>0.0001f){n.x/=nl;n.y/=nl;n.z/=nl;light=std::clamp(0.56f+0.44f*(0.25f*n.x+0.91f*n.y+0.32f*n.z),0.34f,1.0f);}
        Color vc{(t.a.color.r+t.b.color.r+t.c.color.r)/3.0f,(t.a.color.g+t.b.color.g+t.c.color.g)/3.0f,(t.a.color.b+t.b.color.b+t.c.color.b)/3.0f,1};
        Color col=mul({t.materialColor.r*vc.r,t.materialColor.g*vc.g,t.materialColor.b*vc.b,1},light);
        list.push_back({{a[0],b[0],c[0]},{a[1],b[1],c[1]},(a[2]+b[2]+c[2])/3.0f,{t.a.uv,t.b.uv,t.c.uv},col,t.textureIndex,t.paletteIndex});
    }
    std::sort(list.begin(),list.end(),[](auto const& a,auto const& b){return a.z>b.z;});
    const float step=std::max(4.0f,rasterStep);
    for(auto const& t:list){
        float minx=std::max(0.0f,std::floor(std::min({t.x[0],t.x[1],t.x[2]}))),maxx=std::min(LW,std::ceil(std::max({t.x[0],t.x[1],t.x[2]})));
        float miny=std::max(70.0f,std::floor(std::min({t.y[0],t.y[1],t.y[2]}))),maxy=std::min(LH-34,std::ceil(std::max({t.y[0],t.y[1],t.y[2]})));
        float denom=(t.y[1]-t.y[2])*(t.x[0]-t.x[2])+(t.x[2]-t.x[1])*(t.y[0]-t.y[2]);if(std::fabs(denom)<0.0001f)continue;
        const NsbmdTexture* tex=(textures&&t.textureIndex>=0&&size_t(t.textureIndex)<textures->size())?&(*textures)[size_t(t.textureIndex)]:nullptr;
        for(float y=miny;y<=maxy;y+=step)for(float x=minx;x<=maxx;x+=step){float px=x+step*0.5f,py=y+step*0.5f;float w0=((t.y[1]-t.y[2])*(px-t.x[2])+(t.x[2]-t.x[1])*(py-t.y[2]))/denom;float w1=((t.y[2]-t.y[0])*(px-t.x[2])+(t.x[0]-t.x[2])*(py-t.y[2]))/denom;float w2=1.0f-w0-w1;if(w0<-0.001f||w1<-0.001f||w2<-0.001f)continue;Color cc=t.color;
            if(tex){float u=w0*t.uv[0].x+w1*t.uv[1].x+w2*t.uv[2].x,v=w0*t.uv[0].y+w1*t.uv[1].y+w2*t.uv[2].y;Color tc=rgbaColor(*tex,t.paletteIndex,int(std::floor(u)),int(std::floor(v)));if(tc.a<0.08f)continue;cc={cc.r*tc.r,cc.g*tc.g,cc.b*tc.b,1};}
            rect(f,x,y,step+0.4f,step+0.4f,cc);
        }
    }
}

}

struct NativeGame::Impl {
    std::filesystem::path assets;
    std::filesystem::path savePath;
    HgRomWorld romWorld;
    bool romWorldReady=false;
    std::map<int,NsbmdMember> romFieldBuildings;
    std::map<int,NsbmdMember> romRoomBuildings;
    mutable std::map<int,NsbmdMember> romNpcSprites;
    AssetStats stats{};
    bool assetsReady=false;
    std::vector<MapDef> maps;
    int mapIndex=0;
    int tx=16,ty=11;
    float rx=16.0f,ry=11.0f;
    int fromX=16,fromY=11,toX=16,toY=11;
    float moveProgress=1.0f;
    struct FieldTransition {
        enum class Phase { None, DoorOpening, DoorOpenHold, AutoStep, DoorClosing, FadeOut, FadeIn, DestinationAutoStep, DestinationDoorHold, DestinationDoorClosing };
        Phase phase=Phase::None;
        double clock=0.0;
        HgWarpEvent warp{};
        int sourceMap=-1;
        int sourceDoorX=0,sourceDoorY=0;
        int destinationMap=-1;
        int destinationDoorX=0,destinationDoorY=0;
        bool sourceDoor=false;
        bool destinationDoor=false;
        bool sourceInterior=false;
        bool destinationInterior=false;
        Dir destinationWalkDir=Dir::Down;
        int destinationWalkSteps=0;
        float doorAmount=0.0f;
        float fade=0.0f;
        bool active() const { return phase!=Phase::None; }
        void clear(){*this=FieldTransition{};}
    } fieldTransition;
    Dir facing=Dir::Down;
    Mode mode=Mode::Intro;
    bool debug=false;
    bool debugWalkThroughWalls=false;
    bool playerLedgeJump=false;
    bool quit=false;
    double playSeconds=0.0;
    double fieldAnimClock=0.0;
    double npcClock=0.0;
    std::vector<std::string> dialogue;
    std::string speaker;
    std::size_t dialoguePage=0;
    int menuIndex=0;
    std::string toast;
    double toastTime=0.0;
    AssetSource assetSource=AssetSource::Field;
    std::size_t assetIndex=1;
    std::size_t assetMemberCount=0;
    NsbmdMember assetMember{};
    LandChunk assetLandChunk{};
    float assetYaw=0.55f;
    float assetPitch=0.28f;
    std::vector<NsbmdMember> worldProps;
    NsbmdMember playerSprite{}, heroineSprite{}, boySprite{}, doctorSprite{}, aideSprite{}, momSprite{}, picnicSprite{};
    std::array<NsbmdMember,3> starterSprites{};
    NsbmdMember starterBallSprite{};
    std::size_t authenticSpriteSets=0;
    std::size_t spriteArchiveMembers=0;
    std::size_t spriteViewMember=69;
    int spriteViewFrame=0;
    NsbmdMember spriteViewResource{};
    LandChunk land60Chunk{};
    NitroRgbaImage titleSun{}, titleLogo{}, titleFooter{};
    NsbmdMember titleHooh{}, titleSparkle{};
    HgMessageBank titleMessages{};
    HgGameState gameState{};
    HgScriptVm scriptVm{};
    std::vector<HgGlobalScriptEntry> globalScriptTable;
    std::unordered_map<std::uint16_t,HgMessageBank> scriptMessageCache;
    HgScriptHeader scriptHeader{};
    int scriptHeaderMap=-1;
    std::deque<std::uint16_t> pendingMapScripts;
    HgSdat sdat{};
    NativeAudio audio{};
    std::uint16_t currentBgm=0;
    std::uint16_t lastSfxId=0;
    double audioClock=0.0,lastSfxClock=-1000.0;
    bool fanfarePausedBgm=false;
    bool resumeBgmAfterSoundWait=false;
    std::unordered_map<std::uint16_t,HgPcmWave> bgmCache;
    std::deque<std::uint16_t> bgmCacheOrder;
    std::unordered_map<std::uint16_t,HgPcmWave> sfxCache;
    std::deque<std::uint16_t> sfxCacheOrder;
    std::uint16_t currentDynamicTextureType=0;
    struct IntroMovieScene { std::vector<NitroRgbaImage> frames; double duration=5.0; };
    std::vector<IntroMovieScene> introMovie;
    std::array<NsbmdMember,3> introMapModels{};
    std::vector<NitroRgbaImage> introHoohCells, introLugiaCells, introRivalCells, introHeroCells, introGrassCells, introTouchCells;
    NitroNanrBank introHoohAnim, introLugiaAnim, introRivalAnim, introHeroAnim, introGrassAnim, introTouchAnim;
    double introClock=0.0;
    std::size_t introScene=0;
    HgMessageBank mainMenuMessages{};
    HgMessageBank newGameMessages{};
    NitroRgbaImage redGyarados{};
    // The retail new-game/Oak application uses demo/intro/intro.narc.  Keep
    // decoded authored BG states separately from the unrelated opening.narc
    // movie resources.
    std::vector<NitroRgbaImage> newGameTvFrames;
    std::vector<NitroRgbaImage> newGameOakFrames;
    NitroRgbaImage newGameOakChar{}, newGameMarillChar{}, newGameBoyPose{}, newGameGirlPose{};
    std::vector<NitroRgbaImage> newGameOakCells, newGameBoyCells, newGameGirlCells, newGameMarillCells;
    bool newGameRetailAssets=false;
    int mainMenuIndex=0;
    int newGameStage=0;
    int newGameGender=0;
    int newGameNameCursor=0;
    std::string newGameName{};
    double newGameClock=0.0;
    int savePromptIndex=0;
    struct RuntimeNpc { float x=0,y=0,homeX=0,homeY=0,startX=0,startY=0,targetX=0,targetY=0; Dir facing=Dir::Down; double timer=0; float move=1.0f; int phase=0; };
    std::unordered_map<int,RuntimeNpc> runtimeNpcs;
    int runtimeNpcMap=-1;
    std::uint32_t rngState=0x48474f4cu;
    int stepsSinceEncounter=0;
    // Reused every field frame; avoids allocating several MB of prepared triangle
    // records while the camera is moving. Render remains single-entry on the host.
    mutable std::vector<PreparedFieldTriangle> fieldPreparedScratch;
    struct BattleData {
        bool active=false; bool trainer=false; std::uint16_t trainerId=0; std::uint8_t trainerClass=0;
        HgMon enemy{}; std::vector<HgMon> enemyParty; std::size_t enemyIndex=0;
        int menu=0,moveMenu=0; bool choosingMove=false; int runAttempts=0;
        std::array<int,5> playerStages{},enemyStages{}; std::string message; bool awaiting=false;
        double sceneClock=0.0; double actionClock=0.0; double transitionClock=0.0;
        // StartEncounter.s is command-driven in retail. Keep its encounter/message/
        // enemy-send-out/player-send-out stages separate instead of running the
        // trainer back animation from battle frame zero.
        int introPhase=0; double introClock=0.0;
        std::uint8_t retailEffect=42; std::uint16_t transitionId=0xffff; std::uint16_t battleBgm=1116;
        // A Gen IV turn is presented one action at a time. turnStep is:
        // 0 idle, 1 first action message, 2 second action message,
        // 3 end-of-turn residual message, 4 faint message.
        int turnStep=0; int pendingPlayerSlot=-1,pendingEnemySlot=-1;
        bool secondActorPlayer=false; bool actionByPlayer=true; bool experienceAwarded=false;
        std::uint16_t currentMove=0,currentMoveEffect=0; std::uint8_t currentMoveType=0,currentMoveCategory=2;
    } battle;
    RenderFrame battleEntryFrame{}; bool battleEntryFrameValid=false;
    mutable std::unordered_map<std::uint32_t,NitroRgbaImage> battleSpriteCache;
    mutable std::unordered_map<int,NitroRgbaImage> battleBackdropCache;
    mutable std::unordered_map<int,NitroRgbaImage> battleTerrainEnemyCache;
    mutable std::unordered_map<int,NitroRgbaImage> battleTerrainPlayerCache;
    mutable std::unordered_map<int,std::vector<NitroRgbaImage>> battleTrainerFrontCache;
    std::vector<NitroRgbaImage> battleBoyBackCells,battleGirlBackCells;
    NitroNanrBank battleBoyBackAnim{},battleGirlBackAnim{};
    struct BattleFxPack { std::vector<NitroRgbaImage> cells; NitroNanrBank anim; };
    mutable std::unordered_map<int,BattleFxPack> battleFxCache;
    NitroRgbaImage battleEnemyHpBox{},battlePlayerHpBox{};
    NitroRgbaImage battleMenuMessage{},battleMenuMain{},battleMenuFight{};
    int starterIndex=0;
    int partyIndex=0;
    int pcIndex=0;
    bool pcPartySide=false;
    bool partyReturnBattle=false;
    bool partySelectForScript=false;
    int summaryIndex=0;
    bool summaryReturnToScript=false;
    enum class NameTarget { None, Player, Rival, Nickname };
    NameTarget nameTarget=NameTarget::None;
    int appNameCursor=0;
    std::string appName{};
    std::uint16_t appNameResultVar=0;
    std::size_t appNamePartySlot=0;
    bool appResumeScript=false;
    struct MartEntry { std::uint16_t id=0; std::uint32_t price=0; std::uint16_t slot=0; bool sold=false; };
    std::vector<MartEntry> martEntries;
    int martIndex=0;
    int martQuantity=1;
    std::uint16_t martId=0;
    std::uint16_t martOpcode=0;
    bool martSelling=false;
    bool martAthlete=false;
    bool martDataCards=false;
    bool resumeScriptAfterBattle=false;
    std::uint16_t frameScriptLatch=0;
    bool resumeScriptAfterDialogue=false;
    double scriptWaitSeconds=0.0;
    std::uint16_t scriptWaitVar=0;
    bool scriptWaitingInput=false;
    std::uint16_t scriptWaitInputVar=0;
    bool scriptWaitingMovement=false;
    bool scriptObjectsLockedAll=false;
    std::unordered_set<int> scriptLockedObjects;
    std::unordered_set<int> scriptHiddenObjects;
    std::unordered_map<int,std::uint16_t> scriptMovementOverrides;
    std::unordered_map<int,std::deque<Dir>> scriptNpcMoves;
    std::deque<Dir> retailPlayerMoves;
    std::uint16_t scriptChoiceDest=0;
    bool scriptChoiceCanCancel=false;
    int scriptChoiceIndex=0;
    std::vector<std::pair<std::string,std::uint16_t>> scriptChoiceOptions;
    std::uint16_t bankAmountDest=0;
    std::uint32_t bankAmount=0,bankAmountMax=0;
    bool bankDeposit=true;
    std::uint16_t activeScriptNumber=0;
    std::size_t scriptMessagesShown=0;
    std::uint16_t coordinateTriggerLatch=0;
    std::uint16_t elmLabSceneVar=0;
    std::uint16_t newBarkSceneVar=0;
    std::uint16_t elmLabPreventFlag=0;
    std::uint16_t gotStarterFlag=0;
    std::uint16_t pokegearFlag=0;
    std::uint16_t newBarkMomDoorHideFlag=0;
    std::deque<Dir> scriptedPlayerMoves;
    int nativeLabPhase=0;
    int nativeDialogueAction=0;
    bool nativeStarterSelection=false;
    bool retailStarterSelectionPending=false;
    bool scriptFollowerMovementEnabled=true;
    int activeFacingNpcId=-1;
    int activeBackgroundId=-1;
    Dir activeFacingNpcDir=Dir::Down;
    double titleClock=0.0;
    int terrainX=0,terrainY=0;
    float terrainHeight=1.0f;

    Impl(std::filesystem::path a,std::filesystem::path s):assets(std::move(a)),savePath(std::move(s)),romWorld(assets){
        maps={makeTown(),makeRoute(),makeLab(),makeHouse()};
        hg_initialize_pokemon_database(assets);
        scriptVm.bindState(&gameState);
        globalScriptTable=load_hg_global_script_table(assets);
    }
    void recoverRetailProgressionFlags(){
        // Recover the numeric story flags from the bundled retail script banks.
        // This keeps the PC port tied to the extracted ROM rather than duplicating
        // region-specific constants in native code.
        if(!pokegearFlag){auto bank=load_hg_script_bank(assets,842);if(auto f=hgFirstCheckedFlag(bank,3))pokegearFlag=*f;}
        if(!gotStarterFlag||!elmLabPreventFlag){auto bank=load_hg_script_bank(assets,843);if(!gotStarterFlag)if(auto f=hgFirstCheckedFlag(bank,13))gotStarterFlag=*f;if(!elmLabPreventFlag)if(auto f=hgFirstCheckedFlag(bank,12))elmLabPreventFlag=*f;}
    }
    bool hasPokegear() const { return pokegearFlag!=0 && gameState.flag(pokegearFlag); }
    MapDef& map(){return maps[static_cast<std::size_t>(mapIndex)];}
    const MapDef& map() const{return maps[static_cast<std::size_t>(mapIndex)];}
    bool npcVisible(const HgOverworldEvent& o) const { return scriptHiddenObjects.count(int(o.id))==0 && (o.flag==0 || !gameState.flag(o.flag)); }
    const HgOverworldEvent* npcEventAt(int x,int y) const {
        if(!romWorldReady)return nullptr;
        for(auto const& o:romWorld.events().overworlds){
            if(!npcVisible(o))continue;
            auto it=runtimeNpcs.find(int(o.id));
            if(it==runtimeNpcs.end())continue;
            // The opposite-gender friend is authored on Elm's Lab warp tile
            // (4,14). After the starter has been obtained, never allow stale
            // visibility state to soft-lock that exit; during its retail
            // cutscene player input is script-locked anyway.
            if(romWorld.mapId()==61 && gameState.gotStarter && x==4 && y==14 &&
               o.x==4 && o.y==14 && o.flag!=0)continue;
            if(int(std::round(it->second.x))==x&&int(std::round(it->second.y))==y)return &o;
        }
        return nullptr;
    }
    bool npcAt(int x,int y) const {
        if(romWorldReady)return npcEventAt(x,y)!=nullptr;
        for(auto const& n:map().npcs)if(n.x==x&&n.y==y)return true;
        return false;
    }
    const HgOverworldEvent* interactionNpcAhead(int& targetX,int& targetY) const {
        auto [dx,dy]=delta(facing);
        targetX=tx+dx;targetY=ty+dy;
        if(auto const* o=npcEventAt(targetX,targetY))return o;
        // Retail HG/SS has a special through-counter object lookup. The front
        // tile remains collision-blocked, but behavior 0x80 tells field input to
        // continue one tile farther in the facing direction. Nurses, Mart clerks,
        // department-store cashiers and other service counters depend on this.
        auto const* permission=romWorld.permissionAt(targetX,targetY);
        if(permission&&hg_permission_is_counter(*permission)){
            int beyondX=targetX+dx,beyondY=targetY+dy;
            if(auto const* o=npcEventAt(beyondX,beyondY)){targetX=beyondX;targetY=beyondY;return o;}
        }
        return nullptr;
    }
    void syncFromRomWorld(){tx=romWorld.x();ty=romWorld.y();rx=float(tx);ry=float(ty);fromX=toX=tx;fromY=toY=ty;moveProgress=1.0f;}
    static bool isDoorModel(const NsbmdMember& m){
        if(m.models.empty())return false;
        std::string n=m.models.front().name;for(char& c:n)c=char(std::tolower(static_cast<unsigned char>(c)));
        return n.find("door")!=std::string::npos;
    }
    static std::pair<float,float> buildingTilePosition(const HgLoadedChunk& c,const HgBuildingPlacement& bp){
        // Placed-building coordinates are centered on the 32x32 matrix cell. Convert
        // back to global retail tile coordinates so door geometry can be associated
        // with the event warp that drives it.
        const float x=float(c.matrixX*32+16)+float(bp.x)+float(bp.xFraction)/65536.0f;
        const float y=float(c.matrixY*32+16)+float(bp.z)+float(bp.zFraction)/65536.0f;
        return {x,y};
    }
    bool doorNearCurrentWarp(int wx,int wy) const{
        if(!romWorldReady)return false;
        const bool interior=romWorld.header()&&romWorld.header()->mapType==4;
        auto const& cache=interior?romRoomBuildings:romFieldBuildings;
        for(auto const& c:romWorld.visibleChunks())for(auto const& bp:c.land.buildings){
            auto it=cache.find(int(bp.modelId));if(it==cache.end()||!isDoorModel(it->second))continue;
            auto [bx,by]=buildingTilePosition(c,bp);
            // HG/SS house/lab doors are separate placed models whose hinge origin can
            // sit roughly one tile to the side of the actual warp cell.
            if(std::fabs(bx-float(wx))<=1.75f&&std::fabs(by-float(wy))<=1.25f)return true;
        }
        return false;
    }
    bool buildingDoorMatches(const HgLoadedChunk& c,const HgBuildingPlacement& bp,const NsbmdMember& m) const{
        if(!fieldTransition.active()||!isDoorModel(m)||fieldTransition.doorAmount<=0.001f)return false;
        int map=romWorld.mapId(),wx=0,wy=0;
        if(map==fieldTransition.sourceMap){wx=fieldTransition.sourceDoorX;wy=fieldTransition.sourceDoorY;}
        else if(map==fieldTransition.destinationMap){wx=fieldTransition.destinationDoorX;wy=fieldTransition.destinationDoorY;}
        else return false;
        auto [bx,by]=buildingTilePosition(c,bp);
        return std::fabs(bx-float(wx))<=1.75f&&std::fabs(by-float(wy))<=1.25f;
    }
    const HgWarpEvent* warpAt(int x,int y) const{
        if(!romWorldReady)return nullptr;
        for(auto const& w:romWorld.events().warps)if(w.x==x&&w.y==y)return &w;
        return nullptr;
    }
    bool isBuildingTransition(const HgWarpEvent& w) const{
        if(!romWorldReady||!romWorld.header())return false;
        auto const* dst=hg_map_header(w.targetMap);if(!dst)return false;
        const bool srcInterior=romWorld.header()->mapType==4,dstInterior=dst->mapType==4;
        return srcInterior!=dstInterior;
    }
    void beginTransitionStep(){
        fromX=tx;fromY=ty;toX=fieldTransition.warp.x;toY=fieldTransition.warp.y;moveProgress=0.0f;
    }
    bool beginBuildingTransition(const HgWarpEvent& w,Dir d){
        if(fieldTransition.active()||!isBuildingTransition(w))return false;
        fieldTransition.clear();fieldTransition.warp=w;fieldTransition.sourceMap=romWorld.mapId();
        fieldTransition.sourceInterior=romWorld.header()&&romWorld.header()->mapType==4;
        fieldTransition.sourceDoorX=w.x;fieldTransition.sourceDoorY=w.y;fieldTransition.sourceDoor=doorNearCurrentWarp(w.x,w.y);
        fieldTransition.destinationMap=w.targetMap;
        if(auto const* dh=hg_map_header(w.targetMap))fieldTransition.destinationInterior=dh->mapType==4;
        if(auto const* h=hg_map_header(w.targetMap)){
            auto ev=load_hg_event_bank(assets,h->eventsBank);
            if(ev.valid&&w.targetWarp<ev.warps.size()){
                auto const& dw=ev.warps[w.targetWarp];fieldTransition.destinationDoorX=dw.x;fieldTransition.destinationDoorY=dw.y;
            }
        }
        facing=d;fieldTransition.clock=0.0;fieldTransition.fade=0.0f;fieldTransition.doorAmount=0.0f;
        if(fieldTransition.sourceDoor)fieldTransition.phase=FieldTransition::Phase::DoorOpening;
        else{fieldTransition.phase=FieldTransition::Phase::AutoStep;beginTransitionStep();}
        return true;
    }
    void finishTransitionStep(){
        tx=toX;ty=toY;rx=float(tx);ry=float(ty);moveProgress=1.0f;gameState.onPlayerStep();
        if(romWorldReady){int ocx=romWorld.currentCellX(),ocy=romWorld.currentCellY(),omid=romWorld.mapId();romWorld.commitPosition(tx,ty);if(romWorld.currentCellX()!=ocx||romWorld.currentCellY()!=ocy||romWorld.mapId()!=omid)refreshRomVisualAssets();}
    }
    bool beginDestinationExitStep(){
        if(fieldTransition.destinationWalkSteps<=0||!romWorldReady)return false;
        auto [dx,dy]=delta(fieldTransition.destinationWalkDir);
        int nx=tx+dx,ny=ty+dy;
        if(!romWorld.canMoveTo(nx,ny)||npcAt(nx,ny)){fieldTransition.destinationWalkSteps=0;return false;}
        facing=fieldTransition.destinationWalkDir;fromX=tx;fromY=ty;toX=nx;toY=ny;moveProgress=0.0f;return true;
    }
    void updateFieldTransition(double dt){
        using P=FieldTransition::Phase;
        constexpr double doorOpenSeconds=0.20,doorHoldSeconds=0.12,doorCloseSeconds=0.18,fadeOutSeconds=0.26,fadeInSeconds=0.30;
        if(!fieldTransition.active())return;
        if(fieldTransition.phase==P::DoorOpening){
            fieldTransition.clock+=dt;float q=float(std::clamp(fieldTransition.clock/doorOpenSeconds,0.0,1.0));q=q*q*(3.0f-2.0f*q);fieldTransition.doorAmount=q;
            if(fieldTransition.clock>=doorOpenSeconds){fieldTransition.doorAmount=1.0f;fieldTransition.clock=0.0;fieldTransition.phase=P::DoorOpenHold;}
            return;
        }
        if(fieldTransition.phase==P::DoorOpenHold){
            // Retail doors visibly finish opening before the automatic player step.
            // Holding the panel for a few frames prevents the character from
            // clipping the closing geometry while keeping the transition snappy.
            fieldTransition.doorAmount=1.0f;fieldTransition.clock+=dt;
            if(fieldTransition.clock>=doorHoldSeconds){fieldTransition.clock=0.0;fieldTransition.phase=P::AutoStep;beginTransitionStep();}
            return;
        }
        if(fieldTransition.phase==P::AutoStep){
            moveProgress=std::min(1.0f,moveProgress+float(dt)*5.0f);float q=moveProgress;rx=float(fromX)+(toX-fromX)*q;ry=float(fromY)+(toY-fromY)*q;
            if(moveProgress>=1.0f){finishTransitionStep();fieldTransition.clock=0.0;if(fieldTransition.sourceDoor)fieldTransition.phase=P::DoorClosing;else fieldTransition.phase=P::FadeOut;}
            return;
        }
        if(fieldTransition.phase==P::DoorClosing){
            fieldTransition.clock+=dt;float q=float(std::clamp(fieldTransition.clock/doorCloseSeconds,0.0,1.0));q=q*q*(3.0f-2.0f*q);fieldTransition.doorAmount=1.0f-q;
            if(fieldTransition.clock>=doorCloseSeconds){fieldTransition.doorAmount=0.0f;fieldTransition.clock=0.0;fieldTransition.phase=P::FadeOut;}
            return;
        }
        if(fieldTransition.phase==P::FadeOut){
            fieldTransition.clock+=dt;fieldTransition.fade=float(std::clamp(fieldTransition.clock/fadeOutSeconds,0.0,1.0));
            if(fieldTransition.clock>=fadeOutSeconds){
                fieldTransition.fade=1.0f;
                if(!romWorld.useWarpAt(fieldTransition.warp.x,fieldTransition.warp.y)){showToast("BUILDING WARP FAILED");fieldTransition.clear();return;}
                // Generic warps spawn one tile off their destination to prevent an
                // immediate bounce. A controlled building exit owns movement, so
                // put the player back on the authored exterior warp/door cell and
                // visibly walk the two clearance steps instead of silently
                // consuming the first one inside the warp resolver.
                if(fieldTransition.sourceInterior&&!fieldTransition.destinationInterior)
                    romWorld.commitPosition(fieldTransition.destinationDoorX,fieldTransition.destinationDoorY);
                onMapEntered(true);showToast(romWorld.locationName());
                const bool targetOutdoor=romWorld.header()&&romWorld.header()->mapType!=4;
                fieldTransition.destinationDoor=targetOutdoor&&doorNearCurrentWarp(fieldTransition.destinationDoorX,fieldTransition.destinationDoorY);
                fieldTransition.doorAmount=fieldTransition.destinationDoor?1.0f:0.0f;
                // Retail exits carry the player clear of the doorway instead of
                // handing control back while the character is still visually
                // inside the house facade. The warp resolver already places the
                // player one safe tile off the destination warp; for an
                // interior->exterior transition, continue two authored-looking
                // walk steps in the direction away from the outside door.
                if(fieldTransition.sourceInterior&&targetOutdoor){
                    int dx=tx-fieldTransition.destinationDoorX,dy=ty-fieldTransition.destinationDoorY;
                    if(std::abs(dx)>std::abs(dy)&&dx!=0)fieldTransition.destinationWalkDir=dx<0?Dir::Left:Dir::Right;
                    else if(dy!=0)fieldTransition.destinationWalkDir=dy<0?Dir::Up:Dir::Down;
                    else fieldTransition.destinationWalkDir=facing;
                    fieldTransition.destinationWalkSteps=2;
                }else fieldTransition.destinationWalkSteps=0;
                fieldTransition.clock=0.0;fieldTransition.phase=P::FadeIn;
            }
            return;
        }
        if(fieldTransition.phase==P::FadeIn){
            fieldTransition.clock+=dt;fieldTransition.fade=1.0f-float(std::clamp(fieldTransition.clock/fadeInSeconds,0.0,1.0));
            if(fieldTransition.clock>=fadeInSeconds){
                fieldTransition.fade=0.0f;fieldTransition.clock=0.0;
                if(fieldTransition.destinationWalkSteps>0&&beginDestinationExitStep())fieldTransition.phase=P::DestinationAutoStep;
                else if(fieldTransition.destinationDoor)fieldTransition.phase=P::DestinationDoorHold;
                else fieldTransition.clear();
            }
            return;
        }
        if(fieldTransition.phase==P::DestinationAutoStep){
            moveProgress=std::min(1.0f,moveProgress+float(dt)*5.0f);float q=moveProgress;rx=float(fromX)+(toX-fromX)*q;ry=float(fromY)+(toY-fromY)*q;
            if(moveProgress>=1.0f){
                finishTransitionStep();fieldTransition.destinationWalkSteps--;fieldTransition.clock=0.0;
                if(fieldTransition.destinationWalkSteps>0&&beginDestinationExitStep())return;
                if(fieldTransition.destinationDoor)fieldTransition.phase=P::DestinationDoorHold;else fieldTransition.clear();
            }
            return;
        }
        if(fieldTransition.phase==P::DestinationDoorHold){
            fieldTransition.doorAmount=1.0f;fieldTransition.fade=0.0f;fieldTransition.clock+=dt;
            if(fieldTransition.clock>=doorHoldSeconds){fieldTransition.clock=0.0;fieldTransition.phase=P::DestinationDoorClosing;}
            return;
        }
        if(fieldTransition.phase==P::DestinationDoorClosing){
            fieldTransition.clock+=dt;float q=float(std::clamp(fieldTransition.clock/doorCloseSeconds,0.0,1.0));q=q*q*(3.0f-2.0f*q);fieldTransition.doorAmount=1.0f-q;fieldTransition.fade=0.0f;
            if(fieldTransition.clock>=doorCloseSeconds)fieldTransition.clear();
        }
    }
    void refreshRomVisualAssets(){
        if(!romWorldReady)return;
        currentDynamicTextureType=0;
        if(auto* h=romWorld.header()){auto area=load_hg_area_data(assets,h->areaDataBank);if(area.valid)currentDynamicTextureType=area.dynamicTextureType;}
        auto fieldArc=assets/"fielddata/build_model/bm_field.narc";
        auto roomArc=assets/"fielddata/build_model/bm_room.narc";
        for(auto const& c:romWorld.visibleChunks()){
            bool interior=false;if(auto* h=hg_map_header(c.mapId))interior=h->mapType==4;
            auto& cache=interior?romRoomBuildings:romFieldBuildings;auto const& arc=interior?roomArc:fieldArc;
            for(auto const& b:c.land.buildings){int id=int(b.modelId);if(cache.find(id)==cache.end()){auto m=load_nsbmd_from_narc(arc,std::size_t(id));if(m.valid)cache.emplace(id,std::move(m));}}
        }
        for(auto const& o:romWorld.events().overworlds){
            if(!npcVisible(o))continue;
            std::uint16_t resolvedSprite=0;
            const int member=hgss::resolveMmodelMember(o.model,scriptVm.vars(),&resolvedSprite);
            // Dynamic graphics slots are initialized by retail map/script state.  If a
            // slot has not been initialized yet, leave it unresolved rather than
            // loading an unrelated archive member.  SPRITE_VAR_1 has a gender-aware
            // draw-time fallback for the early friend cutscene below.
            if(member<0||std::size_t(member)>=spriteArchiveMembers)continue;
            if(romNpcSprites.find(int(resolvedSprite))!=romNpcSprites.end())continue;
            auto sp=load_nitro_texture_from_narc(assets/"a/0/8/1",std::size_t(member));
            if(sp.valid&&!sp.textures.empty())romNpcSprites.emplace(int(resolvedSprite),std::move(sp));
        }
    }
    const NsbmdMember* romSpriteForModel(int model) const {
        auto it=romNpcSprites.find(model);
        return it!=romNpcSprites.end()?&it->second:nullptr;
    }
    const NsbmdMember* romSpriteForEvent(const HgOverworldEvent& o) const {
        if(auto resolved=hgss::resolveDynamicSprite(o.model,scriptVm.vars())){
            if(auto* sprite=romSpriteForModel(int(*resolved)))return sprite;
            // Object-GFX variables can change after the map's initial asset refresh.
            // Load their newly selected retail sprite lazily so scripted costume/NPC
            // swaps work on every map rather than only for the New Bark friend.
            const int member=hgss::mmodelMemberForSprite(*resolved);
            if(member>=0&&std::size_t(member)<spriteArchiveMembers){
                auto sp=load_nitro_texture_from_narc(assets/"a/0/8/1",std::size_t(member));
                if(sp.valid&&!sp.textures.empty()){
                    auto [it,inserted]=romNpcSprites.emplace(int(*resolved),std::move(sp));
                    (void)inserted;
                    return &it->second;
                }
            }
            // Player graphics are always preloaded and remain valid fallbacks for
            // GetFriendSprite's HERO/HEROINE result.
            if(*resolved==0&&playerSprite.valid)return &playerSprite;
            if(*resolved==97&&heroineSprite.valid)return &heroineSprite;
        }
        // The New Bark/Elm friend event is SPRITE_VAR_1. During the first frame of
        // a transition its VAR_OBJ_1 initializer can still be pending.
        if(o.model==101){
            if(gameState.female)return playerSprite.valid?&playerSprite:nullptr;
            return heroineSprite.valid?&heroineSprite:nullptr;
        }
        return nullptr;
    }
    void beginDialogue(std::string who,std::vector<std::string> pages){ speaker=std::move(who); dialogue=std::move(pages); dialoguePage=0; mode=Mode::Dialogue; }
    std::vector<std::string> romMessagePages(std::uint16_t id,std::vector<std::string> fallback={},const std::vector<std::string>& strvars={}) const {
        if(!romWorldReady)return fallback;
        HgMessageBank mb(romWorld.messageBank());
        auto dm=mb.decode(id,gameState.playerName.empty()?"PLAYER":gameState.playerName,strvars);
        if(dm.valid&&!dm.pages.empty())return dm.pages;
        return fallback;
    }
    void beginNativeDialogue(std::string who,std::uint16_t id,std::vector<std::string> fallback,int action,const std::vector<std::string>& strvars={}){
        nativeDialogueAction=action;
        beginDialogue(std::move(who),romMessagePages(id,std::move(fallback),strvars));
    }
    void setElmFacing(Dir d){auto it=runtimeNpcs.find(0);if(it!=runtimeNpcs.end())it->second.facing=d;}
    void beginElmLabIntro(){
        if(!romWorldReady||romWorld.mapId()!=61||gameState.elmLabIntroDone)return;
        if(!elmLabPreventFlag)if(auto f=hgFirstCheckedFlag(romWorld.scriptBank(),12))elmLabPreventFlag=*f;
        if(!gotStarterFlag)if(auto f=hgFirstCheckedFlag(romWorld.scriptBank(),13))gotStarterFlag=*f;
        scriptedPlayerMoves.clear();
        // Retail scr_seq_T20R0101_011 varies only by the entry X coordinate and
        // always finishes at (4,7) in front of Elm.
        if(tx==3)scriptedPlayerMoves={Dir::Up,Dir::Up,Dir::Right,Dir::Up};
        else if(tx==4)scriptedPlayerMoves={Dir::Up,Dir::Up,Dir::Up};
        else if(tx==5)scriptedPlayerMoves={Dir::Up,Dir::Up,Dir::Left,Dir::Up};
        else if(tx==6)scriptedPlayerMoves={Dir::Up,Dir::Up,Dir::Left,Dir::Left,Dir::Up};
        else{
            int x=tx,y=ty;while(y>7){scriptedPlayerMoves.push_back(Dir::Up);--y;}while(x>4){scriptedPlayerMoves.push_back(Dir::Left);--x;}while(x<4){scriptedPlayerMoves.push_back(Dir::Right);++x;}
        }
        nativeLabPhase=1;facing=Dir::Up;setElmFacing(Dir::Down);
    }
    void finishElmLabIntro(){
        gameState.elmLabIntroDone=true;
        if(elmLabPreventFlag)gameState.flags.insert(elmLabPreventFlag);
        nativeLabPhase=0;scriptedPlayerMoves.clear();setElmFacing(Dir::Right);
    }
    void beginStarterSelection(){
        if(!romWorldReady||romWorld.mapId()!=61||gameState.gotStarter)return;
        if(!elmLabPreventFlag)if(auto f=hgFirstCheckedFlag(romWorld.scriptBank(),12))elmLabPreventFlag=*f;
        if(!gotStarterFlag)if(auto f=hgFirstCheckedFlag(romWorld.scriptBank(),13))gotStarterFlag=*f;
        if(!gameState.elmLabIntroDone){showToast("TALK TO PROFESSOR ELM FIRST");return;}
        if(gameState.gotStarter||gameState.starter){
            std::vector<std::string> vars(2);vars[1]=hg_species_name(gameState.starter);
            beginNativeDialogue("PROF. ELM",7,{"You already chose "+hg_species_name(gameState.starter)+"."},0,vars);return;
        }
        starterIndex=0;nativeStarterSelection=true;mode=Mode::StarterSelect;
    }
    void finishStarterSelection(std::uint16_t species){
        if(!species)return;
        if(!gameState.gotStarter&&!gameState.starter){
            gameState.starter=species;
            gameState.giveMon(species,5);
            gameState.own(species);
        }
        gameState.gotStarter=true;gameState.starter=species;
        if(gotStarterFlag)gameState.flags.insert(gotStarterFlag);
        nativeStarterSelection=false;
        std::vector<std::string> vars(2);vars[1]=hg_species_name(species);
        // Retail message 7 is the first Elm line after GetPartyMonSpecies /
        // SetStarterChoice.  The message decoder receives the species in STRVAR 1.
        beginNativeDialogue("PROF. ELM",7,{gameState.playerName+" received "+hg_species_name(species)+"!"},10,vars);
    }
    void handleNativeDialogueAction(int action){
        if(!romWorldReady)return;
        if(action==1){
            setElmFacing(Dir::Down);
            auto pages=romMessagePages(2,{"...Oh! I have an email!"});auto p3=romMessagePages(3,{"Hm... Uh-huh... OK..."});pages.insert(pages.end(),p3.begin(),p3.end());
            nativeDialogueAction=2;beginDialogue("PROF. ELM",std::move(pages));return;
        }
        if(action==2){setElmFacing(Dir::Down);beginNativeDialogue("PROF. ELM",4,{"I want you to choose one of the Pokemon over there."},3);return;}
        if(action==3){finishElmLabIntro();return;}
        if(action==10){
            std::vector<std::string> vars(2);vars[1]=hg_species_name(gameState.starter);
            beginNativeDialogue("PROF. ELM",10,{"Walking with your Pokemon will help you grow closer."},11,vars);return;
        }
        if(action==11){
            std::vector<std::string> vars(2);vars[1]=hg_species_name(gameState.starter);
            beginNativeDialogue("PROF. ELM",11,{"You can take it outside with you."},12,vars);return;
        }
        if(action==12){
            std::vector<std::string> vars(2);vars[1]=hg_species_name(gameState.starter);
            const std::uint16_t msg=gameState.female?13:12;
            beginNativeDialogue("PROF. ELM",msg,{"If anything comes up, just call me."},13,vars);return;
        }
        if(action==13){
            // The current lab coordinate trigger tells us the numeric retail
            // VAR_SCENE_ELMS_LAB id even when symbolic constants are unavailable.
            if(elmLabSceneVar>=0x4000)gameState.setVar(elmLabSceneVar,1);
            if(newBarkSceneVar>=0x4000)gameState.setVar(newBarkSceneVar,1);
            if(gotStarterFlag)gameState.flags.insert(gotStarterFlag);
            if(elmLabPreventFlag)gameState.flags.erase(elmLabPreventFlag);
            gameState.elmLabIntroDone=true;gameState.gotStarter=true;setElmFacing(Dir::Down);
            return;
        }
        if(action==20){
            // Retail _0197 pushes the player one tile north after Elm's warning.
            // Keep the trigger latched until the step finishes so it cannot retrigger
            // under the player's feet during the scripted movement.
            beginScriptedPlayerStep(Dir::Up);
            setElmFacing(Dir::Right);
            return;
        }
    }
    bool beginScriptedPlayerStep(Dir d){
        facing=d;auto [dx,dy]=delta(d);int nx=tx+dx,ny=ty+dy;
        if(romWorldReady&&!romWorld.canMoveTo(nx,ny))return false;
        fromX=tx;fromY=ty;toX=nx;toY=ny;moveProgress=0.0f;return true;
    }
    bool processCoordinateTrigger(){
        if(!romWorldReady||scriptVm.active()||nativeLabPhase!=0)return false;
        std::uint16_t hit=0;
        for(auto const& t:romWorld.events().triggers){
            const bool inside=tx>=t.x&&ty>=t.y&&tx<t.x+int(std::max<std::uint16_t>(1,t.width))&&ty<t.y+int(std::max<std::uint16_t>(1,t.height));
            if(!inside)continue;
            if(t.variable>=0x4000&&gameState.var(t.variable)!=t.expectedValue)continue;
            hit=t.scriptNumber;
            if(hit==coordinateTriggerLatch)return false;
            coordinateTriggerLatch=hit;
            activeFacingNpcId=-1;activeBackgroundId=-1;if(startRomScript(t.scriptNumber))return true;
            return false;
        }
        if(hit==0)coordinateTriggerLatch=0;
        return false;
    }
    void showToast(std::string t){toast=std::move(t);toastTime=2.4;}
    std::uint32_t rng(){rngState=rngState*1664525u+1013904223u;return rngState;}
    static int retailDirection(Dir d){switch(d){case Dir::Up:return 0;case Dir::Down:return 1;case Dir::Left:return 2;case Dir::Right:return 3;}return 1;}
    static Dir dirFromRetail(int d){switch(d&3){case 0:return Dir::Up;case 1:return Dir::Down;case 2:return Dir::Left;default:return Dir::Right;}}
    static bool retailMovementTranslates(std::uint16_t a){
        return (a>=4&&a<=23)||(a>=52&&a<=59)||(a>=76&&a<=95);
    }
    static int retailMovementDistance(std::uint16_t a){
        if(a>=56&&a<=59)return 2;
        if(a==94||a==95)return 3;
        return retailMovementTranslates(a)?1:0;
    }
    static int retailMovementDirection(std::uint16_t a){
        if((a>=4&&a<=23)||(a>=52&&a<=59)||(a>=76&&a<=91))return int(a&3);
        if(a==92||a==94)return 2;
        if(a==93||a==95)return 3;
        if(a<=3)return int(a);
        // In-place walk/turn variants preserve the same N,S,W,E modulo-four order.
        if(a>=24&&a<=47)return int(a&3);
        return -1;
    }
    void clearScriptHostState(){
        scriptWaitSeconds=0;scriptWaitVar=0;scriptWaitingInput=false;scriptWaitInputVar=0;scriptWaitingMovement=false;
        scriptObjectsLockedAll=false;scriptLockedObjects.clear();scriptHiddenObjects.clear();scriptMovementOverrides.clear();
        scriptNpcMoves.clear();retailPlayerMoves.clear();scriptChoiceDest=0;scriptChoiceCanCancel=false;scriptChoiceIndex=0;scriptChoiceOptions.clear();
        resumeScriptAfterDialogue=false;resumeScriptAfterBattle=false;activeFacingNpcId=-1;
        retailStarterSelectionPending=false;scriptFollowerMovementEnabled=true;
    }
    void primeRetailVisibilityFlags(){
        if(!romWorldReady)return;
        recoverRetailProgressionFlags();

        if(romWorld.mapId()==60){
            // Retail New Bark event bank 57 deliberately places Mom (object 7) on
            // the exact Player House 1F warp tile. FLAG_HIDE_MOM_NEW_BARK is set
            // during normal free-roam and script 12 temporarily clears it only
            // while Mom walks out for the authored "Where are you going?" scene.
            // Native new-game state starts with an empty flag table, so without
            // bootstrapping this ROM-authored hide flag Mom becomes a permanent
            // solid object on the door and the player can never enter the house.
            // Discover both the door and flag from the extracted event data.
            for(auto const& w:romWorld.events().warps){
                if(w.targetMap!=63)continue; // PLAYER_HOUSE_1F
                for(auto const& o:romWorld.events().overworlds){
                    if(o.x==w.x&&o.y==w.y&&o.flag!=0){
                        newBarkMomDoorHideFlag=o.flag;
                        gameState.flags.insert(o.flag);
                        break;
                    }
                }
                if(newBarkMomDoorHideFlag)break;
            }
        }

        if(romWorld.mapId()==61){
            // Elm's Lab opposite-gender friend is likewise authored directly on
            // the exit warp at (4,14) and starts hidden until its retail cutscene.
            for(auto const& o:romWorld.events().overworlds){
                if(o.x==4&&o.y==14&&o.flag!=0){gameState.flags.insert(o.flag);break;}
            }
        }
    }
    void syncRuntimeNpcs(){
        if(!romWorldReady)return;
        if(runtimeNpcMap==romWorld.mapId()&&!runtimeNpcs.empty())return;
        runtimeNpcs.clear();runtimeNpcMap=romWorld.mapId();
        auto od=[](std::uint16_t o){switch(o&3){case 1:return Dir::Up;case 2:return Dir::Left;case 3:return Dir::Right;default:return Dir::Down;}};
        for(auto const& o:romWorld.events().overworlds){
            if(!npcVisible(o))continue;
            RuntimeNpc n;n.x=n.homeX=n.startX=n.targetX=float(o.x);n.y=n.homeY=n.startY=n.targetY=float(o.y);n.facing=od(o.orientation);n.timer=0.35+(o.id%7)*0.17;
            // Elm's event record is authored at (6,5), but the retail map-load
            // script immediately MovePersonFacing's him to (4,5).  Reproduce that
            // placement side effect so his visible position matches the original.
            if(romWorld.mapId()==61&&o.id==0){n.x=n.homeX=n.startX=n.targetX=4.0f;n.y=n.homeY=n.startY=n.targetY=5.0f;n.facing=gameState.elmLabIntroDone?Dir::Right:Dir::Down;}
            runtimeNpcs[int(o.id)]=n;
        }
    }
    void updateRuntimeNpcs(double dt){
        if(!romWorldReady)return;
        syncRuntimeNpcs();
        for(auto const& o:romWorld.events().overworlds){
            auto it=runtimeNpcs.find(int(o.id));if(it==runtimeNpcs.end())continue;auto& n=it->second;
            if(n.move<1.0f){
                n.move=std::min(1.0f,n.move+float(dt)*2.7f);float q=n.move*n.move*(3.0f-2.0f*n.move);
                n.x=n.startX+(n.targetX-n.startX)*q;n.y=n.startY+(n.targetY-n.startY)*q;
                if(n.move>=1.0f){n.x=n.targetX;n.y=n.targetY;}
                continue;
            }
            auto qit=scriptNpcMoves.find(int(o.id));
            if(qit!=scriptNpcMoves.end()&&!qit->second.empty()){
                Dir d=qit->second.front();qit->second.pop_front();n.facing=d;
                auto [dx,dy]=delta(d);int cx=int(std::round(n.x)),cy=int(std::round(n.y)),nx=cx+dx,ny=cy+dy;
                // Scripted retail movement owns the object while the queue is active.
                // The authored scripts expect these paths to complete; only reject a
                // tile when the map's collision data says it is truly impassable.
                if(romWorld.canMoveTo(nx,ny)&&!(nx==tx&&ny==ty)){
                    n.startX=n.x;n.startY=n.y;n.targetX=float(nx);n.targetY=float(ny);n.move=0.0f;
                }
                continue;
            }
            n.timer-=dt;if(n.timer>0)continue;n.timer=0.8+double(rng()%180)/100.0;
            if(scriptObjectsLockedAll||scriptLockedObjects.count(int(o.id)))continue;
            // HG movement records include an explicit movement controller plus X/Y roam ranges.
            // Script command 109 can temporarily replace that controller.
            const std::uint16_t controller=scriptMovementOverrides.count(int(o.id))?scriptMovementOverrides[int(o.id)]:o.movement;
            if(controller==0||(o.xRange==0&&o.yRange==0))continue;
            int pick=int(rng()%4),dx=0,dy=0;if(pick==0){dy=-1;n.facing=Dir::Up;}else if(pick==1){dy=1;n.facing=Dir::Down;}else if(pick==2){dx=-1;n.facing=Dir::Left;}else{dx=1;n.facing=Dir::Right;}
            int cx=int(std::round(n.x)),cy=int(std::round(n.y)),nx=cx+dx,ny=cy+dy;
            if(std::abs(nx-int(n.homeX))>int(o.xRange)||std::abs(ny-int(n.homeY))>int(o.yRange))continue;
            if(nx==tx&&ny==ty)continue;
            if(!romWorld.canMoveTo(nx,ny))continue;
            bool occupied=false;
            for(auto const& [oid,on]:runtimeNpcs)if(oid!=int(o.id)&&int(std::round(on.x))==nx&&int(std::round(on.y))==ny){occupied=true;break;}
            if(occupied)continue;
            n.startX=n.x;n.startY=n.y;n.targetX=float(nx);n.targetY=float(ny);n.move=0.0f;
        }
    }
    void playBgmSequence(std::uint16_t id,float volume=0.18f){
        if(!id||!sdat.stats().valid||!audio.ready())return;
        fanfarePausedBgm=false;resumeBgmAfterSoundWait=false;
        // A music sequence owns the single BGM voice. Re-requesting the current
        // sequence resumes it if a fanfare paused it; changing sequences destroys
        // the old voice before any potentially expensive SDAT render/cache work.
        if(id==currentBgm){if(audio.bgmPaused())audio.resumeBgm();return;}
        // Scene music replacement is atomic: discard any transient/jingle from
        // the old scene before starting the new sequence. Ordinary battle/field
        // SFX still mix normally while the BGM id itself remains unchanged.
        audio.stopSfx();
        audio.stopBgm();
        currentBgm=0;
        auto it=bgmCache.find(id);
        if(it==bgmCache.end()){
            auto song=sdat.renderSequence(id,60.0);if(!song.valid)return;song.loop=true;
            if(bgmCacheOrder.size()>=4){auto old=bgmCacheOrder.front();bgmCacheOrder.pop_front();bgmCache.erase(old);}
            bgmCacheOrder.push_back(id);it=bgmCache.emplace(id,std::move(song)).first;
        }
        if(audio.playBgm(it->second,volume,true))currentBgm=id;
    }
    void playSfxSequence(std::uint16_t id,float volume=0.34f,double seconds=6.0){
        if(!id||!sdat.stats().valid||!audio.ready())return;
        // Retail script events can be observed from more than one host-side path in
        // the same update. Collapse an identical immediate duplicate and keep the
        // native SFX backend single-voice on both Linux and Windows.
        if(id==lastSfxId&&audioClock-lastSfxClock<0.080)return;
        lastSfxId=id;lastSfxClock=audioClock;
        auto it=sfxCache.find(id);
        if(it==sfxCache.end()){
            auto seq=sdat.renderSequence(id,seconds);
            if(!seq.valid)return;
            seq.loop=false;
            if(sfxCacheOrder.size()>=24){auto old=sfxCacheOrder.front();sfxCacheOrder.pop_front();sfxCache.erase(old);}
            sfxCacheOrder.push_back(id);
            it=sfxCache.emplace(id,std::move(seq)).first;
        }
        audio.stopSfx();
        audio.playSfx(it->second,volume);
    }
    std::uint16_t effectiveFieldBgm() const {
        if(!romWorldReady||!romWorld.header())return 0;
        int h=localHour();bool night=(h>=20||h<4);
        auto* mh=romWorld.header();
        return night&&mh->nightMusicId?mh->nightMusicId:mh->dayMusicId;
    }
    void restoreFieldBgm(){auto id=effectiveFieldBgm();if(id&&id!=currentBgm)playBgmSequence(id,0.18f);}
    void reloadScriptHeader(bool queueInit){
        scriptHeader={};scriptHeaderMap=-1;pendingMapScripts.clear();if(!romWorldReady||!romWorld.header())return;
        auto raw=read_narc_member(assets/"a/0/1/2",romWorld.header()->scriptHeaderBank);scriptHeader=parse_hg_script_header(raw);scriptHeaderMap=romWorld.mapId();
        // T20's retail frame table is ordered PLAYER_HOUSE_1F first, NEW_BARK second.
        // Recover the actual numeric scene variable from the ROM header so this
        // remains region/build independent.
        if(scriptHeader.valid&&romWorld.mapId()==60&&scriptHeader.frame.size()>=2){
            // Cache the authored variable id for diagnostics only. Retail scripts,
            // not the native host, own every New Bark scene transition.
            newBarkSceneVar=scriptHeader.frame[1].var;
        }
        if(queueInit&&scriptHeader.valid){if(scriptHeader.transition)pendingMapScripts.push_back(scriptHeader.transition);if(scriptHeader.load)pendingMapScripts.push_back(scriptHeader.load);if(scriptHeader.resume)pendingMapScripts.push_back(scriptHeader.resume);}
        auto fieldBgm=effectiveFieldBgm();
        if(fieldBgm&&fieldBgm!=currentBgm)playBgmSequence(fieldBgm,0.18f);
    }
    void onMapEntered(bool queueInit=true){clearScriptHostState();syncFromRomWorld();primeRetailVisibilityFlags();refreshRomVisualAssets();runtimeNpcMap=-1;syncRuntimeNpcs();reloadScriptHeader(queueInit);stepsSinceEncounter=0;}
    bool startNextMapScript(){if(!romWorldReady||scriptVm.active()||pendingMapScripts.empty())return false;activeFacingNpcId=-1;activeBackgroundId=-1;auto id=pendingMapScripts.front();pendingMapScripts.pop_front();return startRomScript(id);}
    static float stageMultiplier(int stage){stage=std::clamp(stage,-6,6);return stage>=0?float(2+stage)/2.0f:2.0f/float(2-stage);}
    int effectiveStat(const HgMon& m,int which,int stage) const{
        unsigned base=which==0?m.attack:which==1?m.defense:which==2?m.spAttack:which==3?m.spDefense:m.speed;
        if(which==4&&m.status==3)base=std::max(1u,base/4u); // Gen IV paralysis speed
        return std::max(1,int(float(base)*stageMultiplier(stage)));
    }
    std::string statusName(std::uint8_t st) const{switch(st){case 1:return "POISONED";case 2:return "BURNED";case 3:return "PARALYZED";case 4:return "ASLEEP";case 5:return "FROZEN";default:return {};}}
    const NitroRgbaImage* battleMonSprite(std::uint16_t species,std::uint8_t gender,bool back,bool shiny=false) const{
        if(species==0||species>hg_species_count())return nullptr;
        // Retail GetMonSpriteCharAndPlttNarcIdsEx indexes pokegra in complete
        // six-member species groups. Female uses the first char for a facing;
        // male/genderless uses the second. Front is facing offset 2, back 0.
        const std::uint32_t key=std::uint32_t(species)|(std::uint32_t(gender&3u)<<16)|(back?0x40000u:0u)|(shiny?0x80000u:0u);
        auto it=battleSpriteCache.find(key);
        if(it!=battleSpriteCache.end())return it->second.valid?&it->second:nullptr;
        const std::size_t base=std::size_t(species)*6u;
        const std::size_t facing=back?0u:2u;
        const std::size_t genderOffset=gender==1?0u:1u;
        const std::size_t gfxIndex=base+facing+genderOffset;
        const std::size_t palIndex=base+(shiny?5u:4u);
        NitroRgbaImage img=decode_hg_pokepic(read_narc_member(assets/"a/0/0/4",gfxIndex),read_narc_member(assets/"a/0/0/4",palIndex),true,0);
        auto [pos,_]=battleSpriteCache.emplace(key,std::move(img));
        return pos->second.valid?&pos->second:nullptr;
    }
    const std::vector<NitroRgbaImage>& playerBattleBackCells() const{
        return gameState.female?battleGirlBackCells:battleBoyBackCells;
    }
    const NitroNanrBank& playerBattleBackAnim() const{
        return gameState.female?battleGirlBackAnim:battleBoyBackAnim;
    }
    const BattleFxPack* battleFxPack(int index) const{
        index=((index%37)+37)%37;
        auto it=battleFxCache.find(index);
        if(it==battleFxCache.end()){
            const auto charArc=assets/"a/0/2/2",palArc=assets/"a/0/2/3",cellArc=assets/"a/0/2/4",animArc=assets/"a/0/2/5";
            BattleFxPack pack;
            auto ncgr=readNitro2dMember(charArc,std::size_t(index));
            auto ncer=readNitro2dMember(cellArc,std::size_t(index));
            auto nanr=readNitro2dMember(animArc,std::size_t(index));
            // The graphics/cell/animation archives share 37 authored object banks.
            // Palette archive has 39 banks; matching index is correct for the common
            // banks and preserves the two extra global palettes for script use.
            auto nclr=readNitro2dMember(palArc,std::size_t(std::min(index,38)));
            pack.cells=decode_nitro_cells(ncgr,ncer,nclr,true);
            pack.anim=decode_nitro_nanr(nanr);
            it=battleFxCache.emplace(index,std::move(pack)).first;
        }
        return it->second.cells.empty()?nullptr:&it->second;
    }
    const std::vector<NitroRgbaImage>* opponentTrainerCells(int trainerClass) const{
        if(trainerClass<0||trainerClass>=129)return nullptr;
        auto it=battleTrainerFrontCache.find(trainerClass);
        if(it==battleTrainerFrontCache.end()){
            const auto arc=assets/"a/0/5/8";
            const std::size_t b=std::size_t(trainerClass)*5u;
            auto cells=decode_nitro_cells(readNitro2dMember(arc,b),readNitro2dMember(arc,b+2),readNitro2dMember(arc,b+1),true);
            it=battleTrainerFrontCache.emplace(trainerClass,std::move(cells)).first;
        }
        return it->second.empty()?nullptr:&it->second;
    }
    int battleTimeSlot() const{
        // Retail battle graphics use three palette/fill variants.  HG/SS keeps
        // morning/day on slot 0, evening on slot 1 and night on slot 2.
        const int h=localHour();
        if(h>=17&&h<20)return 1;
        if(h>=20||h<4)return 2;
        return 0;
    }
    static Color battleBgr555(std::uint16_t c){
        return {float(c&31u)/31.0f,float((c>>5)&31u)/31.0f,float((c>>10)&31u)/31.0f,1.0f};
    }
    const NitroRgbaImage* battleBackdropArt(int style) const{
        style=std::clamp(style,0,22);
        const int slot=battleTimeSlot();
        const int key=style*3+slot;
        auto it=battleBackdropCache.find(key);
        if(it==battleBackdropCache.end()){
            const auto arc=assets/"a/0/0/7";
            // overlay 12 retail layout:
            //   NSCR 2
            //   NCGR 3 + BattleBg
            //   NCLR 176 + (3 * BattleBg) + time-of-day slot
            auto img=decode_nitro_bg(readNitro2dMember(arc,std::size_t(3+style)),
                                     readNitro2dMember(arc,2),
                                     readNitro2dMember(arc,std::size_t(176+style*3+slot)),true);
            it=battleBackdropCache.emplace(key,std::move(img)).first;
        }
        return it->second.valid?&it->second:nullptr;
    }
    int currentBattleBackdrop() const{
        if(romWorldReady&&romWorld.header())return int(romWorld.header()->battleBg);
        return mapIndex==2?6:(mapIndex==1?0:2);
    }
    int currentBattleTerrain() const{
        // BattleBg and Terrain are separate retail enums.  The field header gives
        // us BattleBg; map the shared environment classes to the matching terrain
        // until the field encounter record itself is hosted natively.
        const int bg=std::clamp(currentBattleBackdrop(),0,22);
        switch(bg){
            case 0:return 0;   // plain
            case 1:return 7;   // water
            case 2:return 9;   // city/building
            case 3:return 2;   // grass/forest
            case 4:return 4;   // mountain
            case 5:return 6;   // snow
            case 6:case 7:case 8:return 9;
            case 9:case 10:case 11:return 5;
            default:return bg; // E4 / Frontier ids line up with Terrain 12..22
        }
    }
    const NitroRgbaImage* battleTerrainArt(int terrain,bool playerSide) const{
        terrain=std::clamp(terrain,0,23);
        const int slot=battleTimeSlot();
        const int key=terrain*3+slot;
        auto& cache=playerSide?battleTerrainPlayerCache:battleTerrainEnemyCache;
        auto it=cache.find(key);
        if(it==cache.end()){
            static constexpr std::array<int,24> enemyChar={135,145,127,151,139,149,141,133,137,143,147,151,153,155,157,159,161,163,165,167,169,171,173,175};
            static constexpr std::array<int,24> playerChar={136,146,130,152,140,150,142,134,138,144,148,148,154,156,158,160,162,164,166,168,170,172,174,176};
            static constexpr std::array<std::array<int,3>,24> pal={{
                {{7,8,9}},{{22,23,24}},{{1,2,3}},{{31,32,33}},{{13,14,15}},{{28,29,30}},
                {{16,17,18}},{{4,5,6}},{{10,11,12}},{{19,20,21}},{{25,26,27}},{{25,26,27}},
                {{34,35,36}},{{37,38,39}},{{40,41,42}},{{43,44,45}},{{46,47,48}},{{49,50,51}},
                {{52,53,54}},{{55,56,57}},{{58,59,60}},{{61,62,63}},{{64,65,66}},{{67,68,69}}
            }};
            const auto arc=assets/"a/0/0/8";
            const int chr=playerSide?playerChar[std::size_t(terrain)]:enemyChar[std::size_t(terrain)];
            const int cell=playerSide?131:128;
            auto cells=decode_nitro_cells(readNitro2dMember(arc,std::size_t(chr)),
                                          readNitro2dMember(arc,std::size_t(cell)),
                                          readNitro2dMember(arc,std::size_t(pal[std::size_t(terrain)][std::size_t(slot)])),true);
            NitroRgbaImage selected;
            for(auto& cellImg:cells){
                if(!cellImg.valid)continue;
                auto b=nitroOpaqueBounds(cellImg);
                if(b.w>0&&b.h>0){selected=std::move(cellImg);break;}
            }
            it=cache.emplace(key,std::move(selected)).first;
        }
        return it->second.valid?&it->second:nullptr;
    }
    void drawBattleBackdrop(RenderFrame& f,int style) const{
        style=std::clamp(style,0,22);
        const int slot=battleTimeSlot();
        // Exact BGR555 underfill table read from retail overlay 12 at
        // ov12_0226C1C8. Transparent NSCR pixels expose this backdrop color.
        static constexpr std::array<std::array<std::uint16_t,3>,23> fill={{
            {{0x7B51,0x023E,0x5E00}},{{0x7B51,0x023E,0x5E00}},{{0x7B51,0x023E,0x5E00}},
            {{0x16AB,0x16AB,0x16AB}},{{0x7B51,0x023E,0x5E00}},{{0x7F5F,0x5B5F,0x5AD8}},
            {{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},
            {{0x1CA5,0x1CA5,0x1CA5}},{{0x1CA5,0x1CA5,0x1CA5}},{{0x1CA5,0x1CA5,0x1CA5}},
            {{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},
            {{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},
            {{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}},
            {{0x7FFF,0x7FFF,0x7FFF}},{{0x7FFF,0x7FFF,0x7FFF}}
        }};
        constexpr int vh=535;
        const int vw=int(std::lround(256.0/192.0*vh));
        const int vx=(RenderFrame::PixelWidth-vw)/2;
        pixelRect(f,vx,0,vw,vh,battleBgr555(fill[std::size_t(style)][std::size_t(slot)]));
        if(auto const* art=battleBackdropArt(style);art&&art->valid)
            blitImageCrop(f,*art,0,0,std::min(256,art->width),std::min(192,art->height),vx,0,vw,vh,true);
    }
    void drawBattleTerrain(RenderFrame& f,bool playerSide) const{
        if(auto const* art=battleTerrainArt(currentBattleTerrain(),playerSide);art&&art->valid){
            constexpr float scale=535.0f/192.0f;
            const float viewX=float(RenderFrame::PixelWidth-int(std::lround(256.0f*scale)))/2.0f;
            // Retail terrain objects enter from x=336/-80 and settle exactly one
            // DS screen inward: enemy=(80,136), player=(176,88).
            if(playerSide)drawDsAnchoredCell(f,*art,176.0f,88.0f,scale,viewX,0.0f);
            else drawDsAnchoredCell(f,*art,80.0f,136.0f,scale,viewX,0.0f);
        }
    }
    struct BattleFloorPlacement { float centerX=0.0f; float surfaceY=0.0f; bool valid=false; };
    BattleFloorPlacement battleFloorPlacement(bool playerPokemon) const{
        // The near/player floor uses the resource drawn by the pre-battler terrain
        // pass (cell 128, settled anchor 80,136); the far/enemy floor uses cell 131
        // at 176,88.  Center battlers on the opaque pixels of those actual ROM
        // objects instead of hard-coded PC coordinates.
        const bool terrainPlayerSide=!playerPokemon;
        auto const* art=battleTerrainArt(currentBattleTerrain(),terrainPlayerSide);
        if(!art||!art->valid)return {};
        auto b=nitroOpaqueBounds(*art); if(!b.w||!b.h)return {};
        constexpr float scale=535.0f/192.0f;
        const float viewX=float(RenderFrame::PixelWidth-int(std::lround(256.0f*scale)))/2.0f;
        const float anchorX=playerPokemon?80.0f:176.0f;
        const float anchorY=playerPokemon?136.0f:88.0f;
        const float planeX=viewX+(anchorX-128.0f)*scale;
        const float planeY=(anchorY-96.0f)*scale;
        BattleFloorPlacement out;
        out.centerX=planeX+(float(b.x)+float(b.w)*0.5f)*scale;
        // Put the feet/base slightly below the visual centre of the ellipse, as
        // the retail Pokepic origin does, while leaving the foreground lip free
        // to occlude the player's lower pixels.
        out.surfaceY=planeY+(float(b.y)+float(b.h)*0.72f)*scale;
        out.valid=true; return out;
    }
    static void drawDsAnchoredCell(RenderFrame& f,const NitroRgbaImage& img,float anchorX,float anchorY,float scale,float viewX,float viewY){
        if(!img.valid)return;
        const int dx=int(std::lround(viewX+(anchorX-128.0f)*scale));
        const int dy=int(std::lround(viewY+(anchorY-96.0f)*scale));
        blitImage(f,img,dx,dy,int(std::lround(256.0f*scale)),int(std::lround(192.0f*scale)),true);
    }
    static constexpr double kBattleTransitionSeconds=0.90;
    // subscript_0000_StartEncounter.s uses these exact wait counts around the
    // encounter/send-out controller commands. trbgra NANR sequence 1 itself is
    // 94 frames; the battle script waits 96 frames for the complete player send-out.
    static constexpr double kWildEncounterWait=122.0/60.0;
    static constexpr double kTrainerEncounterWait=96.0/60.0;
    static constexpr double kEnemySendOutWait=112.0/60.0;
    static constexpr double kPlayerSendOutWait=96.0/60.0;
    static constexpr double kPlayerThrowSeconds=94.0/60.0;
    bool currentMapIsKanto() const { return romWorldReady&&romWorld.header()&&romWorld.header()->regionNo!=0; }
    void beginBattle(HgMon enemy,bool trainer=false,std::uint16_t trainerId=0,std::uint8_t trainerClass=0){
        if(!gameState.leadAlive()){showToast("NO USABLE POKEMON");return;}
        // Preserve the real field composition for the encounter transition before
        // the battle application owns the screen.
        battleEntryFrameValid=false;
        if(romWorldReady){battleEntryFrame=renderRomField();battleEntryFrameValid=true;}
        hg_rehydrate_mon(*gameState.leadAlive());hg_rehydrate_mon(enemy);
        battle={};battle.active=true;battle.trainer=trainer;battle.trainerId=trainerId;battle.trainerClass=trainerClass;battle.enemy=enemy;battle.menu=0;battle.sceneClock=0.0;battle.actionClock=0.0;battle.transitionClock=0.0;
        const auto cue=hg_retail_battle_cue(trainer,trainerClass,enemy.species,currentMapIsKanto());
        battle.retailEffect=cue.effect;battle.transitionId=cue.transition;battle.battleBgm=cue.bgm;
        // Retail encounter startup hands both the effect and BGM to the transition
        // task.  Make this a hard scene/audio boundary so field music or a fanfare
        // can never survive underneath battle music.
        audio.stop();currentBgm=0;fanfarePausedBgm=false;resumeBgmAfterSoundWait=false;
        playBgmSequence(battle.battleBgm,0.18f);
        gameState.see(enemy.species);
        // The retail StartEncounter battle subscript does not throw the player's
        // ball under the encounter text. It first runs PlayEncounterAnimation,
        // waits 122 (wild) or 96 (trainer) frames, waits for the encounter message,
        // then performs the trainer send-out (trainer battles) followed by the
        // player's ThrowPokeball/PokemonSlideIn command pair.
        battle.introPhase=0;battle.introClock=0.0;battle.message.clear();battle.awaiting=false;mode=Mode::Battle;
    }
    void beginTrainerBattle(std::uint16_t trainerId){
        auto tr=load_hg_trainer(assets,trainerId);
        if(!tr.valid||tr.party.empty()){showToast("TRAINER DATA "+std::to_string(trainerId)+" UNAVAILABLE");return;}
        std::vector<HgMon> mons;mons.reserve(tr.party.size());
        for(auto const& r:tr.party){auto m=hg_make_mon(r.species,std::uint8_t(std::clamp<int>(r.level,1,100)),r.heldItem);if(r.moves[0]){m.moves=r.moves;for(int i=0;i<4;i++)if(auto* md=hg_move_data(m.moves[i]))m.pp[i]=m.maxPp[i]=md->pp;}mons.push_back(m);}
        beginBattle(mons.front(),true,trainerId,tr.trainerClass);battle.enemyParty=std::move(mons);battle.enemyIndex=0;
    }
    void finishBattle(bool victory){
        gameState.lastBattleWon=victory;
        if(victory&&battle.trainer)gameState.trainerFlags.insert(battle.trainerId);
        // Battle is a separate sound scene in retail.  Tear it down completely,
        // then restore whatever field sequence the destination map header selects.
        audio.stop();currentBgm=0;fanfarePausedBgm=false;resumeBgmAfterSoundWait=false;
        battle.active=false;mode=Mode::Field;partyReturnBattle=false;battleEntryFrameValid=false;
        if(resumeScriptAfterBattle&&scriptVm.active()){resumeScriptAfterBattle=false;continueRomScript();}
        if(currentBgm==0)restoreFieldBgm();
    }
    bool applySimpleStatusMove(HgMon& atk,HgMon& def,std::uint16_t move,bool player){
        auto& own=player?battle.playerStages:battle.enemyStages;auto& foe=player?battle.enemyStages:battle.playerStages;
        auto lower=[&](int stat,int n){int old=foe[stat];foe[stat]=std::max(-6,foe[stat]-n);return foe[stat]!=old;};
        auto raise=[&](int stat,int n){int old=own[stat];own[stat]=std::min(6,own[stat]+n);return own[stat]!=old;};
        if(move==45)return lower(0,1); // Growl: Attack
        if(move==43)return lower(1,1); // Leer: Defense
        if(move==39)return lower(1,1); // Tail Whip
        if(move==81)return lower(4,1); // String Shot (Gen IV)
        if(move==97)return raise(4,2); // Agility
        if(move==14)return raise(0,2); // Swords Dance
        if(move==106)return raise(1,1); // Harden
        if(move==86&&def.status==0){def.status=3;return true;} // Thunder Wave
        if((move==77||move==92)&&def.status==0){def.status=1;return true;} // PoisonPowder/Toxic base status
        if(move==261&&def.status==0){def.status=2;return true;} // Will-O-Wisp
        if((move==79||move==95)&&def.status==0){def.status=4;return true;} // sleep moves
        if(move==105||move==135||move==234||move==235||move==236||move==355){auto old=atk.hp;atk.hp=std::min<std::uint16_t>(atk.maxHp,std::uint16_t(atk.hp+std::max<int>(1,atk.maxHp/2)));return atk.hp!=old;}
        return false;
    }
    std::string useMove(HgMon& atk,HgMon& def,int slot,bool player){
        slot=std::clamp(slot,0,3);std::uint16_t move=atk.moves[slot];auto* md=hg_move_data(move);std::string who=player?hg_species_name(atk.species):"FOE "+hg_species_name(atk.species);
        if(!move||!md){return who+" HAS NO MOVE!";}if(atk.pp[slot]==0)return who+" HAS NO PP LEFT FOR "+hg_move_name(move)+"!";atk.pp[slot]--;
        if(atk.status==3&&(rng()%4)==0)return who+" IS PARALYZED! IT CAN'T MOVE!";
        if(atk.status==4){if((rng()%3)!=0)return who+" IS FAST ASLEEP!";atk.status=0;}
        if(atk.status==5){if((rng()%5)!=0)return who+" IS FROZEN SOLID!";atk.status=0;}
        if(md->accuracy&&int(rng()%100)>=md->accuracy)return who+" USED "+hg_move_name(move)+", BUT IT MISSED!";
        if(md->power==0){bool ok=applySimpleStatusMove(atk,def,move,player);return who+" USED "+hg_move_name(move)+"!"+(ok?"":"  BUT IT FAILED!");}
        auto* ap=hg_personal_data(atk.species);auto* dp=hg_personal_data(def.species);int ai=md->category==1?2:0,di=md->category==1?3:1;int as=effectiveStat(atk,ai,player?battle.playerStages[ai]:battle.enemyStages[ai]);int ds=effectiveStat(def,di,player?battle.enemyStages[di]:battle.playerStages[di]);if(md->category==0&&atk.status==2)as=std::max(1,as/2);
        int base=((2*int(atk.level)/5+2)*int(md->power)*as/std::max(1,ds))/50+2;float mult=float(85+(rng()%16))/100.0f;bool crit=(rng()%16)==0;if(crit)mult*=2.0f;if(ap&&(ap->type1==md->type||ap->type2==md->type))mult*=1.5f;float eff=1.0f;if(dp){eff*=hg_type_effectiveness(md->type,dp->type1);if(dp->type2!=dp->type1)eff*=hg_type_effectiveness(md->type,dp->type2);}mult*=eff;int damage=eff==0?0:std::max(1,int(float(base)*mult));def.hp=std::uint16_t(damage>=def.hp?0:def.hp-damage);
        if(def.hp&&def.status==0&&md->effectChance&&int(rng()%100)<md->effectChance){if(move==52||move==126||move==172||move==257)def.status=2;else if(move==40||move==188)def.status=1;else if(move==84||move==85||move==34)def.status=3;else if(move==58||move==59)def.status=5;}
        std::string out=who+" USED "+hg_move_name(move)+"!";if(crit)out+="  A CRITICAL HIT!";if(eff==0)out+="  IT DOESN'T AFFECT THE TARGET.";else if(eff>1.01f)out+="  IT'S SUPER EFFECTIVE!";else if(eff<0.99f)out+="  IT'S NOT VERY EFFECTIVE.";return out;
    }
    int chooseEnemyMove(){std::vector<int> slots;for(int i=0;i<4;i++)if(battle.enemy.moves[i]&&battle.enemy.pp[i])slots.push_back(i);return slots.empty()?0:slots[rng()%slots.size()];}
    void awardExperience(const HgMon& defeated){auto* me=gameState.leadAlive();auto* p=hg_personal_data(defeated.species);if(!me||!p)return;std::uint32_t gain=std::max<std::uint32_t>(1,std::uint32_t(p->baseExp)*defeated.level/7u);me->exp+=gain;while(me->level<100&&me->exp>=hg_exp_for_level(me->species,std::uint8_t(me->level+1))){auto oldMax=me->maxHp;me->level++;hg_rehydrate_mon(*me);me->hp=std::min<std::uint16_t>(me->maxHp,std::uint16_t(me->hp+(me->maxHp-oldMax)));auto learned=hg_levelup_moves(me->species,me->level);if(!learned.empty()){auto mv=learned.back();bool known=std::find(me->moves.begin(),me->moves.end(),mv)!=me->moves.end();if(!known){int slot=0;while(slot<4&&me->moves[slot])slot++;if(slot==4)slot=0;me->moves[slot]=mv;if(auto* md=hg_move_data(mv))me->pp[slot]=me->maxPp[slot]=md->pp;}}}}
    void performBattleAction(bool player,int slot){
        auto* me=gameState.leadAlive();if(!me)return;
        battle.actionByPlayer=player;battle.actionClock=0.0;
        battle.currentMove=player?me->moves[slot]:battle.enemy.moves[slot];
        if(auto* md=hg_move_data(battle.currentMove)){battle.currentMoveEffect=md->effect;battle.currentMoveType=md->type;battle.currentMoveCategory=md->category;}
        else {battle.currentMoveEffect=0;battle.currentMoveType=0;battle.currentMoveCategory=2;}
        battle.message=player?useMove(*me,battle.enemy,slot,true):useMove(battle.enemy,*me,slot,false);
        battle.awaiting=true;
    }
    bool queueBattleFaintMessage(){
        auto* me=gameState.leadAlive();
        if(battle.enemy.hp==0){
            if(!battle.experienceAwarded){awardExperience(battle.enemy);battle.experienceAwarded=true;}
            battle.message=hg_species_name(battle.enemy.species)+" FAINTED!";battle.awaiting=true;battle.turnStep=4;return true;
        }
        if(!me||me->hp==0){
            std::string n=me?hg_species_name(me->species):"YOUR POKEMON";
            battle.message=n+" FAINTED!";battle.awaiting=true;battle.turnStep=4;return true;
        }
        return false;
    }
    bool applyBattleResidual(){
        auto* me=gameState.leadAlive();if(!me)return false;
        std::vector<std::string> parts;
        if(me->hp&&(me->status==1||me->status==2)){
            me->hp=std::uint16_t(std::max(0,int(me->hp)-std::max(1,int(me->maxHp)/8)));
            parts.push_back(hg_species_name(me->species)+(me->status==1?" IS HURT BY POISON!":" IS HURT BY ITS BURN!"));
        }
        if(battle.enemy.hp&&(battle.enemy.status==1||battle.enemy.status==2)){
            battle.enemy.hp=std::uint16_t(std::max(0,int(battle.enemy.hp)-std::max(1,int(battle.enemy.maxHp)/8)));
            parts.push_back("FOE "+hg_species_name(battle.enemy.species)+(battle.enemy.status==1?" IS HURT BY POISON!":" IS HURT BY ITS BURN!"));
        }
        if(parts.empty())return false;
        battle.message=parts.front();for(std::size_t i=1;i<parts.size();++i)battle.message+="  "+parts[i];
        battle.awaiting=true;battle.turnStep=3;battle.actionClock=0.0;return true;
    }
    void endBattleTurn(){
        battle.turnStep=0;battle.pendingPlayerSlot=-1;battle.pendingEnemySlot=-1;battle.secondActorPlayer=false;
        battle.experienceAwarded=false;battle.awaiting=false;battle.message.clear();battle.choosingMove=false;
    }
    bool advanceBattleTurn(){
        if(battle.turnStep==0)return false;
        if(battle.turnStep==1){
            if(queueBattleFaintMessage())return true;
            battle.turnStep=2;
            performBattleAction(battle.secondActorPlayer,battle.secondActorPlayer?battle.pendingPlayerSlot:battle.pendingEnemySlot);
            return true;
        }
        if(battle.turnStep==2){
            if(queueBattleFaintMessage())return true;
            if(applyBattleResidual())return true;
            endBattleTurn();return false;
        }
        if(battle.turnStep==3){
            if(queueBattleFaintMessage())return true;
            endBattleTurn();return false;
        }
        if(battle.turnStep==4){
            battle.turnStep=0;battle.pendingPlayerSlot=-1;battle.pendingEnemySlot=-1;battle.experienceAwarded=false;
            return false;
        }
        endBattleTurn();return false;
    }
    void executeBattleTurn(int playerSlot){
        auto* me=gameState.leadAlive();if(!me||battle.turnStep!=0)return;
        int enemySlot=chooseEnemyMove();
        auto* pm=hg_move_data(me->moves[playerSlot]);auto* em=hg_move_data(battle.enemy.moves[enemySlot]);
        int pp=pm?pm->priority:0,ep=em?em->priority:0;
        int ps=effectiveStat(*me,4,battle.playerStages[4]),es=effectiveStat(battle.enemy,4,battle.enemyStages[4]);
        bool playerFirst=pp!=ep?pp>ep:(ps!=es?ps>es:(rng()&1));
        battle.pendingPlayerSlot=playerSlot;battle.pendingEnemySlot=enemySlot;battle.secondActorPlayer=!playerFirst;
        battle.turnStep=1;battle.experienceAwarded=false;battle.choosingMove=false;
        performBattleAction(playerFirst,playerFirst?playerSlot:enemySlot);
    }
    std::string enemyFreeAction(){
        auto* me=gameState.leadAlive();
        if(!me||!me->hp||!battle.enemy.hp)return {};
        int slot=chooseEnemyMove();
        return useMove(battle.enemy,*me,slot,false);
    }
    void battleFight(){battle.choosingMove=true;battle.moveMenu=0;battle.message.clear();}
    void battleBag(){
        if(battle.trainer){battle.message="YOU CAN'T CATCH A TRAINER'S POKEMON!";battle.awaiting=true;return;}
        if(!gameState.hasItem(4,1)){battle.message="NO POKE BALLS IN THE BAG.";battle.awaiting=true;return;}
        gameState.takeItem(4,1);auto* p=hg_personal_data(battle.enemy.species);int rate=p?p->catchRate:45;int maxhp=std::max<int>(1,battle.enemy.maxHp),hp=battle.enemy.hp;int a=((3*maxhp-2*hp)*rate)/(3*maxhp);if(battle.enemy.status==4||battle.enemy.status==5)a*=2;else if(battle.enemy.status)a=a*3/2;int chance=std::clamp(a*100/255,1,95);
        if(int(rng()%100)<chance){
            const auto beforeParty=gameState.party.size();
            if(gameState.storeMon(battle.enemy)){gameState.own(battle.enemy.species);battle.message="GOTCHA! "+hg_species_name(battle.enemy.species)+" WAS CAUGHT!";if(beforeParty>=6)battle.message+="  IT WAS SENT TO THE PC.";battle.enemy.hp=0;}
            else battle.message="THE POKEMON STORAGE SYSTEM IS FULL!";
        }else{
            battle.message="OH NO! THE POKEMON BROKE FREE!";
            auto counter=enemyFreeAction();if(!counter.empty())battle.message+="  "+counter;
        }
        battle.awaiting=true;
    }
    void battleRun(){
        if(battle.trainer){battle.message="NO! THERE'S NO RUNNING FROM A TRAINER BATTLE!";battle.awaiting=true;return;}auto* me=gameState.leadAlive();battle.runAttempts++;int my=me?effectiveStat(*me,4,battle.playerStages[4]):1,en=effectiveStat(battle.enemy,4,battle.enemyStages[4]);bool ok=my>en||((my*128/std::max(1,en)+30*battle.runAttempts)&255)>(int(rng()&255));if(ok){battle.message="GOT AWAY SAFELY!";battle.enemy.hp=0;}else{battle.message="CAN'T ESCAPE!";auto counter=enemyFreeAction();if(!counter.empty())battle.message+="  "+counter;}battle.awaiting=true;
    }
    void applyMovementYield(const HgScriptYield& y){
        if(!romWorldReady)return;
        auto const& bank=romWorld.scriptBank();std::int64_t q=std::int64_t(scriptVm.pc())+y.rel;
        if(q<0||q+4>std::int64_t(bank.size()))return;
        const int objectId=int(y.a);
        for(int guard=0;guard<1024&&q+4<=std::int64_t(bank.size());guard++,q+=4){
            std::uint16_t action=std::uint16_t(bank[std::size_t(q)])|(std::uint16_t(bank[std::size_t(q)+1])<<8);
            std::uint16_t count=std::uint16_t(bank[std::size_t(q)+2])|(std::uint16_t(bank[std::size_t(q)+3])<<8);
            if(action==254)break;
            const int rd=retailMovementDirection(action);const int dist=retailMovementDistance(action);
            if(rd>=0){
                Dir d=dirFromRetail(rd);
                if(objectId==255){facing=d;}
                else if(auto it=runtimeNpcs.find(objectId);it!=runtimeNpcs.end())it->second.facing=d;
                if(dist>0){
                    const std::uint32_t total=std::max<std::uint32_t>(1,count)*std::uint32_t(dist);
                    if(objectId==HG_SCRIPT_OBJ_PLAYER){
                        for(std::uint32_t n=0;n<total&&n<4096;n++)retailPlayerMoves.push_back(d);
                    }else if(objectId==HG_SCRIPT_OBJ_PARTNER_POKE){
                        // obj_partner_poke (253) is a virtual follower actor in retail.
                        // The PC renderer derives its position from the player/follower
                        // state, so its authored movement completes synchronously here.
                        // Never enqueue it as a normal NPC: no runtimeNpcs entry can
                        // consume that queue and WaitMovement would deadlock forever.
                        if(!gameState.party.empty()&&gameState.followerEnabled){
                            gameState.followerSpecies=gameState.party[std::min<std::size_t>(gameState.followerPartySlot,gameState.party.size()-1)].species;
                        }
                    }else if(hg_script_object_queues_runtime_npc(objectId,runtimeNpcs.count(objectId)!=0)){
                        auto& queue=scriptNpcMoves[objectId];
                        for(std::uint32_t n=0;n<total&&n<4096;n++)queue.push_back(d);
                    }
                    // Hidden/removed/non-map script actors also complete synchronously.
                    // Retail can issue movement to actors controlled by another field
                    // task; leaving a queue for an object absent from runtimeNpcs is a
                    // permanent native-engine wait, never legitimate game behavior.
                }
            }
            // Visibility actions also exist in the movement table. They operate on
            // the same object as ApplyMovement and complete synchronously.
            if(action==69){scriptHiddenObjects.insert(objectId);runtimeNpcs.erase(objectId);}
            else if(action==70){scriptHiddenObjects.erase(objectId);runtimeNpcMap=-1;syncRuntimeNpcs();}
        }
    }
    bool allScriptMovementDone() const {
        if(!retailPlayerMoves.empty()||moveProgress<1.0f)return false;
        for(auto const& [id,q]:scriptNpcMoves)if(runtimeNpcs.count(id)&&!q.empty())return false;
        for(auto const& [id,n]:runtimeNpcs)if(n.move<1.0f)return false;
        return true;
    }
    void restoreScriptObject(int id){
        if(!romWorldReady||runtimeNpcs.count(id))return;
        for(auto const& o:romWorld.events().overworlds)if(int(o.id)==id&&npcVisible(o)){
            auto od=[](std::uint16_t v){switch(v&3){case 1:return Dir::Up;case 2:return Dir::Left;case 3:return Dir::Right;default:return Dir::Down;}};
            RuntimeNpc n;n.x=n.homeX=n.startX=n.targetX=float(o.x);n.y=n.homeY=n.startY=n.targetY=float(o.y);n.facing=od(o.orientation);n.timer=0.4;
            if(romWorld.mapId()==61&&o.id==0){n.x=n.homeX=n.startX=n.targetX=4.0f;n.y=n.homeY=n.startY=n.targetY=5.0f;}
            runtimeNpcs[id]=n;return;
        }
    }
    void checkWildEncounter(){
        if(!romWorldReady||!romWorld.header()||romWorld.header()->wildEncounterBank==0xff||!gameState.leadAlive())return;
        // HG/SS land encounters are gated by the per-tile permission/behavior layer.
        // A map having an encounter table does not mean every walkable tile can battle.
        if(!romWorld.isLandEncounterTile(romWorld.x(),romWorld.y())){stepsSinceEncounter=0;return;}
        if(++stepsSinceEncounter<3)return;
        auto table=load_hg_wild_table(assets,romWorld.header()->wildEncounterBank);
        if(!table.valid||table.walkingRate==0)return;
        if((rng()%100)>=table.walkingRate)return;
        std::time_t now=std::time(nullptr);std::tm lt{};
#ifdef _WIN32
        localtime_s(&lt,&now);
#else
        localtime_r(&now,&lt);
#endif
        auto slot=choose_hg_land_encounter(table,rng(),lt.tm_hour);
        if(!slot.species)return;
        auto e=hg_make_mon(slot.species,slot.minLevel);stepsSinceEncounter=0;beginBattle(e,false,0);
    }
    void beginAppNameEntry(NameTarget target,std::size_t partySlot=0,std::uint16_t resultVar=0){
        nameTarget=target;appNamePartySlot=partySlot;appNameResultVar=resultVar;appNameCursor=0;
        if(target==NameTarget::Player)appName=gameState.playerName;
        else if(target==NameTarget::Rival)appName=gameState.rivalName=="???"?std::string{}:gameState.rivalName;
        else if(target==NameTarget::Nickname&&partySlot<gameState.party.size()){auto const&m=gameState.party[partySlot];appName=m.nickname.empty()?hg_species_name(m.species):m.nickname;}
        else appName.clear();
        mode=Mode::Naming;appResumeScript=scriptVm.active();
    }
    void finishAppNameEntry(bool accepted){
        if(accepted){
            std::string chosen=appName;
            if(nameTarget==NameTarget::Player){if(chosen.empty())chosen=gameState.female?"LYRA":"ETHAN";gameState.playerName=chosen;}
            else if(nameTarget==NameTarget::Rival){if(chosen.empty())chosen="SILVER";gameState.rivalName=chosen;}
            else if(nameTarget==NameTarget::Nickname&&appNamePartySlot<gameState.party.size()){if(chosen.empty())chosen=hg_species_name(gameState.party[appNamePartySlot].species);gameState.party[appNamePartySlot].nickname=chosen;}
            if(appNameResultVar>=0x4000)scriptVm.writeVar(appNameResultVar,1);
        } else if(appNameResultVar>=0x4000)scriptVm.writeVar(appNameResultVar,0);
        nameTarget=NameTarget::None;appName.clear();mode=Mode::Field;bool resume=appResumeScript;appResumeScript=false;if(resume&&scriptVm.active())continueRomScript();
    }
    static unsigned bitCount32(std::uint32_t v){unsigned n=0;while(v){n+=v&1u;v>>=1u;}return n;}
    void loadRetailMartEntries(const std::vector<HgRetailMartItem>& source){
        martEntries.clear();martEntries.reserve(source.size());
        for(auto const& r:source)martEntries.push_back({r.id,r.price?r.price:hg_item_price(r.id),r.slot,false});
    }
    unsigned retailBadgeCount() const {
        return std::max<unsigned>(gameState.badges,bitCount32(gameState.badgeFlags));
    }
    void openMart(std::uint16_t opcode,std::uint16_t id){
        martOpcode=opcode;martId=id;martIndex=0;martQuantity=1;martSelling=false;martAthlete=false;martDataCards=false;
        if(opcode==275)loadRetailMartEntries(hg_standard_mart_inventory(retailBadgeCount()));
        else if(opcode==276)loadRetailMartEntries(hg_special_mart_inventory(id));
        else if(opcode==277)loadRetailMartEntries(hg_decoration_mart_inventory(id));
        else if(opcode==278)loadRetailMartEntries(hg_seal_mart_inventory(id));
        else martEntries.clear();
        appResumeScript=scriptVm.active();mode=Mode::Mart;
    }
    void openMartSell(){
        martOpcode=782;martId=0;martIndex=0;martQuantity=1;martSelling=true;martAthlete=false;martDataCards=false;martEntries.clear();
        std::vector<std::pair<std::uint16_t,std::uint16_t>> bag(gameState.bag.begin(),gameState.bag.end());
        std::sort(bag.begin(),bag.end(),[](auto const&a,auto const&b){auto pa=hg_item_pocket(a.first),pb=hg_item_pocket(b.first);return pa==pb?a.first<b.first:pa<pb;});
        for(auto [id,qty]:bag){(void)qty;auto price=hg_item_price(id);if(price>0)martEntries.push_back({id,price/2,0,false});}
        appResumeScript=scriptVm.active();mode=Mode::Mart;
    }
    void openPokeathlonMart(std::uint16_t opcode){
        martOpcode=opcode;martId=0;martIndex=0;martQuantity=1;martSelling=false;martAthlete=true;martDataCards=opcode==772;
        if(opcode==771){
            std::time_t now=std::time(nullptr);std::tm lt{};
#ifdef _WIN32
            localtime_s(&lt,&now);
#else
            localtime_r(&now,&lt);
#endif
            const std::uint32_t stamp=std::uint32_t(lt.tm_year+1900)*1000u+std::uint32_t(lt.tm_yday+1);
            if(gameState.pokeathlonPrizeDay!=stamp){gameState.pokeathlonPrizeDay=stamp;gameState.pokeathlonPrizeFlags=0;}
            loadRetailMartEntries(hg_pokeathlon_daily_inventory(unsigned(lt.tm_wday),gameState.nationalDex));
            for(auto& e:martEntries)e.sold=(gameState.pokeathlonPrizeFlags&(std::uint16_t(1u)<<e.slot))!=0;
        }else{
            loadRetailMartEntries(hg_pokeathlon_data_card_inventory(bitCount32(gameState.pokeathlonDataCardFlags&0x07ffffffu)));
            for(auto& e:martEntries)if(e.id>=505&&e.id<=531)e.sold=(gameState.pokeathlonDataCardFlags&(1u<<(e.id-505)))!=0;
        }
        appResumeScript=scriptVm.active();mode=Mode::Mart;
    }
    void finishBlockingApp(){mode=Mode::Field;bool resume=appResumeScript;appResumeScript=false;if(resume&&scriptVm.active())continueRomScript();}
    void useFieldMove(std::uint16_t opcode,std::uint16_t partySlot){
        if(partySlot<gameState.party.size()){auto species=gameState.party[partySlot].species;showToast(hg_species_name(species)+" USED "+(opcode==177?"ROCK CLIMB":opcode==178?"SURF":opcode==179?"WATERFALL":"WHIRLPOOL")+"!");}
        if(opcode==178)gameState.onBike=false;
        scriptWaitSeconds=0.35;scriptWaitVar=0;
    }
    const HgMessageBank& currentScriptMessages(){
        std::uint16_t id=scriptVm.messageBankId();
        if(id==0xffff){if(auto* h=romWorld.header())id=h->msgBank;}
        auto it=scriptMessageCache.find(id);
        if(it!=scriptMessageCache.end())return it->second;
        auto [put,_]=scriptMessageCache.emplace(id,HgMessageBank(load_hg_message_bank(assets,id)));
        return put->second;
    }
    std::optional<HgGlobalScriptResolution> resolveGlobalScript(std::uint16_t id) const {
        for(auto const& e:globalScriptTable){
            if(id<e.minScriptId)continue;
            const std::uint32_t local=std::uint32_t(id-e.minScriptId)+1u;
            if(local>0xffffu)return std::nullopt;
            return HgGlobalScriptResolution{true,id,e.scriptBank,e.messageBank,std::uint16_t(local)};
        }
        return std::nullopt;
    }
    bool continueRomScript(bool carriedInput=false){
        if(!romWorldReady||!scriptVm.active())return false;
        for(int guard=0;guard<256&&scriptVm.active();guard++){
            auto y=scriptVm.runUntilYield(nullptr);
            if(y.type==HgScriptYield::Type::Message){
                HgDecodedMessage dm;
                if(y.opcode==439||y.opcode==440){
                    // MsgBoxExtern/NonNPCMsgExtern address a different message member.
                    // GetStdMsgNaix stores the selected member in y.a. Try that retail
                    // scenario member first, then fall back to the local bank rather
                    // than aborting the entire New Bark optional-photo script.
                    auto raw=read_narc_member(assets/"msgdata/scenario/scr_msg.narc",y.a);
                    HgMessageBank ext(std::move(raw));
                    if(ext.valid())dm=ext.decode(y.messageId,gameState.playerName.empty()?"PLAYER":gameState.playerName,std::vector<std::string>(gameState.formatSlots.begin(),gameState.formatSlots.end()));
                }
                if(!dm.valid){auto const& mb=currentScriptMessages();dm=mb.decode(y.messageId,gameState.playerName.empty()?"PLAYER":gameState.playerName,std::vector<std::string>(gameState.formatSlots.begin(),gameState.formatSlots.end()));}
                if(dm.valid&&!dm.pages.empty()){
                    scriptMessagesShown++;speaker.clear();dialogue=dm.pages;dialoguePage=0;mode=Mode::Dialogue;
                    // Rendering is instantaneous in the PC host, so the retail text-printer
                    // native wait is already satisfied. Continue until the script's real
                    // WaitButton/choice/movement wait is reached.
                }
                continue;
            }
            if(y.type==HgScriptYield::Type::WaitFrames){
                scriptWaitSeconds=std::max(0.0,double(y.a)/30.0);scriptWaitVar=y.b;
                if(scriptWaitSeconds<=0){scriptVm.writeVar(scriptWaitVar,0);scriptWaitVar=0;continue;}
                return true;
            }
            if(y.type==HgScriptYield::Type::WaitInput){
                if(carriedInput){if(y.a>=0x4000)scriptVm.writeVar(y.a,1);carriedInput=false;continue;}
                scriptWaitingInput=true;scriptWaitInputVar=y.a;
                if(!dialogue.empty())mode=Mode::Dialogue;else mode=Mode::Field;
                return true;
            }
            if(y.type==HgScriptYield::Type::Sound){
                // Match the retail field sound command table instead of treating
                // every non-PlayBGM operand as an SSEQ id. In particular, 84/85
                // are BGM fade controls and their operands are levels/frames.
                switch(y.opcode){
                    case 73: playSfxSequence(y.a,0.34f,3.0); break;             // PlaySE
                    case 74: audio.stopSfx(); break;                            // StopSE
                    case 76: /* Pokemon cries use the cry wave bank, not SSEQ ids. */ break;
                    case 78:                                                     // PlayFanfare
                        if(currentBgm&&!audio.bgmPaused()){audio.pauseBgm();fanfarePausedBgm=true;}
                        playSfxSequence(y.a,0.34f,8.0); break;
                    case 80: playBgmSequence(y.a,0.18f); break;                // PlayBGM
                    case 81: audio.stopBgm();currentBgm=0;fanfarePausedBgm=false;resumeBgmAfterSoundWait=false; break; // StopBGM
                    case 82: audio.stopBgm();currentBgm=0;fanfarePausedBgm=false;resumeBgmAfterSoundWait=false;restoreFieldBgm(); break; // ResetBGM
                    case 83: break;                                             // retail sound-state helper
                    case 84:                                                    // FadeOutBGM(target, frames)
                        if(y.a==0)audio.stopBgm();
                        break;
                    case 85:                                                    // FadeInBGM(frames)
                        if(currentBgm){auto id=currentBgm;currentBgm=0;playBgmSequence(id,0.18f);}
                        else restoreFieldBgm();
                        break;
                    case 87: playBgmSequence(y.a,0.18f); break;                // TempBGM
                    default: break;
                }
                continue;
            }
            if(y.type==HgScriptYield::Type::WaitSound){
                // The current audio backend does not expose per-sequence completion.
                // Use the retail wait boundary so scripts still block instead of running
                // ahead; fanfares get a longer conservative native wait than SE/cry.
                scriptWaitSeconds=(y.opcode==79?1.25:0.25);scriptWaitVar=0;
                if(y.opcode==79&&fanfarePausedBgm)resumeBgmAfterSoundWait=true;
                return true;
            }
            if(y.type==HgScriptYield::Type::Movement){applyMovementYield(y);continue;}
            if(y.type==HgScriptYield::Type::WaitMovement){
                if(allScriptMovementDone())continue;
                scriptWaitingMovement=true;mode=Mode::Field;return true;
            }
            if(y.type==HgScriptYield::Type::ObjectCommand){
                const int id=int(y.a);
                switch(y.opcode){
                    case 52: break; // message window open: dialogue layer is lazy-created
                    case 53: dialogue.clear();speaker.clear();dialoguePage=0;if(mode==Mode::Dialogue)mode=Mode::Field;break;
                    case 54: break; // hold message: keep current dialogue contents
                    case 96: scriptObjectsLockedAll=true;break;
                    case 97: scriptObjectsLockedAll=false;scriptLockedObjects.clear();break;
                    case 98: scriptLockedObjects.insert(id);break;
                    case 99: scriptLockedObjects.erase(id);break;
                    case 100: scriptHiddenObjects.erase(id);restoreScriptObject(id);refreshRomVisualAssets();break;
                    case 101: scriptHiddenObjects.insert(id);runtimeNpcs.erase(id);break;
                    case 102: break; // camera target is not yet separately rendered
                    case 103: break; // camera release
                    case 104:{
                        int faceId=activeFacingNpcId;
                        if(faceId>=0){auto it=runtimeNpcs.find(faceId);if(it!=runtimeNpcs.end())it->second.facing=dirFromRetail((retailDirection(facing)+2)&3);}
                        break;
                    }
                    case 107: gameState.followerEnabled=true;gameState.followerPartySlot=std::uint8_t(std::min<std::uint16_t>(5,y.a));gameState.followerSpecies=(y.b?y.b:(gameState.followerPartySlot<gameState.party.size()?gameState.party[gameState.followerPartySlot].species:0));break;
                    case 108: if(y.b==0)gameState.followerEnabled=false;break;
                    case 109: scriptMovementOverrides[id]=y.b;break;
                    case 601: break; // FollowMonFacePlayer: rendered follower already tracks player.
                    case 602: scriptFollowerMovementEnabled=(y.a!=0);break;
                    case 603: break; // follower movement is synchronous in native renderer
                    case 604:
                        if(!gameState.party.empty()){gameState.followerEnabled=true;gameState.followerPartySlot=0;gameState.followerSpecies=gameState.party[0].species;}
                        break;
                    case 605: break; // starter-ball/follower setup helper; no separate native task needed
                    case 606: case 607: break;
                    case 608:
                        if(!gameState.party.empty()){gameState.followerEnabled=true;gameState.followerPartySlot=0;gameState.followerSpecies=gameState.party[0].species;}
                        break;
                    case 609: break; // retail follower/map-event preamble
                    case 610: break;
                    case 436: break; // healing-machine visual helper; field renderer remains resident
                    case 582: // special New Bark/Elm 2F relocation helper
                        gameState.dynamicWarp={true,y.a,0,y.b,y.c,std::uint16_t(retailDirection(facing))};break;
                    case 596: break; // follower transition preparation
                    case 600: if(!gameState.party.empty()){gameState.followerEnabled=true;gameState.followerPartySlot=0;gameState.followerSpecies=gameState.party[0].species;}break;
                    case 621: break; // starter balls are already authored by the loaded lab resources
                    case 746: case 747: break; // DS bottom-screen show/hide; host choice UI owns this layer
                    case 251: showToast("CATCHING TUTORIAL");break;
                    case 338:{
                        auto &n=runtimeNpcs[id];
                        n.x=n.homeX=n.startX=n.targetX=float(y.b);n.y=n.homeY=n.startY=n.targetY=float(y.c);n.move=1.0f;break;
                    }
                    case 339:{
                        auto &n=runtimeNpcs[id];
                        n.x=n.homeX=n.startX=n.targetX=float(y.b);n.y=n.homeY=n.startY=n.targetY=float(y.d);n.facing=dirFromRetail(int(y.word));n.move=1.0f;break;
                    }
                    case 340: scriptMovementOverrides[id]=y.b;break;
                    case 341:{auto it=runtimeNpcs.find(id);if(it!=runtimeNpcs.end())it->second.facing=dirFromRetail(y.b);break;}
                    case 375: scriptHiddenObjects.erase(id);restoreScriptObject(id);refreshRomVisualAssets();break;
                    default: break;
                }
                continue;
            }
            if(y.type==HgScriptYield::Type::PositionQuery){
                if(y.opcode==105){scriptVm.writeVar(y.a,std::uint16_t(tx));scriptVm.writeVar(y.b,std::uint16_t(ty));}
                else if(y.opcode==106){
                    int ox=0xffff,oy=0xffff;auto it=runtimeNpcs.find(int(y.a));
                    if(it!=runtimeNpcs.end()){ox=int(std::round(it->second.x));oy=int(std::round(it->second.y));}
                    scriptVm.writeVar(y.b,std::uint16_t(ox));scriptVm.writeVar(y.c,std::uint16_t(oy));
                }
                continue;
            }
            if(y.type==HgScriptYield::Type::Choice){
                scriptChoiceDest=y.a;scriptChoiceCanCancel=y.flag;scriptChoiceOptions.clear();
                if(y.opcode==63||y.opcode==748){scriptChoiceOptions={{"YES",0},{"NO",1}};scriptChoiceIndex=0;}
                else{
                    auto const& mb=currentScriptMessages();
                    for(auto const& o:y.choices){auto dm=mb.decode(o.messageId,gameState.playerName.empty()?"PLAYER":gameState.playerName,std::vector<std::string>(gameState.formatSlots.begin(),gameState.formatSlots.end()));std::string label=dm.valid?dm.text:std::string{};if(label.empty())label="OPTION "+std::to_string(o.value);auto nl=label.find('\n');if(nl!=std::string::npos)label.resize(nl);scriptChoiceOptions.emplace_back(std::move(label),o.value);}
                    if(scriptChoiceOptions.empty())scriptChoiceOptions={{"OK",0}};
                    scriptChoiceIndex=std::clamp<int>(y.b,0,int(scriptChoiceOptions.size())-1);
                }
                mode=Mode::ScriptChoice;return true;
            }
            if(y.type==HgScriptYield::Type::FieldTransition){
                if(y.opcode==174){ // FadeScreen: native renderer currently transitions atomically.
                    scriptWaitSeconds=std::max(0.03,double(y.b)/30.0);scriptWaitVar=0;return true;
                }
                if(y.opcode==176){ // Warp map, warp-id, x, y, facing
                    if(!romWorld.loadMap(int(y.a),int(y.c),int(y.d))){
                        std::cerr<<"[HG SCRIPT] warp failed map="<<y.a<<" x="<<y.c<<" y="<<y.d<<"\n";
                        showToast("SCRIPT WARP FAILED: MAP "+std::to_string(y.a));scriptVm.stop();return false;
                    }
                    facing=dirFromRetail(int(y.word));retailPlayerMoves.clear();scriptNpcMoves.clear();scriptHiddenObjects.clear();scriptMovementOverrides.clear();
                    syncFromRomWorld();refreshRomVisualAssets();runtimeNpcMap=-1;syncRuntimeNpcs();reloadScriptHeader(false);stepsSinceEncounter=0;continue;
                }
                if(y.opcode==219||y.opcode==279){ // Battle/overworld WhiteOut
                    for(auto &m:gameState.party)m.hp=m.maxHp;
                    if(!romWorld.loadMap(60,6,11)){showToast("WHITE OUT");}
                    facing=Dir::Down;syncFromRomWorld();refreshRomVisualAssets();runtimeNpcMap=-1;syncRuntimeNpcs();reloadScriptHeader(false);stepsSinceEncounter=0;continue;
                }
            }
            if(y.type==HgScriptYield::Type::AppCommand){
                switch(y.opcode){
                    case 143: beginAppNameEntry(NameTarget::Rival);return true;
                    case 376: appResumeScript=true;pcIndex=0;pcPartySide=false;mode=Mode::PCStorage;return true;
                    case 425:{
                        beginDialogue("",{y.a==2?"Congratulations! This certifies your special bond with your Pokemon.":"Congratulations! Your achievement has been recorded."});
                        resumeScriptAfterDialogue=true;return true;
                    }
                    case 150: continue; // RestoreOverworld: field renderer is already resident.
                    case 151: case 152: case 153: case 154: case 155: case 156:
                    case 158: case 159: case 160: case 161: case 162:
                        appResumeScript=true;mode=Mode::Pokegear;return true;
                    case 157: appResumeScript=true;mode=Mode::TownMap;return true;
                    case 163:
                        showToast("HALL OF FAME / CREDITS APPLICATION");gameState.gameCleared=true;continue;
                    case 164: case 165: case 166: continue;
                    case 172: beginAppNameEntry(NameTarget::Player,0,y.a);return true;
                    case 173: beginAppNameEntry(NameTarget::Nickname,y.a,y.b);return true;
                    case 177: case 178: case 179: case 182: useFieldMove(y.opcode,y.a);return true;
                    case 275: case 276: case 277: case 278: openMart(y.opcode,y.a);return true;
                    case 771: case 772: openPokeathlonMart(y.opcode);return true;
                    case 782: openMartSell();return true;
                    case 349:
                        partySelectForScript=true;partyIndex=0;mode=Mode::Party;return true;
                    case 352:
                        summaryIndex=std::clamp<int>(y.b,0,std::max(0,int(gameState.party.size())-1));summaryReturnToScript=true;mode=Mode::Summary;return true;
                    case 369:{
                        for(auto& m:gameState.party)if(m.egg){m.egg=false;if(m.nickname.empty())m.nickname=hg_species_name(m.species);beginDialogue("",{"Oh? The EGG is hatching...",m.nickname+" hatched from the EGG!"});resumeScriptAfterDialogue=true;return true;}
                        continue;
                    }
                    case 615: showToast("PHOTO SAVED");continue;
                    case 617:{beginDialogue("PHOTO ALBUM",{gameState.savedPhotos?"Saved photos: "+std::to_string(gameState.savedPhotos):"There are no saved photos yet."});resumeScriptAfterDialogue=true;return true;}
                    case 794:{
                        bankAmountDest=y.a;bankDeposit=(y.word!=0);
                        bankAmountMax=bankDeposit?std::min<std::uint32_t>(gameState.money,999999u-gameState.momSavings):std::min<std::uint32_t>(gameState.momSavings,999999u-gameState.money);
                        bankAmount=bankAmountMax?std::min<std::uint32_t>(bankAmountMax,1000u):0;appResumeScript=true;mode=Mode::BankAmount;return true;
                    }
                    case 378: showToast("RANKINGS APPLICATION OPENED");continue;
                    default:
                        // The field VM has already consumed this retail command using
                        // its exact HG/SS operand layout.  A number of commands launch
                        // Nitro overlay applications (Frontier/Pokeathlon/Pal Park,
                        // minigames, special UI tasks, etc.) which do not yet have a
                        // native host screen.  Do not terminate the original field
                        // script at that boundary: retail resumes the caller after the
                        // application task closes.  Preserve that control-flow contract
                        // and leave a diagnostic so missing native overlay behavior is
                        // still visible during compatibility work.
                        std::cerr<<"[HG SCRIPT] map="<<romWorld.mapId()<<" script="<<activeScriptNumber
                                 <<" pc=0x"<<std::hex<<scriptVm.pc()<<std::dec
                                 <<" retail application-opcode="<<y.opcode
                                 <<" has no native overlay yet; resuming field script\n";
                        continue;
                }
            }
            if(y.type==HgScriptYield::Type::CommonScript){
                // GotoStd/CallStd are retail bytecode transfers, not host-side
                // shortcuts. Resolve the global ID through the exact dispatch
                // table recovered from this ROM's decoded ARM9, load that
                // scr_seq NARC member and switch the matching retail text bank.
                auto r=resolveGlobalScript(y.a);
                if(!r){
                    std::cerr<<"[HG SCRIPT] global script "<<y.a<<" is outside the retail dispatch table\n";
                    showToast("UNKNOWN RETAIL STANDARD SCRIPT "+std::to_string(y.a));scriptVm.stop();return false;
                }
                auto bank=load_hg_script_bank(assets,r->scriptBank);
                if(!scriptVm.enterExternalBank(bank,r->localScriptNumber,r->messageBank,y.flag)){
                    std::cerr<<"[HG SCRIPT] failed to enter global="<<y.a<<" bank="<<r->scriptBank
                             <<" local="<<r->localScriptNumber<<" call="<<y.flag<<"\n";
                    showToast("RETAIL STANDARD SCRIPT LOAD FAILED: "+std::to_string(y.a));scriptVm.stop();return false;
                }
                continue;
            }
            if(y.type==HgScriptYield::Type::Save){saveInternal();continue;}
            if(y.type==HgScriptYield::Type::StarterChoice){
                // ChooseStarter only enters the application. Retail story state is
                // committed later by SetFlag/SetStarterChoice/scene-variable commands.
                // Do not mark Elm's sequence complete merely because the chooser opened.
                retailStarterSelectionPending=true;starterIndex=0;nativeStarterSelection=false;mode=Mode::StarterSelect;return true;
            }
            if(y.type==HgScriptYield::Type::Battle){resumeScriptAfterBattle=true;beginTrainerBattle(y.a);return true;}
            if(y.type==HgScriptYield::Type::WildBattle){
                HgMon e;e.species=y.a;e.level=std::uint8_t(std::clamp<int>(y.b,1,100));e.maxHp=std::uint16_t(10+e.level*3);e.hp=e.maxHp;e.moves={33,0,0,0};resumeScriptAfterBattle=true;beginBattle(e,false,0);return true;
            }
            if(y.type==HgScriptYield::Type::Unsupported){
                std::cerr << "[HG SCRIPT] map=" << romWorld.mapId() << " script=" << activeScriptNumber
                          << " pc=0x" << std::hex << scriptVm.pc() << std::dec
                          << " unsupported-opcode=" << y.opcode << " detail=" << y.detail << "\n";
                resumeScriptAfterDialogue=false;activeFacingNpcId=-1;showToast("SCRIPT STOPPED SAFELY: OPCODE "+std::to_string(y.opcode));return false;
            }
            if(y.type==HgScriptYield::Type::Error){resumeScriptAfterDialogue=false;showToast("SCRIPT ERROR: "+y.detail);return false;}
            if(y.type==HgScriptYield::Type::Finished){
                if(retailStarterSelectionPending&&gameState.gotStarter){gameState.elmLabIntroDone=true;retailStarterSelectionPending=false;}
                activeFacingNpcId=-1;activeBackgroundId=-1;activeScriptNumber=0;scriptWaitingInput=false;scriptWaitingMovement=false;
                if(!dialogue.empty()){dialogue.clear();speaker.clear();dialoguePage=0;}if(mode==Mode::Dialogue)mode=Mode::Field;break;
            }
            if(y.type==HgScriptYield::Type::None){showToast("SCRIPT COMMAND BUDGET EXHAUSTED");return true;}
        }
        resumeScriptAfterDialogue=false;if(!scriptVm.active())startNextMapScript();return false;
    }
    bool startRomScript(std::uint16_t scriptNumber){
        if(!romWorldReady||scriptNumber==0)return false;
        // Map-script headers and event records may point either at a local entry
        // number or directly at one of HG/SS's global standard-script IDs.  The
        // latter are *not* local indices: resolve them through the same 30-range
        // ARM9 dispatch table used by RunScript/CallStd so map load/resume/frame
        // hooks reach the retail script and retail message bank.
        activeScriptNumber=scriptNumber;scriptMessagesShown=0;resumeScriptAfterDialogue=false;
        scriptVm.setInteractionContext(activeFacingNpcId,activeBackgroundId,retailDirection(facing));
        if(scriptNumber>=2000){
            auto r=resolveGlobalScript(scriptNumber);
            if(!r){std::cerr<<"[HG SCRIPT] root global script "<<scriptNumber<<" is outside the retail dispatch table\n";return false;}
            auto bank=load_hg_script_bank(assets,r->scriptBank);
            if(!scriptVm.start(bank,r->localScriptNumber,r->messageBank)){
                std::cerr<<"[HG SCRIPT] failed root global="<<scriptNumber<<" bank="<<r->scriptBank
                         <<" local="<<r->localScriptNumber<<"\n";
                return false;
            }
            return continueRomScript();
        }
        const std::uint16_t msgId=romWorld.header()?romWorld.header()->msgBank:0xffff;
        if(!scriptVm.start(romWorld.scriptBank(),scriptNumber,msgId))return false;
        return continueRomScript();
    }
    void doWarpIfNeeded(){
        if(romWorldReady){if(romWorld.processWarp()){onMapEntered(true);showToast(romWorld.locationName());}return;}
        for(auto const& w:map().warps) if(w.x==tx&&w.y==ty){ mapIndex=w.targetMap;tx=w.targetX;ty=w.targetY;rx=float(tx);ry=float(ty);fromX=toX=tx;fromY=toY=ty;moveProgress=1.0f;facing=w.targetFacing;showToast(maps[mapIndex].name);return; }
    }
    void tryMove(Dir d,bool run){
        facing=d; auto [dx,dy]=delta(d); int nx=tx+dx,ny=ty+dy;
        playerLedgeJump=false;
        if(romWorldReady){
            // A retail building entrance owns the final step even when the closed
            // doorway's collision plate itself is not walkable.  Test the authored
            // warp before generic terrain collision, while still respecting a real
            // visible NPC occupying the doorway.
            if(auto const* w=warpAt(nx,ny);w&&isBuildingTransition(*w)){if(npcAt(nx,ny))return;beginBuildingTransition(*w,d);return;}

            // HG/SS ledges are directional metatile behaviors (56..59), not merely
            // collision blockers. Crossing one advances two grid cells and uses a
            // hop arc; the landing cell must still be legal and unoccupied.
            if(auto const* permission=romWorld.permissionAt(nx,ny);permission&&hg_permission_is_ledge_jump(*permission,dx,dy)){
                const int lx=nx+dx,ly=ny+dy;
                if(!npcAt(nx,ny)&&!npcAt(lx,ly)&&(debugWalkThroughWalls||romWorld.canMoveTo(lx,ly))){
                    fromX=tx;fromY=ty;toX=lx;toY=ly;moveProgress=0.0f;playerLedgeJump=true;return;
                }
                return;
            }
            if((!debugWalkThroughWalls&&!romWorld.canMoveTo(nx,ny))||npcAt(nx,ny))return;
        } else if((!debugWalkThroughWalls&&!passable(map().get(nx,ny)))||npcAt(nx,ny))return;
        fromX=tx;fromY=ty;toX=nx;toY=ny;moveProgress=0.0f;(void)run;
    }
    std::string interactionLabel() const{
        auto [dx,dy]=delta(facing);int ix=tx+dx,iy=ty+dy;
        if(romWorldReady){
            int objectX=ix,objectY=iy;
            if(auto const* o=interactionNpcAhead(objectX,objectY))return "E / ENTER  TALK - OBJECT "+std::to_string(o->id);
            for(auto const& e:romWorld.events().spawnables)if(e.x==ix&&e.y==iy)return "E / ENTER  SCRIPT "+std::to_string(e.scriptNumber);
            for(auto const& w:romWorld.events().warps)if(w.x==ix&&w.y==iy)return "E / ENTER  ENTER";
            return {};
        }
        for(auto const& n:map().npcs) if(n.x==ix&&n.y==iy) return "E / ENTER  TALK - "+n.name;
        for(auto const& sg:map().signs) if(sg.x==ix&&sg.y==iy) return "E / ENTER  READ";
        if(map().get(ix,iy)==Tile::Door) return "E / ENTER  ENTER";
        return {};
    }
    void interact(){
        auto [dx,dy]=delta(facing);int ix=tx+dx,iy=ty+dy;
        if(romWorldReady){
            int objectX=ix,objectY=iy;
            if(auto const* o=interactionNpcAhead(objectX,objectY)){
                activeFacingNpcId=int(o->id);activeBackgroundId=-1;
                switch(facing){case Dir::Down:activeFacingNpcDir=Dir::Up;break;case Dir::Up:activeFacingNpcDir=Dir::Down;break;case Dir::Left:activeFacingNpcDir=Dir::Right;break;case Dir::Right:activeFacingNpcDir=Dir::Left;break;}
                if(startRomScript(o->scriptNumber))return;
                activeFacingNpcId=-1;
                HgMessageBank mb(romWorld.messageBank());
                std::ostringstream a;a<<"SCRIPT "<<o->scriptNumber<<" COULD NOT YIELD A RECOVERED MESSAGE";beginDialogue("ROM SCRIPT",{a.str()});return;}
            for(std::size_t ei=0;ei<romWorld.events().spawnables.size();++ei){auto const& e=romWorld.events().spawnables[ei];if(e.x==ix&&e.y==iy){
                activeFacingNpcId=-1;activeBackgroundId=int(ei);
                if(startRomScript(e.scriptNumber))return;
                activeBackgroundId=-1;std::ostringstream a;a<<"SCRIPT "<<e.scriptNumber<<" STOPPED ON AN UNIMPLEMENTED ENGINE SPECIAL";beginDialogue("ROM SCRIPT",{a.str()});return;}}
            if(auto const* w=warpAt(ix,iy)){
                if(isBuildingTransition(*w)){beginBuildingTransition(*w,facing);return;}
                if(romWorld.useWarpAt(ix,iy)){onMapEntered(true);showToast(romWorld.locationName());return;}
            }
            return;
        }
        for(auto& n:map().npcs) if(n.x==ix&&n.y==iy){ n.facing = static_cast<Dir>((static_cast<int>(facing)+2)%4); beginDialogue(n.name,n.lines); return; }
        for(auto const& sg:map().signs) if(sg.x==ix&&sg.y==iy){ beginDialogue("",sg.lines); return; }
        Tile t=map().get(ix,iy);if(t==Tile::Door){tryMove(facing,false);return;}if(t==Tile::Wall||t==Tile::Counter)beginDialogue("",{"Nothing unusual here yet."});
    }
    void menuAction(){
        static const char* items[]={"POKEDEX","POKEMON","PC STORAGE","BAG","POKEGEAR","SPRITES","REAL MODELS","WORLD DATA","SAVE","CONTROLS","CLOSE"};
        std::string item=items[menuIndex];
        if(item=="POKEDEX")mode=Mode::Pokedex;
        else if(item=="POKEMON"){partyIndex=0;mode=Mode::Party;}
        else if(item=="PC STORAGE"){pcIndex=0;pcPartySide=false;mode=Mode::PCStorage;}
        else if(item=="BAG")mode=Mode::Bag;
        else if(item=="POKEGEAR"){
            if(!hasPokegear()){showToast("MOM HAS YOUR POKEGEAR AT HOME");return;}
            mode=Mode::Pokegear;
        }
        else if(item=="SPRITES"){mode=Mode::SpriteViewer;spriteViewMember=69;spriteViewFrame=0;loadSpriteViewerMember();}
        else if(item=="REAL MODELS"){mode=Mode::AssetViewer;loadAssetMember();}
        else if(item=="WORLD DATA"){if(romWorldReady){mode=Mode::TerrainSandbox;terrainX=romWorld.localX()-16;terrainY=romWorld.localY()-16;terrainHeight=romWorld.sampleHeightAt(romWorld.x(),romWorld.y(),0.0f);}else beginDialogue("SYSTEM",{"ROM world data is unavailable. Prepare the ROM assets first."});}
        else if(item=="SAVE"){savePromptIndex=0;mode=Mode::SavePrompt;}
        else if(item=="CONTROLS"){mode=Mode::Field;beginDialogue("CONTROLS",{"WASD or arrows move. Hold Shift to run.","E, Z, Enter or Space interacts. X or Escape opens/backtracks menus.","The native menu now exposes Pokedex, party and Bag state. Wild encounters use the ROM encounter tables.","F2 opens real models. F4 inspects the active ROM world chunk. F5 saves and F9 loads."});}
        else if(item=="CLOSE") mode=Mode::Field;
    }
    std::filesystem::path assetArchivePath() const {
        if (assetSource == AssetSource::Room) return assets/"fielddata/build_model/bm_room.narc";
        if (assetSource == AssetSource::Land) return assets/"a/0/6/5";
        return assets/"fielddata/build_model/bm_field.narc";
    }
    std::string assetSourceName() const {
        if (assetSource == AssetSource::Room) return "ROOM";
        if (assetSource == AssetSource::Land) return "LAND";
        return "FIELD";
    }
    void loadAssetMember(){
        auto info=inspect_narc(assetArchivePath());
        assetMemberCount=info.valid?info.members.size():0;
        if(assetMemberCount==0){assetMember={};assetMember.error="model archive unavailable";assetLandChunk={};return;}
        assetIndex%=assetMemberCount;
        assetLandChunk={};
        if(assetSource==AssetSource::Land){
            assetLandChunk=load_land_chunk(assetArchivePath(),assetIndex);
            assetMember=assetLandChunk.model;
            // Resolve external terrain textures through the same supported map-header -> matrix -> area chain
            // used by the playable ROM world, rather than using the old hard-coded land-60 texture guess.
            for(auto const& h:hg_supported_map_headers()){
                auto mx=load_hg_map_matrix(assets,h.matrixId);if(!mx.valid)continue;
                if(std::find(mx.landMembers.begin(),mx.landMembers.end(),std::uint16_t(assetIndex))==mx.landMembers.end())continue;
                auto ar=load_hg_area_data(assets,h.areaDataBank);if(!ar.valid)continue;
                auto ext=load_nitro_texture_from_narc(assets/"a/0/4/4",ar.mapTileset);
                if(ext.valid){bind_nsbmd_external_textures(assetMember,ext);break;}
            }
            if(!assetLandChunk.valid && assetMember.error.empty()) assetMember.error=assetLandChunk.error;
        }else{
            assetMember=load_nsbmd_from_narc(assetArchivePath(),assetIndex);
        }
        if(!assetMember.valid)showToast("MODEL PARSE FAILED");
    }
    void changeAsset(int delta){
        if(assetMemberCount==0)loadAssetMember();
        if(assetMemberCount==0)return;
        long long n=static_cast<long long>(assetIndex)+delta;
        long long c=static_cast<long long>(assetMemberCount);
        n=(n%c+c)%c;
        assetIndex=static_cast<std::size_t>(n);
        loadAssetMember();
    }
    void cycleAssetArchive(){
        if(assetSource==AssetSource::Field) assetSource=AssetSource::Room;
        else if(assetSource==AssetSource::Room) assetSource=AssetSource::Land;
        else assetSource=AssetSource::Field;
        assetIndex=(assetSource==AssetSource::Field)?1:0;
        loadAssetMember();
        showToast(assetSourceName()+" MODEL ARCHIVE");
    }

    bool terrainMove(int dx,int dy){
        const LandChunk* land=nullptr;
        if(romWorldReady){
            if(auto* c=romWorld.chunkForTile(romWorld.x(),romWorld.y())) land=&c->land;
        }else if(land60Chunk.valid) land=&land60Chunk;
        if(!land||!land->bdhc.valid) return false;
        int nx=terrainX+dx,ny=terrainY+dy;float h=0;
        if(nx<-16||nx>16||ny<-16||ny>16||!sample_bdhc_height(land->bdhc,float(nx),float(ny),h)) return false;
        terrainX=nx;terrainY=ny;terrainHeight=h;return true;
    }

    struct SaveSummary { bool valid=false; std::string name="PLAYER"; double play=0; int badges=0; int dexOwned=0; int mapId=0; };
    bool saveExists() const { std::error_code ec; return std::filesystem::is_regular_file(savePath,ec); }
    SaveSummary readSaveSummary() const {
        SaveSummary r; std::ifstream in(savePath); if(!in)return r; std::string version; if(!std::getline(in,version))return r;
        if(version.rfind("HG_NATIVE_WORLD_V",0)!=0)return r;
        r.valid=true; std::string line;
        while(std::getline(in,line)){auto eq=line.find('=');if(eq==std::string::npos)continue;auto k=line.substr(0,eq),v=line.substr(eq+1);try{
            if(k=="name")r.name=v;else if(k=="play")r.play=std::stod(v);else if(k=="badges")r.badges=std::stoi(v);else if(k=="owned")r.dexOwned++;else if(k=="map")r.mapId=std::stoi(v);
        }catch(...){} }
        return r;
    }
    std::string ngMessage(std::uint16_t id) const {
        if(!newGameMessages.valid())return {};
        auto dm=newGameMessages.decode(id,gameState.playerName.empty()?"PLAYER":gameState.playerName);
        return dm.valid?dm.text:std::string{};
    }
    void beginNewGame(){
        gameState=HgGameState{}; gameState.playerName="ETHAN"; gameState.female=false; gameState.newGameStarted=true; gameState.momIntroDone=false; gameState.badges=0;
        scriptVm.bindState(&gameState); scriptVm.stop(); clearScriptHostState(); pendingMapScripts.clear(); frameScriptLatch=0; playSeconds=0; newGameStage=0; newGameGender=0; newGameNameCursor=0; newGameName.clear(); newGameClock=0; newGameStage=1; mode=Mode::NewGameIntro;
        // HeartGold's New Game application has its own STARTING music; title
        // music continuing under Oak was a native-port bug.
        playBgmSequence(SEQ_GS_STARTING,0.18f);
    }
    void finishNewGameIntro(){
        fieldTransition.clear();
        if(romWorldReady&&romWorld.loadMap(64,7,6)){ facing=Dir::Down; onMapEntered(true); mode=Mode::Field; showToast("YOUR ROOM"); }
        else { mode=Mode::Field; if(romWorldReady)onMapEntered(true); }
    }
    void setNewGameStage(int stage){
        if(stage==newGameStage)return;
        newGameStage=stage;newGameClock=0.0;
        // Keep SEQ_GS_STARTING running through Oak's entire New Game
        // application. STARTING2 is a separate/alternate SDAT sequence and
        // switching to it after the TV lead-in was another v0.14 divergence.
    }
    void advanceNewGame(){
                if(newGameStage==5){gameState.female=newGameGender==1;gameState.playerName=gameState.female?"LYRA":"ETHAN";setNewGameStage(6);return;}
        if(newGameStage==6){newGameName.clear();newGameNameCursor=0;setNewGameStage(7);return;}
        if(newGameStage==7)return; // handled by the native name-entry grid
        if(newGameStage>=11)return; // final shrink/fade completes on its timer
        setNewGameStage(newGameStage+1);
    }
    bool saveInternal(){
        try {
            if(savePath.has_parent_path()) std::filesystem::create_directories(savePath.parent_path());
            std::ofstream o(savePath);
            if(!o)return false;
            if(romWorldReady){
                o<<"HG_NATIVE_WORLD_V10\n"<<"map="<<romWorld.mapId()<<"\n"<<"x="<<romWorld.x()<<"\n"<<"y="<<romWorld.y()<<"\n"<<"facing="<<static_cast<int>(facing)<<"\n"<<std::fixed<<std::setprecision(3)<<"play="<<playSeconds<<"\n";
                o<<"name="<<gameState.playerName<<"\n"<<"rival="<<gameState.rivalName<<"\n"<<"friend="<<gameState.friendName<<"\n"<<"female="<<gameState.female<<"\n"<<"newgame="<<gameState.newGameStarted<<"\n"<<"momintro="<<gameState.momIntroDone<<"\n"<<"elmlabintro="<<gameState.elmLabIntroDone<<"\n"<<"gotstarter="<<gameState.gotStarter<<"\n"<<"badges="<<int(gameState.badges)<<"\n"<<"badgeflags="<<gameState.badgeFlags<<"\n";
                o<<"money="<<gameState.money<<"\n"<<"momsavings="<<gameState.momSavings<<"\n"<<"photos="<<gameState.savedPhotos<<"\n"<<"coins="<<gameState.coins<<"\n"<<"athlete="<<gameState.athletePoints<<"\n"<<"pokeathlonprizes="<<gameState.pokeathlonPrizeFlags<<"\n"<<"pokeathloncards="<<gameState.pokeathlonDataCardFlags<<"\n"<<"pokeathlonday="<<gameState.pokeathlonPrizeDay<<"\n"<<"battlepoints="<<gameState.battlePoints<<"\n"<<"pokegearcards="<<gameState.pokegearCards<<"\n"<<"pokedex="<<gameState.pokedex<<"\n"<<"nationaldex="<<gameState.nationalDex<<"\n"<<"running="<<gameState.runningShoes<<"\n"<<"onbike="<<gameState.onBike<<"\n"<<"bikelocked="<<gameState.bikeLocked<<"\n"<<"escort="<<gameState.escortMode<<"\n"<<"gameclear="<<gameState.gameCleared<<"\n"<<"steptaken="<<gameState.stepTaken<<"\n"<<"strength="<<gameState.strengthActive<<"\n"<<"flash="<<gameState.flashActive<<"\n"<<"defog="<<gameState.defogActive<<"\n"<<"starter="<<gameState.starter<<"\n"<<"spawn="<<gameState.spawnId<<"\n";
                o<<"follower="<<gameState.followerEnabled<<","<<int(gameState.followerPartySlot)<<","<<gameState.followerSpecies<<"\n";
                if(gameState.dynamicWarp.valid)o<<"dynwarp="<<gameState.dynamicWarp.map<<","<<gameState.dynamicWarp.warp<<","<<gameState.dynamicWarp.x<<","<<gameState.dynamicWarp.y<<","<<gameState.dynamicWarp.facing<<"\n";
                {std::vector<std::uint16_t> pn(gameState.phoneNumbers.begin(),gameState.phoneNumbers.end());std::sort(pn.begin(),pn.end());for(auto id:pn)o<<"phone="<<id<<"\n";}
                {std::vector<std::pair<std::uint16_t,std::uint16_t>> ps(gameState.phoneCallState.begin(),gameState.phoneCallState.end());std::sort(ps.begin(),ps.end());for(auto [id,v]:ps)o<<"phonecall="<<id<<","<<v<<"\n";}
                {std::vector<std::pair<std::uint16_t,std::uint16_t>> ss(gameState.seals.begin(),gameState.seals.end());std::sort(ss.begin(),ss.end());for(auto [id,q]:ss)o<<"seal="<<id<<","<<q<<"\n";}
                {std::vector<std::pair<std::uint16_t,std::uint16_t>> dd(gameState.decorations.begin(),gameState.decorations.end());std::sort(dd.begin(),dd.end());for(auto [id,q]:dd)o<<"deco="<<id<<","<<q<<"\n";}
                std::vector<std::pair<std::uint16_t,std::uint16_t>> vv(gameState.vars.begin(),gameState.vars.end());std::sort(vv.begin(),vv.end());for(auto [id,v]:vv)o<<"var="<<id<<","<<v<<"\n";
                std::vector<std::uint16_t> ff(gameState.flags.begin(),gameState.flags.end());std::sort(ff.begin(),ff.end());for(auto id:ff)o<<"flag="<<id<<"\n";
                std::vector<std::uint16_t> tf(gameState.trainerFlags.begin(),gameState.trainerFlags.end());std::sort(tf.begin(),tf.end());for(auto id:tf)o<<"trainerflag="<<id<<"\n";
                std::vector<std::pair<std::uint16_t,std::uint16_t>> bag(gameState.bag.begin(),gameState.bag.end());std::sort(bag.begin(),bag.end());for(auto [id,q]:bag)o<<"item="<<id<<","<<q<<"\n";
                auto safe=[](std::string v){std::replace(v.begin(),v.end(),',',' ');return v;};
                auto writeMon=[&](const char* key,const HgMon& m){o<<key<<"="<<m.species<<","<<int(m.level)<<","<<m.hp<<","<<m.maxHp<<","<<m.heldItem<<","<<int(m.form)<<","<<int(m.ability)<<","<<m.moves[0]<<","<<m.moves[1]<<","<<m.moves[2]<<","<<m.moves[3]<<","<<m.exp<<","<<m.egg<<","<<safe(m.nickname)<<","<<int(m.friendship)<<","<<int(m.gender)<<","<<int(m.status)<<","<<m.mine<<","<<int(m.shinyLeaves)<<","<<m.shinyLeafCrown<<","<<m.attack<<","<<m.defense<<","<<m.spAttack<<","<<m.spDefense<<","<<m.speed<<","<<int(m.pp[0])<<","<<int(m.pp[1])<<","<<int(m.pp[2])<<","<<int(m.pp[3])<<","<<int(m.maxPp[0])<<","<<int(m.maxPp[1])<<","<<int(m.maxPp[2])<<","<<int(m.maxPp[3])<<"\n";};
                for(auto const& m:gameState.party)writeMon("mon",m);
                for(auto const& m:gameState.pcStorage)writeMon("pcmon",m);
                for(std::size_t i=0;i<gameState.daycare.size();i++)if(gameState.daycare[i].occupied){auto const& d=gameState.daycare[i];auto const& m=d.mon;o<<"daycare="<<i<<","<<d.steps<<","<<m.species<<","<<int(m.level)<<","<<m.hp<<","<<m.maxHp<<","<<m.heldItem<<","<<int(m.form)<<","<<int(m.ability)<<","<<m.moves[0]<<","<<m.moves[1]<<","<<m.moves[2]<<","<<m.moves[3]<<","<<m.exp<<","<<m.egg<<","<<safe(m.nickname)<<","<<int(m.friendship)<<","<<int(m.gender)<<","<<int(m.status)<<","<<m.mine<<","<<int(m.shinyLeaves)<<","<<m.shinyLeafCrown<<","<<m.attack<<","<<m.defense<<","<<m.spAttack<<","<<m.spDefense<<","<<m.speed<<","<<int(m.pp[0])<<","<<int(m.pp[1])<<","<<int(m.pp[2])<<","<<int(m.pp[3])<<","<<int(m.maxPp[0])<<","<<int(m.maxPp[1])<<","<<int(m.maxPp[2])<<","<<int(m.maxPp[3])<<"\n";}
                o<<"daycareegg="<<gameState.daycareEggReady<<","<<gameState.daycareEggSpecies<<"\n";
                for(auto id:gameState.dexSeen)o<<"seen="<<id<<"\n";
                for(auto id:gameState.dexOwned)o<<"owned="<<id<<"\n";
            }else o<<"HG_NATIVE_DEMO_V1\n"<<"map="<<mapIndex<<"\n"<<"x="<<tx<<"\n"<<"y="<<ty<<"\n"<<"facing="<<static_cast<int>(facing)<<"\n"<<std::fixed<<std::setprecision(3)<<"play="<<playSeconds<<"\n";
            return bool(o);
        }catch(...){return false;}
    }
    bool loadInternal(){
        std::ifstream in(savePath);if(!in)return false;std::string version;if(!std::getline(in,version))return false;
        int nm=romWorldReady?romWorld.mapId():mapIndex,nx=tx,ny=ty,nf=static_cast<int>(facing);double np=playSeconds;HgGameState loaded;loaded.money=3000;std::string line;
        auto parts=[](const std::string& v){std::vector<std::string> a;std::size_t p=0;while(p<=v.size()){auto q=v.find(',',p);if(q==std::string::npos){a.push_back(v.substr(p));break;}a.push_back(v.substr(p,q-p));p=q+1;}return a;};
        auto parseMon=[](const std::vector<std::string>& a,std::size_t off,HgMon& m)->bool{
            if(a.size()<off+13) return false;
            m.species=std::uint16_t(std::stoul(a[off+0]));
            m.level=std::uint8_t(std::stoul(a[off+1]));
            m.hp=std::uint16_t(std::stoul(a[off+2]));
            m.maxHp=std::uint16_t(std::stoul(a[off+3]));
            m.heldItem=std::uint16_t(std::stoul(a[off+4]));
            m.form=std::uint8_t(std::stoul(a[off+5]));
            m.ability=std::uint8_t(std::stoul(a[off+6]));
            for(int i=0;i<4;i++) m.moves[i]=std::uint16_t(std::stoul(a[off+7+i]));
            m.exp=std::uint32_t(std::stoul(a[off+11]));
            m.egg=std::stoi(a[off+12])!=0;
            if(a.size()>off+13) m.nickname=a[off+13];
            if(a.size()>off+14) m.friendship=std::uint8_t(std::stoul(a[off+14]));
            if(a.size()>off+15) m.gender=std::uint8_t(std::stoul(a[off+15]));
            if(a.size()>off+16) m.status=std::uint8_t(std::stoul(a[off+16]));
            if(a.size()>off+17) m.mine=std::stoi(a[off+17])!=0;
            if(a.size()>off+18) m.shinyLeaves=std::uint8_t(std::stoul(a[off+18]));
            if(a.size()>off+19) m.shinyLeafCrown=std::stoi(a[off+19])!=0;
            if(a.size()>off+24){m.attack=std::uint16_t(std::stoul(a[off+20]));m.defense=std::uint16_t(std::stoul(a[off+21]));m.spAttack=std::uint16_t(std::stoul(a[off+22]));m.spDefense=std::uint16_t(std::stoul(a[off+23]));m.speed=std::uint16_t(std::stoul(a[off+24]));}
            if(a.size()>off+32){for(int i=0;i<4;i++)m.pp[i]=std::uint8_t(std::stoul(a[off+25+i]));for(int i=0;i<4;i++)m.maxPp[i]=std::uint8_t(std::stoul(a[off+29+i]));}
            hg_rehydrate_mon(m);
            if(m.nickname.empty()) m.nickname=hg_species_name(m.species);
            return true;
        };
        while(std::getline(in,line)){auto eq=line.find('=');if(eq==std::string::npos)continue;auto k=line.substr(0,eq),v=line.substr(eq+1);try{
            if(k=="map")nm=std::stoi(v);else if(k=="x")nx=std::stoi(v);else if(k=="y")ny=std::stoi(v);else if(k=="facing")nf=std::stoi(v);else if(k=="play")np=std::stod(v);
            else if(k=="name")loaded.playerName=v;else if(k=="rival")loaded.rivalName=v;else if(k=="friend")loaded.friendName=v;else if(k=="female")loaded.female=std::stoi(v)!=0;else if(k=="newgame")loaded.newGameStarted=std::stoi(v)!=0;else if(k=="momintro")loaded.momIntroDone=std::stoi(v)!=0;else if(k=="elmlabintro")loaded.elmLabIntroDone=std::stoi(v)!=0;else if(k=="gotstarter")loaded.gotStarter=std::stoi(v)!=0;else if(k=="badges")loaded.badges=std::uint8_t(std::stoul(v));else if(k=="badgeflags")loaded.badgeFlags=std::uint16_t(std::stoul(v));
            else if(k=="money")loaded.money=std::uint32_t(std::stoul(v));else if(k=="momsavings")loaded.momSavings=std::uint32_t(std::stoul(v));else if(k=="photos")loaded.savedPhotos=std::uint16_t(std::stoul(v));else if(k=="coins")loaded.coins=std::uint16_t(std::stoul(v));else if(k=="athlete")loaded.athletePoints=std::uint16_t(std::stoul(v));else if(k=="pokeathlonprizes")loaded.pokeathlonPrizeFlags=std::uint16_t(std::stoul(v));else if(k=="pokeathloncards")loaded.pokeathlonDataCardFlags=std::uint32_t(std::stoul(v));else if(k=="pokeathlonday")loaded.pokeathlonPrizeDay=std::uint32_t(std::stoul(v));else if(k=="battlepoints")loaded.battlePoints=std::uint16_t(std::stoul(v));else if(k=="pokegearcards")loaded.pokegearCards=std::uint32_t(std::stoul(v));else if(k=="pokedex")loaded.pokedex=std::stoi(v)!=0;else if(k=="nationaldex")loaded.nationalDex=std::stoi(v)!=0;else if(k=="running")loaded.runningShoes=std::stoi(v)!=0;else if(k=="onbike")loaded.onBike=std::stoi(v)!=0;else if(k=="bikelocked")loaded.bikeLocked=std::stoi(v)!=0;else if(k=="escort")loaded.escortMode=std::stoi(v)!=0;else if(k=="gameclear")loaded.gameCleared=std::stoi(v)!=0;else if(k=="steptaken")loaded.stepTaken=std::stoi(v)!=0;else if(k=="strength")loaded.strengthActive=std::stoi(v)!=0;else if(k=="flash")loaded.flashActive=std::stoi(v)!=0;else if(k=="defog")loaded.defogActive=std::stoi(v)!=0;else if(k=="starter")loaded.starter=std::uint16_t(std::stoul(v));else if(k=="spawn")loaded.spawnId=std::uint16_t(std::stoul(v));
            else if(k=="var"){auto a=parts(v);if(a.size()>=2)loaded.vars[std::uint16_t(std::stoul(a[0]))]=std::uint16_t(std::stoul(a[1]));}
            else if(k=="flag")loaded.flags.insert(std::uint16_t(std::stoul(v)));else if(k=="trainerflag")loaded.trainerFlags.insert(std::uint16_t(std::stoul(v)));else if(k=="phone")loaded.phoneNumbers.insert(std::uint16_t(std::stoul(v)));
            else if(k=="phonecall"){auto a=parts(v);if(a.size()>=2)loaded.phoneCallState[std::uint16_t(std::stoul(a[0]))]=std::uint16_t(std::stoul(a[1]));}
            else if(k=="follower"){auto a=parts(v);if(a.size()>=3){loaded.followerEnabled=std::stoi(a[0])!=0;loaded.followerPartySlot=std::uint8_t(std::stoul(a[1]));loaded.followerSpecies=std::uint16_t(std::stoul(a[2]));}}
            else if(k=="dynwarp"){auto a=parts(v);if(a.size()>=5){loaded.dynamicWarp.valid=true;loaded.dynamicWarp.map=std::uint16_t(std::stoul(a[0]));loaded.dynamicWarp.warp=std::uint16_t(std::stoul(a[1]));loaded.dynamicWarp.x=std::uint16_t(std::stoul(a[2]));loaded.dynamicWarp.y=std::uint16_t(std::stoul(a[3]));loaded.dynamicWarp.facing=std::uint16_t(std::stoul(a[4]));}}
            else if(k=="seal"){auto a=parts(v);if(a.size()>=2)loaded.seals[std::uint16_t(std::stoul(a[0]))]=std::uint16_t(std::stoul(a[1]));}
            else if(k=="deco"){auto a=parts(v);if(a.size()>=2)loaded.decorations[std::uint16_t(std::stoul(a[0]))]=std::uint16_t(std::stoul(a[1]));}
            else if(k=="item"){auto a=parts(v);if(a.size()>=2)loaded.bag[std::uint16_t(std::stoul(a[0]))]=std::uint16_t(std::stoul(a[1]));}
            else if(k=="mon"){auto a=parts(v);HgMon m;if(parseMon(a,0,m))loaded.party.push_back(m);}
            else if(k=="pcmon"){auto a=parts(v);HgMon m;if(parseMon(a,0,m))loaded.pcStorage.push_back(m);}
            else if(k=="daycare"){auto a=parts(v);if(a.size()>=20){auto slot=std::size_t(std::stoul(a[0]));if(slot<loaded.daycare.size()){HgMon m;if(parseMon(a,2,m)){loaded.daycare[slot].occupied=true;loaded.daycare[slot].steps=std::uint32_t(std::stoul(a[1]));loaded.daycare[slot].mon=std::move(m);}}}}
            else if(k=="daycareegg"){auto a=parts(v);if(a.size()>=2){loaded.daycareEggReady=std::stoi(a[0])!=0;loaded.daycareEggSpecies=std::uint16_t(std::stoul(a[1]));}}
            else if(k=="seen")loaded.dexSeen.insert(std::uint16_t(std::stoul(v)));else if(k=="owned")loaded.dexOwned.insert(std::uint16_t(std::stoul(v)));
        }catch(...){}}
        facing=static_cast<Dir>(std::clamp(nf,0,3));playSeconds=std::max(0.0,np);
        if(version=="HG_NATIVE_WORLD_V2"||version=="HG_NATIVE_WORLD_V3"||version=="HG_NATIVE_WORLD_V4"||version=="HG_NATIVE_WORLD_V5"||version=="HG_NATIVE_WORLD_V6"||version=="HG_NATIVE_WORLD_V7"||version=="HG_NATIVE_WORLD_V8"||version=="HG_NATIVE_WORLD_V9"||version=="HG_NATIVE_WORLD_V10"){
            if(!romWorldReady||!romWorld.loadMap(nm,nx,ny))return false;
            if(version=="HG_NATIVE_WORLD_V4"||version=="HG_NATIVE_WORLD_V5"||version=="HG_NATIVE_WORLD_V6"||version=="HG_NATIVE_WORLD_V7"||version=="HG_NATIVE_WORLD_V8"||version=="HG_NATIVE_WORLD_V9"||version=="HG_NATIVE_WORLD_V10")gameState=std::move(loaded);else{gameState.vars=std::move(loaded.vars);gameState.flags=std::move(loaded.flags);}
            // Older saves already containing a starter necessarily passed the lab
            // selection event; promote them into the explicit v6 story flags.
            if(version!="HG_NATIVE_WORLD_V6"&&version!="HG_NATIVE_WORLD_V7"&&version!="HG_NATIVE_WORLD_V8"&&version!="HG_NATIVE_WORLD_V9"&&version!="HG_NATIVE_WORLD_V10"&&gameState.starter){gameState.gotStarter=true;gameState.elmLabIntroDone=true;}
            // v0.11-v0.16 interpreted CheckFlag + GoToIf TRUE/FALSE with the
            // numeric comparison table.  A fresh pre-starter save could therefore
            // execute Elm's postgame S.S. Ticket branch and persist its flag/item.
            // Such a state is impossible in retail HG/SS: no valid game can have
            // the S.S. Ticket/game-clear state before choosing the first Pokemon.
            if(!gameState.gotStarter&&gameState.starter==0){
                gameState.flags.erase(0x006A); // FLAG_GOT_STARTER
                gameState.flags.erase(0x00F2); // FLAG_GOT_SS_TICKET_FROM_ELM
                gameState.flags.erase(0x0964); // FLAG_GAME_CLEAR
                gameState.gameCleared=false;
                gameState.bag.erase(456);      // ITEM_S_S__TICKET
            }
            scriptVm.bindState(&gameState);scriptVm.stop();clearScriptHostState();nativeLabPhase=0;nativeDialogueAction=0;nativeStarterSelection=false;scriptedPlayerMoves.clear();fieldTransition.clear();onMapEntered(false);mode=Mode::Field;
            std::string loadMsg="LEGACY WORLD SAVE LOADED";if(version=="HG_NATIVE_WORLD_V10")loadMsg="V10 BATTLE STATE LOADED";else if(version=="HG_NATIVE_WORLD_V9")loadMsg="V9 GAME STATE LOADED";else if(version=="HG_NATIVE_WORLD_V8")loadMsg="V8 GAME STATE LOADED";else if(version=="HG_NATIVE_WORLD_V7")loadMsg="V7 GAME STATE LOADED";else if(version=="HG_NATIVE_WORLD_V6")loadMsg="V6 GAME STATE LOADED";else if(version=="HG_NATIVE_WORLD_V5")loadMsg="V5 GAME STATE LOADED";else if(version=="HG_NATIVE_WORLD_V4")loadMsg="V4 GAME STATE LOADED";showToast(loadMsg);return true;
        }
        if(version!="HG_NATIVE_DEMO_V1"||nm<0||nm>=static_cast<int>(maps.size()))return false;
        mapIndex=nm;
        tx=std::clamp(nx,1,map().w-2);
        ty=std::clamp(ny,1,map().h-2);
        if(!passable(map().get(tx,ty))){tx=16;ty=11;mapIndex=0;}
        rx=float(tx);ry=float(ty);fromX=toX=tx;fromY=toY=ty;moveProgress=1;
        mode=Mode::Field;showToast("LEGACY SAVE LOADED");return true;
    }

    void update(const InputState& input,double dt){
        dt=std::clamp(dt,0.0,0.05);audioClock+=dt;audio.update();fieldAnimClock+=dt;
        if(input.wasPressed(GameButton::Quit)){quit=true;return;}
        if(input.wasPressed(GameButton::Debug))debug=!debug;
        // F3 opens the debug HUD; the collision override is an explicit clickable
        // option so accidentally pressing F3 never changes gameplay collision.
        if(debug&&mode==Mode::Field&&input.mousePressed&&input.mouseInside){
            constexpr float cbX=866.0f,cbY=225.0f,cbW=360.0f,cbH=28.0f;
            if(input.mouseX>=cbX&&input.mouseX<cbX+cbW&&input.mouseY>=cbY&&input.mouseY<cbY+cbH){
                debugWalkThroughWalls=!debugWalkThroughWalls;showToast(debugWalkThroughWalls?"DEBUG: WALK THROUGH WALLS ON":"DEBUG: WALK THROUGH WALLS OFF");return;
            }
        }
        if(input.wasPressed(GameButton::Reset)){
            gameState=HgGameState{};scriptVm.bindState(&gameState);scriptVm.stop();clearScriptHostState();pendingMapScripts.clear();frameScriptLatch=0;fieldTransition.clear();playerLedgeJump=false;debugWalkThroughWalls=false;
            if(romWorldReady){romWorld.initialize();onMapEntered(true);}else{mapIndex=0;tx=16;ty=11;rx=16;ry=11;fromX=toX=tx;fromY=toY=ty;moveProgress=1;}
            mode=Mode::Intro;introClock=0;introScene=0;playSeconds=0;fieldAnimClock=0;toast.clear();dialogue.clear();currentBgm=0;playBgmSequence(SEQ_GS_OPENING_TITLE_G,0.18f);return;
        }
        if(input.wasPressed(GameButton::Save)&&mode!=Mode::Intro&&mode!=Mode::Title&&mode!=Mode::MainMenu&&mode!=Mode::NewGameIntro){savePromptIndex=0;mode=Mode::SavePrompt;}
        if(input.wasPressed(GameButton::Load)&&mode!=Mode::Intro){if(!loadInternal())showToast("NO VALID SAVE FOUND");}
        if(input.wasPressed(GameButton::Assets)){if(mode==Mode::AssetViewer)mode=Mode::Field;else{mode=Mode::AssetViewer;loadAssetMember();}}
        if(input.wasPressed(GameButton::Terrain)){
            if(mode==Mode::TerrainSandbox)mode=Mode::Field;
            else if(romWorldReady){mode=Mode::TerrainSandbox;terrainX=romWorld.localX()-16;terrainY=romWorld.localY()-16;terrainHeight=romWorld.sampleHeightAt(romWorld.x(),romWorld.y(),0.0f);}
            else if(land60Chunk.valid){mode=Mode::TerrainSandbox;float h=terrainHeight;sample_bdhc_height(land60Chunk.bdhc,float(terrainX),float(terrainY),h);terrainHeight=h;}
        }
        if(toastTime>0)toastTime=std::max(0.0,toastTime-dt);

        if(mode==Mode::Intro){
            introClock+=dt;
            double total=0.0;introScene=0;for(std::size_t i=0;i<introMovie.size();++i){total+=introMovie[i].duration;if(introClock<total){introScene=i;break;}introScene=i;}
            if(input.wasPressed(GameButton::Interact)||input.wasPressed(GameButton::Menu)||introClock>total+0.25){mode=Mode::Title;titleClock=0;playBgmSequence(SEQ_GS_TITLE,0.18f);}
            return;
        }
        if(mode==Mode::Title){
            titleClock+=dt;
            if(input.wasPressed(GameButton::Interact)){auto ss=readSaveSummary();mainMenuIndex=ss.valid?0:1;mode=Mode::MainMenu;}
            return;
        }
        if(mode==Mode::MainMenu){
            if(input.wasPressed(GameButton::Up))mainMenuIndex=(mainMenuIndex+2)%3;
            if(input.wasPressed(GameButton::Down))mainMenuIndex=(mainMenuIndex+1)%3;
            if(input.wasPressed(GameButton::Menu)){mode=Mode::Title;playBgmSequence(SEQ_GS_TITLE,0.18f);return;}
            if(input.wasPressed(GameButton::Interact)){
                if(mainMenuIndex==0){if(!loadInternal())showToast("NO SAVE DATA");}
                else if(mainMenuIndex==1)beginNewGame();
                else showToast("MYSTERY GIFT - FUTURE CUSTOM SERVER");
            }
            return;
        }
        if(mode==Mode::NewGameIntro){
            newGameClock+=dt;
            if(newGameStage==10&&newGameClock>5.8)setNewGameStage(11);
            if(newGameStage==11&&newGameClock>1.85){finishNewGameIntro();return;}
            if(newGameStage==5){if(input.wasPressed(GameButton::Left)||input.wasPressed(GameButton::Right))newGameGender=1-newGameGender;}
            if(newGameStage==7){
                constexpr int cols=6,count=29;
                if(input.wasPressed(GameButton::Left)){int row=newGameNameCursor/cols,col=newGameNameCursor%cols;col=(col+cols-1)%cols;int n=row*cols+col;if(n>=count)n=count-1;newGameNameCursor=n;}
                if(input.wasPressed(GameButton::Right)){int row=newGameNameCursor/cols,col=(newGameNameCursor%cols+1)%cols;int n=row*cols+col;if(n>=count)n=row*cols;newGameNameCursor=std::min(n,count-1);}
                if(input.wasPressed(GameButton::Up)){int n=newGameNameCursor-cols;while(n<0)n+=count;newGameNameCursor=n;}
                if(input.wasPressed(GameButton::Down)){int n=newGameNameCursor+cols;if(n>=count)n%=count;newGameNameCursor=n;}
                if(input.wasPressed(GameButton::Menu)){if(!newGameName.empty())newGameName.pop_back();else newGameStage=6;return;}
                if(input.wasPressed(GameButton::Interact)){
                    if(newGameNameCursor<26){if(newGameName.size()<7)newGameName.push_back(char('A'+newGameNameCursor));}
                    else if(newGameNameCursor==26){if(!newGameName.empty()&&newGameName.size()<7)newGameName.push_back(' ');}
                    else if(newGameNameCursor==27){if(!newGameName.empty())newGameName.pop_back();}
                    else {gameState.playerName=newGameName.empty()?(gameState.female?"LYRA":"ETHAN"):newGameName;setNewGameStage(8);}
                }
                return;
            }
            if(input.wasPressed(GameButton::Interact)){newGameClock=0;advanceNewGame();}
            if(input.wasPressed(GameButton::Menu)&&newGameStage>0){mode=Mode::MainMenu;playBgmSequence(SEQ_GS_TITLE,0.18f);}
            return;
        }
        if(mode==Mode::SavePrompt){
            if(input.wasPressed(GameButton::Left)||input.wasPressed(GameButton::Right)||input.wasPressed(GameButton::Up)||input.wasPressed(GameButton::Down))savePromptIndex=1-savePromptIndex;
            if(input.wasPressed(GameButton::Menu)){mode=Mode::Field;return;}
            if(input.wasPressed(GameButton::Interact)){if(savePromptIndex==0){bool ok=saveInternal();mode=Mode::Field;beginDialogue("SYSTEM",{ok?(gameState.playerName+" saved the game."):"Save failed. Check the save path permissions."});}else mode=Mode::Field;}
            return;
        }
        if(mode==Mode::SpriteViewer){
            int frames=spriteViewResource.valid?int(spriteViewResource.textures.size()):0;
            if(input.wasPressed(GameButton::Left))changeSpriteMember(-1);
            if(input.wasPressed(GameButton::Right))changeSpriteMember(1);
            if(input.isDown(GameButton::Run)&&input.wasPressed(GameButton::Up))changeSpriteMember(-10);else if(input.isDown(GameButton::Run)&&input.wasPressed(GameButton::Down))changeSpriteMember(10);
            else if(input.wasPressed(GameButton::Up)&&frames>0)spriteViewFrame=(spriteViewFrame+frames-1)%frames;else if(input.wasPressed(GameButton::Down)&&frames>0)spriteViewFrame=(spriteViewFrame+1)%frames;
            if(input.wasPressed(GameButton::Interact)&&frames>0)spriteViewFrame=(spriteViewFrame+1)%frames;
            if(input.wasPressed(GameButton::Menu)){mode=Mode::Menu;menuIndex=0;}
            return;
        }
        if(mode==Mode::TerrainSandbox){int step=input.isDown(GameButton::Run)?2:1;if(input.wasPressed(GameButton::Up))terrainMove(0,-step);if(input.wasPressed(GameButton::Down))terrainMove(0,step);if(input.wasPressed(GameButton::Left))terrainMove(-step,0);if(input.wasPressed(GameButton::Right))terrainMove(step,0);if(input.wasPressed(GameButton::Menu))mode=Mode::Field;return;}
        if(mode==Mode::AssetViewer){if(input.isDown(GameButton::Left))assetYaw-=float(dt)*1.8f;if(input.isDown(GameButton::Right))assetYaw+=float(dt)*1.8f;if(input.wasPressed(GameButton::Up))changeAsset(-10);if(input.wasPressed(GameButton::Down))changeAsset(10);if(input.wasPressed(GameButton::Interact)){if(input.isDown(GameButton::Run))cycleAssetArchive();else changeAsset(1);}if(input.wasPressed(GameButton::Menu))mode=Mode::Field;return;}
        // Native script waits must be serviced before ordinary dialogue/menu input.
        if(scriptVm.active()&&scriptWaitSeconds>0.0){
            playSeconds+=dt;npcClock+=dt;updateRuntimeNpcs(dt);scriptWaitSeconds=std::max(0.0,scriptWaitSeconds-dt);
            if(scriptWaitSeconds<=0.0){
                if(resumeBgmAfterSoundWait){audio.stopSfx();audio.resumeBgm();resumeBgmAfterSoundWait=false;fanfarePausedBgm=false;}
                if(scriptWaitVar>=0x4000)scriptVm.writeVar(scriptWaitVar,0);
                scriptWaitVar=0;continueRomScript();
            }
            return;
        }
        if(mode==Mode::Dialogue){
            if(input.wasPressed(GameButton::Interact)||input.wasPressed(GameButton::Menu)){
                if(dialoguePage+1<dialogue.size()){dialoguePage++;return;}
                if(scriptVm.active()&&scriptWaitingInput){
                    scriptWaitingInput=false;if(scriptWaitInputVar>=0x4000)scriptVm.writeVar(scriptWaitInputVar,1);scriptWaitInputVar=0;
                    continueRomScript();return;
                }
                dialogue.clear();speaker.clear();dialoguePage=0;mode=Mode::Field;
                int nativeAction=nativeDialogueAction;nativeDialogueAction=0;
                bool resume=resumeScriptAfterDialogue;resumeScriptAfterDialogue=false;
                if(nativeAction){handleNativeDialogueAction(nativeAction);return;}
                if(resume&&scriptVm.active())continueRomScript();
            }return;
        }
        if(mode==Mode::ScriptChoice){
            if(scriptChoiceOptions.empty()){mode=Mode::Field;scriptVm.writeVar(scriptChoiceDest,0);continueRomScript();return;}
            if(input.wasPressed(GameButton::Up))scriptChoiceIndex=(scriptChoiceIndex+int(scriptChoiceOptions.size())-1)%int(scriptChoiceOptions.size());
            if(input.wasPressed(GameButton::Down))scriptChoiceIndex=(scriptChoiceIndex+1)%int(scriptChoiceOptions.size());
            if(input.wasPressed(GameButton::Menu)&&scriptChoiceCanCancel){scriptVm.writeVar(scriptChoiceDest,0xff);scriptChoiceOptions.clear();mode=dialogue.empty()?Mode::Field:Mode::Dialogue;continueRomScript();return;}
            if(input.wasPressed(GameButton::Interact)){auto value=scriptChoiceOptions[std::size_t(scriptChoiceIndex)].second;scriptVm.writeVar(scriptChoiceDest,value);scriptChoiceOptions.clear();mode=dialogue.empty()?Mode::Field:Mode::Dialogue;continueRomScript();return;}
            return;
        }
        if(mode==Mode::Naming){
            constexpr int cols=6,count=29;const std::size_t limit=nameTarget==NameTarget::Nickname?10u:7u;
            if(input.wasPressed(GameButton::Left)){int row=appNameCursor/cols,col=appNameCursor%cols;col=(col+cols-1)%cols;int n=row*cols+col;if(n>=count)n=count-1;appNameCursor=n;}
            if(input.wasPressed(GameButton::Right)){int row=appNameCursor/cols,col=(appNameCursor%cols+1)%cols;int n=row*cols+col;if(n>=count)n=row*cols;appNameCursor=std::min(n,count-1);}
            if(input.wasPressed(GameButton::Up)){int n=appNameCursor-cols;while(n<0)n+=count;appNameCursor=n;}
            if(input.wasPressed(GameButton::Down)){int n=appNameCursor+cols;if(n>=count)n%=count;appNameCursor=n;}
            if(input.wasPressed(GameButton::Menu)){if(!appName.empty())appName.pop_back();else finishAppNameEntry(false);return;}
            if(input.wasPressed(GameButton::Interact)){
                if(appNameCursor<26){if(appName.size()<limit)appName.push_back(char('A'+appNameCursor));}
                else if(appNameCursor==26){if(!appName.empty()&&appName.size()<limit)appName.push_back(' ');}
                else if(appNameCursor==27){if(!appName.empty())appName.pop_back();}
                else finishAppNameEntry(true);
            }
            return;
        }
        if(mode==Mode::BankAmount){
            auto clampAmount=[&](){bankAmount=std::min(bankAmount,bankAmountMax);};
            if(input.wasPressed(GameButton::Left)){bankAmount=bankAmount>=100?bankAmount-100:0;clampAmount();}
            if(input.wasPressed(GameButton::Right)){bankAmount=std::min(bankAmountMax,bankAmount+100);}
            if(input.wasPressed(GameButton::Up)){bankAmount=std::min(bankAmountMax,bankAmount+1000);}
            if(input.wasPressed(GameButton::Down)){bankAmount=bankAmount>=1000?bankAmount-1000:0;}
            if(input.isDown(GameButton::Run)&&input.wasPressed(GameButton::Up))bankAmount=bankAmountMax;
            if(input.wasPressed(GameButton::Menu)){scriptVm.writeVar(bankAmountDest,0);finishBlockingApp();return;}
            if(input.wasPressed(GameButton::Interact)){scriptVm.writeVar(bankAmountDest,std::uint16_t(std::min<std::uint32_t>(bankAmount,65535)));finishBlockingApp();return;}
            return;
        }
        if(mode==Mode::Pokegear||mode==Mode::TownMap){
            if(input.wasPressed(GameButton::Menu)||input.wasPressed(GameButton::Interact)){
                if(appResumeScript)finishBlockingApp();else mode=Mode::Menu;
            }
            return;
        }
        if(mode==Mode::Mart){
            if(!martEntries.empty()){
                if(input.wasPressed(GameButton::Up)){martIndex=(martIndex+int(martEntries.size())-1)%int(martEntries.size());martQuantity=1;}
                if(input.wasPressed(GameButton::Down)){martIndex=(martIndex+1)%int(martEntries.size());martQuantity=1;}
                auto const& e=martEntries[std::size_t(martIndex)];
                auto maxQuantity=[&](){
                    if(e.sold)return 1;
                    if(martSelling){
                        auto it=gameState.bag.find(e.id);if(it==gameState.bag.end())return 0;
                        unsigned n=std::min<unsigned>(99,it->second);
                        if(e.price&&gameState.money<999999u)n=std::min<unsigned>(n,(999999u-gameState.money)/e.price);
                        else if(gameState.money>=999999u)n=0;
                        return int(n);
                    }
                    if(martAthlete||martOpcode==277)return 1;
                    unsigned n=99;
                    if(e.price)n=std::min<unsigned>(n,gameState.money/e.price);
                    if(martOpcode==278){auto it=gameState.seals.find(e.id);unsigned have=it==gameState.seals.end()?0:it->second;n=std::min<unsigned>(n,have>=99?0:99-have);}
                    else {auto it=gameState.bag.find(e.id);unsigned have=it==gameState.bag.end()?0:it->second;unsigned cap=hg_item_pocket(e.id)==3?99:999;n=std::min<unsigned>(n,have>=cap?0:cap-have);}
                    return int(n);
                };
                if(!martAthlete&&martOpcode!=277&&(input.wasPressed(GameButton::Left)||input.wasPressed(GameButton::Right))){
                    int maxQ=maxQuantity();int step=input.isDown(GameButton::Run)?10:1;
                    if(input.wasPressed(GameButton::Left))martQuantity=std::max(1,martQuantity-step);
                    if(input.wasPressed(GameButton::Right))martQuantity=std::min(std::max(1,maxQ),martQuantity+step);
                }
            }
            if(input.wasPressed(GameButton::Menu)){if(appResumeScript)finishBlockingApp();else mode=Mode::Field;return;}
            if(input.wasPressed(GameButton::Interact)&&!martEntries.empty()){
                auto& e=martEntries[std::size_t(martIndex)];
                if(e.sold){showToast(martDataCards?"ALREADY PURCHASED":"ALREADY PURCHASED TODAY");return;}
                if(martSelling){
                    auto it=gameState.bag.find(e.id);if(it==gameState.bag.end()){showToast("NO ITEMS TO SELL");return;}
                    unsigned room=e.price?(999999u-gameState.money)/e.price:0;
                    unsigned qty=std::min<unsigned>({unsigned(std::max(1,martQuantity)),unsigned(it->second),99u,room});
                    if(!qty){showToast("MONEY IS FULL");return;}
                    if(!gameState.takeItem(e.id,std::uint16_t(qty)))return;
                    gameState.money=std::min<std::uint32_t>(999999u,gameState.money+e.price*qty);
                    showToast("SOLD "+hg_item_name(e.id)+" x"+std::to_string(qty));
                    if(!gameState.hasItem(e.id,1)){martEntries.erase(martEntries.begin()+martIndex);if(martIndex>=int(martEntries.size()))martIndex=std::max(0,int(martEntries.size())-1);}martQuantity=1;
                    return;
                }
                const unsigned qty=(martAthlete||martOpcode==277)?1u:unsigned(std::max(1,martQuantity));
                const std::uint32_t total=e.price*qty;
                const std::uint32_t funds=martAthlete?gameState.athletePoints:gameState.money;
                if(funds<total){showToast(martAthlete?"NOT ENOUGH ATHLETE POINTS":"NOT ENOUGH MONEY");return;}
                bool added=false;
                if(martOpcode==277){gameState.decorations[e.id]++;added=true;}
                else if(martOpcode==278){auto& q=gameState.seals[e.id];if(unsigned(q)+qty<=99u){q=std::uint16_t(q+qty);added=true;}}
                else if(martOpcode==771){added=gameState.addItem(e.id,1);if(added){gameState.pokeathlonPrizeFlags|=std::uint16_t(1u<<e.slot);e.sold=true;}}
                else if(martOpcode==772){if(e.id>=505&&e.id<=531){gameState.pokeathlonDataCardFlags|=1u<<(e.id-505);e.sold=true;added=true;}}
                else added=gameState.addItem(e.id,std::uint16_t(qty));
                if(!added){showToast("NO ROOM FOR THAT ITEM");return;}
                if(martAthlete)gameState.athletePoints=std::uint16_t(gameState.athletePoints-total);else gameState.money-=total;
                if(!martAthlete&&martOpcode!=277&&martOpcode!=278&&e.id==4&&qty>=10)gameState.addItem(12,1); // retail Premier Ball bonus
                std::string name=martOpcode==277?hg_decoration_name(e.id):martOpcode==278?hg_seal_name(e.id):hg_item_name(e.id);
                showToast("BOUGHT "+name+(qty>1?" x"+std::to_string(qty):std::string{}));martQuantity=1;
            }
            return;
        }
        if(mode==Mode::Summary){
            if(!gameState.party.empty()){
                if(input.wasPressed(GameButton::Up)||input.wasPressed(GameButton::Left))summaryIndex=(summaryIndex+int(gameState.party.size())-1)%int(gameState.party.size());
                if(input.wasPressed(GameButton::Down)||input.wasPressed(GameButton::Right))summaryIndex=(summaryIndex+1)%int(gameState.party.size());
            }
            if(input.wasPressed(GameButton::Menu)||input.wasPressed(GameButton::Interact)){
                if(summaryReturnToScript){summaryReturnToScript=false;mode=Mode::Field;if(scriptVm.active())continueRomScript();}
                else mode=Mode::Party;
            }
            return;
        }
        if(mode==Mode::Menu){if(input.wasPressed(GameButton::Up))menuIndex=(menuIndex+10)%11;if(input.wasPressed(GameButton::Down))menuIndex=(menuIndex+1)%11;if(input.wasPressed(GameButton::Interact))menuAction();if(input.wasPressed(GameButton::Menu))mode=Mode::Field;return;}
        if(mode==Mode::PCStorage){
            if(input.wasPressed(GameButton::Left)||input.wasPressed(GameButton::Right)){pcPartySide=!pcPartySide;pcIndex=0;}
            int n=pcPartySide?int(gameState.party.size()):int(gameState.pcStorage.size());
            if(n>0){if(input.wasPressed(GameButton::Up))pcIndex=(pcIndex+n-1)%n;if(input.wasPressed(GameButton::Down))pcIndex=(pcIndex+1)%n;}else pcIndex=0;
            if(input.wasPressed(GameButton::Menu)){if(appResumeScript)finishBlockingApp();else mode=Mode::Menu;return;}
            if(input.wasPressed(GameButton::Interact)&&n>0){
                if(pcPartySide){
                    if(gameState.party.size()<=1){showToast("YOU NEED A POKEMON WITH YOU");return;}
                    gameState.pcStorage.push_back(gameState.party[std::size_t(pcIndex)]);gameState.party.erase(gameState.party.begin()+pcIndex);showToast("POKEMON DEPOSITED");
                } else {
                    if(gameState.party.size()>=6){showToast("YOUR PARTY IS FULL");return;}
                    gameState.party.push_back(gameState.pcStorage[std::size_t(pcIndex)]);gameState.pcStorage.erase(gameState.pcStorage.begin()+pcIndex);showToast("POKEMON WITHDRAWN");
                }
                pcIndex=0;
            }
            return;
        }
        if(mode==Mode::Pokedex){if(input.wasPressed(GameButton::Menu)||input.wasPressed(GameButton::Interact))mode=Mode::Menu;return;}
        if(mode==Mode::Bag){if(input.wasPressed(GameButton::Menu))mode=Mode::Menu;return;}
        if(mode==Mode::Party){
            const int n=int(gameState.party.size());
            if(n>0){if(input.wasPressed(GameButton::Up))partyIndex=(partyIndex+n-1)%n;if(input.wasPressed(GameButton::Down))partyIndex=(partyIndex+1)%n;}
            if(input.wasPressed(GameButton::Menu)){
                if(partySelectForScript){scriptVm.writeVar(0x800c,0xff);partySelectForScript=false;mode=Mode::Field;if(scriptVm.active())continueRomScript();}
                else {mode=partyReturnBattle?Mode::Battle:Mode::Menu;partyReturnBattle=false;}
                return;
            }
            if(input.wasPressed(GameButton::Interact)&&n>0){
                if(partySelectForScript){scriptVm.writeVar(0x800c,std::uint16_t(partyIndex));partySelectForScript=false;mode=Mode::Field;if(scriptVm.active())continueRomScript();return;}
                if(partyReturnBattle){if(!gameState.party[std::size_t(partyIndex)].egg&&gameState.party[std::size_t(partyIndex)].hp){std::rotate(gameState.party.begin(),gameState.party.begin()+partyIndex,gameState.party.begin()+partyIndex+1);partyReturnBattle=false;mode=Mode::Battle;}else showToast("THAT POKEMON CAN'T BATTLE");return;}
                summaryIndex=partyIndex;summaryReturnToScript=false;mode=Mode::Summary;
            }
            return;
        }
        if(mode==Mode::StarterSelect){
            if(input.wasPressed(GameButton::Left))starterIndex=(starterIndex+2)%3;
            if(input.wasPressed(GameButton::Right))starterIndex=(starterIndex+1)%3;
            if(input.wasPressed(GameButton::Menu)){if(scriptVm.active()&&!nativeStarterSelection)return;nativeStarterSelection=false;mode=Mode::Field;return;}
            if(input.wasPressed(GameButton::Interact)){
                static const std::uint16_t choices[3]={152,155,158};auto species=choices[starterIndex];
                if(nativeStarterSelection){finishStarterSelection(species);return;}
                if(scriptVm.active()&&retailStarterSelectionPending){
                    // The application supplies party slot 0; the retail script then
                    // reads that species and calls SetStarterChoice itself. Award it
                    // exactly once here but leave story flags/scene vars to bytecode.
                    if(gameState.party.empty())gameState.giveMon(species,5);
                    else if(!gameState.gotStarter&&gameState.party[0].species!=species){
                        // A fresh New Game should have an empty party. Avoid duplicating
                        // a mon if a diagnostic save entered the chooser mid-script.
                        gameState.party[0].species=species;gameState.party[0].nickname=hg_species_name(species);gameState.own(species);
                    }
                    mode=Mode::Field;continueRomScript();return;
                }
                if(!gameState.starter){gameState.starter=species;gameState.giveMon(species,5);gameState.own(species);}
                gameState.starter=species;gameState.gotStarter=true;gameState.elmLabIntroDone=true;mode=Mode::Field;
            }
            return;
        }
        if(mode==Mode::Battle){
            if(!battle.active){mode=Mode::Field;return;}
            // Encounter transition is a distinct retail task and completes before
            // the battle application's send-out scene begins.
            if(battle.transitionClock<kBattleTransitionSeconds){battle.transitionClock+=dt;return;}
            battle.sceneClock+=dt; battle.actionClock+=dt;
            // Reproduce the retail StartEncounter command order for the initial
            // battlers. This fixes the old native shortcut where the player's
            // trainer NANR started immediately and sat behind the encounter text.
            if(battle.enemyIndex==0&&battle.introPhase<4){
                const bool confirm=input.wasPressed(GameButton::Interact)||input.mousePressed;
                if(battle.introPhase==0){
                    battle.introClock+=dt;
                    const double wait=battle.trainer?kTrainerEncounterWait:kWildEncounterWait;
                    if(battle.introClock>=wait){
                        battle.introPhase=1;battle.introClock=0.0;battle.awaiting=true;
                        battle.message=battle.trainer?("TRAINER "+std::to_string(battle.trainerId)+" WANTS TO BATTLE!"):("A WILD "+hg_species_name(battle.enemy.species)+" APPEARED!");
                    }
                    return;
                }
                if(battle.introPhase==1){
                    if(confirm){
                        battle.awaiting=false;battle.introClock=0.0;
                        if(battle.trainer){
                            battle.introPhase=2;
                            battle.message="TRAINER "+std::to_string(battle.trainerId)+" SENT OUT "+hg_species_name(battle.enemy.species)+"!";
                        }else{
                            battle.introPhase=3;
                            if(auto* lead=gameState.leadAlive())battle.message="GO! "+hg_species_name(lead->species)+"!";
                        }
                    }
                    return;
                }
                if(battle.introPhase==2){
                    battle.introClock+=dt;
                    if(battle.introClock>=kEnemySendOutWait){
                        battle.introPhase=3;battle.introClock=0.0;
                        if(auto* lead=gameState.leadAlive())battle.message="GO! "+hg_species_name(lead->species)+"!";
                    }
                    return;
                }
                if(battle.introPhase==3){
                    battle.introClock+=dt;
                    if(battle.introClock>=kPlayerSendOutWait){
                        battle.introPhase=4;battle.introClock=0.0;battle.message.clear();battle.awaiting=false;
                    }
                    return;
                }
            }
            if(battle.enemyIndex>0&&battle.sceneClock<0.38)return;
            auto mouseIn=[&](const HgBattleUiRect& r){
                return input.mouseInside&&r.contains(input.mouseX,input.mouseY);
            };
            if(battle.awaiting&&(input.wasPressed(GameButton::Interact)||input.mousePressed)){
                // Preserve the visible Gen IV turn cadence: the current move gets a
                // short animation window and message before the next battler acts.
                if((battle.turnStep==1||battle.turnStep==2)&&battle.actionClock<0.34)return;
                battle.awaiting=false;battle.message.clear();
                if(battle.turnStep!=0&&advanceBattleTurn())return;
                if(battle.enemy.hp==0){
                    if(battle.trainer&&battle.enemyIndex+1<battle.enemyParty.size()){
                        battle.enemyIndex++;battle.enemy=battle.enemyParty[battle.enemyIndex];hg_rehydrate_mon(battle.enemy);battle.enemyStages.fill(0);battle.sceneClock=0.0;battle.actionClock=0.0;gameState.see(battle.enemy.species);
                        battle.message="TRAINER "+std::to_string(battle.trainerId)+" SENT OUT "+hg_species_name(battle.enemy.species)+"!";battle.awaiting=true;return;
                    }
                    finishBattle(true);return;
                }
                if(!gameState.leadAlive()){finishBattle(false);return;}
            }
            if(battle.awaiting)return;
            if(battle.choosingMove){
                if(input.wasPressed(GameButton::Left)) battle.moveMenu=(battle.moveMenu+3)%4;
                if(input.wasPressed(GameButton::Right)) battle.moveMenu=(battle.moveMenu+1)%4;
                if(input.wasPressed(GameButton::Up)) battle.moveMenu=(battle.moveMenu+2)%4;
                if(input.wasPressed(GameButton::Down)) battle.moveMenu=(battle.moveMenu+2)%4;
                for(int i=0;i<4;i++)if(mouseIn(HG_BATTLE_MOVE_RECTS[std::size_t(i)])){battle.moveMenu=i;if(input.mousePressed&&gameState.leadAlive()&&gameState.leadAlive()->moves[i]){executeBattleTurn(i);return;}}
                if(input.mousePressed&&mouseIn(HG_BATTLE_CANCEL_RECT)){battle.choosingMove=false;return;}
                if(input.wasPressed(GameButton::Menu)){battle.choosingMove=false;return;}
                if(input.wasPressed(GameButton::Interact)&&gameState.leadAlive()&&gameState.leadAlive()->moves[battle.moveMenu]){executeBattleTurn(battle.moveMenu);return;}
                return;
            }
            if(input.wasPressed(GameButton::Left)||input.wasPressed(GameButton::Right)) battle.menu^=1;
            if(input.wasPressed(GameButton::Up)||input.wasPressed(GameButton::Down)) battle.menu^=2;
            for(int i=0;i<4;i++)if(mouseIn(HG_BATTLE_MAIN_RECTS[std::size_t(i)])){battle.menu=i;if(input.mousePressed){if(i==0)battleFight();else if(i==1)battleBag();else if(i==2){partyReturnBattle=true;mode=Mode::Party;}else battleRun();return;}}
            if(input.wasPressed(GameButton::Interact)){if(battle.menu==0)battleFight();else if(battle.menu==1)battleBag();else if(battle.menu==2){partyReturnBattle=true;mode=Mode::Party;}else battleRun();}
            if(input.wasPressed(GameButton::Menu)&&!battle.trainer)battleRun();
            return;
        }

        // FIELD: script-header triggers, NPC movement and ordinary player movement share one native loop.
        playSeconds+=dt;npcClock+=dt;updateRuntimeNpcs(dt);
        // Retail building transitions own player input from the moment the door is
        // approached until the destination has faded back in.
        if(fieldTransition.active()){updateFieldTransition(dt);return;}
        if(scriptVm.active()&&scriptWaitingInput){
            if(input.wasPressed(GameButton::Interact)||input.wasPressed(GameButton::Menu)){scriptWaitingInput=false;if(scriptWaitInputVar>=0x4000)scriptVm.writeVar(scriptWaitInputVar,1);scriptWaitInputVar=0;continueRomScript();}
            return;
        }
        if(romWorldReady&&scriptHeaderMap!=romWorld.mapId())reloadScriptHeader(false);
        if(!scriptVm.active()){
            if(startNextMapScript())return;
            if(scriptHeader.valid){auto trigger=hg_triggered_frame_script(scriptHeader,gameState);if(trigger==0)frameScriptLatch=0;else if(trigger!=frameScriptLatch){frameScriptLatch=trigger;activeFacingNpcId=-1;activeBackgroundId=-1;if(startRomScript(trigger))return;}}
        }
        if(moveProgress<1.0f){
            const bool scripted=nativeLabPhase!=0||scriptVm.active()||scriptWaitingMovement;
            float speed=playerLedgeJump?4.2f:(scripted?5.0f:(input.isDown(GameButton::Run)?8.0f:5.0f));moveProgress=std::min(1.0f,moveProgress+float(dt)*speed);float q=moveProgress;rx=float(fromX)+(toX-fromX)*q;ry=float(fromY)+(toY-fromY)*q;
            if(moveProgress>=1.0f){tx=toX;ty=toY;rx=float(tx);ry=float(ty);const bool finishedLedgeJump=playerLedgeJump;playerLedgeJump=false;gameState.onPlayerStep();int oldMap=romWorldReady?romWorld.mapId():mapIndex;if(romWorldReady){int ocx=romWorld.currentCellX(),ocy=romWorld.currentCellY(),omid=romWorld.mapId();romWorld.commitPosition(tx,ty);if(romWorld.currentCellX()!=ocx||romWorld.currentCellY()!=ocy||romWorld.mapId()!=omid)refreshRomVisualAssets();}doWarpIfNeeded();int newMap=romWorldReady?romWorld.mapId():mapIndex;if(!scripted&&oldMap==newMap&&!finishedLedgeJump)checkWildEncounter();}
            return;
        }
        if(scriptVm.active()&&!retailPlayerMoves.empty()){Dir d=retailPlayerMoves.front();retailPlayerMoves.pop_front();if(beginScriptedPlayerStep(d))return;}
        if(scriptVm.active()&&scriptWaitingMovement){
            if(allScriptMovementDone()){scriptWaitingMovement=false;continueRomScript();return;}
            return;
        }
        // A script that is still active must own field input until one of its native
        // waits resumes it. This mirrors the retail field task's script context.
        if(scriptVm.active())return;
        // Coordinate events are retail floor triggers, not button interactions.  This
        // is what starts Elm's automatic walk as soon as the player enters the lab row.
        if(processCoordinateTrigger())return;
        if(nativeLabPhase==1){
            if(!scriptedPlayerMoves.empty()){Dir d=scriptedPlayerMoves.front();scriptedPlayerMoves.pop_front();if(beginScriptedPlayerStep(d))return;}
            nativeLabPhase=2;
            const std::uint16_t firstMsg=gameState.female?1:0;
            beginNativeDialogue("PROF. ELM",firstMsg,{"Ah, "+gameState.playerName+"! I've been waiting for you!"},1);
            return;
        }
        if(input.wasPressed(GameButton::Menu)){mode=Mode::Menu;return;}if(input.wasPressed(GameButton::Interact)){interact();return;}
        if(input.isDown(GameButton::Up))tryMove(Dir::Up,input.isDown(GameButton::Run));else if(input.isDown(GameButton::Down))tryMove(Dir::Down,input.isDown(GameButton::Run));else if(input.isDown(GameButton::Left))tryMove(Dir::Left,input.isDown(GameButton::Run));else if(input.isDown(GameButton::Right))tryMove(Dir::Right,input.isDown(GameButton::Run));
    }

    const NsbmdMember* spriteForNpc(const Npc& n) const{
        if(n.name=="LYRA")return heroineSprite.valid?&heroineSprite:nullptr;
        if(n.name=="PROF. ELM")return doctorSprite.valid?&doctorSprite:nullptr;
        if(n.name=="AIDE")return aideSprite.valid?&aideSprite:nullptr;
        if(n.name=="MOM")return momSprite.valid?&momSprite:nullptr;
        if(n.name=="YOUNGSTER")return picnicSprite.valid?&picnicSprite:(boySprite.valid?&boySprite:nullptr);
        return boySprite.valid?&boySprite:nullptr;
    }

    void loadSpriteViewerMember(){
        if(spriteArchiveMembers==0){auto info=inspect_narc(assets/"a/0/8/1");spriteArchiveMembers=info.valid?info.members.size():0;}
        if(spriteArchiveMembers==0){spriteViewResource={};spriteViewResource.error="overworld sprite archive unavailable";return;}
        spriteViewMember%=spriteArchiveMembers;
        spriteViewResource=load_nitro_texture_from_narc(assets/"a/0/8/1",spriteViewMember);
        if(!spriteViewResource.valid)showToast("SPRITE RESOURCE PARSE FAILED");
    }
    void changeSpriteMember(int delta){
        if(spriteArchiveMembers==0)loadSpriteViewerMember();
        if(spriteArchiveMembers==0)return;
        long long n=static_cast<long long>(spriteViewMember)+delta,c=static_cast<long long>(spriteArchiveMembers);
        n=(n%c+c)%c;spriteViewMember=static_cast<std::size_t>(n);spriteViewFrame=0;loadSpriteViewerMember();
    }
    void drawTile(RenderFrame& f,Tile t,float sx,float sy,int wx,int wy,float light) const{
        Color base=tileBase(t,light); rect(f,sx,sy,TILE+1,TILE+1,base);
        if(t==Tile::Grass||t==Tile::Flower){ Color blade=mul({0.14f,0.43f,0.20f,1},light); if(((wx*17+wy*31)&3)==0){rect(f,sx+7,sy+10,3,8,blade);rect(f,sx+10,sy+13,3,5,blade);} if(t==Tile::Flower){Color petal=mul({0.95f,0.86f,0.28f,1},light);rect(f,sx+26,sy+11,4,4,petal);rect(f,sx+22,sy+15,4,4,petal);rect(f,sx+30,sy+15,4,4,petal);}}
        else if(t==Tile::Path){Color edge=mul({0.62f,0.56f,0.37f,1},light);rect(f,sx,sy,TILE,3,edge); if(((wx+wy)&2)==0)rect(f,sx+12,sy+23,4,3,edge);}
        else if(t==Tile::Water){Color wave=mul({0.42f,0.68f,0.92f,1},light);int off=(wx*7+wy*3)%12;rect(f,sx+4+off,sy+10,18,3,wave);rect(f,sx+15-off/2,sy+27,17,3,wave);}
        else if(t==Tile::Tree){Color trunk=mul({0.33f,0.20f,0.10f,1},light);Color leaf=mul({0.10f,0.44f,0.16f,1},light);rect(f,sx+16,sy+24,8,16,trunk);rect(f,sx+5,sy+4,30,25,leaf);rect(f,sx+10,sy,20,8,mul({0.18f,0.55f,0.22f,1},light));}
        else if(t==Tile::Wall){Color roof=mul({0.74f,0.22f,0.18f,1},light);rect(f,sx,sy,TILE,11,roof);rect(f,sx+4,sy+16,TILE-8,TILE-16,mul({0.72f,0.55f,0.38f,1},light));}
        else if(t==Tile::Door){rect(f,sx+9,sy+4,22,36,mul({0.29f,0.17f,0.09f,1},light));rect(f,sx+26,sy+22,3,3,mul({0.95f,0.78f,0.25f,1},light));}
        else if(t==Tile::Floor){Color line=mul({0.64f,0.55f,0.42f,1},light);rect(f,sx,sy+TILE-2,TILE,2,line);rect(f,sx+TILE-2,sy,2,TILE,line);}
        else if(t==Tile::Counter){rect(f,sx,sy,TILE,TILE,mul({0.38f,0.24f,0.15f,1},light));rect(f,sx,sy,TILE,7,mul({0.60f,0.42f,0.22f,1},light));}
        else if(t==Tile::Ledge){rect(f,sx,sy+26,TILE,14,mul({0.38f,0.31f,0.20f,1},light));rect(f,sx,sy+24,TILE,3,mul({0.82f,0.72f,0.50f,1},light));}
    }
    void drawCharacter(RenderFrame& f,float cx,float cy,Dir d,Color clothes,bool player) const{
        float x=cx-13,y=cy-31; Color outline={0.08f,0.07f,0.08f,1}; Color skin={0.95f,0.73f,0.56f,1};
        rect(f,x+5,y+24,7,8,outline);rect(f,x+16,y+24,7,8,outline);rect(f,x+4,y+13,20,14,outline);rect(f,x+7,y+14,14,12,clothes);rect(f,x+6,y+2,16,14,outline);rect(f,x+8,y+4,12,11,skin);
        if(player){rect(f,x+4,y,20,6,outline);rect(f,x+5,y+1,18,4,{0.84f,0.25f,0.20f,1});rect(f,x+18,y+3,8,3,{0.92f,0.76f,0.22f,1});}
        else rect(f,x+6,y+1,16,4,mul(clothes,0.78f));
        if(d==Dir::Down){rect(f,x+10,y+8,2,2,outline);rect(f,x+17,y+8,2,2,outline);} else if(d==Dir::Left)rect(f,x+8,y+8,2,2,outline); else if(d==Dir::Right)rect(f,x+19,y+8,2,2,outline);
    }
    RenderFrame renderRomField() const{
        RenderFrame f;
        const bool interior=romWorld.header()&&romWorld.header()->mapType==4;
        // Indoor HG maps use their area/material lighting rather than the outdoor time tint.
        // Until the complete retail light-table is ported, preserve authored material color
        // indoors and only apply time-of-day brightness to outdoor maps.
        const float light=interior?1.0f:dayLightFactor();
        f.clear=mul(interior?Color{0.10f,0.095f,0.085f,1}:Color{0.055f,0.085f,0.105f,1},light);
        SoftwareRaster scene(f.clear);

        // Retail field camera follows the player's continuous target, including terrain height.
        const float ph=romWorld.sampleHeightAt(int(std::round(rx)),int(std::round(ry)),0.0f)/4.0f;
        const std::uint8_t cameraType=romWorld.header()?romWorld.header()->cameraType:0;
        const HgFieldCamera camera=makeHgFieldCamera(cameraType,hg_field_tile_center(rx),ph,hg_field_tile_center(ry));

        // Authentic terrain BMD0 chunks selected by the active ROM map matrix.
        // All chunks and placed props share the same Z buffer, so geometry occludes
        // other geometry by per-pixel depth rather than per-object painter ordering.
        std::vector<const HgLoadedChunk*> chunks;for(auto const& c:romWorld.visibleChunks())chunks.push_back(&c);
        auto& preparedField=fieldPreparedScratch;
        preparedField.clear();
        if(preparedField.capacity()<24000)preparedField.reserve(24000);
        for(auto const* cp:chunks){
            auto const& c=*cp;
            if(c.land.model.valid&&!c.land.model.models.empty())
                preparePlacedNsbmdModel(preparedField,c.land.model.models.front(),&c.land.model.textures,
                    hg_field_chunk_center(c.matrixX),0.0f,hg_field_chunk_center(c.matrixY),camera,light,fieldAnimClock,currentDynamicTextureType);
        }

        // Authentic building placement records. Position fractions and model-specific
        // Nitro upScale are both significant: signs/doors commonly use upScale 4 while
        // full buildings use 16 (terrain baseline 64).
        auto placeComponent=[](std::int16_t whole,std::uint16_t frac){
            return (float(whole)+float(frac)/65536.0f)/4.0f;
        };
        for(auto const* cp:chunks){
            auto const& c=*cp;bool chunkInterior=false;if(auto* h=hg_map_header(c.mapId))chunkInterior=h->mapType==4;
            auto const& cache=chunkInterior?romRoomBuildings:romFieldBuildings;
            for(auto const& bp:c.land.buildings){
                auto it=cache.find(int(bp.modelId));if(it==cache.end()||!it->second.valid||it->second.models.empty())continue;
                float bx=hg_field_chunk_center(c.matrixX)+placeComponent(bp.x,bp.xFraction);
                float by=placeComponent(bp.y,bp.yFraction);
                float bz=hg_field_chunk_center(c.matrixY)+placeComponent(bp.z,bp.zFraction);
                std::uint16_t doorYRotation=bp.yRotation;
                if(buildingDoorMatches(c,bp,it->second)){
                    auto const& dm=it->second.models.front();
                    if(dm.name=="p_door"){
                        // Cherrygrove and the other retail Pokemon Center/Poke Mart
                        // exteriors both place bm_field member 15 (p_door).  It is a
                        // single automatic glass panel, not a hinged house door. Treat
                        // the authored p_door as an automatic sliding panel: moving the
                        // actual model vertically preserves its texture,
                        // transparency and building-relative placement.
                        const float ms=(std::isfinite(dm.normalizedScale)&&dm.normalizedScale>0.0001f)?dm.normalizedScale:1.0f;
                        const float panelHeight=std::max(0.0f,(dm.max.y-dm.min.y)*ms);
                        by += panelHeight*fieldTransition.doorAmount;
                    }else{
                        // Legacy hinged field-door models keep their authored pivot.
                        const int extra=int(std::lround(14500.0f*fieldTransition.doorAmount));
                        doorYRotation=std::uint16_t((std::uint32_t(bp.yRotation)+std::uint32_t(extra))&0xffffu);
                    }
                }
                preparePlacedNsbmdModel(preparedField,it->second.models.front(),&it->second.textures,bx,by,bz,camera,light,fieldAnimClock,currentDynamicTextureType,
                    bp.xRotation,doorYRotation,bp.zRotation);
            }
        }

        rasterPreparedField(scene,preparedField);

        auto odir=[](std::uint16_t o){switch(o&3){case 1:return Dir::Up;case 2:return Dir::Left;case 3:return Dir::Right;default:return Dir::Down;}};
        // Event-object visibility and movement now use persistent retail-style flags plus
        // the native movement-controller state, rather than drawing every object at spawn.
        for(auto const& o:romWorld.events().overworlds){
            if(!npcVisible(o))continue;
            auto it=runtimeNpcs.find(int(o.id));float ox=it==runtimeNpcs.end()?float(o.x):it->second.x,oy=it==runtimeNpcs.end()?float(o.y):it->second.y;
            float hh=romWorld.sampleHeightAt(int(std::round(ox)),int(std::round(oy)),0.0f)/4.0f;
            auto sp=projectWorldPoint({},hg_field_tile_center(ox),hh+0.04f,hg_field_tile_center(oy),camera);
            if(sp[0]<-160||sp[0]>LW+160||sp[1]<-80||sp[1]>LH+100)continue;
            float ss=spriteScaleAtDepth(camera,sp[2]);
            Dir renderDir=(activeFacingNpcId==int(o.id))?activeFacingNpcDir:(it==runtimeNpcs.end()?odir(o.orientation):it->second.facing);
            const bool npcMoving=it!=runtimeNpcs.end()&&it->second.move<1.0f;
            if(auto* sm=romSpriteForEvent(o))rasterOverworldSprite(scene,*sm,sp[0],sp[1]+ss*4.7f,sp[2],renderDir,npcMoving,npcClock,light,ss);
            if(debug)text(f,sp[0]-12,sp[1]-42,1,"#"+std::to_string(o.id),{1,0.85f,0.35f,1},false);
        }
        for(auto const& e:romWorld.events().spawnables){
            float hh=romWorld.sampleHeightAt(e.x,e.y,0.0f)/4.0f;auto sp=projectWorldPoint({},hg_field_tile_center(e.x),hh+0.03f,hg_field_tile_center(e.y),camera);
            if(debug&&sp[0]>=0&&sp[0]<=LW&&sp[1]>=60&&sp[1]<=LH-40){rect(f,sp[0]-4,sp[1]-4,8,8,{0.98f,0.75f,0.18f,1});text(f,sp[0]+7,sp[1]-6,1,"S"+std::to_string(e.scriptNumber),{1,0.86f,0.45f,1},false);}
        }

        // Retail-style walking follower. The script engine controls whether a follower
        // exists; for the Johto starters we use their authentic HG/SS overworld resources.
        if(gameState.followerEnabled && gameState.var(0x40f9)==0){
            std::uint16_t species=gameState.followerSpecies;
            if(gameState.followerPartySlot<gameState.party.size())species=gameState.party[gameState.followerPartySlot].species;
            int spriteIndex=species==152?0:(species==155?1:(species==158?2:-1));
            if(spriteIndex>=0&&starterSprites[std::size_t(spriteIndex)].valid){
                float fx=rx,fy=ry;Dir fd=facing;
                if(facing==Dir::Down){fy-=1;fd=Dir::Down;}else if(facing==Dir::Up){fy+=1;fd=Dir::Up;}else if(facing==Dir::Left){fx+=1;fd=Dir::Left;}else{fx-=1;fd=Dir::Right;}
                float fh=romWorld.sampleHeightAt(int(std::round(fx)),int(std::round(fy)),0.0f)/4.0f;
                auto fp=projectWorldPoint({},hg_field_tile_center(fx),fh+0.045f,hg_field_tile_center(fy),camera);float fs=spriteScaleAtDepth(camera,fp[2]);
                rasterFollowerSprite(scene,starterSprites[std::size_t(spriteIndex)],fp[0],fp[1]+fs*4.7f,fp[2],fd,moveProgress<1.0f||!retailPlayerMoves.empty(),npcClock,light,fs);
            }
        }

        // Player uses the same depth buffer as world geometry, fixing building/tree
        // faces incorrectly popping behind/in front of the sprite.
        const float jumpHeight=playerLedgeJump?0.16f*std::sin(std::clamp(moveProgress,0.0f,1.0f)*3.1415926535f):0.0f;
        auto pp=projectWorldPoint({},hg_field_tile_center(rx),ph+0.05f+jumpHeight,hg_field_tile_center(ry),camera);
        bool moving=moveProgress<1.0f;float playerScale=spriteScaleAtDepth(camera,pp[2]);
        auto const& activePlayerSprite=(gameState.female&&heroineSprite.valid)?heroineSprite:playerSprite;if(activePlayerSprite.valid)rasterOverworldSprite(scene,activePlayerSprite,pp[0],pp[1]+playerScale*4.7f,pp[2],facing,moving,npcClock,light,playerScale);
        else drawCharacter(f,pp[0],pp[1]+18,facing,mul({0.22f,0.38f,0.72f,1},light),true);

        f.rgba=std::move(scene.rgba);

        rect(f,18,16,520,54,{0.035f,0.05f,0.065f,1});rect(f,22,20,512,46,{0.82f,0.73f,0.38f,1});rect(f,26,24,504,38,{0.09f,0.12f,0.145f,1});text(f,40,35,2,romWorld.locationName(),{1,1,1,1});
        if(debug){rect(f,LW-430,14,412,250,{0.025f,0.035f,0.05f,1});text(f,LW-414,28,2,"ROM WORLD DEBUG",{0.95f,0.82f,0.30f,1});std::ostringstream a;a<<"MAP "<<romWorld.mapId()<<"  MATRIX "<<(romWorld.header()?romWorld.header()->matrixId:0)<<"  LAND "<<romWorld.currentLandMember();text(f,LW-414,56,1,a.str());std::ostringstream b;b<<"GLOBAL "<<tx<<","<<ty<<"  CELL "<<romWorld.currentCellX()<<","<<romWorld.currentCellY()<<"  LOCAL "<<romWorld.localX()<<","<<romWorld.localY();text(f,LW-414,78,1,b.str());if(auto* h=romWorld.header()){std::ostringstream c;c<<"AREA "<<h->areaDataBank<<"  EVENTS "<<h->eventsBank<<"  SCRIPT "<<h->scriptsBank<<"  MSG "<<h->msgBank;text(f,LW-414,100,1,c.str());std::ostringstream cam;cam<<"CAMERA TYPE "<<int(h->cameraType)<<"  RETAIL SCALE "<<int(camera.targetPixelsPerUnit)<<" PX/U";text(f,LW-414,122,1,cam.str(),{0.72f,0.88f,1.0f,1});}std::ostringstream d;d<<"OW "<<romWorld.events().overworlds.size()<<"  SPAWN "<<romWorld.events().spawnables.size()<<"  WARP "<<romWorld.events().warps.size()<<"  TRIGGER "<<romWorld.events().triggers.size();text(f,LW-414,144,1,d.str());std::ostringstream e;e<<"VISIBLE CHUNKS "<<romWorld.visibleChunks().size()<<"  SCRIPT BYTES "<<romWorld.scriptBank().size()<<"  MSG BYTES "<<romWorld.messageBank().size();text(f,LW-414,166,1,e.str());std::ostringstream st;st<<"SCRIPT "<<activeScriptNumber<<"  VARS "<<scriptVm.vars().size()<<"  FLAGS "<<scriptVm.flags().size()<<"  MSG "<<scriptMessagesShown;text(f,LW-414,188,1,st.str(),{0.72f,0.88f,1.0f,1});text(f,LW-414,208,1,"ROM BYTECODE -> NATIVE C++ / NO ARM VM",{0.55f,0.90f,0.62f,1});rect(f,LW-414,230,18,18,{0.82f,0.78f,0.64f,1});rect(f,LW-411,233,12,12,debugWalkThroughWalls?Color{0.95f,0.72f,0.20f,1}:Color{0.08f,0.10f,0.12f,1});text(f,LW-386,232,1,std::string("WALK THROUGH WALLS ")+(debugWalkThroughWalls?"[ON]":"[OFF]"),debugWalkThroughWalls?Color{1.0f,0.84f,0.35f,1}:Color{0.80f,0.84f,0.88f,1},false);}
        if(toastTime>0){float w=std::min(760.0f,40.0f+float(toast.size())*16.0f);rect(f,(LW-w)/2,LH-72,w,42,{0.03f,0.04f,0.055f,1});text(f,(LW-w)/2+18,LH-59,2,toast,{0.95f,0.88f,0.48f,1});}
        auto prompt=interactionLabel();if(!prompt.empty()&&mode==Mode::Field&&!fieldTransition.active()){float w=std::min(600.0f,34.0f+float(prompt.size())*13.0f);rect(f,(LW-w)/2,LH-106,w,34,{0.025f,0.035f,0.05f,1});text(f,(LW-w)/2+14,LH-97,1,prompt,{1.0f,0.90f,0.48f,1},false);}
        text(f,20,LH-28,1,"REAL ROM WORLD  |  MOVE WASD/ARROWS  RUN SHIFT  INTERACT E/Z/ENTER  MENU X/ESC  SAVE F5  LOAD F9  DEBUG F3",{0.93f,0.95f,0.96f,1},false);
        if(fieldTransition.fade>0.001f){
            // Fade the already-composited scene *and* every native overlay.  RenderFrame
            // rectangles and text are presented in separate passes, so a translucent
            // black rectangle alone would leave HUD/text visible over the fade.
            const float keep=1.0f-std::clamp(fieldTransition.fade,0.0f,1.0f);
            for(std::size_t i=0;i+3<f.rgba.size();i+=4){
                f.rgba[i]=std::uint8_t(float(f.rgba[i])*keep+0.5f);
                f.rgba[i+1]=std::uint8_t(float(f.rgba[i+1])*keep+0.5f);
                f.rgba[i+2]=std::uint8_t(float(f.rgba[i+2])*keep+0.5f);
            }
            f.clear.r*=keep;f.clear.g*=keep;f.clear.b*=keep;
            for(auto& r:f.rects){r.color.r*=keep;r.color.g*=keep;r.color.b*=keep;}
            for(auto& t:f.texts){t.color.r*=keep;t.color.g*=keep;t.color.b*=keep;}
        }
        return f;
    }

    RenderFrame renderField() const{
        if(romWorldReady)return renderRomField();
        RenderFrame f; float light=dayLightFactor(); f.clear=mul({0.05f,0.09f,0.12f,1},light);
        float camX=rx*TILE-LW*0.5f; float camY=ry*TILE-LH*0.5f; int minX=int(std::floor(camX/TILE))-1,maxX=int(std::ceil((camX+LW)/TILE))+1,minY=int(std::floor(camY/TILE))-1,maxY=int(std::ceil((camY+LH)/TILE))+1;
        for(int y=minY;y<=maxY;y++)for(int x=minX;x<=maxX;x++){float sx=x*TILE-camX,sy=y*TILE-camY;drawTile(f,map().get(x,y),sx,sy,x,y,light);}
        if(mapIndex==0&&worldProps.size()>=3){
            // Real HeartGold BMD0 geometry + TEX0 textures embedded in the playable reconstructed field.
            drawNsbmdModel(f,worldProps[0].models.front(),&worldProps[0].textures,4*TILE-camX,2*TILE-camY,7*TILE,7*TILE,0.08f,0.20f);
            drawNsbmdModel(f,worldProps[1].models.front(),&worldProps[1].textures,20*TILE-camX,3*TILE-camY,9*TILE,7*TILE,-0.10f,0.20f);
            drawNsbmdModel(f,worldProps[2].models.front(),&worldProps[2].textures,4*TILE-camX,14*TILE-camY,9*TILE,8*TILE,0.10f,0.20f);
        }
        for(auto const& s:map().signs){float sx=s.x*TILE-camX+11,sy=s.y*TILE-camY+10;rect(f,sx,sy,18,22,mul({0.48f,0.30f,0.13f,1},light));rect(f,sx+3,sy+3,12,8,mul({0.86f,0.72f,0.42f,1},light));}
        for(auto const& n:map().npcs){float cx=n.x*TILE+TILE*.5f-camX,cy=n.y*TILE+TILE*.94f-camY;if(auto* sp=spriteForNpc(n))drawOverworldSprite(f,*sp,cx,cy,n.facing,false,npcClock,light);else drawCharacter(f,cx,cy,n.facing,mul(n.color,light),false);}
        {float cx=rx*TILE+TILE*.5f-camX,cy=ry*TILE+TILE*.94f-camY-(playerLedgeJump?24.0f*std::sin(std::clamp(moveProgress,0.0f,1.0f)*3.1415926535f):0.0f);bool moving=moveProgress<1.0f;auto const& activePlayerSprite=(gameState.female&&heroineSprite.valid)?heroineSprite:playerSprite;if(activePlayerSprite.valid)drawOverworldSprite(f,activePlayerSprite,cx,cy,facing,moving,npcClock,light);else drawCharacter(f,cx,cy,facing,mul({0.22f,0.38f,0.72f,1},light),true);}
        rect(f,18,16,420,48,{0.04f,0.06f,0.08f,1});rect(f,22,20,412,40,{0.82f,0.73f,0.38f,1});rect(f,26,24,404,32,{0.10f,0.13f,0.15f,1});text(f,40,34,2,map().name,{1,1,1,1});
        if(debug){rect(f,LW-430,14,412,250,{0.03f,0.04f,0.06f,1});text(f,LW-414,28,2,"NATIVE DEBUG HUD",{0.95f,0.82f,0.30f,1});std::ostringstream a;a<<"MAP "<<mapIndex<<"  X "<<tx<<"  Y "<<ty; text(f,LW-340,58,2,a.str());text(f,LW-340,84,2,"FACING "+dirName(facing));text(f,LW-414,110,2,"VULKAN / REAL BMD0 / NO ARM VM",{0.55f,0.90f,0.62f,1});rect(f,LW-414,230,18,18,{0.82f,0.78f,0.64f,1});rect(f,LW-411,233,12,12,debugWalkThroughWalls?Color{0.95f,0.72f,0.20f,1}:Color{0.08f,0.10f,0.12f,1});text(f,LW-386,232,1,std::string("WALK THROUGH WALLS ")+(debugWalkThroughWalls?"[ON]":"[OFF]"),debugWalkThroughWalls?Color{1.0f,0.84f,0.35f,1}:Color{0.80f,0.84f,0.88f,1},false);}
        if(toastTime>0){float w=std::min(700.0f,40.0f+float(toast.size())*16.0f);rect(f,(LW-w)/2,LH-72,w,42,{0.03f,0.04f,0.055f,1});text(f,(LW-w)/2+18,LH-59,2,toast,{0.95f,0.88f,0.48f,1});}
        auto prompt=interactionLabel();if(!prompt.empty()&&mode==Mode::Field){float w=std::min(520.0f,34.0f+float(prompt.size())*13.0f);rect(f,(LW-w)/2,LH-102,w,34,{0.025f,0.035f,0.05f,0.94f});text(f,(LW-w)/2+14,LH-93,1,prompt,{1.0f,0.90f,0.48f,1},false);}
        text(f,22,LH-30,2,"WASD/ARROWS MOVE  SHIFT RUN  E/Z/ENTER TALK  X/ESC MENU  F5 SAVE  F9 LOAD  F2 REAL MODELS  F4 REAL LAND  F3 DEBUG  Q QUIT",{0.93f,0.95f,0.96f,1},false);
        return f;
    }
    RenderFrame renderSpriteViewer() const{
        RenderFrame f;f.clear={0.025f,0.032f,0.045f,1};rect(f,0,0,LW,LH,{0.035f,0.055f,0.072f,1});
        rect(f,36,30,LW-72,LH-60,{0.07f,0.08f,0.10f,1});rect(f,48,42,LW-96,LH-84,{0.015f,0.020f,0.028f,1});
        text(f,72,64,3,"ORIGINAL HEARTGOLD OVERWORLD SPRITES",{0.95f,0.78f,0.24f,1});
        std::ostringstream sh;sh<<"MMODEL MEMBER "<<spriteViewMember<<" / "<<(spriteArchiveMembers?spriteArchiveMembers-1:0);text(f,74,110,2,sh.str(),{0.68f,0.86f,1,1});
        auto const* sp=&spriteViewResource;
        if(sp->valid&&!sp->textures.empty()){
            int fi=std::clamp(spriteViewFrame,0,int(sp->textures.size())-1);auto const& t=sp->textures[size_t(fi)];
            float scale=7.0f;float x=230,y=180;drawTexturePixels(f,t,x,y,scale,1.0f,false);
            rect(f,720,160,430,350,{0.055f,0.070f,0.09f,1});text(f,750,188,2,"ROM RESOURCE",{0.95f,0.78f,0.24f,1});
            text(f,750,226,2,t.name,{1,1,1,1});std::ostringstream a;a<<"FRAME "<<(fi+1)<<" / "<<sp->textures.size();text(f,750,266,2,a.str());
            std::ostringstream b;b<<t.width<<" x "<<t.height<<"   DS FMT "<<int(t.format);text(f,750,304,2,b.str());
            text(f,750,352,1,"DECODED FROM a/0/8/1",{0.55f,0.90f,0.62f,1});text(f,750,374,1,"ORIGINAL PALETTE + ALPHA",{0.55f,0.90f,0.62f,1});
            text(f,750,422,1,"LEFT/RIGHT = ARCHIVE MEMBER",{0.86f,0.90f,0.94f,1});text(f,750,444,1,"UP/DOWN/ENTER = FRAME",{0.86f,0.90f,0.94f,1});text(f,750,466,1,"SHIFT+UP/DOWN = +/-10 MEMBERS",{0.86f,0.90f,0.94f,1});
        }else text(f,250,330,3,"SPRITE RESOURCE UNAVAILABLE",{0.95f,0.40f,0.35f,1});
        text(f,74,660,1,"ORIGINAL ROM SPRITE DATA - ESC/X RETURNS TO MENU",{0.88f,0.91f,0.94f,1},false);return f;
    }

    RenderFrame renderAssetViewer() const{
        RenderFrame f;f.clear={0.018f,0.022f,0.032f,1};rect(f,0,0,LW,LH,{0.025f,0.032f,0.046f,1});
        rect(f,28,24,LW-56,LH-48,{0.07f,0.08f,0.10f,1});rect(f,42,38,LW-84,LH-76,{0.018f,0.023f,0.031f,1});
        text(f,70,58,3,"HEARTGOLD REAL ROM MODEL GALLERY",{0.95f,0.78f,0.24f,1});
        std::ostringstream head;head<<assetSourceName()<<" MEMBER "<<assetIndex<<" / "<<(assetMemberCount?assetMemberCount-1:0);text(f,74,104,2,head.str(),{0.68f,0.86f,1,1});
        rect(f,72,142,850,500,{0.08f,0.10f,0.13f,1});rect(f,78,148,838,488,{0.12f,0.16f,0.18f,1});
        if(assetMember.valid&&!assetMember.models.empty()){
            auto const& m=assetMember.models.front();drawNsbmdModel(f,m,&assetMember.textures,82,152,830,480,assetYaw,assetPitch);text(f,954,164,2,"MODEL",{0.95f,0.78f,0.24f,1});text(f,954,192,2,m.name,{1,1,1,1});
            std::ostringstream a;a<<"TRIANGLES "<<m.triangles.size();text(f,954,236,2,a.str());std::ostringstream b;b<<"PIECES "<<m.sourcePieces; text(f,954,264,2,b.str());std::ostringstream c;c<<"TEX "<<assetMember.textureCount<<"  PAL "<<assetMember.paletteCount;text(f,954,292,2,c.str());
            std::ostringstream d;d<<std::fixed<<std::setprecision(2)<<"SIZE "<<(m.max.x-m.min.x)<<" x "<<(m.max.y-m.min.y)<<" x "<<(m.max.z-m.min.z);text(f,954,320,1,d.str(),{0.82f,0.88f,0.92f,1});
            if(assetSource==AssetSource::Land && assetLandChunk.valid){
                std::ostringstream lo;lo<<"BMD0 @ "<<assetLandChunk.modelOffset<<"  "<<assetLandChunk.modelSize<<" B";text(f,954,350,1,lo.str(),{0.86f,0.90f,0.94f,1});
                std::ostringstream co;co<<"BDHC @ "<<assetLandChunk.collisionOffset<<"  "<<assetLandChunk.collisionSize<<" B";text(f,954,372,1,co.str(),{0.86f,0.90f,0.94f,1});
                text(f,954,400,1,"TERRAIN + COLLISION CONTAINER",{0.95f,0.78f,0.24f,1});
                if(assetLandChunk.bdhc.valid){
                    std::ostringstream bp;bp<<"PLATES "<<assetLandChunk.bdhc.plates.size();text(f,954,422,1,bp.str(),{0.78f,0.86f,0.92f,1});
                    drawBdhcPlates(f,assetLandChunk.bdhc,954,446,238,132);
                }
            }else if(!assetMember.textures.empty()){
                auto const& txr=assetMember.textures.front();
                text(f,954,350,1,"EMBEDDED TEXTURE",{0.95f,0.78f,0.24f,1});
                text(f,954,368,1,txr.name,{0.86f,0.90f,0.94f,1});
                drawNsbmdTexture(f,txr,954,390,238,180);
                std::ostringstream ti;ti<<txr.width<<"x"<<txr.height<<" FMT "<<int(txr.format);text(f,954,580,1,ti.str(),{0.75f,0.82f,0.88f,1});
            }
            text(f,954,608,1,assetSource==AssetSource::Land?"REAL LAND BMD0 + BDHC":"REAL BMD0 + TEX0 DATA",{0.55f,0.90f,0.62f,1});
            text(f,954,626,1,"NO ARM EXECUTION",{0.55f,0.90f,0.62f,1});
        }else{text(f,180,370,3,"MODEL COULD NOT BE DECODED",{0.95f,0.40f,0.35f,1});text(f,180,410,1,assetMember.error,{0.90f,0.75f,0.70f,1});}
        text(f,72,670,1,"LEFT/RIGHT ROTATE   ENTER NEXT   UP/DOWN +/-10   SHIFT+ENTER FIELD/ROOM/LAND   F2 OR ESC EXIT",{0.88f,0.91f,0.94f,1},false);return f;
    }
    RenderFrame renderTerrainSandbox() const{
        RenderFrame f;f.clear={0.018f,0.024f,0.032f,1};rect(f,0,0,LW,LH,{0.025f,0.032f,0.042f,1});
        const LandChunk* land=nullptr;std::uint16_t landId=60;std::string loc="LEGACY LAND INSPECTOR";
        if(romWorldReady){
            landId=romWorld.currentLandMember();loc=romWorld.locationName();
            if(auto* c=romWorld.chunkForTile(romWorld.x(),romWorld.y())) land=&c->land;
        }else if(land60Chunk.valid) land=&land60Chunk;
        text(f,42,28,3,"ACTIVE ROM WORLD CHUNK",{0.95f,0.78f,0.24f,1});
        std::ostringstream head;head<<loc<<" / LAND "<<landId<<" / NATIVE BMD0 + BDHC";text(f,44,68,1,head.str(),{0.65f,0.86f,1.0f,1},false);
        rect(f,38,96,900,544,{0.07f,0.08f,0.10f,1});rect(f,46,104,884,528,{0.12f,0.15f,0.17f,1});
        if(land&&land->valid&&!land->model.models.empty()){
            auto const& m=land->model.models.front();constexpr float vx=50,vy=108,vw=876,vh=520;float yaw=0.62f,pitch=0.48f;
            drawNsbmdModel(f,m,&land->model.textures,vx,vy,vw,vh,yaw,pitch);
            Vec3f pp{terrainX/4.0f,terrainHeight/4.0f+0.10f,terrainY/4.0f};auto sp=projectNsbmdPoint(m,pp,vx,vy,vw,vh,yaw,pitch);
            rect(f,sp[0]-8,sp[1]-18,16,18,{0.95f,0.82f,0.25f,1});rect(f,sp[0]-5,sp[1]-26,10,10,{0.95f,0.70f,0.55f,1});
            rect(f,966,112,280,310,{0.055f,0.07f,0.09f,1});text(f,984,130,2,"ROM CHUNK DATA",{0.95f,0.78f,0.24f,1});text(f,984,164,2,m.name,{1,1,1,1});
            std::ostringstream tr;tr<<"TRIANGLES "<<m.triangles.size();text(f,984,198,1,tr.str());std::ostringstream pl;pl<<"BDHC PLATES "<<land->bdhc.plates.size();text(f,984,220,1,pl.str());
            std::ostringstream pos;pos<<"LOCAL X "<<terrainX+16<<" Y "<<terrainY+16;text(f,984,260,1,pos.str(),{0.72f,0.88f,1,1});std::ostringstream ht;ht<<std::fixed<<std::setprecision(2)<<"HEIGHT "<<terrainHeight;text(f,984,282,1,ht.str(),{0.72f,0.88f,1,1});
            text(f,984,320,1,"MOVEMENT REQUIRES A",{0.62f,0.90f,0.66f,1});text(f,984,338,1,"REAL BDHC SURFACE",{0.62f,0.90f,0.66f,1});
            drawBdhcPlates(f,land->bdhc,966,438,280,174);
        }else text(f,230,340,3,"ACTIVE LAND DATA UNAVAILABLE",{0.95f,0.40f,0.35f,1});
        text(f,42,670,1,"ARROWS/WASD WALK ON ACTIVE BDHC   SHIFT = 2-CELL STEP   F4 OR ESC EXIT",{0.88f,0.91f,0.94f,1},false);
        return f;
    }

    RenderFrame render() const{
        if(mode==Mode::SpriteViewer)return renderSpriteViewer();
        if(mode==Mode::AssetViewer)return renderAssetViewer();
        if(mode==Mode::TerrainSandbox)return renderTerrainSandbox();
        if(mode==Mode::Intro){
            RenderFrame f;f.clear={0.0f,0.0f,0.0f,1};ensurePixels(f,f.clear);
            if(!introMovie.empty()){
                std::size_t si=std::min(introScene,introMovie.size()-1);auto const& sc=introMovie[si];double before=0;for(std::size_t i=0;i<si;i++)before+=introMovie[i].duration;double local=std::clamp(introClock-before,0.0,sc.duration);double q=sc.duration>0?local/sc.duration:0;
                if(!sc.frames.empty()){
                    std::size_t fi=0;
                    if(si==0&&sc.frames.size()>=3){fi=q<0.18?0:(q<0.48?1:2);}
                    else fi=std::min(sc.frames.size()-1,std::size_t(q*double(sc.frames.size())));
                    auto const& img=sc.frames[fi];
                    int cw=std::min(256,img.width),ch=std::min(192,img.height);auto [baseX,baseY]=bestNitroCrop(img,cw,ch);
                    int maxX=std::max(0,img.width-cw),maxY=std::max(0,img.height-ch);double wave=0.5-0.5*std::cos(q*3.1415926535);
                    // Retail pans are modest within a scene. Keep motion near useful content instead
                    // of sweeping across the entire oversized backing tilemap.
                    int panX=std::min(si==4?6:24,maxX),panY=std::min(si==4?4:16,maxY);
                    int sx=std::clamp(baseX+int((wave-0.5)*2.0*panX),0,maxX);
                    int sy=std::clamp(baseY+int(((si==0?1.0-wave:wave)-0.5)*2.0*panY),0,maxY);
                    blitImageCrop(f,img,sx,sy,cw,ch,160,0,960,720,false);
                }
                // Restore moving foreground elements that the old flattened intro
                // renderer discarded. These use authentic game resources and the
                // same high-level scene order as the retail movie.
                if(si==2&&q<0.46){
                    int mi=std::min(2,int(q/0.1534));auto const& mm=introMapModels[std::size_t(mi)];
                    if(mm.valid&&!mm.models.empty())drawNsbmdModel(f,mm.models.front(),&mm.textures,250,95,780,520,0.08f+0.10f*std::sin(float(introClock)*0.6f),0.34f);
                }
                // Nitro OBJ layers from the same opening archive. These replace
                // the v0.21 substitute overworld sprites wherever the retail movie
                // actually supplies NCER-composed art.
                if(si==0){
                    // opening.narc carries real NANR timing for both legendary OBJ
                    // banks. Use its ping-pong sequence instead of cycling cell
                    // indices at an arbitrary PC frame rate.
                    const bool hooh=q<0.76;auto const& cells=hooh?introHoohCells:introLugiaCells;auto const& anim=hooh?introHoohAnim:introLugiaAnim;
                    if(!cells.empty()){const double t=hooh?local:std::max(0.0,local-sc.duration*0.76);std::size_t ci=anim.valid?sample_nitro_nanr_cell(anim,2,t):std::size_t(t*6.0)%cells.size();ci=std::min(ci,cells.size()-1);blitImage(f,cells[ci],160,0,960,720,true);}
                }else if(si==2&&!introRivalCells.empty()&&q>0.46){
                    std::size_t ci=introRivalAnim.valid?sample_nitro_nanr_cell(introRivalAnim,0,local):0;ci=std::min(ci,introRivalCells.size()-1);blitImage(f,introRivalCells[ci],160,0,960,720,true);
                }else if(si==3){
                    // Scene 4 uses the authored hero and grass OBJ planes together.
                    if(!introHeroCells.empty()){std::size_t seq=std::min<std::size_t>(introHeroAnim.sequences.empty()?0:introHeroAnim.sequences.size()-1,std::size_t(q*7.0));std::size_t ci=introHeroAnim.valid?sample_nitro_nanr_cell(introHeroAnim,seq,local):std::min(introHeroCells.size()-1,std::size_t(q*introHeroCells.size()));ci=std::min(ci,introHeroCells.size()-1);blitImage(f,introHeroCells[ci],160,0,960,720,true);}
                    if(!introGrassCells.empty()){std::size_t seq=std::min<std::size_t>(3,std::size_t(q*4.0));std::size_t ci=introGrassAnim.valid?sample_nitro_nanr_cell(introGrassAnim,seq,local):seq%introGrassCells.size();ci=std::min(ci,introGrassCells.size()-1);blitImage(f,introGrassCells[ci],160,0,960,720,true);}
                }else if(si==4&&!introTouchCells.empty()){
                    // The final touch/logo accent is a six-cell NANR loop in sequence 2.
                    std::size_t ci=introTouchAnim.valid?sample_nitro_nanr_cell(introTouchAnim,2,local):std::size_t(local*6.0)%introTouchCells.size();ci=std::min(ci,introTouchCells.size()-1);blitImage(f,introTouchCells[ci],160,0,960,720,true);
                }
                float edge=float(std::min({q*7.0,(1.0-q)*7.0,1.0}));if(edge<1.0f)rect(f,0,0,1280,720,{0,0,0,1.0f-edge});
                if(debug)text(f,20,690,1,"OPENING SCENE "+std::to_string(si+1)+" / 5   ENTER = SKIP",{0.82f,0.84f,0.88f,1},false);
            } else{text(f,415,310,4,"POKEMON HEARTGOLD",{0.95f,0.78f,0.22f,1});}
            return f;
        }
        if(mode==Mode::Title){
            RenderFrame f;f.clear={0.12f,0.055f,0.01f,1};ensurePixels(f,f.clear);
            if(titleSun.valid)blitImage(f,titleSun,160,0,960,720,false);
            else{rect(f,0,0,LW,LH,{0.35f,0.18f,0.03f,1});text(f,360,220,5,"POKEMON HEARTGOLD",{0.98f,0.82f,0.22f,1});}
            // Real HeartGold Ho-Oh BMD0. v0.9 drives the recovered model with a native
            // title motion pass; the BCA/BTA/BMA/BTP files are retained/validated for
            // the next exact node/material-animation layer rather than executing Nitro code.
            if(titleHooh.valid&&!titleHooh.models.empty()){
                // The real Ho-Oh model now executes its rest-pose bone/matrix-stack/skinning data.
                // Whole-model flight motion is native until the remaining NSBCA curve evaluator lands.
                float yaw=-0.30f+0.10f*std::sin(float(titleClock)*0.75f);
                float pitch=0.16f+0.03f*std::sin(float(titleClock)*1.25f);
                drawNsbmdModel(f,titleHooh.models.front(),&titleHooh.textures,410,190+8*std::sin(float(titleClock)*0.9f),460,310,yaw,pitch);
                if(titleSparkle.valid&&!titleSparkle.models.empty()){
                    for(int i=0;i<3;i++){float q=float(titleClock)*1.7f+float(i)*2.094f;float sx=575+std::cos(q)*180,sy=350+std::sin(q*0.8f)*105;drawNsbmdModel(f,titleSparkle.models.front(),&titleSparkle.textures,sx,sy,56,56,q,0.1f);}
                }
            }
            if(titleLogo.valid)blitImage(f,titleLogo,315,-105,650,650,true);
            if(titleFooter.valid)blitImage(f,titleFooter,160,0,960,720,true);
            auto prompt=titleMessages.valid()?titleMessages.decode(0):HgDecodedMessage{};
            std::string promptText=prompt.valid&&!prompt.text.empty()?prompt.text:"TOUCH TO START";
            // Retail title blinks the start prompt; preserve that cadence feel on PC.
            if((int(titleClock*60.0)%45)<31){
                float w=std::min(520.0f,80.0f+float(promptText.size())*22.0f);
                rect(f,(LW-w)/2,594,w,48,{0.03f,0.025f,0.02f,0.84f});
                text(f,(LW-w)/2+28,608,2,promptText,{1.0f,0.96f,0.74f,1});
            }
            text(f,22,682,1,"ENTER / A = START   ORIGINAL HEARTGOLD TITLE ASSETS   v0.24 HOUSE EXIT + OAK SPRITE FIX / NATIVE C++ / VULKAN",{0.98f,0.94f,0.82f,1},false);
            return f;
        }
        if(mode==Mode::MainMenu){
            RenderFrame f;f.clear={0.84f,0.88f,0.92f,1};rect(f,0,0,LW,LH,{0.76f,0.82f,0.88f,1});
            auto ss=readSaveSummary();
            rect(f,145,52,990,610,{0.95f,0.95f,0.91f,1});rect(f,160,67,960,580,{0.12f,0.16f,0.19f,1});
            text(f,195,92,3,"POKEMON HEARTGOLD",{0.98f,0.80f,0.22f,1});
            const char* fallback[3]={"CONTINUE","NEW GAME","MYSTERY GIFT"};
            for(int i=0;i<3;i++){std::string label=fallback[i];if(mainMenuMessages.valid()){auto dm=mainMenuMessages.decode(i==0?0:(i==1?1:2));if(dm.valid&&!dm.text.empty())label=dm.text;}float y=185+i*126;bool disabled=(i==0&&!ss.valid)||i==2;Color box=disabled?Color{0.24f,0.27f,0.29f,1}:Color{0.22f,0.36f,0.42f,1};if(i==mainMenuIndex)box=disabled?Color{0.32f,0.34f,0.35f,1}:Color{0.55f,0.38f,0.08f,1};rect(f,210,y-20,860,92,box);text(f,250,y,3,std::string(i==mainMenuIndex?"> ":"  ")+label,disabled?Color{0.52f,0.55f,0.57f,1}:Color{1,1,1,1});if(i==2)text(f,746,y+10,1,"COMING LATER - CUSTOM SERVER",{0.50f,0.52f,0.54f,1});}
            if(ss.valid){int h=int(ss.play/3600),m=int(ss.play/60)%60;text(f,575,197,1,"PLAYER  "+ss.name,{0.82f,0.92f,1,1});text(f,575,220,1,"TIME    "+std::to_string(h)+":"+(m<10?"0":"")+std::to_string(m));text(f,780,220,1,"BADGES  "+std::to_string(ss.badges));text(f,575,243,1,"POKEDEX "+std::to_string(ss.dexOwned));}
            text(f,230,607,1,"ARROWS = SELECT   ENTER / A = CONFIRM   ESC / X = TITLE",{0.84f,0.88f,0.92f,1},false);return f;
        }
        if(mode==Mode::NewGameIntro){
            RenderFrame f;f.clear={0.90f,0.95f,0.91f,1};ensurePixels(f,f.clear);
            auto drawAuthored=[](RenderFrame& frame,const std::vector<NitroRgbaImage>& states,std::size_t idx){
                if(states.empty())return false;
                idx=std::min(idx,states.size()-1);
                auto const& im=states[idx];
                if(!im.valid)return false;
                blitImage(frame,im,0,0,int(LW),int(LH),false);
                return true;
            };
            if(newGameStage==10){
                bool authored=false;
                if(!newGameTvFrames.empty()){
                    // The seven retail TV maps are a one-shot sequence rather than
                    // an endlessly blinking PC placeholder.
                    double per=5.6/double(newGameTvFrames.size());
                    std::size_t fi=std::min(newGameTvFrames.size()-1,std::size_t(std::max(0.0,newGameClock)/per));
                    authored=drawAuthored(f,newGameTvFrames,fi);
                }
                if(!authored){
                    rect(f,0,0,LW,LH,{0.08f,0.02f,0.02f,1});
                    if(redGyarados.valid){int sw=std::min(32,redGyarados.width);int sh=std::min(32,redGyarados.height);int sy=(int(newGameClock*4.0)&1)&&redGyarados.height>=64?32:0;blitImageCrop(f,redGyarados,0,sy,sw,sh,430,145,420,420,true);}
                    text(f,78,92,2,"SIGHTING AT THE LAKE OF RAGE",{0.95f,0.95f,0.95f,1});
                }
                // Retail TV sequence fades to Oak rather than displaying the old
                // debug/teaser caption.
                if(newGameClock<0.35)rect(f,0,0,LW,LH,{0,0,0,float(1.0-newGameClock/0.35)});
                if(newGameClock>5.25)rect(f,0,0,LW,LH,{0,0,0,float(std::clamp((newGameClock-5.25)/0.55,0.0,1.0))});
                return f;
            }

            // The authored later NSCR states contain Professor/Oak-era intro art.
            // Keep the state tied to the retail dialogue progression rather than
            // replacing it with a made-up "PROFESSOR OAK" panel.
            // Retail New Game application: clean pale stage + authored Nitro
            // screen-map accents + NCER-composed Professor Oak/Marill objects.
            // v0.21 incorrectly stretched one partial NSCR layer to the entire PC
            // window, which caused the giant striped face/corruption.
            // The NSCRs in members 43..52 are separate DS UI layers; stretching
            // one of them over the whole 1280x720 presentation was the v0.21
            // corruption. Keep them decoded for future dual-screen placement, but
            // render the authored object layer at the correct DS-centered scale.
            if(newGameStage<=4){
                if(!newGameOakCells.empty())blitImage(f,newGameOakCells.front(),160,0,960,720,true);
                if((newGameStage==2||newGameStage==3)&&!newGameMarillCells.empty()){
                    std::size_t mi=(std::size_t(newGameClock*5.0))%std::min<std::size_t>(2,newGameMarillCells.size());
                    blitImage(f,newGameMarillCells[mi],160,0,960,720,true);
                }
            } else if(newGameStage==6||newGameStage==8||newGameStage==9){
                auto const& cells=gameState.female?newGameGirlCells:newGameBoyCells;
                if(!cells.empty())blitImage(f,cells.front(),160,0,960,720,true);
                else {auto const& pose=gameState.female?newGameGirlPose:newGameBoyPose;if(pose.valid)blitImage(f,pose,160,0,960,720,true);}
            }

            if(newGameStage==5){
                rect(f,95,72,1090,205,{0.975f,0.975f,0.93f,0.96f});rect(f,110,87,1060,175,{0.08f,0.10f,0.12f,0.97f});
                std::string q=ngMessage(37);wrappedText(f,145,112,2,q.empty()?"Are you a boy? Or are you a girl?":q,59);
                const NitroRgbaImage* choices[2]={&newGameBoyPose,&newGameGirlPose};const char* names[2]={"BOY","GIRL"};
                for(int i=0;i<2;i++){float x=290+i*430;rect(f,x,335,280,205,i==newGameGender?Color{0.58f,0.40f,0.10f,0.96f}:Color{0.12f,0.16f,0.18f,0.92f});if(choices[i]->valid)blitImage(f,*choices[i],int(x+70),305,180,360,true);text(f,x+100,495,2,names[i],{1,1,1,1});}
                text(f,375,592,1,"LEFT / RIGHT   ENTER = CONFIRM",{0.92f,0.94f,0.96f,1});return f;
            }
            if(newGameStage==7){
                rect(f,90,60,1100,590,{0.055f,0.075f,0.085f,0.94f});
                auto prompt=ngMessage(40);wrappedText(f,160,105,2,prompt.empty()?"What is your name?":prompt,58);
                rect(f,310,190,660,58,{0.18f,0.24f,0.27f,1});text(f,340,207,3,newGameName.empty()?"_":newGameName+"_",{1.0f,0.90f,0.50f,1});
                for(int idx=0;idx<29;idx++){int row=idx/6,col=idx%6;float x=235+col*138,y=280+row*58;bool sel=idx==newGameNameCursor;if(sel)rect(f,x-12,y-9,116,40,{0.58f,0.40f,0.10f,1});std::string key;if(idx<26)key=std::string(1,char('A'+idx));else if(idx==26)key="SPACE";else if(idx==27)key="DEL";else key="OK";text(f,x,y,idx<26?2:1,std::string(sel?">":" ")+key,sel?Color{1,0.95f,0.72f,1}:Color{0.90f,0.94f,0.97f,1});}
                text(f,270,595,1,"D-PAD = SELECT   ENTER/A = TYPE   ESC/X = DELETE   7 CHARACTERS",{0.86f,0.91f,0.95f,1});return f;
            }
            if(newGameStage==11){
                // The previous port shrank textures.front(), i.e. an entire
                // overworld atlas, so unrelated tiles appeared as random garbage.
                // Shrink the retail full-body NCER actor first, then reveal one
                // actual composed standing frame from the overworld sprite set.
                float t=float(std::clamp(newGameClock/1.75,0.0,1.0));
                auto const& fullBody=gameState.female?newGameGirlCells:newGameBoyCells;
                if(t<0.72f&&!fullBody.empty()){
                    float q=std::clamp(t/0.72f,0.0f,1.0f);q=q*q*(3.0f-2.0f*q);
                    int dw=int(std::lround(960.0f+(230.0f-960.0f)*q));
                    int dh=int(std::lround(720.0f+(173.0f-720.0f)*q));
                    blitImage(f,fullBody.front(),int(LW*0.5f-dw*0.5f),int(LH*0.5f-dh*0.5f),dw,dh,true);
                }else{
                    auto const& chosen=gameState.female?heroineSprite:playerSprite;
                    if(auto const* standing=findWalkFrame(chosen,Dir::Down,false,0)){
                        float q=std::clamp((t-0.68f)/0.32f,0.0f,1.0f);
                        float scale=3.4f-0.8f*q;float w=standing->width*scale,h=standing->height*scale;
                        drawTexturePixels(f,*standing,LW*0.5f-w*0.5f,LH*0.5f-h*0.5f,scale,1.0f,false);
                    }
                }
                // Hide the exact full-body -> overworld frame swap with the same
                // quick bright flash style, then fade cleanly into the bedroom.
                float flash=0.0f;if(t>=0.62f&&t<0.72f)flash=(t-0.62f)/0.10f;else if(t>=0.72f&&t<0.82f)flash=1.0f-(t-0.72f)/0.10f;
                if(flash>0.0f)rect(f,0,0,LW,LH,{1,1,1,std::clamp(flash,0.0f,1.0f)});
                if(t>0.86f)rect(f,0,0,LW,LH,{1,1,1,std::clamp((t-0.86f)/0.14f,0.0f,1.0f)});
                return f;
            }
            int msg=-1;if(newGameStage==1)msg=6;else if(newGameStage==2)msg=34;else if(newGameStage==3)msg=35;else if(newGameStage==4)msg=36;else if(newGameStage==6)msg=gameState.female?39:38;else if(newGameStage==8)msg=gameState.female?42:41;else if(newGameStage==9)msg=43;
            std::string body=msg>=0?ngMessage(std::uint16_t(msg)):std::string{};if(body.empty())body="Your adventure is about to begin.";
            rect(f,84,484,1112,172,{0.97f,0.97f,0.93f,0.97f});rect(f,100,500,1080,140,{0.06f,0.075f,0.085f,0.97f});wrappedText(f,130,524,2,body,64);text(f,1082,611,1,"ENTER",{0.98f,0.82f,0.30f,1});return f;
        }
        if(mode==Mode::SavePrompt){
            RenderFrame f=renderField();rect(f,225,170,830,360,{0.035f,0.045f,0.055f,0.97f});rect(f,242,187,796,326,{0.88f,0.86f,0.74f,1});rect(f,255,200,770,300,{0.10f,0.12f,0.14f,1});text(f,292,238,3,"SAVE THE GAME?",{1,0.84f,0.30f,1});text(f,292,294,2,"PLAYER  "+gameState.playerName,{0.92f,0.95f,0.98f,1});text(f,292,330,2,"LOCATION  "+(romWorldReady?romWorld.locationName():map().name),{0.92f,0.95f,0.98f,1});int h=int(playSeconds/3600),m=int(playSeconds/60)%60;text(f,292,366,2,"TIME  "+std::to_string(h)+":"+(m<10?"0":"")+std::to_string(m),{0.92f,0.95f,0.98f,1});for(int i=0;i<2;i++){float x=420+i*290;if(i==savePromptIndex)rect(f,x-25,425,220,48,{0.54f,0.36f,0.08f,1});text(f,x,438,2,std::string(i==savePromptIndex?"> ":"  ")+(i==0?"YES":"NO"),{1,0.95f,0.78f,1});}return f;
        }
        if(mode==Mode::BankAmount){
            RenderFrame f=renderField();rect(f,235,150,810,420,{0.035f,0.045f,0.055f,0.98f});rect(f,255,170,770,380,{0.88f,0.86f,0.74f,1});rect(f,270,185,740,350,{0.10f,0.12f,0.14f,1});
            text(f,330,220,3,bankDeposit?"DEPOSIT WITH MOM":"WITHDRAW FROM MOM",{1,0.86f,0.34f,1});
            text(f,335,290,2,"WALLET  $"+std::to_string(gameState.money),{0.90f,0.95f,1,1});text(f,335,330,2,"SAVINGS $"+std::to_string(gameState.momSavings),{0.90f,0.95f,1,1});
            text(f,335,395,3,"AMOUNT  $"+std::to_string(bankAmount),{1,0.95f,0.70f,1});
            text(f,320,485,1,"LEFT/RIGHT +/-100   UP/DOWN +/-1000   SHIFT+UP MAX",{0.78f,0.86f,0.92f,1});text(f,360,515,1,"ENTER = OK   ESC = CANCEL",{0.78f,0.86f,0.92f,1});return f;
        }
        if(mode==Mode::Party){
            RenderFrame f;f.clear={0.09f,0.17f,0.18f,1};rect(f,70,55,1140,610,{0.91f,0.94f,0.88f,1});rect(f,90,75,1100,570,{0.12f,0.18f,0.19f,1});text(f,120,100,3,"POKEMON",{0.96f,0.82f,0.28f,1});
            if(gameState.party.empty())text(f,145,210,3,"NO POKEMON IN YOUR PARTY",{0.95f,0.95f,0.95f,1});
            for(std::size_t i=0;i<gameState.party.size()&&i<6;i++){auto const& m=gameState.party[i];float y=165+float(i)*70;bool sel=int(i)==partyIndex;rect(f,125,y,1030,55,sel?Color{0.48f,0.36f,0.11f,1}:Color{0.18f,0.27f,0.27f,1});std::string monName=m.nickname.empty()?hg_species_name(m.species):m.nickname;text(f,150,y+12,2,std::string(sel?"> ":"  ")+std::to_string(i+1)+"  "+monName+(m.egg?"  EGG":"  LV."+std::to_string(m.level)),{1,1,1,1});float hp=m.maxHp?float(m.hp)/m.maxHp:0;rect(f,660,y+18,300,14,{0.06f,0.08f,0.08f,1});rect(f,660,y+18,300*hp,14,hp>.5f?Color{0.30f,0.82f,0.35f,1}:hp>.2f?Color{0.95f,0.72f,0.22f,1}:Color{0.90f,0.25f,0.20f,1});text(f,985,y+11,1,std::to_string(m.hp)+" / "+std::to_string(m.maxHp));}
            std::string help=partySelectForScript?"ENTER / A = SELECT   ESC / X = CANCEL":partyReturnBattle?"ENTER / A = SWITCH   ESC / X = BACK":"ENTER / A = SUMMARY   ESC / X = BACK";text(f,118,610,1,help,{0.8f,0.88f,0.88f,1});return f;
        }
        if(mode==Mode::PCStorage){
            RenderFrame f;f.clear={0.07f,0.11f,0.15f,1};rect(f,55,42,1170,635,{0.86f,0.90f,0.90f,1});rect(f,75,62,1130,595,{0.08f,0.11f,0.14f,1});text(f,105,88,3,"POKEMON STORAGE SYSTEM",{0.98f,0.84f,0.32f,1});
            text(f,110,140,2,std::string(pcPartySide?"  ":"> ")+"BOX 1  "+std::to_string(gameState.pcStorage.size())+" / 540",pcPartySide?Color{0.65f,0.72f,0.78f,1}:Color{1,0.94f,0.62f,1});text(f,720,140,2,std::string(pcPartySide?"> ":"  ")+"PARTY  "+std::to_string(gameState.party.size())+" / 6",pcPartySide?Color{1,0.94f,0.62f,1}:Color{0.65f,0.72f,0.78f,1});
            auto const& list=pcPartySide?gameState.party:gameState.pcStorage;
            for(std::size_t i=0;i<list.size()&&i<10;i++){
                float y=195+float(i)*39;
                bool sel=int(i)==pcIndex;
                auto const&m=list[i];
                if(sel) rect(f,125,y-8,950,32,{0.46f,0.32f,0.08f,1});
                std::string n=m.nickname.empty()?hg_species_name(m.species):m.nickname;
                text(f,155,y,2,std::string(sel?"> ":"  ")+n+"  LV."+std::to_string(m.level)+(m.egg?"  EGG":""),{0.94f,0.97f,1,1});
            }
            if(list.empty()) text(f,155,230,2,pcPartySide?"NO POKEMON IN PARTY":"BOX IS EMPTY",{0.65f,0.72f,0.78f,1});
            text(f,180,615,1,"LEFT/RIGHT = BOX / PARTY   ENTER/A = WITHDRAW/DEPOSIT   ESC/X = BACK",{0.78f,0.86f,0.92f,1});
            return f;
        }
        if(mode==Mode::Bag){
            RenderFrame f;f.clear={0.23f,0.16f,0.08f,1};rect(f,85,55,1110,610,{0.93f,0.87f,0.67f,1});rect(f,105,75,1070,570,{0.13f,0.10f,0.075f,1});text(f,135,100,3,"BAG",{0.98f,0.82f,0.28f,1});text(f,860,106,2,"MONEY  $"+std::to_string(gameState.money),{0.85f,0.95f,0.75f,1});
            std::vector<std::pair<std::uint16_t,std::uint16_t>> items(gameState.bag.begin(),gameState.bag.end());std::sort(items.begin(),items.end());if(items.empty())text(f,150,210,3,"THE BAG IS EMPTY",{0.94f,0.94f,0.92f,1});for(std::size_t i=0;i<items.size()&&i<12;i++){float y=165+float(i)*34;text(f,155,y,2,hg_item_name(items[i].first),{1,1,1,1});text(f,785,y,2,"x"+std::to_string(items[i].second),{0.94f,0.84f,0.38f,1});}
            text(f,130,610,1,"ITEMS GIVEN BY ORIGINAL FIELD SCRIPTS APPEAR HERE   ESC / X = BACK",{0.8f,0.84f,0.78f,1});return f;
        }
        if(mode==Mode::Pokedex){
            RenderFrame f;f.clear={0.25f,0.035f,0.035f,1};rect(f,100,55,1080,610,{0.84f,0.08f,0.08f,1});rect(f,125,80,1030,560,{0.10f,0.12f,0.13f,1});text(f,155,108,3,"POKEDEX",{1,0.90f,0.70f,1});text(f,165,180,3,gameState.pokedex?"POKEDEX RECEIVED":"POKEDEX NOT YET RECEIVED",gameState.pokedex?Color{0.55f,1,0.60f,1}:Color{0.9f,0.55f,0.55f,1});text(f,165,245,3,"SEEN   "+std::to_string(gameState.dexSeen.size()),{0.84f,0.92f,1,1});text(f,165,295,3,"OWNED  "+std::to_string(gameState.dexOwned.size()),{0.84f,1,0.84f,1});float y=375;int shown=0;for(auto species:gameState.dexSeen){if(shown++>=8)break;text(f,165,y,2,"#"+std::to_string(species)+"  "+hg_species_name(species),{0.92f,0.94f,0.96f,1});y+=30;}text(f,155,600,1,"ESC / X OR ENTER = BACK",{0.9f,0.85f,0.85f,1});return f;
        }
        if(mode==Mode::Naming){
            RenderFrame f;f.clear={0.07f,0.11f,0.14f,1};rect(f,115,65,1050,590,{0.86f,0.84f,0.70f,1});rect(f,130,80,1020,560,{0.085f,0.11f,0.13f,1});
            std::string title=nameTarget==NameTarget::Rival?"NAME YOUR RIVAL":nameTarget==NameTarget::Nickname?"GIVE A NICKNAME":"ENTER YOUR NAME";text(f,365,110,3,title,{1,0.88f,0.38f,1});
            rect(f,310,175,660,58,{0.18f,0.24f,0.27f,1});text(f,340,192,3,appName.empty()?"_":appName+"_",{1.0f,0.90f,0.50f,1});
            for(int idx=0;idx<29;idx++){int row=idx/6,col=idx%6;float x=235+col*138,y=275+row*58;bool sel=idx==appNameCursor;if(sel)rect(f,x-12,y-9,116,40,{0.58f,0.40f,0.10f,1});std::string key;if(idx<26)key=std::string(1,char('A'+idx));else if(idx==26)key="SPACE";else if(idx==27)key="DEL";else key="OK";text(f,x,y,idx<26?2:1,std::string(sel?">":" ")+key,sel?Color{1,0.95f,0.72f,1}:Color{0.90f,0.94f,0.97f,1});}
            text(f,260,590,1,"D-PAD = SELECT   ENTER/A = TYPE   ESC/X = DELETE OR CANCEL",{0.76f,0.86f,0.94f,1});return f;
        }
        if(mode==Mode::Pokegear){
            RenderFrame f;f.clear={0.10f,0.13f,0.15f,1};rect(f,90,48,1100,625,{0.80f,0.18f,0.18f,1});rect(f,112,70,1056,581,{0.08f,0.10f,0.12f,1});text(f,145,98,4,"POKEGEAR",{1,0.88f,0.46f,1});
            const bool mapCard=gameState.hasPokegearCard(1),radioCard=gameState.hasPokegearCard(2);
            text(f,160,160,2,"TRAINER  "+gameState.playerName,{0.94f,0.96f,1,1});
            text(f,160,202,2,"PHONE CARD   INSTALLED",{0.62f,1.0f,0.68f,1});
            text(f,160,236,2,std::string("MAP CARD     ")+(mapCard?"INSTALLED":"NOT INSTALLED"),mapCard?Color{0.62f,1.0f,0.68f,1}:Color{0.58f,0.60f,0.62f,1});
            text(f,160,270,2,std::string("RADIO CARD   ")+(radioCard?"INSTALLED":"NOT INSTALLED"),radioCard?Color{0.62f,1.0f,0.68f,1}:Color{0.58f,0.60f,0.62f,1});
            text(f,680,160,2,"PHONE CONTACTS  "+std::to_string(gameState.phoneNumbers.size()),{0.94f,0.96f,1,1});
            text(f,680,196,2,"PENDING CALL STATE  "+std::to_string(gameState.phoneCallState.size()),{0.94f,0.96f,1,1});
            std::vector<std::uint16_t> contacts(gameState.phoneNumbers.begin(),gameState.phoneNumbers.end());std::sort(contacts.begin(),contacts.end());
            float y=330;int shown=0;for(auto id:contacts){if(shown++>=8)break;text(f,190,y,2,hgPhoneContactName(id),{0.74f,0.90f,0.98f,1});y+=32;}
            if(contacts.empty())text(f,190,330,2,"NO REGISTERED CONTACTS",{0.65f,0.70f,0.74f,1});
            text(f,185,600,1,"PHONE IS THE BASE CARD; MAP/RADIO USE THE RETAIL TWO-BIT CARD MASK",{0.86f,0.90f,0.94f,1});
            text(f,305,626,1,"ENTER/A OR ESC/X = CLOSE",{0.86f,0.90f,0.94f,1});return f;
        }
        if(mode==Mode::TownMap){
            RenderFrame f;f.clear={0.12f,0.18f,0.18f,1};rect(f,75,45,1130,630,{0.88f,0.84f,0.66f,1});rect(f,95,65,1090,590,{0.10f,0.14f,0.15f,1});text(f,445,92,3,"TOWN MAP",{1,0.90f,0.42f,1});
            rect(f,160,155,820,390,{0.28f,0.49f,0.33f,1});for(int i=0;i<8;i++)rect(f,215+i*95,215+(i%3)*88,14,14,{0.92f,0.84f,0.34f,1});rect(f,575,335,20,20,{0.95f,0.24f,0.22f,1});
            text(f,175,575,2,"CURRENT: "+(romWorldReady?romWorld.locationName():map().name),{0.90f,0.95f,1,1});if(romWorldReady)text(f,175,607,1,"MAP "+std::to_string(romWorld.mapId())+"   POSITION "+std::to_string(romWorld.x())+","+std::to_string(romWorld.y()),{0.75f,0.84f,0.88f,1});return f;
        }
        if(mode==Mode::Mart){
            RenderFrame f;f.clear={0.08f,0.12f,0.15f,1};rect(f,110,55,1060,610,{0.91f,0.90f,0.78f,1});rect(f,130,75,1020,570,{0.09f,0.12f,0.14f,1});
            std::string title=martSelling?"SELL ITEMS":martOpcode==277?"DECORATION SHOP":martOpcode==278?"SEAL SHOP":martOpcode==771?"POKEATHLON PRIZE SHOP":martOpcode==772?"POKEATHLON DATA CARDS":"POKE MART";
            text(f,160,105,3,title,{0.98f,0.83f,0.30f,1});text(f,850,110,2,(martAthlete?"AP ":"$")+std::to_string(martAthlete?gameState.athletePoints:gameState.money),{0.72f,1,0.72f,1});
            const int visible=10;const int top=std::clamp(martIndex-4,0,std::max(0,int(martEntries.size())-visible));
            for(int row=0;row<visible;row++){int idx=top+row;if(idx>=int(martEntries.size()))break;float y=165+float(row)*40;bool sel=idx==martIndex;if(sel)rect(f,155,y-9,900,34,{0.46f,0.31f,0.08f,1});auto const&e=martEntries[std::size_t(idx)];std::string name=martOpcode==277?hg_decoration_name(e.id):martOpcode==278?hg_seal_name(e.id):hg_item_name(e.id);text(f,180,y,2,std::string(sel?"> ":"  ")+name,e.sold?Color{0.55f,0.57f,0.58f,1}:Color{1,1,1,1});std::string cost=e.sold?"SOLD":(martAthlete?std::to_string(e.price)+" AP":"$"+std::to_string(e.price));text(f,875,y,2,cost,e.sold?Color{0.65f,0.65f,0.65f,1}:Color{0.94f,0.88f,0.45f,1});}
            if(martEntries.empty())text(f,330,320,2,martSelling?"YOU HAVE NOTHING TO SELL":"NO ITEMS AVAILABLE",{0.82f,0.88f,0.92f,1});
            if(!martEntries.empty()&&!martAthlete&&martOpcode!=277){auto const&e=martEntries[std::size_t(martIndex)];text(f,175,575,1,"QUANTITY x"+std::to_string(martQuantity)+"   TOTAL $"+std::to_string(std::uint64_t(e.price)*unsigned(std::max(1,martQuantity))),{0.88f,0.92f,0.72f,1});}
            std::string help=(martSelling?"ENTER/A = SELL":"ENTER/A = BUY");if(!martAthlete&&martOpcode!=277)help+="   LEFT/RIGHT = QUANTITY";help+="   ESC/X = LEAVE";text(f,175,610,1,help,{0.82f,0.88f,0.92f,1});return f;
        }
        if(mode==Mode::Summary){
            RenderFrame f;f.clear={0.10f,0.16f,0.20f,1};rect(f,75,48,1130,625,{0.90f,0.92f,0.84f,1});rect(f,95,68,1090,585,{0.08f,0.11f,0.13f,1});text(f,135,98,3,"POKEMON SUMMARY",{1,0.86f,0.34f,1});
            if(gameState.party.empty()){text(f,160,200,3,"NO POKEMON",{1,1,1,1});return f;}auto const&m=gameState.party[std::size_t(std::clamp(summaryIndex,0,int(gameState.party.size())-1))];std::string monName=m.nickname.empty()?hg_species_name(m.species):m.nickname;
            text(f,150,165,4,monName,{0.92f,0.98f,1,1});text(f,150,220,2,"SPECIES  "+hg_species_name(m.species),{0.82f,0.90f,0.95f,1});text(f,150,255,2,m.egg?"EGG":"LEVEL  "+std::to_string(m.level),{0.82f,0.90f,0.95f,1});text(f,150,290,2,"HP  "+std::to_string(m.hp)+" / "+std::to_string(m.maxHp),{0.82f,0.90f,0.95f,1});text(f,150,325,2,"FRIENDSHIP  "+std::to_string(m.friendship),{0.82f,0.90f,0.95f,1});text(f,150,360,2,"HELD ITEM  "+(m.heldItem?hg_item_name(m.heldItem):"NONE"),{0.82f,0.90f,0.95f,1});
            text(f,690,165,3,"MOVES",{0.98f,0.84f,0.35f,1});for(int i=0;i<4;i++)text(f,700,220+i*58,2,std::to_string(i+1)+"  "+(m.moves[i]?hg_move_name(m.moves[i]):"---"),{0.92f,0.95f,1,1});text(f,270,610,1,"UP/DOWN = OTHER POKEMON   ENTER/A OR ESC/X = BACK",{0.80f,0.87f,0.92f,1});return f;
        }
        if(mode==Mode::StarterSelect){
            // Keep the lab visible behind the selection UI, then render the actual
            // HG/SS follower resources for Chikorita/Cyndaquil/Totodile when the
            // original mmodel archive is available.
            RenderFrame f=romWorldReady?renderRomField():RenderFrame{};if(!romWorldReady)f.clear={0.06f,0.09f,0.11f,1};
            rect(f,80,55,1120,585,{0.025f,0.035f,0.045f,0.96f});rect(f,96,71,1088,553,{0.84f,0.80f,0.65f,0.98f});rect(f,108,83,1064,529,{0.075f,0.095f,0.105f,0.98f});
            text(f,315,105,4,"CHOOSE A POKEMON",{1,0.92f,0.56f,1});
            static const std::uint16_t choices[3]={152,155,158};
            for(int i=0;i<3;i++){
                float x=150+i*350;rect(f,x,205,280,285,i==starterIndex?Color{0.82f,0.62f,0.18f,1}:Color{0.20f,0.25f,0.28f,1});rect(f,x+10,215,260,265,{0.08f,0.11f,0.13f,1});
                if(starterSprites[std::size_t(i)].valid&&!starterSprites[std::size_t(i)].textures.empty())drawOverworldSprite(f,starterSprites[std::size_t(i)],x+140,350,Dir::Down,false,npcClock,1.0f,2.7f);
                else{rect(f,x+105,267,70,70,{0.88f,0.18f,0.16f,1});rect(f,x+105,300,70,8,{0.12f,0.12f,0.12f,1});}
                text(f,x+38,395,3,hg_species_name(choices[i]),{1,1,1,1});text(f,x+92,442,2,"LV. 5",{0.75f,0.92f,0.80f,1});
            }
            text(f,330,555,2,"LEFT / RIGHT   ENTER / A = CHOOSE   ESC / X = BACK",{0.9f,0.93f,0.96f,1});return f;
        }
        if(mode==Mode::Battle){
            // Retail starts an encounter task before the battle application.  The
            // exact effect/BGM selector comes from ARM9; reproduce its two principal
            // wipe families here while preserving the actual field frame underneath.
            if(battle.transitionClock<kBattleTransitionSeconds){
                RenderFrame f=battleEntryFrameValid?battleEntryFrame:RenderFrame{};
                if(!battleEntryFrameValid){f.clear={0,0,0,1};ensurePixels(f,f.clear);}
                const float q=float(std::clamp(battle.transitionClock/kBattleTransitionSeconds,0.0,1.0));
                if(q<0.16f){
                    const float flash=std::sin((q/0.16f)*3.14159265f);
                    pixelRect(f,0,0,RenderFrame::PixelWidth,RenderFrame::PixelHeight,{1,1,1,0.82f*flash});
                }
                const float w=std::clamp((q-0.10f)/0.82f,0.0f,1.0f);
                if(battle.trainer){
                    // Trainer encounters close the field in opposing horizontal bands;
                    // named trainer classes use the ROM's non-FFFF transition ids and
                    // retain their real trainer portrait during the middle beat.
                    constexpr int bands=12;
                    for(int i=0;i<bands;i++){
                        const float delay=float(i%3)*0.035f;
                        const float bq=std::clamp((w-delay)/(1.0f-delay),0.0f,1.0f);
                        const int bh=(RenderFrame::PixelHeight+bands-1)/bands;
                        const int bw=int(std::lround(RenderFrame::PixelWidth*bq));
                        const int y=i*bh;
                        if(i&1)pixelRect(f,0,y,bw,bh+1,{0,0,0,1});
                        else pixelRect(f,RenderFrame::PixelWidth-bw,y,bw,bh+1,{0,0,0,1});
                    }
                    if(battle.transitionId!=0xffff&&q>0.28f&&q<0.74f){
                        if(auto const* cells=opponentTrainerCells(battle.trainerClass);cells&&!cells->empty())
                            blitCroppedNitro(f,cells->front(),850,92,260,300,true);
                    }
                }else{
                    // Ordinary wild/legendary encounters use alternating strips that
                    // converge on black before the battle app is displayed.
                    constexpr int bands=16;
                    for(int i=0;i<bands;i++){
                        const int bh=(RenderFrame::PixelHeight+bands-1)/bands;
                        const float phase=float((i*5)%bands)/float(bands)*0.16f;
                        const float bq=std::clamp((w-phase)/(1.0f-phase),0.0f,1.0f);
                        const int bw=int(std::lround(RenderFrame::PixelWidth*bq));
                        const int y=i*bh;
                        if(i&1)pixelRect(f,0,y,bw,bh+1,{0,0,0,1});
                        else pixelRect(f,RenderFrame::PixelWidth-bw,y,bw,bh+1,{0,0,0,1});
                    }
                }
                if(q>0.90f)pixelRect(f,0,0,RenderFrame::PixelWidth,RenderFrame::PixelHeight,{0,0,0,1});
                return f;
            }

            RenderFrame f;f.clear={0.08f,0.11f,0.13f,1};
            drawBattleBackdrop(f,currentBattleBackdrop());
            // Both authored terrain objects are established before the battler
            // Pokepics; the player/enemy origins below are derived from these same
            // objects so sprites stay centered on their own ellipses.
            drawBattleTerrain(f,true);
            drawBattleTerrain(f,false);

            const double scene=battle.sceneClock;
            const double act=battle.actionClock;
            const bool initialSendOut=battle.enemyIndex==0;
            // In trbgra sequence 1 the authored release pose starts after 40 NANR
            // ticks. Drive it from the StartEncounter player-send-out phase, not
            // from total battle scene time, so text/transition waits cannot freeze
            // the trainer on an arbitrary animation cell.
            const double throwClock=(initialSendOut&&battle.introPhase==3)?battle.introClock:(battle.introPhase>3?kPlayerSendOutWait:0.0);
            const float playerAppear=initialSendOut
                ?(battle.introPhase<3?0.0f:float(std::clamp((throwClock-(40.0/60.0))/0.34,0.0,1.0)))
                :1.0f;
            float enemyAppear=1.0f;
            if(initialSendOut){
                if(battle.trainer){
                    enemyAppear=battle.introPhase<2?0.0f:(battle.introPhase==2?float(std::clamp((battle.introClock-0.08)/0.36,0.0,1.0)):1.0f);
                }else{
                    enemyAppear=battle.introPhase==0?float(std::clamp((battle.introClock-0.22)/0.36,0.0,1.0)):1.0f;
                }
            }else enemyAppear=float(std::clamp((scene-0.12)/0.36,0.0,1.0));
            float hitPulse=1.0f;
            if((battle.turnStep==1||battle.turnStep==2)&&act<0.30){
                float q=float(std::clamp(act/0.30,0.0,1.0));hitPulse=1.0f-0.14f*std::sin(q*3.14159265f);
            }
            const float enemyHit=((battle.turnStep==1||battle.turnStep==2)&&battle.actionByPlayer)?hitPulse:1.0f;
            const float playerHit=((battle.turnStep==1||battle.turnStep==2)&&!battle.actionByPlayer)?hitPulse:1.0f;
            auto hpColor=[&](float q){return q>0.5f?Color{0.20f,0.76f,0.22f,1}:q>0.2f?Color{0.96f,0.78f,0.16f,1}:Color{0.92f,0.24f,0.16f,1};};

            // Single-screen PC composition: retain the ROM trainer/Pokemon/HUD art,
            // but promote the original touch commands into the lower part of this
            // same 1280x720 frame instead of drawing a second DS screen.
            if(initialSendOut&&battle.introPhase<4){
                auto const& cells=playerBattleBackCells();
                if(!cells.empty()){
                    auto const& nanr=playerBattleBackAnim();
                    std::size_t ci=0;
                    if(battle.introPhase==3){
                        ci=nanr.valid?sample_nitro_nanr_cell(nanr,nanr.sequences.size()>1?1:0,std::min(battle.introClock,kPlayerThrowSeconds)):std::size_t(battle.introClock*10.0);
                    }else if(nanr.valid){
                        // SetTrainerEncounter PLAYER presents the idle back pose until
                        // ThrowPokeball starts. Sequence 0 is that retail idle cell.
                        ci=sample_nitro_nanr_cell(nanr,0,0.0);
                    }
                    ci=std::min<std::size_t>(cells.size()-1,ci);
                    // The NCER decoder now applies both the bank's 1D OBJ boundary
                    // and per-cell VRAM-transfer source table. Keep the full DS OAM
                    // plane so every frame retains its authored object coordinates.
                    constexpr float scale=535.0f/192.0f;
                    const float viewX=float(RenderFrame::PixelWidth-int(std::lround(256.0f*scale)))/2.0f;
                    // ManagedSprite movement in retail translates the complete OAM
                    // plane.  Keep Ethan/Lyra in the authored lower-left battle
                    // position instead of treating (128,96) as final screen space.
                    // Keep the authored 256x192 OAM plane pinned to one DS-space
                    // anchor for every NANR cell. Per-frame image bounds must never
                    // alter the trainer's screen position during the throw.
                    constexpr float kTrainerBackAnchorX=56.0f,kTrainerBackAnchorY=108.0f;
                    drawDsAnchoredCell(f,cells[ci],kTrainerBackAnchorX,kTrainerBackAnchorY,scale,viewX,0.0f);
                }
            }

            if(battle.trainer&&initialSendOut&&battle.introPhase<=2){
                if(auto const* cells=opponentTrainerCells(battle.trainerClass);cells&&!cells->empty())
                    blitCroppedNitro(f,cells->front(),940,55,245,285,true);
            }

            if(auto* me=gameState.leadAlive()){
                if(auto const* mon=battleMonSprite(me->species,me->gender,true,false)){
                    float q=playerAppear;q=q*q*(3.0f-2.0f*q);
                    auto b=nitroOpaqueBounds(*mon);int bw=b.w?b.w:80,bh=b.h?b.h:80;
                    int dw=int(std::lround(float(bw)*3.25f*q*playerHit));
                    int dh=int(std::lround(float(bh)*3.25f*q*playerHit));
                    auto floor=battleFloorPlacement(true);
                    const int cx=int(std::lround(floor.valid?floor.centerX:506.0f));
                    const int baseY=int(std::lround(floor.valid?floor.surfaceY:402.0f));
                    if(dw>4&&dh>4)blitCroppedNitro(f,*mon,cx-dw/2,baseY-dh,dw,dh,true);
                }
            }
            if(auto const* foe=battleMonSprite(battle.enemy.species,battle.enemy.gender,false,false)){
                float q=enemyAppear;q=q*q*(3.0f-2.0f*q);
                auto b=nitroOpaqueBounds(*foe);int bw=b.w?b.w:80,bh=b.h?b.h:80;
                int dw=int(std::lround(float(bw)*2.75f*q*enemyHit));
                int dh=int(std::lround(float(bh)*2.75f*q*enemyHit));
                auto floor=battleFloorPlacement(false);
                const int cx=int(std::lround(floor.valid?floor.centerX:774.0f));
                const int baseY=int(std::lround(floor.valid?floor.surfaceY:264.0f));
                if(dw>4&&dh>4)blitCroppedNitro(f,*foe,cx-dw/2,baseY-dh,dw,dh,true);
            }

            // Retail move-object path.  Every move now enters the ROM's shared WE
            // NCGR/NCER/NANR object system instead of the old PC-only hit pulse.
            // The battle script's effect id selects the authored object bank while
            // NANR supplies cell order/durations at native 60 Hz timing.
            if((battle.turnStep==1||battle.turnStep==2)&&battle.currentMove&&act<1.25){
                const int fxIndex=int(battle.currentMoveEffect%37u);
                if(auto const* fx=battleFxPack(fxIndex);fx&&!fx->cells.empty()){
                    std::size_t ci=0;
                    if(fx->anim.valid)ci=sample_nitro_nanr_cell(fx->anim,0,act);
                    ci=std::min<std::size_t>(ci,fx->cells.size()-1);
                    const float targetX=battle.actionByPlayer?980.0f:320.0f;
                    const float targetY=battle.actionByPlayer?245.0f:425.0f;
                    const float sourceX=battle.actionByPlayer?350.0f:930.0f;
                    const float sourceY=battle.actionByPlayer?405.0f:235.0f;
                    float q=float(std::clamp(act/0.62,0.0,1.0));q=q*q*(3.0f-2.0f*q);
                    float x=sourceX+(targetX-sourceX)*q,y=sourceY+(targetY-sourceY)*q;
                    auto const& im=fx->cells[ci];
                    drawDsAnchoredCell(f,im,128,96,1.45f,x,y);
                }
                // Camera/impact choreography is driven from the actual move class:
                // physical attacks lunge, special attacks flash, status attacks pulse.
                float impact=float(std::clamp((act-0.35)/0.30,0.0,1.0));
                if(impact>0.0f&&impact<1.0f){
                    float pulse=std::sin(impact*3.14159265f);
                    if(battle.currentMoveCategory==1)pixelRect(f,0,0,RenderFrame::PixelWidth,535,{1,1,1,0.12f*pulse});
                    else if(battle.currentMoveCategory==2)pixelRect(f,0,0,RenderFrame::PixelWidth,535,{0.72f,0.82f,1.0f,0.07f*pulse});
                }
            }

            // Preserve the retail single-battle topology: foe status is upper-left,
            // player status is lower-right, opposing Pokemon is upper-right and the
            // player's back sprite is lower-left. The NCER HP boxes are 128x64;
            // scale them uniformly so the cartridge artwork is not distorted.
            constexpr int hpW=320,hpH=160;
            constexpr int foeBoxX=62,foeBoxY=48,playerBoxX=858,playerBoxY=350;
            pixelRect(f,foeBoxX,foeBoxY,hpW,hpH,{0.92f,0.92f,0.89f,0.96f});
            if(battleEnemyHpBox.valid)blitCroppedNitro(f,battleEnemyHpBox,foeBoxX,foeBoxY,hpW,hpH,true);
            float eh=battle.enemy.maxHp?float(battle.enemy.hp)/battle.enemy.maxHp:0;
            text(f,foeBoxX+34,foeBoxY+29,2,hg_species_name(battle.enemy.species),{0.06f,0.07f,0.08f,1},false);
            text(f,foeBoxX+244,foeBoxY+30,1,"Lv."+std::to_string(battle.enemy.level),{0.06f,0.07f,0.08f,1},false);
            rect(f,foeBoxX+84,foeBoxY+93,200,12,{0.20f,0.22f,0.20f,1});
            rect(f,foeBoxX+84,foeBoxY+93,200*eh,12,hpColor(eh));
            auto est=statusName(battle.enemy.status);if(!est.empty())text(f,foeBoxX+190,foeBoxY+61,1,est,{0.45f,0.18f,0.55f,1},false);

            if(auto* me=gameState.leadAlive()){
                pixelRect(f,playerBoxX,playerBoxY,hpW,hpH,{0.92f,0.92f,0.89f,0.96f});
                if(battlePlayerHpBox.valid)blitCroppedNitro(f,battlePlayerHpBox,playerBoxX,playerBoxY,hpW,hpH,true);
                float hp=me->maxHp?float(me->hp)/me->maxHp:0;
                text(f,playerBoxX+34,playerBoxY+29,2,hg_species_name(me->species),{0.06f,0.07f,0.08f,1},false);
                text(f,playerBoxX+244,playerBoxY+30,1,"Lv."+std::to_string(me->level),{0.06f,0.07f,0.08f,1},false);
                rect(f,playerBoxX+84,playerBoxY+86,200,12,{0.20f,0.22f,0.20f,1});
                rect(f,playerBoxX+84,playerBoxY+86,200*hp,12,hpColor(hp));
                text(f,playerBoxX+174,playerBoxY+113,1,std::to_string(me->hp)+" / "+std::to_string(me->maxHp),{0.06f,0.07f,0.08f,1},false);
                auto pst=statusName(me->status);if(!pst.empty())text(f,playerBoxX+190,playerBoxY+58,1,pst,{0.45f,0.18f,0.55f,1},false);
            }

            // One ordinary lower command panel, not a simulated DS touchscreen.
            rect(f,0,535,LW,185,{0.055f,0.07f,0.085f,1});
            rect(f,12,547,LW-24,161,{0.89f,0.87f,0.76f,1});
            rect(f,22,557,LW-44,141,{0.08f,0.105f,0.12f,1});
            if(!battle.message.empty()){
                wrappedText(f,50,580,2,battle.message,71,{1,1,1,1});
                if(battle.awaiting)text(f,1130,668,1,"ENTER / CLICK",{1,0.86f,0.42f,1},false);
            }else if(battle.choosingMove&&gameState.leadAlive()){
                auto* me=gameState.leadAlive();
                text(f,48,578,2,"CHOOSE A MOVE",{0.96f,0.90f,0.60f,1},false);
                for(int i=0;i<4;i++){
                    auto r=HG_BATTLE_MOVE_RECTS[std::size_t(i)];
                    rect(f,r.x,r.y,r.w,r.h,i==battle.moveMenu?Color{0.58f,0.39f,0.10f,1}:Color{0.18f,0.23f,0.26f,1});
                    rect(f,r.x+5,r.y+5,r.w-10,r.h-10,{0.10f,0.13f,0.15f,1});
                    auto mv=me->moves[i];std::string name=mv?hg_move_name(mv):"---";
                    text(f,r.x+18,r.y+12,2,name,{1,0.96f,0.84f,1},false);
                    if(mv){if(auto* md=hg_move_data(mv))text(f,r.x+190,r.y+14,1,std::string(hg_type_name(md->type))+"  PP "+std::to_string(me->pp[i])+"/"+std::to_string(me->maxPp[i]),{0.76f,0.88f,0.96f,1},false);}
                }
                auto cr=HG_BATTLE_CANCEL_RECT;
                rect(f,cr.x,cr.y,cr.w,cr.h,{0.28f,0.19f,0.19f,1});
                text(f,cr.x+8,cr.y+24,1,"BACK",{1,0.90f,0.82f,1},false);
            }else{
                auto* me=gameState.leadAlive();
                text(f,48,578,2,"WHAT WILL "+std::string(me?hg_species_name(me->species):"POKEMON")+" DO?",{0.96f,0.90f,0.60f,1},false);
                static const char* opts[]={"FIGHT","BAG","POKEMON","RUN"};
                for(int i=0;i<4;i++){
                    auto r=HG_BATTLE_MAIN_RECTS[std::size_t(i)];
                    rect(f,r.x,r.y,r.w,r.h,i==battle.menu?Color{0.58f,0.39f,0.10f,1}:Color{0.18f,0.23f,0.26f,1});
                    rect(f,r.x+5,r.y+5,r.w-10,r.h-10,{0.10f,0.13f,0.15f,1});
                    text(f,r.x+50,r.y+19,2,opts[i],{1,0.96f,0.84f,1},false);
                }
            }
            return f;
        }
        RenderFrame f=renderField();
        if(mode==Mode::Dialogue||mode==Mode::ScriptChoice){rect(f,72,LH-174,LW-144,142,{0.04f,0.05f,0.07f,1});rect(f,80,LH-166,LW-160,126,{0.90f,0.88f,0.78f,1});rect(f,88,LH-158,LW-176,110,{0.10f,0.12f,0.14f,1});if(!speaker.empty()){rect(f,104,LH-188,280,34,{0.08f,0.10f,0.12f,1});text(f,118,LH-178,2,speaker,{0.98f,0.82f,0.32f,1});} std::string page=dialogue.empty()?"":dialogue[dialoguePage];
            const std::size_t wrap=58; float y=LH-136; for(std::size_t pos=0;pos<page.size();){std::size_t len=std::min(wrap,page.size()-pos);if(pos+len<page.size()){auto sp=page.rfind(' ',pos+len);if(sp!=std::string::npos&&sp>pos)len=sp-pos;}text(f,112,y,2,page.substr(pos,len),{1,1,1,1},true,14.0f,32.0f);pos+=len;while(pos<page.size()&&page[pos]==' ')pos++;y+=32;}text(f,LW-122,LH-76,1,"ENTER",{0.95f,0.82f,0.34f,1});
            if(mode==Mode::ScriptChoice){
                const float boxW=420.0f;const float rowH=42.0f;const float boxH=32.0f+rowH*float(std::max<std::size_t>(1,scriptChoiceOptions.size()));const float bx=LW-boxW-82.0f;const float by=std::max(72.0f,LH-174.0f-boxH-12.0f);
                rect(f,bx,by,boxW,boxH,{0.04f,0.05f,0.07f,1});rect(f,bx+8,by+8,boxW-16,boxH-16,{0.88f,0.86f,0.73f,1});rect(f,bx+14,by+14,boxW-28,boxH-28,{0.10f,0.12f,0.14f,1});
                for(std::size_t i=0;i<scriptChoiceOptions.size();++i){float yy=by+24.0f+float(i)*rowH;if(int(i)==scriptChoiceIndex)rect(f,bx+24,yy-5,boxW-48,32,{0.48f,0.32f,0.08f,1});text(f,bx+38,yy,2,std::string(int(i)==scriptChoiceIndex?"> ":"  ")+scriptChoiceOptions[i].first,{1,0.96f,0.80f,1});}
            }
        }
        else if(mode==Mode::Menu){static const char* items[]={"POKEDEX","POKEMON","PC STORAGE","BAG","POKEGEAR","SPRITES","REAL MODELS","WORLD DATA","SAVE","CONTROLS","CLOSE"};rect(f,LW-390,38,334,642,{0.04f,0.05f,0.07f,1});rect(f,LW-380,48,314,622,{0.82f,0.78f,0.64f,1});rect(f,LW-370,58,294,602,{0.12f,0.14f,0.16f,1});text(f,LW-336,78,3,"MENU",{0.95f,0.78f,0.24f,1});for(int i=0;i<11;i++){float y=118+i*45;if(i==menuIndex)rect(f,LW-350,y-10,250,40,{0.45f,0.30f,0.08f,1});bool locked=(i==4&&!hasPokegear());Color c=locked?Color{0.48f,0.50f,0.52f,1}:(i==menuIndex?Color{1,0.92f,0.55f,1}:Color{0.93f,0.94f,0.96f,1});text(f,LW-330,y,2,std::string(i==menuIndex?"> ":"  ")+items[i],c);}}
        return f;
    }
};

NativeGame::NativeGame(std::filesystem::path assets,std::filesystem::path savePath):p(std::make_unique<Impl>(std::move(assets),std::move(savePath))){}
NativeGame::~NativeGame()=default;
bool NativeGame::validate(){
    p->stats=scan_assets(p->assets); p->assetsReady=p->stats.files>0;
    if(p->assetsReady)p->recoverRetailProgressionFlags();
    if(!p->assetsReady){std::cerr<<"Warning: no extracted NitroFS files under "<<p->assets<<". The fallback scene can still run, but ROM-world integration is disabled.\n";return false;}
    std::cout<<"Asset root: "<<p->assets<<"\nFiles: "<<p->stats.files<<"  Bytes: "<<p->stats.bytes<<"\nNARC: "<<p->stats.narc<<"  NSBMD: "<<p->stats.models<<"  BTX0: "<<p->stats.textures<<"  2D gfx blocks: "<<p->stats.sprites<<"  SDAT: "<<p->stats.soundArchives<<"\n";
    struct Probe{const char* label;const char* path;};
    const Probe probes[]={{"Field models","fielddata/build_model/bm_field.narc"},{"Room models","fielddata/build_model/bm_room.narc"},{"Scenario messages","msgdata/scenario/scr_msg.narc"}};
    for(auto const& probe:probes){auto info=inspect_narc(p->assets/probe.path);if(info.valid)std::cout<<"  "<<probe.label<<": "<<info.members.size()<<" archive members\n";}
    auto field=p->assets/"fielddata/build_model/bm_field.narc";auto mini=validate_nsbmd_narc(field,8);std::cout<<"NSBMD smoke test: "<<mini.parsedMembers<<"/"<<mini.members<<" members, "<<mini.triangles<<" triangles\n";p->loadAssetMember();
    p->worldProps.clear();for(std::size_t idx:{std::size_t(11),std::size_t(2),std::size_t(5)}){auto rm=load_nsbmd_from_narc(field,idx);if(rm.valid&&!rm.models.empty())p->worldProps.push_back(std::move(rm));}
    std::size_t propTextured=0,propTriangles=0;for(auto const& rm:p->worldProps)for(auto const& md:rm.models)for(auto const& tr:md.triangles){propTriangles++;if(tr.textureIndex>=0&&size_t(tr.textureIndex)<rm.textures.size())propTextured++;}
    std::cout<<"Playable field real-model props: "<<p->worldProps.size()<<" loaded; texture-bound triangles "<<propTextured<<"/"<<propTriangles<<"\n";
    auto mmodel=p->assets/"a/0/8/1";
    p->playerSprite=load_nitro_texture_from_narc(mmodel,69);p->heroineSprite=load_nitro_texture_from_narc(mmodel,70);p->boySprite=load_nitro_texture_from_narc(mmodel,0);p->doctorSprite=load_nitro_texture_from_narc(mmodel,54);p->aideSprite=load_nitro_texture_from_narc(mmodel,25);p->momSprite=load_nitro_texture_from_narc(mmodel,159);p->picnicSprite=load_nitro_texture_from_narc(mmodel,50);
    // HGSS follower mmodel members for the three Johto starters.  These are used
    // by the native ChooseStarter presentation instead of text-only placeholders.
    p->starterSprites[0]=load_nitro_texture_from_narc(mmodel,450);
    p->starterSprites[1]=load_nitro_texture_from_narc(mmodel,454);
    p->starterSprites[2]=load_nitro_texture_from_narc(mmodel,457);
    p->starterBallSprite=load_nitro_texture_from_narc(mmodel,94);
    {
        const auto backs=p->assets/"a/0/0/6";
        p->battleBoyBackCells=decode_nitro_cells(readNitro2dMember(backs,0),readNitro2dMember(backs,2),readNitro2dMember(backs,1),true);
        p->battleBoyBackAnim=decode_nitro_nanr(readNitro2dMember(backs,3));
        // trbgra groups are five members each.  Group 0 is Ethan; group 1 is
        // Lyra. v0.27 accidentally pointed at group 15 for the female pose.
        p->battleGirlBackCells=decode_nitro_cells(readNitro2dMember(backs,5),readNitro2dMember(backs,7),readNitro2dMember(backs,6),true);
        p->battleGirlBackAnim=decode_nitro_nanr(readNitro2dMember(backs,8));
        const auto hud=p->assets/"a/0/0/8";
        auto enemy=decode_nitro_cells(readNitro2dMember(hud,188),readNitro2dMember(hud,187),readNitro2dMember(hud,71),true);
        auto player=decode_nitro_cells(readNitro2dMember(hud,191),readNitro2dMember(hud,190),readNitro2dMember(hud,71),true);
        if(!enemy.empty())p->battleEnemyHpBox=std::move(enemy.front());
        if(!player.empty())p->battlePlayerHpBox=std::move(player.front());
        // Retail touch-screen BattleInput resources. The main menu uses screen
        // buffers 1/2/0 -> NARC members 36/41/43 over char member 28, while
        // the Fight menu uses screen 37 over the shared base screen 43.
        const auto menuArc=p->assets/"a/0/0/7";
        auto menuLayer=[&](int screen){return decode_nitro_bg(readNitro2dMember(menuArc,28),readNitro2dMember(menuArc,std::size_t(screen)),readNitro2dMember(menuArc,246),true);};
        p->battleMenuMessage=menuLayer(43);
        p->battleMenuMain=compositeNitroLayers({menuLayer(43),menuLayer(41),menuLayer(36)});
        p->battleMenuFight=compositeNitroLayers({menuLayer(43),menuLayer(37)});
        std::cout<<"Retail battle presentation: Ethan back="<<p->battleBoyBackCells.size()
                 <<" cells, Lyra back="<<p->battleGirlBackCells.size()
                 <<" cells, enemy/player HP boxes="<<(p->battleEnemyHpBox.valid?"OK":"FAIL")<<"/"<<(p->battlePlayerHpBox.valid?"OK":"FAIL")
                 <<", touch menus="<<(p->battleMenuMessage.valid?"OK":"FAIL")<<"/"<<(p->battleMenuMain.valid?"OK":"FAIL")<<"/"<<(p->battleMenuFight.valid?"OK":"FAIL")<<"\n";
    }
    p->authenticSpriteSets=0;for(auto const* sp:{&p->playerSprite,&p->heroineSprite,&p->boySprite,&p->doctorSprite,&p->aideSprite,&p->momSprite,&p->picnicSprite})if(sp->valid&&!sp->textures.empty())p->authenticSpriteSets++;
    auto spriteArchiveInfo=inspect_narc(mmodel);p->spriteArchiveMembers=spriteArchiveInfo.valid?spriteArchiveInfo.members.size():0;p->spriteViewMember=69;p->loadSpriteViewerMember();
    int starterSpriteSets=0;for(auto const& sp:p->starterSprites)if(sp.valid&&!sp.textures.empty())starterSpriteSets++;
    std::cout<<"Original HG overworld sprites: "<<p->authenticSpriteSets<<"/7 gameplay sets loaded; starter followers "<<starterSpriteSets<<"/3; browser exposes "<<p->spriteArchiveMembers<<" mmodel members; player frames="<<p->playerSprite.textures.size()<<"\n";
    {
        auto introArc=p->assets/"a/2/6/2";
        auto unpack=[&](std::size_t i){auto raw=read_narc_member(introArc,i);auto dec=nitro_lz10_decompress(raw);return dec.empty()?raw:dec;};
        auto bg=[&](int pal,int gfx,int map,bool transparent){return decode_nitro_bg(unpack(std::size_t(gfx)),unpack(std::size_t(map)),unpack(std::size_t(pal)),transparent);};
        p->introMovie.clear();
        auto push=[&](double duration,std::vector<NitroRgbaImage> frames){Impl::IntroMovieScene sc;sc.duration=duration;for(auto& f:frames)if(f.valid)sc.frames.push_back(std::move(f));if(!sc.frames.empty())p->introMovie.push_back(std::move(sc));};
        // Scene 1 uses separate copyright, Game Freak and sunrise states in retail.
        // v0.13 accidentally held copyright for half the scene and skipped the
        // Game Freak background entirely.
        auto s1copyright=bg(1,5,13,false);
        auto s1gamefreak=bg(0,4,12,false);
        auto s1sun=fillNitroTransparencyFromArtwork(compositeNitroLayers({bg(1,7,18,true),bg(1,7,17,true),bg(1,7,16,true)}));
        push(8.8,{std::move(s1copyright),std::move(s1gamefreak),std::move(s1sun)});
        // Scene 3's New Bark / Goldenrod / Ecruteak shots are genuine Nitro 3D
        // resources in the opening archive. Their BCA/BTA curve evaluator is not
        // complete yet, but rendering the authored BMDs fixes the previous empty
        // background-only portions of the movie.
        p->introMapModels[0]=load_nsbmd_from_narc(introArc,103);
        p->introMapModels[1]=load_nsbmd_from_narc(introArc,100);
        p->introMapModels[2]=load_nsbmd_from_narc(introArc,97);
        p->introHoohCells=decode_nitro_cells(unpack(24),unpack(26),unpack(23),true);p->introHoohAnim=decode_nitro_nanr(unpack(25));
        p->introLugiaCells=decode_nitro_cells(unpack(28),unpack(30),unpack(27),true);p->introLugiaAnim=decode_nitro_nanr(unpack(29));
        p->introRivalCells=decode_nitro_cells(unpack(66),unpack(68),unpack(65),true);p->introRivalAnim=decode_nitro_nanr(unpack(67));
        p->introHeroCells=decode_nitro_cells(unpack(74),unpack(76),unpack(73),true);p->introHeroAnim=decode_nitro_nanr(unpack(75));
        p->introGrassCells=decode_nitro_cells(unpack(78),unpack(80),unpack(77),true);p->introGrassAnim=decode_nitro_nanr(unpack(79));
        p->introTouchCells=decode_nitro_cells(unpack(82),unpack(84),unpack(81),true);p->introTouchAnim=decode_nitro_nanr(unpack(83));
        // Scene 2: main BG2/BG1/BG0 are concurrent in retail. Composite them instead of
        // displaying the partial tilemaps one at a time (the source of the earlier broken look).
        auto s2=fillNitroTransparencyFromArtwork(compositeNitroLayers({
            bg(31,33,37,false), // 512x512 backing field
            bg(32,34,35,true),bg(32,34,36,true),bg(32,34,38,true)
        }));
        push(6.7,{std::move(s2)});
        // Scene 3 swaps rival/beast sections into BG0 while BG1-3 remain active.
        std::vector<NitroRgbaImage> s3;for(int dynamic:{42,43,44,45,47,50,51,52})s3.push_back(fillNitroTransparencyFromArtwork(compositeNitroLayers({bg(39,40,46,true),bg(39,40,49,true),bg(39,40,48,true),bg(39,dynamic>=50?41:40,dynamic,true)})));
        push(7.4,std::move(s3));
        // Scene 4 simultaneously layers maps 56/55/58 on both engines.
        auto s4=fillNitroTransparencyFromArtwork(compositeNitroLayers({bg(53,54,56,true),bg(53,54,55,true),bg(53,54,58,true)}));push(6.4,{std::move(s4)});
        // Scene 5 main-screen vertical composition uses maps 64 + 62 and is scrolled by the runtime.
        auto s5=fillNitroTransparencyFromArtwork(compositeNitroLayers({
            bg(53,59,64,false),bg(53,60,62,true),bg(53,60,63,true)
        }));push(5.2,{std::move(s5)});
        std::size_t frames=0;for(auto const& sc:p->introMovie)frames+=sc.frames.size();
        std::cout<<"Original five-scene intro: "<<p->introMovie.size()<<" scenes / "<<frames<<" composited retail background states from opening.narc\n";
    }
    {
        p->mainMenuMessages=HgMessageBank(load_hg_message_bank(p->assets,442));
        p->newGameMessages=HgMessageBank(load_hg_message_bank(p->assets,219));
        auto iconArc=p->assets/"pbr/poke_icon.narc";
        p->redGyarados=decode_nitro_char_sheet(read_narc_member(iconArc,137),read_narc_member(iconArc,0),true,0);
        if(p->redGyarados.valid){for(std::size_t i=0;i+3<p->redGyarados.rgba.size();i+=4){auto& r=p->redGyarados.rgba[i];auto& g=p->redGyarados.rgba[i+1];auto& b=p->redGyarados.rgba[i+2];if(p->redGyarados.rgba[i+3]&&b>r+20&&b>g+15){r=230;g=52;b=42;}}}

        // Oak / New Game is a different Nitro archive from opening.narc.  The
        // retail source names it demo/intro/intro and it contains the TV maps
        // (0-9), Professor/new-game graphic states (34-52) and four NCER/NANR
        // animation groups (53-66). Use the original archive instead of the
        // hand-built v0.14 placeholder screen.
        std::filesystem::path ngArc;
        for(auto const& candidate:{p->assets/"demo/intro/intro.narc",p->assets/"demo/intro/intro",p->assets/"a/1/2/0"}){
            auto ni=inspect_narc(candidate);if(ni.valid&&ni.members.size()>=67){ngArc=candidate;break;}
        }
        p->newGameTvFrames.clear();p->newGameOakFrames.clear();p->newGameRetailAssets=false;
        if(!ngArc.empty()){
            auto unpackNg=[&](std::size_t i){auto raw=read_narc_member(ngArc,i);auto dec=nitro_lz10_decompress(raw);return dec.empty()?raw:dec;};
            auto score=[](NitroRgbaImage const& im){
                if(!im.valid)return std::uint64_t(0);
                std::uint64_t n=0,detail=0;
                std::uint32_t prev=0;
                bool have=false;
                for(std::size_t i=0;i+3<im.rgba.size();i+=4){
                    if(!im.rgba[i+3])continue;
                    n++;
                    std::uint32_t c=(std::uint32_t(im.rgba[i])<<16)|(std::uint32_t(im.rgba[i+1])<<8)|im.rgba[i+2];
                    if(have&&c!=prev)detail++;
                    prev=c;
                    have=true;
                }
                return n+detail*3;
            };
            // The first NCGR plus the two palettes drive the Red Gyarados TV
            // sequence across NSCR members 3..9. Pick the palette that produces
            // the authored non-empty state for each map.
            auto tvGfx=unpackNg(0),tvPal1=unpackNg(1),tvPal2=unpackNg(2);
            for(int map=3;map<=9;map++){
                auto nscr=unpackNg(std::size_t(map));
                auto a=decode_nitro_bg(tvGfx,nscr,tvPal1,false),b=decode_nitro_bg(tvGfx,nscr,tvPal2,false);
                auto best=score(b)>score(a)?std::move(b):std::move(a);if(best.valid)p->newGameTvFrames.push_back(std::move(best));
            }
            // New-game/Oak backgrounds use the later character set and maps.
            // The original application swaps NCGR banks while retaining palette
            // 33, so select the matching non-empty bank per screen map rather
            // than inventing a PC-only portrait.
            auto oakPal=unpackNg(33);
            // Members 43..52 are authored UI/background screen maps sharing the
            // larger character bank at member 32. The old "pick the busiest gfx"
            // heuristic paired them with tiny members 34..42 and produced the
            // striped/giant-face corruption seen in v0.21.
            for(int map=43;map<=52;map++){
                auto im=decode_nitro_bg(unpackNg(32),unpackNg(std::size_t(map)),oakPal,true);
                if(im.valid)p->newGameOakFrames.push_back(std::move(im));
            }
            p->newGameOakCells=decode_nitro_cells(unpackNg(hg_new_game_asset::OakGfx),unpackNg(hg_new_game_asset::OakCell),unpackNg(hg_new_game_asset::OakPalette),true);
            p->newGameBoyCells=decode_nitro_cells(unpackNg(hg_new_game_asset::BoyCellGfx),unpackNg(hg_new_game_asset::BoyCell),unpackNg(hg_new_game_asset::BoyPalette),true);
            p->newGameGirlCells=decode_nitro_cells(unpackNg(hg_new_game_asset::GirlCellGfx),unpackNg(hg_new_game_asset::GirlCell),unpackNg(hg_new_game_asset::GirlPalette),true);
            p->newGameMarillCells=decode_nitro_cells(unpackNg(hg_new_game_asset::MarillGfx),unpackNg(hg_new_game_asset::MarillCell),unpackNg(hg_new_game_asset::MarillPalette),true);
            p->newGameOakChar=decode_nitro_char_sheet(unpackNg(hg_new_game_asset::OakGfx),unpackNg(hg_new_game_asset::OakPalette),true,0);
            p->newGameMarillChar=decode_nitro_char_sheet(unpackNg(hg_new_game_asset::MarillGfx),unpackNg(hg_new_game_asset::MarillPalette),true,0);
            p->newGameBoyPose=decode_nitro_char_sheet(unpackNg(hg_new_game_asset::BoyPoseGfx),unpackNg(hg_new_game_asset::BoyPalette),true,0);
            p->newGameGirlPose=decode_nitro_char_sheet(unpackNg(hg_new_game_asset::GirlPoseGfx),unpackNg(hg_new_game_asset::GirlPalette),true,0);
            p->newGameRetailAssets=!p->newGameTvFrames.empty()&&!p->newGameOakFrames.empty()&&!p->newGameOakCells.empty();
        }
        auto oak=p->newGameMessages.decode(6,"PLAYER");
        std::cout<<"New-game ROM data: menu="<<(p->mainMenuMessages.valid()?"OK":"FAIL")<<" Oak text="<<(oak.valid?"OK":"FAIL")<<" red Gyarados="<<(p->redGyarados.valid?"OK":"FAIL")
                 <<" retail intro="<<(p->newGameRetailAssets?"OK":"MISSING")<<" TV frames="<<p->newGameTvFrames.size()<<" Oak states="<<p->newGameOakFrames.size()<<" Oak cells="<<p->newGameOakCells.size()<<" Marill cells="<<p->newGameMarillCells.size()<<"\n";
    }
    {
        auto titleArc=p->assets/"a/0/4/6";
        auto pal=read_narc_member(titleArc,4);
        p->titleSun=decode_nitro_bg(read_narc_member(titleArc,34),read_narc_member(titleArc,35),pal,false);
        p->titleLogo=decode_nitro_bg(read_narc_member(titleArc,3),read_narc_member(titleArc,0),pal,true);
        p->titleFooter=decode_nitro_bg(read_narc_member(titleArc,15),read_narc_member(titleArc,17),pal,true);
        p->titleHooh=load_nsbmd_from_narc(titleArc,25);
        p->titleSparkle=load_nsbmd_from_narc(titleArc,38);
        p->titleMessages=HgMessageBank(load_hg_message_bank(p->assets,719));
        auto touch=p->titleMessages.decode(0);
        std::cout<<"Retail title assets: background="<<(p->titleSun.valid?"OK":"FAIL")<<" logo="<<(p->titleLogo.valid?"OK":"FAIL")<<" footer="<<(p->titleFooter.valid?"OK":"FAIL")
                 <<" Ho-Oh resource="<<(p->titleHooh.valid?"OK":"FAIL")<<" prompt="<<(touch.valid?touch.text:"<decode failed>")<<"\n";
    }
    {
        bool soundOk=p->sdat.load(p->assets/"data/sound/gs_sound_data.sdat");auto st=p->sdat.stats();
        std::cout<<"Native SDAT: "<<(soundOk?"OK":"FAIL")<<" files="<<st.files<<" SSEQ="<<st.sequences<<" SBNK="<<st.banks<<" SWAR="<<st.waveArchives<<" audioDevice="<<(p->audio.ready()?"READY":"UNAVAILABLE")<<"\n";
    }
    bool fullHeaders=initialize_hg_map_headers(p->assets);
    std::cout<<"MapHeader table: "<<(fullHeaders?"540/540 records recovered from decoded ARM9":"seed fallback only")<<"\n";
    auto worldCheck=validate_hg_starting_world(p->assets);
    std::cout<<"ROM overworld validation: "<<worldCheck.summary<<"\n";
    p->romWorld.setAssets(p->assets);p->romWorldReady=p->romWorld.initialize();
    if(p->romWorldReady){p->onMapEntered(true);std::cout<<"Native ROM world: "<<p->romWorld.locationName()<<" map="<<p->romWorld.mapId()<<" pos="<<p->romWorld.x()<<","<<p->romWorld.y()<<" matrix="<<(p->romWorld.header()?p->romWorld.header()->matrixId:0)<<" land="<<p->romWorld.currentLandMember()<<" visible chunks="<<p->romWorld.visibleChunks().size()<<"\n";}else std::cout<<"Native ROM world unavailable: "<<p->romWorld.error()<<"\n";
    p->playBgmSequence(SEQ_GS_OPENING_TITLE_G,0.18f);
    return true;
}
void NativeGame::update(const InputState& input,double dtSeconds){p->update(input,dtSeconds);} RenderFrame NativeGame::render() const{return p->render();} bool NativeGame::battleTurnSequenceTest(){
    p->gameState.party.clear();
    HgMon player=hg_make_mon(152,25); // Chikorita
    HgMon enemy=hg_make_mon(19,25);   // Rattata
    player.moves={98,0,0,0}; // Quick Attack: guarantees player acts first by priority.
    enemy.moves={33,0,0,0};  // Tackle.
    if(auto* md=hg_move_data(98))player.pp[0]=player.maxPp[0]=md->pp;
    if(auto* md=hg_move_data(33))enemy.pp[0]=enemy.maxPp[0]=md->pp;
    p->gameState.party.push_back(player);
    p->beginBattle(enemy,false,0);p->battle.transitionClock=Impl::kBattleTransitionSeconds;p->battle.sceneClock=2.0;p->battle.introPhase=4;
    auto* me=p->gameState.leadAlive();if(!me)return false;
    const auto playerHp0=me->hp,enemyHp0=p->battle.enemy.hp;
    p->executeBattleTurn(0);
    const bool firstOnly=p->battle.turnStep==1&&p->battle.awaiting&&p->battle.enemy.hp<enemyHp0&&me->hp==playerHp0;
    p->battle.actionClock=1.0;
    const bool queuedSecond=p->advanceBattleTurn();
    const bool secondOnly=queuedSecond&&p->battle.turnStep==2&&p->battle.awaiting&&me->hp<playerHp0;
    return firstOnly&&secondOnly;
}
RenderFrame NativeGame::battleRenderRegressionFrame(){
    p->gameState.party.clear();
    HgMon player=hg_make_mon(158,5); // Totodile, matches the reported battle case.
    HgMon enemy=hg_make_mon(16,2);   // Pidgey.
    p->gameState.party.push_back(player);
    p->beginBattle(enemy,false,0);
    p->battle.transitionClock=Impl::kBattleTransitionSeconds;
    p->battle.sceneClock=2.0;
    p->battle.introPhase=4;
    p->battle.awaiting=false;
    p->battle.message.clear();
    p->battle.choosingMove=true;
    p->battle.moveMenu=0;
    return p->render();
}
bool NativeGame::fieldCounterInteractionTest(){
    if(!p->romWorldReady)return false;
    const auto savedState=p->gameState;
    const auto savedFacing=p->facing;
    const auto savedMode=p->mode;

    auto probe=[&](int mapId,int x,int y,Dir face,int expectedObject){
        p->scriptVm.stop();p->pendingMapScripts.clear();p->clearScriptHostState();p->dialogue.clear();p->speaker.clear();p->mode=Mode::Field;
        if(!p->romWorld.loadMap(mapId,x,y))return false;
        p->onMapEntered(false);p->facing=face;
        const auto label=p->interactionLabel();
        const bool targeted=label.find("OBJECT "+std::to_string(expectedObject))!=std::string::npos;
        if(!targeted)return false;
        p->interact();
        // An actual retail interaction must leave free-roam: either the script is
        // still active, it yielded dialogue/choice, or it launched its native app.
        return p->scriptVm.active()||p->mode!=Mode::Field;
    };

    // Cherrygrove Poké Mart: player (4,6) -> behavior-0x80 counter (3,6)
    // -> clerk object 0 (2,6), facing west.
    const bool mart=probe(68,4,6,Dir::Left,0);
    // Cherrygrove Pokémon Center 1F: player (8,13) -> behavior-0x80
    // counter (8,12) -> nurse object 0 (8,11), facing north.
    const bool center=probe(69,8,13,Dir::Up,0);

    p->gameState=savedState;p->scriptVm.bindState(&p->gameState);p->scriptVm.stop();
    p->facing=savedFacing;p->mode=savedMode;
    return mart&&center;
}

bool NativeGame::battleRenderVisibilityTest(){
    auto f=battleRenderRegressionFrame();
    if(!f.hasPixels())return false;
    // Background/platform underlays must no longer be deferred vector rectangles,
    // because deferred rects are composited after raster images and hide Pokepics.
    for(auto const& r:f.rects)if(r.y<535.0f&&r.w>1000.0f&&r.h>40.0f)return false;
    auto distinct=[&](int x0,int y0,int x1,int y1){
        std::unordered_set<std::uint32_t> c;
        x0=std::clamp(x0,0,RenderFrame::PixelWidth);x1=std::clamp(x1,0,RenderFrame::PixelWidth);
        y0=std::clamp(y0,0,RenderFrame::PixelHeight);y1=std::clamp(y1,0,RenderFrame::PixelHeight);
        for(int y=y0;y<y1;y+=2)for(int x=x0;x<x1;x+=2){auto i=(std::size_t(y)*RenderFrame::PixelWidth+x)*4;std::uint32_t v=(std::uint32_t(f.rgba[i])<<16)|(std::uint32_t(f.rgba[i+1])<<8)|f.rgba[i+2];c.insert(v);}
        return c.size();
    };
    // v0.41 derives the Pokepic origins from the ROM floor objects instead of
    // the old hard-coded PC coordinates.  Probe the centered near/far regions.
    return distinct(380,215,640,415)>10 && distinct(680,135,870,300)>10;
}

bool NativeGame::wantsQuit() const{return p->quit;}
bool NativeGame::save(){return p->saveInternal();} bool NativeGame::load(){return p->loadInternal();}
void NativeGame::resetGame(){
    p->gameState=HgGameState{};p->scriptVm.bindState(&p->gameState);p->scriptVm.stop();p->pendingMapScripts.clear();p->frameScriptLatch=0;p->fieldTransition.clear();p->playerLedgeJump=false;p->debugWalkThroughWalls=false;
    if(p->romWorldReady){p->romWorld.initialize();p->onMapEntered(true);}
    else{p->mapIndex=0;p->tx=16;p->ty=11;p->rx=16;p->ry=11;p->fromX=p->toX=16;p->fromY=p->toY=11;p->moveProgress=1;}
    p->facing=Dir::Down;p->mode=Mode::Intro;p->introClock=0;p->introScene=0;p->titleClock=0;p->playSeconds=0;p->fieldAnimClock=0;p->toast.clear();p->dialogue.clear();p->currentBgm=0;p->playBgmSequence(SEQ_GS_OPENING_TITLE_G,0.18f);
}
