#pragma once
#include "game/hg_state.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_set>

struct HgMysteryGiftConfig {
    std::string host="209.25.140.16";
    std::uint16_t port=11508;
    int timeoutMs=3000;
};

enum class HgMysteryGiftType { None, Item, Pokemon };

struct HgMysteryGiftPayload {
    std::string id;
    std::string title;
    std::string message;
    HgMysteryGiftType type=HgMysteryGiftType::None;
    std::uint16_t itemId=0;
    std::uint16_t quantity=0;
    HgMon pokemon{};
};

struct HgMysteryGiftFetchResult {
    bool transportOk=false;
    bool hasGift=false;
    bool alreadyClaimed=false;
    std::string status;
    std::string message;
    HgMysteryGiftPayload gift{};
};

HgMysteryGiftConfig hg_load_mystery_gift_config(const std::filesystem::path& savePath);
std::string hg_mystery_gift_client_id(const std::filesystem::path& savePath);
std::string hg_new_mystery_gift_save_id();
HgMysteryGiftFetchResult hg_fetch_mystery_gift(const HgMysteryGiftConfig& config,
                                                const std::string& clientId,
                                                const std::string& playerName,
                                                const std::unordered_set<std::string>& receivedGiftIds={});
bool hg_ack_mystery_gift(const HgMysteryGiftConfig& config,
                         const std::string& clientId,
                         const std::string& giftId,
                         std::string* error=nullptr);
bool hg_apply_mystery_gift(HgGameState& state,const HgMysteryGiftPayload& gift,std::string* description=nullptr);
