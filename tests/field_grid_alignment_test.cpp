#include "game/field_coords.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

static bool near(float a,float b){return std::fabs(a-b)<0.00001f;}

int main(){
    // New Bark Player House door is warp tile 695,396. Integer event coords
    // identify the tile, while world geometry uses its edges; the actor's feet
    // must be half a tile in from both edges.
    assert(near(hg_field_tile_center(695),173.875f));
    assert(near(hg_field_tile_center(396),99.125f));

    // Within matrix cell X=21, tile 695 is local tile 23. The cell begins at
    // 168 world units, so local tile center is 168+(23.5/4)=173.875.
    assert(near(hg_field_chunk_center(21)+((23.5f-16.0f)*0.25f),hg_field_tile_center(695)));

    // Continuous movement remains centered: halfway from tile 10 to 11 maps
    // halfway between their two geometric centers.
    assert(near(hg_field_tile_center(10.5f),(hg_field_tile_center(10)+hg_field_tile_center(11))*0.5f));
    std::cout<<"field_grid_alignment_test: PASS\n";
}
