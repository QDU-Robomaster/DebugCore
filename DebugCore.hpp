#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: 终端调试命令工具库：为 Module 的 RamFS 命令文件提供 once / monitor 命令解析与字段打印 / Terminal debug command utilities that provide once / monitor command parsing and field printing for Module RamFS command files
standalone: false
depends: []
=== END MANIFEST === */
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "thread.hpp"

namespace debug_core
{

/**
 * @brief 视图名称与视图值的映射项。
 *        Mapping entry between a view name and a view value.
 *
 * @tparam View 视图值类型。
 *              View value type.
 */
template <typename View>
struct ViewEntry
{
  const char* name;  ///< 视图名称 View name
  View view;         ///< 视图值 View value
};

/**
 * @brief 按视图表解析视图名。
 *        Parse a view name with a view table.
 *
 * @tparam View 视图值类型。
 *              View value type.
 * @tparam N 视图表大小。
 *           View table size.
 * @param arg 视图字符串。
 *            View string.
 * @param table 视图映射表。
 *              View mapping table.
 * @param out 输出视图值。
 *            Output view value.
 * @return 解析成功为 true；`arg` 或 `out` 为空、名称不在表中时为 false。
 *         True on success; false when `arg` or `out` is null or the name is not in the
 *         table.
 */
template <typename View, size_t N>
bool parse_view_table(const char* arg, const std::array<ViewEntry<View>, N>& table,
                      View* out)
{
  if (arg == nullptr || out == nullptr)
  {
    return false;
  }
  for (const auto& item : table)
  {
    if (std::strcmp(arg, item.name) == 0)
    {
      *out = item.view;
      return true;
    }
  }
  return false;
}

/**
 * @brief 解析 `uint8_t` 视图名。
 *        Parse a `uint8_t` view name.
 *
 * @tparam N 视图表大小。
 *           View table size.
 * @param arg 视图字符串。
 *            View string.
 * @param table 视图映射表。
 *              View mapping table.
 * @param out 输出视图值。
 *            Output view value.
 * @return 解析成功为 true。
 *         True on success.
 */
template <size_t N>
bool parse_view_name(const char* arg, const std::array<ViewEntry<uint8_t>, N>& table,
                     uint8_t* out)
{
  return parse_view_table(arg, table, out);
}

/**
 * @brief 根据视图值获取视图名。
 *        Get the view name of a view value.
 *
 * @tparam N 视图表大小。
 *           View table size.
 * @param view 视图值。
 *             View value.
 * @param table 视图映射表。
 *              View mapping table.
 * @param fallback 未找到时返回的字符串。
 *                 String returned when the view is not found.
 * @return 视图名字符串。
 *         View name string.
 */
template <size_t N>
const char* view_name(uint8_t view, const std::array<ViewEntry<uint8_t>, N>& table,
                      const char* fallback = "unknown")
{
  for (const auto& item : table)
  {
    if (item.view == view)
    {
      return item.name;
    }
  }
  return fallback;
}

/**
 * @brief 把成员命令函数转换为 RamFS 命令文件使用的函数。
 *        Convert a member command function into the function used by a RamFS command
 *        file.
 *
 * @tparam Owner 模块类型。
 *               Module type.
 * @tparam MemberFunc 成员命令函数。
 *                    Member command function.
 * @param self 模块实例。
 *             Module instance.
 * @param argc 参数数量。
 *             Argument count.
 * @param argv 参数数组。
 *             Argument array.
 * @return 命令返回值。
 *         Command return value.
 */
template <typename Owner, int (Owner::*MemberFunc)(int, char**)>
int command_thunk(Owner* self, int argc, char** argv)
{
  return (self->*MemberFunc)(argc, argv);
}

/**
 * @brief 解析并执行 `once` / `monitor` 命令。
 *        Parse and execute the `once` / `monitor` commands.
 *
 * @tparam View 视图类型。
 *              View type.
 * @tparam ParseViewFn 视图解析回调类型。
 *                     View parse callback type.
 * @tparam PrintOnceFn 单次打印回调类型。
 *                     Single-print callback type.
 * @tparam PrintUsageFn 帮助打印回调类型。
 *                      Help-print callback type.
 * @param argc 参数数量。
 *             Argument count.
 * @param argv 参数数组。
 *             Argument array.
 * @param default_view 默认视图。
 *                     Default view.
 * @param parse_view 视图解析回调。
 *                   View parse callback.
 * @param print_once 单次打印回调。
 *                   Single-print callback.
 * @param print_usage 帮助打印回调。
 *                    Help-print callback.
 * @return 成功为 0，参数错误为 -1。
 *         0 on success, -1 on an argument error.
 */
template <typename View, typename ParseViewFn, typename PrintOnceFn,
          typename PrintUsageFn>
int run_command(int argc, char** argv, View default_view, ParseViewFn parse_view,
                PrintOnceFn print_once, PrintUsageFn print_usage)
{
  if (argc <= 1)
  {
    print_usage();
    return 0;
  }

  if (std::strcmp(argv[1], "monitor") == 0)
  {
    if (argc == 2)
    {
      print_once(default_view);
      return 0;
    }

    if (argc > 5)
    {
      LibXR::STDIO::Printf<"Error: Too many arguments for monitor.\r\n">();
      return -1;
    }

    int time_ms = std::atoi(argv[2]);
    int interval_ms = 1000;
    View view = default_view;
    bool third_is_view = false;

    if (argc >= 4)
    {
      View parsed_view = default_view;
      if (parse_view(argv[3], &parsed_view))
      {
        view = parsed_view;
        third_is_view = true;
      }
      else
      {
        interval_ms = std::atoi(argv[3]);
      }
    }

    if (argc == 5)
    {
      if (third_is_view)
      {
        LibXR::STDIO::Printf<
            "Error: Invalid monitor args. Use monitor <time_ms> [interval_ms] "
            "[view].\r\n">();
        return -1;
      }
      if (!parse_view(argv[4], &view))
      {
        LibXR::STDIO::Printf<"Error: Unknown view '%s'.\r\n">(argv[4]);
        return -1;
      }
    }

    if (time_ms <= 0 || interval_ms <= 0)
    {
      LibXR::STDIO::Printf<"Error: time_ms and interval_ms must be > 0.\r\n">();
      return -1;
    }

    int elapsed = 0;
    while (elapsed < time_ms)
    {
      print_once(view);
      LibXR::Thread::Sleep(interval_ms);
      elapsed += interval_ms;
    }
    return 0;
  }

  if (std::strcmp(argv[1], "once") == 0)
  {
    if (argc > 3)
    {
      LibXR::STDIO::Printf<"Error: Too many arguments for once.\r\n">();
      return -1;
    }

    View view = default_view;
    if (argc == 3 && !parse_view(argv[2], &view))
    {
      LibXR::STDIO::Printf<"Error: Unknown view '%s'.\r\n">(argv[2]);
      return -1;
    }

    print_once(view);
    return 0;
  }

  View direct_view = default_view;
  if (argc == 2 && parse_view(argv[1], &direct_view))
  {
    print_once(direct_view);
    return 0;
  }

  LibXR::STDIO::Printf<"Error: Unknown command '%s'.\r\n">(argv[1]);
  return -1;
}

using ViewMask = uint32_t;

/**
 * @brief 根据视图编号生成掩码位。
 *        Generate the mask bit of a view number.
 *
 * @param view 视图编号。
 *             View number.
 * @return 视图掩码。
 *         View mask.
 */
constexpr ViewMask view_bit(uint8_t view) { return 1u << view; }

/**
 * @brief Structured 模式字段描述。
 *        Field descriptor of the Structured mode.
 */
struct FieldDesc
{
  const char* name;    ///< 字段名 Field name
  size_t offset;       ///< 字段在快照中的偏移 Offset of the field in the snapshot
  ViewMask view_mask;  ///< 包含该字段的视图掩码 Mask of the views that contain the field
  void (*print)(const char* name, const void* field_ptr);  ///< 打印函数 Print function
};

/**
 * @brief Structured 模式提供器。
 *        Provider of the Structured mode.
 *
 * @tparam Snapshot 快照类型。
 *                  Snapshot type.
 */
template <typename Snapshot>
struct StructuredProvider
{
  const char* module_name;  ///< 打印的模块名 Module name printed
  const char* view_help;    ///< 帮助中的视图列表 View list shown in the help
  bool (*parse_view)(const char* arg, uint8_t* out_view);  ///< 视图名解析 View parser
  const char* (*view_to_string)(uint8_t view);  ///< 视图值转名称 View value to name
  void (*capture)(void* self, Snapshot* out_snapshot);  ///< 抓取快照 Capture a snapshot
  const FieldDesc* fields;                              ///< 字段表 Field table
  size_t field_count;                                   ///< 字段数量 Number of fields
};

/**
 * @brief 按偏移指针打印布尔字段。
 *        Print a boolean field from a field pointer.
 *
 * @param name 字段名。
 *             Field name.
 * @param field_ptr 指向字段值的指针。
 *                  Pointer to the field value.
 */
inline void print_bool_field(const char* name, const void* field_ptr)
{
  bool value = *reinterpret_cast<const bool*>(field_ptr);
  LibXR::STDIO::Printf<"  %s=%s\r\n">(name, value ? "true" : "false");
}

/**
 * @brief 按偏移指针打印 uint8 字段。
 *        Print a uint8 field from a field pointer.
 *
 * @param name 字段名。
 *             Field name.
 * @param field_ptr 指向字段值的指针。
 *                  Pointer to the field value.
 */
inline void print_u8_field(const char* name, const void* field_ptr)
{
  uint8_t value = *reinterpret_cast<const uint8_t*>(field_ptr);
  LibXR::STDIO::Printf<"  %s=%u\r\n">(name, static_cast<unsigned>(value));
}

/**
 * @brief 按偏移指针打印 float 字段，保留 4 位小数。
 *        Print a float field from a field pointer with 4 decimal places.
 *
 * @param name 字段名。
 *             Field name.
 * @param field_ptr 指向字段值的指针。
 *                  Pointer to the field value.
 */
inline void print_f32_field(const char* name, const void* field_ptr)
{
  float value = *reinterpret_cast<const float*>(field_ptr);
  LibXR::STDIO::Printf<"  %s=%.4f\r\n">(name, value);
}

/**
 * @brief 打印布尔值。
 *        Print a boolean value.
 *
 * @param name 字段名。
 *             Field name.
 * @param value 字段值。
 *              Field value.
 */
inline void print_bool_value(const char* name, bool value)
{
  LibXR::STDIO::Printf<"  %s=%s\r\n">(name, value ? "true" : "false");
}

/**
 * @brief 打印 uint8 值。
 *        Print a uint8 value.
 *
 * @param name 字段名。
 *             Field name.
 * @param value 字段值。
 *              Field value.
 */
inline void print_u8_value(const char* name, uint8_t value)
{
  LibXR::STDIO::Printf<"  %s=%u\r\n">(name, static_cast<unsigned>(value));
}

/**
 * @brief 打印 float 值，保留 4 位小数。
 *        Print a float value with 4 decimal places.
 *
 * @param name 字段名。
 *             Field name.
 * @param value 字段值。
 *              Field value.
 */
inline void print_f32_value(const char* name, float value)
{
  LibXR::STDIO::Printf<"  %s=%.4f\r\n">(name, value);
}

/**
 * @brief Live 模式字段描述。
 *        Field descriptor of the Live mode.
 *
 * @tparam Owner 模块类型。
 *               Module type.
 */
template <typename Owner>
struct LiveFieldDesc
{
  const char* name;    ///< 字段名 Field name
  ViewMask view_mask;  ///< 包含该字段的视图掩码 Mask of the views that contain the field
  void (*print)(const char* name, const Owner* self);  ///< 打印函数 Print function
};

/**
 * @brief 执行 Live 模式命令，按当前对象实时读取字段。
 *        Execute a Live mode command, reading the fields from the current object in real
 *        time.
 *
 * @tparam Owner 模块类型。
 *               Module type.
 * @tparam ViewCount 视图数量。
 *                   Number of views.
 * @param self 模块实例。
 *             Module instance.
 * @param module_name 打印的模块名。
 *                    Module name printed.
 * @param view_help 帮助中的视图列表。
 *                  View list shown in the help.
 * @param view_table 视图映射表。
 *                   View mapping table.
 * @param fields 字段表。
 *               Field table.
 * @param field_count 字段数量。
 *                    Number of fields.
 * @param argc 参数数量。
 *             Argument count.
 * @param argv 参数数组。
 *             Argument array.
 * @param default_view 默认视图，选中时打印全部字段。
 *                     Default view; selecting it prints all fields.
 * @param lock_self 每次打印前调用的加锁回调，可为空。
 *                  Lock callback called before each print; may be null.
 * @param unlock_self 每次打印后调用的解锁回调，可为空。
 *                    Unlock callback called after each print; may be null.
 * @return 成功为 0，参数错误为 -1。
 *         0 on success, -1 on an argument error.
 */
template <typename Owner, size_t ViewCount>
int run_live_command(Owner* self, const char* module_name, const char* view_help,
                     const std::array<ViewEntry<uint8_t>, ViewCount>& view_table,
                     const LiveFieldDesc<Owner>* fields, size_t field_count, int argc,
                     char** argv, uint8_t default_view,
                     void (*lock_self)(Owner*) = nullptr,
                     void (*unlock_self)(Owner*) = nullptr)
{
  auto parse_view = [&](const char* arg, uint8_t* out_view)
  { return parse_view_name(arg, view_table, out_view); };

  auto print_usage = [&]()
  {
    LibXR::STDIO::Printf<"Usage:\r\n">();
    LibXR::STDIO::Printf<"  monitor\r\n">();
    LibXR::STDIO::Printf<"  monitor <time_ms> [interval_ms] [%s]\r\n">(view_help);
    LibXR::STDIO::Printf<"  once [%s]\r\n">(view_help);
    LibXR::STDIO::Printf<"  %s\r\n">(view_help);
  };

  auto print_once = [&](uint8_t view)
  {
    if (lock_self != nullptr)
    {
      lock_self(self);
    }

    LibXR::STDIO::Printf<"[%u ms] %s %s\r\n">(
        static_cast<unsigned>(LibXR::Thread::GetTime()), module_name,
        view_name(view, view_table));

    bool is_full_view = (view == default_view);
    uint32_t selected_mask = view_bit(view);
    for (size_t i = 0; i < field_count; ++i)
    {
      const auto& f = fields[i];
      if (!is_full_view && (f.view_mask & selected_mask) == 0)
      {
        continue;
      }
      f.print(f.name, self);
    }

    if (unlock_self != nullptr)
    {
      unlock_self(self);
    }
  };

  return run_command(argc, argv, default_view, parse_view, print_once, print_usage);
}

/**
 * @brief 执行 Structured 模式命令，先抓取快照再按字段偏移打印。
 *        Execute a Structured mode command, capturing a snapshot and then printing by
 *        field offset.
 *
 * @tparam Snapshot 快照类型。
 *                  Snapshot type.
 * @param self 传给 `capture` 的模块实例。
 *             Module instance passed to `capture`.
 * @param provider 提供器。
 *                 Provider.
 * @param argc 参数数量。
 *             Argument count.
 * @param argv 参数数组。
 *             Argument array.
 * @param default_view 默认视图，选中时打印全部字段。
 *                     Default view; selecting it prints all fields.
 * @return 成功为 0，参数错误为 -1。
 *         0 on success, -1 on an argument error.
 */
template <typename Snapshot>
int run_structured_command(void* self, const StructuredProvider<Snapshot>& provider,
                           int argc, char** argv, uint8_t default_view)
{
  auto print_usage = [&]()
  {
    LibXR::STDIO::Printf<"Usage:\r\n">();
    LibXR::STDIO::Printf<"  monitor\r\n">();
    LibXR::STDIO::Printf<"  monitor <time_ms> [interval_ms] [%s]\r\n">(
        provider.view_help);
    LibXR::STDIO::Printf<"  once [%s]\r\n">(provider.view_help);
    LibXR::STDIO::Printf<"  %s\r\n">(provider.view_help);
  };

  auto print_once = [&](uint8_t view)
  {
    Snapshot snapshot{};
    provider.capture(self, &snapshot);

    auto current_view_name =
        provider.view_to_string ? provider.view_to_string(view) : "unknown";
    LibXR::STDIO::Printf<"[%u ms] %s %s\r\n">(
        static_cast<unsigned>(LibXR::Thread::GetTime()), provider.module_name,
        current_view_name);

    bool is_full_view = (view == default_view);
    uint32_t selected_mask = view_bit(view);
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&snapshot);
    for (size_t i = 0; i < provider.field_count; ++i)
    {
      const auto& f = provider.fields[i];
      if (!is_full_view && (f.view_mask & selected_mask) == 0)
      {
        continue;
      }
      const void* field_ptr = base + f.offset;
      f.print(f.name, field_ptr);
    }
  };

  return run_command(argc, argv, default_view, provider.parse_view, print_once,
                     print_usage);
}

}  // namespace debug_core

#define DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, mask, printer) \
  {#member, offsetof(SnapshotType, member), (mask), (printer)}
#define DEBUG_CORE_FIELD_F32(SnapshotType, member, mask) \
  DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, (mask), debug_core::print_f32_field)
#define DEBUG_CORE_FIELD_BOOL(SnapshotType, member, mask) \
  DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, (mask), debug_core::print_bool_field)
#define DEBUG_CORE_FIELD_U8(SnapshotType, member, mask) \
  DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, (mask), debug_core::print_u8_field)

#define DEBUG_CORE_LIVE_F32(OwnerType, name, mask, expr) \
  {(name), (mask),                                       \
   +[](const char* field_name, const OwnerType* self)    \
   { debug_core::print_f32_value(field_name, static_cast<float>((expr))); }}
#define DEBUG_CORE_LIVE_BOOL(OwnerType, name, mask, expr) \
  {(name), (mask),                                        \
   +[](const char* field_name, const OwnerType* self)     \
   { debug_core::print_bool_value(field_name, static_cast<bool>((expr))); }}
#define DEBUG_CORE_LIVE_U8(OwnerType, name, mask, expr) \
  {(name), (mask),                                      \
   +[](const char* field_name, const OwnerType* self)   \
   { debug_core::print_u8_value(field_name, static_cast<uint8_t>((expr))); }}
#define DEBUG_CORE_LIVE_CUSTOM(OwnerType, name, mask, printer) {(name), (mask), (printer)}

/**
 * @brief 终端调试命令工具库的 Module 类，构造后不创建线程、Topic 或命令。
 *        Module class of the terminal debug command utilities; construction creates no
 *        thread, Topic or command.
 */
class DebugCore
{
 public:
  /**
   * @brief 构造 DebugCore。
   *        Construct DebugCore.
   */
  DebugCore() {}
};
