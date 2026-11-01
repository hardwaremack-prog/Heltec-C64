// Minimal RFB (VNC) server for the C64 screen.
// - RFB 3.3 / 3.7 / 3.8, security type "None"
// - Raw and Hextile encodings, any true-colour pixel format (8/16/32 bpp) or 8-bit colour map
// - Only changed 16x16 tiles are sent
// - Keyboard -> C64 keyboard; clipboard paste -> typed into the C64
// Transport is abstract so the same code runs on the ESP32 (WiFiClient) and on a PC (sockets).
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include "c64.h"
#include "keymap.h"

struct VncConn {
  virtual ~VncConn() {}
  virtual int read(uint8_t* buf, int n) = 0;            // non-blocking; returns bytes read (0 = none)
  virtual bool write(const uint8_t* buf, int n) = 0;    // blocking; false = connection lost
  virtual bool connected() = 0;
  virtual void close() = 0;
};

class VncServer {
 public:
  VncServer(C64& c, KeyMapper& k) : c64(c), keys(k) {}
  void attach(VncConn* conn);   // takes ownership; replaces any current client
  void poll();                  // call often
  bool active() const { return conn != nullptr; }
  std::string name = "Commodore 64 on Heltec V3";
  std::function<void(const std::string&)> onPaste;
  uint32_t bytesSent = 0;

 private:
  enum State { WAIT_VERSION, WAIT_SECTYPE, WAIT_CLIENTINIT, NORMAL };
  static const int TW = 16, TCOLS = C64_W / 16, TROWS = C64_H / 16;

  C64& c64;
  KeyMapper& keys;
  VncConn* conn = nullptr;
  State state = WAIT_VERSION;
  int minor = 3;
  std::vector<uint8_t> in, out;

  // pixel format
  int bpp = 4;          // bytes per pixel
  bool bigEndian = false, trueColour = true;
  uint32_t pal[16];
  bool hextile = false;

  bool updateRequested = false, fullRequested = false;
  uint32_t lastFrame = 0xFFFFFFFF;
  uint32_t tileHash[TCOLS * TROWS];
  bool tileDirty[TCOLS * TROWS];

  void drop();
  void sendInitial();
  bool handleMessage();
  void setPixelFormat(const uint8_t* pf);
  void sendColourMap();
  void maybeSendUpdate();
  void putPixel(uint8_t idx);
  void encodeTileRaw(int tx, int ty);
  void encodeTileHextile(int tx, int ty);
  void flush(bool force);
  void put8(uint8_t v) { out.push_back(v); }
  void put16(uint16_t v) { out.push_back(v >> 8); out.push_back(v & 0xFF); }
  void put32(uint32_t v) { put16(v >> 16); put16(v & 0xFFFF); }
  uint8_t px(int x, int y) const {
    uint8_t b = c64.fb[y * (C64_W / 2) + (x >> 1)];
    return (x & 1) ? (b >> 4) : (b & 15);
  }
};
