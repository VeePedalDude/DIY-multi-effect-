// hosts/rig-host/zip.h - reads one entry from a zip held in memory: enough for a .rigfx bundle
// (tools/build_plugin.py writes them stored, uncompressed). A compressed entry is refused with a message.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

namespace zip {
inline uint32_t u32(const unsigned char* p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
inline uint16_t u16(const unsigned char* p) { return (uint16_t)(p[0] | p[1] << 8); }
// The bytes of `name` inside the zip `z` (`n` bytes); false with `err` if it isn't there or can't be read.
inline bool find(const unsigned char* z, size_t n, const std::string& name, std::string& out, std::string& err) {
  if (n < 22) { err = "not a zip file"; return false; }
  size_t e = 0; bool found = false;
  for (size_t i = n - 22 + 1; i-- > (n > 65557 ? n - 65557 : 0);) if (u32(z + i) == 0x06054b50) { e = i; found = true; break; }
  if (!found) { err = "not a zip file"; return false; }
  const unsigned count = u16(z + e + 10); size_t p = u32(z + e + 16);
  for (unsigned k = 0; k < count; k++) {
    if (p + 46 > n || u32(z + p) != 0x02014b50) { err = "damaged zip directory"; return false; }
    const unsigned method = u16(z + p + 10); const size_t csize = u32(z + p + 20), nl = u16(z + p + 28), xl = u16(z + p + 30), cl = u16(z + p + 32), lo = u32(z + p + 42);
    if (p + 46 + nl > n) { err = "damaged zip directory"; return false; }
    const std::string nm((const char*)z + p + 46, nl); p += 46 + nl + xl + cl;
    if (nm != name) continue;
    if (lo + 30 > n) { err = "damaged zip"; return false; }
    const size_t start = lo + 30 + u16(z + lo + 26) + u16(z + lo + 28);
    if (start + csize > n) { err = "damaged zip"; return false; }
    if (method != 0) { err = name + " is compressed: rebuild the bundle with tools/build_plugin.py"; return false; }
    out.assign((const char*)z + start, csize); return true;
  }
  err = "no " + name + " inside"; return false;
}
}  // namespace zip
