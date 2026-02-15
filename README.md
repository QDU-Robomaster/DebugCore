# DebugCore

用于给模块快速挂接终端调试命令的通用工具，适配 RamFS 命令入口。

## 解决什么问题

DebugCore 主要解决三件事：

1. 统一 `once` / `monitor` 命令解析逻辑。
2. 统一多视图（`state|cmd|pid|...`）字段打印流程。
3. 减少每个模块重复写命令解析、参数检查、打印框架代码。

## 终端命令格式

每个模块命令名由你在 `run_live_command(...)` / `StructuredProvider` 里定义，例如 `gimbal`、`launcher`、`chassis`。

通用子命令：

1. `module once [view]`
2. `module monitor <time_ms> [interval_ms] [view]`
3. `module <view>`
4. `module`（打印帮助）

示例：

```bash
gimbal once state
gimbal monitor 5000 100 pid
launcher monitor 3000 200 heat
chassis motion
```

## 两种接入方式

### 1) Live 模式（推荐）

直接按当前对象实时读取字段，不需要定义 snapshot 结构体。

适用场景：

1. 字段来源就是当前对象成员。
2. 需要自定义打印格式（例如 PID 全参数、多行输出）。

最小示例：

```cpp
enum class DebugView : uint8_t { STATE, FULL };
constexpr uint8_t view_state = static_cast<uint8_t>(DebugView::STATE);
constexpr uint8_t view_full = static_cast<uint8_t>(DebugView::FULL);
constexpr auto mask_state = debug_core::view_bit(view_state);

static constexpr std::array<debug_core::ViewEntry<uint8_t>, 2> view_table{{
    {"state", view_state},
    {"full", view_full},
}};

static const debug_core::LiveFieldDesc<MyModule> fields[] = {
    DEBUG_CORE_LIVE_U8(MyModule, "state", mask_state, self->state_),
    DEBUG_CORE_LIVE_F32(MyModule, "dt_s", mask_state, self->dt_),
};

return debug_core::run_live_command(
    this, "my_module", "state|full", view_table, fields,
    sizeof(fields) / sizeof(fields[0]), argc, argv, view_full);
```

### 2) Structured 模式

先抓取一次快照，再按字段偏移打印。

适用场景：

1. 想明确区分“采样时刻”和“打印时刻”。
2. 需要把多个来源字段整理成稳定快照。

最小示例：

```cpp
struct DebugSnapshot {
  uint8_t state;
  float dt;
};

static const debug_core::FieldDesc fields[] = {
    DEBUG_CORE_FIELD_U8(DebugSnapshot, state, mask_state),
    DEBUG_CORE_FIELD_F32(DebugSnapshot, dt, mask_state),
};
```

配合 `debug_core::StructuredProvider<T>` 与 `run_structured_command(...)` 使用。

## 视图和字段约定

建议保留 `full` 作为默认视图，便于一次性排查问题。

1. 视图表：`ViewEntry<uint8_t>` 数组。
2. 字段掩码：`view_bit(view_xxx)` 生成。
3. 字段宏：
   - Structured：`DEBUG_CORE_FIELD_U8/F32/BOOL/...`
   - Live：`DEBUG_CORE_LIVE_U8/F32/BOOL/CUSTOM`

## 并发与锁注意事项

`run_live_command(...)` 支持传入 `lock_self` / `unlock_self`，用于打印时保护共享状态。

推荐做法：

1. 仅在读取共享成员时加锁。
2. 避免在持锁区执行可能阻塞的外设操作（例如 CAN 发送、耗时 IO），否则终端命令可能卡住。

## `.inl` 引入写法（推荐）

为了让调试实现只在 Debug 构建参与编译，并避免循环包含，建议使用下面这种模式。

头文件末尾（例如 `Mecanum.hpp`）：

```cpp
#ifdef DEBUG
#define MECANUM_CHASSIS_DEBUG_IMPL
#include "MecanumDebug.inl"
#undef MECANUM_CHASSIS_DEBUG_IMPL
#endif
```

`MecanumDebug.inl` 文件头：

```cpp
#pragma once

#ifndef MECANUM_CHASSIS_DEBUG_IMPL
#include "Mecanum.hpp"
#endif
```

这套写法的效果：

1. Release 不会编译调试实现。
2. 直接单独包含 `.inl` 时也能拿到类声明。
3. 正常从对应 `.hpp` 包含时不会重复反向包含。

## 模块信息

1. Required Hardware：None
2. Constructor Arguments：None
3. Template Arguments：None
4. Depends：None
