// engine/core/json_assert.h - force-included in every file (build.sh), engine and vendored code alike.
// nlohmann/json checks its preconditions with JSON_ASSERT, which compiles to nothing in release builds.
// Reading a missing key from a const object is then undefined behaviour, and NAM core does exactly
// that with a damaged .nam (no "config", no "version"): usually an exception, sometimes a crash.
// Here a failed check throws instead, which the loaders catch and report. (Inside the library's
// noexcept functions a failed check now stops the program cleanly rather than running on garbage.)
#pragma once
#include <stdexcept>
[[noreturn]] inline void rig_json_assert_failed() { throw std::runtime_error("damaged file: a setting it needs is missing or the wrong type"); }
#define JSON_ASSERT(x) do { if (!(x)) rig_json_assert_failed(); } while (0)
