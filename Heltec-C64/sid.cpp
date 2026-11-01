#include "sid.h"
#include <math.h>
#include <string.h>

static const float CPS = (float)(SID_CLOCK / SID_RATE);            // ~44.68 cycles per sample
static const uint32_t STEP_FIX = (uint32_t)(SID_CLOCK / SID_RATE * 256.0 + 0.5);
// ADSR: CPU cycles per envelope step for each rate setting (from the real chip)
static const float RATE[16] = {9, 32, 63, 95, 149, 220, 267, 313, 392, 977, 1954, 3126, 3907, 11720, 19532, 31251};

static inline int expPeriod(uint8_t e) {
  if (e >= 93) return 1;
  if (e >= 54) return 2;
  if (e >= 26) return 4;
  if (e >= 14) return 8;
  if (e >= 6) return 16;
  if (e == 0) return 1;
  return 30;
}

void SID::reset() {
  for (auto& s : v) s = SidVoice();
  memset(regs, 0, sizeof regs);
  lp = bp = hp = 0;
  dcIn = dcOut = 0;
}

void SID::write(int reg, uint8_t val) {
  reg &= 31;
  regs[reg] = val;
  if (reg < 21) {
    SidVoice& s = v[reg / 7];
    switch (reg % 7) {
      case 0: s.freq = (s.freq & 0xFF00) | val; break;
      case 1: s.freq = (s.freq & 0x00FF) | (val << 8); break;
      case 2: s.pw = (s.pw & 0x0F00) | val; break;
      case 3: s.pw = (s.pw & 0x00FF) | ((val & 15) << 8); break;
      case 4: {
        bool was = s.ctrl & 1, now = val & 1;
        if (!was && now) { s.state = 0; }
        else if (was && !now) { s.state = 2; }
        if (val & 8) { s.acc = 0; s.noise = 0x7FFFF8; }
        s.ctrl = val;
        break;
      }
      case 5: s.ad = val; break;
      case 6: s.sr = val; break;
    }
  }
}

static inline void clockNoise(SidVoice& s) {
  uint32_t bit = ((s.noise >> 22) ^ (s.noise >> 17)) & 1;
  s.noise = ((s.noise << 1) | bit) & 0x7FFFFF;
}

uint8_t SID::read(int reg) {
  reg &= 31;
  if (reg == 0x1B) {
    if (v[2].ctrl & 0x80) clockNoise(v[2]);  // keep "random number" reads changing
    return wave(2) >> 4;
  }
  if (reg == 0x1C) return v[2].env;
  if (reg == 0x19 || reg == 0x1A) return 0xFF;  // paddles
  return 0;
}

uint16_t SID::wave(int i) {
  SidVoice& s = v[i];
  const SidVoice& src = v[(i + 2) % 3];
  uint8_t c = s.ctrl;
  if (!(c & 0xF0)) return 0x800;
  uint16_t out = 0xFFF;
  if (c & 0x10) {
    uint32_t msb = s.acc & 0x800000;
    if (c & 0x04) msb ^= src.acc & 0x800000;
    out &= ((msb ? ~s.acc : s.acc) >> 11) & 0xFFF;
  }
  if (c & 0x20) out &= (s.acc >> 12) & 0xFFF;
  if (c & 0x40) out &= ((c & 8) || ((s.acc >> 12) >= s.pw)) ? 0xFFF : 0;
  if (c & 0x80) {
    uint32_t n = s.noise;
    out &= ((n >> 11) & 0x800) | ((n >> 10) & 0x400) | ((n >> 7) & 0x200) | ((n >> 5) & 0x100) |
           ((n >> 4) & 0x080) | ((n >> 1) & 0x040) | ((n << 1) & 0x020) | ((n << 2) & 0x010);
  }
  return out;
}

void SID::clockEnvelope(SidVoice& s, float cycles) {
  s.rateCnt += cycles;
  if (s.state == 0) {
    float per = RATE[s.ad >> 4];
    while (s.rateCnt >= per) {
      s.rateCnt -= per;
      if (s.env < 255) s.env++;
      if (s.env == 255) { s.state = 1; s.expCnt = 0; break; }
    }
  } else {
    float per = RATE[s.state == 1 ? (s.ad & 15) : (s.sr & 15)];
    uint8_t floor_ = s.state == 1 ? (uint8_t)((s.sr >> 4) * 17) : 0;
    while (s.rateCnt >= per) {
      s.rateCnt -= per;
      if (++s.expCnt >= expPeriod(s.env)) {
        s.expCnt = 0;
        if (s.env > floor_) s.env--;
      }
    }
  }
}

void SID::render(int16_t* out, int n) {
  // filter coefficients (TPT state-variable filter, unconditionally stable)
  int fc = (regs[0x16] << 3) | (regs[0x15] & 7);
  float hz = 30.0f + fc * 5.8f;
  if (hz > SID_RATE * 0.45f) hz = SID_RATE * 0.45f;
  float g = tanf(3.14159265f * hz / SID_RATE);
  float k = 1.414f - (regs[0x17] >> 4) * (1.2f / 15.0f);
  float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
  uint8_t route = regs[0x17] & 7, mode = regs[0x18];
  float vol = (mode & 15) / 15.0f;

  for (int t = 0; t < n; t++) {
    // oscillators
    for (int i = 0; i < 3; i++) {
      SidVoice& s = v[i];
      if (s.ctrl & 8) { s.acc = 0; s.msbRise = false; continue; }
      uint32_t old = s.acc;
      uint32_t u = old + ((s.freq * STEP_FIX) >> 8);
      uint32_t edges = ((u + 0x80000) >> 20) - ((old + 0x80000) >> 20);
      if (edges > 16) edges = 16;
      while (edges--) clockNoise(s);
      s.acc = u & 0xFFFFFF;
      s.msbRise = ((old & 0x800000) == 0 && (u & 0x800000)) || (u > 0xFFFFFF && (s.acc & 0x800000));
    }
    for (int i = 0; i < 3; i++)
      if ((v[i].ctrl & 2) && v[(i + 2) % 3].msbRise) v[i].acc = 0;

    float direct = 0, filt = 0;
    for (int i = 0; i < 3; i++) {
      SidVoice& s = v[i];
      clockEnvelope(s, CPS);
      uint16_t w = wave(i);
      s.lastOut = w;
      float o = ((int)w - 0x800) * (float)s.env;
      if (route & (1 << i)) filt += o;
      else if (!(i == 2 && (mode & 0x80))) direct += o;
    }
    // filter
    float v3 = filt - hp, v1 = a1 * lp + a2 * v3, v2 = hp + a2 * lp + a3 * v3;
    lp = 2 * v1 - lp;   // (state ic1 stored in lp)
    hp = 2 * v2 - hp;   // (state ic2 stored in hp)
    float fout = 0;
    if (mode & 0x10) fout += v2;          // low pass
    if (mode & 0x20) fout += v1;          // band pass
    if (mode & 0x40) fout += filt - k * v1 - v2;  // high pass
    // mixer: volume scales everything, plus the 6581's DC offset that makes $D418 digis audible
    float mix = (direct + fout) * vol + vol * 250000.0f;
    // DC blocker
    float y = mix - dcIn + 0.996f * dcOut;
    dcIn = mix; dcOut = y;
    float sOut = y * 0.021f;
    if (sOut > 32000) sOut = 32000 + (sOut - 32000) * 0.1f;
    if (sOut < -32000) sOut = -32000 + (sOut + 32000) * 0.1f;
    if (sOut > 32767) sOut = 32767;
    if (sOut < -32768) sOut = -32768;
    out[t] = (int16_t)sOut;
  }
}
