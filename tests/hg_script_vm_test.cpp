#include "game/hg_script.hpp"
#include "assets/pokemon_data.hpp"
#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>
static void u16(std::vector<unsigned char>& b, std::uint16_t v){b.push_back(v&255);b.push_back(v>>8);} 
static void u32(std::vector<unsigned char>& b, std::uint32_t v){for(int i=0;i<4;i++)b.push_back((v>>(8*i))&255);} 
int main(int argc,char** argv){
  if(argc>1) assert(hg_initialize_pokemon_database(argv[1]));
  std::vector<unsigned char> b; u32(b,4); u16(b,0xfd13); u16(b,0);
  // SETVAR 0x4000, 5
  u16(b,41);u16(b,0x4000);u16(b,5);
  // compare var to literal
  u16(b,17);u16(b,0x4000);u16(b,5);
  // GoToIf EQ over SETVAR 0x4001=99 (6 bytes)
  u16(b,28);b.push_back(1);u32(b,6);
  u16(b,41);u16(b,0x4001);u16(b,99);
  // Wait 30 frames using 0x4002
  u16(b,3);u16(b,30);u16(b,0x4002);
  // movement player with rel=0 to movement data immediately after End? Just test yield operands.
  u16(b,94);u16(b,255);u32(b,2); // after rel, skip End opcode to movement record
  u16(b,2);
  u16(b,12);u16(b,2);u16(b,254);u16(b,0);

  HgGameState st;HgScriptVm vm;vm.bindState(&st);assert(vm.start(b,1));
  auto y=vm.runUntilYield();
  assert(y.type==HgScriptYield::Type::WaitFrames && y.a==30 && y.b==0x4002);
  assert(st.var(0x4000)==5);assert(st.var(0x4001)==0);assert(st.var(0x4002)==30);
  st.setVar(0x4002,0);
  y=vm.runUntilYield();
  assert(y.type==HgScriptYield::Type::Movement && y.a==255 && y.rel==2);
  y=vm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);
  // Register semantics + flag check + Yes/No
  std::vector<unsigned char> c;u32(c,4);u16(c,0xfd13);u16(c,0);
  u16(c,4);c.push_back(0);c.push_back(7);u16(c,5);c.push_back(1);u32(c,0x12345678);u16(c,30);u16(c,0x120);
  u16(c,32);u16(c,0x120);u16(c,63);u16(c,0x4003);u16(c,2);
  assert(vm.start(c,1));y=vm.runUntilYield();assert(y.type==HgScriptYield::Type::Choice && y.a==0x4003 && y.choices.size()==2);assert(vm.scriptRegister(0)==7&&vm.scriptRegister(1)==0x12345678);assert(vm.flags().count(0x120));

  // SetPtrByte must write the literal byte, not interpret it as a register.
  std::vector<unsigned char> d;u32(d,4);u16(d,0xfd13);u16(d,0);
  u16(d,8);u32(d,0x100);d.push_back(0xAB);
  u16(d,6);d.push_back(2);u32(d,0x100);u16(d,2);
  assert(vm.start(d,1));y=vm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(vm.scriptRegister(2)==0xAB);

  // 16-badge bitfield and party queries.
  HgGameState st2;st2.giveMon(152,5);HgScriptVm vm2;vm2.bindState(&st2);
  std::vector<unsigned char> e;u32(e,4);u16(e,0xfd13);u16(e,0);
  u16(e,295);u16(e,15);u16(e,294);u16(e,15);u16(e,0x4010);u16(e,296);u16(e,0x4011);
  u16(e,354);u16(e,0);u16(e,0x4012);u16(e,356);u16(e,0x4013);u16(e,386);u16(e,0x4014);u16(e,2);
  vm2.setInteractionContext(-1,-1,3);assert(vm2.start(e,1));y=vm2.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);
  assert(st2.var(0x4010)==1&&st2.var(0x4011)==1&&st2.badgeFlags==0x8000);assert(st2.var(0x4012)==152&&st2.var(0x4013)==1&&st2.var(0x4014)==3);

  // Warp/object-placement operand decoding must preserve authored values.
  std::vector<unsigned char> f;u32(f,4);u16(f,0xfd13);u16(f,0);u16(f,176);u16(f,60);u16(f,0);u16(f,6);u16(f,11);u16(f,2);u16(f,2);
  assert(vm2.start(f,1));y=vm2.runUntilYield();assert(y.type==HgScriptYield::Type::FieldTransition&&y.opcode==176&&y.a==60&&y.c==6&&y.d==11&&y.word==2);
  std::vector<unsigned char> g;u32(g,4);u16(g,0xfd13);u16(g,0);u16(g,339);u16(g,4);u16(g,7);u16(g,0);u16(g,9);u16(g,1);u16(g,2);
  assert(vm2.start(g,1));y=vm2.runUntilYield();assert(y.type==HgScriptYield::Type::ObjectCommand&&y.opcode==339&&y.a==4&&y.b==7&&y.c==0&&y.d==9&&y.word==1);


  // Expanded inventory/buffer and field-state command coverage.
  HgGameState st3;HgScriptVm vm3;vm3.bindState(&st3);
  std::vector<unsigned char> h;u32(h,4);u16(h,0xfd13);u16(h,0);
  u16(h,130);u16(h,4);u16(h,0x4020);                 // GetItemPocket POKE BALL
  u16(h,194);h.push_back(0);u16(h,4);               // Buffer item name
  u16(h,197);h.push_back(1);u16(h,57);              // Buffer SURF
  u16(h,400);h.push_back(1);                         // Strength on
  u16(h,400);h.push_back(2);u16(h,0x4021);          // Strength query
  u16(h,2);
  assert(vm3.start(h,1));y=vm3.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);
  assert(st3.var(0x4020)==hg_item_pocket(4));assert(st3.var(0x4021)==1);
  assert(st3.formatSlots[0]=="POKE BALL"&&st3.formatSlots[1]=="SURF");

  // GetFriendSprite stores the actual opposite-gender SPRITE_* graphics ID.
  // HERO=0 and HEROINE=97; writing 1 here would incorrectly select BABYBOY1.
  std::vector<unsigned char> friendGfx;u32(friendGfx,4);u16(friendGfx,0xfd13);u16(friendGfx,0);
  u16(friendGfx,144);u16(friendGfx,0x4021);u16(friendGfx,2);
  HgGameState maleState;maleState.female=false;HgScriptVm maleVm;maleVm.bindState(&maleState);
  assert(maleVm.start(friendGfx,1));y=maleVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(maleState.var(0x4021)==97);
  HgGameState femaleState;femaleState.female=true;HgScriptVm femaleVm;femaleVm.bindState(&femaleState);
  assert(femaleVm.start(friendGfx,1));y=femaleVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(femaleState.vars.count(0x4021)&&femaleState.var(0x4021)==0);

  // Party overflow feeds the PC storage system instead of losing a Pokemon.
  HgGameState st4;for(int i=0;i<6;i++)assert(st4.giveMon(std::uint16_t(152+i),5));
  assert(st4.party.size()==6);assert(st4.giveMon(25,7));assert(st4.pcStorage.size()==1&&st4.pcStorage[0].species==25);

  // Daycare is persistent gameplay state: deposit, step growth/egg progress, withdraw.
  HgGameState st5;assert(st5.giveMon(152,5));assert(st5.giveMon(155,5));assert(st5.putMonInDaycare(0));assert(st5.putMonInDaycare(0));
  for(int i=0;i<256;i++){ st5.onPlayerStep(); }
  assert(st5.daycare[0].mon.level==6&&st5.daycare[1].mon.level==6&&st5.daycareEggReady);
  assert(st5.retrieveDaycareMon(0));assert(!st5.party.empty());


  // Retail CheckFlag/CheckTrainerFlag are boolean tests.  GoToIf TRUE (1)
  // must branch when the flag is SET, while GoToIf FALSE (0) must branch when
  // it is UNSET.  v0.16 interpreted 1 as numeric EQ and inverted Elm's early
  // story branches, skipping ChooseStarter and reaching later dialogue.
  auto makeFlagBranch=[](std::uint8_t cond)->std::vector<unsigned char>{
    std::vector<unsigned char> q;u32(q,4);u16(q,0xfd13);u16(q,0);
    u16(q,32);u16(q,0x006A);                 // CheckFlag FLAG_GOT_STARTER
    u16(q,28);q.push_back(cond);u32(q,8);    // branch over SetVar+End
    u16(q,41);u16(q,0x4030);u16(q,1);u16(q,2);
    u16(q,41);u16(q,0x4030);u16(q,2);u16(q,2);
    return q;
  };
  HgGameState flagState;HgScriptVm flagVm;flagVm.bindState(&flagState);
  auto flagTrue=makeFlagBranch(1);
  assert(flagVm.start(flagTrue,1));y=flagVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(flagState.var(0x4030)==1); // unset: TRUE must not branch
  flagState.setVar(0x4030,0);flagState.flags.insert(0x006A);
  assert(flagVm.start(flagTrue,1));y=flagVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(flagState.var(0x4030)==2); // set: TRUE branches
  auto flagFalse=makeFlagBranch(0);flagState.setVar(0x4030,0);flagState.flags.erase(0x006A);
  assert(flagVm.start(flagFalse,1));y=flagVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(flagState.var(0x4030)==2); // unset: FALSE branches
  flagState.setVar(0x4030,0);flagState.flags.insert(0x006A);
  assert(flagVm.start(flagFalse,1));y=flagVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(flagState.var(0x4030)==1); // set: FALSE does not

  // Trainer flags use the same retail boolean branch mode.
  std::vector<unsigned char> trainerBool;u32(trainerBool,4);u16(trainerBool,0xfd13);u16(trainerBool,0);
  u16(trainerBool,38);u16(trainerBool,25);u16(trainerBool,28);trainerBool.push_back(1);u32(trainerBool,8);
  u16(trainerBool,41);u16(trainerBool,0x4031);u16(trainerBool,1);u16(trainerBool,2);
  u16(trainerBool,41);u16(trainerBool,0x4031);u16(trainerBool,2);u16(trainerBool,2);
  HgGameState trainerState;trainerState.trainerFlags.insert(25);HgScriptVm trainerVm;trainerVm.bindState(&trainerState);
  assert(trainerVm.start(trainerBool,1));y=trainerVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(trainerState.var(0x4031)==2);

  // Elm's Lab retail high-number command family must decode with exact operand
  // widths instead of stopping/desynchronizing immediately after ChooseStarter.
  HgGameState st6;HgScriptVm vm6;vm6.bindState(&st6);
  std::vector<unsigned char> i;u32(i,4);u16(i,0xfd13);u16(i,0);
  u16(i,609);                         // lab/follower preamble
  u16(i,621);                         // PlaceStarterBallsInElmsLab
  u16(i,605);i.push_back(3);i.push_back(2);
  u16(i,602);u16(i,0);               // follower movement off
  u16(i,608);
  u16(i,603);
  u16(i,604);u16(i,55);
  u16(i,746);                         // TouchscreenMenuHide
  u16(i,748);u16(i,0x4000);          // GetMenuChoice
  u16(i,747);                         // TouchscreenMenuShow
  u16(i,131);u16(i,152);             // SetStarterChoice CHIKORITA
  u16(i,132);i.push_back(12);i.push_back(13); // GenderMsgBox
  u16(i,2);
  assert(vm6.start(i,1));
  const std::uint16_t elmOps[]={609,621,605,602,608,603,604,746};
  for(auto op:elmOps){y=vm6.runUntilYield();assert(y.type==HgScriptYield::Type::ObjectCommand&&y.opcode==op);}
  y=vm6.runUntilYield();assert(y.type==HgScriptYield::Type::Choice&&y.opcode==748&&y.a==0x4000);vm6.writeVar(0x4000,0);
  y=vm6.runUntilYield();assert(y.type==HgScriptYield::Type::ObjectCommand&&y.opcode==747);
  y=vm6.runUntilYield();assert(y.type==HgScriptYield::Type::Message&&y.opcode==132&&y.messageId==12);
  assert(st6.starter==152&&st6.gotStarter);y=vm6.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);


  // CallStd crosses into a different retail script bank and Return resumes the
  // caller with its original bank/message context. RunScript launches a child context;\n  // this native VM serializes that child while preserving the parent continuation.
  std::vector<unsigned char> stdParent;u32(stdParent,4);u16(stdParent,0xfd13);u16(stdParent,0);
  u16(stdParent,20);u16(stdParent,2000); // CallStd
  u16(stdParent,41);u16(stdParent,0x4040);u16(stdParent,7);u16(stdParent,2);
  std::vector<unsigned char> stdChild;u32(stdChild,4);u16(stdChild,0xfd13);u16(stdChild,0);
  u16(stdChild,41);u16(stdChild,0x4041);u16(stdChild,9);u16(stdChild,27); // Return
  HgGameState stdState;HgScriptVm stdVm;stdVm.bindState(&stdState);
  assert(stdVm.start(stdParent,1,100));y=stdVm.runUntilYield();
  assert(y.type==HgScriptYield::Type::CommonScript&&y.a==2000&&y.flag&&stdVm.messageBankId()==100);
  assert(stdVm.enterExternalBank(stdChild,1,200,true));assert(stdVm.externalDepth()==1&&stdVm.messageBankId()==200);
  y=stdVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);
  assert(stdState.var(0x4041)==9&&stdState.var(0x4040)==7&&stdVm.messageBankId()==100&&stdVm.externalDepth()==0);

  std::vector<unsigned char> gotoParent;u32(gotoParent,4);u16(gotoParent,0xfd13);u16(gotoParent,0);
  u16(gotoParent,19);u16(gotoParent,2000); // RunScript
  u16(gotoParent,41);u16(gotoParent,0x4042);u16(gotoParent,99);u16(gotoParent,2);
  HgScriptVm gotoVm;gotoVm.bindState(&stdState);assert(gotoVm.start(gotoParent,1,101));y=gotoVm.runUntilYield();
  assert(y.type==HgScriptYield::Type::CommonScript&&!y.flag);assert(gotoVm.enterExternalBank(stdChild,1,201,false));
  y=gotoVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);assert(stdState.var(0x4042)==99&&gotoVm.messageBankId()==101&&gotoVm.externalDepth()==0);

  // Pokémon Center service path: HealParty restores HP/status/PP, and the
  // following retail DebugWatch/DummyGetVar opcode is a synchronous no-op.
  HgGameState healState;assert(healState.giveMon(152,5));
  healState.party[0].hp=1;healState.party[0].status=1;
  for(std::size_t pi=0;pi<healState.party[0].pp.size();++pi)healState.party[0].pp[pi]=0;
  healState.setVar(0x4050,123);HgScriptVm healVm;healVm.bindState(&healState);
  std::vector<unsigned char> healScript;u32(healScript,4);u16(healScript,0xfd13);u16(healScript,0);
  u16(healScript,282);u16(healScript,437);u16(healScript,0x4050);u16(healScript,2);
  assert(healVm.start(healScript,1));y=healVm.runUntilYield();assert(y.type==HgScriptYield::Type::Finished);
  assert(healState.party[0].hp==healState.party[0].maxHp&&healState.party[0].status==0);
  for(std::size_t pi=0;pi<healState.party[0].pp.size();++pi)assert(healState.party[0].pp[pi]==healState.party[0].maxPp[pi]);

  std::cout << "hg_script_vm_test: PASS\n";
}
