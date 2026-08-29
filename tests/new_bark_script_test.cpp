#include "game/hg_script.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

static void u16(std::vector<unsigned char>& b,std::uint16_t v){b.push_back(v&255);b.push_back(v>>8);}
static void u32(std::vector<unsigned char>& b,std::uint32_t v){for(int i=0;i<4;i++)b.push_back((v>>(i*8))&255);}
static std::vector<unsigned char> bank(){std::vector<unsigned char>b;u32(b,4);u16(b,0xfd13);u16(b,0);return b;}

int main(){
    HgScriptYield y{};

    // T20 Cameron standard-message bank: GetStdMsgNaix 2 must resolve to
    // NARC_msg_msg_0030_bin (member 30), then the external message command
    // must preserve that member and the authored message number.
    HgGameState cam;HgScriptVm cv;cv.bindState(&cam);auto c=bank();
    u16(c,438);u16(c,2);u16(c,0x800c);
    u16(c,440);u16(c,0x800c);u16(c,3);u16(c,2);
    assert(cv.start(c,1));y=cv.runUntilYield();
    assert(y.type==HgScriptYield::Type::Message&&y.opcode==440&&y.a==30&&y.messageId==3);
    cv.stop();

    // Player bedroom PC path (T20R0202): the availability query must write a
    // nonzero result and ScrCmd_376 must suspend into the native PC app.
    HgGameState pc;HgScriptVm pv;pv.bindState(&pc);auto p=bank();
    u16(p,377);u16(p,0x800c);u16(p,376);u16(p,2);
    assert(pv.start(p,1));y=pv.runUntilYield();
    assert(pc.var(0x800c)==1);assert(y.type==HgScriptYield::Type::AppCommand&&y.opcode==376);
    pv.stop();

    // Mom's four-item savings menu uses MenuInit/MenuItemAdd/MenuExec with
    // 255 as the unconditional-enable value and result values 0..3.
    HgGameState mom;HgScriptVm mv;mv.bindState(&mom);auto m=bank();
    u16(m,795);u16(m,1);u16(m,1);
    u16(m,750);m.push_back(1);m.push_back(1);m.push_back(0);m.push_back(1);u16(m,0x800c);
    for(std::uint16_t i=0;i<4;i++){u16(m,751);u16(m,std::uint16_t(29+i));u16(m,255);u16(m,i);}
    u16(m,752);u16(m,2);
    assert(mv.start(m,1));y=mv.runUntilYield();
    assert(y.type==HgScriptYield::Type::Choice&&y.opcode==752&&y.a==0x800c&&y.choices.size()==4);
    for(std::size_t i=0;i<4;i++){assert(y.choices[i].messageId==29+i);assert(y.choices[i].value==i);}
    mv.stop();

    // Mom's bank state must transfer the exact selected amount in both
    // directions and use the retail wallet/bank-full query polarity.
    HgGameState bankState;bankState.money=3000;bankState.momSavings=500;HgScriptVm bv;bv.bindState(&bankState);auto b=bank();
    u16(b,41);u16(b,0x4000);u16(b,1000);
    u16(b,793);u16(b,1);u16(b,0x4000); // deposit
    u16(b,838);u16(b,1);u16(b,0x4001); // bank full?
    u16(b,41);u16(b,0x4000);u16(b,250);
    u16(b,793);u16(b,0);u16(b,0x4000); // withdraw
    u16(b,2);
    assert(bv.start(b,1));y=bv.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);
    assert(bankState.money==2250&&bankState.momSavings==1250&&bankState.var(0x4001)==0);

    // Friend room Shiny Leaf/certificate path: count, grant crown, and then
    // return 6 as the retail crown-complete sentinel used by T20R0402.
    HgGameState leaf;assert(leaf.giveMon(152,5));leaf.party[0].shinyLeaves=5;HgScriptVm lv;lv.bindState(&leaf);auto l=bank();
    u16(l,825);u16(l,0);u16(l,0x4000);
    u16(l,826);u16(l,0);
    u16(l,825);u16(l,0);u16(l,0x4001);
    u16(l,425);u16(l,2);u16(l,2);
    assert(lv.start(l,1));y=lv.runUntilYield();
    assert(leaf.var(0x4000)==5&&leaf.party[0].shinyLeafCrown&&leaf.var(0x4001)==6);
    assert(y.type==HgScriptYield::Type::AppCommand&&y.opcode==425&&y.a==2);
    lv.stop();

    // Cameron/photo state is persistent script-visible data.
    HgGameState photos;HgScriptVm phv;phv.bindState(&photos);auto ph=bank();
    u16(ph,615);u16(ph,0);u16(ph,616);u16(ph,0x4000);u16(ph,618);u16(ph,0x4001);u16(ph,2);
    assert(phv.start(ph,1));y=phv.runUntilYield();assert(y.type==HgScriptYield::Type::AppCommand&&y.opcode==615);assert(photos.savedPhotos==1);
    y=phv.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(photos.var(0x4000)==1&&photos.var(0x4001)==0);

    // T20 special Elm-2F route helpers must decode to the host without
    // desynchronizing the command stream.
    HgGameState special;special.giveMon(152,5);special.followerEnabled=true;special.followerPartySlot=0;special.followerSpecies=152;HgScriptVm sv;sv.bindState(&special);auto s=bank();
    u16(s,729);u16(s,0x800c);u16(s,596);u16(s,0x800c);u16(s,600);u16(s,582);u16(s,60);u16(s,688);u16(s,393);u16(s,2);
    assert(sv.start(s,1));assert(special.var(0x800c)==0);
    y=sv.runUntilYield();assert(y.type==HgScriptYield::Type::ObjectCommand&&y.opcode==596);
    // 729 executed before the yield and reported a present follower.
    assert(special.var(0x800c)==1);
    y=sv.runUntilYield();assert(y.type==HgScriptYield::Type::ObjectCommand&&y.opcode==600);
    y=sv.runUntilYield();assert(y.type==HgScriptYield::Type::ObjectCommand&&y.opcode==582&&y.a==60&&y.b==688&&y.c==393);
    y=sv.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);

    std::cout<<"new_bark_script_test: PASS\n";
}
