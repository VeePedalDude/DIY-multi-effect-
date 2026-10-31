// hosts/rig-host/net.h - just enough HTTP and WebSocket (RFC 6455) for rig-host: serve the page, then
// talk to it over one WebSocket per open page. Sockets on Windows (Winsock) and Linux; no libraries.
// Also a tiny reader for the flat JSON messages the page sends, and a string escaper for replies.
#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_BAD INVALID_SOCKET
inline void sockClose(sock_t s) { closesocket(s); }
inline bool netInit() { WSADATA w; return WSAStartup(MAKEWORD(2, 2), &w) == 0; }
#else
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>
typedef int sock_t;
#define SOCK_BAD (-1)
inline void sockClose(sock_t s) { close(s); }
inline bool netInit() { signal(SIGPIPE, SIG_IGN); return true; }
#endif

namespace net {

// ------------------------------------------------------------------ SHA-1 and base64 (the handshake)
inline std::string sha1(const std::string& msg) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::string m = msg; const uint64_t bits = (uint64_t)msg.size() * 8;
  m += (char)0x80; while (m.size() % 64 != 56) m += (char)0;
  for (int i = 7; i >= 0; i--) m += (char)((bits >> (i * 8)) & 0xff);
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++) w[i] = ((uint8_t)m[off + i * 4] << 24) | ((uint8_t)m[off + i * 4 + 1] << 16) | ((uint8_t)m[off + i * 4 + 2] << 8) | (uint8_t)m[off + i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; } else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; } else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      const uint32_t t = rol(a, 5) + f + e + k + w[i]; e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  std::string out; for (uint32_t x : h) for (int i = 3; i >= 0; i--) out += (char)((x >> (i * 8)) & 0xff);
  return out;
}
inline std::string base64(const std::string& s) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o; size_t i = 0;
  for (; i + 2 < s.size(); i += 3) { const uint32_t v = ((uint8_t)s[i] << 16) | ((uint8_t)s[i + 1] << 8) | (uint8_t)s[i + 2]; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += T[v & 63]; }
  if (i + 1 == s.size()) { const uint32_t v = (uint8_t)s[i] << 16; o += T[v >> 18]; o += T[(v >> 12) & 63]; o += "=="; }
  else if (i + 2 == s.size()) { const uint32_t v = ((uint8_t)s[i] << 16) | ((uint8_t)s[i + 1] << 8); o += T[v >> 18]; o += T[(v >> 12) & 63]; o += T[(v >> 6) & 63]; o += '='; }
  return o;
}
inline std::string wsAccept(const std::string& key) { return base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11")); }

// ------------------------------------------------------------------ JSON in and out (flat messages only)
inline std::string esc(const std::string& s) {
  std::string o; o.reserve(s.size() + 8);
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') { o += '\\'; o += (char)c; }
    else if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += (char)c;
  }
  return o;
}
// The value of "key" in a flat JSON object: a string (unescaped) or the raw text of a number. Empty if absent.
inline std::string field(const std::string& j, const char* key) {
  const std::string k = std::string("\"") + key + "\"";
  size_t p = j.find(k); if (p == std::string::npos) return "";
  p = j.find(':', p + k.size()); if (p == std::string::npos) return "";
  while (++p < j.size() && (j[p] == ' ' || j[p] == '\t')) {}
  if (p >= j.size()) return "";
  if (j[p] != '"') { size_t e = p; while (e < j.size() && j[e] != ',' && j[e] != '}' && j[e] != ' ') e++; return j.substr(p, e - p); }
  std::string o;
  for (size_t i = p + 1; i < j.size() && j[i] != '"'; i++) {
    if (j[i] != '\\' || i + 1 >= j.size()) { o += j[i]; continue; }
    const char c = j[++i];
    if (c == 'n') o += '\n'; else if (c == 't') o += '\t'; else if (c == 'r') o += '\r';
    else if (c == 'u' && i + 4 < j.size()) { const unsigned u = (unsigned)strtoul(j.substr(i + 1, 4).c_str(), nullptr, 16); i += 4;
      if (u < 0x80) o += (char)u; else if (u < 0x800) { o += (char)(0xC0 | (u >> 6)); o += (char)(0x80 | (u & 63)); } else { o += (char)(0xE0 | (u >> 12)); o += (char)(0x80 | ((u >> 6) & 63)); o += (char)(0x80 | (u & 63)); } }
    else o += c;
  }
  return o;
}

// ------------------------------------------------------------------ a client: HTTP first, then WebSocket
struct Client {
  sock_t s = SOCK_BAD;
  bool ws = false;
  std::string in;                        // bytes not yet parsed
  std::string msg; int msgOp = 0;        // a message being reassembled from fragments
  // a binary file announced by the last text message ({type:'asset', ...}), waiting for its bytes
  std::string pendInst, pendRef; bool pend = false;
};
inline bool sendAll(sock_t s, const char* p, size_t n) {
  while (n) { const int k = (int)send(s, p, (int)(n > (1u << 20) ? (1u << 20) : n), 0); if (k <= 0) return false; p += k; n -= (size_t)k; }
  return true;
}
inline bool wsSend(Client& c, const std::string& payload, int op = 1) {
  std::string h; h += (char)(0x80 | op);
  const size_t n = payload.size();
  if (n < 126) h += (char)n;
  else if (n < 65536) { h += (char)126; h += (char)(n >> 8); h += (char)(n & 0xff); }
  else { h += (char)127; for (int i = 7; i >= 0; i--) h += (char)((n >> (i * 8)) & 0xff); }
  return sendAll(c.s, h.data(), h.size()) && sendAll(c.s, payload.data(), n);
}
// Parse complete frames from c.in. Calls onMsg(op, payload) per whole message (1 text, 2 binary).
// Returns false when the client closed or broke the protocol.
template <class F> bool wsParse(Client& c, F&& onMsg) {
  for (;;) {
    if (c.in.size() < 2) return true;
    const uint8_t b0 = (uint8_t)c.in[0], b1 = (uint8_t)c.in[1]; const bool fin = b0 & 0x80, masked = b1 & 0x80; const int op = b0 & 15;
    uint64_t len = b1 & 127; size_t p = 2;
    if (len == 126) { if (c.in.size() < 4) return true; len = ((uint8_t)c.in[2] << 8) | (uint8_t)c.in[3]; p = 4; }
    else if (len == 127) { if (c.in.size() < 10) return true; len = 0; for (int i = 0; i < 8; i++) len = (len << 8) | (uint8_t)c.in[2 + i]; p = 10; }
    if (len > (64ull << 20)) return false;                       // 64 MB: more than any file the engine takes
    if (!masked) return false;                                   // clients must mask
    if (c.in.size() < p + 4 + len) return true;
    const uint8_t* mk = (const uint8_t*)c.in.data() + p; p += 4;
    std::string data = c.in.substr(p, (size_t)len); for (size_t i = 0; i < data.size(); i++) data[i] = (char)(data[i] ^ mk[i & 3]);
    c.in.erase(0, p + (size_t)len);
    if (op == 8) return false;                                   // close
    if (op == 9) { wsSend(c, data, 10); continue; }              // ping -> pong
    if (op == 10) continue;
    if (op == 1 || op == 2) { c.msg = data; c.msgOp = op; } else if (op == 0) c.msg += data; else return false;
    if (fin) { onMsg(c.msgOp, c.msg); c.msg.clear(); }
  }
}
// Read and answer the HTTP request in c.in. Returns: 0 need more bytes, 1 page or 404 sent (close), 2 now a WebSocket.
inline int httpHandle(Client& c, const std::string& page) {
  const size_t end = c.in.find("\r\n\r\n"); if (end == std::string::npos) return c.in.size() > 16384 ? 1 : 0;
  const std::string req = c.in.substr(0, end); c.in.erase(0, end + 4);
  const std::string line = req.substr(0, req.find("\r\n"));
  std::string lower = req; for (char& ch : lower) ch = (char)tolower((unsigned char)ch);
  const bool get = line.rfind("GET ", 0) == 0, upgrade = lower.find("upgrade: websocket") != std::string::npos;
  std::string path = get ? line.substr(4, line.find(' ', 4) - 4) : "";
  if (get && upgrade && path == "/ws") {
    const size_t k = lower.find("sec-websocket-key:"); if (k == std::string::npos) return 1;
    size_t a = k + 18; while (a < req.size() && req[a] == ' ') a++; const size_t b = req.find("\r\n", a);
    const std::string key = req.substr(a, (b == std::string::npos ? req.size() : b) - a);
    const std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + wsAccept(key) + "\r\n\r\n";
    sendAll(c.s, resp.data(), resp.size()); c.ws = true; return 2;
  }
  if (get && (path == "/" || path == "/index.html" || path.rfind("/?", 0) == 0)) {
    const std::string h = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: " + std::to_string(page.size()) + "\r\nConnection: close\r\n\r\n";
    sendAll(c.s, h.data(), h.size()); sendAll(c.s, page.data(), page.size()); return 1;
  }
  const std::string nf = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\nConnection: close\r\n\r\nnot found";
  sendAll(c.s, nf.data(), nf.size()); return 1;
}
inline sock_t listenOn(const char* addr, int port) {
  sock_t s = socket(AF_INET, SOCK_STREAM, 0); if (s == SOCK_BAD) return SOCK_BAD;
  int one = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&one, sizeof one);
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons((uint16_t)port);
  if (inet_pton(AF_INET, addr, &a.sin_addr) != 1 || bind(s, (sockaddr*)&a, sizeof a) != 0 || listen(s, 8) != 0) { sockClose(s); return SOCK_BAD; }
  return s;
}
inline void noDelay(sock_t s) { int one = 1; setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one); }

} // namespace net
