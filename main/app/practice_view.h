#pragma once

#include <string>

namespace agentsphere {

enum class PracticePage { Hidden, Offline, Available, Checking, Unavailable, Entering, Active };

struct PracticeView {
    PracticePage page = PracticePage::Hidden;
    bool can_repeat = false;
    std::string caption;

    bool operator==(const PracticeView& other) const {
        return page == other.page && can_repeat == other.can_repeat && caption == other.caption;
    }
};

}  // namespace agentsphere
