// tools/wasm/nam_no_files.cpp - replaces NAM's file readers (nam_file.cpp, wav.cpp) in the
// WebAssembly build. The browser has no file system: the page reads .nam and .wav files itself
// and hands the engine their contents, so these are never called there.
#include <stdexcept>
#include <vector>
#include "NAM/nam_file.h"
#include "NAM/wav.h"
namespace nam {
nlohmann::json validate_nam_file(const std::filesystem::path&) { throw NamFileValidationError("no file system in the browser build"); }
namespace detail {
std::vector<float> load_wav_ir(const std::filesystem::path&, double&, int&) { throw std::runtime_error("no file system in the browser build"); }
}  // namespace detail
}  // namespace nam
