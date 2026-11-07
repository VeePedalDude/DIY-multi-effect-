// hosts/rig-host/library.h - the library as a real folder: Documents/RIG01 (or --library DIR), with one
// folder per kind of file. The page reads and writes it over HTTP (/lib/..., see net.h) with the same
// keys its other libraries use ("nam/x.nam", "plugins/x.rigfx", "library.json"); this file maps them
// to folders people recognise:
//   nam -> NAM   ir -> IR   skins -> Skins   presets -> Presets
//   plugins -> Effects or Mod Sources (a .rigfx goes by its manifest's "kind"; both are read)
// Paths are UTF-8; on Windows they go through the wide-character API so any file name works.
#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include "zip.h"
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#else
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#endif

namespace lib {

struct Entry { std::string key; uint64_t size; double mtime; };
static const char* const SOURCES = "Mod Sources";
static const char* const EFFECTS = "Effects";

inline std::string folderFor(const std::string& kind) {
  return kind == "nam" ? "NAM" : kind == "ir" ? "IR" : kind == "skins" ? "Skins" : kind == "presets" ? "Presets" : kind;                                                   // a kind added later keeps its own name
}
inline const char* kindOf(const std::string& folder) {
  return folder == "NAM" ? "nam" : folder == "IR" ? "ir" : folder == "Skins" ? "skins" : folder == "Presets" ? "presets"
       : folder == EFFECTS || folder == SOURCES ? "plugins" : nullptr;
}

#ifdef _WIN32
inline std::wstring W(const std::string& s) {
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0); std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n); return w;
}
inline std::string U(const wchar_t* w) {
  const int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr); if (n <= 1) return "";
  std::string s((size_t)n - 1, '\0'); WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], n, nullptr, nullptr); return s;
}
static const char SEP = '\\';
inline bool mkdirs(const std::string& p) { CreateDirectoryW(W(p).c_str(), nullptr); const DWORD a = GetFileAttributesW(W(p).c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
inline FILE* openf(const std::string& p, const char* mode) { return _wfopen(W(p).c_str(), W(mode).c_str()); }
inline bool stat2(const std::string& p, uint64_t& size, double& mtime) {
  WIN32_FILE_ATTRIBUTE_DATA d; if (!GetFileAttributesExW(W(p).c_str(), GetFileExInfoStandard, &d) || (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
  size = ((uint64_t)d.nFileSizeHigh << 32) | d.nFileSizeLow;
  const uint64_t t = ((uint64_t)d.ftLastWriteTime.dwHighDateTime << 32) | d.ftLastWriteTime.dwLowDateTime;
  mtime = (double)((t - 116444736000000000ull) / 10000ull); return true;
}
inline std::vector<std::string> files(const std::string& dir) {
  std::vector<std::string> out; WIN32_FIND_DATAW f; HANDLE h = FindFirstFileW(W(dir + "\\*").c_str(), &f);
  if (h == INVALID_HANDLE_VALUE) return out;
  do { if (!(f.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_HIDDEN))) out.push_back(U(f.cFileName)); } while (FindNextFileW(h, &f));
  FindClose(h); return out;
}
inline bool replace(const std::string& from, const std::string& to) { return MoveFileExW(W(from).c_str(), W(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0; }
inline bool removef(const std::string& p) { return DeleteFileW(W(p).c_str()) != 0; }
inline std::string documents() {
  PWSTR p = nullptr; std::string s;
  if (SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &p) == S_OK) s = U(p);
  if (p) CoTaskMemFree(p);
  if (s.empty()) { const char* u = getenv("USERPROFILE"); s = std::string(u ? u : ".") + "\\Documents"; }
  return s;
}
inline void reveal(const std::string& p) { ShellExecuteW(nullptr, L"open", W(p).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
#else
static const char SEP = '/';
inline bool mkdirs(const std::string& p) { mkdir(p.c_str(), 0755); struct stat st; return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }
inline FILE* openf(const std::string& p, const char* mode) { return fopen(p.c_str(), mode); }
inline bool stat2(const std::string& p, uint64_t& size, double& mtime) {
  struct stat st; if (stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) return false;
  size = (uint64_t)st.st_size; mtime = (double)st.st_mtim.tv_sec * 1e3 + (double)(st.st_mtim.tv_nsec / 1000000); return true;
}
inline std::vector<std::string> files(const std::string& dir) {
  std::vector<std::string> out; DIR* d = opendir(dir.c_str()); if (!d) return out;
  while (dirent* e = readdir(d)) { if (e->d_name[0] == '.') continue; uint64_t s; double m; if (stat2(dir + "/" + e->d_name, s, m)) out.push_back(e->d_name); }
  closedir(d); return out;
}
inline bool replace(const std::string& from, const std::string& to) { return rename(from.c_str(), to.c_str()) == 0; }
inline bool removef(const std::string& p) { return unlink(p.c_str()) == 0; }
inline std::string documents() {                                  // ~/Documents if there is one, else home
  const char* h = getenv("HOME"); const std::string home = h && *h ? h : ".";
  struct stat st; return stat((home + "/Documents").c_str(), &st) == 0 && S_ISDIR(st.st_mode) ? home + "/Documents" : home;
}
inline void reveal(const std::string& p) { if (fork() == 0) { execlp("xdg-open", "xdg-open", p.c_str(), (char*)nullptr); _exit(127); } }
#endif

// A file name the page may use: one level, no separators or names Windows refuses.
inline bool okName(const std::string& n) {
  if (n.empty() || n.size() > 200 || n[0] == '.' || n.back() == ' ' || n.back() == '.') return false;
  for (unsigned char c : n) if (c < 0x20 || strchr("/\\:*?\"<>|", c)) return false;
  return true;
}
inline bool okKind(const std::string& k) { if (k.empty() || k.size() > 32) return false; for (char c : k) if (!(c >= 'a' && c <= 'z')) return false; return true; }

struct Library {
  std::string root;                                              // no trailing separator
  bool ensure() {
    if (!mkdirs(root)) return false;
    for (const char* f : { "NAM", "IR", EFFECTS, SOURCES, "Skins", "Presets" }) mkdirs(root + SEP + f);
    return true;
  }
  // key -> where it is now (or "" if nowhere). kind is set ("" for a file at the top, like library.json).
  bool split(const std::string& key, std::string& kind, std::string& name) const {
    const size_t s = key.find('/');
    if (s == std::string::npos) { kind.clear(); name = key; return name == "library.json"; }
    kind = key.substr(0, s); name = key.substr(s + 1); return okKind(kind) && okName(name);
  }
  std::string where(const std::string& kind, const std::string& name) const {
    uint64_t sz; double mt;
    if (kind.empty()) return root + SEP + name;
    if (kind == "plugins") {
      for (const char* f : { EFFECTS, SOURCES }) { const std::string p = root + SEP + f + SEP + name; if (stat2(p, sz, mt)) return p; }
      return "";
    }
    return root + SEP + folderFor(kind) + SEP + name;
  }
  // Every file the page could use, as JSON: {"root":"...","files":[{"k":"nam/x.nam","s":123,"m":1700000000000}, ...]}
  std::string list() const {
    std::string j = "{\"root\":\"" + jsonEsc(root) + "\",\"files\":[";
    bool first = true; std::vector<std::string> seen;
    auto add = [&](const std::string& key, const std::string& path) {
      for (const auto& s : seen) if (s == key) return;            // the same plugin name in both folders: the first one wins
      uint64_t sz; double mt; if (!stat2(path, sz, mt)) return;
      char b[64]; snprintf(b, sizeof b, "\",\"s\":%llu,\"m\":%.0f}", (unsigned long long)sz, mt);
      j += std::string(first ? "" : ",") + "{\"k\":\"" + jsonEsc(key) + b; first = false; seen.push_back(key);
    };
    for (const char* f : { "NAM", "IR", EFFECTS, SOURCES, "Skins", "Presets" })
      for (const auto& n : files(root + SEP + f)) if (okName(n)) add(std::string(kindOf(f)) + "/" + n, root + SEP + f + SEP + n);
    add("library.json", root + SEP + "library.json");
    return j + "]}";
  }
  // 0 ok, 404 no such file, 400 a key it won't take
  int get(const std::string& key, std::string& out, double& mtime) const {
    std::string kind, name; if (!split(key, kind, name)) return 400;
    const std::string p = where(kind, name); uint64_t sz; if (p.empty() || !stat2(p, sz, mtime)) return 404;
    FILE* f = openf(p, "rb"); if (!f) return 404;
    out.resize((size_t)sz); const size_t k = sz ? fread(&out[0], 1, (size_t)sz, f) : 0; fclose(f); out.resize(k); return 0;
  }
  // Written whole, then moved into place, so a crash never leaves half a preset.
  int put(const std::string& key, const std::string& data) const {
    std::string kind, name; if (!split(key, kind, name)) return 400;
    std::string dir = root;
    if (!kind.empty()) { dir += SEP; dir += kind == "plugins" ? (pluginIsSource(name, data) ? SOURCES : EFFECTS) : folderFor(kind); mkdirs(dir); }
    const std::string p = dir + SEP + name;
    if (kind == "plugins") { const std::string old = where(kind, name); if (!old.empty() && old != p) removef(old); }   // replaced by one of the other kind
    const std::string tmp = p + ".part";
    FILE* f = openf(tmp, "wb"); if (!f) return 500;
    const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size(); if (fclose(f) != 0 || !ok) { removef(tmp); return 500; }
    if (!replace(tmp, p)) { removef(tmp); return 500; }
    return 0;
  }
  int del(const std::string& key) const {
    std::string kind, name; if (!split(key, kind, name)) return 400;
    const std::string p = where(kind, name); return !p.empty() && removef(p) ? 0 : 404;
  }
  static bool pluginIsSource(const std::string& name, const std::string& data) {
    const size_t n = name.size(); if (n < 6) return false;
    std::string ext = name.substr(n - 6); for (char& c : ext) c = (char)tolower((unsigned char)c);
    if (ext != ".rigfx") return false;                           // a plain .clap: an effect
    std::string m, why; if (!zip::find((const unsigned char*)data.data(), data.size(), "manifest.json", m, why)) return false;
    const size_t k = m.find("\"kind\""); if (k == std::string::npos) return false;
    const size_t q = m.find('"', m.find(':', k)); return q != std::string::npos && m.compare(q, 8, "\"source\"") == 0;
  }
  static std::string jsonEsc(const std::string& s) {
    std::string o; for (unsigned char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += (char)c; } else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c; }
    return o;
  }
};

inline std::string defaultRoot() { return documents() + SEP + "RIG01"; }

} // namespace lib
