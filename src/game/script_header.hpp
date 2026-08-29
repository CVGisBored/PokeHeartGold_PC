#pragma once
#include "game/hg_state.hpp"
#include <cstdint>
#include <vector>
struct HgFrameTrigger{std::uint16_t var=0,value=0,script=0;};
struct HgScriptHeader{bool valid=false;std::uint16_t transition=0,resume=0,load=0;std::vector<HgFrameTrigger> frame;};
HgScriptHeader parse_hg_script_header(const std::vector<unsigned char>& bytes);
std::uint16_t hg_triggered_frame_script(const HgScriptHeader& h,const HgGameState& state);
