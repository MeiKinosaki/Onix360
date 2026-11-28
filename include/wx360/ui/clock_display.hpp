#pragma once
#include <string>
namespace wx360::ui {
struct ClockDateTime {
 unsigned year = 0;
 unsigned month = 0;
 unsigned day = 0;
 unsigned hour = 0;
 unsigned minute = 0;
 unsigned second = 0;
};
struct ClockText {
 std::wstring time;
 std::wstring date;
};
[[nodiscard]] inline ClockText format_clock(const ClockDateTime& value) {
 auto two_digits = [](unsigned number) {
  return (number < 10 ? std::wstring(L"0") : std::wstring{}) + std::to_wstring(number);
 };
 ClockText result;
 result.time = two_digits(value.hour) + L":" + two_digits(value.minute) + L":" + two_digits(value.second);
 result.date = two_digits(value.day) + L"/" + two_digits(value.month) + L"/" + std::to_wstring(value.year);
 return result;
}
}
