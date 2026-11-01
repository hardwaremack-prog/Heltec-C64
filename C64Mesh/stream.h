// Streams the C64 to a web browser over a WebSocket (port 81):
// screen as changed 16x16 tiles, SID sound as 16-bit PCM, keys/paste back from the browser.
// Transport is abstract so the same code runs on the ESP32 (WiFiClient) and on a PC (sockets).
//
// Server -> browser (binary messages)
//   0x00 hello      : w u16, h u16, sampleRate u32                     (little endian)
//   0x01 tiles      : count u16, then per tile: tx u8, ty u8, mode u8, data
//                     mode 0 solid: colour u8
//                     mode 1 two-colour: c0|c1<<4, 32 bytes bitmap (16 rows x 2 bytes, MSB = left, 1 = c1)
//                     mode 2 raw: 128 bytes, 2 pixels per byte, left pixel in low nibble
//   0x02 audio      : int16 samples
//   0x03 frame end  : (browser draws, then sends 0x13 ack)
//   0x04 info       : utf-8 text (e.g. "Another viewer took over")
// Browser -> server
//   0x10 key        : down u8, keysym u32 (X11 keysyms; 0xC64000+row*8+col = raw C64 matrix key)
//   0x11 paste      : utf-8 text to type
//   0x12 refresh    : resend whole screen
//   0x13 frame ack
//   0x14 sound      : on u8
//   0x15 joystick   : port u8 (1 or 2)
#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <functional>
#include "c64.h"
#include "keymap.h"

struct NetConn {
  virtual ~NetConn() {}
  virtual int read(uint8_t* buf, int n) = 0;          // non-blocking; bytes read (0 = none)
  virtual bool write(const uint8_t* buf, int n) = 0;  // blocking; false = connection lost
  virtual bool connected() = 0;
  virtual void close() = 0;
};

class StreamServer {
 public:
  StreamServer(C64& c, KeyMapper& k) : c64(c), keys(k) {}
  void attach(NetConn* conn);   // takes ownership; replaces any current viewer
  void poll();                  // call often (every ~1-5 ms)
  bool active() const { return conn != nullptr && state == OPEN; }
  bool soundOn() const { return active() && sound; }
  std::function<void(const std::string&)> onPaste;
  uint32_t bytesSent = 0;

 private:
  enum State { HANDSHAKE, OPEN };
  static const int TCOLS = C64_W / 16, TROWS = C64_H / 16;
  C64& c64;
  KeyMapper& keys;
  NetConn* conn = nullptr;
  State state = HANDSHAKE;
  std::vector<uint8_t> in, out;
  bool sound = false;
  bool needFull = true;
  int inFlight = 0;            // frames sent but not yet acknowledged
  uint32_t lastFrame = 0xFFFFFFFF;
  uint32_t lastSentMs = 0;
  uint32_t tileHash[TCOLS * TROWS];

  void drop();
  bool doHandshake();
  bool readFrame();
  void handleMessage(const uint8_t* m, size_t n);
  void sendWs(const uint8_t* data, size_t n);
  void sendVideo();
  void sendAudio();
};

// helpers shared with tests
std::string wsAcceptKey(const std::string& clientKey);
