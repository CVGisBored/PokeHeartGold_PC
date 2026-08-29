#include "assets/sdat.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#else
#include <dlfcn.h>
#endif
#include <fstream>
#include <unordered_map>

namespace{
std::uint16_t u16(const std::vector<unsigned char>&b,std::size_t p){return p+2<=b.size()?std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8):0;}
std::uint32_t u32(const std::vector<unsigned char>&b,std::size_t p){return p+4<=b.size()?std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24):0;}
bool magic(const std::vector<unsigned char>&b,std::size_t p,const char*s){return p+4<=b.size()&&std::memcmp(b.data()+p,s,4)==0;}
std::vector<std::int16_t> decodeWave(const unsigned char* p,std::size_t n,unsigned type){
    std::vector<std::int16_t> out;
    if(type==0){out.reserve(n);for(std::size_t i=0;i<n;i++)out.push_back(std::int16_t(std::int8_t(p[i]))<<8);return out;}
    if(type==1){out.reserve(n/2);for(std::size_t i=0;i+1<n;i+=2)out.push_back(std::int16_t(std::uint16_t(p[i])|(std::uint16_t(p[i+1])<<8)));return out;}
    if(type!=2||n<4)return out;
    static const int indexTable[8]={-1,-1,-1,-1,2,4,6,8};
    static const int stepTable[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
    int sample=std::int16_t(std::uint16_t(p[0])|(std::uint16_t(p[1])<<8));int index=std::clamp(int(p[2]),0,88);out.push_back(std::int16_t(sample));
    for(std::size_t i=4;i<n;i++)for(int h=0;h<2;h++){int code=h?(p[i]>>4):(p[i]&15),step=stepTable[index];int diff=step>>3;if(code&1)diff+=step>>2;if(code&2)diff+=step>>1;if(code&4)diff+=step;if(code&8)sample-=diff;else sample+=diff;sample=std::clamp(sample,-32768,32767);index=std::clamp(index+indexTable[code&7],0,88);out.push_back(std::int16_t(sample));}
    return out;
}
struct SdlAudioSpec{int freq;std::uint16_t format;std::uint8_t channels;std::uint8_t silence;std::uint16_t samples;std::uint16_t padding;std::uint32_t size;void(*callback)(void*,std::uint8_t*,int);void*userdata;};
}

HgSbnkNoteRef hg_resolve_sbnk_note(const std::vector<unsigned char>& bankFile,std::size_t program,int note){
    HgSbnkNoteRef out;
    if(!magic(bankFile,0,"SBNK")||bankFile.size()<0x3c)return out;
    const auto count=u32(bankFile,0x38);
    const auto rec=std::size_t(0x3c)+program*4u;
    if(program>=count||rec+4>bankFile.size())return out;
    const std::uint16_t kind=bankFile[rec];
    // SBNK instrument table entries are {type:u8, offset:u16, pad:u8}.
    // v0.15 incorrectly folded the pad byte into a 24-bit offset.
    const std::size_t off=u16(bankFile,rec+1);
    if(!off||off>=bankFile.size())return out;
    std::size_t def=off;
    std::uint16_t noteType=kind;
    if(kind==16){
        if(off+2>bankFile.size())return out;
        const int lo=bankFile[off],hi=bankFile[off+1];
        if(lo>hi||note<lo||note>hi)return out;
        const int key=note;
        // Range/drum entries include a 16-bit type prefix before the ordinary
        // 10-byte note definition: 12 bytes per key.
        def=off+2+std::size_t(key-lo)*12u;
        if(def+12>bankFile.size())return out;
        noteType=u16(bankFile,def);
        out.waveId=u16(bankFile,def+2);
        out.waveArchiveSlot=u16(bankFile,def+4);
        out.rootKey=bankFile[def+6];
    } else if(kind==17){
        if(off+8>bankFile.size())return out;
        int regions=0;
        // A regional instrument may legitimately have a first region ending at
        // pitch 0. A zero terminates the list only after at least one region.
        for(int i=0;i<8;i++){if(i>0&&bankFile[off+std::size_t(i)]==0)break;regions++;}
        if(regions==0)return out;
        int region=regions-1;
        for(int i=0;i<regions;i++)if(note<=bankFile[off+std::size_t(i)]){region=i;break;}
        def=off+8+std::size_t(region)*12u;
        if(def+12>bankFile.size())return out;
        noteType=u16(bankFile,def);
        out.waveId=u16(bankFile,def+2);
        out.waveArchiveSlot=u16(bankFile,def+4);
        out.rootKey=bankFile[def+6];
    } else if(kind>=1&&kind<16){
        if(def+10>bankFile.size())return out;
        out.waveId=u16(bankFile,def);
        out.waveArchiveSlot=u16(bankFile,def+2);
        out.rootKey=bankFile[def+4];
    } else return out;
    out.type=noteType;
    out.valid=(noteType>=1&&noteType<16);
    return out;
}

bool HgSdat::load(const std::filesystem::path& path){
    stats_={};files_.clear();seqEntry_.clear();bankEntry_.clear();swarEntry_.clear();std::ifstream f(path,std::ios::binary);if(!f){stats_.error="SDAT missing";return false;}data_.assign(std::istreambuf_iterator<char>(f),{});if(!magic(data_,0,"SDAT")||data_.size()<0x40){stats_.error="invalid SDAT";return false;}
    infoOff_=u32(data_,0x18);fatOff_=u32(data_,0x20);if(!magic(data_,infoOff_,"INFO")||!magic(data_,fatOff_,"FAT ")){stats_.error="missing INFO/FAT";return false;}
    auto count=u32(data_,fatOff_+8);if(fatOff_+12+std::size_t(count)*16>data_.size()){stats_.error="FAT truncated";return false;}files_.reserve(count);
    for(std::size_t i=0;i<count;i++){auto p=fatOff_+12+i*16;auto off=u32(data_,p),sz=u32(data_,p+4);if(std::size_t(off)+sz>data_.size()){stats_.error="FAT file outside SDAT";return false;}files_.push_back({off,sz});if(magic(data_,off,"SSEQ"))stats_.sequences++;else if(magic(data_,off,"SBNK"))stats_.banks++;else if(magic(data_,off,"SWAR"))stats_.waveArchives++;}
    seqEntry_=infoEntries(0);bankEntry_=infoEntries(2);swarEntry_=infoEntries(3);stats_.files=files_.size();stats_.sequenceInfoCount=seqEntry_.size();stats_.bankInfoCount=bankEntry_.size();stats_.waveInfoCount=swarEntry_.size();stats_.valid=true;return true;
}
std::vector<std::uint32_t> HgSdat::infoEntries(int part) const{std::vector<std::uint32_t> out;if(part<0||part>7||infoOff_+40>data_.size())return out;auto rel=u32(data_,infoOff_+8+part*4);auto p=std::size_t(infoOff_)+rel;if(!rel||p+4>data_.size())return out;auto n=u32(data_,p);if(p+4+std::size_t(n)*4>data_.size())return out;out.reserve(n);for(std::size_t i=0;i<n;i++){auto e=u32(data_,p+4+i*4);out.push_back(e?infoOff_+e:0);}return out;}
std::vector<unsigned char> HgSdat::fileBytes(std::size_t id) const{if(id>=files_.size())return{};auto [o,n]=files_[id];return {data_.begin()+o,data_.begin()+o+n};}
HgSdatSequenceInfo HgSdat::sequence(std::size_t id) const{HgSdatSequenceInfo o;if(id>=seqEntry_.size()||!seqEntry_[id])return o;auto p=seqEntry_[id];if(p+10>data_.size())return o;o.fileId=u16(data_,p);o.bankId=u16(data_,p+4);o.volume=data_[p+6];o.playerId=data_[p+9];o.valid=o.fileId<files_.size()&&o.bankId<bankEntry_.size();return o;}
HgPcmWave HgSdat::wave(std::size_t archiveId,std::size_t waveId) const{
    HgPcmWave o;
    if(archiveId>=swarEntry_.size()||!swarEntry_[archiveId]){o.error="SWAR id invalid";return o;}
    auto fileId=u16(data_,swarEntry_[archiveId]);auto sw=fileBytes(fileId);
    if(!magic(sw,0,"SWAR")||sw.size()<0x40){o.error="SWAR file invalid";return o;}
    auto count=u32(sw,0x38);if(waveId>=count||0x3c+count*4>sw.size()){o.error="SWAV index invalid";return o;}
    auto off=u32(sw,0x3c+waveId*4);auto end=waveId+1<count?u32(sw,0x3c+(waveId+1)*4):u32(sw,8);
    if(off+12>sw.size()||end>sw.size()||end<=off+12){o.error="SWAV bounds invalid";return o;}
    unsigned type=sw[off];o.loop=sw[off+1]!=0;o.sampleRate=u16(sw,off+2);
    const std::uint16_t loopStartWords=u16(sw,off+6);
    const std::uint32_t loopLengthWords=u32(sw,off+8);
    o.samples=decodeWave(sw.data()+off+12,end-(off+12),type);
    // SWAV stores loop positions in 32-bit words relative to the encoded sample
    // payload. Convert those offsets to decoded PCM sample indices.
    if(!o.samples.empty()&&o.loop){
        std::size_t start=0,length=0;
        if(type==0){start=std::size_t(loopStartWords)*4u;length=std::size_t(loopLengthWords)*4u;}
        else if(type==1){start=std::size_t(loopStartWords)*2u;length=std::size_t(loopLengthWords)*2u;}
        else if(type==2){
            const std::size_t startBytes=std::size_t(loopStartWords)*4u;
            start=startBytes<=4?0u:1u+(startBytes-4u)*2u;
            length=std::size_t(loopLengthWords)*8u;
        }
        o.loopStart=std::min(start,o.samples.size()-1);
        o.loopEnd=std::min(o.samples.size(),o.loopStart+std::max<std::size_t>(1,length));
        if(o.loopEnd<=o.loopStart+1){o.loopStart=0;o.loopEnd=o.samples.size();}
    } else {o.loopStart=0;o.loopEnd=o.samples.size();}
    o.valid=o.sampleRate>0&&!o.samples.empty();if(!o.valid)o.error="unsupported/empty SWAV";return o;
}
HgPcmWave HgSdat::auditionSequenceSample(std::size_t seqId) const{auto sq=sequence(seqId);if(!sq.valid)return{};if(sq.bankId>=bankEntry_.size()||!bankEntry_[sq.bankId])return{};auto p=bankEntry_[sq.bankId];if(p+12>data_.size())return{};for(int i=0;i<4;i++){auto id=std::int16_t(u16(data_,p+4+i*2));if(id>=0){auto w=wave(std::size_t(id),0);if(w.valid)return w;}}return{};}

HgPcmWave HgSdat::renderSequence(std::size_t seqId,double seconds) const{
    HgPcmWave out;auto info=sequence(seqId);if(!info.valid){out.error="SSEQ info invalid";return out;}auto f=fileBytes(info.fileId);
    if(!magic(f,0,"SSEQ")||f.size()<0x1d){out.error="SSEQ file invalid";return out;}const std::size_t dataOff=u32(f,0x18);if(dataOff>=f.size()){out.error="SSEQ DATA offset invalid";return out;}
    const auto fallback=auditionSequenceSample(seqId);const int rate=22050;const std::size_t total=std::size_t(std::max(0.25,seconds)*rate);std::vector<float> mix(total,0.0f);
    struct ProgramWave { HgPcmWave wave; int root=60; int synthType=0; bool tried=false; };
    // Split/range programs can select a different note definition for every key.
    // Keep the full packed SSEQ instrument number: high bits select SBNK programs
    // above 127 and must not be thrown away.
    std::unordered_map<std::uint64_t,ProgramWave> programWaves;
    auto resolveProgram=[&](int program,int note)->ProgramWave&{
        program=std::max(0,program);note=std::clamp(note,0,127);
        const std::uint64_t cacheKey=(std::uint64_t(std::uint32_t(program))<<8)|std::uint64_t(unsigned(note));
        auto [it,inserted]=programWaves.try_emplace(cacheKey);auto& pw=it->second;
        (void)inserted;
        if(pw.tried)return pw;
        pw.tried=true;pw.wave=fallback;pw.root=60;
        if(info.bankId>=bankEntry_.size()||!bankEntry_[info.bankId])return pw;
        auto bi=bankEntry_[info.bankId];auto bankFile=fileBytes(u16(data_,bi));
        auto ref=hg_resolve_sbnk_note(bankFile,std::size_t(program),note);
        if(!ref.valid)return pw;
        pw.root=ref.rootKey;
        if(ref.type==2||ref.type==3){pw.wave={};pw.synthType=int(ref.type);return pw;}
        if(ref.waveArchiveSlot<4&&bi+4+std::size_t(ref.waveArchiveSlot)*2+2<=data_.size()){
            auto arc=std::int16_t(u16(data_,bi+4+std::size_t(ref.waveArchiveSlot)*2));
            if(arc>=0){auto real=wave(std::size_t(arc),ref.waveId);if(real.valid){pw.wave=std::move(real);pw.synthType=0;}}
        }
        return pw;
    };
    auto varlen=[&](std::size_t& p)->std::uint32_t{std::uint32_t v=0;for(int n=0;n<4&&p<f.size();n++){auto b=f[p++];v=(v<<7)|(b&0x7f);if(!(b&0x80))break;}return v;};
    auto noteSample=[&](const HgPcmWave& wave,int synthType,int root,int note,double bendSemitones,int velocity,double start,double dur,float gain){
        std::size_t a=std::min(total,std::size_t(std::max(0.0,start)*rate)),b=std::min(total,std::size_t(std::max(0.0,start+dur)*rate));if(b<=a)return;
        double ratio=std::pow(2.0,(double(note)-double(root)+bendSemitones)/12.0);double phase=0.0;
        for(std::size_t i=a;i<b;i++){float env=1.0f;double t=double(i-a)/rate;if(t<0.008)env=float(t/0.008);double tail=double(b-i)/rate;if(tail<0.025)env*=float(tail/0.025);float v=0;
            if(wave.valid&&!wave.samples.empty()){
                const std::size_t loopEnd=wave.loopEnd?std::min(wave.loopEnd,wave.samples.size()):wave.samples.size();
                if(phase<double(wave.samples.size())){
                    auto j0=std::min(std::size_t(phase),wave.samples.size()-1);auto j1=std::min(j0+1,wave.samples.size()-1);
                    double frac=phase-double(j0);v=float((double(wave.samples[j0])+(double(wave.samples[j1])-double(wave.samples[j0]))*frac)/32768.0);
                }
                phase+=ratio*double(wave.sampleRate)/rate;
                if(wave.loop&&loopEnd>wave.loopStart+1&&phase>=double(loopEnd)){
                    const double span=double(loopEnd-wave.loopStart);
                    phase=double(wave.loopStart)+std::fmod(std::max(0.0,phase-double(wave.loopStart)),span);
                }
            }
            else{
                const double hz=440.0*std::pow(2.0,(double(note)-69.0+bendSemitones)/12.0);
                if(synthType==2)v=(std::fmod(phase,2.0*3.14159265358979323846)<3.14159265358979323846)?0.55f:-0.55f;
                else if(synthType==3){std::uint32_t n=std::uint32_t((i-a)*1103515245u+unsigned(note)*12345u);v=float((int((n>>16)&0xffff)-32768)/32768.0)*0.42f;}
                else v=float(std::sin(phase));
                phase+=2.0*3.14159265358979323846*hz/rate;
            }
            mix[i]+=v*env*gain*(velocity/127.0f);
        }
    };
    std::vector<std::size_t> starts{dataOff};
    // Track declarations live in the sequence stream as 0x93 + track + 24-bit offset.
    for(std::size_t p=dataOff,guard=0;p+5<=f.size()&&guard<96;guard++){
        auto op=f[p];if(op==0xfe){p+=3;continue;}if(op==0x93){std::size_t q=std::size_t(f[p+2])|(std::size_t(f[p+3])<<8)|(std::size_t(f[p+4])<<16);if(dataOff+q<f.size())starts.push_back(dataOff+q);p+=5;continue;}break;
    }
    std::sort(starts.begin(),starts.end());starts.erase(std::unique(starts.begin(),starts.end()),starts.end());if(starts.size()>16)starts.resize(16);

    // Nitro SSEQ has one sequence clock. Tempo commands alter that shared clock;
    // they are *not* private to the track containing the E1 event. Older native
    // builds converted each track to seconds while parsing it and therefore left
    // tracks without their own tempo event running at the 120 BPM default. That
    // made instrumental parts drift apart even though their SSEQ ticks matched.
    struct RawNote {
        std::uint64_t startTick=0,durationTicks=0;
        int note=60,velocity=100,instrument=0;
        double bendSemitones=0.0;
        float gain=1.0f;
    };
    struct RawTempo { std::uint64_t tick=0; double bpm=120.0; std::uint64_t order=0; };
    std::vector<RawNote> rawNotes;
    std::vector<RawTempo> rawTempos{{0,120.0,0}};
    std::uint64_t tempoOrder=1;
    // 400 BPM is the upper clamp used below: 400 * 48 / 60 = 320 ticks/sec.
    // Parse enough shared-timeline ticks to cover the requested render window
    // even if the song spends the entire window at that maximum tempo.
    const std::uint64_t maxTicks=std::uint64_t(std::ceil(std::max(0.25,seconds)*320.0))+4096u;
    for(std::size_t ti=0;ti<starts.size();ti++){
        std::size_t pc=starts[ti];std::uint64_t tick=0;int transpose=0;bool noteWait=true;
        const float seqVolume=float(info.volume)/127.0f;
        const float baseGain=(seqVolume*seqVolume)/std::sqrt(float(starts.size()));
        float vol=baseGain,expression=1.0f;int pitchBend=0,bendRange=2;
        std::vector<std::size_t> calls;std::vector<std::pair<std::size_t,int>> loops;int instrument=0;
        for(int guard=0;guard<400000&&pc<f.size()&&tick<maxTicks;guard++){
            auto op=f[pc++];
            if(op<=0x7f){
                if(pc>=f.size())break;
                int vel=f[pc++]&0x7f;auto ticks=std::uint64_t(varlen(pc));
                const double bend=(double(pitchBend)/127.0)*double(bendRange);
                rawNotes.push_back({tick,std::max<std::uint64_t>(1,ticks),std::clamp(int(op)+transpose,0,127),vel,instrument,bend,vol*expression});
                if(noteWait)tick+=ticks;
                continue;
            }
            if(op==0x80){tick+=std::uint64_t(varlen(pc));continue;}
            if(op==0x81){instrument=int(varlen(pc));continue;}
            if(op==0x93){pc+=std::min<std::size_t>(4,f.size()-pc);continue;}
            if(op==0x94&&pc+3<=f.size()){std::size_t q=std::size_t(f[pc])|(std::size_t(f[pc+1])<<8)|(std::size_t(f[pc+2])<<16);pc=dataOff+q;continue;}
            if(op==0x95&&pc+3<=f.size()){std::size_t q=std::size_t(f[pc])|(std::size_t(f[pc+1])<<8)|(std::size_t(f[pc+2])<<16);pc+=3;calls.push_back(pc);pc=dataOff+q;continue;}
            if(op==0xfd){if(calls.empty())break;pc=calls.back();calls.pop_back();continue;}
            if(op==0xff)break;
            if(op==0xfe){pc+=std::min<std::size_t>(2,f.size()-pc);continue;}
            if(op==0xe1&&pc+2<=f.size()){double bpm=std::clamp<double>(u16(f,pc),20,400);pc+=2;rawTempos.push_back({tick,bpm,tempoOrder++});continue;}
            if(op==0xc1&&pc<f.size()){float q=std::max(0.0f,float(f[pc++])/127.0f);vol=baseGain*q*q;continue;}
            if(op==0xc2&&pc<f.size()){pc++;continue;}
            if(op==0xc3&&pc<f.size()){transpose=std::int8_t(f[pc++]);continue;}
            if(op==0xc4&&pc<f.size()){pitchBend=std::int8_t(f[pc++]);continue;}
            if(op==0xc5&&pc<f.size()){bendRange=std::max(0,int(f[pc++]));continue;}
            // Expression is a per-track multiplier. It does not affect timing,
            // but retaining it here avoids flattening the retail arrangement.
            if(op==0xd5&&pc<f.size()){float q=std::max(0.0f,float(f[pc++])/127.0f);expression=q*q;continue;}
            if(op==0xc7&&pc<f.size()){noteWait=f[pc++]!=0;continue;}
            if(op==0xd4&&pc<f.size()){int count=f[pc++];loops.push_back({pc,count?count:255});continue;}
            if(op==0xfc){if(!loops.empty()){auto& L=loops.back();if(--L.second>0)pc=L.first;else loops.pop_back();}continue;}
            // Common one-byte controls C0..D6; E0/E3 carry 16-bit values.
            if((op>=0xc0&&op<=0xd6)){if(pc<f.size())pc++;continue;}
            if(op==0xe0||op==0xe3){pc+=std::min<std::size_t>(2,f.size()-pc);continue;}
            if(op==0xa2)continue;
            if(op>=0xb0&&op<=0xbd){pc+=std::min<std::size_t>(3,f.size()-pc);continue;}
            // Random/from-variable commands have variable layouts; ending this track is safer than misparsing.
            break;
        }
    }

    std::stable_sort(rawTempos.begin(),rawTempos.end(),[](auto const& a,auto const& b){return a.tick<b.tick;});
    // If more than one tempo event lands on the same sequence tick, the later
    // command wins. Keep only the resulting state at that tick.
    std::vector<RawTempo> tempos;
    for(auto const& t:rawTempos){
        if(!tempos.empty()&&tempos.back().tick==t.tick)tempos.back()=t;
        else tempos.push_back(t);
    }
    struct TempoPoint { std::uint64_t tick=0; double bpm=120.0,secondsAtTick=0.0; };
    std::vector<TempoPoint> tempoMap;tempoMap.reserve(tempos.size());
    double elapsed=0.0;std::uint64_t prevTick=0;double prevBpm=120.0;
    for(auto const& t:tempos){
        if(t.tick>prevTick)elapsed+=double(t.tick-prevTick)*60.0/(prevBpm*48.0);
        tempoMap.push_back({t.tick,t.bpm,elapsed});prevTick=t.tick;prevBpm=t.bpm;
    }
    auto tickToSeconds=[&](std::uint64_t t){
        auto it=std::upper_bound(tempoMap.begin(),tempoMap.end(),t,[](std::uint64_t tick,TempoPoint const& p){return tick<p.tick;});
        if(it==tempoMap.begin())return double(t)*60.0/(120.0*48.0);
        --it;return it->secondsAtTick+double(t-it->tick)*60.0/(it->bpm*48.0);
    };
    for(auto const& n:rawNotes){
        const double start=tickToSeconds(n.startTick);if(start>=seconds)continue;
        const double end=tickToSeconds(n.startTick+n.durationTicks);
        const double dur=std::max(0.015,end-start);
        auto& pw=resolveProgram(n.instrument,n.note);
        noteSample(pw.wave,pw.synthType,pw.root,n.note,n.bendSemitones,n.velocity,start,dur,n.gain);
    }
    float peak=0;for(float v:mix)peak=std::max(peak,std::abs(v));float scale=peak>0.96f?0.96f/peak:1.0f;out.samples.resize(total);for(std::size_t i=0;i<total;i++)out.samples[i]=std::int16_t(std::clamp<int>(int(std::lround(mix[i]*scale*32767.0f)),-32768,32767));out.sampleRate=rate;out.loop=true;out.valid=!out.samples.empty();return out;
}


NativeAudio::NativeAudio(){
#ifdef _WIN32
    WAVEFORMATEX fmt{};fmt.wFormatTag=WAVE_FORMAT_PCM;fmt.nChannels=1;fmt.nSamplesPerSec=44100;fmt.wBitsPerSample=16;fmt.nBlockAlign=2;fmt.nAvgBytesPerSec=88200;
    HWAVEOUT b=nullptr,s=nullptr;
    if(waveOutOpen(&b,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR)bgmWave_=b;
    if(waveOutOpen(&s,WAVE_MAPPER,&fmt,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR)sfxWave_=s;
#else
    lib_=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_LOCAL);if(!lib_)return;
    initSub_=reinterpret_cast<decltype(initSub_)>(dlsym(lib_,"SDL_InitSubSystem"));quitSub_=reinterpret_cast<decltype(quitSub_)>(dlsym(lib_,"SDL_QuitSubSystem"));openDev_=reinterpret_cast<decltype(openDev_)>(dlsym(lib_,"SDL_OpenAudioDevice"));queue_=reinterpret_cast<decltype(queue_)>(dlsym(lib_,"SDL_QueueAudio"));queuedSize_=reinterpret_cast<decltype(queuedSize_)>(dlsym(lib_,"SDL_GetQueuedAudioSize"));pause_=reinterpret_cast<decltype(pause_)>(dlsym(lib_,"SDL_PauseAudioDevice"));clear_=reinterpret_cast<decltype(clear_)>(dlsym(lib_,"SDL_ClearQueuedAudio"));close_=reinterpret_cast<decltype(close_)>(dlsym(lib_,"SDL_CloseAudioDevice"));
    if(!initSub_||!openDev_||!queue_||!queuedSize_||!pause_||!clear_||!close_||initSub_(0x10)!=0)return;
    SdlAudioSpec want{};want.freq=44100;want.format=0x8010;want.channels=1;want.samples=1024;
    bgmDevice_=openDev_(nullptr,0,&want,nullptr,0);if(bgmDevice_)pause_(bgmDevice_,0);
    sfxDevice_=openDev_(nullptr,0,&want,nullptr,0);if(sfxDevice_)pause_(sfxDevice_,0);
#endif
}
NativeAudio::~NativeAudio(){
#ifdef _WIN32
    stop();if(bgmWave_)waveOutClose(static_cast<HWAVEOUT>(bgmWave_));if(sfxWave_)waveOutClose(static_cast<HWAVEOUT>(sfxWave_));
#else
    if(sfxDevice_&&close_)close_(sfxDevice_);
    if(bgmDevice_&&close_)close_(bgmDevice_);
    if(quitSub_)quitSub_(0x10);
    if(lib_)dlclose(lib_);
#endif
}
std::vector<std::int16_t> NativeAudio::convert(const HgPcmWave& w,float volume) const{
    std::vector<std::int16_t> pcm;if(!w.valid||w.samples.empty()||w.sampleRate<=0)return pcm;
    double ratio=44100.0/double(w.sampleRate);std::size_t n=std::max<std::size_t>(1,std::size_t(std::llround(w.samples.size()*ratio)));pcm.resize(n);
    const float gain=std::clamp(volume,0.0f,1.0f);
    for(std::size_t i=0;i<n;i++){double src=double(i)/ratio;std::size_t j0=std::min<std::size_t>(w.samples.size()-1,std::size_t(src));std::size_t j1=std::min<std::size_t>(w.samples.size()-1,j0+1);double frac=src-double(j0);double sample=double(w.samples[j0])+(double(w.samples[j1])-double(w.samples[j0]))*frac;int v=int(std::lround(sample*gain));pcm[i]=std::int16_t(std::clamp(v,-32768,32767));}
    return pcm;
}
bool NativeAudio::playBgm(const HgPcmWave& w,float volume,bool loop){
#ifdef _WIN32
    if(!bgmWave_||!w.valid||w.samples.empty())return false;
    stopBgm();
    bgmPcm_=convert(w,volume);
    if(bgmPcm_.empty())return false;
    auto* h=new WAVEHDR{};
    h->lpData=reinterpret_cast<LPSTR>(bgmPcm_.data());
    h->dwBufferLength=DWORD(bgmPcm_.size()*sizeof(std::int16_t));
    if(loop){h->dwFlags=WHDR_BEGINLOOP|WHDR_ENDLOOP;h->dwLoops=0xffffffffu;}
    if(waveOutPrepareHeader(static_cast<HWAVEOUT>(bgmWave_),h,sizeof(*h))!=MMSYSERR_NOERROR){delete h;return false;}
    bgmHeader_=h;bgmLoop_=loop;bgmPaused_=false;
    return waveOutWrite(static_cast<HWAVEOUT>(bgmWave_),h,sizeof(*h))==MMSYSERR_NOERROR;
#else
    if(!bgmDevice_||!w.valid||w.samples.empty())return false;
    auto pcm=convert(w,volume);if(pcm.empty())return false;
    clear_(bgmDevice_);bgmPcm_=std::move(pcm);bgmLoop_=loop;bgmPaused_=false;
    // Keep only one decoded loop buffered initially. update() refills just before
    // it drains, which prevents stale copies of the previous route/scene music
    // from surviving a transition in the SDL queue.
    const bool ok=queue_(bgmDevice_,bgmPcm_.data(),std::uint32_t(bgmPcm_.size()*2))==0;
    if(ok&&pause_)pause_(bgmDevice_,0);
    return ok;
#endif
}
bool NativeAudio::playSfx(const HgPcmWave& w,float volume){
#ifdef _WIN32
    if(!sfxWave_||!w.valid||w.samples.empty())return false;
    stopSfx();
    sfxPcm_=convert(w,volume);
    if(sfxPcm_.empty())return false;
    auto* h=new WAVEHDR{};
    h->lpData=reinterpret_cast<LPSTR>(sfxPcm_.data());
    h->dwBufferLength=DWORD(sfxPcm_.size()*sizeof(std::int16_t));
    if(waveOutPrepareHeader(static_cast<HWAVEOUT>(sfxWave_),h,sizeof(*h))!=MMSYSERR_NOERROR){delete h;return false;}
    sfxHeader_=h;
    return waveOutWrite(static_cast<HWAVEOUT>(sfxWave_),h,sizeof(*h))==MMSYSERR_NOERROR;
#else
    if(!sfxDevice_||!w.valid||w.samples.empty())return false;
    auto pcm=convert(w,volume);if(pcm.empty())return false;
    // NativeAudio exposes one SFX voice. Match the Windows waveOut backend and
    // replace the current effect instead of accumulating duplicate script events.
    clear_(sfxDevice_);
    return queue_(sfxDevice_,pcm.data(),std::uint32_t(pcm.size()*2))==0;
#endif
}
void NativeAudio::pauseBgm(){
    if(bgmPaused_)return;
#ifdef _WIN32
    if(bgmWave_&&bgmHeader_)waveOutPause(static_cast<HWAVEOUT>(bgmWave_));
#else
    if(bgmDevice_&&pause_)pause_(bgmDevice_,1);
#endif
    bgmPaused_=true;
}
void NativeAudio::resumeBgm(){
    if(!bgmPaused_)return;
#ifdef _WIN32
    if(bgmWave_&&bgmHeader_)waveOutRestart(static_cast<HWAVEOUT>(bgmWave_));
#else
    if(bgmDevice_&&pause_)pause_(bgmDevice_,0);
#endif
    bgmPaused_=false;
}
void NativeAudio::update(){
#ifdef _WIN32
    if(sfxHeader_){auto* h=static_cast<WAVEHDR*>(sfxHeader_);if(h->dwFlags&WHDR_DONE){waveOutUnprepareHeader(static_cast<HWAVEOUT>(sfxWave_),h,sizeof(*h));delete h;sfxHeader_=nullptr;sfxPcm_.clear();}}
    if(bgmHeader_&&!bgmLoop_){auto* h=static_cast<WAVEHDR*>(bgmHeader_);if(h->dwFlags&WHDR_DONE){waveOutUnprepareHeader(static_cast<HWAVEOUT>(bgmWave_),h,sizeof(*h));delete h;bgmHeader_=nullptr;bgmPcm_.clear();}}
#else
    if(!bgmDevice_||!bgmLoop_||bgmPcm_.empty()||bgmPaused_)return;
    const std::uint32_t bytes=std::uint32_t(bgmPcm_.size()*2);
    // One queued copy plus the currently playing portion is enough to loop
    // continuously and bounds transition latency/backlog to one stream buffer.
    if(queuedSize_(bgmDevice_)<bytes/2u)queue_(bgmDevice_,bgmPcm_.data(),bytes);
#endif
}
void NativeAudio::stopBgm(){
#ifdef _WIN32
    if(bgmWave_)waveOutReset(static_cast<HWAVEOUT>(bgmWave_));
    if(bgmHeader_){
        auto* h=static_cast<WAVEHDR*>(bgmHeader_);
        waveOutUnprepareHeader(static_cast<HWAVEOUT>(bgmWave_),h,sizeof(*h));
        delete h;bgmHeader_=nullptr;
    }
    bgmPcm_.clear();bgmLoop_=false;bgmPaused_=false;
#else
    if(bgmDevice_&&clear_)clear_(bgmDevice_);
    bgmPcm_.clear();bgmLoop_=false;bgmPaused_=false;
#endif
}
void NativeAudio::stopSfx(){
#ifdef _WIN32
    if(sfxWave_)waveOutReset(static_cast<HWAVEOUT>(sfxWave_));
    if(sfxHeader_){
        auto* h=static_cast<WAVEHDR*>(sfxHeader_);
        waveOutUnprepareHeader(static_cast<HWAVEOUT>(sfxWave_),h,sizeof(*h));
        delete h;sfxHeader_=nullptr;
    }
    sfxPcm_.clear();
#else
    if(sfxDevice_&&clear_)clear_(sfxDevice_);
#endif
}
void NativeAudio::stop(){stopBgm();stopSfx();}
