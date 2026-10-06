#include "teaching/information.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <utility>

using namespace observatory;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        std::set<std::string> ids;
        for (const auto& a : informationArticles()) {
            require(ids.insert(a.id).second, "Duplicate article identity");
            require(!a.title.empty() && a.summary.size() > 80 && a.how.size() > 180 && a.uses.size() > 80 && a.limits.size() > 100,
                    "Article is missing a substantive explanation section");
            require(a.related.size() >= 3, "Article has no path to related concepts");
            for (const auto& related : a.related) require(informationArticle(related) && related != a.id, "Broken or self-referencing topic link");
        }
        require(ids.size() >= 35, "Hardware reference lost a major topic family");
        for (unsigned a = 0; a <= 0xFFFF; ++a)
            require(informationArticle(informationTopicForAddress(std::uint16_t(a))), "An address has no hardware explanation");
        const std::pair<std::uint16_t, const char*> cases[] = {
            {0x7FFF,"rom"}, {0x8000,"tile-data"}, {0x97FF,"tile-data"}, {0x9800,"tile-map"}, {0x9FFF,"tile-map"},
            {0xA000,"cart-ram"}, {0xC000,"wram"}, {0xDFFF,"wram"}, {0xE000,"echo"}, {0xFDFF,"echo"},
            {0xFE00,"oam"}, {0xFE9F,"oam"}, {0xFEA0,"unusable"}, {0xFEFF,"unusable"}, {0xFF00,"joypad"},
            {0xFF02,"serial"}, {0xFF03,"io"}, {0xFF04,"timers"}, {0xFF0F,"interrupts"}, {0xFF30,"audio"},
            {0xFF40,"lcdc"}, {0xFF41,"stat"}, {0xFF42,"scroll"}, {0xFF46,"dma"}, {0xFF49,"palettes"},
            {0xFF4B,"window"}, {0xFF50,"boot"}, {0xFF7F,"io"}, {0xFF80,"hram"}, {0xFFFE,"hram"}, {0xFFFF,"interrupts"}
        };
        for (const auto& [a, topic] : cases) require(informationTopicForAddress(a) == topic, "A region boundary or hardware register selects the wrong concept");
        require(informationTopicForGlossary("reg.sp") == "stack" && informationTopicForGlossary("flag.c") == "flags", "Register/flag topic mapping wrong");
        require(!informationArticle("unknown"), "Unknown topic should be rejected");
        std::cout << "PASS information reference: " << ids.size() << " detailed articles, linked concepts, all addresses covered and region/IO boundaries checked\n";
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
