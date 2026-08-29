#pragma once

// HeartGold/SoulSilver field events, warps and movement state address tiles by
// integer cell index. Nitro map geometry, however, is expressed on tile edges.
// Rendering an event at x/4 therefore puts its feet on the left/top edge of the
// tile. The retail actor origin is the center of that addressed cell.
constexpr float hg_field_tile_center(float tileIndex){
    return (tileIndex + 0.5f) * 0.25f;
}

constexpr float hg_field_tile_center(int tileIndex){
    return hg_field_tile_center(float(tileIndex));
}

constexpr float hg_field_chunk_center(int matrixCell){
    // One matrix cell is 32 tiles = 8 native world units. Embedded land BMDs
    // are authored around the cell origin, so their placement is at +4 units.
    return float(matrixCell) * 8.0f + 4.0f;
}
