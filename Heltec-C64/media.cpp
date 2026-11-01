#include "media.h"
#include <string.h>
#include <stdio.h>

// ---------------------------------------------------------------- cartridges
static uint32_t be32(const uint8_t* p) { return ((uint32_t)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }
static uint16_t be16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }

const char* cartTypeName(int t) {
  switch (t) {
    case 0: return "Normal cartridge";
    case 1: return "Action Replay";
    case 2: return "KCS Power Cartridge";
    case 3: return "Final Cartridge III";
    case 4: return "Simons' BASIC";
    case 5: return "Ocean";
    case 6: return "Expert Cartridge";
    case 7: return "Fun Play / Power Play";
    case 8: return "Super Games";
    case 9: return "Atomic Power";
    case 10: return "Epyx FastLoad";
    case 11: return "Westermann Learning";
    case 12: return "Rex Utility";
    case 13: return "Final Cartridge I";
    case 14: return "Magic Formel";
    case 15: return "C64 Game System / System 3";
    case 16: return "Warp Speed";
    case 17: return "Dinamic";
    case 18: return "Zaxxon / Super Zaxxon";
    case 19: return "Magic Desk / Domark / HES Australia";
    case 20: return "Super Snapshot 5";
    case 21: return "Comal-80";
    case 32: return "EasyFlash";
    case 36: return "Retro Replay";
    case 60: return "GMod2";
    default: return "Unknown type";
  }
}

bool cartTypeSupported(int t) {
  return t == 0 || t == 4 || t == 5 || t == 8 || t == 15 || t == 17 || t == 19 || t == 32;
}

static bool chipSizeOk(uint16_t size, uint16_t load) {
  if (load == 0x8000 && size == 16384) return true;
  return size == 2048 || size == 4096 || size == 8192;
}

bool parseCartridge(const uint8_t* d, size_t n, CartImage& c, std::string& err) {
  c = CartImage();
  if (n >= 64 && memcmp(d, "C64 CARTRIDGE   ", 16) == 0) {
    uint32_t hl = be32(d + 16);
    if (hl < 0x20 || hl > n) { err = "Damaged .crt header"; return false; }
    c.type = be16(d + 22);
    c.exrom = d[24] ? 1 : 0;
    c.game = d[25] ? 1 : 0;
    memcpy(c.name, d + 32, 32);
    c.name[32] = 0;
    if (!cartTypeSupported(c.type)) {
      char b[96];
      snprintf(b, sizeof b, "This is a %s cartridge (type %d), which isn't supported yet", cartTypeName(c.type), c.type);
      err = b;
      return false;
    }
    size_t p = hl;
    int chips = 0;
    while (p + 16 <= n) {
      if (memcmp(d + p, "CHIP", 4) != 0) {
        if (chips) break;  // trailing junk after valid chips: ignore
        err = "Damaged .crt file (bad CHIP block)";
        return false;
      }
      uint32_t pl = be32(d + p + 4);
      uint16_t bank = be16(d + p + 10), load = be16(d + p + 12), size = be16(d + p + 14);
      if (pl < 16 || p + 16 + (size_t)size > n) { err = "Damaged .crt file (chip runs past the end)"; return false; }
      if (bank >= CartImage::MAX_BANKS) { err = "Cartridge has too many banks"; return false; }
      if (!chipSizeOk(size, load)) { err = "Cartridge has an unusual chip size"; return false; }
      const uint8_t* body = d + p + 16;
      if (load == 0x8000) {
        c.roml[bank] = body;
        c.maskL[bank] = size >= 8192 ? 0x1FFF : size - 1;
        if (size == 16384) { c.romh[bank] = body + 8192; c.maskH[bank] = 0x1FFF; }
      } else if (load == 0xA000 || load == 0xE000) {
        c.romh[bank] = body;
        c.maskH[bank] = size - 1;
      } else if (load == 0xF000 && size == 4096) {   // 4K Ultimax ROM mirrors through $E000-$FFFF
        c.romh[bank] = body;
        c.maskH[bank] = 0x0FFF;
      } else if (load == 0x9000 && size == 4096) {
        c.roml[bank] = body;
        c.maskL[bank] = 0x0FFF;
      } else {
        char b[64];
        snprintf(b, sizeof b, "Cartridge chip at unusual address $%04X", load);
        err = b;
        return false;
      }
      if (bank + 1 > c.banks) c.banks = bank + 1;
      chips++;
      p += pl;
    }
    if (!chips) { err = "The .crt file has no ROM chips in it"; return false; }
    return true;
  }
  if (n == 8192 || n == 16384) {   // raw cartridge dump starting with the CBM80 signature
    c.type = 0;
    c.exrom = 0;
    c.game = n == 16384 ? 0 : 1;
    c.roml[0] = d; c.maskL[0] = 0x1FFF;
    if (n == 16384) { c.romh[0] = d + 8192; c.maskH[0] = 0x1FFF; }
    c.banks = 1;
    return true;
  }
  err = "Not a C64 cartridge (.crt) file";
  return false;
}

// ---------------------------------------------------------------- D64
static const uint8_t SPT[41] = {0, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21,
                                19, 19, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 18, 17, 17, 17, 17, 17,
                                17, 17, 17, 17, 17};

bool D64::validSize(uint32_t n) { return n == 174848 || n == 175531 || n == 196608 || n == 197376; }

bool D64::sector(int t, int s, uint8_t* buf) {
  if (t < 1 || t > tracks() || s >= SPT[t]) return false;
  uint32_t off = 0;
  for (int i = 1; i < t; i++) off += SPT[i];
  off = (off + s) * 256;
  if (off + 256 > size) return false;
  return readAt && readAt(off, buf, 256);
}

bool D64::header(uint8_t name[16], uint8_t id[5], int& blocksFree) {
  uint8_t b[256];
  if (!sector(18, 0, b)) return false;
  memcpy(name, b + 0x90, 16);
  memcpy(id, b + 0xA2, 5);
  blocksFree = 0;
  for (int t = 1; t <= 35; t++)
    if (t != 18) blocksFree += b[4 + (t - 1) * 4];
  return true;
}

bool D64::list(std::vector<Entry>& out) {
  out.clear();
  uint8_t b[256];
  int t = 18, s = 1, guard = 0;
  while (t && guard++ < 40) {
    if (!sector(t, s, b)) return !out.empty();
    for (int i = 0; i < 8; i++) {
      const uint8_t* e = b + i * 32;
      if ((e[2] & 7) == 0 && !(e[2] & 0x80)) continue;   // scratched / empty slot
      Entry en;
      memcpy(en.name, e + 5, 16);
      en.nameLen = 16;
      while (en.nameLen > 0 && en.name[en.nameLen - 1] == 0xA0) en.nameLen--;
      en.type = e[2];
      en.blocks = e[30] | (e[31] << 8);
      en.t = e[3];
      en.s = e[4];
      out.push_back(en);
    }
    t = b[0];
    s = b[1];
  }
  return true;
}

static bool petMatch(const uint8_t* pat, int pl, const uint8_t* name, int nl) {
  int i = 0;
  for (; i < pl; i++) {
    if (pat[i] == '*') return true;
    if (i >= nl) return false;
    if (pat[i] != '?' && pat[i] != name[i]) return false;
  }
  return i == nl;
}

bool D64::load(const uint8_t* pat, int pl, std::vector<uint8_t>& data) {
  // strip drive prefix like "0:" or ":"
  for (int i = 0; i < pl && i < 3; i++)
    if (pat[i] == ':') { pat += i + 1; pl -= i + 1; break; }
  if (pl == 0) return false;
  std::vector<Entry> files;
  if (!list(files)) return false;
  for (auto& f : files) {
    int kind = f.type & 7;
    if (!(f.type & 0x80) || (kind != 2 && kind != 3 && kind != 1)) continue;   // closed PRG/USR/SEQ only
    if (!petMatch(pat, pl, f.name, f.nameLen)) continue;
    data.clear();
    uint8_t b[256];
    int t = f.t, s = f.s, guard = 0;
    while (t && guard++ < 800) {
      if (!sector(t, s, b)) return false;
      if (b[0] == 0) {
        int last = b[1] < 2 ? 2 : b[1];
        data.insert(data.end(), b + 2, b + last + 1);
        break;
      }
      data.insert(data.end(), b + 2, b + 256);
      t = b[0];
      s = b[1];
    }
    return data.size() >= 2;
  }
  return false;
}
