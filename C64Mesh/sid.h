// SID 6581 sound chip emulation (compact): 3 voices with triangle/saw/pulse/noise,
// ring mod + sync, real ADSR rate tables, state-variable filter, master volume
// (including the $D418 "volume digi" trick). Output: 16-bit mono PCM.
#pragma once
#include <stdint.h>

static const int SID_RATE = 22050;               // output sample rate
static const double SID_CLOCK = 985248.0;        // PAL phi2

struct SidVoice {
  uint32_t acc = 0, noise = 0x7FFFF8;
  uint16_t freq = 0, pw = 0;
  uint8_t ctrl = 0, ad = 0, sr = 0;
  // envelope
  uint8_t env = 0;
  uint8_t state = 2;          // 0 attack, 1 decay/sustain, 2 release
  float rateCnt = 0;
  int expCnt = 0;
  bool msbRise = false;       // accumulator MSB went 0->1 this sample (for sync)
  uint16_t lastOut = 0;
};

class SID {
 public:
  SID() { reset(); }
  void reset();
  void write(int reg, uint8_t v);
  uint8_t read(int reg);
  void render(int16_t* out, int n);   // render n samples at SID_RATE

 private:
  SidVoice v[3];
  uint8_t regs[32];
  float lp = 0, bp = 0, hp = 0;
  float dcIn = 0, dcOut = 0;          // DC blocker state
  uint8_t lastVol = 0;

  void clockEnvelope(SidVoice& s, float cycles);
  uint16_t wave(int i);
};
