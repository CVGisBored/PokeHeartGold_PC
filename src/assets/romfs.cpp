#include "assets/romfs.hpp"
#include <array>
#include <fstream>
AssetStats scan_assets(const std::filesystem::path& root){ AssetStats s; if(!std::filesystem::exists(root)) return s; for(auto const& e:std::filesystem::recursive_directory_iterator(root)){ if(!e.is_regular_file())continue; ++s.files; s.bytes+=e.file_size(); std::ifstream f(e.path(),std::ios::binary); std::array<char,4> m{};f.read(m.data(),4);std::string magic(m.data(),size_t(f.gcount())); ++s.magic[magic]; if(magic=="NARC")++s.narc; if(magic=="BMD0")++s.models; if(magic=="BTX0")++s.textures; if(magic=="RGCN"||magic=="RECN"||magic=="RLCN")++s.sprites; if(magic=="SDAT")++s.soundArchives; } return s; }
