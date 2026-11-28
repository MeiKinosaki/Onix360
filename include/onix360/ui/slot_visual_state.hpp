#pragma once
#include "onix360/state/state_slots.hpp"
namespace onix360::ui {
enum class SlotVisualState { Empty, Valid, Invalid };
[[nodiscard]] inline SlotVisualState slot_visual_state(const state::StateSlots& slots, unsigned index) {
 if (!slots.exists(index)) return SlotVisualState::Empty;
 state::Snapshot snapshot;
 return slots.load(index, snapshot) ? SlotVisualState::Valid : SlotVisualState::Invalid;
}
}
