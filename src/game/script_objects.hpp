#pragma once

// Retail special field-object IDs from include/constants/scrcmd.h.
// These are virtual field actors and must never be put in the normal map-NPC
// movement queue: there is no HgOverworldEvent with these IDs to consume it.
constexpr int HG_SCRIPT_OBJ_PARTNER_POKE = 253;
constexpr int HG_SCRIPT_OBJ_CAMERA       = 254;
constexpr int HG_SCRIPT_OBJ_PLAYER       = 255;

inline constexpr bool hg_script_object_is_virtual(int objectId){
    return objectId == HG_SCRIPT_OBJ_PARTNER_POKE || objectId == HG_SCRIPT_OBJ_CAMERA;
}

inline constexpr bool hg_script_object_queues_runtime_npc(int objectId, bool runtimeNpcExists){
    return objectId != HG_SCRIPT_OBJ_PLAYER && !hg_script_object_is_virtual(objectId) && runtimeNpcExists;
}
