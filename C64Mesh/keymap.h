// Maps browser key events (as X11 keysyms) onto the C64 keyboard matrix and joystick.
// Typing is "symbolic": pressing '"' on a PC sends SHIFT+2 to the C64, etc.
// Numeric keypad 8/2/4/6 (+7/9/1/3 diagonals) and keypad 0/5 or Right-Ctrl = joystick (port 2 by default).
// Keysyms 0xC64000 + row*8 + col press a C64 matrix key directly (used by the on-screen keyboard).
#pragma once
#include <stdint.h>
#include "c64.h"

class KeyMapper {
 public:
  // Queue a raw event (thread-safe enough for 1 producer / 1 consumer)
  void push(uint32_t keysym, bool down);
  // Apply queued events to the C64; call once per emulated frame
  void apply(C64& c64);
  void releaseAll(C64& c64);
  volatile int joyPort = 2;   // which port the keypad / on-screen joystick drives

 private:
  struct Ev { uint32_t sym; uint8_t down; };
  static const int QN = 128;
  Ev q[QN];
  volatile int qHead = 0, qTail = 0;
  int holdFrames = 0;     // frames to wait before processing the next event

  struct Held { uint32_t sym; int8_t row, col; int8_t shift; uint8_t joy; };
  Held held[16];
  int nHeld = 0;
  bool physShiftL = false, physShiftR = false;

  void rebuild(C64& c64);
};
