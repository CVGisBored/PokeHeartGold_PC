#include "game/hg_script.hpp"
#include "assets/hg_text.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <chrono>
#include <cctype>
#include <ctime>

namespace {
std::uint16_t at16(const std::vector<unsigned char>& b,std::size_t p){return std::uint16_t(b[p])|(std::uint16_t(b[p+1])<<8);}
std::uint32_t at32(const std::vector<unsigned char>& b,std::size_t p){return std::uint32_t(b[p])|(std::uint32_t(b[p+1])<<8)|(std::uint32_t(b[p+2])<<16)|(std::uint32_t(b[p+3])<<24);}
std::uint16_t popcount16(std::uint16_t v){std::uint16_t n=0;for(;v;v=std::uint16_t(v&(v-1)))++n;return n;}
}

std::uint8_t HgScriptVm::read8(){if(!need(1))return 0;return bank_[pc_++];}
std::uint16_t HgScriptVm::read16(){if(!need(2))return 0;auto v=at16(bank_,pc_);pc_+=2;return v;}
std::uint32_t HgScriptVm::read32(){if(!need(4))return 0;auto v=at32(bank_,pc_);pc_+=4;return v;}
std::int32_t HgScriptVm::readS32(){return static_cast<std::int32_t>(read32());}
std::unordered_map<std::uint16_t,std::uint16_t>& HgScriptVm::vars(){return state_?state_->vars:localVars_;}
const std::unordered_map<std::uint16_t,std::uint16_t>& HgScriptVm::vars() const{return state_?state_->vars:localVars_;}
std::unordered_set<std::uint16_t>& HgScriptVm::flags(){return state_?state_->flags:localFlags_;}
const std::unordered_set<std::uint16_t>& HgScriptVm::flags() const{return state_?state_->flags:localFlags_;}
void HgScriptVm::clearPersistentState(){
    active_=false;bank_.clear();entries_.clear();stack_.clear();externalStack_.clear();scratchMemory_.clear();data_.fill(0);choiceOptions_.clear();
    if(state_){state_->vars.clear();state_->flags.clear();state_->trainerFlags.clear();}else{localVars_.clear();localFlags_.clear();}
    pc_=startPc_=0;scriptNumber_=0;messageBankId_=0xffff;compare_=0;booleanCondition_=false;booleanResult_=false;lastObjectId_=backgroundId_=-1;playerDirection_=0;
}
std::uint16_t HgScriptVm::varValue(std::uint16_t id) const{if(id<0x4000)return id;if(state_)return state_->var(id);auto it=localVars_.find(id);return it==localVars_.end()?0:it->second;}
void HgScriptVm::setVar(std::uint16_t id,std::uint16_t v){if(id<0x4000)return;if(state_)state_->setVar(id,v);else localVars_[id]=v;}
bool HgScriptVm::condition(std::uint8_t c) const{
    // CheckFlag and CheckTrainerFlag feed boolean TRUE/FALSE branches in the
    // retail macro layer.  Their condition bytes are 1/0 respectively; they
    // must not be interpreted as EQ/LT from the numeric comparison table.
    if(booleanCondition_&&(c==0||c==1))return c==1?booleanResult_:!booleanResult_;
    switch(c){case 0:return compare_<0;case 1:return compare_==0;case 2:return compare_>0;case 3:return compare_<=0;case 4:return compare_>=0;case 5:return compare_!=0;default:return false;}
}
bool HgScriptVm::branchRelative(std::int32_t rel){std::int64_t q=std::int64_t(pc_)+rel;if(q<0||q>=std::int64_t(bank_.size()))return false;pc_=std::size_t(q);return true;}

bool HgScriptVm::loadBank(const std::vector<unsigned char>& bank,std::uint16_t scriptNumber,std::uint16_t messageBankId){
    bank_=bank;entries_.clear();stack_.clear();pc_=startPc_=0;scriptNumber_=scriptNumber;messageBankId_=messageBankId;active_=false;
    if(bank_.size()<6||scriptNumber==0)return false;
    for(std::size_t p=0;p+2<=bank_.size();p+=4){
        if(at16(bank_,p)==0xfd13) break;
        if(p+4>bank_.size()) return false;
        std::int32_t rel=static_cast<std::int32_t>(at32(bank_,p));std::int64_t dest=std::int64_t(p)+4+rel;
        if(dest<0||dest>=std::int64_t(bank_.size())) return false;
        entries_.push_back(std::size_t(dest));
        if(entries_.size()>4096) return false;
    }
    const std::size_t i=std::size_t(scriptNumber-1);if(i>=entries_.size())return false;
    pc_=startPc_=entries_[i];active_=true;return true;
}

bool HgScriptVm::start(const std::vector<unsigned char>& bank,std::uint16_t eventScriptNumber,std::uint16_t messageBankId){
    externalStack_.clear();scratchMemory_.clear();data_.fill(0);choiceOptions_.clear();choiceDestination_=0;choiceCancel_=false;choiceCursor_=0;
    compare_=0;booleanCondition_=false;booleanResult_=false;active_=false;
    return loadBank(bank,eventScriptNumber,messageBankId);
}

bool HgScriptVm::enterExternalBank(const std::vector<unsigned char>& bank,std::uint16_t scriptNumber,std::uint16_t messageBankId,bool waitForChild){
    if(!active_||bank.empty()||scriptNumber==0)return false;
    // In retail these commands allocate a sibling ScriptContext inside the same
    // ScriptEnvironment (up to three contexts). Preserve the parent regardless
    // of RunScript/CallStd so End in the child cannot discard the map script.
    // The host may execute the child serially around blocking UI, but the state
    // lifetime and return boundary match the retail context model.
    if(stack_.size()+externalStack_.size()+1>=20)return false;
    BankContext c;c.bank=std::move(bank_);c.entries=std::move(entries_);c.stack=std::move(stack_);c.pc=pc_;c.startPc=startPc_;c.scriptNumber=scriptNumber_;c.messageBankId=messageBankId_;c.waitForChild=waitForChild;
    externalStack_.push_back(std::move(c));
    if(!loadBank(bank,scriptNumber,messageBankId)){
        restoreExternalCaller();
        return false;
    }
    return true;
}

void HgScriptVm::restoreExternalCaller(){
    if(externalStack_.empty())return;
    auto c=std::move(externalStack_.back());externalStack_.pop_back();
    bank_=std::move(c.bank);entries_=std::move(c.entries);stack_=std::move(c.stack);pc_=c.pc;startPc_=c.startPc;scriptNumber_=c.scriptNumber;messageBankId_=c.messageBankId;active_=true;
}

HgScriptYield HgScriptVm::error(std::uint16_t opcode,const std::string& detail){active_=false;HgScriptYield y;y.type=HgScriptYield::Type::Error;y.opcode=opcode;y.detail=detail;return y;}
HgScriptYield HgScriptVm::unsupported(std::uint16_t opcode,const HgMessageBank*,const std::string& why){
    // Do not scan forward looking for a message. That v0.10 recovery heuristic could
    // land in operands/movement data and permanently desynchronise the bytecode PC.
    active_=false;HgScriptYield y;y.type=HgScriptYield::Type::Unsupported;y.opcode=opcode;y.detail=why.empty()?"unsupported HG field opcode "+std::to_string(opcode):why;return y;
}

HgScriptYield HgScriptVm::runUntilYield(const HgMessageBank* messages,std::size_t budget){
    if(!active_){HgScriptYield y;y.type=HgScriptYield::Type::Finished;return y;}
    for(std::size_t steps=0;steps<budget&&active_;steps++){
        if(!need(2))return error(0xffff,"script pointer left bank");
        const std::uint16_t op=read16();
        switch(op){
            case 0: case 1: break;
            case 2:{
                // End stops only the current retail ScriptContext. If this VM is
                // executing a standard-script child, restore the preserved parent.
                if(!externalStack_.empty()){restoreExternalCaller();break;}
                active_=false;HgScriptYield y;y.type=HgScriptYield::Type::Finished;y.opcode=op;return y;
            }
            case 3:{ // Wait(time, countdown variable)
                if(!need(4)) return error(op,"truncated Wait");
                HgScriptYield y; y.type=HgScriptYield::Type::WaitFrames; y.opcode=op; y.a=read16(); y.b=read16(); setVar(y.b,y.a); return y;
            }
            case 4:{if(!need(2))return error(op,"truncated LoadByte");auto r=read8(),v=read8();if(r<data_.size())data_[r]=v;break;}
            case 5:{if(!need(5))return error(op,"truncated LoadWord");auto r=read8();auto v=read32();if(r<data_.size())data_[r]=v;break;}
            case 6:{if(!need(5))return error(op,"truncated LoadByteFromAddr");auto r=read8();auto a=read32();if(r<data_.size())data_[r]=scratchMemory_[a];break;}
            case 7:{if(!need(5))return error(op,"truncated WriteByteToAddr");auto a=read32();auto r=read8();scratchMemory_[a]=std::uint8_t(r<data_.size()?data_[r]:0);break;}
            case 8:{if(!need(5))return error(op,"truncated SetPtrByte");auto a=read32();auto v=read8();scratchMemory_[a]=v;break;}
            case 9:{if(!need(2))return error(op,"truncated CopyLocal");auto d=read8(),s=read8();if(d<data_.size()&&s<data_.size())data_[d]=data_[s];break;}
            case 10:{if(!need(8))return error(op,"truncated CopyByte");auto d=read32(),s=read32();scratchMemory_[d]=scratchMemory_[s];break;}
            case 11:{if(!need(2))return error(op,"truncated CompareLocalToLocal");auto a=read8(),b=read8();setCompare(a<data_.size()?data_[a]:0,b<data_.size()?data_[b]:0);break;}
            case 12:{if(!need(2))return error(op,"truncated CompareLocalToValue");auto a=read8(),b=read8();setCompare(a<data_.size()?data_[a]:0,b);break;}
            case 13:{if(!need(5))return error(op,"truncated CompareLocalToAddr");auto r=read8();auto a=read32();setCompare(r<data_.size()?data_[r]:0,scratchMemory_[a]);break;}
            case 14:{if(!need(5))return error(op,"truncated CompareAddrToLocal");auto a=read32();auto r=read8();setCompare(scratchMemory_[a],r<data_.size()?data_[r]:0);break;}
            case 15:{if(!need(5))return error(op,"truncated CompareAddrToValue");auto a=read32();auto v=read8();setCompare(scratchMemory_[a],v);break;}
            case 16:{if(!need(8))return error(op,"truncated CompareAddrToAddr");auto a=read32(),b=read32();setCompare(scratchMemory_[a],scratchMemory_[b]);break;}
            case 17:{if(!need(4))return error(op,"truncated CompareVarToValue");auto a=varValue(read16()),b=read16();setCompare(a,b);break;}
            case 18:{if(!need(4))return error(op,"truncated CompareVarToVar");auto a=varValue(read16()),b=varValue(read16());setCompare(a,b);break;}
            case 19: case 20:{if(!need(2))return error(op,"truncated common script command");HgScriptYield y;y.type=HgScriptYield::Type::CommonScript;y.opcode=op;y.a=read16();y.flag=(op==20);return y;}
            case 21:{ // Yield to the parent script context. In a single-context host this is a one-frame yield.
                HgScriptYield y;y.type=HgScriptYield::Type::WaitFrames;y.opcode=op;y.a=1;return y;
            }
            case 22:{if(!need(4))return error(op,"truncated GoTo");auto rel=readS32();if(!branchRelative(rel))return error(op,"GoTo target outside bank");break;}
            case 23:{if(!need(5))return error(op,"truncated ObjectGoTo");auto id=read8();auto rel=readS32();if(lastObjectId_==id&&!branchRelative(rel))return error(op,"ObjectGoTo target outside bank");break;}
            case 24:{if(!need(5))return error(op,"truncated BGGoTo");auto id=read8();auto rel=readS32();if(backgroundId_==id&&!branchRelative(rel))return error(op,"BGGoTo target outside bank");break;}
            case 25:{if(!need(5))return error(op,"truncated DirectionGoTo");auto dir=read8();auto rel=readS32();if(playerDirection_==dir&&!branchRelative(rel))return error(op,"DirectionGoTo target outside bank");break;}
            case 26:{if(!need(4))return error(op,"truncated Call");auto rel=readS32();if(stack_.size()+externalStack_.size()+1>=20)return error(op,"retail script call stack overflow");auto ret=pc_;if(!branchRelative(rel))return error(op,"Call target outside bank");stack_.push_back(ret);break;}
            case 27:{
                if(!stack_.empty()){pc_=stack_.back();stack_.pop_back();break;}
                if(!externalStack_.empty()){restoreExternalCaller();break;}
                active_=false;HgScriptYield y;y.type=HgScriptYield::Type::Finished;y.opcode=op;return y;
            }
            case 28:{if(!need(5))return error(op,"truncated GoToIf");auto c=read8();auto rel=readS32();if(condition(c)&&!branchRelative(rel))return error(op,"GoToIf target outside bank");break;}
            case 29:{if(!need(5))return error(op,"truncated CallIf");auto c=read8();auto rel=readS32();if(condition(c)){if(stack_.size()+externalStack_.size()+1>=20)return error(op,"retail script call stack overflow");auto ret=pc_;if(!branchRelative(rel))return error(op,"CallIf target outside bank");stack_.push_back(ret);}break;}
            case 30:flags().insert(read16());break;
            case 31:flags().erase(read16());break;
            case 32:{auto id=read16();booleanCondition_=true;booleanResult_=flags().count(id)!=0;break;}
            case 33:{auto id=varValue(read16());flags().insert(id);break;}
            case 34:{auto id=varValue(read16());flags().erase(id);break;}
            case 35:{if(!need(4))return error(op,"truncated CheckFlagVar");auto id=varValue(read16()),dst=read16();setVar(dst,flags().count(id)?1:0);break;}
            case 36:{auto id=varValue(read16());if(state_)state_->trainerFlags.insert(id);break;}
            case 37:{auto id=varValue(read16());if(state_)state_->trainerFlags.erase(id);break;}
            case 38:{auto id=varValue(read16());booleanCondition_=true;booleanResult_=state_&&state_->trainerFlags.count(id)!=0;break;}
            case 39:{auto dst=read16(),v=varValue(read16());setVar(dst,std::uint16_t(varValue(dst)+v));break;}
            case 40:{auto dst=read16(),v=varValue(read16());setVar(dst,std::uint16_t(varValue(dst)-v));break;}
            case 41:{auto dst=read16(),v=read16();setVar(dst,v);break;}
            case 42:{auto dst=read16(),src=read16();setVar(dst,varValue(src));break;}
            case 43:{auto dst=read16(),src=read16();setVar(dst,varValue(src));break;}
            case 44: case 45:{if(!need(1))return error(op,"truncated message");HgScriptYield y;y.type=HgScriptYield::Type::Message;y.messageId=read8();y.npcMessage=(op==45);y.opcode=op;return y;}
            case 46: case 47:{if(!need(2))return error(op,"truncated variable message");HgScriptYield y;y.type=HgScriptYield::Type::Message;y.messageId=varValue(read16());y.npcMessage=(op==47);y.opcode=op;return y;}
            case 48:{if(!need(1))return error(op,"truncated ScrCmd_048");HgScriptYield y;y.type=HgScriptYield::Type::Message;y.messageId=read8();y.npcMessage=true;y.opcode=op;return y;}
            case 49: case 50: case 51:{HgScriptYield y;y.type=HgScriptYield::Type::WaitInput;y.opcode=op;return y;}
            case 52: case 53: case 54:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 55:{if(!need(6))return error(op,"truncated DirectionSignpost");HgScriptYield y;y.type=HgScriptYield::Type::Message;y.opcode=op;y.messageId=read8();y.a=read8();y.b=read16();y.c=read16();y.npcMessage=false;return y;}
            case 56:{if(!need(3))return error(op,"truncated text board");read8();read16();break;}
            case 57:{if(!need(1))return error(op,"truncated board process");read8();break;}
            case 58:break; // native board animation wait; instantaneous in desktop renderer
            case 59:{if(!need(3))return error(op,"truncated TrainerTips");HgScriptYield y;y.type=HgScriptYield::Type::Message;y.messageId=read8();y.a=read16();y.opcode=op;y.npcMessage=false;setVar(y.a,1);return y;}
            case 60:{if(!need(2))return error(op,"truncated CloseBoard");HgScriptYield y;y.type=HgScriptYield::Type::WaitInput;y.opcode=op;y.a=read16();return y;}
            case 61:break;
            case 62:{if(!need(6))return error(op,"truncated ScrCmd_062");for(int i=0;i<6;i++)read8();break;}
            case 63:{if(!need(2))return error(op,"truncated YesNo");HgScriptYield y;y.type=HgScriptYield::Type::Choice;y.opcode=op;y.a=read16();y.flag=true;y.choices={{0,0},{0,1}};return y;}
            case 64: case 65:{if(!need(6))return error(op,"truncated multi setup");read8();read8();choiceCursor_=read8();choiceCancel_=read8()!=0;choiceDestination_=read16();choiceOptions_.clear();break;}
            case 66:{if(!need(2))return error(op,"truncated AddMultiOption");HgScriptChoiceOption o;o.messageId=read8();o.value=read8();choiceOptions_.push_back(o);break;}
            case 67:{HgScriptYield y;y.type=HgScriptYield::Type::Choice;y.opcode=op;y.a=choiceDestination_;y.b=choiceCursor_;y.flag=choiceCancel_;y.choices=choiceOptions_;return y;}
            case 68: case 69:{if(!need(6))return error(op,"truncated list setup");read8();read8();choiceCursor_=read8();choiceCancel_=read8()!=0;choiceDestination_=read16();choiceOptions_.clear();break;}
            case 70:{if(!need(6))return error(op,"truncated AddListOption");HgScriptChoiceOption o;o.messageId=read16();read16();o.value=read16();choiceOptions_.push_back(o);break;}
            case 71:{HgScriptYield y;y.type=HgScriptYield::Type::Choice;y.opcode=op;y.a=choiceDestination_;y.b=choiceCursor_;y.flag=choiceCancel_;y.choices=choiceOptions_;return y;}
            case 72:{if(!need(1))return error(op,"truncated MultiColumn");read8();break;}
            case 73:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 74:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 75:{HgScriptYield y;y.type=HgScriptYield::Type::WaitSound;y.opcode=op;y.a=varValue(read16());return y;}
            case 76:{if(!need(4))return error(op,"truncated PlayCry");HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=varValue(read16());y.b=read16();return y;}
            case 77:{HgScriptYield y;y.type=HgScriptYield::Type::WaitSound;y.opcode=op;return y;}
            case 78:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 79:{HgScriptYield y;y.type=HgScriptYield::Type::WaitSound;y.opcode=op;return y;}
            case 80:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 81:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 82:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;return y;}
            case 83:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 84:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();y.b=read16();return y;}
            case 85:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 86:{if(!need(2))return error(op,"truncated music pause status");read8();read8();break;}
            case 87:{HgScriptYield y;y.type=HgScriptYield::Type::Sound;y.opcode=op;y.a=read16();return y;}
            case 88:{if(!need(1))return error(op,"truncated BGM flag");read8();break;}
            case 89: case 90:{auto dst=read16();setVar(dst,0);break;}
            case 91: case 92: case 93:break;
            case 94:{if(!need(6))return error(op,"truncated ApplyMovement");HgScriptYield y;y.type=HgScriptYield::Type::Movement;y.opcode=op;y.a=varValue(read16());y.rel=readS32();return y;}
            case 95:{HgScriptYield y;y.type=HgScriptYield::Type::WaitMovement;y.opcode=op;return y;}
            case 96: case 97:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 98: case 99: case 100: case 101:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 102:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;}
            case 103: case 104:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 105:{HgScriptYield y;y.type=HgScriptYield::Type::PositionQuery;y.opcode=op;y.a=read16();y.b=read16();return y;}
            case 106:{HgScriptYield y;y.type=HgScriptYield::Type::PositionQuery;y.opcode=op;y.a=varValue(read16());y.b=read16();y.c=read16();return y;}
            case 107:{if(!need(6))return error(op,"truncated SetFollowingOverworld");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());return y;}
            case 108:{if(!need(3))return error(op,"truncated KeepOverworld");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=read8();return y;}
            case 109:{if(!need(4))return error(op,"truncated SetOWMovement");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;}
            case 110:{auto amount=read32();if(state_)state_->money=std::min<std::uint32_t>(999999,state_->money+amount);break;}
            case 111:{auto amount=read32();if(state_)state_->money=(amount>state_->money)?0:state_->money-amount;break;}
            case 112:{auto dst=read16();auto amount=read32();setVar(dst,(state_&&state_->money>=amount)?1:0);break;}
            case 113:{if(!need(4))return error(op,"truncated ShowMoneyBox");read16();read16();break;}
            case 114: case 115:break;
            case 116:{if(!need(5))return error(op,"truncated special currency box");read8();read16();read16();break;}
            case 117:break;
            case 118:{read8();break;}
            case 119:{auto dst=read16();setVar(dst,state_?state_->coins:0);break;}
            case 120:{auto amount=varValue(read16());if(state_)state_->coins=std::uint16_t(std::min<unsigned>(50000,unsigned(state_->coins)+amount));break;}
            case 121:{auto amount=varValue(read16());if(state_)state_->coins=std::uint16_t(amount>state_->coins?0:state_->coins-amount);break;}
            case 122:{auto amount=varValue(read16());if(state_)state_->athletePoints=std::uint16_t(std::min<unsigned>(9999,unsigned(state_->athletePoints)+amount));break;}
            case 123:{auto amount=varValue(read16());if(state_)state_->athletePoints=std::uint16_t(amount>state_->athletePoints?0:state_->athletePoints-amount);break;}
            case 124:{auto amount=varValue(read16()),dst=read16();setVar(dst,(state_&&state_->athletePoints>=amount)?1:0);break;}
            case 125: case 126: case 127: case 128:{auto item=varValue(read16()),qty=varValue(read16()),dst=read16();bool ok=false;if(state_){if(op==125)ok=state_->addItem(item,qty);else if(op==126)ok=state_->takeItem(item,qty);else if(op==127){auto it=state_->bag.find(item);const unsigned limit=hg_item_pocket(item)==3?99u:999u;ok=item&&qty&&((it!=state_->bag.end()&&unsigned(it->second)+qty<=limit)||(it==state_->bag.end()&&qty<=limit&&state_->bag.size()<600));}else ok=state_->hasItem(item,qty);}setVar(dst,ok?1:0);break;}
            case 129:{auto item=varValue(read16()),dst=read16();setVar(dst,(item>=328&&item<=435)?1:0);break;}
            case 130:{auto item=varValue(read16());auto dst=read16();setVar(dst,hg_item_pocket(item));break;}
            case 131:{
                auto starter=varValue(read16());
                if(state_){state_->starter=starter;state_->gotStarter=starter!=0;}
                break;
            }
            case 132:{
                if(!need(2))return error(op,"truncated GenderMsgBox");
                auto male=read8(),female=read8();
                HgScriptYield y;y.type=HgScriptYield::Type::Message;y.opcode=op;
                y.messageId=(state_&&state_->female)?female:male;y.npcMessage=false;return y;
            }
            case 133:{auto seal=varValue(read16()),dst=read16();setVar(dst,state_&&state_->seals.count(seal)?state_->seals[seal]:0);break;}
            case 134:{auto seal=varValue(read16()),amount=varValue(read16());if(state_){auto &q=state_->seals[seal];if(amount&0x8000){auto n=std::uint16_t(-std::int16_t(amount));q=n>q?0:std::uint16_t(q-n);}else q=std::uint16_t(std::min<unsigned>(99,unsigned(q)+amount));}break;}
            case 135:{read16();read16();auto dst=read16();setVar(dst,1);break;}
            case 136:{read16();auto dst=read16();setVar(dst,0);break;}
            case 137:{auto species=varValue(read16());auto level=varValue(read16());auto held=varValue(read16());auto form=varValue(read16());auto ability=varValue(read16());auto dst=read16();bool ok=state_&&state_->giveMon(species,std::uint8_t(level),held,std::uint8_t(form),std::uint8_t(ability));setVar(dst,ok?1:0);break;}
            case 138:{auto species=varValue(read16());read16();if(state_&&state_->party.size()<6){state_->giveMon(species,1);state_->party.back().egg=true;}break;}
            case 139:{auto mon=varValue(read16()),slot=varValue(read16()),move=varValue(read16());if(state_&&mon<state_->party.size()&&slot<4)state_->party[mon].moves[slot]=move;break;}
            case 140:{auto dst=read16(),move=varValue(read16()),slot=varValue(read16());bool has=false;if(state_&&slot<state_->party.size())for(auto m:state_->party[slot].moves)if(m==move)has=true;setVar(dst,has?1:0);break;}
            case 141:{auto dst=read16(),move=varValue(read16());std::uint16_t found=6;if(state_)for(std::size_t i=0;i<state_->party.size();i++){for(auto m:state_->party[i].moves)if(m==move){found=std::uint16_t(i);break;}if(found!=6)break;}setVar(dst,found);break;}
            case 142:{read16();auto dst=read16();setVar(dst,0);break;}
            case 143:{read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 144:{
                // GetFriendSprite writes an overworld SPRITE_* graphics ID into an
                // object-GFX variable.  HERO is 0 and HEROINE is 97; v0.33 wrote 1
                // for Lyra, which is SPRITE_BABYBOY1 and caused scripted VAR sprites
                // to resolve to the wrong actor.
                auto dst=read16();setVar(dst,state_&&state_->female?0:97);break;
            }
            case 145:{auto card=read8();if(state_)state_->registerPokegearCard(card);break;}
            case 146:{auto num=varValue(read16());if(state_)state_->phoneNumbers.insert(num);break;}
            case 147:{auto num=varValue(read16()),dst=read16();setVar(dst,state_&&state_->phoneNumbers.count(num)?1:0);break;}
            case 148:{read8();read8();break;}
            case 149:{
                auto trigger=read8();
                if(state_)state_->phoneCallState.erase(trigger);
                break;
            }
            case 150: case 151: case 152: case 156: case 157: case 159: case 160: case 161: case 162: case 164:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 153:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 154:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());return y;}
            case 155: case 165:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;}
            case 158:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=read8();return y;}
            case 163:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=read16();return y;}
            case 166:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 167:{HgScriptYield y;y.type=HgScriptYield::Type::StarterChoice;y.opcode=op;return y;}
            case 168:{auto dst=read16();setVar(dst,0);break;}
            case 169:{read16();read16();break;}
            case 170:{auto dst=read16();setVar(dst,0);break;}
            case 171:{read16();auto dst=read16();setVar(dst,0);break;}
            case 172:{auto dst=read16();setVar(dst,0);HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=dst;return y;}
            case 173:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());y.b=read16();return y;}
            case 174:{HgScriptYield y;y.type=HgScriptYield::Type::FieldTransition;y.opcode=op;y.a=read16();y.b=read16();y.c=read16();y.d=read16();return y;}
            case 175:{HgScriptYield y;y.type=HgScriptYield::Type::WaitFrames;y.opcode=op;y.a=1;return y;}
            case 176:{HgScriptYield y;y.type=HgScriptYield::Type::FieldTransition;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());y.d=varValue(read16());y.word=varValue(read16());return y;}
            case 177: case 178: case 179: case 182:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 180:{read16();read16();read16();break;}
            case 181:break;
            case 183:{read16();break;}
            case 184:{auto dst=read16();setVar(dst,state_&&state_->onBike?1:0);break;}
            case 185:{auto v=read8();if(state_)state_->onBike=(v!=0);break;}
            case 186:{auto v=read8();if(state_)state_->bikeLocked=(v==1);break;}
            case 187:{auto dst=read16();setVar(dst,state_&&state_->onBike?1:0);break;}
            case 188:{read16();break;}
            case 189:break;
            case 190:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=state_->playerName;break;}
            case 191:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=state_->rivalName;break;}
            case 192:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=state_->friendName.empty()?(state_->female?"ETHAN":"LYRA"):state_->friendName;break;}
            case 193:{auto slot=read8(); auto pos=varValue(read16());if(state_&&slot<state_->formatSlots.size()&&pos<state_->party.size()){auto const&m=state_->party[pos];state_->formatSlots[slot]=m.nickname.empty()?hg_species_name(m.species):m.nickname;}break;}
            case 194:{auto slot=read8(); auto item=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_item_name(item);break;}
            case 195:{auto slot=read8(); auto pocket=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_pocket_name(pocket);break;}
            case 196:{auto slot=read8(); auto tmhm=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_item_name(tmhm);break;}
            case 197:{auto slot=read8(); auto move=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_move_name(move);break;}
            case 198:{auto slot=read8(); auto value=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=std::to_string(value);break;}
            case 199:{auto slot=read8(); auto pos=varValue(read16());if(state_&&slot<state_->formatSlots.size()&&pos<state_->party.size()){auto const&m=state_->party[pos];state_->formatSlots[slot]=m.nickname.empty()?hg_species_name(m.species):m.nickname;}break;}
            case 200:{auto slot=read8();read16();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="TRAINER";break;}
            case 201:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="TRAINER";break;}
            case 202:{auto slot=read8(); auto species=varValue(read16());read16();read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_species_name(species);break;}
            case 203:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_species_name(state_->starter);break;}
            case 204:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_species_name(387);break;}
            case 205:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_species_name(390);break;}
            case 206:{auto dst=read16();setVar(dst,state_?state_->starter:0);break;}
            case 207:{auto slot=read8(); auto deco=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="DECORATION "+std::to_string(deco);break;}
            case 208: case 209:{read8();read16();break;}
            case 210:{auto slot=read8(); auto location=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="LOCATION "+std::to_string(location);break;}
            case 211:{read16();read16();break;}
            case 212:{auto dst=read16();setVar(dst,lastObjectId_>=0?std::uint16_t(lastObjectId_):0);break;}
            case 213:{if(!need(6))return error(op,"truncated TrainerBattle");HgScriptYield y;y.type=HgScriptYield::Type::Battle;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=read8();y.d=read8();return y;}
            case 214:{read16();read16();break;}
            case 215: case 216:{read16();read16();read16();break;}
            case 217:{auto dst=read16();setVar(dst,0);break;}
            case 218:{read16();break;}
            case 219:{HgScriptYield y;y.type=HgScriptYield::Type::FieldTransition;y.opcode=op;return y;}
            case 220:{auto dst=read16();setVar(dst,state_&&state_->lastBattleWon?1:0);break;}
            case 221:{auto dst=read16();read8();setVar(dst,state_&&state_->lastBattleWon?1:0);break;}
            case 222:{auto dst=read16();setVar(dst,state_&&state_->aliveParty()>=2?1:0);break;}
            case 223: case 224:break;
            case 225:{auto rel=readS32();if(lastObjectId_>=0&&state_&&state_->trainerFlags.count(std::uint16_t(lastObjectId_))&&!branchRelative(rel))return error(op,"trainer branch outside bank");break;}
            case 226: case 227:{read16();read16();read16();read16();break;}
            case 228: case 229: case 232: case 233: case 235: case 236:{read16();break;}
            case 230: case 231: case 237:break;
            case 234:{read16();read16();read16();read16();break;}
            case 238:{auto dst=read16();setVar(dst,0);break;}
            case 239:{auto slot=varValue(read16()),dst=read16();setVar(dst,state_&&slot<state_->party.size()?state_->party[slot].gender:2);break;}
            case 240:{auto map=varValue(read16()),warp=varValue(read16()),x=varValue(read16()),y=varValue(read16()),facing=varValue(read16());if(state_){state_->dynamicWarp={true,map,warp,x,y,facing};}break;}
            case 241:{auto dst=read16();setVar(dst,state_&&state_->dynamicWarp.valid?state_->dynamicWarp.warp:0);break;}
            case 242:{read8();read8();read16();read16();break;}
            case 243:{auto dst=read16();std::uint16_t n=0;if(state_)for(auto id:state_->dexSeen)if(id<=251)n++;setVar(dst,n);break;}
            case 244:{auto dst=read16();std::uint16_t n=0;if(state_)for(auto id:state_->dexOwned)if(id<=251)n++;setVar(dst,n);break;}
            case 245: case 246:{auto dst=read16();setVar(dst,state_?std::uint16_t(op==245?state_->dexSeen.size():state_->dexOwned.size()):0);break;}
            case 247:break;
            case 248:{read8();auto msg=read16(),fan=read16();setVar(msg,0);setVar(fan,0);break;}
            case 249: case 250:{if(!need(4))return error(op,"truncated scripted wild battle");HgScriptYield y;y.type=HgScriptYield::Type::WildBattle;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.flag=(op==250);return y;}
            case 251:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 252:break;
            case 253:{auto dst=read16();setVar(dst,3);break;}
            case 254:{if(!need(2))return error(op,"truncated SaveGameNormal");HgScriptYield y;y.type=HgScriptYield::Type::Save;y.opcode=op;y.a=read16();return y;}
            case 275: case 276: case 277: case 278:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 279:{HgScriptYield y;y.type=HgScriptYield::Type::FieldTransition;y.opcode=op;return y;}
            case 280:{auto spawn=varValue(read16());if(state_)state_->spawnId=spawn;break;}
            case 281:{auto dst=read16();setVar(dst,state_&&state_->female?1:0);break;}
            case 282:{ // HealParty: retail Pokémon Center/field healing command.
                if(state_)for(auto& m:state_->party){
                    m.hp=m.maxHp;m.status=0;
                    for(std::size_t i=0;i<m.pp.size();++i)m.pp[i]=m.maxPp[i];
                }
                break;
            }
            case 290:{auto dst=read16();setVar(dst,state_&&state_->pokedex?1:0);break;}
            case 291:if(state_)state_->pokedex=true;break;
            case 292:{auto dst=read16();setVar(dst,state_&&state_->runningShoes?1:0);break;}
            case 293:if(state_)state_->runningShoes=true;break;
            case 294:{auto badge=varValue(read16()),dst=read16();setVar(dst,state_&&badge<16&&((state_->badgeFlags>>badge)&1)?1:0);break;}
            case 295:{auto badge=varValue(read16());if(state_&&badge<16){state_->badgeFlags|=std::uint16_t(1u<<badge);state_->badges=std::uint8_t(popcount16(state_->badgeFlags));}break;}
            case 296:{auto dst=read16();setVar(dst,state_?popcount16(state_->badgeFlags):0);break;}
            case 297:{read16();break;}
            case 298:break;
            case 299:{auto dst=read16();setVar(dst,state_&&state_->escortMode?1:0);break;}
            case 300:if(state_)state_->escortMode=true;break;
            case 301:if(state_)state_->escortMode=false;break;
            case 302:{auto dst=read16();setVar(dst,state_&&state_->stepTaken?1:0);break;}
            case 303:if(state_)state_->stepTaken=true;break;
            case 304:if(state_)state_->stepTaken=false;break;
            case 305:{auto dst=read16();setVar(dst,state_&&state_->gameCleared?1:0);break;}
            case 306:if(state_)state_->gameCleared=true;break;
            case 307:{read16();read16();read16();read16();read8();break;}
            case 308: case 309: case 310: case 311:{read8();break;}
            case 312:{if(state_){for(std::size_t i=0;i<state_->daycare.size()&&i<state_->formatSlots.size();i++){auto const&d=state_->daycare[i];state_->formatSlots[i]=d.occupied?(d.mon.nickname.empty()?hg_species_name(d.mon.species):d.mon.nickname):"";}}break;}
            case 313:{auto dst=read16();std::uint16_t n=0;if(state_)for(auto const&d:state_->daycare)if(d.occupied)n++;setVar(dst,n);break;}
            case 314: case 315: case 316:break;
            case 317:{read8();break;}
            case 318: case 320: case 323: case 324: case 325: case 326:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 319:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 321:{auto a=read8(),b=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=b;return y;}
            case 322:{auto can=read8();auto dst=read16();(void)can;setVar(dst,0);break;} // VermilionGymCanCheck; gym-state overlay supplies the retail answer when hosted
            case 327: case 328:{read8();break;}
            case 329: case 330: case 331:break;
            case 332:{auto dst=read16();setVar(dst,state_?std::uint16_t(state_->party.size()):0);break;}
            case 338:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());return y;}
            case 339:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());y.d=varValue(read16());y.word=varValue(read16());return y;}
            case 340: case 341:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;}
            case 342: case 343:{read16();read16();read16();break;}
            case 345: case 346:break;
            case 347:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 349:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 350:break;
            case 351:{auto dst=read16();setVar(dst,state_?state_->var(0x800c):0xff);break;}
            case 352:{if(!need(5))return error(op,"truncated PokemonSummaryScreen");HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=read8();y.b=varValue(read16());y.c=read16();return y;}
            case 353:{auto moveSlot=read8(); auto dst=read16();std::uint16_t out=0xff;if(state_){auto partySlot=state_->var(0x800c);if(partySlot<state_->party.size()&&moveSlot<4)out=state_->party[partySlot].moves[moveSlot];}setVar(dst,out);break;}
            case 354:{auto slot=varValue(read16()),dst=read16();setVar(dst,state_&&slot<state_->party.size()?state_->party[slot].species:0);break;}
            case 355:{auto slot=varValue(read16()),dst=read16();setVar(dst,state_&&slot<state_->party.size()&&state_->party[slot].mine?1:0);break;}
            case 356:{auto dst=read16();std::uint16_t n=0;if(state_)for(auto const& m:state_->party)if(!m.egg)n++;setVar(dst,n);break;}
            case 357:{auto dst=read16(),except=varValue(read16());std::uint16_t n=0;if(state_)for(std::size_t i=0;i<state_->party.size();i++)if(i!=except&&!state_->party[i].egg&&state_->party[i].hp)n++;setVar(dst,n);break;}
            case 358:{auto dst=read16();setVar(dst,state_?std::uint16_t(state_->alivePartyAndPC()):0);break;}
            case 359:{auto dst=read16();std::uint16_t n=0;if(state_)for(auto const& m:state_->party)if(m.egg)n++;setVar(dst,n);break;}
            case 360:{auto amount=varValue(read16());if(state_)state_->money=amount>state_->money?0:state_->money-amount;break;}
            case 361:{auto slot=varValue(read16()),dst=read16();bool ok=state_&&state_->retrieveDaycareMon(slot);setVar(dst,ok?1:0);break;}
            case 362:{auto species=read8(); auto level=read8(); auto dst=read16();bool ok=false;if(state_){auto ps=state_->party.size(),cs=state_->pcStorage.size();ok=state_->giveMon(species,level);if(ok){if(state_->party.size()>ps)state_->party.back().mine=false;else if(state_->pcStorage.size()>cs)state_->pcStorage.back().mine=false;}}setVar(dst,ok?1:0);break;}
            case 363:{read8();auto species=varValue(read16()),dst=read16();setVar(dst,state_&&state_->hasSpecies(species)?1:0);break;}
            case 364:{auto species=varValue(read16());if(state_){auto it=std::find_if(state_->party.begin(),state_->party.end(),[&](auto const&m){return !m.mine&&m.species==species;});if(it!=state_->party.end())state_->party.erase(it);}break;}
            case 365:if(state_)state_->daycareEggReady=false;break;
            case 366:{if(state_&&state_->daycareEggReady){auto species=state_->daycareEggSpecies?state_->daycareEggSpecies:175;auto ps=state_->party.size(),cs=state_->pcStorage.size();if(state_->giveMon(species,1)){if(state_->party.size()>ps)state_->party.back().egg=true;else if(state_->pcStorage.size()>cs)state_->pcStorage.back().egg=true;state_->daycareEggReady=false;}}break;}
            case 367:{auto slot=varValue(read16()),dst=read16();setVar(dst,state_?std::uint16_t(std::min<std::uint32_t>(65535,state_->daycareWithdrawCost(slot))):0);break;}
            case 368:{auto dst=read16(),amount=varValue(read16());setVar(dst,state_&&state_->money>=amount?1:0);break;}
            case 369:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 370:{read8();break;}
            case 371:{auto slot=varValue(read16()),dst=read16();std::uint16_t growth=0;if(state_&&slot<state_->daycare.size()&&state_->daycare[slot].occupied)growth=std::uint16_t(std::min<std::uint32_t>(65535,state_->daycare[slot].steps/256));setVar(dst,growth);break;}
            case 372:{auto dst=read16();std::uint16_t species=0;if(state_){for(int i=int(state_->daycare.size())-1;i>=0;--i)if(state_->daycare[std::size_t(i)].occupied){species=state_->daycare[std::size_t(i)].mon.species;break;}}setVar(dst,species);break;}
            case 373:{auto slot=varValue(read16());if(state_)state_->putMonInDaycare(slot);break;}
            case 374:{read16();break;}
            case 375:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 376:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 377:{auto dst=read16();setVar(dst,1);break;} // bedroom PC is available in the native engine
            case 378:{read16();read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 379:{read16();break;}
            case 380:{auto dst=read16(),max=varValue(read16());setVar(dst,max?std::uint16_t((std::uint32_t(pc_)*1103515245u+12345u)%max):0);break;}
            case 381:{read16();read16();break;}
            case 382:{auto slot=varValue(read16()),dst=read16();setVar(dst,state_&&slot<state_->party.size()?state_->party[slot].friendship:0);break;}
            case 383:{auto slot=varValue(read16()),amount=varValue(read16());if(state_&&slot<state_->party.size())state_->party[slot].friendship=std::uint8_t(std::min<int>(255,int(state_->party[slot].friendship)+amount));break;}
            case 384:{auto slot=varValue(read16()),amount=varValue(read16());if(state_&&slot<state_->party.size())state_->party[slot].friendship=std::uint8_t(amount>state_->party[slot].friendship?0:state_->party[slot].friendship-amount);break;}
            case 385:{auto slot=varValue(read16()),a=read16(),b=read16(),c=read16();if(state_&&slot<state_->daycare.size()&&state_->daycare[slot].occupied){auto const&m=state_->daycare[slot].mon;setVar(a,m.level);setVar(b,m.species);setVar(c,m.friendship);}else{setVar(a,0);setVar(b,0);setVar(c,0);}break;}
            case 386:{auto dst=read16();setVar(dst,std::uint16_t(playerDirection_));break;}
            case 387:{auto dst=read16();std::uint16_t v=0;if(state_&&state_->daycare[0].occupied&&state_->daycare[1].occupied)v=2;setVar(dst,v);break;}
            case 388:{auto dst=read16();setVar(dst,state_&&state_->daycareEggReady?1:0);break;}
            case 389:{auto species=varValue(read16()),dst=read16();setVar(dst,state_&&state_->hasSpecies(species)?1:0);break;}
            case 390:{read16();auto dst=read16();setVar(dst,0);break;}
            case 391:{read16();break;}
            case 392: case 393:{read16();read16();read16();break;}
            case 394: case 395:{read16();break;}
            case 396:{auto slot=varValue(read16()),dst=read16();std::uint16_t n=0;if(state_&&slot<state_->party.size())for(auto m:state_->party[slot].moves)if(m)n++;setVar(dst,n);break;}
            case 397:{auto slot=varValue(read16()),moveSlot=varValue(read16());if(state_&&slot<state_->party.size()&&moveSlot<4)state_->party[slot].moves[moveSlot]=0;break;}
            case 398:{auto slot=varValue(read16()),moveSlot=varValue(read16()),dst=read16();setVar(dst,state_&&slot<state_->party.size()&&moveSlot<4?state_->party[slot].moves[moveSlot]:0);break;}
            case 399:{auto fmt=read8(); auto slot=varValue(read16()); auto moveSlot=varValue(read16());if(state_&&fmt<state_->formatSlots.size()&&slot<state_->party.size()&&moveSlot<4)state_->formatSlots[fmt]=hg_move_name(state_->party[slot].moves[moveSlot]);break;}
            case 400: case 401: case 402:{auto action=read8();bool* stateFlag=nullptr;if(state_)stateFlag=(op==400?&state_->strengthActive:(op==401?&state_->flashActive:&state_->defogActive));if(action==2){auto dst=read16();setVar(dst,stateFlag&&*stateFlag?1:0);}else if(stateFlag)*stateFlag=(action==1);break;}

            // Retail command layouts below are taken from asm/macros/script.inc.
            // Commands whose native subsystem is still hosted outside the VM yield
            // a typed request after consuming the exact retail operand stream. This
            // preserves bytecode synchronization across every standard-script bank.
            // Additional HG/SS map-script commands.  The operand widths here
            // mirror asm/macros/script.inc exactly; commands whose DS overlay is not
            // native yet still return a typed host request only after their complete
            // operand stream has been consumed, so the script PC remains retail-safe.
            case 255:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 262:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 264:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 268:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 270:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 287:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // BufferUnionRoomAvatarChoices
            case 344:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 408:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 409:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 412:{auto a=read16(),b=read16(),c=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);return y;}
            case 413:{auto a=read16(),b=read16(),c=read16(),d=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);y.d=varValue(d);return y;}
            case 414: case 415: case 419:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 429:{ // CountFossils(result)
                auto dst=read16();std::uint16_t count=0;
                if(state_){static constexpr std::uint16_t fossils[]={103,104,105,106,107,108,109};for(auto id:fossils){auto it=state_->bag.find(id);if(it!=state_->bag.end())count=std::uint16_t(std::min<unsigned>(0xffffu,unsigned(count)+it->second));}}
                setVar(dst,count);break;
            }
            case 445:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 452:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 462:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 463:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // EnableMassOutbreaks
            case 480:{auto slot=varValue(read16()),ribbon=varValue(read16()),dst=read16();(void)slot;(void)ribbon;setVar(dst,0);break;} // ribbons not yet persisted per-mon
            case 486:break; // retail Dummy486
            case 488:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;} // ElevatorAnim
            case 491:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 503:{auto dst=read16();setVar(dst,state_?state_->var(0x40ff):0);break;} // LotoIDGet
            case 507:{auto dst=read16();std::size_t used=state_?state_->pcStorage.size():0;setVar(dst,std::uint16_t(used>=540?0:540-used));break;} // CountPCEmptySpace
            case 508:{auto action=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(action);return y;} // PalParkAction
            case 511:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;} // PalParkScoreGet
            case 514:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;} // HallOfFameAnim
            case 515:{read16();break;} // AddSpecialGameStat: counter block not yet exposed to UI
            case 517:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 522:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 523:{for(int i=0;i<5;i++)read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 528:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 540:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 545:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 546:{auto a=read8();auto b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=varValue(b);return y;}
            case 562:{auto a=read16(),b=read16(),c=read16();auto d=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);y.d=d;return y;} // MultiBattle
            case 566:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 573:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 589:{HgScriptYield y;y.type=HgScriptYield::Type::WildBattle;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.flag=read8()!=0;return y;} // WildBattle(species,level,shiny)
            case 620:{auto action=read8();if(state_)state_->setVar(0x40fb,action?1:0);break;} // RocketCostumeFlagAction mirror
            case 628:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 636: case 640: case 646: case 663: case 681: case 682: case 708: case 735: case 743: case 815: case 830: case 835:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 649:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // ScratchOffCard
            case 669:{auto item=varValue(read16()),dst=read16();std::uint16_t q=0;if(state_){auto it=state_->bag.find(item);if(it!=state_->bag.end())q=it->second;}setVar(dst,q);break;} // GetItemQuantity
            case 671:{if(state_&&!state_->party.empty())state_->setVar(0x40fa,state_->party.front().species);break;} // SetFavoriteMon mirror
            case 673:{ // GetOwnedRotomForms(out0..out4)
                std::array<std::uint16_t,5> dst{};for(auto &v:dst)v=read16();std::array<bool,5> have{};
                if(state_){auto scan=[&](auto const& mons){for(auto const&m:mons)if(m.species==479&&m.form>=1&&m.form<=5)have[m.form-1]=true;};scan(state_->party);scan(state_->pcStorage);}
                for(std::size_t i=0;i<dst.size();++i){setVar(dst[i],have[i]?1:0);}break;
            }
            case 674:{auto a=read16(),dst=read16();(void)a;std::uint16_t n=0;if(state_)for(auto const&m:state_->party)if(m.species==479&&m.form)n++;setVar(dst,n);break;} // CountTransformedRotomsInParty
            case 680:{read16();break;} // AddSpecialGameStat2
            case 688:{read16();auto dst=read16();setVar(dst,0xff);break;} // no per-mon fateful flag in native save yet
            case 693:{auto dst=read16();setVar(dst,0);break;} // BattleHallCountUsedSpecies
            case 698:{auto a=read8();auto b=read16(),c=read16();HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=a;y.b=varValue(b);y.c=varValue(c);return y;} // FollowerPokeIsEventTrigger
            case 709: case 710: case 715: case 744: case 805: case 810: case 814:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 712:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 713:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;} // AlphPuzzle
            case 714:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;} // OpenAlphHiddenRoom
            case 722: case 723:{auto a=read8(),b=read8();auto c=read16(),d=read16();read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=b;y.c=varValue(c);y.d=varValue(d);return y;}
            case 724:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 770:{auto dst=read16();setVar(dst,0);break;} // CheckSeenAllLetterUnown; form-seen bitset pending
            case 775:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 776:{ // GiveTogepiEgg
                if(state_){HgMon egg=hg_make_mon(175,1);egg.egg=true;egg.nickname="EGG";state_->storeMon(std::move(egg));}
                break;
            }
            case 778:{if(state_)state_->giveMon(172,30,0,1);break;} // GiveSpikyEarPichu
            case 779:{read16();auto dst=read16();setVar(dst,0);break;} // RadioMusicIsPlaying; audio sequence query pending
            case 781:{auto dst=read16();setVar(dst,0);break;} // KenyaCheckPartyOrMailbox; mail metadata pending
            case 783:{auto inhibit=read8();if(state_)state_->setVar(0x40f9,inhibit?1:0);break;} // SetFollowMonInhibitState mirror
            case 803:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 804:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 820:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 844:{auto slot=read8();auto item=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_item_name(item);break;} // BufferItemNamePlural
            case 852:{auto slot=read8(),floor=read8();if(state_&&slot<state_->formatSlots.size()){state_->formatSlots[slot]=floor==0?"B1F":std::to_string(unsigned(floor))+"F";}break;} // BufferDeptStoreFloorNo
            case 267: case 274: case 410: case 538:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 288:{auto avatar=varValue(read16()),dst=read16();setVar(dst,avatar);break;} // UnionRoomAvatarIdxToTrainerClass compatibility
            case 411: case 510: case 816:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 420: case 527: case 592:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 444:{auto a=read8();auto b=read16(),c=read16();auto d=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=varValue(b);y.c=varValue(c);y.d=d;return y;}
            case 464:{auto roamer=read8();if(state_)state_->setVar(std::uint16_t(0x40e0+(roamer&0xf)),1);break;} // CreateRoamer persistent compatibility marker
            case 470:{auto trade=read8();if(state_)state_->setVar(0x40df,trade);HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=trade;return y;} // LoadNPCTrade
            case 471:{auto dst=read16();setVar(dst,0);break;} // GetOfferedSpecies: populated by native NPC-trade overlay when active
            case 472:{auto dst=read16();setVar(dst,0);break;} // NPCTradeGetReqSpecies
            case 473:{auto slot=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(slot);return y;} // NPCTradeExec
            case 474: case 475: case 476:break;
            case 478:{read16();auto dst=read16();setVar(dst,0);break;} // per-mon ribbon save block pending
            case 479:{auto dst=read16();setVar(dst,0);break;}
            case 481:{read16();read16();break;}
            case 482:{auto slot=read8();auto ribbon=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="RIBBON "+std::to_string(ribbon);break;}
            case 483:{read16();auto dst=read16();setVar(dst,0);break;} // EV storage pending
            case 493:{auto a=read16(),b=read16(),c=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);return y;} // PromptEasyChat
            case 541:{auto slot=read8();auto value=varValue(read16());auto digits=read8();auto mode=read8();(void)digits;(void)mode;if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=std::to_string(value);break;} // BufferIntEx
            case 553:{auto a=read8();auto b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=varValue(b);return y;}
            case 627:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 633: case 637: case 643:{auto a=read16(),b=read16(),c=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);return y;}
            case 683:{auto dst=read16();setVar(dst,state_&&state_->lastBattleWon?1:0);break;} // GetStaticEncounterOutcome native battle mirror
            case 738:{auto dst=read16();std::uint16_t n=0;if(state_)for(std::uint16_t id=485;id<=491;++id){auto it=state_->bag.find(id);if(it!=state_->bag.end())n=std::uint16_t(std::min<unsigned>(0xffffu,unsigned(n)+it->second));}setVar(dst,n);break;} // GetTotalApricornCount
            case 755: case 756: case 757: case 759: case 760: case 763: case 764: case 765: case 766:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 758: case 761: case 762:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 846:{auto slot=read8();auto species=varValue(read16());read16();read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_species_name(species);break;} // BufferSpeciesNameIndef
            case 263: case 453:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 269:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 348:{auto frames=varValue(read16());HgScriptYield y;y.type=HgScriptYield::Type::WaitFrames;y.opcode=op;y.a=frames?frames:1;return y;} // WaitButtonOrDelay host approximation
            case 504:{auto a=read16(),b=read16(),c=read16(),d=read16();(void)a;setVar(b,0);setVar(c,0);setVar(d,0);break;} // LotoIDSearch; native mons do not yet persist OT IDs
            case 767: case 768: case 769:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // Lugia-arrival effect phases
            case 773: case 774:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;} // Cinematic / ShowLegendaryWing
            case 777:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 780:{auto a=read8(),b=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=b;return y;} // CasinoGame
            case 817: case 841: case 842:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 818: case 819: case 822:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 821:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 823: case 824: case 829: case 831: case 832: case 833: case 834: case 836:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 828:{auto a=read16();auto b=read8();auto c=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=b;y.c=varValue(c);return y;}
            case 847:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_species_name(state_->starter);break;}
            case 848:{auto slot=read8();auto id=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="FASHION "+std::to_string(id);break;}
            case 849:{auto slot=read8();auto cls=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="TRAINER CLASS "+std::to_string(cls);break;}
            case 850:{auto slot=read8();auto seal=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="SEAL "+std::to_string(seal);break;}
            case 851:{auto slot=read8();if(state_&&slot<state_->formatSlots.size()){auto &v=state_->formatSlots[slot];if(!v.empty())v[0]=char(std::toupper(static_cast<unsigned char>(v[0])));}break;}
            case 261:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 558:{auto avatar=varValue(read16()),dst=read16();setVar(dst,avatar);break;} // UnionRoomAvatarIdxToSprite compatibility
            case 585:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 726:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // ProcessSoundplate
            case 271:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 273: case 289:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 272:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 265: case 266:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 257:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 284:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 403:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 406:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 423:{ // CheckJohtoDexComplete(result)
                auto dst=read16();std::size_t owned=0;if(state_)for(auto species:state_->dexOwned)if(species>=1&&species<=251)++owned;setVar(dst,owned>=251?1:0);break;
            }
            case 430:{auto a=read16(),b=read16(),c=read16();if(state_)state_->phoneCallState[varValue(a)]=varValue(b);HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);return y;} // SetPhoneCall
            case 431:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // RunPhoneCall
            case 435:{ // SurvivePoisoning(result, partySlot)
                auto result=read16(),slot=varValue(read16());std::uint16_t survived=0;
                if(state_&&slot<state_->party.size()){auto &m=state_->party[slot];if(m.status&&m.hp<=1){m.hp=1;survived=1;}}
                setVar(result,survived);break;
            }
            case 437:{ // DebugWatch / DummyGetVar: retail debug no-op, not an application.
                auto a=read16();(void)varValue(a);break;
            }
            case 446:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 447:{auto a=read8(),b=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=b;return y;} // SafariZoneAction
            case 449:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 454:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 450:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 455:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 460:{ // LoadPhoneDat(contact, destination)
                auto contact=varValue(read16()),dst=read16();
                if(state_)state_->phoneNumbers.insert(contact);
                setVar(dst,contact);break;
            }
            case 465:{ // variable-layout roamer/field helper
                auto mode=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(mode);
                if(mode<=3){y.b=varValue(read16());y.c=varValue(read16());}
                else if(mode!=6){y.b=varValue(read16());}
                return y;
            }
            case 477:{ // NatDexFlagAction(action, result)
                auto action=read8(); auto dst=read16();
                if(state_&&action==1)state_->nationalDex=true;
                if(action==0||action==2)setVar(dst,state_&&state_->nationalDex?1:0);
                break;
            }
            case 487:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(a);return y;} // PokeCenAnim
            case 490:{read16();break;} // NopVar490
            case 489:{ // MysteryGift variable layout
                auto mode=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(mode);
                if(mode>=1&&mode<=3)y.b=varValue(read16());
                else if(mode==5||mode==6){y.b=varValue(read16());y.c=varValue(read16());}
                return y;
            }
            case 492:{auto a=read16(),b=read16(),c=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);return y;}
            case 500:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 501:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;}
            case 512:{if(state_)state_->setVar(0x40fd,1);break;} // PlayerMovementSavingSet mirror
            case 513:{if(state_)state_->setVar(0x40fd,0);break;} // PlayerMovementSavingClear
            case 516:{auto slot=read8();auto id=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="FASHION "+std::to_string(id);break;} // BufferFashionName
            case 531:{auto slot=read8();auto id=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="BACKGROUND "+std::to_string(id);break;} // BufferBackgroundName
            case 547:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 505:{ // LotoIDSet: keep a persistent daily value in a retail-style save var mirror.
                if(state_&&state_->var(0x40ff)==0){state_->setVar(0x40ff,std::uint16_t((std::uint32_t(pc_)*1103515245u+12345u)>>16));}break;
            }
            case 521:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 551:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 555:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 552:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 557:{auto amount=varValue(read16()),dst=read16();setVar(dst,state_&&state_->battlePoints>=amount?1:0);break;} // CheckBattlePoints
            case 560:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 564:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 565:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 574:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;} // exact layout; semantics not named in decomp
            case 587:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 599:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 561:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());y.d=varValue(read16());return y;} // ScreenShake
            case 584:{ // PartyLegalCheck(result)
                auto dst=read16();bool legal=state_&&!state_->party.empty()&&state_->party.size()<=6;
                if(legal)for(auto const&m:state_->party)if(m.species==0||m.level==0||m.level>100){legal=false;break;}
                setVar(dst,legal?1:0);break;
            }
            case 590:{ // GetTrcardStars(result): native approximation from persistent milestones.
                auto dst=read16();std::uint16_t stars=0;if(state_){stars+=state_->gameCleared?1:0;stars+=state_->nationalDex?1:0;stars+=state_->badges>=16?1:0;stars+=state_->savedPhotos>=36?1:0;}setVar(dst,std::min<std::uint16_t>(5,stars));break;
            }
            case 593: case 594:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // Show/HideSaveStats
            case 611:{ // Pokeathlon(mode, submode, arg2..arg6)
                auto mode=read8(),sub=read8();std::array<std::uint16_t,5> a{};for(auto &v:a)v=varValue(read16());
                HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=mode;y.b=sub;y.c=a[0];y.d=a[1];y.word=(std::uint32_t(a[2])<<16)|a[3];return y;
            }
            case 641:{break;} // SaveWipeExtraChunks: native save has no DS auxiliary chunks
            case 642:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 689:{auto dst=read16();setVar(dst,1);break;} // CommSanitizeParty: native party representation is already sanitized
            case 690:{auto slot=varValue(read16()),dst=read16();(void)slot;setVar(dst,1);break;} // DaycareSanitizeMon
            case 631:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());return y;}
            case 662:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());return y;}
            case 691:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 711:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;} // FollowMonInteract
            case 717:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 718:{auto a=read8();auto b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;y.b=varValue(b);return y;}
            case 719:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;}
            case 720:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 721:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 730:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 749:{ // MenuInitStdGmm
                if(!need(6))return error(op,"truncated MenuInitStdGmm");
                read8();read8();auto cursor=read8();auto cancel=read8();choiceDestination_=read16();choiceOptions_.clear();choiceCursor_=cursor;choiceCancel_=cancel!=0;break;
            }
            case 771: case 772:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 782:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;} // MartSell
            case 784:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=read8();y.b=read8();return y;} // ScriptOverlayCmd
            case 785:{auto mode=read8();auto dst=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=mode;y.b=varValue(dst);return y;} // BugContestAction
            case 786:{auto slot=read8();if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]="BUG CONTEST WINNER";break;} // BufferBugContestWinner
            case 787:{auto a=read16(),b=read16(),c=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);y.c=varValue(c);return y;} // JudgeBugContest
            case 789:{auto a=read8();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=a;return y;} // BugContestGetTimeLeft
            case 790:{auto contestant=varValue(read16());auto dst=read16();(void)contestant;setVar(dst,0);break;} // IsBugContestantRegistered; registration host is still pending.
            case 798:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;} // BufferRulesetName
            case 799:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 800:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 806:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 809:{auto msg=varValue(read16());HgScriptYield y;y.type=HgScriptYield::Type::Message;y.opcode=op;y.messageId=msg;y.npcMessage=false;return y;} // ShowTrainerHouseIntroMessage
            case 807:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());return y;} // SetTrainerHouseSprite
            case 808:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 813:{auto dst=read16();setVar(dst,state_&&state_->momSavings?1:0);break;} // MomGiftCheck
            case 837:{auto a=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);return y;}
            case 839:{auto v=varValue(read16());if(state_)state_->setVar(0x40fe,v?1:0);break;} // SysSetSleepFlag
            case 840:{auto a=read16(),b=read16();HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(a);y.b=varValue(b);return y;}
            case 843:{auto slot=read8();auto item=varValue(read16());if(state_&&slot<state_->formatSlots.size())state_->formatSlots[slot]=hg_item_name(item);break;} // BufferItemNameIndef
            case 484:{auto dst=read16();
                std::time_t now=std::time(nullptr);std::tm lt{};
#if defined(_WIN32)
                localtime_s(&lt,&now);
#else
                localtime_r(&now,&lt);
#endif
                setVar(dst,std::uint16_t(lt.tm_wday));break;}
            case 425:{if(!need(2))return error(op,"truncated ShowCertificate");HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 436:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 438:{
                auto kind=varValue(read16()),dst=read16();
                // Retail ov01_022067C8: weekday siblings, field moves, Cameron, shops.
                static constexpr std::uint16_t stdMsgFiles[4]={752,211,30,435};
                setVar(dst,kind<4?stdMsgFiles[kind]:0);break;
            }
            case 439: case 440:{
                if(!need(4))return error(op,"truncated external message");
                HgScriptYield y;y.type=HgScriptYield::Type::Message;y.opcode=op;
                y.a=varValue(read16());y.messageId=varValue(read16());y.npcMessage=(op==440);return y;
            }
            case 495:{auto dst=read16();setVar(dst,7);break;} // VERSION_HEARTGOLD
            case 496:{auto dst=read16();setVar(dst,state_&&!state_->party.empty()?0:0xff);break;}
            case 529:{
                auto dst=read16();std::uint16_t lead=0xff;
                if(state_)for(std::size_t i=0;i<state_->party.size();++i){auto const&m=state_->party[i];if(!m.egg&&m.hp){lead=std::uint16_t(i);break;}}
                setVar(dst,lead);break;
            }
            case 582:{if(!need(6))return error(op,"truncated ScrCmd_582");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());y.b=varValue(read16());y.c=varValue(read16());return y;}
            case 596:{if(!need(2))return error(op,"truncated ScrCmd_596");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 600:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}

            // HG/SS follower / starter-lab specials. These layouts come directly
            // from the retail script command table and are required by Elm's Lab.
            case 601:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;} // FollowMonFacePlayer
            case 602:{if(!need(2))return error(op,"truncated ToggleFollowingPokemonMovement");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 603:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;} // WaitFollowingPokemonMovement
            case 604:{if(!need(2))return error(op,"truncated FollowingPokemonMovement");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 605:{if(!need(2))return error(op,"truncated ScrCmd_605");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=read8();y.b=read8();return y;}
            case 606: case 607: case 608: case 609:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 610:{if(!need(2))return error(op,"truncated ScrCmd_610");HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;y.a=varValue(read16());return y;}
            case 615:{
                if(!need(2))return error(op,"truncated CameronPhoto");
                auto photoType=varValue(read16());(void)photoType;
                if(state_&&state_->savedPhotos<36)++state_->savedPhotos;
                HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;
            }
            case 616:{auto dst=read16();setVar(dst,state_?state_->savedPhotos:0);break;}
            case 617:{HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;return y;}
            case 618:{auto dst=read16();setVar(dst,state_&&state_->savedPhotos>=36?1:0);break;}
            case 621:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;} // PlaceStarterBallsInElmsLab

            case 727:{auto dst=read16();setVar(dst,state_&&state_->followerEnabled?state_->followerPartySlot:0xff);break;}
            case 729:{auto dst=read16();setVar(dst,state_&&state_->followerEnabled?1:0);break;}

            // Retail generic menu builder used by Mom's savings menu.
            case 750:{
                if(!need(6))return error(op,"truncated MenuInit");
                read8();read8();auto cursor=read8();auto cancel=read8();choiceDestination_=read16();
                choiceOptions_.clear();choiceCursor_=cursor;choiceCancel_=cancel!=0;break;
            }
            case 751:{
                if(!need(6))return error(op,"truncated MenuItemAdd");
                auto msg=read16();auto enable=varValue(read16());auto value=read16();
                if(enable!=0)choiceOptions_.push_back({msg,value});
                break;
            }
            case 752:{
                HgScriptYield y;y.type=HgScriptYield::Type::Choice;y.opcode=op;y.a=choiceDestination_;y.b=choiceCursor_;y.flag=choiceCancel_;y.choices=choiceOptions_;return y;
            }

            // Mom's savings account. CheckBankBalance opens the retail-style amount
            // selector in the host; BankTransaction applies the selected amount.
            case 793:{
                auto direction=read16(),amount=varValue(read16());
                if(state_){
                    if(direction){auto n=std::min<std::uint32_t>({std::uint32_t(amount),state_->money,999999u-state_->momSavings});state_->money-=n;state_->momSavings+=n;}
                    else{auto n=std::min<std::uint32_t>({std::uint32_t(amount),state_->momSavings,999999u-state_->money});state_->momSavings-=n;state_->money+=n;}
                }
                break;
            }
            case 794:{
                if(!need(6))return error(op,"truncated CheckBankBalance");
                HgScriptYield y;y.type=HgScriptYield::Type::AppCommand;y.opcode=op;y.a=read16();y.word=read32();return y;
            }
            case 795:{read16();read16();break;}
            case 796:break;

            case 825:{
                auto partySlot=varValue(read16()),dst=read16();std::uint16_t count=0;
                if(state_&&partySlot<state_->party.size()){auto const&m=state_->party[partySlot];count=m.shinyLeafCrown?6:m.shinyLeaves;}
                setVar(dst,count);break;
            }
            case 826:{
                auto partySlot=varValue(read16());
                if(state_&&partySlot<state_->party.size()&&state_->party[partySlot].shinyLeaves>=5)state_->party[partySlot].shinyLeafCrown=true;
                break;
            }
            case 827:{auto partySlot=varValue(read16()),dst=read16();setVar(dst,state_&&partySlot<state_->party.size()?state_->party[partySlot].form:0);break;}
            case 838:{
                auto which=varValue(read16()),dst=read16();bool full=false;
                if(state_)full=which?state_->momSavings>=999999:state_->money>=999999;
                setVar(dst,full?1:0);break;
            }
            case 845:{
                auto fmt=read8();auto partySlot=varValue(read16());
                if(state_&&fmt<state_->formatSlots.size()&&partySlot<state_->party.size())state_->formatSlots[fmt]=hg_species_name(state_->party[partySlot].species);
                break;
            }

            // Bottom-screen menu helpers used by the starter nickname prompt.
            case 746: case 747:{HgScriptYield y;y.type=HgScriptYield::Type::ObjectCommand;y.opcode=op;return y;}
            case 748:{
                if(!need(2))return error(op,"truncated GetMenuChoice");
                HgScriptYield y;y.type=HgScriptYield::Type::Choice;y.opcode=op;y.a=read16();
                y.flag=false;y.choices={{0,0},{0,1}};return y;
            }
            default:return unsupported(op,messages);
        }
    }
    HgScriptYield y;y.type=HgScriptYield::Type::None;y.detail="script command budget exhausted";return y;
}
