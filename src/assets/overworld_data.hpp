#pragma once
#include "assets/land_data.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct HgMapHeader {
    int mapId=-1;
    std::string name;
    std::uint8_t wildEncounterBank=0xff;
    std::uint8_t areaDataBank=0;
    std::uint16_t moveModelBank=15;
    std::uint16_t worldMapX=0,worldMapY=0;
    std::uint16_t matrixId=0;
    std::uint16_t scriptsBank=0;
    std::uint16_t scriptHeaderBank=0;
    std::uint16_t msgBank=0;
    std::uint16_t dayMusicId=0,nightMusicId=0;
    std::uint16_t eventsBank=0;
    std::uint8_t mapsec=0,areaIcon=0,momCallIntroParam=0;
    std::uint8_t regionNo=0,weather=0,mapType=0,cameraType=0,followMode=0,battleBg=0;
    bool bikeAllowed=false,runningAllowed=false,escapeRopeAllowed=false,flyAllowed=false;
    bool outgoingCalls=false,incomingCalls=false,radioSignal=false;
};

struct HgAreaData {
    bool valid=false;
    std::string error;
    std::uint16_t buildingsTileset=0;
    std::uint16_t mapTileset=0;
    std::uint16_t dynamicTextureType=0;
    std::uint8_t areaType=0;
    std::uint8_t lightType=0;
};

struct HgMapMatrix {
    bool valid=false;
    std::string error;
    std::uint8_t width=0, height=0;
    bool hasHeaders=false, hasAltitudes=false;
    std::string name;
    std::vector<std::uint16_t> headers;
    std::vector<std::uint8_t> altitudes;
    std::vector<std::uint16_t> landMembers;
    std::size_t cells() const { return std::size_t(width)*height; }
    bool in_bounds(int x,int y) const { return x>=0&&y>=0&&x<int(width)&&y<int(height); }
    std::size_t index(int x,int y) const { return std::size_t(y)*width+std::size_t(x); }
    std::uint16_t header_at(int x,int y,std::uint16_t fallback=0) const;
    std::uint16_t land_at(int x,int y,std::uint16_t fallback=0) const;
    std::uint8_t altitude_at(int x,int y,std::uint8_t fallback=0) const;
};

struct HgSpawnEvent {
    std::uint16_t scriptNumber=0, type=0;
    std::int16_t x=0,y=0;
    std::int32_t z=0;
    std::uint16_t unknown2=0,unknown4=0,direction=0,unknown5=0;
};
struct HgOverworldEvent {
    std::uint16_t id=0, model=0, movement=0, type=0, flag=0, scriptNumber=0, orientation=0, sightRange=0;
    std::uint16_t unknown1=0,unknown2=0,xRange=0,yRange=0;
    std::int16_t x=0,y=0;
    std::int32_t z=0;
};
struct HgWarpEvent {
    std::int16_t x=0,y=0;
    std::uint16_t targetMap=0,targetWarp=0;
    std::uint32_t height=0;
};
struct HgTriggerEvent {
    std::uint16_t scriptNumber=0;
    std::int16_t x=0,y=0;
    std::uint16_t width=0,height=0,z=0,expectedValue=0,variable=0;
};
struct HgEventBank {
    bool valid=false;
    std::string error;
    std::vector<HgSpawnEvent> spawnables;
    std::vector<HgOverworldEvent> overworlds;
    std::vector<HgWarpEvent> warps;
    std::vector<HgTriggerEvent> triggers;
};

bool initialize_hg_map_headers(const std::filesystem::path& assets);
const HgMapHeader* hg_map_header(int mapId);
const std::vector<HgMapHeader>& hg_supported_map_headers();
bool hg_map_headers_from_rom();
HgAreaData load_hg_area_data(const std::filesystem::path& assets,std::size_t areaId);
HgMapMatrix load_hg_map_matrix(const std::filesystem::path& assets,std::size_t matrixId);
HgEventBank load_hg_event_bank(const std::filesystem::path& assets,std::size_t eventBank);
std::vector<unsigned char> load_hg_script_bank(const std::filesystem::path& assets,std::size_t scriptBank);
std::vector<unsigned char> load_hg_message_bank(const std::filesystem::path& assets,std::size_t msgBank);


// Retail ScriptContext_LoadAndOffsetID mapping recovered from the supplied
// decoded ARM9. Global script IDs are not a second native implementation: the
// entry selects the original scr_seq NARC member and matching text archive,
// while localScriptNumber is the 1-based entry within that member.
struct HgGlobalScriptEntry {
    std::uint16_t minScriptId=0;
    std::uint16_t scriptBank=0;
    std::uint16_t messageBank=0;
};
struct HgGlobalScriptResolution {
    bool valid=false;
    std::uint16_t globalScriptId=0;
    std::uint16_t scriptBank=0;
    std::uint16_t messageBank=0;
    std::uint16_t localScriptNumber=0;
};
std::vector<HgGlobalScriptEntry> load_hg_global_script_table(const std::filesystem::path& assets);
HgGlobalScriptResolution resolve_hg_global_script(const std::filesystem::path& assets,std::uint16_t globalScriptId);

struct HgWorldValidation {
    bool valid=false;
    std::size_t headersChecked=0, matricesLoaded=0, eventBanksLoaded=0, mapChunksLoaded=0;
    std::size_t areaBanksReferenced=0,scriptBanksReferenced=0,messageBanksReferenced=0,textureSetsReferenced=0;
    std::size_t overworlds=0, warps=0, triggers=0, spawnables=0, buildings=0;
    std::size_t failures=0;
    std::string summary;
};
HgWorldValidation validate_hg_starting_world(const std::filesystem::path& assets);
