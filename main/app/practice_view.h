#pragma once

#include <cstdint>
#include <string>

namespace agentsphere {

enum class PracticePage { Hidden, Offline, Available, Checking, Unavailable, Entering, Active };

struct PracticeView {
    PracticePage page = PracticePage::Hidden;
    bool can_repeat = false;
    std::string caption;
    std::string caption_zh{};
    uint32_t caption_revision = 0;

    bool SameCaption(const PracticeView& other) const {
        return caption_revision == other.caption_revision &&
               caption == other.caption && caption_zh == other.caption_zh;
    }

    bool operator==(const PracticeView& other) const {
        return page == other.page && can_repeat == other.can_repeat && SameCaption(other);
    }
};

}  // namespace agentsphere
