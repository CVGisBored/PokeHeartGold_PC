#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

inline bool hgLooksLikeRetailNitroFs(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_directory(p, ec)
        && std::filesystem::is_regular_file(p / "a/0/0/4", ec)
        && std::filesystem::is_regular_file(p / "a/0/8/1", ec)
        && std::filesystem::is_regular_file(p / "data/sound/gs_sound_data.sdat", ec);
}

inline std::filesystem::path hgExecutablePath(const char* argv0) {
    std::error_code ec;
    if (argv0 && *argv0) {
        auto p = std::filesystem::absolute(std::filesystem::path(argv0), ec);
        if (!ec) return p.lexically_normal();
    }
    return {};
}

inline std::filesystem::path hgFindRetailNitroFs(const char* argv0) {
    if (const char* env = std::getenv("HG_ASSETS")) {
        std::filesystem::path p(env);
        if (hgLooksLikeRetailNitroFs(p)) return p;
    }

    std::error_code ec;
    const auto cwd = std::filesystem::current_path(ec);
    const auto exe = hgExecutablePath(argv0);
    const auto exeDir = exe.empty() ? std::filesystem::path{} : exe.parent_path();

    std::vector<std::filesystem::path> candidates;
    if (!ec) candidates.push_back(cwd / "assets/nitrofs");
    if (!exeDir.empty()) {
        candidates.push_back(exeDir / "assets/nitrofs");
        candidates.push_back(exeDir / "../assets/nitrofs");
        candidates.push_back(exeDir / "../../assets/nitrofs");
    }

    for (auto p : candidates) {
        p = p.lexically_normal();
        if (hgLooksLikeRetailNitroFs(p)) return p;
    }
    return {};
}

inline std::string hgAssetSearchHint(const char* argv0) {
    const auto exe = hgExecutablePath(argv0);
    const auto exeDir = exe.empty() ? std::filesystem::path{} : exe.parent_path();
    std::string s = "Expected extracted HeartGold NitroFS at one of:\n"
                    "  ./assets/nitrofs\n";
    if (!exeDir.empty()) {
        s += "  " + (exeDir / "assets/nitrofs").lexically_normal().string() + "\n";
        s += "  " + (exeDir / "../assets/nitrofs").lexically_normal().string() + "\n";
    }
    s += "Or launch with --assets <path-to-nitrofs> / set HG_ASSETS.\n";
    return s;
}
