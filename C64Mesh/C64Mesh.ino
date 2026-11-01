/*
  C64 Mesh  -  a Commodore 64 on a Heltec WiFi LoRa 32 V3 (ESP32-S3), played in a web browser.

  - Emulator (6502 + VIC-II + CIA + SID sound) runs on CPU core 1
  - Wi-Fi, web pages (port 80), screen+sound stream (WebSocket, port 81) and the OLED run on core 0
  - Open http://<board-ip>/ (or http://c64.local/) in any browser: screen, sound, keyboard, joystick
  - ROMs: built-in Open ROMs, or your own BASIC/KERNAL/CHARGEN uploaded from the web page
  - Virtual disk on flash: LOAD"$",8 / LOAD"NAME",8,1 / SAVE"NAME",8
  - OLED shows the IP address; PRG button: tap = switch OLED view, hold 2 s = reset the C64

  Board: "Heltec WiFi LoRa 32(V3)" (esp32 Arduino core 3.x). partitions.csv in this folder
  gives a 3 MB app + ~4.8 MB of flash storage.
*/
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <Wire.h>
#include <vector>
#include "c64.h"
#include "keymap.h"
#include "stream.h"
#include "builtin_roms.h"
#include "webui.h"
#include "player.h"

// ---------------- Heltec V3 pins
#define PIN_VEXT 36      // LOW = power to OLED
#define PIN_OLED_RST 21
#define PIN_OLED_SDA 17
#define PIN_OLED_SCL 18
#define PIN_LED 35
#define PIN_BUTTON 0     // PRG button

static const char* AP_SSID = "C64-Setup";
static const char* AP_PASS = "commodore64";
static const char* HOSTNAME = "c64";

static C64 c64;
static KeyMapper keys;
static StreamServer stream(c64, keys);
static WebServer web(80);
static WiFiServer streamListener(81);
static Preferences prefs;

static bool apMode = false;
static volatile bool warp = false;
static volatile int speedPct = 0;
static volatile int oledPage = 0;

// ---------------- commands from core 0 to the emulator task
static portMUX_TYPE cmdMux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool cmdReset = false, cmdRestore = false;
static std::vector<uint8_t> cmdPrg;
static bool cmdPrgRun = false, cmdPrgPending = false;

// ---------------- ROM slots
struct RomSlot {
  const char* key;
  const char* path;
  size_t size;
  const uint8_t* builtin;
  bool custom;   // a valid file is stored
  bool use;      // boot from the file
  uint32_t crc;
  String label;
};
static RomSlot slots[3] = {
    {"basic", "/roms/basic.bin", 8192, BUILTIN_BASIC, false, false, 0, ""},
    {"kernal", "/roms/kernal.bin", 8192, BUILTIN_KERNAL, false, false, 0, ""},
    {"chargen", "/roms/chargen.bin", 4096, BUILTIN_CHARGEN, false, false, 0, ""},
};
static bool romsPending = false;
static String romName = BUILTIN_ROM_NAME;

static uint32_t crc32(const uint8_t* d, size_t n) {
  uint32_t c = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(int32_t)(c & 1));
  }
  return ~c;
}

static String romLabel(int slot, uint32_t crc) {
  struct { uint32_t crc; const char* name; } known[] = {
      {0xF833D117, "BASIC V2 901226-01"},
      {0xDCE782FA, "KERNAL 901227-01"},
      {0xA5C687B3, "KERNAL 901227-02"},
      {0xDBE3E7C7, "KERNAL 901227-03"},
      {0xEC4272EE, "CHARGEN 901225-01"},
  };
  for (auto& k : known) if (k.crc == crc) return k.name;
  return slot == 1 ? "Custom KERNAL" : slot == 0 ? "Custom BASIC" : "Custom charset";
}

static void scanRomFiles() {
  for (int i = 0; i < 3; i++) {
    RomSlot& s = slots[i];
    s.custom = false;
    File f = LittleFS.open(s.path, "r");
    if (f && f.size() == s.size) {
      std::vector<uint8_t> buf(s.size);
      f.read(buf.data(), s.size);
      s.crc = crc32(buf.data(), s.size);
      s.label = romLabel(i, s.crc);
      s.custom = true;
    }
    if (f) f.close();
    s.use = prefs.getBool((String("use_") + s.key).c_str(), false) && s.custom;
  }
}

// Load the selected ROMs into the emulator (call only from the emulator task or before it starts)
static void applyRoms() {
  uint8_t* b = (uint8_t*)malloc(8192 + 8192 + 4096);
  uint8_t* ptr[3] = {b, b + 8192, b + 16384};
  int nCustom = 0;
  for (int i = 0; i < 3; i++) {
    RomSlot& s = slots[i];
    bool ok = false;
    if (s.use) {
      File f = LittleFS.open(s.path, "r");
      if (f && f.size() == s.size) ok = f.read(ptr[i], s.size) == s.size;
      if (f) f.close();
    }
    if (ok) nCustom++;
    else memcpy(ptr[i], s.builtin, s.size);
  }
  c64.setRoms(ptr[0], ptr[1], ptr[2]);
  free(b);
  romName = nCustom == 0 ? String(BUILTIN_ROM_NAME) : nCustom == 3 ? String("Your ROMs (") + slots[1].label + ")" : String("Mixed: your ROMs + Open ROMs");
}

// ---------------- virtual disk
static String diskName(String n) {
  if (n.endsWith(".prg") || n.endsWith(".PRG")) n = n.substring(0, n.length() - 4);
  n.toUpperCase();
  String o;
  for (char ch : n) if (isalnum((unsigned char)ch) || strchr(" -_.!+", ch)) o += ch;
  o.trim();
  if (o.length() > 16) o = o.substring(0, 16);
  return o;
}

static void listDisk(std::vector<std::pair<std::string, uint32_t>>& files) {
  File dir = LittleFS.open("/disk");
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".prg")) files.push_back({n.substring(0, n.length() - 4).c_str(), (uint32_t)f.size()});
    f.close();
  }
  std::sort(files.begin(), files.end());
}

static bool wildMatch(const char* p, const char* s) {
  if (*p == '*') return true;
  if (!*p) return !*s;
  if (!*s) return false;
  if (*p == '?' || *p == *s) return wildMatch(p + 1, s + 1);
  return false;
}

static bool readDiskFile(const String& name, std::vector<uint8_t>& data) {
  File f = LittleFS.open("/disk/" + name + ".prg", "r");
  if (!f) return false;
  data.resize(f.size());
  f.read(data.data(), data.size());
  f.close();
  return true;
}

// ---------------- OLED (SSD1306 128x64), drawn with the C64's own character set
struct Oled {
  uint8_t buf[1024];
  bool ok = false;
  void cmd(uint8_t c) { Wire.beginTransmission(0x3C); Wire.write(0x00); Wire.write(c); Wire.endTransmission(); }
  void begin() {
    pinMode(PIN_VEXT, OUTPUT); digitalWrite(PIN_VEXT, LOW);
    delay(50);
    pinMode(PIN_OLED_RST, OUTPUT);
    digitalWrite(PIN_OLED_RST, LOW); delay(20); digitalWrite(PIN_OLED_RST, HIGH); delay(20);
    Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL, 400000);
    Wire.beginTransmission(0x3C);
    ok = Wire.endTransmission() == 0;
    if (!ok) return;
    const uint8_t init[] = {0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8,
                            0xDA, 0x12, 0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF};
    for (uint8_t c : init) cmd(c);
    clear(); show();
  }
  void clear() { memset(buf, 0, sizeof buf); }
  void pset(int x, int y) { if (x >= 0 && x < 128 && y >= 0 && y < 64) buf[(y >> 3) * 128 + x] |= 1 << (y & 7); }
  void text(int col, int row, const char* s, bool inv = false) {
    const uint8_t* cs = c64.charset();
    for (; *s && col < 16; s++, col++) {
      int ch = (unsigned char)*s, sc;
      if (ch >= '@' && ch <= 'Z') sc = ch - '@';
      else if (ch >= 'a' && ch <= 'z') sc = ch - 'a' + 1;
      else if (ch >= 0x20 && ch <= 0x3F) sc = ch;
      else sc = '?';
      if (inv) sc |= 0x80;
      for (int y = 0; y < 8; y++) {
        uint8_t bits = cs[sc * 8 + y];
        for (int x = 0; x < 8; x++) if (bits & (0x80 >> x)) pset(col * 8 + x, row * 8 + y);
      }
    }
  }
  void show() {
    if (!ok) return;
    cmd(0x21); cmd(0); cmd(127); cmd(0x22); cmd(0); cmd(7);
    for (int i = 0; i < 1024; i += 16) {
      Wire.beginTransmission(0x3C);
      Wire.write(0x40);
      Wire.write(buf + i, 16);
      Wire.endTransmission();
    }
  }
} oled;

static void drawOled() {
  oled.clear();
  if (oledPage == 1) {  // miniature C64 screen: pixels that differ from the background colour
    uint8_t bg = c64.vicReg(0x21) & 15;
    for (int oy = 0; oy < 64; oy++) {
      int y0 = 36 + oy * 200 / 64, y1 = 36 + (oy + 1) * 200 / 64;
      for (int ox = 0; ox < 128; ox++) {
        int x0 = 32 + ox * 320 / 128, x1 = 32 + (ox + 1) * 320 / 128;
        bool on = false;
        for (int y = y0; y < y1 && !on; y++)
          for (int x = x0; x < x1; x++) {
            uint8_t b = c64.fb[y * (C64_W / 2) + (x >> 1)];
            if (((x & 1) ? b >> 4 : b & 15) != bg) { on = true; break; }
          }
        if (on) oled.pset(ox, oy);
      }
    }
  } else {
    char line[24];
    oled.text(0, 0, " **C64  MESH** ", true);
    if (apMode) {
      oled.text(0, 2, "WIFI SETUP:");
      oled.text(0, 3, AP_SSID);
      oled.text(0, 4, "PW:COMMODORE64");
      oled.text(0, 5, "192.168.4.1");
    } else {
      oled.text(0, 2, "OPEN IN BROWSER:");
      oled.text(0, 3, WiFi.localIP().toString().c_str());
      oled.text(0, 4, WiFi.status() == WL_CONNECTED ? "OR C64.LOCAL" : "WIFI LOST...");
    }
    snprintf(line, sizeof line, "%s %d%%", stream.active() ? (stream.soundOn() ? "PLAYING+SND" : "PLAYING") : "NO VIEWER", speedPct);
    oled.text(0, 6, line);
    oled.text(0, 7, slots[1].use ? "ROMS: YOURS" : "ROMS: OPEN");
  }
  oled.show();
}

// ---------------- stream transport over WiFiClient
struct WifiConn : NetConn {
  WiFiClient c;
  explicit WifiConn(WiFiClient cl) : c(cl) { c.setNoDelay(true); }
  int read(uint8_t* b, int n) override {
    int a = c.available();
    if (a <= 0) return 0;
    return c.read(b, a < n ? a : n);
  }
  bool write(const uint8_t* b, int n) override {
    int idle = 0;
    while (n > 0) {
      size_t w = c.write(b, n);
      if (w == 0) {
        if (!c.connected() || ++idle > 3000) return false;
        delay(1);
        continue;
      }
      idle = 0; b += w; n -= w;
    }
    return true;
  }
  bool connected() override { return c.connected(); }
  void close() override { c.stop(); }
};

// ---------------- emulator task (core 1)
static void emuTask(void*) {
  int64_t next = esp_timer_get_time();
  int renderEvery = 1;
  uint32_t fc = 0;
  for (;;) {
    if (cmdReset) { cmdReset = false; applyRoms(); c64.reset(); keys.releaseAll(c64); }
    if (cmdRestore) { cmdRestore = false; c64.restoreKey(); }
    if (cmdPrgPending) {
      std::vector<uint8_t> prg;
      bool run;
      portENTER_CRITICAL(&cmdMux);
      prg.swap(cmdPrg); run = cmdPrgRun; cmdPrgPending = false;
      portEXIT_CRITICAL(&cmdMux);
      c64.injectPrg(prg.data(), prg.size(), run);
    }
    keys.apply(c64);
    bool want = stream.active() || oledPage == 1;
    c64.audioEnabled = stream.soundOn();
    c64.renderEnabled = want && (fc % renderEvery) == 0;
    c64.runFrame();
    fc++;
    int64_t now = esp_timer_get_time();
    next += (int64_t)C64_FRAME_US;
    if (warp) next = now;
    if (now > next + 20000 && renderEvery < 4) renderEvery++;           // falling behind: draw fewer frames
    else if (now + 8000 < next && renderEvery > 1 && (fc & 63) == 0) renderEvery--;
    if (now > next + 200000) next = now;  // way behind: don't try to catch up
    while ((now = esp_timer_get_time()) < next) {
      if (next - now > 1500) vTaskDelay(1);
      else taskYIELD();
    }
  }
}

// ---------------- web API
static String jsonEsc(const String& s) {
  String o;
  for (char ch : s) {
    if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
    else if ((uint8_t)ch < 0x20) o += ' ';
    else o += ch;
  }
  return o;
}

static std::vector<uint8_t> upBuf;
static String upName;
static bool upTooBig = false;

static void handleUpload() {
  HTTPUpload& u = web.upload();
  if (u.status == UPLOAD_FILE_START) {
    upBuf.clear(); upTooBig = false; upName = u.filename;
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (upBuf.size() + u.currentSize > 65538) upTooBig = true;
    else upBuf.insert(upBuf.end(), u.buf, u.buf + u.currentSize);
  }
}

static void queuePrg(const std::vector<uint8_t>& d, bool run) {
  portENTER_CRITICAL(&cmdMux);
  cmdPrg = d; cmdPrgRun = run; cmdPrgPending = true;
  portEXIT_CRITICAL(&cmdMux);
}

static void setupWeb() {
  web.on("/", HTTP_GET, [] { web.send_P(200, "text/html", PLAYER_HTML); });
  web.on("/settings", HTTP_GET, [] { web.send_P(200, "text/html", SETTINGS_HTML); });

  web.on("/api/status", HTTP_GET, [] {
    String j = "{";
    j += "\"ip\":\"" + (apMode ? String("192.168.4.1") : WiFi.localIP().toString()) + "\",";
    j += "\"mode\":\"" + String(apMode ? "ap" : "sta") + "\",";
    j += "\"ssid\":\"" + jsonEsc(WiFi.SSID()) + "\",\"rssi\":" + String(WiFi.RSSI()) + ",";
    j += "\"viewer\":" + String(stream.active() ? "true" : "false") + ",\"sound\":" + String(stream.soundOn() ? "true" : "false") + ",";
    j += "\"speed\":" + String(speedPct) + ",\"warp\":" + String(warp ? "true" : "false") + ",";
    j += "\"romName\":\"" + jsonEsc(romName) + "\",\"romsPending\":" + String(romsPending ? "true" : "false") + ",\"roms\":{";
    for (int i = 0; i < 3; i++) {
      char crc[12];
      snprintf(crc, sizeof crc, "%08X", (unsigned)slots[i].crc);
      j += String(i ? "," : "") + "\"" + slots[i].key + "\":{\"custom\":" + (slots[i].custom ? "true" : "false") +
           ",\"use\":" + (slots[i].use ? "1" : "0") + ",\"crc\":\"" + crc + "\",\"label\":\"" + jsonEsc(slots[i].label) + "\"}";
    }
    j += "},\"disk\":[";
    std::vector<std::pair<std::string, uint32_t>> files;
    listDisk(files);
    for (size_t i = 0; i < files.size(); i++)
      j += String(i ? "," : "") + "{\"n\":\"" + jsonEsc(files[i].first.c_str()) + "\",\"s\":" + String(files[i].second) + "}";
    j += "],\"fsfree\":" + String((uint32_t)(LittleFS.totalBytes() - LittleFS.usedBytes()));
    j += ",\"heap\":" + String(ESP.getFreeHeap()) + "}";
    web.send(200, "application/json", j);
  });

  web.on("/api/reset", HTTP_POST, [] { cmdReset = true; web.send(200, "text/plain", "ok"); });
  web.on("/api/restore", HTTP_POST, [] { cmdRestore = true; web.send(200, "text/plain", "ok"); });
  web.on("/api/warp", HTTP_POST, [] { warp = web.arg("on") == "1"; web.send(200, "text/plain", "ok"); });
  web.on("/api/type", HTTP_POST, [] { c64.typeText(web.arg("plain").c_str()); web.send(200, "text/plain", "ok"); });

  // ---- ROMs
  web.on("/api/rom", HTTP_POST, [] {
    String slot = web.arg("slot");
    int idx = slot == "basic" ? 0 : slot == "kernal" ? 1 : slot == "chargen" ? 2 : -1;
    if (idx < 0 || upTooBig) { web.send(400, "text/plain", "Bad slot or file too big"); return; }
    if (upBuf.size() == 16384 && idx < 2) {  // BASIC+KERNAL combo file
      for (int i = 0; i < 2; i++) {
        File f = LittleFS.open(slots[i].path, "w");
        f.write(upBuf.data() + i * 8192, 8192); f.close();
        prefs.putBool((String("use_") + slots[i].key).c_str(), true);
      }
    } else if (upBuf.size() == slots[idx].size + 2 && idx < 3) {  // some dumps carry a 2-byte load address
      File f = LittleFS.open(slots[idx].path, "w");
      f.write(upBuf.data() + 2, slots[idx].size); f.close();
      prefs.putBool((String("use_") + slots[idx].key).c_str(), true);
    } else if (upBuf.size() == slots[idx].size) {
      File f = LittleFS.open(slots[idx].path, "w");
      f.write(upBuf.data(), upBuf.size()); f.close();
      prefs.putBool((String("use_") + slots[idx].key).c_str(), true);
    } else {
      web.send(400, "text/plain", "Wrong size: " + slot + " must be " + String(slots[idx].size) + " bytes, got " + String(upBuf.size()));
      return;
    }
    upBuf.clear();
    scanRomFiles();
    romsPending = true;
    web.send(200, "text/plain", "ok");
  }, handleUpload);

  web.on("/api/romdel", HTTP_POST, [] {
    for (auto& s : slots)
      if (web.arg("slot") == s.key) { LittleFS.remove(s.path); prefs.putBool((String("use_") + s.key).c_str(), false); }
    scanRomFiles();
    romsPending = true;
    web.send(200, "text/plain", "ok");
  });

  web.on("/api/romsel", HTTP_POST, [] {
    for (auto& s : slots) prefs.putBool((String("use_") + s.key).c_str(), web.arg(s.key) == "1");
    scanRomFiles();
    romsPending = false;
    cmdReset = true;  // emulator task reloads ROMs and resets
    web.send(200, "text/plain", "ok");
  });

  // ---- virtual disk
  web.on("/api/prg", HTTP_POST, [] {
    if (upTooBig || upBuf.size() < 3) { web.send(400, "text/plain", "Not a valid .PRG"); return; }
    String n = diskName(upName);
    if (!n.length()) n = "PROGRAM";
    File f = LittleFS.open("/disk/" + n + ".prg", "w");
    if (!f) { web.send(500, "text/plain", "Flash full?"); return; }
    f.write(upBuf.data(), upBuf.size()); f.close();
    if (web.arg("run") == "1") queuePrg(upBuf, true);
    upBuf.clear();
    web.send(200, "text/plain", "ok");
  }, handleUpload);

  web.on("/api/run", HTTP_POST, [] {
    std::vector<uint8_t> d;
    if (!readDiskFile(diskName(web.arg("name")), d)) { web.send(404, "text/plain", "Not found"); return; }
    queuePrg(d, true);
    web.send(200, "text/plain", "ok");
  });

  web.on("/api/diskdel", HTTP_POST, [] {
    LittleFS.remove("/disk/" + diskName(web.arg("name")) + ".prg");
    web.send(200, "text/plain", "ok");
  });

  web.onNotFound([] {
    String u = web.uri();
    if (u.startsWith("/api/disk/")) {
      String n = diskName(web.urlDecode(u.substring(10)));
      File f = LittleFS.open("/disk/" + n + ".prg", "r");
      if (f) {
        web.sendHeader("Content-Disposition", "attachment; filename=\"" + n + ".prg\"");
        web.streamFile(f, "application/octet-stream");
        f.close();
        return;
      }
    }
    if (apMode) { web.sendHeader("Location", "http://192.168.4.1/"); web.send(302); return; }  // captive-ish
    web.send(404, "text/plain", "Not found");
  });

  // ---- Wi-Fi
  web.on("/api/scan", HTTP_GET, [] {
    int n = WiFi.scanNetworks();
    String j = "[";
    for (int i = 0; i < n; i++) j += String(i ? "," : "") + "{\"s\":\"" + jsonEsc(WiFi.SSID(i)) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
    j += "]";
    WiFi.scanDelete();
    web.send(200, "application/json", j);
  });
  web.on("/api/wifi", HTTP_POST, [] {
    prefs.putString("ssid", web.arg("ssid"));
    prefs.putString("pass", web.arg("pass"));
    web.send(200, "text/plain", "ok");
    delay(600);
    ESP.restart();
  });
  web.begin();
}

// ---------------- network task (core 0)
static void netTask(void*) {
  uint32_t lastOled = 0, lastSpeed = millis(), lastFrames = c64.frameCount;
  uint32_t btnDown = 0;
  bool btnHeld = false;
  for (;;) {
    web.handleClient();
    WiFiClient nc = streamListener.accept();
    if (nc) stream.attach(new WifiConn(nc));
    stream.poll();
    digitalWrite(PIN_LED, stream.active() ? HIGH : LOW);

    uint32_t now = millis();
    if (now - lastSpeed >= 1000) {
      uint32_t f = c64.frameCount;
      speedPct = (int)((f - lastFrames) * 100000.0 / (now - lastSpeed) / 50.125 + 0.5);
      lastFrames = f; lastSpeed = now;
    }
    // PRG button: tap = OLED view, hold 2 s = reset C64
    bool down = digitalRead(PIN_BUTTON) == LOW;
    if (down && !btnDown) { btnDown = now ? now : 1; btnHeld = false; }
    if (down && btnDown && !btnHeld && now - btnDown > 2000) { btnHeld = true; cmdReset = true; }
    if (!down && btnDown) {
      if (!btnHeld && now - btnDown > 40) oledPage ^= 1;
      btnDown = 0;
      lastOled = 0;
    }
    if (now - lastOled > (oledPage ? 150u : 1000u)) { drawOled(); lastOled = now; }
    delay(1);
  }
}

// ---------------- setup
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  if (!LittleFS.begin(true)) Serial.println("LittleFS mount failed");
  LittleFS.mkdir("/roms");
  LittleFS.mkdir("/disk");
  prefs.begin("c64mesh", false);

  // ROMs + emulator
  scanRomFiles();
  applyRoms();
  c64.diskList = listDisk;
  c64.diskLoad = [](const std::string& pat, std::vector<uint8_t>& data) {
    std::vector<std::pair<std::string, uint32_t>> files;
    listDisk(files);
    String p = String(pat.c_str());
    p.toUpperCase();
    for (auto& f : files)
      if (wildMatch(p.c_str(), f.first.c_str())) return readDiskFile(f.first.c_str(), data);
    return false;
  };
  c64.diskSave = [](const std::string& name, const std::vector<uint8_t>& data) {
    String n = diskName(String(name.c_str()));
    if (!n.length()) return false;
    File f = LittleFS.open("/disk/" + n + ".prg", "w");
    if (!f) return false;
    bool ok = f.write(data.data(), data.size()) == data.size();
    f.close();
    return ok;
  };
  c64.reset();

  oled.begin();
  oled.clear(); oled.text(0, 0, " **C64  MESH** ", true); oled.text(0, 3, "JOINING WIFI..."); oled.show();

  // Wi-Fi: saved network, else hotspot
  String ssid = prefs.getString("ssid", ""), pass = prefs.getString("pass", "");
  WiFi.setHostname(HOSTNAME);
  if (ssid.length()) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid.c_str(), pass.c_str());
    for (int i = 0; i < 150 && WiFi.status() != WL_CONNECTED; i++) delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    apMode = true;
    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, AP_PASS);
  } else {
    WiFi.setAutoReconnect(true);
  }
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }
  Serial.printf("C64 Mesh ready: http://%s/\n", apMode ? "192.168.4.1" : WiFi.localIP().toString().c_str());

  stream.onPaste = [](const std::string& t) { c64.typeText(t.c_str()); };
  streamListener.begin();
  streamListener.setNoDelay(true);
  setupWeb();

  xTaskCreatePinnedToCore(emuTask, "c64", 8192, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 2, nullptr, 0);
}

void loop() { vTaskDelete(nullptr); }
