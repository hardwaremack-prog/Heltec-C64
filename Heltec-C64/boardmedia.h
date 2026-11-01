// Board-side storage for cartridges and floppy images (ESP32 only).
//  - Cartridge: one .crt kept in its own flash partition ("cart"), memory-mapped so the
//    emulator reads it straight from flash; no RAM needed even for 1 MB EasyFlash carts.
//  - Disk images: .d64 files in LittleFS (/d64/NAME.d64), read sector by sector on LOAD.
// The emulator task owns the live state; the web side asks it to change via mediaRequest().
#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include "c64.h"

static const uint32_t CART_DATA_OFS = 0x1000;   // first 4K of the partition = header
struct CartHeader {
  char magic[8];      // "HC64CRT1"
  uint32_t len;
  uint32_t crc;
  char fname[48];
};

static const esp_partition_t* cartPart = nullptr;
static const uint8_t* cartMap = nullptr;
static esp_partition_mmap_handle_t cartMapHandle;
static CartImage cartImg;             // parsed stored cartridge (pointers into cartMap)
static bool cartStored = false;       // a valid cartridge is in the partition
static String cartFileName, cartTypeStr;
static uint32_t cartLen = 0;

static File d64File;
static D64 drive;
static char d64Want[40] = "";         // image the web side wants in the drive ("" = empty)
static char d64Now[40] = "";          // image actually in the drive (written by emulator task)

static volatile int mediaCmd = 0;     // 1 pull cart, 2 insert cart, 3 change disk
static volatile bool mediaDone = true;

static uint32_t crc32Update(uint32_t c, const uint8_t* d, size_t n) {
  c = ~c;
  for (size_t i = 0; i < n; i++) {
    c ^= d[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320 & -(int32_t)(c & 1));
  }
  return ~c;
}

// ---- cartridge partition
static void cartUnmap() {
  if (cartMap) { esp_partition_munmap(cartMapHandle); cartMap = nullptr; }
}

static bool cartMapIn() {
  if (cartMap) return true;
  if (!cartPart) return false;
  return esp_partition_mmap(cartPart, 0, cartPart->size, ESP_PARTITION_MMAP_DATA, (const void**)&cartMap, &cartMapHandle) == ESP_OK;
}

// Validates and parses whatever is stored. Sets cartStored.
static bool cartLoadStored(String& err) {
  cartStored = false;
  if (!cartPart) { err = "No cartridge area in flash (re-flash with Erase ticked)"; return false; }
  if (!cartMapIn()) { err = "Couldn't map the cartridge area"; return false; }
  CartHeader h;
  memcpy(&h, cartMap, sizeof h);
  if (memcmp(h.magic, "HC64CRT1", 8) != 0 || h.len == 0 || h.len > cartPart->size - CART_DATA_OFS) { err = "No cartridge stored"; return false; }
  if (crc32Update(0, cartMap + CART_DATA_OFS, h.len) != h.crc) { err = "Stored cartridge is damaged"; return false; }
  std::string e;
  if (!parseCartridge(cartMap + CART_DATA_OFS, h.len, cartImg, e)) { err = e.c_str(); return false; }
  h.fname[47] = 0;
  cartFileName = h.fname;
  cartTypeStr = cartTypeName(cartImg.type);
  cartLen = h.len;
  cartStored = true;
  return true;
}

// Streaming writer used by the upload handler
static uint32_t cwPos = 0, cwErased = 0, cwCrc = 0;
static bool cwOk = false;
static String cwErr;

static void cartWriteBegin() {
  cartUnmap();
  cwPos = 0; cwErased = 0; cwCrc = 0; cwOk = cartPart != nullptr; cwErr = "";
  if (!cwOk) cwErr = "No cartridge area in flash (re-flash with Erase ticked)";
}

static void cartWriteChunk(const uint8_t* d, size_t n) {
  if (!cwOk) return;
  if (CART_DATA_OFS + cwPos + n > cartPart->size) { cwOk = false; cwErr = "Cartridge is too big (max 1.1 MB)"; return; }
  while (cwErased < CART_DATA_OFS + cwPos + n) {
    if (esp_partition_erase_range(cartPart, cwErased, 0x10000) != ESP_OK) { cwOk = false; cwErr = "Flash erase failed"; return; }
    cwErased += 0x10000;
  }
  if (esp_partition_write(cartPart, CART_DATA_OFS + cwPos, d, n) != ESP_OK) { cwOk = false; cwErr = "Flash write failed"; return; }
  cwCrc = crc32Update(cwCrc, d, n);
  cwPos += n;
}

static bool cartWriteEnd(const String& fname, String& err) {
  if (!cwOk) { err = cwErr; return false; }
  if (cwPos == 0) { err = "Empty file"; return false; }
  CartHeader h;
  memset(&h, 0, sizeof h);
  memcpy(h.magic, "HC64CRT1", 8);
  h.len = cwPos;
  h.crc = cwCrc;
  strncpy(h.fname, fname.c_str(), sizeof h.fname - 1);
  if (esp_partition_write(cartPart, 0, &h, sizeof h) != ESP_OK) { err = "Flash write failed"; return false; }
  if (!cartLoadStored(err)) {   // not a usable cartridge: wipe the header so it isn't offered again
    cartUnmap();
    esp_partition_erase_range(cartPart, 0, 0x1000);
    return false;
  }
  return true;
}

// ---- disk images (called on the emulator task, or in setup() before it starts)
static void d64Close() {
  c64.drive8 = nullptr;
  if (d64File) d64File.close();
  d64Now[0] = 0;
}

static bool d64Open(const char* name) {
  d64Close();
  if (!name[0]) return true;
  d64File = LittleFS.open(String("/d64/") + name + ".d64", "r");
  if (!d64File || !D64::validSize(d64File.size())) { if (d64File) d64File.close(); return false; }
  drive.size = d64File.size();
  drive.readAt = [](uint32_t off, uint8_t* b, size_t n) { return d64File.seek(off) && d64File.read(b, n) == n; };
  c64.drive8 = &drive;
  strncpy(d64Now, name, sizeof d64Now - 1);
  return true;
}

// Runs on the emulator task between frames
static void mediaService(KeyMapper& keys) {
  int m = mediaCmd;
  if (!m) return;
  mediaCmd = 0;
  if (m == 1) { c64.removeCart(); c64.reset(); keys.releaseAll(c64); }
  else if (m == 2) { if (cartStored) c64.insertCart(cartImg); c64.reset(); keys.releaseAll(c64); }
  else if (m == 3) { d64Open(d64Want); }
  mediaDone = true;
}

// Called from the web side: waits (briefly) until the emulator task has done it
static void mediaRequest(int cmd) {
  mediaDone = false;
  mediaCmd = cmd;
  uint32_t t = millis();
  while (!mediaDone && millis() - t < 1000) delay(2);
}

static void listD64(std::vector<std::pair<String, uint32_t>>& out) {
  File dir = LittleFS.open("/d64");
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".d64")) out.push_back({n.substring(0, n.length() - 4), (uint32_t)f.size()});
    f.close();
  }
  std::sort(out.begin(), out.end(), [](const std::pair<String, uint32_t>& a, const std::pair<String, uint32_t>& b) { return a.first < b.first; });
}
