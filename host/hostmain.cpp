// PC test harness for C64Mesh: same emulator + stream code as the ESP32 build.
//   ./c64host --port 8181             -> WebSocket stream (what the browser page talks to), real-time
//   ./c64host --frames 300 --type "PRINT 2+2\n" --dump out.ppm   (headless)
//   ROM overrides: --basic f --kernal f --chargen f ; virtual disk: --disk dir
#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <thread>
#include <atomic>
#include <algorithm>
#include "../C64Mesh/c64.h"
#include "../C64Mesh/keymap.h"
#include "../C64Mesh/stream.h"
#include "../C64Mesh/builtin_roms.h"

struct SockConn : NetConn {
  int fd;
  bool ok = true;
  explicit SockConn(int f) : fd(f) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
  }
  int read(uint8_t* b, int n) override {
    int r = ::recv(fd, b, n, 0);
    if (r == 0) { ok = false; return 0; }
    if (r < 0) { if (errno != EAGAIN && errno != EWOULDBLOCK) ok = false; return 0; }
    return r;
  }
  bool write(const uint8_t* b, int n) override {
    while (n > 0) {
      int w = ::send(fd, b, n, MSG_NOSIGNAL);
      if (w < 0) {
        if (errno == EAGAIN) { usleep(1000); continue; }
        ok = false; return false;
      }
      b += w; n -= w;
    }
    return true;
  }
  bool connected() override { return ok; }
  void close() override { ::close(fd); }
};

static std::vector<uint8_t> loadFile(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), {});
}

static void dumpPPM(C64& c, const char* path) {
  FILE* f = fopen(path, "wb");
  fprintf(f, "P6 %d %d 255\n", C64_W, C64_H);
  for (int y = 0; y < C64_H; y++)
    for (int x = 0; x < C64_W; x++) {
      uint8_t b = c.fb[y * C64_W / 2 + x / 2];
      uint32_t col = C64_PALETTE[(x & 1) ? b >> 4 : b & 15];
      fputc(col >> 16, f); fputc((col >> 8) & 255, f); fputc(col & 255, f);
    }
  fclose(f);
}

static std::string upper(std::string s) { for (auto& ch : s) ch = toupper(ch); return s; }
static bool wildMatch(const char* p, const char* s) {
  if (*p == '*') return true;
  if (!*p) return !*s;
  if (!*s) return false;
  if (*p == '?' || *p == *s) return wildMatch(p + 1, s + 1);
  return false;
}

int main(int argc, char** argv) {
  std::string basic, kernal, chargen, disk = ".", dump, typed;
  int frames = 0, port = 8181;
  std::string sysPrg, wav;
  std::vector<int16_t> pcm;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto nx = [&]() { return std::string(argv[++i]); };
    if (a == "--basic") basic = nx();
    else if (a == "--kernal") kernal = nx();
    else if (a == "--chargen") chargen = nx();
    else if (a == "--disk") disk = nx();
    else if (a == "--frames") frames = atoi(argv[++i]);
    else if (a == "--dump") dump = nx();
    else if (a == "--type") typed = nx();
    else if (a == "--port") port = atoi(argv[++i]);
    else if (a == "--sys") sysPrg = nx();
    else if (a == "--wav") wav = nx();
  }
  static C64 c64;
  std::vector<uint8_t> b(BUILTIN_BASIC, BUILTIN_BASIC + 8192), k(BUILTIN_KERNAL, BUILTIN_KERNAL + 8192),
      ch(BUILTIN_CHARGEN, BUILTIN_CHARGEN + 4096);
  if (!basic.empty()) b = loadFile(basic);
  if (!kernal.empty()) k = loadFile(kernal);
  if (!chargen.empty()) ch = loadFile(chargen);
  c64.setRoms(b.data(), k.data(), ch.data());

  c64.diskList = [&](std::vector<std::pair<std::string, uint32_t>>& files) {
    DIR* d = opendir(disk.c_str());
    if (!d) return;
    while (dirent* e = readdir(d)) {
      std::string n = e->d_name;
      if (n.size() > 4 && upper(n.substr(n.size() - 4)) == ".PRG") {
        struct stat st;
        stat((disk + "/" + n).c_str(), &st);
        files.push_back({upper(n.substr(0, n.size() - 4)), (uint32_t)st.st_size});
      }
    }
    closedir(d);
    std::sort(files.begin(), files.end());
  };
  c64.diskLoad = [&](const std::string& pat, std::vector<uint8_t>& data) {
    std::vector<std::pair<std::string, uint32_t>> files;
    c64.diskList(files);
    for (auto& f : files)
      if (wildMatch(upper(pat).c_str(), f.first.c_str())) {
        DIR* d = opendir(disk.c_str());
        while (dirent* e = readdir(d)) {
          std::string n = e->d_name;
          if (upper(n) == f.first + ".PRG") { data = loadFile(disk + "/" + n); break; }
        }
        closedir(d);
        return !data.empty();
      }
    return false;
  };
  c64.diskSave = [&](const std::string& name, const std::vector<uint8_t>& data) {
    std::ofstream f(disk + "/" + upper(name) + ".prg", std::ios::binary);
    f.write((const char*)data.data(), data.size());
    return true;
  };
  c64.reset();

  if (frames > 0) {  // headless test
    auto t0 = std::chrono::steady_clock::now();
    bool typedYet = false;
    for (int f = 0; f < frames; f++) {
      if (!typedYet && f >= 150 && !typed.empty()) { c64.typeText(typed.c_str()); typedYet = true; }
      if (f == 150 && !sysPrg.empty()) { auto d = loadFile(sysPrg); c64.injectPrg(d.data(), d.size(), false); c64.pc = d[0] | (d[1] << 8); }
      c64.audioEnabled = !wav.empty();
      c64.runFrame();
      int16_t tmp[1024]; int n;
      while ((n = c64.readAudio(tmp, 1024)) > 0) if (f > 150) pcm.insert(pcm.end(), tmp, tmp + n);
    }
    if (!wav.empty()) {
      FILE* wf = fopen(wav.c_str(), "wb");
      uint32_t ds = pcm.size() * 2, rate = SID_RATE, br = rate * 2, rs = 36 + ds;
      uint16_t one = 1, ba = 2, bits = 16; uint32_t sixteen = 16;
      fwrite("RIFF", 1, 4, wf); fwrite(&rs, 4, 1, wf); fwrite("WAVEfmt ", 1, 8, wf); fwrite(&sixteen, 4, 1, wf);
      fwrite(&one, 2, 1, wf); fwrite(&one, 2, 1, wf); fwrite(&rate, 4, 1, wf); fwrite(&br, 4, 1, wf);
      fwrite(&ba, 2, 1, wf); fwrite(&bits, 2, 1, wf); fwrite("data", 1, 4, wf); fwrite(&ds, 4, 1, wf);
      fwrite(pcm.data(), 2, pcm.size(), wf); fclose(wf);
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("%d frames in %.3fs (%.1fx real time), pc=%04X\n", frames, s, frames * 0.01995 / s, c64.pc);
    if (getenv("DUMPSCR")) for (int r = 0; r < 5; r++) { for (int c2 = 0; c2 < 8; c2++) printf("%02X/%X ", c64.peek(0x400 + r * 40 + c2), c64.colorAt(r * 40 + c2)); printf("\n"); }
    if (!dump.empty()) dumpPPM(c64, dump.c_str());
    return 0;
  }

  KeyMapper keys;
  StreamServer stream(c64, keys);
  stream.onPaste = [&](const std::string& t) { c64.typeText(t.c_str()); };
  int ls = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in sa{};
  sa.sin_family = AF_INET; sa.sin_port = htons(port); sa.sin_addr.s_addr = INADDR_ANY;
  if (bind(ls, (sockaddr*)&sa, sizeof sa) < 0) { perror("bind"); return 1; }
  std::atomic<bool> run{true};
  std::thread emu([&] {
    auto next = std::chrono::steady_clock::now();
    int fno = 0;
    while (run) {
      if (++fno == 150 && !sysPrg.empty()) { auto d = loadFile(sysPrg); c64.injectPrg(d.data(), d.size(), false); c64.pc = d[0] | (d[1] << 8); }
      keys.apply(c64);
      c64.audioEnabled = stream.soundOn();
      c64.runFrame();
      next += std::chrono::microseconds((int)C64_FRAME_US);
      std::this_thread::sleep_until(next);
    }
  });
  listen(ls, 2);
  fcntl(ls, F_SETFL, O_NONBLOCK);
  printf("WebSocket stream on :%d\n", port);
  fflush(stdout);
  while (true) {
    int fd = accept(ls, nullptr, nullptr);
    if (fd >= 0) stream.attach(new SockConn(fd));
    stream.poll();
    usleep(2000);
  }
}
