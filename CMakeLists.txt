cmake_minimum_required(VERSION 3.16)
project(heartgold_native LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(WIN32)
  set(HG_PLATFORM_SOURCE src/platform/vulkan_win32.cpp)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(XCB REQUIRED xcb)
  set(HG_PLATFORM_SOURCE src/platform/vulkan_xcb.cpp)
endif()
add_executable(heartgold_native
  src/main.cpp
  ${HG_PLATFORM_SOURCE}
  src/assets/romfs.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/assets/land_data.cpp
  src/assets/overworld_data.cpp
  src/assets/nitro2d.cpp
  src/assets/hg_text.cpp
  src/assets/wild_encounter.cpp
  src/assets/pokemon_data.cpp
  src/assets/trainer_data.cpp
  src/assets/sdat.cpp
  src/game/game.cpp
  src/game/hg_state.cpp
  src/game/retail_mart.cpp
  src/game/hg_script.cpp
  src/game/script_header.cpp
  src/game/rom_world.cpp)
target_include_directories(heartgold_native PRIVATE src)
if(WIN32)
  target_link_libraries(heartgold_native PRIVATE user32 gdi32 winmm)
  target_compile_definitions(heartgold_native PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_options(heartgold_native PRIVATE -static)
else()
  target_include_directories(heartgold_native PRIVATE ${XCB_INCLUDE_DIRS})
  target_link_libraries(heartgold_native PRIVATE ${XCB_LIBRARIES} dl)
endif()
target_compile_options(heartgold_native PRIVATE -Wall -Wextra -Wpedantic -Wno-cast-function-type)

include(CTest)
if(BUILD_TESTING)
add_executable(hg_script_vm_test
  tests/hg_script_vm_test.cpp
  src/game/hg_state.cpp
  src/game/hg_script.cpp  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
)
target_include_directories(hg_script_vm_test PRIVATE src)
target_compile_options(hg_script_vm_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME hg_script_vm_core COMMAND hg_script_vm_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(script_coverage_audit
  tests/script_coverage_audit.cpp
  src/assets/overworld_data.cpp
  src/assets/land_data.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/game/hg_script.cpp
  src/game/hg_state.cpp
  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp)
target_include_directories(script_coverage_audit PRIVATE src)
target_compile_options(script_coverage_audit PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)

add_executable(map_script_coverage_audit
  tests/map_script_coverage_audit.cpp
  src/assets/overworld_data.cpp
  src/assets/land_data.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/game/hg_script.cpp
  src/game/hg_state.cpp
  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp)
target_include_directories(map_script_coverage_audit PRIVATE src)
target_compile_options(map_script_coverage_audit PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)

add_executable(script_header_audit
  tests/script_header_audit.cpp
  src/assets/overworld_data.cpp
  src/assets/land_data.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/game/script_header.cpp
  src/game/hg_state.cpp
  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp)
target_include_directories(script_header_audit PRIVATE src)
target_compile_options(script_header_audit PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_script_header_surface
  COMMAND script_header_audit ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(global_script_table_test
  tests/global_script_table_test.cpp
  src/assets/overworld_data.cpp
  src/assets/land_data.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/game/hg_script.cpp
  src/game/hg_state.cpp
  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp)
target_include_directories(global_script_table_test PRIVATE src)
target_compile_options(global_script_table_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_global_script_dispatch
  COMMAND global_script_table_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(sdat_bank_test
  tests/sdat_bank_test.cpp
  src/assets/sdat.cpp)
target_include_directories(sdat_bank_test PRIVATE src)
if(WIN32)
  target_link_libraries(sdat_bank_test PRIVATE winmm)
else()
  target_link_libraries(sdat_bank_test PRIVATE dl)
endif()
target_compile_options(sdat_bank_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME sdat_bank_decode COMMAND sdat_bank_test)

add_executable(overworld_anim_test tests/overworld_anim_test.cpp)
target_include_directories(overworld_anim_test PRIVATE src)
target_compile_options(overworld_anim_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME overworld_walk_animation COMMAND overworld_anim_test)

add_executable(new_bark_script_test
  tests/new_bark_script_test.cpp
  src/game/hg_state.cpp
  src/game/hg_script.cpp  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
)
target_include_directories(new_bark_script_test PRIVATE src)
target_compile_options(new_bark_script_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME new_bark_script_surface COMMAND new_bark_script_test)

add_executable(script_object_test tests/script_object_test.cpp)
target_include_directories(script_object_test PRIVATE src)
target_compile_options(script_object_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_virtual_object_movement COMMAND script_object_test)

# Full native-host regression for the New Bark home/Mom/Pokegear progression.
# Uses the bundled extracted NitroFS so the test covers event flags, map warps,
# retail scripts, phone registration and persistent Pokegear card state together.
add_test(NAME new_bark_pokegear_flow
  COMMAND heartgold_native --assets ${CMAKE_SOURCE_DIR}/assets/nitrofs --pokegear-test)

add_executable(follower_palette_test
  tests/follower_palette_test.cpp
  src/assets/nsbmd.cpp
  src/assets/narc.cpp)
target_include_directories(follower_palette_test PRIVATE src)
target_compile_options(follower_palette_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME follower_normal_palette
  COMMAND follower_palette_test ${CMAKE_SOURCE_DIR}/assets/nitrofs/a/0/8/1)

add_executable(new_game_asset_test
  tests/new_game_asset_test.cpp
  src/assets/nitro2d.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp)
target_include_directories(new_game_asset_test PRIVATE src)
target_compile_options(new_game_asset_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME new_game_retail_actor_palettes
  COMMAND new_game_asset_test ${CMAKE_SOURCE_DIR}/assets/nitrofs/a/1/2/0)

add_executable(field_grid_alignment_test tests/field_grid_alignment_test.cpp)
target_include_directories(field_grid_alignment_test PRIVATE src)
target_compile_options(field_grid_alignment_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME field_grid_alignment COMMAND field_grid_alignment_test)

add_executable(pokemon_battle_data_test
  tests/pokemon_battle_data_test.cpp
  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/assets/land_data.cpp
  src/game/hg_state.cpp)
target_include_directories(pokemon_battle_data_test PRIVATE src)
target_compile_options(pokemon_battle_data_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_pokemon_battle_database COMMAND pokemon_battle_data_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(retail_mart_test
  tests/retail_mart_test.cpp
  src/game/retail_mart.cpp)
target_include_directories(retail_mart_test PRIVATE src)
target_compile_options(retail_mart_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_mart_inventory COMMAND retail_mart_test)

add_executable(item_data_test
  tests/item_data_test.cpp
  src/assets/pokemon_data.cpp
  src/assets/hg_text.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp
  src/game/hg_state.cpp)
target_include_directories(item_data_test PRIVATE src)
target_compile_options(item_data_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_item_database COMMAND item_data_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(battle_presentation_asset_test
  tests/battle_presentation_asset_test.cpp
  src/assets/nitro2d.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp)
target_include_directories(battle_presentation_asset_test PRIVATE src)
target_compile_options(battle_presentation_asset_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_battle_presentation_assets
  COMMAND battle_presentation_asset_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(battle_single_screen_layout_test tests/battle_single_screen_layout_test.cpp)
target_include_directories(battle_single_screen_layout_test PRIVATE src)
target_compile_options(battle_single_screen_layout_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME battle_single_screen_layout COMMAND battle_single_screen_layout_test)

add_executable(battle_pokepic_test
  tests/battle_pokepic_test.cpp
  src/assets/nitro2d.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp)
target_include_directories(battle_pokepic_test PRIVATE src)
target_compile_options(battle_pokepic_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_battle_pokepic_decode
  COMMAND battle_pokepic_test ${CMAKE_SOURCE_DIR}/assets/nitrofs/a/0/0/4)
add_test(NAME sequential_battle_turn COMMAND heartgold_native --assets ${CMAKE_SOURCE_DIR}/assets/nitrofs --battle-turn-test)

add_executable(nitro_nanr_test tests/nitro_nanr_test.cpp src/assets/nitro2d.cpp src/assets/narc.cpp)
target_include_directories(nitro_nanr_test PRIVATE src)
target_compile_options(nitro_nanr_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_nanr_playback COMMAND nitro_nanr_test ${CMAKE_SOURCE_DIR}/assets/nitrofs/a/0/0/6)

add_executable(trainer_back_cell_test tests/trainer_back_cell_test.cpp src/assets/nitro2d.cpp src/assets/narc.cpp)
target_include_directories(trainer_back_cell_test PRIVATE src)
target_compile_options(trainer_back_cell_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_trainer_back_vram_cells COMMAND trainer_back_cell_test ${CMAKE_SOURCE_DIR}/assets/nitrofs/a/0/0/6)

add_executable(retail_npc_sprite_test
  tests/retail_npc_sprite_test.cpp
  src/assets/overworld_data.cpp
  src/assets/land_data.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp)
target_include_directories(retail_npc_sprite_test PRIVATE src)
target_compile_options(retail_npc_sprite_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_world_npc_sprite_resolution
  COMMAND retail_npc_sprite_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_executable(runtime_asset_path_test tests/runtime_asset_path_test.cpp)
target_include_directories(runtime_asset_path_test PRIVATE src)
target_compile_options(runtime_asset_path_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME executable_relative_asset_discovery COMMAND runtime_asset_path_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)
add_executable(counter_tile_test
  tests/counter_tile_test.cpp
  src/assets/overworld_data.cpp
  src/assets/land_data.cpp
  src/assets/narc.cpp
  src/assets/nsbmd.cpp)
target_include_directories(counter_tile_test PRIVATE src)
target_compile_options(counter_tile_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_service_counter_tiles
  COMMAND counter_tile_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)

add_test(NAME retail_service_clerk_interaction
  COMMAND heartgold_native --assets ${CMAKE_SOURCE_DIR}/assets/nitrofs --clerk-test)

endif()

# v0.31: exercise the prepared, multi-band field raster path headlessly so
# worker synchronization cannot regress without CTest catching it.
add_test(NAME field_parallel_raster_smoke
  COMMAND heartgold_native --assets ${CMAKE_SOURCE_DIR}/assets/nitrofs --field-render-benchmark 3 60 695 397)

add_test(NAME battle_runtime_sprite_visibility
  COMMAND heartgold_native --assets ${CMAKE_SOURCE_DIR}/assets/nitrofs --battle-render-test)

if(BUILD_TESTING)
add_executable(battle_retail_cue_test tests/battle_retail_cue_test.cpp)
target_include_directories(battle_retail_cue_test PRIVATE src)
target_compile_options(battle_retail_cue_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_battle_cue_selection COMMAND battle_retail_cue_test)
endif()

if(BUILD_TESTING)
add_executable(battle_bgm_sdat_test tests/battle_bgm_sdat_test.cpp src/assets/sdat.cpp)
target_include_directories(battle_bgm_sdat_test PRIVATE src)
if(WIN32)
  target_link_libraries(battle_bgm_sdat_test PRIVATE winmm)
else()
  target_link_libraries(battle_bgm_sdat_test PRIVATE dl)
endif()
target_compile_options(battle_bgm_sdat_test PRIVATE -Wall -Wextra -Wpedantic -UNDEBUG)
add_test(NAME retail_battle_bgm_sdat COMMAND battle_bgm_sdat_test ${CMAKE_SOURCE_DIR}/assets/nitrofs)
endif()
