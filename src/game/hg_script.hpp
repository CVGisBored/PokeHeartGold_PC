#pragma once
#include "game/hg_state.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

class HgMessageBank;

struct HgScriptChoiceOption {
    std::uint16_t messageId=0;
    std::uint16_t value=0;
};

struct HgScriptYield {
    enum class Type {
        None,
        Message,
        WaitFrames,
        WaitInput,
        Sound,
        WaitSound,
        Movement,
        WaitMovement,
        ObjectCommand,
        PositionQuery,
        Choice,
        CommonScript,
        Battle,
        WildBattle,
        StarterChoice,
        Save,
        FieldTransition,
        AppCommand,
        Finished,
        Unsupported,
        Error
    } type=Type::None;
    std::size_t messageId=0;
    bool npcMessage=true;
    std::uint16_t opcode=0;
    std::uint16_t a=0,b=0,c=0,d=0;
    std::uint32_t word=0;
    std::int32_t rel=0;
    bool flag=false;
    std::vector<HgScriptChoiceOption> choices;
    std::string detail;
};

// Native interpreter for HeartGold/SoulSilver field bytecode.  It mirrors the
// retail ScriptContext model (4 data registers, a 20-entry call stack and
// command/native-wait execution) while delegating field-engine side effects to
// the host through HgScriptYield.  No ARM code is executed.
class HgScriptVm {
public:
    void bindState(HgGameState* s){state_=s;}
    bool start(const std::vector<unsigned char>& bank,std::uint16_t eventScriptNumber,std::uint16_t messageBankId=0xffff);
    // Retail RunScript (opcode 19) and CallStd (opcode 20) create another
    // ScriptContext. The native host serializes a child at this boundary when it
    // cannot safely interleave a blocking UI operation, but it always preserves
    // the parent context and restores it when the child ends. `waitForChild`
    // records the retail distinction for diagnostics/scheduling.
    bool enterExternalBank(const std::vector<unsigned char>& bank,std::uint16_t scriptNumber,std::uint16_t messageBankId,bool waitForChild);
    HgScriptYield runUntilYield(const HgMessageBank* messages=nullptr,std::size_t commandBudget=1024);
    bool active() const { return active_; }
    void stop(){active_=false;externalStack_.clear();}
    void clearPersistentState();

    std::unordered_map<std::uint16_t,std::uint16_t>& vars();
    const std::unordered_map<std::uint16_t,std::uint16_t>& vars() const;
    std::unordered_set<std::uint16_t>& flags();
    const std::unordered_set<std::uint16_t>& flags() const;
    std::uint16_t value(std::uint16_t id) const{return varValue(id);}
    void writeVar(std::uint16_t id,std::uint16_t v){setVar(id,v);}
    std::uint32_t scriptRegister(std::size_t i) const{return i<data_.size()?data_[i]:0;}
    void setScriptRegister(std::size_t i,std::uint32_t v){if(i<data_.size())data_[i]=v;}
    std::size_t pc() const{return pc_;}
    std::uint16_t scriptNumber() const{return scriptNumber_;}
    std::size_t entryCount() const{return entries_.size();}
    std::uint16_t messageBankId() const{return messageBankId_;}
    std::size_t externalDepth() const{return externalStack_.size();}

    // Interaction environment used by ObjectGoTo/BGGoTo/DirectionGoTo and
    // FacePlayer/GetTrainerNum-style commands.
    void setInteractionContext(int objectId,int backgroundId,int playerDirection){
        lastObjectId_=objectId;backgroundId_=backgroundId;playerDirection_=playerDirection;
    }

private:
    HgGameState* state_=nullptr;
    std::vector<unsigned char> bank_;
    std::vector<std::size_t> entries_;
    std::vector<std::size_t> stack_;
    struct BankContext {
        std::vector<unsigned char> bank;
        std::vector<std::size_t> entries;
        std::vector<std::size_t> stack;
        std::size_t pc=0,startPc=0;
        std::uint16_t scriptNumber=0,messageBankId=0xffff;
        bool waitForChild=true;
    };
    std::vector<BankContext> externalStack_;
    std::unordered_map<std::uint16_t,std::uint16_t> localVars_;
    std::unordered_set<std::uint16_t> localFlags_;
    std::unordered_map<std::uint32_t,std::uint8_t> scratchMemory_;
    std::array<std::uint32_t,4> data_{};
    std::size_t pc_=0,startPc_=0;
    std::uint16_t scriptNumber_=0,messageBankId_=0xffff;
    int compare_=0;
    // HG/SS reuses GoToIf condition bytes 0/1 as FALSE/TRUE after
    // CheckFlag/CheckTrainerFlag.  Keep that boolean-result mode separate
    // from ordinary LT/EQ/GT comparisons so GoToIfSet/Unset are not inverted.
    bool booleanCondition_=false;
    bool booleanResult_=false;
    bool active_=false;
    int lastObjectId_=-1;
    int backgroundId_=-1;
    int playerDirection_=0;

    // Menu construction state (commands 64-72).
    std::uint16_t choiceDestination_=0;
    bool choiceCancel_=false;
    std::uint8_t choiceCursor_=0;
    std::vector<HgScriptChoiceOption> choiceOptions_;

    bool need(std::size_t n) const{return pc_+n<=bank_.size();}
    bool loadBank(const std::vector<unsigned char>& bank,std::uint16_t scriptNumber,std::uint16_t messageBankId);
    void restoreExternalCaller();
    std::uint8_t read8();
    std::uint16_t read16();
    std::uint32_t read32();
    std::int32_t readS32();
    std::uint16_t varValue(std::uint16_t id) const;
    void setVar(std::uint16_t id,std::uint16_t v);
    void setCompare(std::uint32_t a,std::uint32_t b){compare_=(a<b?-1:(a>b?1:0));booleanCondition_=false;}
    bool condition(std::uint8_t cond) const;
    bool branchRelative(std::int32_t rel);
    HgScriptYield unsupported(std::uint16_t opcode,const HgMessageBank* messages,const std::string& why={});
    HgScriptYield error(std::uint16_t opcode,const std::string& detail);
};
