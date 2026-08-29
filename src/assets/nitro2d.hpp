#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct NitroRgbaImage {
    bool valid=false;
    int width=0,height=0;
    std::vector<std::uint8_t> rgba;
    std::string error;
};

// Decode a Nitro 2D tiled background assembled from NCGR (RGCN),
// NSCR (RCSN) and NCLR (RLCN) resources. The output uses the exact tilemap,
// palette bank and flip bits stored in the ROM.
NitroRgbaImage decode_nitro_bg(const std::vector<unsigned char>& ncgr,
                               const std::vector<unsigned char>& nscr,
                               const std::vector<unsigned char>& nclr,
                               bool paletteIndexZeroTransparent=false);


// Decode a standalone Nitro NCGR character sheet using a matching NCLR palette.
// Tiles are laid out in the NCGR-declared tile grid (used by HG battle sprites).
NitroRgbaImage decode_nitro_char_sheet(const std::vector<unsigned char>& ncgr,
                                       const std::vector<unsigned char>& nclr,
                                       bool paletteIndexZeroTransparent=true,
                                       int paletteBank=0);

// Decode a HeartGold/SoulSilver Pokémon battle picture from pokegra. HG/SS
// stores each 160x80 4bpp pair of 80x80 animation frames behind the retail
// pokepic "unscan" LCRNG transform rather than ordinary NCGR tile ordering.
// frame selects 0 or 1 from the decoded 80x80 pair.
NitroRgbaImage decode_hg_pokepic(const std::vector<unsigned char>& ncgr,
                                 const std::vector<unsigned char>& nclr,
                                 bool paletteIndexZeroTransparent=true,
                                 int frame=0);


// Decode a Nitro NCER cell bank (OAM sprites) using the matching NCGR/NCLR.
// Each returned image is a 256x192 DS object layer with OAM coordinates centered
// at (128,96). This is used by the HG opening/New Game applications so their
// authored sprite composition is retained instead of substituting PC-only art.
std::vector<NitroRgbaImage> decode_nitro_cells(const std::vector<unsigned char>& ncgr,
                                                const std::vector<unsigned char>& ncer,
                                                const std::vector<unsigned char>& nclr,
                                                bool paletteIndexZeroTransparent=true);

// Nintendo DS LZ10 decompressor used by a number of HG intro/title 2D resources.
// Returns an empty vector when the header/stream is invalid.
std::vector<unsigned char> nitro_lz10_decompress(const std::vector<unsigned char>& input);

struct NitroNanrFrame {
    std::uint16_t cellIndex=0;
    std::uint16_t duration=1;
};
struct NitroNanrSequence {
    std::vector<NitroNanrFrame> frames;
    std::uint16_t startFrame=0;
    std::uint16_t elementType=0;
    std::uint16_t animationType=1;
    std::uint32_t playbackMode=0; // 0 forward, 1 loop, 2 ping-pong, 3 ping-pong loop
};
struct NitroNanrBank {
    bool valid=false;
    std::vector<NitroNanrSequence> sequences;
    std::string error;
};
NitroNanrBank decode_nitro_nanr(const std::vector<unsigned char>& nanr);
std::size_t sample_nitro_nanr_cell(const NitroNanrBank& bank,std::size_t sequence,double seconds,double framesPerSecond=60.0);
