#include "assets/overworld_data.hpp"
#include "assets/nsbmd.hpp"
#include "assets/narc.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
std::uint16_t u16(const std::vector<unsigned char>& b,std::size_t p){if(p+2>b.size())return 0;return std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8);}
std::int16_t s16(const std::vector<unsigned char>& b,std::size_t p){return static_cast<std::int16_t>(u16(b,p));}
std::uint32_t u32(const std::vector<unsigned char>& b,std::size_t p){if(p+4>b.size())return 0;return std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);}
std::int32_t s32(const std::vector<unsigned char>& b,std::size_t p){return static_cast<std::int32_t>(u32(b,p));}

// A small named seed set is retained for human-readable starter-area labels and
// as an anchor for locating the complete original MapHeader table in decoded ARM9.
HgMapHeader seed(int id,const char* name,std::uint8_t area,std::uint16_t matrix,std::uint16_t scripts,
                 std::uint16_t scriptHeader,std::uint16_t msg,std::uint16_t events,std::uint8_t mapType,std::uint8_t camera){
    HgMapHeader h;h.mapId=id;h.name=name;h.areaDataBank=area;h.moveModelBank=15;h.matrixId=matrix;
    h.scriptsBank=scripts;h.scriptHeaderBank=scriptHeader;h.msgBank=msg;h.eventsBank=events;h.mapType=mapType;h.cameraType=camera;return h;
}
const std::vector<HgMapHeader> kSeeds={
    seed(33,"ROUTE 29",2,0,225,470,373,30,2,0),
    seed(60,"NEW BARK TOWN",2,0,842,615,542,57,1,0),
    seed(61,"ELM'S LAB 1F",25,100,843,616,543,58,4,4),
    seed(62,"ELM'S LAB 2F",25,101,844,617,544,59,4,4),
    seed(63,"PLAYER HOUSE 1F",25,71,845,618,545,60,4,4),
    seed(64,"PLAYER HOUSE 2F",25,72,846,619,546,61,4,4),
    seed(65,"NEW BARK SOUTHWEST HOUSE",25,66,847,620,547,62,4,4),
    seed(66,"RIVAL HOUSE 1F",25,71,848,621,548,63,4,4),
    seed(67,"CHERRYGROVE CITY",2,0,850,623,550,64,1,0),
    seed(68,"CHERRYGROVE POKEMART",27,104,851,624,551,65,4,4),
    seed(69,"CHERRYGROVE POKECENTER 1F",26,73,852,625,552,66,4,4),
    seed(70,"CHERRYGROVE SOUTHWEST HOUSE",25,97,854,627,553,67,4,4),
    seed(71,"CHERRYGROVE GUIDE HOUSE",25,96,855,628,554,68,4,4),
    seed(72,"CHERRYGROVE SOUTHEAST HOUSE",25,97,856,629,555,69,4,4),
};
std::vector<HgMapHeader> gHeaders=kSeeds;
bool gHeadersFromRom=false;

std::string seed_name(int mapId){
    for(auto const& h:kSeeds)if(h.mapId==mapId)return h.name;
    static const std::pair<int,const char*> names[]={
        {9,"ROUTE 1"},{10,"ROUTE 2"},{11,"ROUTE 3"},{12,"ROUTE 4"},{13,"ROUTE 5"},{14,"ROUTE 6"},{15,"ROUTE 7"},{16,"ROUTE 8"},{17,"ROUTE 9"},{18,"ROUTE 10"},{19,"ROUTE 11"},{20,"ROUTE 12"},{21,"ROUTE 13"},{22,"ROUTE 14"},{23,"ROUTE 15"},{24,"ROUTE 16"},{25,"ROUTE 17"},{26,"ROUTE 18"},{27,"ROUTE 22"},{28,"ROUTE 24"},{29,"ROUTE 25"},{30,"ROUTE 26"},{31,"ROUTE 27"},{32,"ROUTE 28"},{34,"ROUTE 30"},{35,"ROUTE 31"},{36,"ROUTE 32"},{37,"ROUTE 33"},{38,"ROUTE 34"},{39,"ROUTE 35"},{40,"ROUTE 36"},{41,"ROUTE 37"},{42,"ROUTE 38"},{43,"ROUTE 39"},{44,"ROUTE 42"},{45,"ROUTE 43"},{46,"ROUTE 44"},{47,"ROUTE 45"},{48,"ROUTE 46"},
        {49,"PALLET TOWN"},{50,"VIRIDIAN CITY"},{51,"PEWTER CITY"},{52,"CERULEAN CITY"},{53,"LAVENDER TOWN"},{54,"VERMILION CITY"},{55,"CELADON CITY"},{56,"FUCHSIA CITY"},{57,"CINNABAR ISLAND"},{58,"INDIGO PLATEAU"},{59,"SAFFRON CITY"},
        {73,"VIOLET CITY"},{74,"AZALEA TOWN"},{75,"CIANWOOD CITY"},{76,"GOLDENROD CITY"},{77,"OLIVINE CITY"},{78,"ECRUTEAK CITY"},{87,"MAHOGANY TOWN"},{88,"LAKE OF RAGE"},{89,"BLACKTHORN CITY"},{90,"MOUNT SILVER"},{91,"ROUTE 19"},{92,"ROUTE 20"},{93,"ROUTE 21"},{94,"ROUTE 40"},{95,"ROUTE 41"},{96,"NATIONAL PARK"},{151,"ROUTE 47"},{152,"ROUTE 48"},{384,"NEW BARK RIVAL HOUSE 2F"}
    };
    for(auto const& [id,name]:names)if(id==mapId)return name;
    return "MAP "+std::to_string(mapId);
}

bool header_anchor_matches(const std::vector<unsigned char>& b,std::size_t p){
    if(p+24>b.size())return false;
    const std::uint16_t mm=u16(b,p+2);
    const std::uint32_t flags=u32(b,p+20);
    return b[p+1]==2 && (mm&0xf)==15 && u16(b,p+4)==0 && u16(b,p+6)==842 &&
           u16(b,p+8)==615 && u16(b,p+10)==542 && u16(b,p+16)==57 && ((flags>>8)&0xf)==1;
}
HgMapHeader parse_map_header(const std::vector<unsigned char>& b,std::size_t p,int id){
    HgMapHeader h;h.mapId=id;h.name=seed_name(id);h.wildEncounterBank=b[p];h.areaDataBank=b[p+1];
    const std::uint16_t mm=u16(b,p+2);h.moveModelBank=mm&0xf;h.worldMapX=(mm>>4)&0x3f;h.worldMapY=(mm>>10)&0x3f;
    h.matrixId=u16(b,p+4);h.scriptsBank=u16(b,p+6);h.scriptHeaderBank=u16(b,p+8);h.msgBank=u16(b,p+10);
    h.dayMusicId=u16(b,p+12);h.nightMusicId=u16(b,p+14);h.eventsBank=u16(b,p+16);
    const std::uint16_t misc=u16(b,p+18);h.mapsec=misc&0xff;h.areaIcon=(misc>>8)&0xf;h.momCallIntroParam=(misc>>12)&0xf;
    const std::uint32_t f=u32(b,p+20);h.regionNo=f&1;h.weather=(f>>1)&0x7f;h.mapType=(f>>8)&0xf;h.cameraType=(f>>12)&0x3f;
    h.followMode=(f>>18)&3;h.battleBg=(f>>20)&0x1f;h.bikeAllowed=((f>>25)&1)!=0;h.runningAllowed=((f>>26)&1)!=0;
    h.escapeRopeAllowed=((f>>27)&1)!=0;h.flyAllowed=((f>>28)&1)!=0;h.outgoingCalls=((f>>29)&1)!=0;
    h.incomingCalls=((f>>30)&1)!=0;h.radioSignal=((f>>31)&1)!=0;return h;
}
}


std::uint16_t HgMapMatrix::header_at(int x,int y,std::uint16_t fallback) const { if(!in_bounds(x,y)||!hasHeaders||headers.size()!=cells())return fallback;return headers[index(x,y)]; }
std::uint16_t HgMapMatrix::land_at(int x,int y,std::uint16_t fallback) const { if(!in_bounds(x,y)||landMembers.size()!=cells())return fallback;return landMembers[index(x,y)]; }
std::uint8_t HgMapMatrix::altitude_at(int x,int y,std::uint8_t fallback) const { if(!in_bounds(x,y)||!hasAltitudes||altitudes.size()!=cells())return fallback;return altitudes[index(x,y)]; }

bool initialize_hg_map_headers(const std::filesystem::path& assets){
    std::filesystem::path arm9=assets.parent_path()/"bin/arm9_dec.bin";
    std::ifstream f(arm9,std::ios::binary);
    if(!f){gHeaders=kSeeds;gHeadersFromRom=false;return false;}
    f.seekg(0,std::ios::end);auto n=f.tellg();if(n<=0){gHeaders=kSeeds;gHeadersFromRom=false;return false;}f.seekg(0);
    std::vector<unsigned char> b(static_cast<std::size_t>(n));f.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));if(!f){gHeaders=kSeeds;gHeadersFromRom=false;return false;}
    constexpr std::size_t stride=24,count=540;std::size_t base=std::string::npos;
    for(std::size_t p=0;p+stride<=b.size();p++){
        if(!header_anchor_matches(b,p)||p<std::size_t(60)*stride)continue;
        std::size_t candidate=p-std::size_t(60)*stride;if(candidate+count*stride>b.size())continue;
        // Independent cross-checks prevent an accidental byte-pattern match.
        auto h33=parse_map_header(b,candidate+33*stride,33);auto h61=parse_map_header(b,candidate+61*stride,61);auto h67=parse_map_header(b,candidate+67*stride,67);
        if(h33.scriptsBank==225&&h33.eventsBank==30&&h33.matrixId==0&&h61.matrixId==100&&h61.eventsBank==58&&h67.scriptsBank==850&&h67.eventsBank==64){base=candidate;break;}
    }
    if(base==std::string::npos){gHeaders=kSeeds;gHeadersFromRom=false;return false;}
    std::vector<HgMapHeader> parsed;parsed.reserve(count);for(std::size_t i=0;i<count;i++)parsed.push_back(parse_map_header(b,base+i*stride,int(i)));
    gHeaders=std::move(parsed);gHeadersFromRom=true;return true;
}
const HgMapHeader* hg_map_header(int mapId){if(mapId>=0&&std::size_t(mapId)<gHeaders.size()&&gHeaders[std::size_t(mapId)].mapId==mapId)return &gHeaders[std::size_t(mapId)];for(auto const& h:gHeaders)if(h.mapId==mapId)return &h;return nullptr;}
const std::vector<HgMapHeader>& hg_supported_map_headers(){return gHeaders;}
bool hg_map_headers_from_rom(){return gHeadersFromRom;}

HgAreaData load_hg_area_data(const std::filesystem::path& assets,std::size_t areaId){
    HgAreaData out;auto b=read_narc_member(assets/"a/0/4/2",areaId);if(b.size()<8){out.error="area-data member missing/truncated";return out;}
    out.buildingsTileset=u16(b,0);out.mapTileset=u16(b,2);out.dynamicTextureType=u16(b,4);out.areaType=b[6];out.lightType=b[7];out.valid=true;return out;
}
HgMapMatrix load_hg_map_matrix(const std::filesystem::path& assets,std::size_t matrixId){
    HgMapMatrix out;auto b=read_narc_member(assets/"a/0/4/1",matrixId);if(b.size()<5){out.error="matrix member missing/truncated";return out;}
    std::size_t p=0;out.width=b[p++];out.height=b[p++];out.hasHeaders=b[p++]!=0;out.hasAltitudes=b[p++]!=0;std::size_t nameLen=b[p++];
    const std::size_t cells=std::size_t(out.width)*out.height;if(!out.width||!out.height||cells>65536||p+nameLen>b.size()){out.error="invalid matrix header";return out;}
    out.name.assign(reinterpret_cast<const char*>(b.data()+p),nameLen);p+=nameLen;
    if(out.hasHeaders){if(p+cells*2>b.size()){out.error="matrix header table truncated";return out;}out.headers.resize(cells);for(std::size_t i=0;i<cells;i++){out.headers[i]=u16(b,p);p+=2;}}
    if(out.hasAltitudes){if(p+cells>b.size()){out.error="matrix altitude table truncated";return out;}out.altitudes.assign(b.begin()+static_cast<std::ptrdiff_t>(p),b.begin()+static_cast<std::ptrdiff_t>(p+cells));p+=cells;}
    if(p+cells*2>b.size()){out.error="matrix land-member table truncated";return out;}out.landMembers.resize(cells);for(std::size_t i=0;i<cells;i++){out.landMembers[i]=u16(b,p);p+=2;}
    out.valid=true;return out;
}
HgEventBank load_hg_event_bank(const std::filesystem::path& assets,std::size_t eventBank){
    HgEventBank out;auto b=read_narc_member(assets/"a/0/3/2",eventBank);if(b.size()<4){out.error="event bank missing/truncated";return out;}std::size_t p=0;
    auto need=[&](std::size_t n){return p+n<=b.size();};
    std::uint32_t n=0;
    if(!need(4)){out.error="spawn count missing";return out;}
    n=u32(b,p);p+=4;if(n>4096||!need(std::size_t(n)*0x14)){out.error="spawn table invalid";return out;}out.spawnables.reserve(n);for(std::uint32_t i=0;i<n;i++,p+=0x14){HgSpawnEvent e;e.scriptNumber=u16(b,p);e.type=u16(b,p+2);e.x=s16(b,p+4);e.unknown2=u16(b,p+6);e.y=s16(b,p+8);e.z=s32(b,p+10);e.unknown4=u16(b,p+14);e.direction=u16(b,p+16);e.unknown5=u16(b,p+18);out.spawnables.push_back(e);}
    if(!need(4)){out.error="overworld count missing";return out;}n=u32(b,p);p+=4;if(n>4096||!need(std::size_t(n)*0x20)){out.error="overworld table invalid";return out;}out.overworlds.reserve(n);for(std::uint32_t i=0;i<n;i++,p+=0x20){HgOverworldEvent e;e.id=u16(b,p);e.model=u16(b,p+2);e.movement=u16(b,p+4);e.type=u16(b,p+6);e.flag=u16(b,p+8);e.scriptNumber=u16(b,p+10);e.orientation=u16(b,p+12);e.sightRange=u16(b,p+14);e.unknown1=u16(b,p+16);e.unknown2=u16(b,p+18);e.xRange=u16(b,p+20);e.yRange=u16(b,p+22);e.x=s16(b,p+24);e.y=s16(b,p+26);e.z=s32(b,p+28);out.overworlds.push_back(e);}
    if(!need(4)){out.error="warp count missing";return out;}n=u32(b,p);p+=4;if(n>4096||!need(std::size_t(n)*0x0c)){out.error="warp table invalid";return out;}out.warps.reserve(n);for(std::uint32_t i=0;i<n;i++,p+=0x0c){HgWarpEvent e;e.x=s16(b,p);e.y=s16(b,p+2);e.targetMap=u16(b,p+4);e.targetWarp=u16(b,p+6);e.height=u32(b,p+8);out.warps.push_back(e);}
    if(!need(4)){out.error="trigger count missing";return out;}n=u32(b,p);p+=4;if(n>4096||!need(std::size_t(n)*0x10)){out.error="trigger table invalid";return out;}out.triggers.reserve(n);for(std::uint32_t i=0;i<n;i++,p+=0x10){HgTriggerEvent e;e.scriptNumber=u16(b,p);e.x=s16(b,p+2);e.y=s16(b,p+4);e.width=u16(b,p+6);e.height=u16(b,p+8);e.z=u16(b,p+10);e.expectedValue=u16(b,p+12);e.variable=u16(b,p+14);out.triggers.push_back(e);}
    out.valid=true;return out;
}
std::vector<unsigned char> load_hg_script_bank(const std::filesystem::path& assets,std::size_t scriptBank){return read_narc_member(assets/"a/0/1/2",scriptBank);}
std::vector<unsigned char> load_hg_message_bank(const std::filesystem::path& assets,std::size_t msgBank){return read_narc_member(assets/"a/0/2/7",msgBank);}

HgWorldValidation validate_hg_starting_world(const std::filesystem::path& assets){
    HgWorldValidation r;std::vector<std::size_t> seenMatrices,seenEvents,seenChunks,seenAreas,seenScripts,seenMessages,seenTextures;
    auto matrixArc=inspect_narc(assets/"a/0/4/1"),areaArc=inspect_narc(assets/"a/0/4/2"),textureArc=inspect_narc(assets/"a/0/4/4");
    auto eventArc=inspect_narc(assets/"a/0/3/2"),scriptArc=inspect_narc(assets/"a/0/1/2"),messageArc=inspect_narc(assets/"a/0/2/7");
    for(auto const& h:gHeaders){
        r.headersChecked++;
        if(!matrixArc.valid||h.matrixId>=matrixArc.members.size()||!areaArc.valid||h.areaDataBank>=areaArc.members.size()||
           !eventArc.valid||h.eventsBank>=eventArc.members.size()||!scriptArc.valid||h.scriptsBank>=scriptArc.members.size()||
           !messageArc.valid||h.msgBank>=messageArc.members.size()){r.failures++;continue;}
        if(std::find(seenAreas.begin(),seenAreas.end(),h.areaDataBank)==seenAreas.end()){
            seenAreas.push_back(h.areaDataBank);r.areaBanksReferenced++;
            auto area=load_hg_area_data(assets,h.areaDataBank);if(!area.valid){r.failures++;continue;}
            if(!textureArc.valid||area.mapTileset>=textureArc.members.size()){r.failures++;continue;}
            if(std::find(seenTextures.begin(),seenTextures.end(),area.mapTileset)==seenTextures.end()){seenTextures.push_back(area.mapTileset);r.textureSetsReferenced++;}
        }
        if(std::find(seenScripts.begin(),seenScripts.end(),h.scriptsBank)==seenScripts.end()){seenScripts.push_back(h.scriptsBank);r.scriptBanksReferenced++;}
        if(std::find(seenMessages.begin(),seenMessages.end(),h.msgBank)==seenMessages.end()){seenMessages.push_back(h.msgBank);r.messageBanksReferenced++;}
        auto m=load_hg_map_matrix(assets,h.matrixId);if(!m.valid){r.failures++;continue;}
        if(std::find(seenMatrices.begin(),seenMatrices.end(),h.matrixId)==seenMatrices.end()){
            seenMatrices.push_back(h.matrixId);r.matricesLoaded++;
            for(auto lm:m.landMembers) if(lm!=0xffff&&std::find(seenChunks.begin(),seenChunks.end(),lm)==seenChunks.end()){
                seenChunks.push_back(lm);auto c=load_land_chunk(assets/"a/0/6/5",lm);if(c.valid){r.mapChunksLoaded++;r.buildings+=c.buildings.size();}else r.failures++;
            }
        }
        auto e=load_hg_event_bank(assets,h.eventsBank);if(!e.valid){r.failures++;continue;}
        if(std::find(seenEvents.begin(),seenEvents.end(),h.eventsBank)==seenEvents.end()){
            seenEvents.push_back(h.eventsBank);r.eventBanksLoaded++;r.overworlds+=e.overworlds.size();r.warps+=e.warps.size();r.triggers+=e.triggers.size();r.spawnables+=e.spawnables.size();
        }
    }
    // Main-matrix sanity anchors recovered from the ROM itself.
    auto main=load_hg_map_matrix(assets,0);if(!main.valid||main.width!=47||main.height!=17||main.header_at(21,12)!=60||main.land_at(21,12)!=0||main.header_at(18,12)!=33||main.land_at(18,12)!=1||main.header_at(17,12)!=67||main.land_at(17,12)!=5)r.failures++;
    r.valid=r.failures==0;std::ostringstream ss;ss<<"headers="<<r.headersChecked<<" matrices="<<r.matricesLoaded<<" areas="<<r.areaBanksReferenced<<" texturesets="<<r.textureSetsReferenced<<" events="<<r.eventBanksLoaded<<" scripts="<<r.scriptBanksReferenced<<" messages="<<r.messageBanksReferenced<<" chunks="<<r.mapChunksLoaded<<" objects="<<r.overworlds<<" warps="<<r.warps<<" buildings="<<r.buildings<<" failures="<<r.failures;r.summary=ss.str();return r;
}


std::vector<HgGlobalScriptEntry> load_hg_global_script_table(const std::filesystem::path& assets){
    // The supplied US HG/SS decoded ARM9 stores a pointer to the 30-entry
    // ScriptBankMapping table at file offset 0x40164. Each entry is three u16s:
    // minimum global script id, scr_seq NARC member and message NARC member.
    // Read the table from the user's extracted ROM so the port follows that
    // exact revision instead of baking another region/version-specific list.
    const auto arm9=assets.parent_path()/"bin/arm9_dec.bin";
    std::ifstream f(arm9,std::ios::binary);
    if(!f)return {};
    f.seekg(0,std::ios::end);const auto end=f.tellg();
    if(end<std::streamoff(0x40168))return {};
    f.seekg(0x40164,std::ios::beg);
    std::array<unsigned char,4> ptr{};f.read(reinterpret_cast<char*>(ptr.data()),4);if(!f)return {};
    const std::uint32_t address=std::uint32_t(ptr[0])|(std::uint32_t(ptr[1])<<8)|(std::uint32_t(ptr[2])<<16)|(std::uint32_t(ptr[3])<<24);
    constexpr std::uint32_t kArm9Base=0x02000000u;
    if(address<kArm9Base)return {};
    const std::uint64_t offset=std::uint64_t(address-kArm9Base);
    if(offset+30u*6u>std::uint64_t(end))return {};
    f.seekg(std::streamoff(offset),std::ios::beg);
    std::vector<HgGlobalScriptEntry> out;out.reserve(30);
    for(int i=0;i<30;i++){
        std::array<unsigned char,6> e{};f.read(reinterpret_cast<char*>(e.data()),6);if(!f)return {};
        HgGlobalScriptEntry v;
        v.minScriptId=std::uint16_t(e[0])|(std::uint16_t(e[1])<<8);
        v.scriptBank=std::uint16_t(e[2])|(std::uint16_t(e[3])<<8);
        v.messageBank=std::uint16_t(e[4])|(std::uint16_t(e[5])<<8);
        out.push_back(v);
    }
    // Retail table is descending. Reject a false-positive pointer instead of
    // silently routing scripts into arbitrary NARC members.
    if(out.size()!=30||out.back().minScriptId!=2000)return {};
    for(std::size_t i=1;i<out.size();++i)if(out[i-1].minScriptId<=out[i].minScriptId)return {};
    return out;
}

HgGlobalScriptResolution resolve_hg_global_script(const std::filesystem::path& assets,std::uint16_t globalScriptId){
    HgGlobalScriptResolution r;r.globalScriptId=globalScriptId;
    auto table=load_hg_global_script_table(assets);
    for(auto const& e:table){
        if(globalScriptId<e.minScriptId)continue;
        const std::uint32_t local=std::uint32_t(globalScriptId-e.minScriptId)+1u;
        if(local>0xffffu)return r;
        r.valid=true;r.scriptBank=e.scriptBank;r.messageBank=e.messageBank;r.localScriptNumber=std::uint16_t(local);return r;
    }
    return r;
}
