#ifndef PROJECT_XS_BASE_STATE_REGISTRY_H
#define PROJECT_XS_BASE_STATE_REGISTRY_H

// 兼容过渡头：
// - 单状态与单状态目录请改用 State.h
// - 高维数组状态与数组目录请改用 StateArray.h

#include "base/State.h"
#include "base/StateArray.h"

namespace project_xs::sim {

// 兼容别名：
// 旧代码中的 StateRegistry 现在等价于高维数组状态目录。
using StateRegistry = StateArrayRegistry;

}  // namespace project_xs::sim

#endif
