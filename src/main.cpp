#include "game/game.hpp"
#include "assets/nsbmd.hpp"
#include "assets/land_data.hpp"
#include "assets/narc.hpp"
#include "assets/overworld_data.hpp"
#include "game/rom_world.hpp"
#include "game/hg_script.hpp"
#include "assets/hg_text.hpp"
#include "assets/wild_encounter.hpp"
#include "assets/trainer_data.hpp"
#include "assets/sdat.hpp"
#include "game/script_header.hpp"
#include "game/hg_state.hpp"
#include "platform/vulkan_xcb.hpp"
#include "platform/runtime_paths.hpp"
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <cmath>
#include <chrono>
#include <string>

static std::filesystem::path defaultSavePath(){
#ifdef _WIN32
    if(const char* appdata=std::getenv("APPDATA")) return std::filesystem::path(appdata)/"HeartGoldNative/world_state.sav";
    if(const char* profile=std::getenv("USERPROFILE")) return std::filesystem::path(profile)/"AppData/Roaming/HeartGoldNative/world_state.sav";
#else
    if(const char* home=std::getenv("HOME")) return std::filesystem::path(home)/".local/share/heartgold-native/world_state.sav";
#endif
    return "save/world_state.sav";
}

static void pulse(NativeGame& game, GameButton b, double dt=1.0/60.0){
    InputState i; auto n=static_cast<std::size_t>(b); i.down[n]=true;i.pressed[n]=true;game.update(i,dt);i.clearEdges();i.down[n]=false;i.released[n]=true;game.update(i,dt);
}

static void idleFrames(NativeGame& game,int frames,double dt=1.0/60.0){
    InputState none;for(int i=0;i<frames;i++)game.update(none,dt);
}

static bool frameHasText(const RenderFrame& f,const std::string& needle){
    for(auto const& t:f.texts){
        if(t.text.find(needle)!=std::string::npos)return true;
    }
    return false;
}

static bool frameHasDimText(const RenderFrame& f,const std::string& needle,float maxRgb){
    for(auto const& t:f.texts){
        if(t.text.find(needle)!=std::string::npos && std::max({t.color.r,t.color.g,t.color.b})<maxRgb)return true;
    }
    return false;
}

static bool dumpFramePpm(const RenderFrame& f,const std::filesystem::path& out){
    constexpr int W=int(RenderFrame::LogicalWidth),H=int(RenderFrame::LogicalHeight);std::vector<unsigned char> rgb(size_t(W)*H*3);
    auto to8=[](float v){return static_cast<unsigned char>(std::clamp(v,0.0f,1.0f)*255.0f+0.5f);};
    if(f.hasPixels()){
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){auto q=(size_t(y)*W+x)*3,src=(size_t(y)*W+x)*4;rgb[q]=f.rgba[src];rgb[q+1]=f.rgba[src+1];rgb[q+2]=f.rgba[src+2];}
    }else{
        for(int y=0;y<H;y++)for(int x=0;x<W;x++){auto q=(size_t(y)*W+x)*3;rgb[q]=to8(f.clear.r);rgb[q+1]=to8(f.clear.g);rgb[q+2]=to8(f.clear.b);}
    }
    for(auto const& r:f.rects){int x0=std::max(0,int(std::floor(r.x))),y0=std::max(0,int(std::floor(r.y))),x1=std::min(W,int(std::ceil(r.x+r.w))),y1=std::min(H,int(std::ceil(r.y+r.h)));for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){auto q=(size_t(y)*W+x)*3;rgb[q]=to8(r.color.r);rgb[q+1]=to8(r.color.g);rgb[q+2]=to8(r.color.b);}}
    std::error_code ec;if(out.has_parent_path())std::filesystem::create_directories(out.parent_path(),ec);std::ofstream o(out,std::ios::binary);if(!o)return false;o<<"P6\n"<<W<<" "<<H<<"\n255\n";o.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()));return bool(o);
}

int main(int argc,char** argv){
    std::filesystem::path assets; bool assetsExplicit=false; std::filesystem::path savePath=defaultSavePath(); bool validateOnly=false,logicTest=false,assetTest=false,landTest=false,worldTest=false,scriptTest=false,gameLoopTest=false,newGameTest=false,pokegearTest=false,battleTurnTest=false,battleRenderTest=false,clerkTest=false,noController=false; std::filesystem::path dumpTitleFrame,dumpOpeningFrame,dumpMainMenuFrame,dumpNewGameFrame,dumpDemoFrame,dumpTerrainFrame,dumpSpriteFrame,dumpMapFrame,dumpBattleFrame; double dumpOpeningSeconds=-1.0; int dumpMapId=-1,dumpMapX=0,dumpMapY=0,fieldBenchmarkFrames=0,fieldBenchmarkMap=60,fieldBenchmarkX=695,fieldBenchmarkY=397,inspectMapHeader=-1, exportField=-1,exportRoom=-1,exportLand=-1,exportTexMember=-1,exportTexIndex=0,exportMmodelMember=-1,exportMmodelTexIndex=0; std::filesystem::path exportPath;
    for(int i=1;i<argc;i++){
        std::string a=argv[i];
        if(a=="--assets"&&i+1<argc){assets=argv[++i];assetsExplicit=true;}
        else if(a=="--save"&&i+1<argc)savePath=argv[++i];
        else if(a=="--validate-assets")validateOnly=true;
        else if(a=="--logic-test")logicTest=true;
        else if(a=="--asset-test")assetTest=true;
        else if(a=="--land-test")landTest=true;
        else if(a=="--world-test")worldTest=true;
        else if(a=="--script-test")scriptTest=true;
        else if(a=="--game-loop-test")gameLoopTest=true;
        else if(a=="--new-game-test")newGameTest=true;
        else if(a=="--pokegear-test")pokegearTest=true;
        else if(a=="--battle-turn-test")battleTurnTest=true;
        else if(a=="--battle-render-test")battleRenderTest=true;
        else if(a=="--clerk-test")clerkTest=true;
        else if(a=="--dump-battle-frame"&&i+1<argc)dumpBattleFrame=argv[++i];
        else if(a=="--field-render-benchmark"&&i+1<argc){fieldBenchmarkFrames=std::max(1,std::stoi(argv[++i]));if(i+3<argc&&argv[i+1][0]!='-'){fieldBenchmarkMap=std::stoi(argv[++i]);fieldBenchmarkX=std::stoi(argv[++i]);fieldBenchmarkY=std::stoi(argv[++i]);}}
        else if(a=="--no-controller")noController=true;
        else if(a=="--dump-title-frame"&&i+1<argc)dumpTitleFrame=argv[++i];
        else if(a=="--dump-opening-frame"&&i+2<argc){dumpOpeningSeconds=std::stod(argv[++i]);dumpOpeningFrame=argv[++i];}
        else if(a=="--dump-main-menu-frame"&&i+1<argc)dumpMainMenuFrame=argv[++i];
        else if(a=="--dump-new-game-frame"&&i+1<argc)dumpNewGameFrame=argv[++i];
        else if((a=="--dump-world-frame"||a=="--dump-demo-frame")&&i+1<argc)dumpDemoFrame=argv[++i];
        else if(a=="--dump-map-frame"&&i+4<argc){dumpMapId=std::stoi(argv[++i]);dumpMapX=std::stoi(argv[++i]);dumpMapY=std::stoi(argv[++i]);dumpMapFrame=argv[++i];}
        else if(a=="--map-header"&&i+1<argc)inspectMapHeader=std::stoi(argv[++i]);
        else if(a=="--dump-terrain-frame"&&i+1<argc)dumpTerrainFrame=argv[++i];
        else if(a=="--dump-sprite-frame"&&i+1<argc)dumpSpriteFrame=argv[++i];
        else if(a=="--export-field-model"&&i+2<argc){exportField=std::stoi(argv[++i]);exportPath=argv[++i];}
        else if(a=="--export-room-model"&&i+2<argc){exportRoom=std::stoi(argv[++i]);exportPath=argv[++i];}
        else if(a=="--export-land-model"&&i+2<argc){exportLand=std::stoi(argv[++i]);exportPath=argv[++i];}
        else if(a=="--export-field-texture"&&i+3<argc){exportTexMember=std::stoi(argv[++i]);exportTexIndex=std::stoi(argv[++i]);exportPath=argv[++i];}
        else if(a=="--export-mmodel-texture"&&i+3<argc){exportMmodelMember=std::stoi(argv[++i]);exportMmodelTexIndex=std::stoi(argv[++i]);exportPath=argv[++i];}
        else if(a=="--help"){
            std::cout<<"heartgold_native [--assets PATH] [--save PATH] [--validate-assets] [--logic-test] [--asset-test] [--land-test] [--world-test] [--script-test] [--game-loop-test] [--new-game-test] [--pokegear-test] [--clerk-test] [--map-header ID] [--no-controller]\n                 [--export-field-model INDEX OUT.obj] [--export-room-model INDEX OUT.obj] [--export-land-model INDEX OUT.obj]\n                 [--export-field-texture MEMBER TEXINDEX OUT.ppm] [--export-mmodel-texture MEMBER TEXINDEX OUT.ppm]\n                 [--dump-opening-frame SECONDS OUT.ppm] [--dump-title-frame OUT.ppm] [--dump-main-menu-frame OUT.ppm] [--dump-new-game-frame OUT.ppm] [--dump-world-frame OUT.ppm] [--dump-map-frame MAP X Y OUT.ppm] [--dump-terrain-frame OUT.ppm] [--dump-sprite-frame OUT.ppm]\n\n"
                     <<"Controls:\n  WASD / arrows   Move\n  Shift           Run\n  E / Z / Enter   Interact / confirm\n  X / Escape      Menu / back\n  F2              Real ROM model gallery\n  F3              Debug HUD\n  F4              Inspect active ROM map chunk + BDHC\n  F5              Save\n  F9              Load\n  R               Reset to New Bark ROM spawn\n  Q               Quit\n";
            return 0;
        }
    }
    if(!assetsExplicit) assets=hgFindRetailNitroFs(argc>0?argv[0]:nullptr);
    if(!hgLooksLikeRetailNitroFs(assets)){
        std::cerr << "FATAL: HeartGold retail NitroFS assets were not found.\n"
                  << "The release build will NOT fall back to the old native demo.\n"
                  << hgAssetSearchHint(argc>0?argv[0]:nullptr);
        if(assetsExplicit) std::cerr << "Requested --assets path: " << assets << "\n";
        return 3;
    }
    std::cout << "Retail NitroFS: " << assets << "\n";

    if(inspectMapHeader>=0){
        bool full=initialize_hg_map_headers(assets);
        auto h=hg_map_header(inspectMapHeader);
        if(!h){std::cerr<<"No map header "<<inspectMapHeader<<"\n";return 20;}
        std::cout<<"MapHeader "<<h->mapId<<" ("<<h->name<<") source="<<(full?"decoded ARM9":"seed fallback")<<"\n"
                 <<"  wild="<<int(h->wildEncounterBank)<<" area="<<int(h->areaDataBank)<<" moveModel="<<h->moveModelBank<<" world="<<h->worldMapX<<","<<h->worldMapY<<" matrix="<<h->matrixId<<"\n"
                 <<"  scripts="<<h->scriptsBank<<" scriptHeader="<<h->scriptHeaderBank<<" messages="<<h->msgBank<<" events="<<h->eventsBank<<"\n"
                 <<"  music(day/night)="<<h->dayMusicId<<"/"<<h->nightMusicId<<" mapsec="<<int(h->mapsec)<<" region="<<int(h->regionNo)<<" weather="<<int(h->weather)<<" type="<<int(h->mapType)<<" camera="<<int(h->cameraType)<<" follow="<<int(h->followMode)<<" battleBg="<<int(h->battleBg)<<"\n"
                 <<"  bike="<<h->bikeAllowed<<" run="<<h->runningAllowed<<" escape="<<h->escapeRopeAllowed<<" fly="<<h->flyAllowed<<" calls(out/in)="<<h->outgoingCalls<<"/"<<h->incomingCalls<<" radio="<<h->radioSignal<<"\n";
        return full?0:21;
    }
    if(worldTest){
        bool fullHeaders=initialize_hg_map_headers(assets);
        auto v=validate_hg_starting_world(assets);
        HgRomWorld w(assets);
        bool init=w.initialize();
        bool newBark=init&&w.mapId()==60&&w.currentLandMember()==0&&w.x()==688&&w.y()==398&&w.matrix().width==47&&w.matrix().height==17;
        bool route29=false,cherrygrove=false,labWarp=false,returnWarp=false,fullTableWarp=false;
        if(init&&w.loadMap(60,671,398)){w.commitPosition(671,398);route29=w.mapId()==33;}
        if(init&&w.loadMap(60,559,398)){w.commitPosition(559,398);cherrygrove=w.mapId()==67;}
        if(init&&w.loadMap(60,684,393)){labWarp=w.useWarpAt(684,393)&&w.mapId()==61&&w.header()&&w.header()->matrixId==100&&w.currentLandMember()==244;}
        if(labWarp){returnWarp=w.useWarpAt(4,14)&&w.mapId()==60&&w.header()&&w.header()->matrixId==0;}
        // Map 384 (rival house 2F) was not in the old starter metadata table; reaching it proves
        // warp resolution is using the complete ROM MapHeader table rather than the seed fallback.
        if(init&&w.loadMap(66,3,3)) fullTableWarp=w.useWarpAt(3,3)&&w.mapId()==384&&w.header()!=nullptr;
        std::cout<<"MapHeader source: "<<(fullHeaders?"decoded ARM9 / 540 records":"seed fallback")<<"\n"
                 <<"ROM world test: "<<v.summary<<"\n"
                 <<"New Bark matrix anchor: "<<(newBark?"yes":"NO")<<"\n"
                 <<"New Bark -> Route 29 matrix transition: "<<(route29?"yes":"NO")<<"\n"
                 <<"New Bark -> Cherrygrove matrix transition: "<<(cherrygrove?"yes":"NO")<<"\n"
                 <<"New Bark -> Elm Lab ROM warp: "<<(labWarp?"yes":"NO")<<"\n"
                 <<"Elm Lab -> New Bark return warp: "<<(returnWarp?"yes":"NO")<<"\n"
                 <<"Full-table warp to map 384: "<<(fullTableWarp?"yes":"NO")<<"\n";
        return (fullHeaders&&v.valid&&newBark&&route29&&cherrygrove&&labWarp&&returnWarp&&fullTableWarp)?0:19;
    }
    if(gameLoopTest){
        bool headers=initialize_hg_map_headers(assets);auto* nb=hg_map_header(60);
        auto sh=parse_hg_script_header(read_narc_member(assets/"a/0/1/2",615));
        HgGameState st;st.setVar(0x4106,1);auto triggered=hg_triggered_frame_script(sh,st);
        auto enc=load_hg_wild_table(assets,1);auto slot=enc.valid?choose_hg_land_encounter(enc,0,12):HgWildSlot{};
        auto trainer=load_hg_trainer(assets,1);
        HgSdat sd;bool sdok=sd.load(assets/"data/sound/gs_sound_data.sdat");auto stats=sd.stats();auto song=sdok?sd.renderSequence(nb?nb->dayMusicId:1018,2.0):HgPcmWave{};
        HgGameState state;state.addItem(4,5);state.giveMon(152,5);state.pokedex=true;state.see(152);state.own(152);
        HgMessageBank msgs(load_hg_message_bank(assets,542));HgScriptVm vm;vm.bindState(&state);bool started=vm.start(load_hg_script_bank(assets,842),2);HgScriptYield y;
        for(int i=0;i<16&&vm.active();i++){y=vm.runUntilYield(&msgs);if(y.type==HgScriptYield::Type::Message||y.type==HgScriptYield::Type::Unsupported||y.type==HgScriptYield::Type::Error)break;}
        long long songEnergy=0;for(auto v:song.samples)songEnergy+=std::abs(int(v));
        bool ok=headers&&nb&&sh.valid&&triggered==4&&enc.valid&&enc.walkingRate>0&&slot.species>0&&trainer.valid&&trainer.party.size()==3&&trainer.party[0].species==92&&sdok&&stats.sequences==1231&&stats.banks==561&&stats.waveArchives==561&&song.valid&&song.samples.size()>1000&&songEnergy>0&&state.party.size()==1&&state.hasItem(4,5)&&started&&y.type==HgScriptYield::Type::Message&&y.messageId==9;
        std::cout<<"GAME LOOP REGRESSION\n"
                 <<"MapHeader table: "<<(headers?"540 ROM records":"FAIL")<<"\n"
                 <<"Script header 615: "<<(sh.valid?"valid":"FAIL")<<" transition="<<sh.transition<<" load="<<sh.load<<" resume="<<sh.resume<<" frameTrigger="<<triggered<<"\n"
                 <<"Route 29 encounter bank 1: "<<(enc.valid?"valid":"FAIL")<<" walkingRate="<<int(enc.walkingRate)<<" firstSpecies="<<slot.species<<"\n"
                 <<"Retail trainer bank: "<<(trainer.valid?"valid":"FAIL")<<" trainer1 party="<<trainer.party.size()<<" firstSpecies="<<(trainer.party.empty()?0:trainer.party[0].species)<<"\n"
                 <<"SDAT: files="<<stats.files<<" SSEQ="<<stats.sequences<<" SBNK="<<stats.banks<<" SWAR="<<stats.waveArchives<<" renderedSamples="<<song.samples.size()<<" energy="<<songEnergy<<"\n"
                 <<"Retail state: party="<<state.party.size()<<" bag(Poke Ball)="<<(state.hasItem(4,5)?"yes":"NO")<<" dexOwned="<<state.dexOwned.size()<<"\n"
                 <<"Native field VM original New Bark script: "<<(started&&y.type==HgScriptYield::Type::Message?"message yield":"FAIL")<<"\n";
        return ok?0:27;
    }
    if(newGameTest){
        auto temp=std::filesystem::temp_directory_path()/"hg_native_v09_newgame_test.sav";std::error_code ec;std::filesystem::remove(temp,ec);
        NativeGame ng(assets,temp);bool okAssets=ng.validate();
        pulse(ng,GameButton::Interact); // intro -> title
        pulse(ng,GameButton::Interact); // title -> main menu (NEW GAME selected without save)
        auto menuFrame=ng.render();bool menuOk=frameHasText(menuFrame,"NEW GAME")&&frameHasText(menuFrame,"MYSTERY GIFT")&&frameHasText(menuFrame,"CUSTOM SERVER");
        pulse(ng,GameButton::Interact); // enter new-game sequence: Oak must be first
        auto oakFirst=ng.render();bool oakFirstOk=!oakFirst.texts.empty()&&!frameHasText(oakFirst,"PC-PORT TEASER");
        // Advance Oak through gender confirmation and into the name-entry grid.
        for(int i=0;i<6;i++)pulse(ng,GameButton::Interact);
        auto nameFrame=ng.render();bool nameUiOk=frameHasText(nameFrame,"7 CHARACTERS")&&frameHasText(nameFrame,"SPACE")&&frameHasText(nameFrame,"DEL");
        pulse(ng,GameButton::Interact); // type A
        for(int i=0;i<4;i++)pulse(ng,GameButton::Down); // A -> Y (row 4)
        for(int i=0;i<4;i++)pulse(ng,GameButton::Right); // -> OK
        pulse(ng,GameButton::Interact); // accept A -> stage 8
        pulse(ng,GameButton::Interact); // name confirmation -> final Oak stage 9
        pulse(ng,GameButton::Interact); // final Oak message -> Red Gyarados TV stage 10
        auto tvAfterOak=ng.render();bool tvAfterOakOk=tvAfterOak.texts.empty();
        idleFrames(ng,350);              // TV footage -> protagonist shrink/fade
        idleFrames(ng,125);              // shrink/fade -> bedroom
        pulse(ng,GameButton::Debug);auto room=ng.render();bool roomOk=frameHasText(room,"MAP 64")&&frameHasText(room,"GLOBAL 7,6");
        HgRomWorld w(assets);bool world=w.initialize()&&w.loadMap(64,7,6);bool upstairsWarp=false,mom=false,frontDoor=false; if(world){for(auto const& q:w.events().warps)if(q.x==3&&q.y==4&&q.targetMap==63)upstairsWarp=true;} if(w.loadMap(63,3,4)){for(auto const& o:w.events().overworlds)if(o.x==6&&o.y==7&&o.scriptNumber==2)mom=true;for(auto const& q:w.events().warps)if(q.x==3&&q.y==10&&q.targetMap==60)frontDoor=true;}
        HgMessageBank house(load_hg_message_bank(assets,545));auto momMsg=house.decode(6,"ETHAN");bool msgOk=momMsg.valid&&momMsg.text.find("Professor Elm")!=std::string::npos;
        bool ok=okAssets&&menuOk&&oakFirstOk&&tvAfterOakOk&&nameUiOk&&roomOk&&upstairsWarp&&mom&&frontDoor&&msgOk;
        std::cout<<"OPENING / NEW GAME REGRESSION\n"
                 <<"Main menu Continue/New Game/Mystery Gift: "<<(menuOk?"PASS":"FAIL")<<"\n"
                 <<"Oak scene occurs before Red Gyarados footage: "<<(oakFirstOk?"PASS":"FAIL")<<"\n"
                 <<"Red Gyarados footage occurs after final Oak message: "<<(tvAfterOakOk?"PASS":"FAIL")<<"\n"
                 <<"Native 7-character name-entry grid: "<<(nameUiOk?"PASS":"FAIL")<<"\n"
                 <<"New game starts in player room map 64 @ 7,6: "<<(roomOk?"PASS":"FAIL")<<"\n"
                 <<"Upstairs -> downstairs original warp: "<<(upstairsWarp?"PASS":"FAIL")<<"\n"
                 <<"Mom original event script 2 @ 6,7: "<<(mom?"PASS":"FAIL")<<"\n"
                 <<"Front door original warp -> New Bark: "<<(frontDoor?"PASS":"FAIL")<<"\n"
                 <<"Mom gate message bank 545 / message 6: "<<(msgOk?"PASS":"FAIL")<<"\n";
        std::filesystem::remove(temp,ec);return ok?0:28;
    }
    if(pokegearTest){
        // v0.21 regression: validate the early-game ownership chain plus automated door/fade behavior with the
        // bundled retail event/script/message data and the native host.
        std::error_code ec;
        auto doorSave=std::filesystem::temp_directory_path()/"hg_native_v024_home_door_test.sav";
        auto exitSave=std::filesystem::temp_directory_path()/"hg_native_v024_home_exit_test.sav";
        auto momSave=std::filesystem::temp_directory_path()/"hg_native_v024_mom_pokegear_test.sav";
        std::filesystem::remove(doorSave,ec);std::filesystem::remove(exitSave,ec);std::filesystem::remove(momSave,ec);

        // 1) A save with an empty flag table must still be able to step onto the
        // New Bark Player House warp. primeRetailVisibilityFlags() derives the
        // overlapping Mom object's hide flag from event bank 57 at map load.
        {
            std::ofstream o(doorSave);
            o<<"HG_NATIVE_WORLD_V9\nmap=60\nx=695\ny=397\nfacing=3\nplay=0\nname=ETHAN\nnewgame=1\nmoney=3000\nvar=16646,2\n";
        }
        NativeGame doorGame(assets,doorSave);bool doorAssets=doorGame.validate();bool doorLoaded=doorAssets&&doorGame.load();
        bool autoDoorStep=false,fadeSeen=false;
        if(doorLoaded){
            pulse(doorGame,GameButton::Debug);
            pulse(doorGame,GameButton::Up);
            auto duringDoor=doorGame.render();
            // The first Up press starts the authored door/auto-walk sequence rather
            // than manually moving the player onto the warp immediately.
            autoDoorStep=frameHasText(duringDoor,"GLOBAL 695,397");
            idleFrames(doorGame,40);
            auto fadeFrame=doorGame.render();
            fadeSeen=frameHasDimText(fadeFrame,"NEW BARK TOWN",0.92f);
            idleFrames(doorGame,60);
        }
        auto doorFrame=doorGame.render();bool doorWarp=doorLoaded&&frameHasText(doorFrame,"MAP 63");

        // Exterior return: leaving Player House 1F must animate two additional
        // steps away from the outside door before field input is released.
        {
            std::ofstream o(exitSave);
            o<<"HG_NATIVE_WORLD_V9\nmap=63\nx=3\ny=9\nfacing=0\nplay=0\nname=ETHAN\nnewgame=1\nmoney=3000\nvar=16646,2\n";
        }
        NativeGame exitGame(assets,exitSave);bool exitLoaded=exitGame.validate()&&exitGame.load();
        if(exitLoaded){pulse(exitGame,GameButton::Debug);pulse(exitGame,GameButton::Down);idleFrames(exitGame,150);}
        auto exitFrame=exitGame.render();
        std::string exitGlobal;for(auto const& t:exitFrame.texts)if(t.text.rfind("GLOBAL ",0)==0){exitGlobal=t.text;break;}
        bool exitWalk=exitLoaded&&frameHasText(exitFrame,"MAP 60")&&frameHasText(exitFrame,"GLOBAL 695,398");

        // 2) Talk to Mom after the starter flag is set. Run the actual map-63
        // event script to completion and verify that its retail Pokégear flag is
        // persisted by the native save layer.
        {
            std::ofstream o(momSave);
            o<<"HG_NATIVE_WORLD_V9\nmap=63\nx=6\ny=8\nfacing=3\nplay=0\nname=ETHAN\nnewgame=1\ngotstarter=1\nstarter=155\nmoney=3000\nvar=16646,4\nflag=106\n";
        }
        NativeGame momGame(assets,momSave);bool momAssets=momGame.validate();bool momLoaded=momAssets&&momGame.load();
        if(momLoaded){pulse(momGame,GameButton::Interact);for(int i=0;i<96;i++){idleFrames(momGame,2);pulse(momGame,GameButton::Interact);}}
        bool momSaved=momLoaded&&momGame.save();bool gotPokegear=false;
        if(momSaved){std::ifstream in(momSave);std::string line;while(std::getline(in,line))if(line=="flag=156"){gotPokegear=true;break;}}

        // 3) The same retail New Bark west-exit script must observe the ownership
        // flag and register Professor Elm through command 146 (phone number add).
        HgGameState phoneState;phoneState.playerName="ETHAN";phoneState.gotStarter=true;phoneState.starter=155;phoneState.flags.insert(106);phoneState.flags.insert(156);
        HgScriptVm phoneVm;phoneVm.bindState(&phoneState);HgMessageBank nbMsgs(load_hg_message_bank(assets,542));
        bool phoneStarted=phoneVm.start(load_hg_script_bank(assets,842),3);bool phoneOk=false;
        for(int i=0;i<160&&phoneVm.active();++i){
            auto y=phoneVm.runUntilYield(&nbMsgs,10000);
            if(y.type==HgScriptYield::Type::PositionQuery){phoneVm.writeVar(y.a,695);phoneVm.writeVar(y.b,396);}
            if(y.type==HgScriptYield::Type::Choice)phoneVm.writeVar(y.a,y.choices.empty()?0:y.choices.front().value);
            if(y.type==HgScriptYield::Type::Unsupported||y.type==HgScriptYield::Type::Error)break;
        }
        phoneOk=phoneStarted&&!phoneState.phoneNumbers.empty();

        // 4) Card registration uses the retail SavePokegear encoding. Phone is
        // card 0 / base state, Map sets bit 0, and Radio sets bit 1. This catches
        // the old native bug that interpreted card IDs as arbitrary bit indices.
        auto runCardOpcode=[&](std::uint8_t card,std::uint32_t initialMask){
            std::vector<unsigned char> cardScript={2,0,0,0,0x13,0xfd,0x91,0x00,card,0x02,0x00};
            HgGameState st;st.pokegearCards=initialMask;HgScriptVm vm;vm.bindState(&st);bool started=vm.start(cardScript,1);
            if(started)for(int i=0;i<4&&vm.active();++i)vm.runUntilYield(nullptr,64);
            return std::pair<bool,std::uint32_t>{started,st.pokegearCards};
        };
        auto phoneCard=runCardOpcode(0,3),mapCard=runCardOpcode(1,0),radioCard=runCardOpcode(2,1);
        bool cardOk=phoneCard.first&&mapCard.first&&radioCard.first&&phoneCard.second==0&&mapCard.second==1&&radioCard.second==3;

        bool ok=doorAssets&&autoDoorStep&&fadeSeen&&doorWarp&&exitWalk&&momAssets&&gotPokegear&&phoneOk&&cardOk;
        std::cout<<"NEW BARK / POKEGEAR REGRESSION\n"
                 <<"Door approach is automated before the warp tile: "<<(autoDoorStep?"PASS":"FAIL")<<"\n"
                 <<"Building transition fades the complete field/HUD: "<<(fadeSeen?"PASS":"FAIL")<<"\n"
                 <<"New Bark home door usable from empty native flag state: "<<(doorWarp?"PASS":"FAIL")<<"\n"
                 <<"Interior -> exterior performs two clear-door steps: "<<(exitWalk?"PASS":"FAIL")<<" ["<<exitGlobal<<"]\n"
                 <<"Mom post-starter retail script sets Pokégear ownership: "<<(gotPokegear?"PASS":"FAIL")<<"\n"
                 <<"West-exit retail script registers Elm phone contact: "<<(phoneOk?"PASS":"FAIL")<<"\n"
                 <<"Pokégear Phone/Map/Radio card encoding matches retail: "<<(cardOk?"PASS":"FAIL")<<"\n";
        std::filesystem::remove(doorSave,ec);std::filesystem::remove(exitSave,ec);std::filesystem::remove(momSave,ec);return ok?0:30;
    }
    if(scriptTest){
        HgMessageBank nb(load_hg_message_bank(assets,542));
        HgMessageBank title(load_hg_message_bank(assets,719));
        auto town=nb.decode(34),touch=title.decode(0);
        HgScriptVm vm;bool started=vm.start(load_hg_script_bank(assets,842),2);HgScriptYield y;
        for(int i=0;i<16&&vm.active();i++){y=vm.runUntilYield(&nb);if(y.type==HgScriptYield::Type::Message||y.type==HgScriptYield::Type::Unsupported||y.type==HgScriptYield::Type::Error)break;}
        auto spoken=(y.type==HgScriptYield::Type::Message)?nb.decode(y.messageId):HgDecodedMessage{};
        bool ok=nb.valid()&&town.valid&&town.text.find("New Bark Town")!=std::string::npos&&touch.valid&&touch.text.find("TOUCH TO START")!=std::string::npos&&started&&y.type==HgScriptYield::Type::Message&&y.messageId==9&&spoken.valid;
        std::cout<<"HG message decode: "<<(town.valid?town.text:"FAILED")<<"\n"
                 <<"Title prompt: "<<(touch.valid?touch.text:"FAILED")<<"\n"
                 <<"New Bark script 2 first yield: "<<(started?"started":"FAILED")<<" message="<<(y.type==HgScriptYield::Type::Message?std::to_string(y.messageId):"none")<<" text="<<(spoken.valid?spoken.text:"<none>")<<"\n";
        return ok?0:23;
    }
    if(landTest){
        auto land=validate_land_narc(assets/"a/0/6/5");
        std::cout<<"LAND: "<<land.parsedMembers<<"/"<<land.members<<" members, "<<land.models<<" models, "<<land.triangles<<" triangles, "<<land.textures<<" textures, "<<land.membersWithCollision<<" BDHC blocks, "<<land.collisionPlates<<" collision plates, "<<land.exactModelCollisionBoundaries<<" exact BMD0->BDHC boundaries, failures="<<land.failures<<"\n";
        return (land.failures==0&&land.parsedMembers>0)?0:11;
    }
    if(assetTest){
        auto field=validate_nsbmd_narc(assets/"fielddata/build_model/bm_field.narc");
        auto room=validate_nsbmd_narc(assets/"fielddata/build_model/bm_room.narc");
        struct BindingStats{std::size_t bound=0,total=0,unboundNamed=0,unboundUnnamed=0;};
        auto bindingCount=[&](const std::filesystem::path& arc){BindingStats r;auto info=inspect_narc(arc);if(!info.valid)return r;for(std::size_t i=0;i<info.members.size();i++){auto m=load_nsbmd_from_narc(arc,i);if(!m.valid)continue;for(auto const& md:m.models)for(auto const& t:md.triangles){r.total++;if(t.textureIndex>=0&&static_cast<std::size_t>(t.textureIndex)<m.textures.size())r.bound++;else{bool named=t.materialIndex>=0&&static_cast<std::size_t>(t.materialIndex)<md.materialTextureNames.size()&&!md.materialTextureNames[static_cast<std::size_t>(t.materialIndex)].empty();if(named)r.unboundNamed++;else r.unboundUnnamed++;}}}return r;};
        auto fieldBinding=bindingCount(assets/"fielddata/build_model/bm_field.narc");
        auto roomBinding=bindingCount(assets/"fielddata/build_model/bm_room.narc");
        auto labScale=load_nsbmd_from_narc(assets/"fielddata/build_model/bm_field.narc",21);
        auto doorScale=load_nsbmd_from_narc(assets/"fielddata/build_model/bm_field.narc",26);
        auto signScale=load_nsbmd_from_narc(assets/"fielddata/build_model/bm_field.narc",29);
        auto scaleOf=[](const NsbmdMember& m){return (m.valid&&!m.models.empty())?m.models.front().normalizedScale:-1.0f;};
        bool scaleOk=std::fabs(scaleOf(labScale)-0.25f)<0.001f&&std::fabs(scaleOf(doorScale)-0.0625f)<0.001f&&std::fabs(scaleOf(signScale)-0.0625f)<0.001f;
        std::cout<<"PLACED MODEL SCALE: lab="<<scaleOf(labScale)<<" door="<<scaleOf(doorScale)<<" sign="<<scaleOf(signScale)<<" expected 0.25 / 0.0625 / 0.0625 => "<<(scaleOk?"PASS":"FAIL")<<"\n";
        std::cout<<"FIELD NSBMD: "<<field.parsedMembers<<"/"<<field.members<<" members, "<<field.models<<" models, "<<field.triangles<<" triangles, "<<field.textures<<" textures, failures="<<field.failures<<"\n";
        std::cout<<"FIELD embedded texture binding: "<<fieldBinding.bound<<"/"<<fieldBinding.total<<" triangles; unbound named="<<fieldBinding.unboundNamed<<" untextured/unnamed="<<fieldBinding.unboundUnnamed<<"\n";
        std::cout<<"ROOM  NSBMD: "<<room.parsedMembers<<"/"<<room.members<<" members, "<<room.models<<" models, "<<room.triangles<<" triangles, "<<room.textures<<" textures, failures="<<room.failures<<"\n";
        std::cout<<"ROOM embedded texture binding: "<<roomBinding.bound<<"/"<<roomBinding.total<<" triangles; unbound named="<<roomBinding.unboundNamed<<" untextured/unnamed="<<roomBinding.unboundUnnamed<<"\n";
        return (field.failures==0&&room.failures==0&&field.parsedMembers>0&&scaleOk)?0:6;
    }
    if(exportLand>=0){
        auto chunk=load_land_chunk(assets/"a/0/6/5",static_cast<std::size_t>(exportLand));
        if(!chunk.valid||chunk.model.models.empty()){std::cerr<<"Land model parse failed: "<<chunk.error<<"\n";return 12;}
        if(!export_land_model_obj(chunk,exportPath)){std::cerr<<"Land OBJ write failed\n";return 13;}
        std::cout<<"Exported land member "<<exportLand<<" model "<<chunk.model.models.front().name<<" with "<<chunk.model.models.front().triangles.size()<<" triangles; BMD0 offset="<<chunk.modelOffset<<" size="<<chunk.modelSize<<" BDHC bytes="<<chunk.collisionSize<<" to "<<exportPath<<"\n";
        return 0;
    }
    if(exportField>=0||exportRoom>=0){bool room=exportRoom>=0;int idx=room?exportRoom:exportField;auto arc=assets/(room?"fielddata/build_model/bm_room.narc":"fielddata/build_model/bm_field.narc");auto mem=load_nsbmd_from_narc(arc,static_cast<std::size_t>(idx));if(!mem.valid||mem.models.empty()){std::cerr<<"Model parse failed: "<<mem.error<<"\n";return 7;}if(!export_model_obj(mem.models.front(),exportPath)){std::cerr<<"OBJ write failed\n";return 8;}std::cout<<"Exported "<<mem.models.front().name<<" with "<<mem.models.front().triangles.size()<<" triangles to "<<exportPath<<"\n";return 0;}
    if(exportTexMember>=0){auto arc=assets/"fielddata/build_model/bm_field.narc";auto mem=load_nsbmd_from_narc(arc,static_cast<std::size_t>(exportTexMember));if(!mem.valid||exportTexIndex<0||static_cast<std::size_t>(exportTexIndex)>=mem.textures.size()){std::cerr<<"Texture parse/index failed\n";return 9;}auto const& t=mem.textures[static_cast<std::size_t>(exportTexIndex)];if(!export_texture_ppm(t,exportPath)){std::cerr<<"Texture write failed\n";return 10;}std::cout<<"Exported texture "<<t.name<<" "<<t.width<<"x"<<t.height<<" format "<<int(t.format)<<" to "<<exportPath<<"\n";return 0;}
    if(exportMmodelMember>=0){auto arc=assets/"a/0/8/1";auto mem=load_nitro_texture_from_narc(arc,static_cast<std::size_t>(exportMmodelMember));if(!mem.valid||exportMmodelTexIndex<0||static_cast<std::size_t>(exportMmodelTexIndex)>=mem.textures.size()){std::cerr<<"Mmodel texture parse/index failed: "<<mem.error<<"\n";return 14;}auto const& t=mem.textures[static_cast<std::size_t>(exportMmodelTexIndex)];if(!export_texture_ppm(t,exportPath)){std::cerr<<"Mmodel texture write failed\n";return 15;}std::cout<<"Exported mmodel member "<<exportMmodelMember<<" texture "<<t.name<<" "<<t.width<<"x"<<t.height<<" format "<<int(t.format)<<" to "<<exportPath<<"\n";return 0;}
    NativeGame game(assets,savePath); bool assetsOk=game.validate();
    if(!assetsOk){std::cerr<<"FATAL: retail asset validation failed; refusing to start the legacy fallback demo.\n";return 3;}
    if(validateOnly)return 0;
    if(fieldBenchmarkFrames>0){
        auto temp=std::filesystem::temp_directory_path()/"hg_native_field_benchmark.sav";
        {std::ofstream o(temp);o<<"HG_NATIVE_WORLD_V2\nmap="<<fieldBenchmarkMap<<"\nx="<<fieldBenchmarkX<<"\ny="<<fieldBenchmarkY<<"\nfacing=0\nplay=0\n";}
        NativeGame bench(assets,temp);bench.validate();if(!bench.load()){std::cerr<<"Field benchmark map load failed\n";return 38;}
        for(int i=0;i<3;i++)(void)bench.render();
        const auto t0=std::chrono::steady_clock::now();std::uint64_t checksum=0;
        for(int i=0;i<fieldBenchmarkFrames;i++){auto frame=bench.render();if(frame.hasPixels()){const auto step=std::max<std::size_t>(1,frame.rgba.size()/4096);for(std::size_t q=0;q<frame.rgba.size();q+=step)checksum+=frame.rgba[q];}}
        const auto t1=std::chrono::steady_clock::now();std::error_code ec;std::filesystem::remove(temp,ec);
        const double ms=std::chrono::duration<double,std::milli>(t1-t0).count();
        std::cout<<"Field render benchmark: "<<fieldBenchmarkFrames<<" frames in "<<ms<<" ms, "<<(ms/fieldBenchmarkFrames)<<" ms/frame, "<<(fieldBenchmarkFrames*1000.0/ms)<<" render FPS, checksum="<<checksum<<"\n";
        return 0;
    }
    if(battleTurnTest){bool ok=game.battleTurnSequenceTest();std::cout<<"Sequential battle turn: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:37;}
    if(battleRenderTest){bool ok=game.battleRenderVisibilityTest();std::cout<<"Battle Pokemon visible after final composition: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:38;}
    if(clerkTest){bool ok=game.fieldCounterInteractionTest();std::cout<<"Retail Poké Mart / Pokémon Center counter interaction: "<<(ok?"PASS":"FAIL")<<"\n";return ok?0:40;}
    if(!dumpBattleFrame.empty()){auto f=game.battleRenderRegressionFrame();bool ok=dumpFramePpm(f,dumpBattleFrame);std::cout<<"Battle regression frame: "<<(ok?"WROTE ":"FAILED ")<<dumpBattleFrame<<"\n";return ok?0:39;}
    if(!dumpTitleFrame.empty()){pulse(game,GameButton::Interact);auto f=game.render();if(!dumpFramePpm(f,dumpTitleFrame)){std::cerr<<"Title frame dump failed\n";return 24;}std::cout<<"Dumped retail-asset title frame (pixels="<<(f.hasPixels()?"yes":"NO")<<", overlays="<<f.rects.size()<<") to "<<dumpTitleFrame<<"\n";return 0;}
    if(!dumpOpeningFrame.empty()){idleFrames(game,std::max(0,int(dumpOpeningSeconds*60.0)));auto f=game.render();if(!dumpFramePpm(f,dumpOpeningFrame)){std::cerr<<"Opening frame dump failed\n";return 29;}std::cout<<"Dumped opening frame at "<<dumpOpeningSeconds<<"s to "<<dumpOpeningFrame<<"\n";return 0;}
    if(!dumpMainMenuFrame.empty()){pulse(game,GameButton::Interact);pulse(game,GameButton::Interact);auto f=game.render();if(!dumpFramePpm(f,dumpMainMenuFrame)){std::cerr<<"Main menu frame dump failed\n";return 30;}std::cout<<"Dumped Continue/New Game/Mystery Gift menu to "<<dumpMainMenuFrame<<"\n";return 0;}
    if(!dumpNewGameFrame.empty()){auto temp=std::filesystem::temp_directory_path()/"hg_native_dump_newgame.sav";std::error_code ec;std::filesystem::remove(temp,ec);NativeGame ng(assets,temp);ng.validate();pulse(ng,GameButton::Interact);pulse(ng,GameButton::Interact);pulse(ng,GameButton::Interact);auto f=ng.render();std::filesystem::remove(temp,ec);if(!dumpFramePpm(f,dumpNewGameFrame)){std::cerr<<"New game frame dump failed\n";return 31;}std::cout<<"Dumped requested Red Gyarados new-game teaser to "<<dumpNewGameFrame<<"\n";return 0;}
    if(dumpMapId>=0&&!dumpMapFrame.empty()){
        auto temp=std::filesystem::temp_directory_path()/"hg_native_render_map.sav";
        {std::ofstream o(temp);o<<"HG_NATIVE_WORLD_V2\nmap="<<dumpMapId<<"\nx="<<dumpMapX<<"\ny="<<dumpMapY<<"\nfacing=0\nplay=0\n";}
        NativeGame mapGame(assets,temp);
        mapGame.validate();bool loaded=mapGame.load();auto f=mapGame.render();std::error_code ec;std::filesystem::remove(temp,ec);
        if(!loaded||!dumpFramePpm(f,dumpMapFrame)){std::cerr<<"Map frame dump failed\n";return 22;}
        std::cout<<"Dumped map "<<dumpMapId<<" at "<<dumpMapX<<","<<dumpMapY<<" (Z framebuffer="<<(f.hasPixels()?"yes":"NO")<<") to "<<dumpMapFrame<<"\n";return 0;
    }
    if(!dumpDemoFrame.empty()){auto temp=std::filesystem::temp_directory_path()/"hg_native_dump_world.sav";{std::ofstream o(temp);o<<"HG_NATIVE_WORLD_V2\nmap=60\nx=688\ny=398\nfacing=0\nplay=0\n";}NativeGame dg(assets,temp);dg.validate();dg.load();auto f=dg.render();std::error_code ec;std::filesystem::remove(temp,ec);if(!dumpFramePpm(f,dumpDemoFrame)){std::cerr<<"Frame dump failed\n";return 16;}std::cout<<"Dumped ROM-world frame (software Z framebuffer="<<(f.hasPixels()?"yes":"NO")<<", UI rects="<<f.rects.size()<<") to "<<dumpDemoFrame<<"\n";return 0;}
    if(!dumpTerrainFrame.empty()){auto temp=std::filesystem::temp_directory_path()/"hg_native_dump_terrain.sav";{std::ofstream o(temp);o<<"HG_NATIVE_WORLD_V2\nmap=60\nx=688\ny=398\nfacing=0\nplay=0\n";}NativeGame tg(assets,temp);tg.validate();tg.load();pulse(tg,GameButton::Terrain);auto f=tg.render();std::error_code ec;std::filesystem::remove(temp,ec);if(!dumpFramePpm(f,dumpTerrainFrame)){std::cerr<<"Terrain frame dump failed\n";return 17;}std::cout<<"Dumped textured terrain frame with "<<f.rects.size()<<" rectangles to "<<dumpTerrainFrame<<"\n";return 0;}
    if(!dumpSpriteFrame.empty()){auto temp=std::filesystem::temp_directory_path()/"hg_native_dump_sprite.sav";{std::ofstream o(temp);o<<"HG_NATIVE_WORLD_V2\nmap=60\nx=688\ny=398\nfacing=0\nplay=0\n";}NativeGame sg(assets,temp);sg.validate();sg.load();pulse(sg,GameButton::Menu);for(int i=0;i<3;i++)pulse(sg,GameButton::Down);pulse(sg,GameButton::Interact);auto f=sg.render();std::error_code ec;std::filesystem::remove(temp,ec);if(!dumpFramePpm(f,dumpSpriteFrame)){std::cerr<<"Sprite frame dump failed\n";return 18;}std::cout<<"Dumped original sprite viewer frame with "<<f.rects.size()<<" rectangles to "<<dumpSpriteFrame<<"\n";return 0;}
    if(logicTest){
        auto temp=std::filesystem::temp_directory_path()/"hg_native_logic_v09.sav";{std::ofstream o(temp);o<<"HG_NATIVE_WORLD_V2\nmap=60\nx=688\ny=398\nfacing=0\nplay=0\n";}
        NativeGame lg(assets,temp);lg.validate();bool loaded=lg.load();
        idleFrames(lg,180);pulse(lg,GameButton::Debug);auto idleFrame=lg.render();
        bool stayedStill=frameHasText(idleFrame,"GLOBAL 688,398")&&frameHasText(idleFrame,"MAP 60  MATRIX 0  LAND 0");
        bool cameraBound=frameHasText(idleFrame,"CAMERA TYPE 0")&&frameHasText(idleFrame,"RETAIL SCALE 240 PX/U");
        pulse(lg,GameButton::Save);auto savePrompt=lg.render();bool saveScreen=frameHasText(savePrompt,"SAVE THE GAME?");pulse(lg,GameButton::Interact);pulse(lg,GameButton::Interact);
        bool worldSave=false;{std::ifstream sf(temp);std::string sig;if(std::getline(sf,sig))worldSave=sig=="HG_NATIVE_WORLD_V10";}
        auto frame=lg.render();auto sprite69=load_nitro_texture_from_narc(assets/"a/0/8/1",69);auto sprite70=load_nitro_texture_from_narc(assets/"a/0/8/1",70);bool spriteBrowseWorked=sprite69.valid&&sprite70.valid&&!sprite69.textures.empty()&&!sprite70.textures.empty();
        pulse(lg,GameButton::Assets);auto viewer=lg.render();pulse(lg,GameButton::Assets);pulse(lg,GameButton::Terrain);auto terrain=lg.render();bool terrainIsActive=frameHasText(terrain,"ACTIVE ROM WORLD CHUNK")&&frameHasText(terrain,"LAND 0");
        std::error_code ec;std::filesystem::remove(temp,ec);
        std::cout<<"Logic test OK\n"<<"No-input ROM position stable: "<<(stayedStill?"yes":"NO")<<"\n"<<"MapHeader camera binding: "<<(cameraBound?"yes":"NO")<<"\n"<<"Retail-style save confirmation screen: "<<(saveScreen?"yes":"NO")<<"\n"<<"ROM-world V10 save written: "<<(worldSave?"yes":"NO")<<"\n"<<"Sprite archive navigation: "<<(spriteBrowseWorked?"yes":"NO")<<"\n"<<"Active ROM chunk inspector: "<<(terrainIsActive?"yes":"NO")<<"\n";
        return (loaded&&stayedStill&&cameraBound&&saveScreen&&worldSave&&spriteBrowseWorked&&terrainIsActive&&frame.hasPixels())?0:5;
    }
    if(noController) {
#ifdef _WIN32
        _putenv_s("HG_DISABLE_CONTROLLER","1");
#else
        setenv("HG_DISABLE_CONTROLLER","1",1);
#endif
    }
    VulkanXcbRenderer r; if(!r.init(1280,720,"Pokemon HeartGold - Native Vulkan v0.32 Battle + Audio Fix"))return 4;
    return r.run(game);
}
