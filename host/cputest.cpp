#include "../Heltec-C64/c64.h"
#include <cstdio>
#include <vector>
int main(int argc, char** argv) {
  std::vector<uint8_t> img(65536);
  FILE* f = fopen(argv[1], "rb"); fread(img.data(), 1, 65536, f); fclose(f);
  static C64 c;
  c.cpuTestSetup(img.data(), 0x0400);
  uint16_t last = 0xFFFF; long n = 0;
  while (true) {
    uint16_t pcb = c.pc;
    c.cpuStep(); n++;
    if (c.pc == pcb) { printf("trapped at %04X after %ld instructions\n", c.pc, n); return 0; }
    if (n > 200000000) { printf("timeout pc=%04X\n", c.pc); return 1; }
    last = pcb;
  }
}
