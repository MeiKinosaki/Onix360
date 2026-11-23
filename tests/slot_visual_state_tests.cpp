#include "wx360/ui/slot_visual_state.hpp"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
int main() {
 using namespace wx360;
 const auto dir = std::filesystem::temp_directory_path() / "wx360_slot_visual_tests";
 std::error_code ec; std::filesystem::remove_all(dir, ec);
 state::StateSlots slots(dir);
 if (ui::slot_visual_state(slots, 0) != ui::SlotVisualState::Empty) return EXIT_FAILURE;
 state::Snapshot snapshot; snapshot.cpu.pc = 0x1234; snapshot.ram = {1,2,3};
 std::string error;
 if (!slots.save(0, snapshot, &error)) return EXIT_FAILURE;
 if (ui::slot_visual_state(slots, 0) != ui::SlotVisualState::Valid) return EXIT_FAILURE;
 { std::ofstream corrupt(slots.path_for(0), std::ios::binary | std::ios::trunc); corrupt << "broken"; }
 if (ui::slot_visual_state(slots, 0) != ui::SlotVisualState::Invalid) return EXIT_FAILURE;
 if (!slots.erase(0, &error)) return EXIT_FAILURE;
 if (ui::slot_visual_state(slots, 0) != ui::SlotVisualState::Empty) return EXIT_FAILURE;
 if (ui::slot_visual_state(slots, state::StateSlots::kSlotCount) != ui::SlotVisualState::Empty) return EXIT_FAILURE;
 std::filesystem::remove_all(dir, ec);
 std::cout << "WX360 slot visual state tests passed\\n";
 return EXIT_SUCCESS;
}
