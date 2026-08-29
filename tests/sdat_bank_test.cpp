#include "assets/sdat.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

static void p16(std::vector<unsigned char>& b,std::size_t p,std::uint16_t v){b[p]=v&255;b[p+1]=v>>8;}
static void p32(std::vector<unsigned char>& b,std::size_t p,std::uint32_t v){for(int i=0;i<4;i++)b[p+i]=(v>>(8*i))&255;}
static void typed(std::vector<unsigned char>& b,std::size_t p,std::uint16_t type,std::uint16_t wave,std::uint16_t slot,std::uint8_t root){p16(b,p,type);p16(b,p+2,wave);p16(b,p+4,slot);b[p+6]=root;b[p+7]=127;b[p+8]=127;b[p+9]=127;b[p+10]=127;b[p+11]=64;}
static void simple(std::vector<unsigned char>& b,std::size_t p,std::uint16_t wave,std::uint16_t slot,std::uint8_t root){p16(b,p,wave);p16(b,p+2,slot);b[p+4]=root;b[p+5]=127;b[p+6]=127;b[p+7]=127;b[p+8]=127;b[p+9]=64;}

int main(){
    std::vector<unsigned char> b(0xC0,0);b[0]='S';b[1]='B';b[2]='N';b[3]='K';p32(b,0x38,3);
    // Program 0: range instrument C4..C#4. Instrument table format is <BHx>.
    b[0x3C]=16;p16(b,0x3D,0x60);b[0x3F]=0xEE; // pad must NOT become offset high byte
    b[0x60]=60;b[0x61]=61;typed(b,0x62,1,7,2,60);typed(b,0x6E,1,8,3,61);
    auto c4=hg_resolve_sbnk_note(b,0,60),cs4=hg_resolve_sbnk_note(b,0,61),outside=hg_resolve_sbnk_note(b,0,62);
    assert(c4.valid&&c4.waveId==7&&c4.waveArchiveSlot==2&&c4.rootKey==60);
    assert(cs4.valid&&cs4.waveId==8&&cs4.waveArchiveSlot==3&&cs4.rootKey==61);assert(!outside.valid);

    // Program 1: regional instrument. First region ending at pitch 0 is legal.
    b[0x40]=17;p16(b,0x41,0x88);b[0x88]=0;b[0x89]=72;b[0x8A]=127;
    typed(b,0x90,1,10,0,48);typed(b,0x9C,1,11,1,60);typed(b,0xA8,1,12,2,72);
    auto n0=hg_resolve_sbnk_note(b,1,0),n60=hg_resolve_sbnk_note(b,1,60),n100=hg_resolve_sbnk_note(b,1,100);
    assert(n0.valid&&n0.waveId==10&&n0.rootKey==48);assert(n60.valid&&n60.waveId==11&&n60.rootKey==60);assert(n100.valid&&n100.waveId==12&&n100.rootKey==72);

    // Program 2: ordinary 10-byte note definition.
    b[0x44]=1;p16(b,0x45,0xB4);simple(b,0xB4,22,3,65);auto s=hg_resolve_sbnk_note(b,2,70);
    assert(s.valid&&s.type==1&&s.waveId==22&&s.waveArchiveSlot==3&&s.rootKey==65);
    std::cout<<"sdat_bank_test: PASS\n";
}
