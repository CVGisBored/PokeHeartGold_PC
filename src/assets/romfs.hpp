#pragma once
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
struct AssetStats { size_t files=0,bytes=0,narc=0,models=0,textures=0,sprites=0,soundArchives=0; std::map<std::string,size_t> magic; };
AssetStats scan_assets(const std::filesystem::path& root);
