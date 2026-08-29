#include "game/script_objects.hpp"
#include <cassert>
#include <iostream>

int main(){
    assert(HG_SCRIPT_OBJ_PARTNER_POKE==253);
    assert(HG_SCRIPT_OBJ_PLAYER==255);
    assert(hg_script_object_is_virtual(253));
    assert(!hg_script_object_queues_runtime_npc(253,false));
    assert(!hg_script_object_queues_runtime_npc(253,true));
    assert(!hg_script_object_queues_runtime_npc(255,true));
    assert(!hg_script_object_queues_runtime_npc(7,false));
    assert(hg_script_object_queues_runtime_npc(7,true));
    std::cout<<"script_object_test: PASS\n";
}
