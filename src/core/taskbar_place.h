#pragma once

namespace vela {

// Where the taskbar view sits along a horizontal taskbar.
enum class TaskbarSpot { Clock = 0, Left = 1, Custom = 2 };

// The free stretch of the taskbar the view can use, in taskbar pixels:
// from `start` (the left edge, past anything Windows puts there) to `end`
// (the notification area).
struct TaskbarRoom {
    int start;
    int end;
};

// Left edge of a view `width` pixels wide. `custom` is used for Custom only,
// and is kept inside the room.
int TaskbarViewLeft(TaskbarSpot spot, int custom, int width, TaskbarRoom room);

// The spot for a view dropped at `left`: one of the two ends when within
// `snap` pixels of it, Custom otherwise.
TaskbarSpot SnapTaskbarDrop(int left, int width, TaskbarRoom room, int snap);

}  // namespace vela
