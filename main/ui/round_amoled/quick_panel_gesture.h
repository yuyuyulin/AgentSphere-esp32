#pragma once

#include <cstdlib>

namespace agentsphere {

inline bool OpensQuickPanel(int start_x, int start_y, int end_x, int end_y) {
    const int dy = end_y - start_y;
    return dy >= 35 && dy > std::abs(end_x - start_x);
}

inline bool OpensEnglishMenu(int start_x, int start_y, int end_x, int end_y) {
    const int dx = start_x - end_x;
    return dx >= 40 && dx > std::abs(end_y - start_y);
}

}  // namespace agentsphere
