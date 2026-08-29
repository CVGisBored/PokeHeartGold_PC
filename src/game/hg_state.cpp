#include "game/hg_state.hpp"
#include "assets/pokemon_data.hpp"
#include <algorithm>

std::uint16_t HgGameState::var(std::uint16_t id) const { if(id<0x4000)return id;auto it=vars.find(id);return it==vars.end()?0:it->second; }
void HgGameState::setVar(std::uint16_t id,std::uint16_t v){if(id>=0x4000)vars[id]=v;}
bool HgGameState::flag(std::uint16_t id) const{return flags.count(id)!=0;}
bool HgGameState::addItem(std::uint16_t item,std::uint16_t qty){if(!item||!qty)return false;const unsigned limit=hg_item_pocket(item)==3?99u:999u;auto& q=bag[item];if(unsigned(q)+qty>limit)return false;q=std::uint16_t(q+qty);return true;}
bool HgGameState::takeItem(std::uint16_t item,std::uint16_t qty){auto it=bag.find(item);if(it==bag.end()||it->second<qty)return false;it->second=std::uint16_t(it->second-qty);if(!it->second)bag.erase(it);return true;}
bool HgGameState::hasItem(std::uint16_t item,std::uint16_t qty) const{auto it=bag.find(item);return it!=bag.end()&&it->second>=qty;}
void HgGameState::registerPokegearCard(std::uint8_t card){
    switch(card){
        case 0: pokegearCards=0; break;       // GEARCARD_PHONE: base app, no extra-card bit
        case 1: pokegearCards|=0x1u; break;  // GEARCARD_MAP
        case 2: pokegearCards|=0x2u; break;  // GEARCARD_RADIO
        default: break;
    }
    pokegearCards&=0x3u;
}
bool HgGameState::hasPokegearCard(std::uint8_t card) const{
    if(card==0)return true;
    if(card==1)return (pokegearCards&0x1u)!=0;
    if(card==2)return (pokegearCards&0x2u)!=0;
    return false;
}

HgMon hg_make_mon(std::uint16_t species,std::uint8_t level,std::uint16_t held,std::uint8_t form,std::uint8_t ability){
    HgMon m;m.species=species;m.level=std::max<std::uint8_t>(1,level);m.heldItem=held;m.form=form;m.ability=ability;m.nickname=hg_species_name(species);
    if(auto* p=hg_personal_data(species)){
        auto L=unsigned(m.level);m.maxHp=std::uint16_t(((2u*p->hp+31u)*L)/100u+L+10u);
        auto stat=[L](unsigned base){return std::uint16_t(((2u*base+31u)*L)/100u+5u);};
        m.attack=stat(p->attack);m.defense=stat(p->defense);m.spAttack=stat(p->spAttack);m.spDefense=stat(p->spDefense);m.speed=stat(p->speed);
        m.friendship=p->baseFriendship;if(!ability)m.ability=p->ability1;
        if(p->genderRatio==255)m.gender=2;else if(p->genderRatio==0)m.gender=0;else if(p->genderRatio==254)m.gender=1;else m.gender=std::uint8_t((species*37u+L*13u)&255u)<p->genderRatio?1:0;
    }else{m.maxHp=std::uint16_t(10+m.level*3);m.attack=m.defense=m.spAttack=m.spDefense=m.speed=std::uint16_t(5+m.level*2);m.friendship=70;m.gender=std::uint8_t(species%2);}
    m.hp=m.maxHp;m.exp=hg_exp_for_level(species,m.level);auto learned=hg_levelup_moves(species,m.level);for(std::size_t i=0;i<learned.size()&&i<4;i++)m.moves[i]=learned[i];if(!m.moves[0])m.moves[0]=33;
    for(int i=0;i<4;i++) {
        if(auto* md=hg_move_data(m.moves[i])) m.pp[i]=m.maxPp[i]=md->pp;
    }
    return m;
}
void hg_rehydrate_mon(HgMon& m){
    auto oldHp=m.hp;
    if(auto* p=hg_personal_data(m.species)){
        auto L=unsigned(std::max<std::uint8_t>(1,m.level));
        m.maxHp=std::uint16_t(((2u*p->hp+31u)*L)/100u+L+10u);
        auto stat=[L](unsigned base){return std::uint16_t(((2u*base+31u)*L)/100u+5u);};
        m.attack=stat(p->attack);m.defense=stat(p->defense);m.spAttack=stat(p->spAttack);m.spDefense=stat(p->spDefense);m.speed=stat(p->speed);
        if(!m.ability)m.ability=p->ability1;
    }
    m.hp=std::min(oldHp,m.maxHp);if(m.exp==0&&m.level>1)m.exp=hg_exp_for_level(m.species,m.level);
    for(int i=0;i<4;i++){
        if(auto* md=hg_move_data(m.moves[i])){
            const bool legacyMissingPpMetadata=(m.maxPp[i]==0);
            m.maxPp[i]=md->pp;
            if(legacyMissingPpMetadata)m.pp[i]=md->pp;
        }
    }
    if(m.nickname.empty())m.nickname=hg_species_name(m.species);
}
bool HgGameState::storeMon(HgMon mon){if(!mon.species)return false;if(party.size()<6){party.push_back(std::move(mon));return true;}if(pcStorage.size()>=540)return false;pcStorage.push_back(std::move(mon));return true;}
bool HgGameState::giveMon(std::uint16_t species,std::uint8_t level,std::uint16_t held,std::uint8_t form,std::uint8_t ability){if(!species)return false;HgMon m=hg_make_mon(species,level,held,form,ability);if(!storeMon(std::move(m)))return false;own(species);return true;}
bool HgGameState::hasSpecies(std::uint16_t species) const{for(auto const& m:party)if(m.species==species)return true;for(auto const& m:pcStorage)if(m.species==species)return true;for(auto const& d:daycare)if(d.occupied&&d.mon.species==species)return true;return false;}
void HgGameState::see(std::uint16_t s){if(s)dexSeen.insert(s);}void HgGameState::own(std::uint16_t s){if(s){dexSeen.insert(s);dexOwned.insert(s);}}
std::size_t HgGameState::aliveParty() const{std::size_t n=0;for(auto const& m:party)if(!m.egg&&m.hp)n++;return n;}
std::size_t HgGameState::alivePartyAndPC() const{std::size_t n=aliveParty();for(auto const& m:pcStorage)if(!m.egg&&m.hp)n++;return n;}
HgMon* HgGameState::leadAlive(){for(auto& m:party)if(!m.egg&&m.hp)return &m;return nullptr;}const HgMon* HgGameState::leadAlive() const{for(auto const& m:party)if(!m.egg&&m.hp)return &m;return nullptr;}
bool HgGameState::putMonInDaycare(std::size_t slot){if(slot>=party.size())return false;std::size_t d=daycare[0].occupied?1:0;if(d>=daycare.size()||daycare[d].occupied)return false;daycare[d].occupied=true;daycare[d].mon=party[slot];daycare[d].steps=0;party.erase(party.begin()+std::ptrdiff_t(slot));return true;}
bool HgGameState::retrieveDaycareMon(std::size_t slot){if(slot>=daycare.size()||!daycare[slot].occupied)return false;if(party.size()>=6)return false;party.push_back(daycare[slot].mon);daycare[slot]={};return true;}
std::uint32_t HgGameState::daycareWithdrawCost(std::size_t slot) const{if(slot>=daycare.size()||!daycare[slot].occupied)return 0;return 100u+100u*(daycare[slot].steps/256u);}
void HgGameState::onPlayerStep(){stepTaken=true;for(auto& d:daycare)if(d.occupied){d.steps++;if((d.steps%256u)==0&&d.mon.level<100){auto oldMax=d.mon.maxHp,oldHp=d.mon.hp;d.mon.level++;d.mon.exp=std::max(d.mon.exp,hg_exp_for_level(d.mon.species,d.mon.level));hg_rehydrate_mon(d.mon);d.mon.hp=std::min<std::uint16_t>(d.mon.maxHp,std::uint16_t(oldHp+(d.mon.maxHp-oldMax)));}}if(daycare[0].occupied&&daycare[1].occupied&&!daycareEggReady&&((daycare[0].steps+daycare[1].steps)%256u)==0){daycareEggReady=true;daycareEggSpecies=daycare[0].mon.species;}}

std::string hg_species_name(std::uint16_t s){
    auto rom=hg_rom_species_name(s);if(rom.rfind("POKEMON ",0)!=0)return rom;
    switch(s){case 1:return "BULBASAUR";case 4:return "CHARMANDER";case 7:return "SQUIRTLE";case 16:return "PIDGEY";case 19:return "RATTATA";case 25:return "PIKACHU";case 29:return "NIDORAN F";case 32:return "NIDORAN M";case 39:return "JIGGLYPUFF";case 52:return "MEOWTH";case 54:return "PSYDUCK";case 58:return "GROWLITHE";case 63:return "ABRA";case 74:return "GEODUDE";case 81:return "MAGNEMITE";case 92:return "GASTLY";case 95:return "ONIX";case 113:return "CHANSEY";case 129:return "MAGIKARP";case 133:return "EEVEE";case 152:return "CHIKORITA";case 155:return "CYNDAQUIL";case 158:return "TOTODILE";case 161:return "SENTRET";case 163:return "HOOTHOOT";case 167:return "SPINARAK";case 175:return "TOGEPI";case 179:return "MAREEP";case 183:return "MARILL";case 187:return "HOPPIP";case 194:return "WOOPER";case 198:return "MURKROW";case 200:return "MISDREAVUS";case 203:return "GIRAFARIG";case 209:return "SNUBBULL";case 215:return "SNEASEL";case 216:return "TEDDIURSA";case 220:return "SWINUB";case 228:return "HOUNDOUR";case 231:return "PHANPY";case 243:return "RAIKOU";case 244:return "ENTEI";case 245:return "SUICUNE";case 249:return "LUGIA";case 250:return "HO-OH";case 251:return "CELEBI";case 387:return "TURTWIG";case 390:return "CHIMCHAR";case 393:return "PIPLUP";default:return "SPECIES "+std::to_string(s);}
}
std::string hg_item_name(std::uint16_t i){
    auto rom=hg_rom_item_name(i);
    if(rom.rfind("ITEM ",0)!=0)return rom;
    return rom;
}
std::string hg_move_name(std::uint16_t m){auto rom=hg_rom_move_name(m);if(rom.rfind("MOVE ",0)!=0)return rom;switch(m){case 1:return "POUND";case 10:return "SCRATCH";case 15:return "CUT";case 19:return "FLY";case 22:return "VINE WHIP";case 33:return "TACKLE";case 39:return "TAIL WHIP";case 45:return "GROWL";case 52:return "EMBER";case 55:return "WATER GUN";case 57:return "SURF";case 70:return "STRENGTH";case 127:return "WATERFALL";case 148:return "FLASH";case 249:return "ROCK SMASH";case 250:return "WHIRLPOOL";case 291:return "DIVE";case 431:return "ROCK CLIMB";default:return "MOVE "+std::to_string(m);}}
std::uint16_t hg_item_pocket(std::uint16_t item){if(auto* d=hg_item_data(item))return d->fieldPocket;return 0;}
std::string hg_pocket_name(std::uint16_t p){switch(p){case 0:return "ITEMS";case 1:return "MEDICINE";case 2:return "POKE BALLS";case 3:return "TMs & HMs";case 4:return "BERRIES";case 5:return "MAIL";case 6:return "BATTLE ITEMS";case 7:return "KEY ITEMS";default:return "BAG";}}
std::uint32_t hg_item_price(std::uint16_t i){if(auto* d=hg_item_data(i))return d->price;return 0;}
