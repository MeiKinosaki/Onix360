#include "onix360/ui/clock_display.hpp"
#include <cstdlib>
int main() {
 const auto value = onix360::ui::format_clock({2026, 11, 28, 9, 4, 7});
 if (value.time != L"09:04:07") return EXIT_FAILURE;
 if (value.date != L"28/11/2026") return EXIT_FAILURE;
 const auto midnight = onix360::ui::format_clock({2000, 1, 2, 0, 0, 0});
 if (midnight.time != L"00:00:00" || midnight.date != L"02/01/2000") return EXIT_FAILURE;
 return EXIT_SUCCESS;
}
