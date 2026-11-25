#pragma once

#include <cstdlib>

namespace agentsphere {

inline bool OpensQuickPanel(int start_x, int start_y, int end_x, int end_y) {
    const int dy = end_y - start_y;
    return start_y <= 120 && dy >= 55 && dy > std::abs(end_x - start_x);
}

}  // namespace agentsphere
