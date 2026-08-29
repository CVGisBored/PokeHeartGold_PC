#include "game/rom_world.hpp"
#include "assets/nsbmd.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace {
int floor_div32(int v){ if(v>=0)return v/32; return -((31-v)/32); }
int mod32(int v){ int m=v%32;return m<0?m+32:m; }
}

HgRomWorld::HgRomWorld(std::filesystem::path a):assets_(std::move(a)){}
void HgRomWorld::setAssets(std::filesystem::path a){assets_=std::move(a);}
bool HgRomWorld::initialize(){
    initialize_hg_map_headers(assets_);
    // Native-port field slice begins outdoors at a ROM-permission-valid point in New Bark.
    return loadMap(60,688,398);
}

bool HgRomWorld::loadHeaderData(int mapId,bool keepPosition){
    const auto* h=hg_map_header(mapId);if(!h){error_="map header is not yet in the native decompilation table: "+std::to_string(mapId);return false;}
    auto m=load_hg_map_matrix(assets_,h->matrixId);if(!m.valid){error_="matrix "+std::to_string(h->matrixId)+": "+m.error;return false;}
    auto a=load_hg_area_data(assets_,h->areaDataBank);if(!a.valid){error_="area "+std::to_string(h->areaDataBank)+": "+a.error;return false;}
    auto e=load_hg_event_bank(assets_,h->eventsBank);if(!e.valid){error_="event bank "+std::to_string(h->eventsBank)+": "+e.error;return false;}
    header_=h;mapId_=mapId;matrix_=std::move(m);area_=a;events_=std::move(e);scripts_=load_hg_script_bank(assets_,h->scriptsBank);messages_=load_hg_message_bank(assets_,h->msgBank);ready_=true;error_.clear();
    if(!keepPosition){x_=0;y_=0;}
    refreshVisible();return true;
}

bool HgRomWorld::loadMap(int mapId,int x,int y){
    if(!loadHeaderData(mapId,true)) return false;
    x_=x;
    y_=y;
    refreshVisible();
    return true;
}
bool HgRomWorld::isMainMatrix() const{return header_&&header_->matrixId==0;}
std::pair<int,int> HgRomWorld::cellForTile(int x,int y) const{
    if(isMainMatrix())return {floor_div32(x),floor_div32(y)};
    return {floor_div32(x),floor_div32(y)};
}
std::pair<int,int> HgRomWorld::localForTile(int x,int y) const{return {mod32(x),mod32(y)};}
int HgRomWorld::currentCellX() const{return cellForTile(x_,y_).first;}
int HgRomWorld::currentCellY() const{return cellForTile(x_,y_).second;}
int HgRomWorld::localX() const{return localForTile(x_,y_).first;}
int HgRomWorld::localY() const{return localForTile(x_,y_).second;}

std::uint16_t HgRomWorld::currentLandMember() const{
    if(!matrix_.valid) return 0;
    auto [cx,cy]=cellForTile(x_,y_);
    if(!matrix_.in_bounds(cx,cy)){
        if(!isMainMatrix()&&matrix_.in_bounds(0,0)) return matrix_.land_at(0,0);
        return 0;
    }
    return matrix_.land_at(cx,cy);
}
std::string HgRomWorld::locationName() const{return header_?header_->name:("MAP "+std::to_string(mapId_));}

const HgLoadedChunk* HgRomWorld::chunkForTile(int x,int y) const{
    auto [cx,cy]=cellForTile(x,y);
    if(!isMainMatrix()&&!matrix_.in_bounds(cx,cy)){cx=0;cy=0;}
    for(auto const& c:visible_) if(c.matrixX==cx&&c.matrixY==cy) return &c;
    return nullptr;
}
bool HgRomWorld::isWarpTile(int x,int y) const{for(auto const& w:events_.warps)if(w.x==x&&w.y==y)return true;return false;}
bool HgRomWorld::canMoveTo(int x,int y) const{
    if(!ready_)return false;
    // A door/warp may deliberately sit on a blocked permission cell.
    if(isWarpTile(x,y))return true;
    auto [cx,cy]=cellForTile(x,y);
    if(!matrix_.in_bounds(cx,cy)){
        if(!isMainMatrix()&&matrix_.width==1&&matrix_.height==1){cx=0;cy=0;}else return false;
    }
    if(matrix_.hasHeaders){int next=matrix_.header_at(cx,cy,mapId_);if(next!=mapId_&&!hg_map_header(next))return false;}
    const HgLoadedChunk* c=chunkForTile(x,y);if(!c||!c->land.valid)return false;
    auto [lx,ly]=localForTile(x,y);auto* perm=c->land.permission_at(lx,ly);if(!perm||!perm->walkable())return false;
    return true;
}

bool HgRomWorld::commitPosition(int x,int y){
    const auto oldCell=cellForTile(x_,y_);
    const int oldMap=mapId_;
    x_=x;
    y_=y;
    if(matrix_.hasHeaders){
        auto [cx,cy]=cellForTile(x_,y_);
        if(matrix_.in_bounds(cx,cy)){
            int next=matrix_.header_at(cx,cy,mapId_);
            if(next!=mapId_){
                auto const* nh=hg_map_header(next);
                if(nh&&header_&&nh->matrixId==header_->matrixId){
                    if(!loadHeaderData(next,true)) return false;
                    lastTransition_=locationName();
                    return true; // loadHeaderData already refreshed the visible window.
                }
            }
        }
    }
    if(oldMap!=mapId_ || oldCell!=cellForTile(x_,y_)) refreshVisible();
    return true;
}

bool HgRomWorld::moveToDestination(const HgWarpEvent& w){
    const auto* target=hg_map_header(w.targetMap);if(!target){lastTransition_="TARGET MAP "+std::to_string(w.targetMap)+" NOT PORTED YET";return false;}
    auto targetEvents=load_hg_event_bank(assets_,target->eventsBank);if(!targetEvents.valid||w.targetWarp>=targetEvents.warps.size()){lastTransition_="TARGET WARP DATA INVALID";return false;}
    const auto destination=targetEvents.warps[w.targetWarp];
    if(!loadHeaderData(w.targetMap,true))return false;
    x_=destination.x;y_=destination.y;
    // Spawn one tile away from the destination warp so it cannot instantly bounce.
    int dx=0,dy=0;
    if(target->mapType==4){
        if(y_>=8)dy=-1;else dy=1;
    }else{
        int ly=mod32(y_),lx=mod32(x_);
        if(ly<16)dy=1;else dy=-1;
        if(!canMoveTo(x_+dx,y_+dy)){dy=0;if(lx<16)dx=1;else dx=-1;}
    }
    if(canMoveTo(x_+dx,y_+dy)){x_+=dx;y_+=dy;}
    refreshVisible();lastTransition_=locationName();return true;
}
bool HgRomWorld::processWarp(){for(auto const& w:events_.warps)if(w.x==x_&&w.y==y_)return moveToDestination(w);return false;}
bool HgRomWorld::useWarpAt(int x,int y){for(auto const& w:events_.warps)if(w.x==x&&w.y==y)return moveToDestination(w);return false;}

const HgPermissionCell* HgRomWorld::permissionAt(int x,int y) const{auto* c=chunkForTile(x,y);if(!c||!c->land.valid)return nullptr;auto [lx,ly]=localForTile(x,y);return c->land.permission_at(lx,ly);}
bool HgRomWorld::isLandEncounterTile(int x,int y) const{auto* p=permissionAt(x,y);return p&&hg_permission_allows_land_encounter(*p);}

float HgRomWorld::sampleHeightAt(int x,int y,float fallback) const{
    auto* c=chunkForTile(x,y);if(!c||!c->land.bdhc.valid)return fallback;auto [lx,ly]=localForTile(x,y);float h=fallback;
    // Event coordinates identify a tile cell. BDHC coordinates describe the
    // geometry boundaries around those cells, so sample the actor's feet at
    // the cell center rather than the upper-left edge. This keeps sprite height
    // aligned with ramps/stairs for the same reason the renderer uses +0.5 X/Y.
    if(sample_bdhc_height(c->land.bdhc,float(lx-16)+0.5f,float(ly-16)+0.5f,h)) return h;
    return fallback;
}

void HgRomWorld::refreshVisible(){
    visible_.clear();if(!ready_||!matrix_.valid)return;auto [pcx,pcy]=cellForTile(x_,y_);
    int radius=(matrix_.width==1&&matrix_.height==1)?0:1;
    for(int oy=-radius;oy<=radius;oy++)for(int ox=-radius;ox<=radius;ox++){
        int cx=pcx+ox,cy=pcy+oy;if(!matrix_.in_bounds(cx,cy))continue;
        int cellMap=matrix_.hasHeaders?int(matrix_.header_at(cx,cy,mapId_)):mapId_;
        auto const* ch=hg_map_header(cellMap);std::uint16_t areaId=ch?ch->areaDataBank:header_->areaDataBank;
        auto area=load_hg_area_data(assets_,areaId);if(!area.valid)area=area_;
        auto landId=matrix_.land_at(cx,cy);if(landId==0xffff)continue;
        auto li=landCache_.find(landId);
        if(li==landCache_.end()) li=landCache_.emplace(landId,load_land_chunk(assets_/"a/0/6/5",landId)).first;
        if(!li->second.valid)continue;
        LandChunk land=li->second;
        auto ti=textureCache_.find(area.mapTileset);
        if(ti==textureCache_.end()) ti=textureCache_.emplace(area.mapTileset,load_nitro_texture_from_narc(assets_/"a/0/4/4",area.mapTileset)).first;
        if(ti->second.valid)bind_nsbmd_external_textures(land.model,ti->second);
        visible_.push_back({cx,cy,cellMap,landId,area,std::move(land)});
    }
}
