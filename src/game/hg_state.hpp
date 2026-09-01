#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct HgMon {
    std::uint16_t species=0;
    std::uint8_t level=1;
    std::uint16_t hp=1,maxHp=1;
    std::uint16_t attack=1,defense=1,spAttack=1,spDefense=1,speed=1;
    std::uint16_t heldItem=0;
    std::uint8_t form=0,ability=0;
    std::array<std::uint16_t,4> moves{0,0,0,0};
    std::array<std::uint8_t,4> pp{0,0,0,0},maxPp{0,0,0,0};
    std::uint32_t exp=0;
    bool egg=false;
    std::string nickname{};
    std::uint8_t friendship=70;
    std::uint8_t gender=0; // 0 male, 1 female, 2 genderless
    std::uint8_t status=0;
    bool mine=true;
    std::uint8_t shinyLeaves=0;
    bool shinyLeafCrown=false;
};

struct HgDynamicWarp {
    bool valid=false;
    std::uint16_t map=0,warp=0,x=0,y=0,facing=0;
};

struct HgDaycareSlot {
    bool occupied=false;
    HgMon mon{};
    std::uint32_t steps=0;
};

class HgGameState {
public:
    std::unordered_map<std::uint16_t,std::uint16_t> vars;
    std::unordered_set<std::uint16_t> flags;
    std::unordered_set<std::uint16_t> trainerFlags;
    std::unordered_map<std::uint16_t,std::uint16_t> bag;
    std::vector<HgMon> party;
    std::vector<HgMon> pcStorage;
    std::unordered_set<std::uint16_t> dexSeen,dexOwned;
    // Mystery Gift identity and redeemed event IDs live inside the save file.
    // This mirrors retail's save-bound Wonder Card / delivery state: deleting a
    // machine-local client-id file or resetting the server cannot make a gift
    // redeemable again on the same save.
    std::string mysteryGiftSaveId;
    std::unordered_set<std::string> mysteryGiftClaims;
    std::uint32_t money=3000;
    std::uint32_t momSavings=0;
    std::uint16_t savedPhotos=0;
    std::uint16_t coins=0;
    std::uint16_t athletePoints=0;
    std::uint16_t battlePoints=0;
    // Retail Pokéathlon shop save bits: daily prize slots and Data Cards.
    std::uint16_t pokeathlonPrizeFlags=0;
    std::uint32_t pokeathlonDataCardFlags=0;
    std::uint32_t pokeathlonPrizeDay=0;
    std::uint32_t pokegearCards=0;
    std::unordered_set<std::uint16_t> phoneNumbers;
    std::unordered_map<std::uint16_t,std::uint16_t> phoneCallState;
    std::unordered_map<std::uint16_t,std::uint16_t> seals;
    std::unordered_map<std::uint16_t,std::uint16_t> decorations;
    bool pokedex=false,nationalDex=false,runningShoes=false;
    bool onBike=false,bikeLocked=false;
    bool lastBattleWon=true;
    bool escortMode=false;
    bool gameCleared=false,stepTaken=false;
    bool strengthActive=false,flashActive=false,defogActive=false;
    bool followerEnabled=false;
    std::uint8_t followerPartySlot=0;
    std::uint16_t followerSpecies=0;
    std::uint16_t starter=0;
    std::uint16_t spawnId=0;
    HgDynamicWarp dynamicWarp{};
    std::array<HgDaycareSlot,2> daycare{};
    bool daycareEggReady=false;
    std::uint16_t daycareEggSpecies=0;
    std::array<std::string,8> formatSlots{};
    std::string playerName="PLAYER";
    std::string rivalName="???";
    std::string friendName="LYRA";
    bool female=false;
    bool newGameStarted=false;
    bool momIntroDone=false;
    // Native compatibility mirrors for two early retail story flags.  These are
    // persisted because the native port still has a dedicated Elm transition path.
    bool elmLabIntroDone=false;
    bool gotStarter=false;
    std::uint8_t badges=0;              // cached count for UI/save summary
    std::uint16_t badgeFlags=0;         // retail Johto+Kanto badge bitfield

    std::uint16_t var(std::uint16_t id) const;
    void setVar(std::uint16_t id,std::uint16_t v);
    bool flag(std::uint16_t id) const;
    bool addItem(std::uint16_t item,std::uint16_t qty);
    bool takeItem(std::uint16_t item,std::uint16_t qty);
    bool hasItem(std::uint16_t item,std::uint16_t qty) const;
    // Retail HG/SS SavePokegear::registeredCards is a two-bit field: the Phone
    // card is the base state (card 0 resets the extra-card mask), Map is bit 0
    // and Radio is bit 1. Keep this encoding instead of treating card IDs as
    // arbitrary bit indices.
    void registerPokegearCard(std::uint8_t card);
    bool hasPokegearCard(std::uint8_t card) const;
    bool giveMon(std::uint16_t species,std::uint8_t level,std::uint16_t held=0,std::uint8_t form=0,std::uint8_t ability=0);
    bool storeMon(HgMon mon);
    bool hasSpecies(std::uint16_t species) const;
    void see(std::uint16_t species); void own(std::uint16_t species);
    std::size_t aliveParty() const;
    std::size_t alivePartyAndPC() const;
    HgMon* leadAlive(); const HgMon* leadAlive() const;
    bool putMonInDaycare(std::size_t partySlot);
    bool retrieveDaycareMon(std::size_t daycareSlot);
    std::uint32_t daycareWithdrawCost(std::size_t daycareSlot) const;
    void onPlayerStep();
};

HgMon hg_make_mon(std::uint16_t species,std::uint8_t level,std::uint16_t held=0,std::uint8_t form=0,std::uint8_t ability=0);
void hg_rehydrate_mon(HgMon& mon);
std::string hg_species_name(std::uint16_t species);
std::string hg_item_name(std::uint16_t item);
std::string hg_move_name(std::uint16_t move);
std::string hg_pocket_name(std::uint16_t pocket);
std::uint16_t hg_item_pocket(std::uint16_t item);
std::uint32_t hg_item_price(std::uint16_t item);
