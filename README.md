# DebugCore

终端调试命令工具库：为 Module 的 RamFS 命令文件提供 `once` / `monitor` 命令解析与字段打印 / Terminal debug command utilities that provide `once` / `monitor` command parsing and field printing for Module RamFS command files

## 1. 模块作用 / Purpose

DebugCore 的全部功能位于 `DebugCore.hpp` 的 `debug_core` 命名空间和 `DEBUG_CORE_*` 宏中，只有头文件。其他模块在自己的 manifest `depends` 中声明 `QDU-Robomaster/DebugCore` 并包含 `DebugCore.hpp`，例如 `QDU-Robomaster/Launcher` 用它实现 `launcher` 调试命令。同名的 `DebugCore` 类是空类。

`debug_core` 把 `once` / `monitor` 命令解析、参数检查和多视图（`state|cmd|pid|...`）字段打印流程集中在一处，使用它的模块只需提供视图表和字段表。

DebugCore provides all of its functionality in the `debug_core` namespace and the `DEBUG_CORE_*` macros of `DebugCore.hpp`, as a header-only library. Other Modules declare `QDU-Robomaster/DebugCore` in the `depends` of their manifest and include `DebugCore.hpp`; for example `QDU-Robomaster/Launcher` uses it to implement the `launcher` debug command. The `DebugCore` class of the same name is empty.

`debug_core` gathers the `once` / `monitor` command parsing, argument checking and multi-view (`state|cmd|pid|...`) field printing in one place, so that a Module using it only supplies a view table and a field table.

## 2. 终端命令格式 / Terminal Command Format

命令名由使用方在创建 RamFS 命令文件时决定，例如 `launcher`。通用子命令如下：

1. `<cmd>`：打印帮助。
2. `<cmd> once [view]`：按视图打印一次，省略视图时使用默认视图。
3. `<cmd> monitor`：按默认视图打印一次。
4. `<cmd> monitor <time_ms> [interval_ms] [view]`：每 `interval_ms`（默认 1000）打印一次，持续 `time_ms`，执行期间命令阻塞。`interval_ms` 位置上是视图名时按视图处理，此时间隔为 1000 ms，其后不再接受参数。
5. `<cmd> <view>`：按该视图打印一次。

参数错误时打印 `Error: ...` 并返回 `-1`。

每次打印先输出一行 `[<ms> ms] <模块名> <视图名>`，随后每个字段一行 `  name=value`，浮点数保留 4 位小数。选中默认视图时打印全部字段，选中其他视图时只打印字段掩码包含该视图的字段。

The command name is chosen by the user when creating the RamFS command file, for example `launcher`. The common subcommands are:

1. `<cmd>`: print the help.
2. `<cmd> once [view]`: print once for a view; the default view is used when the view is omitted.
3. `<cmd> monitor`: print once for the default view.
4. `<cmd> monitor <time_ms> [interval_ms] [view]`: print every `interval_ms` (default 1000) for `time_ms`; the command blocks while it runs. When the `interval_ms` position holds a view name, it is taken as the view; the interval is then 1000 ms and no further argument is accepted.
5. `<cmd> <view>`: print once for that view.

On an argument error, `Error: ...` is printed and `-1` is returned.

Each print starts with one line `[<ms> ms] <module name> <view name>`, followed by one line `  name=value` per field, with floating-point values at 4 decimal places. Selecting the default view prints all fields; selecting another view prints only the fields whose mask contains that view.

```bash
launcher once state
launcher monitor 5000 100 heat
launcher motor
```

## 3. 接入方式 / Integration

### 3.1 Live 模式 / Live Mode

Live 模式按当前对象实时读取字段，适用于字段来源是对象成员，或需要自定义打印格式（例如 PID 全参数、多行输出）的场景。

Live mode reads the fields from the current object in real time. It suits fields that come from object members, or custom print formats such as all PID parameters or multi-line output.

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

`run_live_command(...)` 接受 `lock_self` 与 `unlock_self`，在每次打印前后调用，用于保护共享状态。

`run_live_command(...)` accepts `lock_self` and `unlock_self`, which are called before and after each print to protect shared state.

### 3.2 Structured 模式 / Structured Mode

Structured 模式先调用 `capture` 抓取一次快照，再按字段偏移打印，适用于需要区分采样时刻与打印时刻，或把多个来源的字段整理成稳定快照的场景。

Structured mode first calls `capture` to take one snapshot and then prints by field offset. It suits cases that distinguish the sampling time from the print time, or that assemble fields from several sources into a stable snapshot.

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

模块名、视图帮助、视图解析函数、视图名函数、`capture` 函数和字段表填入 `debug_core::StructuredProvider<DebugSnapshot>`，再由 `debug_core::run_structured_command(self, provider, argc, argv, default_view)` 执行。

The module name, view help, view parse function, view name function, `capture` function and field table are filled into `debug_core::StructuredProvider<DebugSnapshot>`, which `debug_core::run_structured_command(self, provider, argc, argv, default_view)` then executes.

### 3.3 视图与字段 / Views and Fields

1. 视图表为 `std::array<ViewEntry<uint8_t>, N>`，`parse_view_name()` 与 `view_name()` 在名称和值之间转换。
2. 字段掩码由 `view_bit(view)` 生成，多个视图用 `|` 组合。
3. 字段宏：Structured 模式为 `DEBUG_CORE_FIELD_U8`、`DEBUG_CORE_FIELD_F32`、`DEBUG_CORE_FIELD_BOOL`、`DEBUG_CORE_FIELD_CUSTOM`；Live 模式为 `DEBUG_CORE_LIVE_U8`、`DEBUG_CORE_LIVE_F32`、`DEBUG_CORE_LIVE_BOOL`、`DEBUG_CORE_LIVE_CUSTOM`。
4. 传给运行函数的 `default_view`（例如 `full`）是默认视图，选中它时打印全部字段。

1. A view table is a `std::array<ViewEntry<uint8_t>, N>`; `parse_view_name()` and `view_name()` convert between names and values.
2. A field mask is generated by `view_bit(view)`; several views are combined with `|`.
3. Field macros: `DEBUG_CORE_FIELD_U8`, `DEBUG_CORE_FIELD_F32`, `DEBUG_CORE_FIELD_BOOL` and `DEBUG_CORE_FIELD_CUSTOM` for Structured mode; `DEBUG_CORE_LIVE_U8`, `DEBUG_CORE_LIVE_F32`, `DEBUG_CORE_LIVE_BOOL` and `DEBUG_CORE_LIVE_CUSTOM` for Live mode.
4. The `default_view` passed to the run function (for example `full`) is the default view; selecting it prints all fields.

### 3.4 注册为 RamFS 命令 / Registering as a RamFS Command

`command_thunk<Owner, &Owner::DebugCommand>` 把成员函数 `int DebugCommand(int argc, char** argv)` 转换为 RamFS 命令文件使用的函数。`QDU-Robomaster/Launcher` 的写法：

`command_thunk<Owner, &Owner::DebugCommand>` converts the member function `int DebugCommand(int argc, char** argv)` into the function used by a RamFS command file. As written in `QDU-Robomaster/Launcher`:

```cpp
cmd_file_(LibXR::RamFS::CreateFile(
    "launcher",
    debug_core::command_thunk<LauncherType, &LauncherType::DebugCommand>,
    &launcher_))
// 构造函数体内 / in the constructor body
ramfs.Add(cmd_file_);
```

### 3.5 调试实现的 `.inl` 引入 / `.inl` Inclusion of the Debug Implementation

`QDU-Robomaster/Launcher` 把调试实现放在 `.inl` 文件中，只在定义 `DEBUG` 的构建里编译，并避免循环包含。头文件末尾（`HeroLauncher.hpp`）：

`QDU-Robomaster/Launcher` keeps the debug implementation in `.inl` files that are compiled only in builds that define `DEBUG`, and avoids circular inclusion. At the end of the header (`HeroLauncher.hpp`):

```cpp
#ifdef DEBUG
#define HERO_LAUNCHER_DEBUG_IMPL
#include "HeroLauncherDebug.inl"
#undef HERO_LAUNCHER_DEBUG_IMPL
#endif
```

`HeroLauncherDebug.inl` 的文件头：

The head of `HeroLauncherDebug.inl`:

```cpp
#pragma once

#ifndef HERO_LAUNCHER_DEBUG_IMPL
#include "HeroLauncher.hpp"
#endif
```

仅在定义 `DEBUG` 时调试实现参与编译；单独包含 `.inl` 时引入类声明；从对应 `.hpp` 包含时，类声明只引入一次。

The debug implementation is compiled only when `DEBUG` is defined; including the `.inl` on its own brings in the class declaration; including it from the matching `.hpp` brings in the class declaration once.

## 4. 构造接口 / Constructor

```cpp
DebugCore();
```

无依赖，无配置参数。

No dependencies and no configuration parameters.

## 5. Topic

无 / None

## 6. 配置示例 / Configuration Example

`xrobot instance add QDU-Robomaster/DebugCore` 写入的实例：

An instance written by `xrobot instance add QDU-Robomaster/DebugCore`:

```yaml
modules:
  - module: QDU-Robomaster/DebugCore
    id: debugcore_0
```

## 7. 依赖与硬件 / Dependencies and Hardware

依赖：LibXR（`LibXR::STDIO::Printf`、`LibXR::Thread`）。

硬件：RamFS 命令文件所在的终端。

Dependencies: LibXR (`LibXR::STDIO::Printf`, `LibXR::Thread`).

Hardware: the terminal that serves the RamFS command files.
