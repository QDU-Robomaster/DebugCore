#pragma once

// clang-format off
/* === MODULE MANIFEST V2 ===
module_description: Shared debug shell utilities
constructor_args: []
template_args: []
required_hardware: []
depends: []
=== END MANIFEST === */
// clang-format on

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "app_framework.hpp"
#include "libxr_def.hpp"
#include "libxr_rw.hpp"
#include "thread.hpp"

namespace debug_core {

template <typename View>
struct ViewEntry {
  const char* name;
  View view;
};

template <typename View, size_t N>
bool parse_view_table(const char* arg,
                      const std::array<ViewEntry<View>, N>& table, View* out) {
  if (arg == nullptr || out == nullptr) {
    return false;
  }
  for (const auto& item : table) {
    if (std::strcmp(arg, item.name) == 0) {
      *out = item.view;
      return true;
    }
  }
  return false;
}

template <size_t N>
bool parse_view_name(const char* arg,
                     const std::array<ViewEntry<uint8_t>, N>& table,
                     uint8_t* out) {
  return parse_view_table(arg, table, out);
}

template <size_t N>
const char* view_name(uint8_t view,
                      const std::array<ViewEntry<uint8_t>, N>& table,
                      const char* fallback = "unknown") {
  for (const auto& item : table) {
    if (item.view == view) {
      return item.name;
    }
  }
  return fallback;
}

template <typename Owner, int (Owner::*MemberFunc)(int, char**)>
int command_thunk(Owner* self, int argc, char** argv) {
  return (self->*MemberFunc)(argc, argv);
}

template <typename View, typename ParseViewFn, typename PrintOnceFn,
          typename PrintUsageFn>
int run_command(int argc, char** argv, View default_view,
                ParseViewFn parse_view, PrintOnceFn print_once,
                PrintUsageFn print_usage) {
  if (argc <= 1) {
    print_usage();
    return 0;
  }

  if (std::strcmp(argv[1], "monitor") == 0) {
    if (argc == 2) {
      print_once(default_view);
      return 0;
    }

    if (argc > 5) {
      LibXR::STDIO::Printf("Error: Too many arguments for monitor.\r\n");
      return -1;
    }

    int time_ms = std::atoi(argv[2]);
    int interval_ms = 1000;
    View view = default_view;
    bool third_is_view = false;

    if (argc >= 4) {
      View parsed_view = default_view;
      if (parse_view(argv[3], &parsed_view)) {
        view = parsed_view;
        third_is_view = true;
      } else {
        interval_ms = std::atoi(argv[3]);
      }
    }

    if (argc == 5) {
      if (third_is_view) {
        LibXR::STDIO::Printf(
            "Error: Invalid monitor args. Use monitor <time_ms> [interval_ms] "
            "[view].\r\n");
        return -1;
      }
      if (!parse_view(argv[4], &view)) {
        LibXR::STDIO::Printf("Error: Unknown view '%s'.\r\n", argv[4]);
        return -1;
      }
    }

    if (time_ms <= 0 || interval_ms <= 0) {
      LibXR::STDIO::Printf("Error: time_ms and interval_ms must be > 0.\r\n");
      return -1;
    }

    int elapsed = 0;
    while (elapsed < time_ms) {
      print_once(view);
      LibXR::Thread::Sleep(interval_ms);
      elapsed += interval_ms;
    }
    return 0;
  }

  if (std::strcmp(argv[1], "once") == 0) {
    if (argc > 3) {
      LibXR::STDIO::Printf("Error: Too many arguments for once.\r\n");
      return -1;
    }

    View view = default_view;
    if (argc == 3 && !parse_view(argv[2], &view)) {
      LibXR::STDIO::Printf("Error: Unknown view '%s'.\r\n", argv[2]);
      return -1;
    }

    print_once(view);
    return 0;
  }

  View direct_view = default_view;
  if (argc == 2 && parse_view(argv[1], &direct_view)) {
    print_once(direct_view);
    return 0;
  }

  LibXR::STDIO::Printf("Error: Unknown command '%s'.\r\n", argv[1]);
  return -1;
}

using ViewMask = uint32_t;

constexpr ViewMask view_bit(uint8_t view) { return 1u << view; }

struct FieldDesc {
  const char* name;
  size_t offset;
  ViewMask view_mask;
  void (*print)(const char* name, const void* field_ptr);
};

template <typename Snapshot>
struct StructuredProvider {
  const char* module_name;
  const char* view_help;
  bool (*parse_view)(const char* arg, uint8_t* out_view);
  const char* (*view_to_string)(uint8_t view);
  void (*capture)(void* self, Snapshot* out_snapshot);
  const FieldDesc* fields;
  size_t field_count;
};

inline void print_bool_field(const char* name, const void* field_ptr) {
  bool value = *reinterpret_cast<const bool*>(field_ptr);
  LibXR::STDIO::Printf("  %s=%s\r\n", name, value ? "true" : "false");
}

inline void print_u8_field(const char* name, const void* field_ptr) {
  uint8_t value = *reinterpret_cast<const uint8_t*>(field_ptr);
  LibXR::STDIO::Printf("  %s=%u\r\n", name, static_cast<unsigned>(value));
}

inline void print_f32_field(const char* name, const void* field_ptr) {
  float value = *reinterpret_cast<const float*>(field_ptr);
  LibXR::STDIO::Printf("  %s=%.4f\r\n", name, value);
}

inline void print_bool_value(const char* name, bool value) {
  LibXR::STDIO::Printf("  %s=%s\r\n", name, value ? "true" : "false");
}

inline void print_u8_value(const char* name, uint8_t value) {
  LibXR::STDIO::Printf("  %s=%u\r\n", name, static_cast<unsigned>(value));
}

inline void print_f32_value(const char* name, float value) {
  LibXR::STDIO::Printf("  %s=%.4f\r\n", name, value);
}

template <typename Owner>
struct LiveFieldDesc {
  const char* name;
  ViewMask view_mask;
  void (*print)(const char* name, const Owner* self);
};

template <typename Owner, size_t ViewCount>
int run_live_command(
    Owner* self, const char* module_name, const char* view_help,
    const std::array<ViewEntry<uint8_t>, ViewCount>& view_table,
    const LiveFieldDesc<Owner>* fields, size_t field_count, int argc,
    char** argv, uint8_t default_view, void (*lock_self)(Owner*) = nullptr,
    void (*unlock_self)(Owner*) = nullptr) {
  auto parse_view = [&](const char* arg, uint8_t* out_view) {
    return parse_view_name(arg, view_table, out_view);
  };

  auto print_usage = [&]() {
    LibXR::STDIO::Printf("Usage:\r\n");
    LibXR::STDIO::Printf("  monitor\r\n");
    LibXR::STDIO::Printf("  monitor <time_ms> [interval_ms] [%s]\r\n",
                         view_help);
    LibXR::STDIO::Printf("  once [%s]\r\n", view_help);
    LibXR::STDIO::Printf("  %s\r\n", view_help);
  };

  auto print_once = [&](uint8_t view) {
    if (lock_self != nullptr) {
      lock_self(self);
    }

    LibXR::STDIO::Printf("[%lu ms] %s %s\r\n", LibXR::Thread::GetTime(),
                         module_name, view_name(view, view_table));

    bool is_full_view = (view == default_view);
    uint32_t selected_mask = view_bit(view);
    for (size_t i = 0; i < field_count; ++i) {
      const auto& f = fields[i];
      if (!is_full_view && (f.view_mask & selected_mask) == 0) {
        continue;
      }
      f.print(f.name, self);
    }

    if (unlock_self != nullptr) {
      unlock_self(self);
    }
  };

  return run_command(argc, argv, default_view, parse_view, print_once,
                     print_usage);
}

template <typename Snapshot>
int run_structured_command(void* self,
                           const StructuredProvider<Snapshot>& provider,
                           int argc, char** argv, uint8_t default_view) {
  auto print_usage = [&]() {
    LibXR::STDIO::Printf("Usage:\r\n");
    LibXR::STDIO::Printf("  monitor\r\n");
    LibXR::STDIO::Printf("  monitor <time_ms> [interval_ms] [%s]\r\n",
                         provider.view_help);
    LibXR::STDIO::Printf("  once [%s]\r\n", provider.view_help);
    LibXR::STDIO::Printf("  %s\r\n", provider.view_help);
  };

  auto print_once = [&](uint8_t view) {
    Snapshot snapshot{};
    provider.capture(self, &snapshot);

    auto current_view_name =
        provider.view_to_string ? provider.view_to_string(view) : "unknown";
    LibXR::STDIO::Printf("[%lu ms] %s %s\r\n", LibXR::Thread::GetTime(),
                         provider.module_name, current_view_name);

    bool is_full_view = (view == default_view);
    uint32_t selected_mask = view_bit(view);
    const uint8_t* base = reinterpret_cast<const uint8_t*>(&snapshot);
    for (size_t i = 0; i < provider.field_count; ++i) {
      const auto& f = provider.fields[i];
      if (!is_full_view && (f.view_mask & selected_mask) == 0) {
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
  DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, (mask),  \
                          debug_core::print_f32_field)
#define DEBUG_CORE_FIELD_BOOL(SnapshotType, member, mask) \
  DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, (mask),   \
                          debug_core::print_bool_field)
#define DEBUG_CORE_FIELD_U8(SnapshotType, member, mask) \
  DEBUG_CORE_FIELD_CUSTOM(SnapshotType, member, (mask), \
                          debug_core::print_u8_field)

#define DEBUG_CORE_LIVE_F32(OwnerType, name, mask, expr)                  \
  {(name), (mask), +[](const char* field_name, const OwnerType* self) {   \
     debug_core::print_f32_value(field_name, static_cast<float>((expr))); \
   }}
#define DEBUG_CORE_LIVE_BOOL(OwnerType, name, mask, expr)                 \
  {(name), (mask), +[](const char* field_name, const OwnerType* self) {   \
     debug_core::print_bool_value(field_name, static_cast<bool>((expr))); \
   }}
#define DEBUG_CORE_LIVE_U8(OwnerType, name, mask, expr)                    \
  {(name), (mask), +[](const char* field_name, const OwnerType* self) {    \
     debug_core::print_u8_value(field_name, static_cast<uint8_t>((expr))); \
   }}
#define DEBUG_CORE_LIVE_CUSTOM(OwnerType, name, mask, printer) \
  {(name), (mask), (printer)}

class DebugCore : public LibXR::Application {
 public:
  DebugCore(LibXR::HardwareContainer& hw, LibXR::ApplicationManager& app) {
    UNUSED(hw);
    UNUSED(app);
  }

  void OnMonitor() override {}
};
