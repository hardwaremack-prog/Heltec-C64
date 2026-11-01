#include "keymap.h"

// matrix position helper: row (CIA1 PA bit), col (CIA1 PB bit)
#define K(r, c) (int8_t)(r), (int8_t)(c)
enum { SH_PHYS = 0, SH_ON = 1, SH_OFF = 2 };

struct Map { int8_t row, col, shift; };

static const Map LETTERS[26] = {
    {K(1, 2), 0}, {K(3, 4), 0}, {K(2, 4), 0}, {K(2, 2), 0}, {K(1, 6), 0}, {K(2, 5), 0}, {K(3, 2), 0},
    {K(3, 5), 0}, {K(4, 1), 0}, {K(4, 2), 0}, {K(4, 5), 0}, {K(5, 2), 0}, {K(4, 4), 0}, {K(4, 7), 0},
    {K(4, 6), 0}, {K(5, 1), 0}, {K(7, 6), 0}, {K(2, 1), 0}, {K(1, 5), 0}, {K(2, 6), 0}, {K(3, 6), 0},
    {K(3, 7), 0}, {K(1, 1), 0}, {K(2, 7), 0}, {K(3, 1), 0}, {K(1, 4), 0}};
static const Map DIGITS[10] = {{K(4, 3), SH_OFF}, {K(7, 0), SH_OFF}, {K(7, 3), SH_OFF}, {K(1, 0), SH_OFF},
                               {K(1, 3), SH_OFF}, {K(2, 0), SH_OFF}, {K(2, 3), SH_OFF}, {K(3, 0), SH_OFF},
                               {K(3, 3), SH_OFF}, {K(4, 0), SH_OFF}};

static bool lookup(uint32_t s, Map& m, uint8_t& joy, bool& restore) {
  joy = 0; restore = false;
  if (s >= 'a' && s <= 'z') { m = LETTERS[s - 'a']; return true; }
  if (s >= 'A' && s <= 'Z') { m = LETTERS[s - 'A']; return true; }
  if (s >= '0' && s <= '9') { m = DIGITS[s - '0']; return true; }
  switch (s) {
    case ' ': m = {K(7, 4), SH_PHYS}; return true;
    case '!': m = {K(7, 0), SH_ON}; return true;
    case '"': m = {K(7, 3), SH_ON}; return true;
    case '#': m = {K(1, 0), SH_ON}; return true;
    case '$': m = {K(1, 3), SH_ON}; return true;
    case '%': m = {K(2, 0), SH_ON}; return true;
    case '&': m = {K(2, 3), SH_ON}; return true;
    case '\'': m = {K(3, 0), SH_ON}; return true;
    case '(': m = {K(3, 3), SH_ON}; return true;
    case ')': m = {K(4, 0), SH_ON}; return true;
    case '+': m = {K(5, 0), SH_OFF}; return true;
    case '-': m = {K(5, 3), SH_OFF}; return true;
    case '.': m = {K(5, 4), SH_OFF}; return true;
    case ',': m = {K(5, 7), SH_OFF}; return true;
    case ':': m = {K(5, 5), SH_OFF}; return true;
    case ';': m = {K(6, 2), SH_OFF}; return true;
    case '@': m = {K(5, 6), SH_OFF}; return true;
    case '*': m = {K(6, 1), SH_OFF}; return true;
    case '=': m = {K(6, 5), SH_OFF}; return true;
    case '/': m = {K(6, 7), SH_OFF}; return true;
    case '<': m = {K(5, 7), SH_ON}; return true;
    case '>': m = {K(5, 4), SH_ON}; return true;
    case '?': m = {K(6, 7), SH_ON}; return true;
    case '[': m = {K(5, 5), SH_ON}; return true;
    case ']': m = {K(6, 2), SH_ON}; return true;
    case '^': m = {K(6, 6), SH_OFF}; return true;         // up-arrow key
    case '_': case '`': m = {K(7, 1), SH_OFF}; return true;  // left-arrow key
    case '\\': case 0xA3: m = {K(6, 0), SH_OFF}; return true;  // pound key
    case 0xFF0D: case 0xFF8D: m = {K(0, 1), SH_PHYS}; return true;  // Return / KP_Enter
    case 0xFF08: case 0xFFFF: m = {K(0, 0), SH_PHYS}; return true;  // BackSpace/Delete = DEL
    case 0xFF63: m = {K(0, 0), SH_ON}; return true;                 // Insert = INST
    case 0xFF50: m = {K(6, 3), SH_PHYS}; return true;               // Home = CLR/HOME
    case 0xFF53: m = {K(0, 2), SH_OFF}; return true;                // Right
    case 0xFF51: m = {K(0, 2), SH_ON}; return true;                 // Left
    case 0xFF54: m = {K(0, 7), SH_OFF}; return true;                // Down
    case 0xFF52: m = {K(0, 7), SH_ON}; return true;                 // Up
    case 0xFF1B: m = {K(7, 7), SH_PHYS}; return true;               // Esc = RUN/STOP
    case 0xFF09: case 0xFFE3: m = {K(7, 2), SH_PHYS}; return true;  // Tab / Ctrl_L = CTRL
    case 0xFFE9: case 0xFFEA: case 0xFFEB: case 0xFFEC: case 0xFFE7: case 0xFFE8:
      m = {K(7, 5), SH_PHYS}; return true;                          // Alt/Super/Meta = C=
    case 0xFFBE: m = {K(0, 4), SH_OFF}; return true;  // F1
    case 0xFFBF: m = {K(0, 4), SH_ON}; return true;   // F2
    case 0xFFC0: m = {K(0, 5), SH_OFF}; return true;  // F3
    case 0xFFC1: m = {K(0, 5), SH_ON}; return true;   // F4
    case 0xFFC2: m = {K(0, 6), SH_OFF}; return true;  // F5
    case 0xFFC3: m = {K(0, 6), SH_ON}; return true;   // F6
    case 0xFFC4: m = {K(0, 3), SH_OFF}; return true;  // F7
    case 0xFFC5: m = {K(0, 3), SH_ON}; return true;   // F8
    case 0xFF55: restore = true; m = {-1, -1, 0}; return true;  // Page Up = RESTORE
    // ---- keypad joystick (port 2)
    case 0xFFB8: case 0xFF97: joy = 1; break;              // 8 up
    case 0xFFB2: case 0xFF99: joy = 2; break;              // 2 down
    case 0xFFB4: case 0xFF96: joy = 4; break;              // 4 left
    case 0xFFB6: case 0xFF98: joy = 8; break;              // 6 right
    case 0xFFB7: case 0xFF95: joy = 1 | 4; break;          // 7
    case 0xFFB9: case 0xFF9A: joy = 1 | 8; break;          // 9
    case 0xFFB1: case 0xFF9C: joy = 2 | 4; break;          // 1
    case 0xFFB3: case 0xFF9B: joy = 2 | 8; break;          // 3
    case 0xFFB0: case 0xFF9E: case 0xFFB5: case 0xFF9D: case 0xFFE4: joy = 16; break;  // 0/5/RCtrl fire
    default: return false;
  }
  m = {-1, -1, 0};
  return true;
}

void KeyMapper::push(uint32_t sym, bool down) {
  int n = (qHead + 1) % QN;
  if (n == qTail) return;  // full: drop
  q[qHead].sym = sym;
  q[qHead].down = down;
  qHead = n;
}

void KeyMapper::releaseAll(C64& c) {
  nHeld = 0;
  physShiftL = physShiftR = false;
  rebuild(c);
}

void KeyMapper::apply(C64& c) {
  if (holdFrames > 0) { holdFrames--; return; }
  while (qTail != qHead) {
    Ev e = q[qTail];
    qTail = (qTail + 1) % QN;
    if (e.sym == 0xFFFFFFFE) {  // client disconnected
      nHeld = 0; physShiftL = physShiftR = false;
      rebuild(c);
      continue;
    }
    if (e.sym == 0xFFE1 || e.sym == 0xFFE2) {  // physical shifts: no delay
      if (e.sym == 0xFFE1) physShiftL = e.down; else physShiftR = e.down;
      rebuild(c);
      continue;
    }
    Map m; uint8_t joy; bool restore;
    if (!lookup(e.sym, m, joy, restore)) continue;
    if (restore) { if (e.down) c.restoreKey(); continue; }
    // remove any existing entry for this sym
    for (int i = 0; i < nHeld; i++)
      if (held[i].sym == e.sym) { held[i] = held[--nHeld]; i--; }
    if (e.down && nHeld < 16) held[nHeld++] = {e.sym, m.row, m.col, m.shift, joy};
    rebuild(c);
    // Hold presses for a few frames so the KERNAL's 60 Hz scan always sees them
    if (!joy) { holdFrames = e.down ? 2 : 1; return; }
  }
}

void KeyMapper::rebuild(C64& c) {
  uint8_t rows[8] = {0};
  uint8_t joy = 0;
  int forced = SH_PHYS;
  for (int i = 0; i < nHeld; i++) {
    const Held& h = held[i];
    joy |= h.joy;
    if (h.row >= 0) {
      rows[h.row] |= 1 << h.col;
      if (h.shift != SH_PHYS) forced = h.shift;
    }
  }
  if (forced == SH_ON) rows[1] |= 0x80;
  else if (forced == SH_PHYS) {
    if (physShiftL) rows[1] |= 0x80;
    if (physShiftR) rows[6] |= 0x10;
  }
  // joystick: up+down or left+right never both
  memcpy(c.keyRow, rows, 8);
  c.joy2 = joy;
}
