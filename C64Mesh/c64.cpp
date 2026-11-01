// C64Mesh emulator core: 6502 CPU, memory banking, VIC-II (line renderer),
// CIA 1/2 (timers, keyboard, joystick, TOD), SID stub, device 8 LOAD/SAVE traps.
#include "c64.h"
#include <stdlib.h>
#include <algorithm>

// Colodore palette
const uint32_t C64_PALETTE[16] = {
    0x000000, 0xFFFFFF, 0x813338, 0x75CEC8, 0x8E3C97, 0x56AC4D, 0x2E2C9B, 0xEDF171,
    0x8E5029, 0x553800, 0xC46C71, 0x4A4A4A, 0x7B7B7B, 0xA9FF9F, 0x706DEB, 0xB2B2B2};

enum { FC = 1, FZ = 2, FI = 4, FD = 8, FB = 0x10, FU = 0x20, FV = 0x40, FN = 0x80 };

static const uint8_t CYC[256] = {
    7, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 3, 2, 2, 2, 3, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    6, 6, 2, 8, 3, 3, 5, 5, 4, 2, 2, 2, 5, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, 2, 6, 2, 6, 4, 4, 4, 4, 2, 5, 2, 5, 5, 5, 5, 5,
    2, 6, 2, 6, 3, 3, 3, 3, 2, 2, 2, 2, 4, 4, 4, 4, 2, 5, 2, 5, 4, 4, 4, 4, 2, 4, 2, 4, 4, 4, 4, 4,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7,
    2, 6, 2, 8, 3, 3, 5, 5, 2, 2, 2, 2, 4, 4, 6, 6, 2, 5, 2, 8, 4, 4, 6, 6, 2, 4, 2, 7, 4, 4, 7, 7};

// ---------------------------------------------------------------- CIA
static uint8_t bcdInc(uint8_t v) {
  v++;
  if ((v & 0x0F) > 9) v = (v & 0xF0) + 0x10;
  return v;
}

void CIA::todTenth() {
  if (todStopped) return;
  tod[0] = (tod[0] + 1);
  if (tod[0] < 10) return;
  tod[0] = 0;
  tod[1] = bcdInc(tod[1]);
  if (tod[1] < 0x60) return;
  tod[1] = 0;
  tod[2] = bcdInc(tod[2]);
  if (tod[2] < 0x60) return;
  tod[2] = 0;
  uint8_t pm = tod[3] & 0x80, h = tod[3] & 0x1F;
  h = bcdInc(h);
  if (h == 0x12) pm ^= 0x80;
  if (h == 0x13) h = 1;
  tod[3] = pm | h;
}

void CIA::tick(int c) {
  int taUnder = 0;
  if ((cra & 1) && !(cra & 0x20)) {
    int t = (int)ta - c;
    while (t < 0) {
      taUnder++;
      icrFlags |= 1;
      if (cra & 0x08) { cra &= ~1; t = la; break; }
      t += (int)la + 1;
    }
    ta = (uint16_t)t;
  }
  if (crb & 1) {
    int mode = (crb >> 5) & 3, dec = 0;
    if (mode == 0) dec = c;
    else if (mode >= 2) dec = taUnder;
    if (dec) {
      int t = (int)tb - dec;
      while (t < 0) {
        icrFlags |= 2;
        if (crb & 0x08) { crb &= ~1; t = lb; break; }
        t += (int)lb + 1;
      }
      tb = (uint16_t)t;
    }
  }
}

// ---------------------------------------------------------------- core
C64::C64() {
  fb = (uint8_t*)calloc(C64_W * C64_H / 2, 1);
  memset(basicRom, 0, sizeof basicRom);
  memset(kernalRom, 0, sizeof kernalRom);
  memset(charRom, 0, sizeof charRom);
  memset(keyRow, 0, sizeof keyRow);
  reset();
}

void C64::setRoms(const uint8_t* b, const uint8_t* k, const uint8_t* c) {
  memcpy(basicRom, b, 8192);
  memcpy(kernalRom, k, 8192);
  memcpy(charRom, c, 4096);
}

void C64::reset() {
  for (int i = 0; i < 65536; i++) ram[i] = (i & 0x40) ? 0xFF : 0x00;
  memset(colorRam, 0x0E, sizeof colorRam);
  memset(vic, 0, sizeof vic);
  memset(sid, 0, sizeof sid);
  cia1.reset();
  cia2.reset();
  port00 = 0x2F; port01 = 0x37;
  updateBanking();
  raster = 0; rasterCmp = 0; vicIrqFlags = 0; collSS = collSB = 0;
  nmiLine = nmiPending = false;
  pendingCycles = 0;
  tqTail = tqHead;
  a = x = y = 0; sp = 0xFD; p = FI | FU;
  pc = read(0xFFFC) | (read(0xFFFD) << 8);
}

void C64::restoreKey() { nmiPending = true; }

void C64::updateBanking() {
  uint8_t eff = (port01 | ~port00) & 7;
  mapBasic = (eff & 3) == 3;
  mapKernal = (eff & 2) != 0;
  mapIO = (eff & 3) && (eff & 4);
  mapChar = (eff & 3) && !(eff & 4);
}

inline uint8_t C64::read(uint16_t ad) {
  if (ad >= 0xA000) {
    if (ad < 0xC000) return mapBasic ? basicRom[ad - 0xA000] : ram[ad];
    if (ad < 0xD000) return ram[ad];
    if (ad < 0xE000) {
      if (mapIO) return readIO(ad);
      if (mapChar) return charRom[ad - 0xD000];
      return ram[ad];
    }
    return mapKernal ? kernalRom[ad - 0xE000] : ram[ad];
  }
  if (ad < 2) return ad ? (uint8_t)((port01 & port00) | (~port00 & 0x17)) : port00;
  return ram[ad];
}

inline void C64::write(uint16_t ad, uint8_t v) {
  if (ad < 2) {
    if (ad) port01 = v; else port00 = v;
    updateBanking();
    return;
  }
  if (ad >= 0xD000 && ad < 0xE000 && mapIO) { writeIO(ad, v); return; }
  ram[ad] = v;
}

uint16_t C64::vicBank() {
  uint8_t pa = (cia2.pra | ~cia2.ddra) & 3;
  return (uint16_t)((3 - pa) * 0x4000);
}

inline uint8_t C64::vicRead(uint16_t a14) {
  uint16_t bank = vicBank();
  a14 &= 0x3FFF;
  if ((bank == 0 || bank == 0x8000) && (a14 & 0x3000) == 0x1000) return charRom[a14 & 0xFFF];
  return ram[(uint16_t)(bank + a14)];
}

// ---------------------------------------------------------------- I/O
uint8_t C64::readIO(uint16_t ad) {
  if (ad < 0xD400) {  // VIC
    int r = ad & 0x3F;
    switch (r) {
      case 0x11: return (vic[0x11] & 0x7F) | ((raster & 0x100) >> 1);
      case 0x12: return raster & 0xFF;
      case 0x19: return vicIrqFlags | 0x70 | ((vicIrqFlags & vic[0x1A] & 0x0F) ? 0x80 : 0);
      case 0x1A: return vic[0x1A] | 0xF0;
      case 0x1E: { uint8_t v = collSS; collSS = 0; return v; }
      case 0x1F: { uint8_t v = collSB; collSB = 0; return v; }
      case 0x16: return vic[0x16] | 0xC0;
      case 0x18: return vic[0x18] | 0x01;
      default:
        if (r >= 0x20 && r <= 0x2E) return vic[r] | 0xF0;
        if (r > 0x2E) return 0xFF;
        return vic[r];
    }
  }
  if (ad < 0xD800) {  // SID
    int r = ad & 0x1F;
    if (r == 0x19 || r == 0x1A) return 0xFF;
    if (r == 0x1B || r == 0x1C) return (uint8_t)rand();
    return 0;
  }
  if (ad < 0xDC00) return (colorRam[ad & 0x3FF] & 0x0F) | 0xF0;
  if (ad < 0xDD00) return readCIA(cia1, ad & 0x0F, true);
  if (ad < 0xDE00) return readCIA(cia2, ad & 0x0F, false);
  return 0xFF;
}

void C64::writeIO(uint16_t ad, uint8_t v) {
  if (ad < 0xD400) {
    int r = ad & 0x3F;
    if (r == 0x19) { vicIrqFlags &= ~v & 0x0F; return; }
    if (r == 0x1E || r == 0x1F) return;
    vic[r] = v;
    if (r == 0x11 || r == 0x12) {
      rasterCmp = (uint16_t)(((vic[0x11] & 0x80) << 1) | vic[0x12]);
      if (r == 0x12 || true) {
        if (raster == rasterCmp) vicIrqFlags |= 1;
      }
    }
    return;
  }
  if (ad < 0xD800) { sid[ad & 0x1F] = v; return; }
  if (ad < 0xDC00) { colorRam[ad & 0x3FF] = v & 0x0F; return; }
  if (ad < 0xDD00) { writeCIA(cia1, ad & 0x0F, v, true); return; }
  if (ad < 0xDE00) { writeCIA(cia2, ad & 0x0F, v, false); return; }
}

uint8_t C64::readCIA(CIA& c, int r, bool isCia1) {
  switch (r) {
    case 0: {
      if (isCia1) {
        uint8_t pb = (c.prb | ~c.ddrb) & ~joy1;
        uint8_t pa = 0xFF;
        for (int col = 0; col < 8; col++)
          if (!(pb & (1 << col)))
            for (int row = 0; row < 8; row++)
              if (keyRow[row] & (1 << col)) pa &= ~(1 << row);
        pa &= ~joy2;
        return (c.pra & c.ddra) | (pa & ~c.ddra);
      }
      // CIA2: serial bus inputs (bits 6,7) read high = no device activity
      return (c.pra & c.ddra) | (0xC0 & ~c.ddra) | (0x3F & ~c.ddra & 0x3F);
    }
    case 1: {
      if (isCia1) {
        uint8_t pa = (c.pra | ~c.ddra) & ~joy2;
        uint8_t pb = 0xFF;
        for (int row = 0; row < 8; row++)
          if (!(pa & (1 << row))) pb &= ~keyRow[row];
        pb &= ~joy1;
        return (c.prb & c.ddrb) | (pb & ~c.ddrb);
      }
      return (c.prb & c.ddrb) | (0xFF & ~c.ddrb);
    }
    case 2: return c.ddra;
    case 3: return c.ddrb;
    case 4: return c.ta & 0xFF;
    case 5: return c.ta >> 8;
    case 6: return c.tb & 0xFF;
    case 7: return c.tb >> 8;
    case 8: {
      uint8_t v = c.todLatched ? c.todLatch[0] : c.tod[0];
      c.todLatched = false;
      return v;
    }
    case 9: return c.todLatched ? c.todLatch[1] : c.tod[1];
    case 10: return c.todLatched ? c.todLatch[2] : c.tod[2];
    case 11:
      if (!c.todLatched) { memcpy(c.todLatch, c.tod, 4); c.todLatched = true; }
      return c.todLatch[3];
    case 12: return c.sdr;
    case 13: {
      uint8_t v = c.icrFlags | (c.irq() ? 0x80 : 0);
      c.icrFlags = 0;
      return v;
    }
    case 14: return c.cra & ~0x10;
    case 15: return c.crb & ~0x10;
  }
  return 0xFF;
}

void C64::writeCIA(CIA& c, int r, uint8_t v, bool isCia1) {
  (void)isCia1;
  switch (r) {
    case 0: c.pra = v; break;
    case 1: c.prb = v; break;
    case 2: c.ddra = v; break;
    case 3: c.ddrb = v; break;
    case 4: c.la = (c.la & 0xFF00) | v; break;
    case 5: c.la = (c.la & 0x00FF) | (v << 8); if (!(c.cra & 1)) c.ta = c.la; break;
    case 6: c.lb = (c.lb & 0xFF00) | v; break;
    case 7: c.lb = (c.lb & 0x00FF) | (v << 8); if (!(c.crb & 1)) c.tb = c.lb; break;
    case 8: if (!(c.crb & 0x80)) { c.tod[0] = v & 0x0F; c.todStopped = false; } break;
    case 9: if (!(c.crb & 0x80)) c.tod[1] = v & 0x7F; break;
    case 10: if (!(c.crb & 0x80)) c.tod[2] = v & 0x7F; break;
    case 11: if (!(c.crb & 0x80)) { c.tod[3] = v & 0x9F; c.todStopped = true; } break;
    case 12: c.sdr = v; break;
    case 13:
      if (v & 0x80) c.icrMask |= v & 0x1F;
      else c.icrMask &= ~(v & 0x1F);
      break;
    case 14:
      if (v & 0x10) c.ta = c.la;
      c.cra = v & ~0x10;
      break;
    case 15:
      if (v & 0x10) c.tb = c.lb;
      c.crb = v & ~0x10;
      break;
  }
}

// ---------------------------------------------------------------- CPU
void C64::irq(bool nmi) {
  push(pc >> 8);
  push(pc & 0xFF);
  push((p & ~FB) | FU);
  p |= FI;
  uint16_t vec = nmi ? 0xFFFA : 0xFFFE;
  pc = read(vec) | (read(vec + 1) << 8);
}

#define SETNZ(v) p = (p & ~(FN | FZ)) | ((v) & FN) | (((v) & 0xFF) ? 0 : FZ)

int C64::step() {
  if (nmiPending) { nmiPending = false; irq(true); return 7; }
  if (!(p & FI) && (cia1.irq() || (vicIrqFlags & vic[0x1A] & 0x0F))) { irq(false); return 7; }
  if (pc >= 0xE000 && mapKernal && trap()) return 6;

  uint8_t op = read(pc++);
  int cyc = CYC[op];
  uint16_t ad = 0;
  uint8_t v;

  // --- addressing helpers (as lambdas over locals)
  auto imm = [&]() { return (uint16_t)(pc++); };
  auto zp = [&]() { return (uint16_t)read(pc++); };
  auto zpx = [&]() { return (uint16_t)((read(pc++) + x) & 0xFF); };
  auto zpy = [&]() { return (uint16_t)((read(pc++) + y) & 0xFF); };
  auto abs_ = [&]() { uint16_t l = read(pc++); return (uint16_t)(l | (read(pc++) << 8)); };
  auto absi = [&](uint8_t idx, bool rd) {
    uint16_t l = read(pc++); uint16_t b = l | (read(pc++) << 8);
    uint16_t r = b + idx;
    if (rd && ((b ^ r) & 0xFF00)) cyc++;
    return r;
  };
  auto indx = [&]() {
    uint8_t z = read(pc++) + x;
    return (uint16_t)(read(z) | (read((uint8_t)(z + 1)) << 8));
  };
  auto indy = [&](bool rd) {
    uint8_t z = read(pc++);
    uint16_t b = read(z) | (read((uint8_t)(z + 1)) << 8);
    uint16_t r = b + y;
    if (rd && ((b ^ r) & 0xFF00)) cyc++;
    return r;
  };
  auto adc = [&](uint8_t m) {
    unsigned c = p & FC;
    if (p & FD) {
      int al = (a & 0x0F) + (m & 0x0F) + c;
      if (al >= 0x0A) al = ((al + 0x06) & 0x0F) + 0x10;
      int r = (a & 0xF0) + (m & 0xF0) + al;
      uint8_t bin = (uint8_t)(a + m + c);
      p &= ~(FN | FZ | FV | FC);
      if (!bin) p |= FZ;
      if (r & 0x80) p |= FN;
      if (~(a ^ m) & (a ^ r) & 0x80) p |= FV;
      if (r >= 0xA0) r += 0x60;
      if (r >= 0x100) p |= FC;
      a = (uint8_t)r;
    } else {
      unsigned r = a + m + c;
      p &= ~(FC | FV);
      if (r > 0xFF) p |= FC;
      if (~(a ^ m) & (a ^ r) & 0x80) p |= FV;
      a = (uint8_t)r;
      SETNZ(a);
    }
  };
  auto sbc = [&](uint8_t m) {
    unsigned c = p & FC;
    int bin = a - m - (1 - c);
    if (p & FD) {
      int al = (a & 0x0F) - (m & 0x0F) - (1 - (int)c);
      if (al < 0) al = ((al - 0x06) & 0x0F) - 0x10;
      int r = (a & 0xF0) - (m & 0xF0) + al;
      if (r < 0) r -= 0x60;
      p &= ~(FN | FZ | FV | FC);
      if (bin >= 0) p |= FC;
      if ((a ^ m) & (a ^ bin) & 0x80) p |= FV;
      SETNZ((uint8_t)bin);
      a = (uint8_t)r;
    } else {
      p &= ~(FC | FV);
      if (bin >= 0) p |= FC;
      if ((a ^ m) & (a ^ bin) & 0x80) p |= FV;
      a = (uint8_t)bin;
      SETNZ(a);
    }
  };
  auto cmp = [&](uint8_t r, uint8_t m) {
    int t = r - m;
    p = (p & ~FC) | (t >= 0 ? FC : 0);
    SETNZ((uint8_t)t);
  };
  auto asl = [&](uint8_t m) { p = (p & ~FC) | (m >> 7); m <<= 1; SETNZ(m); return m; };
  auto lsr = [&](uint8_t m) { p = (p & ~FC) | (m & 1); m >>= 1; SETNZ(m); return m; };
  auto rol = [&](uint8_t m) { uint8_t c = p & FC; p = (p & ~FC) | (m >> 7); m = (m << 1) | c; SETNZ(m); return m; };
  auto ror = [&](uint8_t m) { uint8_t c = p & FC; p = (p & ~FC) | (m & 1); m = (m >> 1) | (c << 7); SETNZ(m); return m; };
  auto branch = [&](bool cond) {
    int8_t off = (int8_t)read(pc++);
    if (cond) {
      uint16_t n = pc + off;
      cyc += ((n ^ pc) & 0xFF00) ? 2 : 1;
      pc = n;
    }
  };

  // ALU group decode for the regular opcodes
  switch (op) {
    // ---- loads
    case 0xA9: a = read(imm()); SETNZ(a); break;
    case 0xA5: a = read(zp()); SETNZ(a); break;
    case 0xB5: a = read(zpx()); SETNZ(a); break;
    case 0xAD: a = read(abs_()); SETNZ(a); break;
    case 0xBD: a = read(absi(x, true)); SETNZ(a); break;
    case 0xB9: a = read(absi(y, true)); SETNZ(a); break;
    case 0xA1: a = read(indx()); SETNZ(a); break;
    case 0xB1: a = read(indy(true)); SETNZ(a); break;
    case 0xA2: x = read(imm()); SETNZ(x); break;
    case 0xA6: x = read(zp()); SETNZ(x); break;
    case 0xB6: x = read(zpy()); SETNZ(x); break;
    case 0xAE: x = read(abs_()); SETNZ(x); break;
    case 0xBE: x = read(absi(y, true)); SETNZ(x); break;
    case 0xA0: y = read(imm()); SETNZ(y); break;
    case 0xA4: y = read(zp()); SETNZ(y); break;
    case 0xB4: y = read(zpx()); SETNZ(y); break;
    case 0xAC: y = read(abs_()); SETNZ(y); break;
    case 0xBC: y = read(absi(x, true)); SETNZ(y); break;
    // ---- stores
    case 0x85: write(zp(), a); break;
    case 0x95: write(zpx(), a); break;
    case 0x8D: write(abs_(), a); break;
    case 0x9D: write(absi(x, false), a); break;
    case 0x99: write(absi(y, false), a); break;
    case 0x81: write(indx(), a); break;
    case 0x91: write(indy(false), a); break;
    case 0x86: write(zp(), x); break;
    case 0x96: write(zpy(), x); break;
    case 0x8E: write(abs_(), x); break;
    case 0x84: write(zp(), y); break;
    case 0x94: write(zpx(), y); break;
    case 0x8C: write(abs_(), y); break;
    // ---- transfers
    case 0xAA: x = a; SETNZ(x); break;
    case 0xA8: y = a; SETNZ(y); break;
    case 0x8A: a = x; SETNZ(a); break;
    case 0x98: a = y; SETNZ(a); break;
    case 0xBA: x = sp; SETNZ(x); break;
    case 0x9A: sp = x; break;
    // ---- stack
    case 0x48: push(a); break;
    case 0x68: a = pop(); SETNZ(a); break;
    case 0x08: push(p | FB | FU); break;
    case 0x28: p = (pop() & ~FB) | FU; break;
    // ---- logic / arithmetic
#define ALU(base, OPX)                                   \
  case base + 0x09: v = read(imm()); OPX; break;         \
  case base + 0x05: v = read(zp()); OPX; break;          \
  case base + 0x15: v = read(zpx()); OPX; break;         \
  case base + 0x0D: v = read(abs_()); OPX; break;        \
  case base + 0x1D: v = read(absi(x, true)); OPX; break; \
  case base + 0x19: v = read(absi(y, true)); OPX; break; \
  case base + 0x01: v = read(indx()); OPX; break;        \
  case base + 0x11: v = read(indy(true)); OPX; break;
    ALU(0x00, a |= v; SETNZ(a))
    ALU(0x20, a &= v; SETNZ(a))
    ALU(0x40, a ^= v; SETNZ(a))
    ALU(0x60, adc(v))
    ALU(0xC0, cmp(a, v))
    ALU(0xE0, sbc(v))
#undef ALU
    case 0xEB: sbc(read(imm())); break;  // illegal SBC
    case 0xE0: cmp(x, read(imm())); break;
    case 0xE4: cmp(x, read(zp())); break;
    case 0xEC: cmp(x, read(abs_())); break;
    case 0xC0: cmp(y, read(imm())); break;
    case 0xC4: cmp(y, read(zp())); break;
    case 0xCC: cmp(y, read(abs_())); break;
    case 0x24: case 0x2C:
      v = read(op == 0x24 ? zp() : abs_());
      p = (p & ~(FN | FV | FZ)) | (v & (FN | FV)) | ((a & v) ? 0 : FZ);
      break;
    // ---- inc/dec
    case 0xE8: x++; SETNZ(x); break;
    case 0xC8: y++; SETNZ(y); break;
    case 0xCA: x--; SETNZ(x); break;
    case 0x88: y--; SETNZ(y); break;
#define RMW(base, EXPR)                                                    \
  case base + 0x06: ad = zp(); v = read(ad); EXPR; write(ad, v); break;           \
  case base + 0x16: ad = zpx(); v = read(ad); EXPR; write(ad, v); break;          \
  case base + 0x0E: ad = abs_(); v = read(ad); EXPR; write(ad, v); break;         \
  case base + 0x1E: ad = absi(x, false); v = read(ad); EXPR; write(ad, v); break;
    RMW(0x00, v = asl(v))
    RMW(0x20, v = rol(v))
    RMW(0x40, v = lsr(v))
    RMW(0x60, v = ror(v))
    RMW(0xC0, v--; SETNZ(v))
    RMW(0xE0, v++; SETNZ(v))
#undef RMW
    case 0x0A: a = asl(a); break;
    case 0x2A: a = rol(a); break;
    case 0x4A: a = lsr(a); break;
    case 0x6A: a = ror(a); break;
    // ---- jumps
    case 0x4C: pc = abs_(); break;
    case 0x6C: {
      uint16_t i = abs_();
      pc = read(i) | (read((i & 0xFF00) | ((i + 1) & 0xFF)) << 8);
      break;
    }
    case 0x20: {
      uint16_t t = abs_();
      uint16_t r = pc - 1;
      push(r >> 8); push(r & 0xFF);
      pc = t;
      break;
    }
    case 0x60: { uint16_t l = pop(); pc = (l | (pop() << 8)) + 1; break; }
    case 0x40: { p = (pop() & ~FB) | FU; uint16_t l = pop(); pc = l | (pop() << 8); break; }
    case 0x00: {
      pc++;
      push(pc >> 8); push(pc & 0xFF);
      push(p | FB | FU);
      p |= FI;
      pc = read(0xFFFE) | (read(0xFFFF) << 8);
      break;
    }
    // ---- branches
    case 0x10: branch(!(p & FN)); break;
    case 0x30: branch(p & FN); break;
    case 0x50: branch(!(p & FV)); break;
    case 0x70: branch(p & FV); break;
    case 0x90: branch(!(p & FC)); break;
    case 0xB0: branch(p & FC); break;
    case 0xD0: branch(!(p & FZ)); break;
    case 0xF0: branch(p & FZ); break;
    // ---- flags
    case 0x18: p &= ~FC; break;
    case 0x38: p |= FC; break;
    case 0x58: p &= ~FI; break;
    case 0x78: p |= FI; break;
    case 0xB8: p &= ~FV; break;
    case 0xD8: p &= ~FD; break;
    case 0xF8: p |= FD; break;
    case 0xEA: break;
    // ---- illegal opcodes (common ones)
#define ILL_RMW(base, EXPR)                                                             \
  case base + 0x07: ad = zp(); v = read(ad); EXPR; break;                               \
  case base + 0x17: ad = zpx(); v = read(ad); EXPR; break;                              \
  case base + 0x0F: ad = abs_(); v = read(ad); EXPR; break;                             \
  case base + 0x1F: ad = absi(x, false); v = read(ad); EXPR; break;                     \
  case base + 0x1B: ad = absi(y, false); v = read(ad); EXPR; break;                     \
  case base + 0x03: ad = indx(); v = read(ad); EXPR; break;                             \
  case base + 0x13: ad = indy(false); v = read(ad); EXPR; break;
    ILL_RMW(0x00, v = asl(v); write(ad, v); a |= v; SETNZ(a))           // SLO
    ILL_RMW(0x20, v = rol(v); write(ad, v); a &= v; SETNZ(a))           // RLA
    ILL_RMW(0x40, v = lsr(v); write(ad, v); a ^= v; SETNZ(a))           // SRE
    ILL_RMW(0x60, v = ror(v); write(ad, v); adc(v))                     // RRA
    ILL_RMW(0xC0, v--; write(ad, v); cmp(a, v))                         // DCP
    ILL_RMW(0xE0, v++; write(ad, v); sbc(v))                            // ISC
#undef ILL_RMW
    // LAX
    case 0xA7: a = x = read(zp()); SETNZ(a); break;
    case 0xB7: a = x = read(zpy()); SETNZ(a); break;
    case 0xAF: a = x = read(abs_()); SETNZ(a); break;
    case 0xBF: a = x = read(absi(y, true)); SETNZ(a); break;
    case 0xA3: a = x = read(indx()); SETNZ(a); break;
    case 0xB3: a = x = read(indy(true)); SETNZ(a); break;
    case 0xAB: a = x = read(imm()); SETNZ(a); break;
    // SAX
    case 0x87: write(zp(), a & x); break;
    case 0x97: write(zpy(), a & x); break;
    case 0x8F: write(abs_(), a & x); break;
    case 0x83: write(indx(), a & x); break;
    // ANC / ALR / ARR / SBX
    case 0x0B: case 0x2B: a &= read(imm()); SETNZ(a); p = (p & ~FC) | (a >> 7); break;
    case 0x4B: a &= read(imm()); a = lsr(a); break;
    case 0x6B: {
      a &= read(imm());
      a = (a >> 1) | ((p & FC) << 7);
      SETNZ(a);
      p &= ~(FC | FV);
      if (a & 0x40) p |= FC;
      if (((a >> 6) ^ (a >> 5)) & 1) p |= FV;
      break;
    }
    case 0xCB: {
      int t = (a & x) - read(imm());
      p = (p & ~FC) | (t >= 0 ? FC : 0);
      x = (uint8_t)t; SETNZ(x);
      break;
    }
    // NOPs with operands
    case 0x1A: case 0x3A: case 0x5A: case 0x7A: case 0xDA: case 0xFA: break;
    case 0x80: case 0x82: case 0x89: case 0xC2: case 0xE2: pc++; break;
    case 0x04: case 0x44: case 0x64: read(zp()); break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xD4: case 0xF4: read(zpx()); break;
    case 0x0C: read(abs_()); break;
    case 0x1C: case 0x3C: case 0x5C: case 0x7C: case 0xDC: case 0xFC: read(absi(x, true)); break;
    // JAM: halt the CPU in place
    case 0x02: case 0x12: case 0x22: case 0x32: case 0x42: case 0x52:
    case 0x62: case 0x72: case 0x92: case 0xB2: case 0xD2: case 0xF2:
      pc--;
      break;
    default:
      // rarely used unstable opcodes (SHA/SHX/SHY/TAS/LAS/XAA): approximate
      switch (op) {
        case 0x8B: a = x & read(imm()); SETNZ(a); break;
        case 0xBB: { v = read(absi(y, true)) & sp; a = x = sp = v; SETNZ(v); break; }
        case 0x9C: { ad = absi(x, false); write(ad, y & ((ad >> 8) + 1)); break; }
        case 0x9E: { ad = absi(y, false); write(ad, x & ((ad >> 8) + 1)); break; }
        case 0x9F: case 0x93: { ad = (op == 0x9F) ? absi(y, false) : indy(false); write(ad, a & x & ((ad >> 8) + 1)); break; }
        case 0x9B: { sp = a & x; ad = absi(y, false); write(ad, sp & ((ad >> 8) + 1)); break; }
        default: break;
      }
      break;
  }
  return cyc;
}

// ---------------------------------------------------------------- KERNAL traps: device 8 virtual disk
static char petToAscii(uint8_t c) {
  if (c >= 0xC1 && c <= 0xDA) return (char)(c - 0x80);
  if (c >= 0x20 && c < 0x60) return (char)c;
  if (c >= 0x61 && c <= 0x7A) return (char)(c - 0x20);
  return '_';
}

bool C64::trap() {
  bool isLoad = (pc == 0xFFD5), isSave = (pc == 0xFFD8);
  uint16_t lv = ram[0x330] | (ram[0x331] << 8), sv = ram[0x332] | (ram[0x333] << 8);
  if (!isLoad && !isSave) {
    if (pc == lv && lv >= 0xE000) isLoad = true;
    else if (pc == sv && sv >= 0xE000) isSave = true;
    else return false;
  }
  uint8_t dev = ram[0xBA];
  if (dev < 8 || dev > 11) return false;

  std::string name;
  uint16_t fn = ram[0xBB] | (ram[0xBC] << 8);
  for (int i = 0; i < ram[0xB7]; i++) name += petToAscii(read(fn + i));

  if (isLoad) {
    std::vector<uint8_t> data;
    bool ok = false;
    uint8_t sa = ram[0xB9];
    if (name == "$" || name.rfind("$0", 0) == 0 || name.rfind("$:", 0) == 0) {
      // Build a BASIC "program" that LISTs like a 1541 directory.
      std::vector<std::pair<std::string, uint32_t>> files;
      if (diskList) diskList(files);
      uint16_t addr = 0x0801;
      data.push_back(0x01); data.push_back(0x08);
      auto addLine = [&](uint16_t num, const std::string& text) {
        uint16_t next = addr + 4 + text.size() + 1;
        data.push_back(next & 0xFF); data.push_back(next >> 8);
        data.push_back(num & 0xFF); data.push_back(num >> 8);
        for (char ch : text) data.push_back((uint8_t)ch);
        data.push_back(0);
        addr = next;
      };
      std::string t = diskTitle.substr(0, 16);
      t.resize(16, ' ');
      addLine(0, std::string("\x12\"") + t + "\" 64 2A");
      uint32_t used = 0;
      for (auto& f : files) {
        uint32_t blocks = (f.second + 253) / 254;
        used += blocks;
        std::string line;
        if (blocks < 10) line += "   ";
        else if (blocks < 100) line += "  ";
        else line += " ";
        std::string nm = f.first.substr(0, 16);
        line += "\"" + nm + "\"";
        line += std::string(17 - nm.size(), ' ');
        line += "PRG";
        addLine((uint16_t)blocks, line);
      }
      uint32_t freeb = used > 664 ? 0 : 664 - used;
      addLine((uint16_t)freeb, "BLOCKS FREE.");
      data.push_back(0); data.push_back(0);
      ok = true;
      sa = 0;
      x = 0x01; y = 0x08;
    } else if (diskLoad) {
      ok = diskLoad(name, data) && data.size() >= 2;
    }
    if (!ok) {
      a = 4;  // FILE NOT FOUND
      p |= FC;
      ram[0x90] = 0x42;
    } else {
      uint16_t dst = sa == 0 ? (uint16_t)(x | (y << 8)) : (uint16_t)(data[0] | (data[1] << 8));
      if (a == 0) {
        for (size_t i = 2; i < data.size(); i++) ram[dst++] = data[i];
      } else {
        dst += data.size() - 2;
      }
      x = dst & 0xFF; y = dst >> 8;
      ram[0xAE] = x; ram[0xAF] = y;
      a = 0;
      p &= ~FC;
      ram[0x90] = 0x40;
    }
  } else {
    uint16_t start = ram[a] | (ram[(uint8_t)(a + 1)] << 8);
    uint16_t end = x | (y << 8);
    std::vector<uint8_t> data;
    data.push_back(start & 0xFF); data.push_back(start >> 8);
    for (uint32_t i = start; i < end; i++) data.push_back(ram[i]);
    bool ok = !name.empty() && diskSave && diskSave(name, data);
    if (ok) { p &= ~FC; a = 0; ram[0x90] = 0; }
    else { p |= FC; a = 5; ram[0x90] = 0x80; }
  }
  uint16_t l = pop();
  pc = (l | (pop() << 8)) + 1;
  return true;
}

// ---------------------------------------------------------------- video
bool C64::isBadLine(int line) {
  return denFrame && line >= 0x30 && line <= 0xF7 && (line & 7) == (vic[0x11] & 7);
}

void C64::renderLine(int line) {
  if (line < C64_FIRST_LINE || line >= C64_FIRST_LINE + C64_H) return;
  bool anySprites = vic[0x15] != 0;
  if (!renderEnabled && !anySprites) return;

  uint8_t out[C64_W];
  uint8_t fg[C64_W];
  memset(fg, 0, sizeof fg);
  uint8_t d011 = vic[0x11], d016 = vic[0x16], d018 = vic[0x18];
  uint8_t border = vic[0x20] & 15, bg0 = vic[0x21] & 15;
  bool den = d011 & 0x10;
  int top = (d011 & 8) ? 51 : 55, bottom = (d011 & 8) ? 251 : 247;
  int left = (d016 & 8) ? 32 : 39, right = (d016 & 8) ? 352 : 343;
  bool inWindow = den && line >= top && line < bottom;
  uint16_t screenBase = ((d018 >> 4) & 15) * 0x400;

  if (inWindow) {
    bool ecm = d011 & 0x40, bmm = d011 & 0x20, mcm = d016 & 0x10;
    int yscroll = d011 & 7, xscroll = d016 & 7;
    int ly = line - (0x30 + yscroll);
    memset(out, bg0, sizeof out);
    if (ly >= 0 && ly < 200) {
      int row = ly >> 3, rc = ly & 7;
      uint16_t charBase = ((d018 >> 1) & 7) * 0x800;
      uint16_t bmpBase = (d018 & 8) ? 0x2000 : 0;
      int sx = 32 + xscroll;
      for (int col = 0; col < 40; col++, sx += 8) {
        int idx = row * 40 + col;
        uint8_t sc = vicRead(screenBase + idx);
        uint8_t cl = colorRam[idx] & 15;
        uint8_t* o = out + sx;
        uint8_t* f = fg + sx;
        int maxp = C64_W - sx;
        if (maxp > 8) maxp = 8;
        if (maxp <= 0) break;
        uint8_t px[8], pf[8];
        if (!bmm) {
          uint8_t bits;
          uint8_t bgc = bg0;
          if (ecm) { bits = vicRead(charBase + (sc & 0x3F) * 8 + rc); bgc = vic[0x21 + (sc >> 6)] & 15; }
          else bits = vicRead(charBase + sc * 8 + rc);
          if (mcm && !ecm && (cl & 8)) {
            uint8_t c4[4] = {bg0, (uint8_t)(vic[0x22] & 15), (uint8_t)(vic[0x23] & 15), (uint8_t)(cl & 7)};
            for (int i = 0; i < 4; i++) {
              int b = (bits >> (6 - i * 2)) & 3;
              px[i * 2] = px[i * 2 + 1] = c4[b];
              pf[i * 2] = pf[i * 2 + 1] = b >> 1;
            }
          } else if (mcm && ecm) {
            for (int i = 0; i < 8; i++) { px[i] = 0; pf[i] = (bits >> (7 - i)) & 1; }
          } else {
            uint8_t fc = (mcm ? (cl & 7) : cl);
            for (int i = 0; i < 8; i++) {
              int b = (bits >> (7 - i)) & 1;
              px[i] = b ? fc : bgc;
              pf[i] = b;
            }
          }
        } else {
          uint8_t bits = vicRead(bmpBase + row * 320 + col * 8 + rc);
          if (ecm) {
            for (int i = 0; i < 8; i++) { px[i] = 0; pf[i] = 0; }
          } else if (mcm) {
            uint8_t c4[4] = {bg0, (uint8_t)(sc >> 4), (uint8_t)(sc & 15), cl};
            for (int i = 0; i < 4; i++) {
              int b = (bits >> (6 - i * 2)) & 3;
              px[i * 2] = px[i * 2 + 1] = c4[b];
              pf[i * 2] = pf[i * 2 + 1] = b >> 1;
            }
          } else {
            uint8_t f1 = sc >> 4, f0 = sc & 15;
            for (int i = 0; i < 8; i++) {
              int b = (bits >> (7 - i)) & 1;
              px[i] = b ? f1 : f0;
              pf[i] = b;
            }
          }
        }
        for (int i = 0; i < maxp; i++) { o[i] = px[i]; f[i] = pf[i]; }
      }
    }
  } else {
    memset(out, border, sizeof out);
  }

  // ---- sprites (drawn from 7 down to 0 so sprite 0 ends on top)
  if (anySprites) {
    uint8_t smask[C64_W];
    memset(smask, 0, sizeof smask);
    uint8_t newSS = 0, newSB = 0;
    // first pass: compute coverage per sprite for collisions, then draw
    for (int n = 7; n >= 0; n--) {
      uint8_t bit = 1 << n;
      if (!(vic[0x15] & bit)) continue;
      int sy = vic[1 + n * 2];
      bool ey = vic[0x17] & bit, ex = vic[0x1D] & bit, mc = vic[0x1C] & bit;
      int h = ey ? 42 : 21;
      int dy = line - sy - 1;
      if (dy < 0) dy += 256;  // wraparound near top
      if (dy >= h) continue;
      int srow = ey ? dy >> 1 : dy;
      uint16_t ptr = vicRead(screenBase + 0x3F8 + n) * 64 + srow * 3;
      uint32_t data = (vicRead(ptr) << 16) | (vicRead(ptr + 1) << 8) | vicRead(ptr + 2);
      int sxp = vic[n * 2] | ((vic[0x10] & bit) ? 0x100 : 0);
      int scr = sxp + 8;
      bool behind = vic[0x1B] & bit;
      uint8_t col = vic[0x27 + n] & 15;
      int wmul = ex ? 2 : 1;
      for (int i = 0; i < 24; i++) {
        uint8_t c;
        bool solid;
        if (mc) {
          int b = (data >> (22 - (i & ~1))) & 3;
          solid = b != 0;
          c = b == 1 ? (vic[0x25] & 15) : b == 2 ? col : (vic[0x26] & 15);
        } else {
          solid = (data >> (23 - i)) & 1;
          c = col;
        }
        if (!solid) continue;
        for (int w = 0; w < wmul; w++) {
          int px = scr + i * wmul + w;
          if (px < 0 || px >= C64_W) continue;
          if (smask[px]) newSS |= smask[px] | bit;
          if (fg[px]) newSB |= bit;
          smask[px] |= bit;
          // later (lower numbered) sprites overwrite; priority vs graphics
          if (!(behind && fg[px])) out[px] = c;
        }
      }
    }
    if (newSS) { if (!collSS) vicIrqFlags |= 4; collSS |= newSS; }
    if (newSB) { if (!collSB) vicIrqFlags |= 2; collSB |= newSB; }
  }
  if (!renderEnabled) return;

  // ---- side borders
  if (inWindow) {
    for (int i = 0; i < left; i++) out[i] = border;
    for (int i = right; i < C64_W; i++) out[i] = border;
  }
  uint8_t* dst = fb + (line - C64_FIRST_LINE) * (C64_W / 2);
  for (int i = 0; i < C64_W; i += 2) dst[i >> 1] = (out[i] & 15) | (out[i + 1] << 4);
}

// ---------------------------------------------------------------- frame loop
void C64::runFrame() {
  for (int line = 0; line < C64_LINES; line++) {
    raster = line;
    if (line == rasterCmp) vicIrqFlags |= 1;
    if (line == 0x30) denFrame = (vic[0x11] & 0x10) != 0;
    bool bad = isBadLine(line);
    pendingCycles += bad ? 23 : C64_CYCLES_PER_LINE;
    while (pendingCycles > 0) {
      int c = step();
      cia1.tick(c);
      cia2.tick(c);
      bool n = cia2.irq();
      if (n && !nmiLine) nmiPending = true;
      nmiLine = n;
      pendingCycles -= c;
      totalCycles += c;
    }
    if (bad) { cia1.tick(40); cia2.tick(40); }
    renderLine(line);
  }
  frameCount++;
  if (frameCount % 5 == 0) { cia1.todTenth(); cia2.todTenth(); }
  pumpTypeBuffer();
}

// ---------------------------------------------------------------- helpers
static int asciiToPetscii(char c) {
  if (c == '\n' || c == '\r') return 0x0D;
  if (c >= 'a' && c <= 'z') return c - 'a' + 0x41;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 0x41;
  if (c >= 0x20 && c <= 0x5F) return c;  // digits, punctuation, @[\]^_
  return -1;
}

bool C64::typeText(const char* s) {
  char prev = 0;
  for (; *s; prev = *s, s++) {
    if (*s == '\n' && prev == '\r') continue;  // CRLF -> one RETURN
    int c = asciiToPetscii(*s);
    if (c < 0) continue;
    int n = (tqHead + 1) % (int)sizeof(tq);
    if (n == tqTail) return false;
    tq[tqHead] = (char)c;
    tqHead = n;
  }
  return true;
}

void C64::pumpTypeBuffer() {
  if (tqTail == tqHead) return;
  if (ram[0xC6] != 0) return;  // wait until the KERNAL has consumed the buffer
  int n = 0;
  while (n < 10 && tqTail != tqHead) {
    uint8_t c = (uint8_t)tq[tqTail];
    tqTail = (tqTail + 1) % (int)sizeof(tq);
    ram[0x277 + n++] = c;
    if (c == 0x0D) break;  // let BASIC run the line before typing more
  }
  ram[0xC6] = n;
}

void C64::injectPrg(const uint8_t* d, size_t len, bool run) {
  if (len < 3) return;
  uint16_t addr = d[0] | (d[1] << 8);
  uint16_t start = addr;
  for (size_t i = 2; i < len; i++) ram[addr++] = d[i];
  if (start == 0x0801) {
    ram[0x2D] = ram[0x2F] = ram[0x31] = addr & 0xFF;
    ram[0x2E] = ram[0x30] = ram[0x32] = addr >> 8;
    ram[0xAE] = addr & 0xFF; ram[0xAF] = addr >> 8;
    if (run) typeText("RUN\r");
  } else if (run) {
    char buf[24];
    snprintf(buf, sizeof buf, "SYS%u\r", start);
    typeText(buf);
  }
}
