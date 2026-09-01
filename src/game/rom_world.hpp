#pragma once
#include "assets/overworld_data.hpp"
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>

struct HgLoadedChunk {
    int matrixX=0,matrixY=0;
    int mapId=0;
    std::uint16_t landMember=0;
    HgAreaData area{};
    LandChunk land{};
};

class HgRomWorld {
public:
    explicit HgRomWorld(std::filesystem::path assetsRoot={});
    void setAssets(std::filesystem::path assetsRoot);
    bool initialize();
    bool loadMap(int mapId,int x,int y);
    bool canMoveTo(int x,int y) const;
    bool commitPosition(int x,int y);
    bool processWarp();
    bool useWarpAt(int x,int y);
    bool useWarpAtExact(int x,int y);
    void refreshVisible();

    bool ready() const { return ready_; }
    int mapId() const { return mapId_; }
    int x() const { return x_; }
    int y() const { return y_; }
    const HgMapHeader* header() const { return header_; }
    const HgMapMatrix& matrix() const { return matrix_; }
    const HgAreaData& area() const { return area_; }
    const HgEventBank& events() const { return events_; }
    const std::vector<unsigned char>& scriptBank() const { return scripts_; }
    const std::vector<unsigned char>& messageBank() const { return messages_; }
    const std::vector<HgLoadedChunk>& visibleChunks() const { return visible_; }
    std::string locationName() const;
    std::uint16_t currentLandMember() const;
    int currentCellX() const;
    int currentCellY() const;
    int localX() const;
    int localY() const;
    float sampleHeightAt(int x,int y,float fallback=0.0f) const;
    const HgPermissionCell* permissionAt(int x,int y) const;
    bool isLandEncounterTile(int x,int y) const;
    const HgLoadedChunk* chunkForTile(int x,int y) const;
    std::string error() const { return error_; }
    std::string lastTransition() const { return lastTransition_; }

private:
    std::filesystem::path assets_;
    bool ready_=false;
    std::string error_,lastTransition_;
    int mapId_=60,x_=688,y_=398;
    const HgMapHeader* header_=nullptr;
    HgMapMatrix matrix_{};
    HgAreaData area_{};
    HgEventBank events_{};
    std::vector<unsigned char> scripts_,messages_;
    std::vector<HgLoadedChunk> visible_;
    std::unordered_map<std::uint16_t,LandChunk> landCache_;
    std::unordered_map<std::uint16_t,NsbmdMember> textureCache_;

    bool loadHeaderData(int mapId,bool keepPosition);
    bool isMainMatrix() const;
    std::pair<int,int> cellForTile(int x,int y) const;
    std::pair<int,int> localForTile(int x,int y) const;
    bool isWarpTile(int x,int y) const;
    bool moveToDestination(const HgWarpEvent& sourceWarp,bool spawnAway=true);
};
