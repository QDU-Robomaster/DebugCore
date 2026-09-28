# DebugCore

用于给模块快速挂接终端调试命令的通用工具，适配 RamFS 命令文件入口。

全部功能在 `DebugCore.hpp` 的 `debug_core` 命名空间和 `DEBUG_CORE_*` 宏中，只有头文件。其他模块
在自己的 manifest `depends` 中声明 `QDU-Robomaster/DebugCore` 并包含 `DebugCore.hpp`，例如
`QDU-Robomaster/Launcher` 用它实现 `launcher` 调试命令。同名的 `DebugCore` 类是空的占位模块，
构造后不做任何事。

## 解决什么问题

1. 统一 `once` / `monitor` 命令解析逻辑。
2. 统一多视图（`state|cmd|pid|...`）字段打印流程。
3. 减少每个模块重复写命令解析、参数检查、打印框架代码。

## 终端命令格式

命令名由使用方在创建 RamFS 命令文件时决定，例如 `launcher`。通用子命令：

1. `<cmd>`：打印帮助。
2. `<cmd> once [view]`：按视图打印一次，省略视图时用默认视图。
3. `<cmd> monitor`：按默认视图打印一次。
4. `<cmd> monitor <time_ms> [interval_ms] [view]`：每 `interval_ms`（默认 1000）打印一次，
   持续 `time_ms`；`interval_ms` 位置若是视图名则当作视图（此时间隔为 1000 ms，后面不能再跟参数）。
   执行期间命令阻塞。
5. `<cmd> <view>`：按该视图打印一次。

参数错误时打印 `Error: ...` 并返回 `-1`。

每次打印输出一行 `[<ms> ms] <模块名> <视图名>`，随后每个字段一行 `  name=value`
（浮点保留 4 位小数）。选中默认视图时打印全部字段，其他视图只打印掩码包含该视图的字段。

示例：

```bash
launcher once state
launcher monitor 5000 100 heat
launcher motor
```

## 两种接入方式

### 1) Live 模式

直接按当前对象实时读取字段，不需要定义 snapshot 结构体。适用于字段来源就是当前对象成员，
或需要自定义打印格式（例如 PID 全参数、多行输出）的场景。

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

先调用 `capture` 抓取一次快照，再按字段偏移打印。适用于想明确区分“采样时刻”和“打印时刻”，
或需要把多个来源字段整理成稳定快照的场景。

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

把模块名、视图帮助、视图解析函数、视图名函数、`capture` 函数和字段表填入
`debug_core::StructuredProvider<DebugSnapshot>`，再调用
`debug_core::run_structured_command(self, provider, argc, argv, default_view)`。

## 视图和字段约定

1. 视图表：`std::array<ViewEntry<uint8_t>, N>`；`parse_view_name()` / `view_name()` 在名称和值之间转换。
2. 字段掩码：`view_bit(view)` 生成，多个视图用 `|` 组合。
3. 字段宏：
   - Structured：`DEBUG_CORE_FIELD_U8` / `F32` / `BOOL` / `CUSTOM`
   - Live：`DEBUG_CORE_LIVE_U8` / `F32` / `BOOL` / `CUSTOM`
4. 建议保留 `full` 作为默认视图，便于一次性打印全部字段。

## 注册为 RamFS 命令

`command_thunk<Owner, &Owner::DebugCommand>` 把成员函数 `int DebugCommand(int argc, char** argv)`
转成 RamFS 命令文件需要的函数。`QDU-Robomaster/Launcher` 中的写法：

```cpp
cmd_file_(LibXR::RamFS::CreateFile(
    "launcher",
    debug_core::command_thunk<LauncherType, &LauncherType::DebugCommand>,
    &launcher_))
// 构造函数体内
ramfs.Add(cmd_file_);
```

## 并发与锁注意事项

`run_live_command(...)` 支持传入 `lock_self` / `unlock_self`，在每次打印前后调用，用于保护共享状态。

1. 仅在读取共享成员时加锁。
2. 避免在持锁区执行可能阻塞的外设操作（例如 CAN 发送、耗时 IO），否则终端命令可能卡住。

## `.inl` 引入写法

为了让调试实现只在定义 `DEBUG` 的构建中参与编译，并避免循环包含，`QDU-Robomaster/Launcher`
使用下面的模式。

头文件末尾（`HeroLauncher.hpp`）：

```cpp
#ifdef DEBUG
#define HERO_LAUNCHER_DEBUG_IMPL
#include "HeroLauncherDebug.inl"
#undef HERO_LAUNCHER_DEBUG_IMPL
#endif
```

`HeroLauncherDebug.inl` 文件头：

```cpp
#pragma once

#ifndef HERO_LAUNCHER_DEBUG_IMPL
#include "HeroLauncher.hpp"
#endif
```

效果：

1. 未定义 `DEBUG` 时不编译调试实现。
2. 直接单独包含 `.inl` 时也能拿到类声明。
3. 正常从对应 `.hpp` 包含时不会重复反向包含。

## 依赖

无其他模块依赖，仅使用 LibXR（`LibXR::STDIO::Printf`、`LibXR::Thread`）。

## 构造接口

```cpp
DebugCore();
```

无依赖项，无配置项。`DebugCore` 类只是占位，不创建线程、topic 或命令。

## 使用

使用 DebugCore 的模块通过 `depends` 自动引入它，一般无需手动添加；需要单独引入时：

```sh
xrobot module add QDU-Robomaster/DebugCore
xrobot setup
```

使用 `debug_core` 工具不需要创建实例。manifest 没有声明 `standalone: false`，因此也可以用
`xrobot instance add QDU-Robomaster/DebugCore` 添加一个实例，但它不做任何事：

```yaml
modules:
  - module: QDU-Robomaster/DebugCore
    id: debugcore_0
```

添加实例后再次运行 `xrobot setup`，生成 `User/xrobot_main.hpp`。

`xrobot module show .`（在本仓库中）或 `xrobot module show Modules/QDU-Robomaster/DebugCore`
（在 BSP 中）打印当前的构造函数。
