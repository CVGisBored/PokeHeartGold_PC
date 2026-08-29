#include "assets/pokemon_data.hpp"
#include "game/hg_state.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv){
  assert(argc>1);
  assert(hg_initialize_pokemon_database(std::filesystem::path(argv[1])));
  assert(hg_item_count()==536);
  assert(hg_rom_item_name(4)=="POKE BALL");
  assert(hg_rom_item_name(17)=="POTION");
  assert(hg_rom_item_name(328)=="TM01");
  assert(hg_rom_item_name(450)=="BICYCLE");
  assert(hg_rom_item_name(485)=="RED APRICORN");
  auto *ball=hg_item_data(4),*potion=hg_item_data(17),*tm=hg_item_data(328),*bike=hg_item_data(450),*enigma=hg_item_data(536);
  assert(ball&&potion&&tm&&bike&&enigma);
  assert(ball->price==200 && ball->fieldPocket==2);
  assert(potion->price==300 && potion->fieldPocket==1);
  assert(tm->fieldPocket==3);
  assert(bike->fieldPocket==7);
  assert(hg_item_pocket(485)==0); // Apricorns occupy the normal Items pocket in HGSS.
  HgGameState bagState;assert(bagState.addItem(328,99));assert(!bagState.addItem(328,1));assert(bagState.addItem(17,999));assert(!bagState.addItem(17,1));
  std::cout<<"Retail item database: "<<hg_item_count()<<" IDs; Poke Ball="<<ball->price<<" Bicycle pocket="<<unsigned(bike->fieldPocket)<<" Enigma Stone="<<hg_rom_item_name(536)<<"\n";
}
