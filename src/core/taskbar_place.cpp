#include "core/taskbar_place.h"

#include <algorithm>

namespace vela {

int TaskbarViewLeft(TaskbarSpot spot, int custom, int width, TaskbarRoom room) {
    const int last = std::max(room.start, room.end - width);
    switch (spot) {
        case TaskbarSpot::Left: return room.start;
        case TaskbarSpot::Custom: return std::clamp(custom, room.start, last);
        case TaskbarSpot::Clock: break;
    }
    return last;
}

TaskbarSpot SnapTaskbarDrop(int left, int width, TaskbarRoom room, int snap) {
    if (left - room.start <= snap) return TaskbarSpot::Left;
    if (room.end - width - left <= snap) return TaskbarSpot::Clock;
    return TaskbarSpot::Custom;
}

}  // namespace vela
