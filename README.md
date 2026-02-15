# DebugCore

Shared debug shell utilities

## Quick Use

`DebugCore` provides two debug bridges for RamFS:

1. `run_structured_command(...)`: snapshot + field-offset mode.
2. `run_live_command(...)`: function-pointer field mode (no snapshot/capture).

For extreme simplification, prefer `run_live_command(...)`.

### Minimal Pattern

```cpp
enum class DebugView : uint8_t { STATE, FULL };
struct DebugSnapshot { uint8_t state; float dt; };

constexpr uint8_t view_state = static_cast<uint8_t>(DebugView::STATE);
constexpr uint8_t view_full = static_cast<uint8_t>(DebugView::FULL);
constexpr auto mask_state = debug_core::view_bit(view_state);

static constexpr std::array<debug_core::ViewEntry<uint8_t>, 2> view_table{{
    {"state", view_state}, {"full", view_full},
}};

static const debug_core::FieldDesc fields[] = {
    DEBUG_CORE_FIELD_U8(DebugSnapshot, state, mask_state),
    DEBUG_CORE_FIELD_F32(DebugSnapshot, dt, mask_state),
};
```

### Live Pattern (Most Concise)

```cpp
static constexpr std::array<debug_core::ViewEntry<uint8_t>, 2> view_table{{
    {"state", view_state}, {"full", view_full},
}};

static const debug_core::LiveFieldDesc<MyModule> fields[] = {
    DEBUG_CORE_LIVE_U8(MyModule, "state", mask_state, self->state_),
    DEBUG_CORE_LIVE_F32(MyModule, "dt", mask_state, self->dt_),
};

return debug_core::run_live_command(this, "my_module", "state|full", view_table, fields,
                                  sizeof(fields) / sizeof(fields[0]), argc, argv,
                                  view_full);
```

## Required Hardware
None

## Constructor Arguments
None

## Template Arguments
None

## Depends
None
