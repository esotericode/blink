#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace observatory {
// Offline hardware reference, independent of any ROM and of emulator execution.
// Bodies are authored HTML; selection values are escaped separately by the UI.
struct InformationArticle {
    std::string id, title, summary, how, uses, limits;
    std::vector<std::string> related;
};
const std::vector<InformationArticle>& informationArticles();
const InformationArticle* informationArticle(const std::string& id);
std::string informationTopicForAddress(std::uint16_t address);
std::string informationTopicForGlossary(const std::string& key);
} // namespace observatory
