// Cartridges (.crt) and floppy images (.d64) for Heltec-C64.
// Both read their data through pointers/callbacks, so the image can live in RAM (PC) or in
// memory-mapped flash / a flash file (ESP32) without being copied into the ESP32's small RAM.
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>

// ---------------------------------------------------------------- cartridges
struct CartImage {
  static const int MAX_BANKS = 128;
  int type = 0;                 // CRT hardware type (0 = normal 8K/16K/Ultimax)
  uint8_t exrom = 1, game = 1;  // line levels at power-on (0 = low/active)
  int banks = 0;
  const uint8_t* roml[MAX_BANKS] = {};   // $8000 window per bank
  const uint8_t* romh[MAX_BANKS] = {};   // $A000 (or $E000 in Ultimax) window per bank
  uint16_t maskL[MAX_BANKS] = {}, maskH[MAX_BANKS] = {};   // chip size - 1 (smaller chips mirror)
  char name[33] = {};
};

// Parses a .crt file (or a raw 8K/16K .bin). On success fills `out` with pointers into `d`.
bool parseCartridge(const uint8_t* d, size_t n, CartImage& out, std::string& err);
const char* cartTypeName(int type);
bool cartTypeSupported(int type);

// ---------------------------------------------------------------- D64 floppy images
class D64 {
 public:
  // Reads `n` bytes at `off` from the image file. Must return false on failure.
  std::function<bool(uint32_t off, uint8_t* buf, size_t n)> readAt;
  uint32_t size = 0;

  struct Entry {
    uint8_t name[16]; int nameLen;
    uint8_t type;      // raw directory type byte (bit 7 = closed, bit 6 = locked, low 3 bits = DEL/SEQ/PRG/USR/REL)
    int blocks;
    uint8_t t, s;
  };
  static bool validSize(uint32_t n);
  bool header(uint8_t name[16], uint8_t id[5], int& blocksFree);
  bool list(std::vector<Entry>& out);
  // pattern is raw PETSCII from the LOAD command (wildcards * and ?, optional "0:" prefix)
  bool load(const uint8_t* pat, int patLen, std::vector<uint8_t>& data);

 private:
  int tracks() const { return size >= 196608 ? 40 : 35; }
  bool sector(int t, int s, uint8_t* buf);
};
