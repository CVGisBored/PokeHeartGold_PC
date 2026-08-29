#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct HgDecodedMessage {
    bool valid=false;
    std::string text;
    std::vector<std::string> pages;
};

class HgMessageBank {
public:
    HgMessageBank()=default;
    explicit HgMessageBank(std::vector<unsigned char> bytes);
    bool valid() const { return valid_; }
    std::size_t count() const { return count_; }
    HgDecodedMessage decode(std::size_t index,const std::string& playerName="PLAYER",const std::vector<std::string>& strvars={}) const;
private:
    std::vector<unsigned char> bytes_;
    bool valid_=false;
    std::uint16_t count_=0,key_=0;
};

std::string hg_ascii_from_code(std::uint16_t code);
