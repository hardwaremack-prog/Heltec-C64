// Heltec-C64 - a small Commodore 64 emulator core (6502 + VIC-II + CIA x2)
// Portable C++: runs on ESP32-S3 (Heltec V3) and on a PC for testing.
#pragma once
#include <stdint.h>
#include <string.h>
#include <string>
#include <vector>
#include <functional>
#include "sid.h"
#include "media.h"

// ---- Screen geometry (PAL, with border) ----
static const int C64_W = 384;            // 32px border + 320 + 32px border
static const int C64_H = 272;            // 36 lines + 200 + 36 lines
static const int C64_FIRST_LINE = 15;    // first raster line shown
static const int C64_LINES = 312;        // PAL
static const int C64_CYCLES_PER_LINE = 63;
static const int C64_CYCLES_PER_FRAME = C64_LINES * C64_CYCLES_PER_LINE;  // 19656
static const double C64_FRAME_US = 19950.0;  // ~50.125 Hz

extern const uint32_t C64_PALETTE[16];  // 0xRRGGBB

struct CIA {
  uint8_t pra = 0xFF, prb = 0xFF, ddra = 0, ddrb = 0;
  uint16_t ta = 0xFFFF, tb = 0xFFFF, la = 0xFFFF, lb = 0xFFFF;
  uint8_t cra = 0, crb = 0;
  uint8_t icrMask = 0, icrFlags = 0;
  uint8_t sdr = 0;
  // time of day (BCD)
  uint8_t tod[4] = {0, 0, 0, 1};  // tenths, sec, min, hr (1 AM)
  uint8_t todLatch[4] = {0, 0, 0, 0};
  bool todLatched = false, todStopped = false;
  void reset() { *this = CIA(); }
  void tick(int cycles);
  bool irq() const { return (icrFlags & icrMask & 0x1F) != 0; }
  void todTenth();
};

class C64 {
 public:
  C64();

  // ROMs (BASIC 8K, KERNAL 8K, CHARGEN 4K). Copied in.
  void setRoms(const uint8_t* basic, const uint8_t* kernal, const uint8_t* chargen);
  void reset();            // hard reset (clears RAM)
  void runFrame();         // emulate one PAL frame
  void restoreKey();       // RESTORE key -> NMI

  // Framebuffer: 4 bits per pixel, 2 pixels per byte (left pixel in low nibble)
  uint8_t* fb;             // C64_W*C64_H/2 bytes
  bool renderEnabled = true;
  volatile uint32_t frameCount = 0;

  // Keyboard matrix: keyRow[row] bit col = pressed (row = CIA1 PA bit, col = PB bit)
  uint8_t keyRow[8];
  uint8_t joy2 = 0;        // bits: 0 up,1 down,2 left,3 right,4 fire (1 = pressed)
  uint8_t joy1 = 0;

  // Cartridge port: insertCart() then reset() to start it; removeCart() to pull it out.
  void insertCart(const CartImage& c);
  void removeCart();
  bool hasCart() const { return cartOn; }

  // Floppy image in drive 8 (takes priority over the file hooks below for LOAD). nullptr = none.
  D64* drive8 = nullptr;

  // Virtual disk (device 8) hooks: return true on success
  std::function<bool(const std::string& pattern, std::vector<uint8_t>& data)> diskLoad;
  std::function<bool(const std::string& name, const std::vector<uint8_t>& data)> diskSave;
  std::function<void(std::vector<std::pair<std::string, uint32_t>>& files)> diskList;
  std::string diskTitle = "HELTEC-C64";

  // Sound: when audioEnabled, each frame renders ~440 samples (SID_RATE Hz, mono int16)
  // into a ring buffer that the network side drains with readAudio().
  volatile bool audioEnabled = false;
  int readAudio(int16_t* out, int max);
  int audioAvailable() const { return (aHead - aTail + AUDIO_RING) % AUDIO_RING; }

  // Helpers for the outside world
  void injectPrg(const uint8_t* data, size_t len, bool basicRun);
  bool typeText(const char* ascii);   // queue text into keyboard buffer (returns false if busy)
  void pumpTypeBuffer();               // called each frame
  uint8_t peek(uint16_t a) const { return ram[a]; }
  const uint8_t* charset() const { return charRom; }
  uint8_t vicReg(int r) const { return vic[r & 63]; }
  uint8_t colorAt(int i) const { return colorRam[i & 0x3FF]; }
  void poke(uint16_t a, uint8_t v) { ram[a] = v; }

  // Test hooks (CPU test suite): load a 64K image with all ROMs banked out
  void cpuTestSetup(const uint8_t* img64k, uint16_t startPc) {
    memcpy(ram, img64k, 65536); port00 = 0x2F; port01 = 0x30; updateBanking(); pc = startPc; sp = 0xFF; p = 0x24; }
  int cpuStep() { return step(); }

  // CPU state (public for debugging)
  uint16_t pc; uint8_t a, x, y, sp, p;
  uint64_t totalCycles = 0;

 private:
  uint8_t ram[65536];
  uint8_t basicRom[8192], kernalRom[8192], charRom[4096];
  uint8_t colorRam[1024];
  uint8_t vic[64];
  SID sid;
  struct SidWrite { uint16_t cycle; uint8_t reg, val; };
  SidWrite sidLog[1024];
  int sidLogN = 0;
  int frameCycle = 0;          // CPU cycle within the current frame
  double sampleCarry = 0;      // cycle position of the next audio sample
  static const int AUDIO_RING = 4096;      // ~185 ms of sound
  int16_t aRing[AUDIO_RING];
  volatile int aHead = 0, aTail = 0;
  void finishAudioFrame();
  CIA cia1, cia2;
  uint8_t port00 = 0x2F, port01 = 0x37;
  int raster = 0;
  uint16_t rasterCmp = 0;
  bool denFrame = false;
  uint8_t collSS = 0, collSB = 0;
  bool nmiLine = false, nmiPending = false;
  uint8_t vicIrqFlags = 0;
  int pendingCycles = 0;
  // single-producer/single-consumer ring for typed text (PETSCII)
  char tq[1024];
  volatile int tqHead = 0, tqTail = 0;

  // banking
  bool mapBasic = true, mapKernal = true, mapIO = true, mapChar = false;
  bool ultimax = false, mapRomL = false, mapRomHa = false, mapRomHe = false;

  // cartridge
  CartImage cart;
  bool cartOn = false;
  uint8_t exromLine = 1, gameLine = 1;   // 1 = high (inactive)
  int cartBank = 0;
  const uint8_t* curL = nullptr;
  const uint8_t* curH = nullptr;
  uint16_t curMaskL = 0x1FFF, curMaskH = 0x1FFF;
  uint8_t efRam[256];
  void cartReset();
  void cartSelect(int bank);
  uint8_t cartIoRead(uint16_t ad);
  void cartIoWrite(uint16_t ad, uint8_t v);
  bool buildDirectory(std::vector<uint8_t>& data);
  void updateBanking();

  uint8_t read(uint16_t addr);
  void write(uint16_t addr, uint8_t v);
  uint8_t readIO(uint16_t addr);
  void writeIO(uint16_t addr, uint8_t v);
  uint8_t readCIA(CIA& c, int reg, bool isCia1);
  void writeCIA(CIA& c, int reg, uint8_t v, bool isCia1);
  uint8_t vicRead(uint16_t a14);  // VIC view of memory (14-bit address in current bank)
  uint16_t vicBank();

  // cpu
  int step();
  void irq(bool nmi);
  inline void push(uint8_t v) { ram[0x100 | sp--] = v; }
  inline uint8_t pop() { return ram[0x100 | ++sp]; }
  bool trap();

  // video
  void renderLine(int line);
  bool isBadLine(int line);

  friend struct CIA;
};
