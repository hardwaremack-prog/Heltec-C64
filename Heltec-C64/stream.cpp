#include "stream.h"
#include <string.h>
#include <ctype.h>

// ---------------------------------------------------------------- SHA-1 + base64 (WebSocket handshake)
static void sha1(const uint8_t* data, size_t len, uint8_t out[20]) {
  uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
  std::vector<uint8_t> m(data, data + len);
  uint64_t bits = (uint64_t)len * 8;
  m.push_back(0x80);
  while (m.size() % 64 != 56) m.push_back(0);
  for (int i = 7; i >= 0; i--) m.push_back((uint8_t)(bits >> (i * 8)));
  auto rol = [](uint32_t x, int n) { return (x << n) | (x >> (32 - n)); };
  for (size_t off = 0; off < m.size(); off += 64) {
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
      w[i] = (m[off + i * 4] << 24) | (m[off + i * 4 + 1] << 16) | (m[off + i * 4 + 2] << 8) | m[off + i * 4 + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
      uint32_t f, k;
      if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
      else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
      else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
      else { f = b ^ c ^ d; k = 0xCA62C1D6; }
      uint32_t t = rol(a, 5) + f + e + k + w[i];
      e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
  }
  for (int i = 0; i < 5; i++) for (int j = 0; j < 4; j++) out[i * 4 + j] = (uint8_t)(h[i] >> (24 - j * 8));
}

static std::string base64(const uint8_t* d, size_t n) {
  static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string o;
  for (size_t i = 0; i < n; i += 3) {
    uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0) | (i + 2 < n ? d[i + 2] : 0);
    o += T[(v >> 18) & 63];
    o += T[(v >> 12) & 63];
    o += i + 1 < n ? T[(v >> 6) & 63] : '=';
    o += i + 2 < n ? T[v & 63] : '=';
  }
  return o;
}

std::string wsAcceptKey(const std::string& key) {
  std::string s = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t h[20];
  sha1((const uint8_t*)s.data(), s.size(), h);
  return base64(h, 20);
}

// ---------------------------------------------------------------- connection
void StreamServer::attach(NetConn* c) {
  if (conn) {
    if (state == OPEN) {
      const char* msg = "\x04" "Another viewer took over";
      sendWs((const uint8_t*)msg, strlen(msg));
    }
    drop();
  }
  conn = c;
  state = HANDSHAKE;
  in.clear();
  sound = false;
  needFull = true;
  inFlight = 0;
  lastFrame = 0xFFFFFFFF;
}

void StreamServer::drop() {
  if (!conn) return;
  conn->close();
  delete conn;
  conn = nullptr;
  sound = false;
  keys.push(0xFFFFFFFE, false);  // release anything the viewer was holding
}

void StreamServer::poll() {
  if (!conn) return;
  if (!conn->connected()) { drop(); return; }
  uint8_t buf[512];
  for (int guard = 0; guard < 16; guard++) {
    int n = conn->read(buf, sizeof buf);
    if (n <= 0) break;
    in.insert(in.end(), buf, buf + n);
    if (in.size() > 70000) { drop(); return; }
  }
  if (state == HANDSHAKE) {
    if (!doHandshake()) return;
  }
  while (conn && readFrame()) {}
  if (!conn) return;
  sendAudio();
  if (conn) sendVideo();
}

bool StreamServer::doHandshake() {
  std::string req(in.begin(), in.end());
  size_t end = req.find("\r\n\r\n");
  if (end == std::string::npos) {
    if (in.size() > 4096) drop();
    return false;
  }
  std::string lower = req.substr(0, end);
  for (auto& ch : lower) ch = tolower(ch);
  size_t k = lower.find("sec-websocket-key:");
  if (k == std::string::npos) {
    const char* r = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
    conn->write((const uint8_t*)r, strlen(r));
    drop();
    return false;
  }
  k += 18;
  size_t e = req.find("\r\n", k);
  std::string key = req.substr(k, e - k);
  while (!key.empty() && key.front() == ' ') key.erase(0, 1);
  while (!key.empty() && key.back() == ' ') key.pop_back();
  std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " +
                     wsAcceptKey(key) + "\r\n\r\n";
  conn->write((const uint8_t*)resp.data(), resp.size());
  in.erase(in.begin(), in.begin() + end + 4);
  state = OPEN;
  uint8_t hello[9] = {0, (uint8_t)(C64_W & 255), (uint8_t)(C64_W >> 8), (uint8_t)(C64_H & 255), (uint8_t)(C64_H >> 8),
                      (uint8_t)(SID_RATE & 255), (uint8_t)((SID_RATE >> 8) & 255), 0, 0};
  sendWs(hello, 9);
  return true;
}

bool StreamServer::readFrame() {
  if (in.size() < 2) return false;
  uint8_t op = in[0] & 0x0F;
  bool masked = in[1] & 0x80;
  uint64_t len = in[1] & 0x7F;
  size_t hdr = 2;
  if (len == 126) { if (in.size() < 4) return false; len = (in[2] << 8) | in[3]; hdr = 4; }
  else if (len == 127) {
    if (in.size() < 10) return false;
    len = 0;
    for (int i = 0; i < 8; i++) len = (len << 8) | in[2 + i];
    hdr = 10;
  }
  if (len > 65536) { drop(); return false; }
  size_t total = hdr + (masked ? 4 : 0) + len;
  if (in.size() < total) return false;
  uint8_t* p = in.data() + hdr + (masked ? 4 : 0);
  if (masked) {
    const uint8_t* mk = in.data() + hdr;
    for (uint64_t i = 0; i < len; i++) p[i] ^= mk[i & 3];
  }
  if (op == 8) { drop(); return false; }
  if (op == 9) {  // ping -> pong
    std::vector<uint8_t> pong = {0x8A, (uint8_t)len};
    pong.insert(pong.end(), p, p + len);
    conn->write(pong.data(), pong.size());
  } else if (op == 1 || op == 2) {
    handleMessage(p, (size_t)len);
  }
  if (conn) in.erase(in.begin(), in.begin() + total);
  return conn != nullptr;
}

void StreamServer::handleMessage(const uint8_t* m, size_t n) {
  if (n < 1) return;
  switch (m[0]) {
    case 0x10:
      if (n >= 6) keys.push(m[2] | (m[3] << 8) | (m[4] << 16) | ((uint32_t)m[5] << 24), m[1] != 0);
      break;
    case 0x11:
      if (onPaste) onPaste(std::string((const char*)m + 1, n - 1));
      break;
    case 0x12: needFull = true; break;
    case 0x13: if (inFlight > 0) inFlight--; break;
    case 0x14:
      if (n >= 2) {
        sound = m[1] != 0;
        int16_t junk[256];
        while (c64.readAudio(junk, 256) > 0) {}  // start fresh: no stale backlog
      }
      break;
    case 0x15: if (n >= 2) keys.joyPort = m[1] == 1 ? 1 : 2; break;
  }
}

void StreamServer::sendWs(const uint8_t* data, size_t n) {
  if (!conn) return;
  out.clear();
  out.push_back(0x82);
  if (n < 126) out.push_back((uint8_t)n);
  else if (n < 65536) { out.push_back(126); out.push_back(n >> 8); out.push_back(n & 255); }
  else { out.push_back(127); for (int i = 7; i >= 0; i--) out.push_back((uint8_t)((uint64_t)n >> (i * 8))); }
  out.insert(out.end(), data, data + n);
  bytesSent += out.size();
  if (!conn->write(out.data(), out.size())) drop();
}

void StreamServer::sendAudio() {
  if (!sound) return;
  if (c64.audioAvailable() < 441) return;  // batch ~one frame of sound per message
  static int16_t smp[1024];
  int n = c64.readAudio(smp, 1024);
  std::vector<uint8_t> msg(1 + n * 2);
  msg[0] = 0x02;
  for (int i = 0; i < n; i++) { msg[1 + i * 2] = smp[i] & 255; msg[2 + i * 2] = (uint16_t)smp[i] >> 8; }
  sendWs(msg.data(), msg.size());
}

static uint32_t hashTile(const uint8_t* fb, int tx, int ty, uint8_t* copy) {
  uint32_t h = 2166136261u;
  for (int r = 0; r < 16; r++) {
    const uint8_t* src = fb + (ty * 16 + r) * (C64_W / 2) + tx * 8;
    for (int i = 0; i < 8; i++) {
      if (copy) copy[r * 8 + i] = src[i];
      h = (h ^ src[i]) * 16777619u;
    }
  }
  return h;
}

static void encodeTile(const uint8_t* t, std::vector<uint8_t>& o) {
  int count[16] = {0};
  for (int i = 0; i < 128; i++) { count[t[i] & 15]++; count[t[i] >> 4]++; }
  int c0 = -1, c1 = -1, ncol = 0;
  for (int i = 0; i < 16; i++)
    if (count[i]) { ncol++; if (c0 < 0) c0 = i; else if (c1 < 0) c1 = i; }
  if (ncol == 1) { o.push_back(0); o.push_back((uint8_t)c0); return; }
  if (ncol == 2) {
    o.push_back(1);
    o.push_back((uint8_t)(c0 | (c1 << 4)));
    for (int y = 0; y < 16; y++) {
      uint16_t bits = 0;
      for (int x = 0; x < 16; x++) {
        uint8_t b = t[y * 8 + (x >> 1)];
        int px = (x & 1) ? b >> 4 : b & 15;
        if (px == c1) bits |= 0x8000 >> x;
      }
      o.push_back(bits >> 8);
      o.push_back(bits & 255);
    }
    return;
  }
  o.push_back(2);
  o.insert(o.end(), t, t + 128);
}

void StreamServer::sendVideo() {
  if (inFlight >= 2) return;
  uint32_t fc = c64.frameCount;
  if (!needFull && fc == lastFrame) return;
  lastFrame = fc;
  bool full = needFull;
  needFull = false;
  bool any = false;
  uint8_t tile[128];
  std::vector<uint8_t> msg;
  for (int ty = 0; ty < TROWS && conn; ty++) {
    msg.assign(3, 0);
    msg[0] = 0x01;
    int count = 0;
    for (int tx = 0; tx < TCOLS; tx++) {
      int i = ty * TCOLS + tx;
      uint32_t h = hashTile(c64.fb, tx, ty, nullptr);
      if (!full && h == tileHash[i]) continue;
      tileHash[i] = hashTile(c64.fb, tx, ty, tile);  // hash exactly what we send
      msg.push_back((uint8_t)tx);
      msg.push_back((uint8_t)ty);
      encodeTile(tile, msg);
      count++;
    }
    if (count) {
      msg[1] = count & 255;
      msg[2] = count >> 8;
      sendWs(msg.data(), msg.size());
      any = true;
    }
  }
  if (any && conn) {
    uint8_t end = 0x03;
    sendWs(&end, 1);
    inFlight++;
  }
}
