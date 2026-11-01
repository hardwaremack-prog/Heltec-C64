#include "vnc.h"

static uint32_t hashTile(const uint8_t* fb, int tx, int ty, uint8_t* copy) {
  uint32_t h = 2166136261u;
  const int stride = C64_W / 2;
  for (int r = 0; r < 16; r++) {
    const uint8_t* src = fb + (ty * 16 + r) * stride + tx * 8;
    for (int i = 0; i < 8; i++) {
      uint8_t b = src[i];
      if (copy) copy[r * 8 + i] = b;
      h = (h ^ b) * 16777619u;
    }
  }
  return h;
}

void VncServer::attach(VncConn* c) {
  if (conn) drop();
  conn = c;
  state = WAIT_VERSION;
  in.clear(); out.clear();
  bpp = 4; bigEndian = false; trueColour = true; hextile = false;
  // default format: 32bpp little-endian, R<<16 G<<8 B
  for (int i = 0; i < 16; i++) pal[i] = C64_PALETTE[i];
  updateRequested = fullRequested = false;
  lastFrame = 0xFFFFFFFF;
  memset(tileHash, 0, sizeof tileHash);
  for (int i = 0; i < TCOLS * TROWS; i++) tileDirty[i] = true;
  const char* ver = "RFB 003.008\n";
  conn->write((const uint8_t*)ver, 12);
}

void VncServer::drop() {
  if (!conn) return;
  conn->close();
  delete conn;
  conn = nullptr;
  keys.push(0xFFFFFFFE, false);  // release any keys the client was holding
}

void VncServer::poll() {
  if (!conn) return;
  if (!conn->connected()) { drop(); return; }
  uint8_t buf[512];
  for (int guard = 0; guard < 16; guard++) {
    int n = conn->read(buf, sizeof buf);
    if (n <= 0) break;
    in.insert(in.end(), buf, buf + n);
  }
  while (conn && handleMessage()) {}
  if (conn && state == NORMAL) maybeSendUpdate();
}

bool VncServer::handleMessage() {
  switch (state) {
    case WAIT_VERSION: {
      if (in.size() < 12) return false;
      minor = (in[8] - '0') * 100 + (in[9] - '0') * 10 + (in[10] - '0');
      in.erase(in.begin(), in.begin() + 12);
      if (minor >= 7) {
        uint8_t m[2] = {1, 1};  // one security type: None
        conn->write(m, 2);
        state = WAIT_SECTYPE;
      } else {
        minor = 3;
        uint8_t m[4] = {0, 0, 0, 1};
        conn->write(m, 4);
        state = WAIT_CLIENTINIT;
      }
      return true;
    }
    case WAIT_SECTYPE: {
      if (in.size() < 1) return false;
      in.erase(in.begin());
      if (minor >= 8) { uint8_t ok[4] = {0, 0, 0, 0}; conn->write(ok, 4); }
      state = WAIT_CLIENTINIT;
      return true;
    }
    case WAIT_CLIENTINIT: {
      if (in.size() < 1) return false;
      in.erase(in.begin());
      sendInitial();
      state = NORMAL;
      return true;
    }
    case NORMAL: break;
  }
  if (in.empty()) return false;
  size_t need;
  switch (in[0]) {
    case 0: need = 20; break;
    case 2: if (in.size() < 4) return false; need = 4 + 4 * ((in[2] << 8) | in[3]); break;
    case 3: need = 10; break;
    case 4: need = 8; break;
    case 5: need = 6; break;
    case 6: {
      if (in.size() < 8) return false;
      uint32_t len = (in[4] << 24) | (in[5] << 16) | (in[6] << 8) | in[7];
      if (len > 65536) { drop(); return false; }
      need = 8 + len;
      break;
    }
    default: drop(); return false;  // unknown message: can't resync
  }
  if (in.size() < need) return false;
  const uint8_t* m = in.data();
  switch (m[0]) {
    case 0: setPixelFormat(m + 4); break;
    case 2: {
      int n = (m[2] << 8) | m[3];
      hextile = false;
      for (int i = 0; i < n; i++) {
        int32_t e = (int32_t)((m[4 + i * 4] << 24) | (m[5 + i * 4] << 16) | (m[6 + i * 4] << 8) | m[7 + i * 4]);
        if (e == 5) { hextile = true; break; }
        if (e == 0) break;
      }
      break;
    }
    case 3:
      updateRequested = true;
      if (!m[1]) fullRequested = true;
      break;
    case 4: {
      uint32_t key = (m[4] << 24) | (m[5] << 16) | (m[6] << 8) | m[7];
      keys.push(key, m[1] != 0);
      break;
    }
    case 5: break;  // pointer: ignored
    case 6:
      if (onPaste) onPaste(std::string((const char*)m + 8, need - 8));
      break;
  }
  in.erase(in.begin(), in.begin() + need);
  return true;
}

void VncServer::sendInitial() {
  out.clear();
  put16(C64_W);
  put16(C64_H);
  // pixel format: 32bpp, depth 24, little endian, true colour, 255/255/255, shifts 16/8/0
  uint8_t pf[16] = {32, 24, 0, 1, 0, 255, 0, 255, 0, 255, 16, 8, 0, 0, 0, 0};
  out.insert(out.end(), pf, pf + 16);
  put32(name.size());
  out.insert(out.end(), name.begin(), name.end());
  flush(true);
}

void VncServer::setPixelFormat(const uint8_t* pf) {
  int bits = pf[0];
  bpp = bits == 8 ? 1 : bits == 16 ? 2 : 4;
  bigEndian = pf[2] != 0;
  trueColour = pf[3] != 0;
  if (trueColour) {
    uint16_t rmax = (pf[4] << 8) | pf[5], gmax = (pf[6] << 8) | pf[7], bmax = (pf[8] << 8) | pf[9];
    int rs = pf[10], gs = pf[11], bs = pf[12];
    for (int i = 0; i < 16; i++) {
      uint32_t c = C64_PALETTE[i];
      uint32_t r = ((c >> 16) & 255) * rmax / 255, g = ((c >> 8) & 255) * gmax / 255, b = (c & 255) * bmax / 255;
      pal[i] = (r << rs) | (g << gs) | (b << bs);
    }
  } else {
    for (int i = 0; i < 16; i++) pal[i] = i;
    sendColourMap();
  }
  for (int i = 0; i < TCOLS * TROWS; i++) tileDirty[i] = true;
  fullRequested = true;
}

void VncServer::sendColourMap() {
  put8(1); put8(0); put16(0); put16(16);
  for (int i = 0; i < 16; i++) {
    uint32_t c = C64_PALETTE[i];
    put16(((c >> 16) & 255) * 257);
    put16(((c >> 8) & 255) * 257);
    put16((c & 255) * 257);
  }
  flush(true);
}

void VncServer::putPixel(uint8_t idx) {
  uint32_t v = pal[idx & 15];
  if (bpp == 1) out.push_back((uint8_t)v);
  else if (bpp == 2) {
    if (bigEndian) { out.push_back(v >> 8); out.push_back(v); }
    else { out.push_back(v); out.push_back(v >> 8); }
  } else {
    if (bigEndian) { out.push_back(v >> 24); out.push_back(v >> 16); out.push_back(v >> 8); out.push_back(v); }
    else { out.push_back(v); out.push_back(v >> 8); out.push_back(v >> 16); out.push_back(v >> 24); }
  }
}

static uint8_t tileBuf[16 * 8];
static inline uint8_t tpx(int x, int y) {
  uint8_t b = tileBuf[y * 8 + (x >> 1)];
  return (x & 1) ? (b >> 4) : (b & 15);
}

void VncServer::encodeTileRaw(int tx, int ty) {
  (void)tx; (void)ty;
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) putPixel(tpx(x, y));
}

void VncServer::encodeTileHextile(int tx, int ty) {
  (void)tx; (void)ty;
  int count[16] = {0};
  for (int y = 0; y < 16; y++)
    for (int x = 0; x < 16; x++) count[tpx(x, y)]++;
  int ncol = 0, bg = 0, fgc = -1;
  for (int i = 0; i < 16; i++)
    if (count[i]) { ncol++; if (count[i] > count[bg] || !count[bg]) bg = i; }
  if (ncol == 1) {
    put8(0x02);
    putPixel(bg);
    return;
  }
  if (ncol == 2) {
    for (int i = 0; i < 16; i++) if (count[i] && i != bg) fgc = i;
    // runs of fg per row, merged vertically when identical
    struct R { uint8_t x, y, w, h; bool open; };
    R rects[256];
    int nr = 0;
    bool overflow = false;
    for (int y = 0; y < 16 && !overflow; y++) {
      // close rects that didn't continue into this row
      int x = 0;
      bool used[256] = {false};
      while (x < 16) {
        if (tpx(x, y) != fgc) { x++; continue; }
        int s = x;
        while (x < 16 && tpx(x, y) == fgc) x++;
        int w = x - s;
        int found = -1;
        for (int i = 0; i < nr; i++)
          if (rects[i].open && rects[i].x == s && rects[i].w == w && rects[i].y + rects[i].h == y) { found = i; break; }
        if (found >= 0) { rects[found].h++; used[found] = true; }
        else {
          if (nr >= 255) { overflow = true; break; }
          rects[nr] = {(uint8_t)s, (uint8_t)y, (uint8_t)w, 1, true};
          used[nr] = true;
          nr++;
        }
      }
      for (int i = 0; i < nr; i++) if (!used[i]) rects[i].open = false;
    }
    int hexSize = 1 + 2 * bpp + 1 + nr * 2;
    if (!overflow && hexSize < 1 + 256 * bpp) {
      put8(0x02 | 0x04 | 0x08);
      putPixel(bg);
      putPixel(fgc);
      put8(nr);
      for (int i = 0; i < nr; i++) {
        put8((rects[i].x << 4) | rects[i].y);
        put8(((rects[i].w - 1) << 4) | (rects[i].h - 1));
      }
      return;
    }
  }
  put8(0x01);
  encodeTileRaw(tx, ty);
}

void VncServer::maybeSendUpdate() {
  if (!updateRequested) return;
  uint32_t fc = c64.frameCount;
  if (!fullRequested && fc == lastFrame) return;
  lastFrame = fc;

  int nDirty = 0;
  for (int ty = 0; ty < TROWS; ty++)
    for (int tx = 0; tx < TCOLS; tx++) {
      int i = ty * TCOLS + tx;
      uint32_t h = hashTile(c64.fb, tx, ty, nullptr);
      if (fullRequested || h != tileHash[i]) tileDirty[i] = true;
      if (tileDirty[i]) nDirty++;
    }
  if (!nDirty) return;  // keep the request pending until something changes

  // Rects: horizontal runs of dirty tiles
  struct Run { int tx, ty, n; };
  std::vector<Run> runs;
  for (int ty = 0; ty < TROWS; ty++) {
    int tx = 0;
    while (tx < TCOLS) {
      if (!tileDirty[ty * TCOLS + tx]) { tx++; continue; }
      int s = tx;
      while (tx < TCOLS && tileDirty[ty * TCOLS + tx]) tx++;
      runs.push_back({s, ty, tx - s});
    }
  }
  out.clear();
  put8(0); put8(0);
  put16(hextile ? runs.size() : nDirty);
  for (auto& r : runs) {
    if (hextile) {
      put16(r.tx * 16); put16(r.ty * 16); put16(r.n * 16); put16(16);
      put32(5);
      for (int t = 0; t < r.n; t++) {
        int tx = r.tx + t, i = r.ty * TCOLS + tx;
        tileHash[i] = hashTile(c64.fb, tx, r.ty, tileBuf);
        tileDirty[i] = false;
        encodeTileHextile(tx, r.ty);
        flush(false);
        if (!conn) return;
      }
    } else {
      for (int t = 0; t < r.n; t++) {
        int tx = r.tx + t, i = r.ty * TCOLS + tx;
        tileHash[i] = hashTile(c64.fb, tx, r.ty, tileBuf);
        tileDirty[i] = false;
        put16(tx * 16); put16(r.ty * 16); put16(16); put16(16);
        put32(0);
        encodeTileRaw(tx, r.ty);
        flush(false);
        if (!conn) return;
      }
    }
  }
  flush(true);
  updateRequested = false;
  fullRequested = false;
}

void VncServer::flush(bool force) {
  if (!conn) { out.clear(); return; }
  if (out.empty() || (!force && out.size() < 4096)) return;
  bytesSent += out.size();
  if (!conn->write(out.data(), out.size())) { out.clear(); drop(); return; }
  out.clear();
}
