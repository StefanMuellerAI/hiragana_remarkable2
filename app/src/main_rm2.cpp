// reMarkable 2 entry point: talks to the rm2fb display server (rM2-stuff,
// swtcon mode) over its UNIX socket and reads pen/touch/power input.
#include "app.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <csignal>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>

namespace {

// --- rm2fb wire protocol (libs/rm2fb/include/rm2fb/Message.h) -------------
// Each message is an int32 variant index followed by the raw struct.
constexpr const char* kSocketPath = "/var/run/rm2fb.sock";
constexpr int32_t kMsgInit = 0;
constexpr int32_t kMsgUpdate = 2;
constexpr int32_t kMsgOpenInput = 4;

struct FbFormat {
  int32_t width = gfx::kWidth;
  int32_t height = gfx::kHeight;
  int32_t pixelFormat = 0; // RGB565
};
struct InitMsg {
  bool ownSwtcon = false;
  FbFormat format;
};
struct UpdateParams {
  int32_t y1, x1, y2, x2; // inclusive
  int32_t flags;
  int32_t waveform;
  float temperatureOverride;
  int32_t extraMode;
};
struct OpenInputMsg {
  char path[64] = {};
  int32_t flags = 0;
};
static_assert(sizeof(InitMsg) == 16, "Init layout");
static_assert(sizeof(UpdateParams) == 32, "UpdateParams layout");
static_assert(sizeof(OpenInputMsg) == 68, "OpenInputDevice layout");

constexpr int32_t kIoctlWaveformFlag = 0xf000;
constexpr int32_t kWaveDU = 1, kWaveGC16 = 2, kWaveGL16 = 3;
constexpr int32_t kFlagSync = 1, kFlagFastDraw = 2;

// Raw evdev event as the 32-bit kernel writes it.
struct RawInputEvent {
  uint32_t sec, usec;
  uint16_t type, code;
  int32_t value;
};
static_assert(sizeof(RawInputEvent) == 16, "input_event layout");
constexpr uint16_t EV_SYN_ = 0, EV_KEY_ = 1, EV_ABS_ = 3;
constexpr uint16_t SYN_REPORT_ = 0;
constexpr uint16_t ABS_X_ = 0x00, ABS_Y_ = 0x01;
constexpr uint16_t ABS_MT_SLOT_ = 0x2f, ABS_MT_POSITION_X_ = 0x35,
                   ABS_MT_POSITION_Y_ = 0x36, ABS_MT_TRACKING_ID_ = 0x39;
constexpr uint16_t BTN_TOUCH_ = 0x14a, BTN_TOOL_PEN_ = 0x140, BTN_TOOL_RUBBER_ = 0x141;
constexpr uint16_t KEY_POWER_ = 116;

constexpr float kWacomMaxX = 20967.f, kWacomMaxY = 15725.f;

bool
writeAll(int fd, const void* data, size_t n) {
  const auto* p = static_cast<const uint8_t*>(data);
  while (n > 0) {
    const ssize_t w = ::write(fd, p, n);
    if (w < 0 && errno == EINTR) {
      continue;
    }
    if (w <= 0) {
      return false;
    }
    p += w;
    n -= size_t(w);
  }
  return true;
}

bool
readAll(int fd, void* data, size_t n) {
  auto* p = static_cast<uint8_t*>(data);
  while (n > 0) {
    const ssize_t r = ::read(fd, p, n);
    if (r < 0 && errno == EINTR) {
      continue;
    }
    if (r <= 0) {
      return false;
    }
    p += r;
    n -= size_t(r);
  }
  return true;
}

void
run(const char* cmd) {
  if (std::system(cmd) != 0) {
    std::fprintf(stderr, "kana-dojo: '%s' failed\n", cmd);
  }
}

// Lets the integration test point the app at fake devices.
std::string
envOr(const char* name, const std::string& fallback) {
  const char* v = std::getenv(name);
  return v != nullptr ? std::string(v) : fallback;
}

int
recvFd(int sock) {
  char data;
  iovec iov{ &data, 1 };
  char control[CMSG_SPACE(sizeof(int))];
  msghdr msg{};
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control;
  msg.msg_controllen = sizeof(control);
  if (recvmsg(sock, &msg, 0) <= 0) {
    return -1;
  }
  cmsghdr* c = CMSG_FIRSTHDR(&msg);
  if (c == nullptr || c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) {
    return -1;
  }
  int fd;
  std::memcpy(&fd, CMSG_DATA(c), sizeof(fd));
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  return fd;
}

class Display {
public:
  bool connect() {
    for (int attempt = 0; attempt < 50; attempt++) {
      sock_ = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
      sockaddr_un addr{};
      addr.sun_family = AF_UNIX;
      const std::string path = envOr("KANA_DOJO_SOCKET", kSocketPath);
      std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
      if (::connect(sock_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        return init();
      }
      close(sock_);
      sock_ = -1;
      usleep(200 * 1000); // display server may still be starting
    }
    std::fprintf(stderr, "kana-dojo: cannot connect to %s\n", kSocketPath);
    return false;
  }

  void push(const gfx::Canvas& c, gfx::Rect r, app::Refresh mode) {
    r = r.clipped(c.rect());
    if (r.empty() || fb_ == nullptr) {
      return;
    }
    for (int y = r.y0; y < r.y1; y++) {
      const uint8_t* src = c.data() + size_t(y) * c.width();
      uint16_t* dst = fb_ + size_t(y) * gfx::kWidth;
      for (int x = r.x0; x < r.x1; x++) {
        const uint8_t g = src[x];
        dst[x] = uint16_t(((g >> 3) << 11) | ((g >> 2) << 5) | (g >> 3));
      }
    }
    UpdateParams u{};
    u.x1 = r.x0;
    u.y1 = r.y0;
    u.x2 = r.x1 - 1;
    u.y2 = r.y1 - 1;
    switch (mode) {
      case app::Refresh::Ink:
        u.waveform = kWaveDU;
        u.flags = kFlagFastDraw;
        break;
      case app::Refresh::Ui:
        u.waveform = kWaveGL16;
        u.flags = 0;
        break;
      case app::Refresh::Full:
        u.waveform = kWaveGC16;
        u.flags = kFlagSync;
        break;
    }
    u.waveform |= kIoctlWaveformFlag;
    const int32_t idx = kMsgUpdate;
    bool ok = false;
    if (!writeAll(sock_, &idx, sizeof idx) || !writeAll(sock_, &u, sizeof u) ||
        !readAll(sock_, &ok, sizeof ok)) {
      std::fprintf(stderr, "kana-dojo: lost display server\n");
      std::exit(1); // systemd restarts us
    }
  }

  int openInput(const std::string& path) {
    OpenInputMsg m;
    std::strncpy(m.path, path.c_str(), sizeof(m.path) - 1);
    m.flags = O_RDONLY | O_NONBLOCK;
    const int32_t idx = kMsgOpenInput;
    bool ok = false;
    if (!writeAll(sock_, &idx, sizeof idx) || !writeAll(sock_, &m, sizeof m) ||
        !readAll(sock_, &ok, sizeof ok) || !ok) {
      return -1;
    }
    return recvFd(sock_);
  }

private:
  bool init() {
    InitMsg m;
    const int32_t idx = kMsgInit;
    FbFormat granted;
    if (!writeAll(sock_, &idx, sizeof idx) || !writeAll(sock_, &m, sizeof m) ||
        !readAll(sock_, &granted, sizeof granted)) {
      return false;
    }
    const int fd = recvFd(sock_);
    if (fd < 0 || granted.width != gfx::kWidth || granted.height != gfx::kHeight ||
        granted.pixelFormat != 0) {
      std::fprintf(stderr, "kana-dojo: unexpected framebuffer from server\n");
      return false;
    }
    const size_t size = size_t(gfx::kWidth) * gfx::kHeight * 3; // RGB565 + grey
    void* mem = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (mem == MAP_FAILED) {
      return false;
    }
    fb_ = static_cast<uint16_t*>(mem);
    return true;
  }

  int sock_ = -1;
  uint16_t* fb_ = nullptr;
};

std::string
readFile(const std::string& path) {
  std::ifstream in(path);
  std::string s;
  std::getline(in, s);
  return s;
}

// Finds /dev/input/eventN by the device name the kernel reports.
std::string
findInput(const std::string& name) {
  DIR* dir = opendir("/sys/class/input");
  if (dir == nullptr) {
    return {};
  }
  std::string found;
  while (dirent* e = readdir(dir)) {
    const std::string n = e->d_name;
    if (n.rfind("event", 0) == 0 &&
        readFile("/sys/class/input/" + n + "/device/name") == name) {
      found = "/dev/input/" + n;
      break;
    }
  }
  closedir(dir);
  return found;
}

int64_t
monotonicMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

std::string
baseDir() {
  char buf[4096];
  const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  std::string exe = n > 0 ? std::string(buf, size_t(n)) : "/home/root/kana-dojo/bin/kana-dojo";
  exe = exe.substr(0, exe.rfind('/')); // .../bin
  return exe.substr(0, exe.rfind('/'));
}

class Rm2Platform : public app::Platform {
public:
  explicit Rm2Platform(Display& d) : display_(d) {}

  int64_t nowMs() override { return monotonicMs(); }

  int today() override {
    const time_t t = time(nullptr);
    tm local{};
    localtime_r(&t, &local);
    return int((t + local.tm_gmtoff) / 86400);
  }

  int batteryPercent() override {
    const std::string v = readFile("/sys/class/power_supply/max77818_battery/capacity");
    return v.empty() ? -1 : std::atoi(v.c_str());
  }

  void push(const gfx::Canvas& c, gfx::Rect r, app::Refresh mode) override {
    display_.push(c, r, mode);
  }

  void suspend() override {
    if (dryRun()) {
      std::fprintf(stderr, "kana-dojo: [dry-run] suspend\n");
      ignorePowerUntil = monotonicMs() + 1500;
      return;
    }
    sync();
    usleep(400 * 1000); // let the e-paper finish the sleep screen
    std::ofstream state("/sys/power/state");
    state << "mem" << std::flush; // blocks until wake-up
    if (!state) {
      run("systemctl suspend");
      sleep(3);
    }
    ignorePowerUntil = monotonicMs() + 1500;
  }

  void powerOff() override {
    if (dryRun()) {
      std::fprintf(stderr, "kana-dojo: [dry-run] poweroff\n");
      return;
    }
    sync();
    run("systemctl poweroff");
  }

  void reboot() override {
    if (dryRun()) {
      std::fprintf(stderr, "kana-dojo: [dry-run] reboot\n");
      return;
    }
    sync();
    run("systemctl reboot");
  }

  void startOriginalSoftware() override {
    if (dryRun()) {
      std::fprintf(stderr, "kana-dojo: [dry-run] start original software\n");
      return;
    }
    sync();
    // The fallback unit stops us and starts xochitl. Without it (test run)
    // we simply quit and the test script brings xochitl back.
    if (std::system("systemctl start --no-block kana-dojo-fallback.service") != 0) {
      std::exit(0);
    }
  }

  int64_t ignorePowerUntil = 0;

private:
  static bool dryRun() { return std::getenv("KANA_DOJO_DRY_RUN") != nullptr; }

  Display& display_;
};

struct TouchSlot {
  int id = -1;
  int x = 0, y = 0;
  int downX = 0, downY = 0;
  int64_t downMs = 0;
  bool changed = false;
};

volatile std::sig_atomic_t screenshotRequested = 0;

void
onScreenshotSignal(int) {
  screenshotRequested = 1;
}

// `kill -USR1 <pid>` writes the current screen as an 8-bit BMP, so the
// screen can be checked from a computer without looking at the tablet.
void
writeScreenshot(const gfx::Canvas& c, const char* path) {
  const int w = c.width(), h = c.height();
  const uint32_t palette = 256 * 4, header = 14 + 40 + palette;
  const uint32_t size = header + uint32_t(w) * h; // rows are already 4-byte aligned
  std::vector<uint8_t> out(header, 0);
  auto put16 = [&](size_t at, uint16_t v) { std::memcpy(&out[at], &v, 2); };
  auto put32 = [&](size_t at, uint32_t v) { std::memcpy(&out[at], &v, 4); };
  out[0] = 'B';
  out[1] = 'M';
  put32(2, size);
  put32(10, header);
  put32(14, 40);
  put32(18, uint32_t(w));
  put32(22, uint32_t(h)); // positive height: bottom-up rows
  put16(26, 1);
  put16(28, 8);
  put32(46, 256);
  for (int i = 0; i < 256; i++) {
    out[54 + i * 4] = out[54 + i * 4 + 1] = out[54 + i * 4 + 2] = uint8_t(i);
  }
  const std::string tmp = std::string(path) + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (f == nullptr) {
    return;
  }
  std::fwrite(out.data(), 1, out.size(), f);
  for (int y = h - 1; y >= 0; y--) {
    std::fwrite(c.data() + size_t(y) * w, 1, size_t(w), f);
  }
  std::fclose(f);
  std::rename(tmp.c_str(), path);
  std::fprintf(stderr, "kana-dojo: screenshot written to %s\n", path);
}

} // namespace

int
main() {
  const std::string base = baseDir();
  gfx::Fonts fonts;
  if (!fonts.load(base + "/share/glyphs.bin")) {
    std::fprintf(stderr, "kana-dojo: cannot load %s/share/glyphs.bin\n", base.c_str());
    return 1;
  }
  mkdir((base + "/data").c_str(), 0755);
  kana::Progress progress(base + "/data/progress.tsv");
  progress.load();

  Display display;
  if (!display.connect()) {
    return 1;
  }
  Rm2Platform platform(display);
  app::App application(platform, fonts, progress);

  struct Source {
    int fd;
    enum { Pen, Touch, Power } kind;
  };
  std::vector<Source> sources;
  struct Wanted {
    const char* name;
    const char* env;
    decltype(Source::kind) kind;
  };
  const Wanted wanted[] = {
    { "Wacom I2C Digitizer", "KANA_DOJO_INPUT_PEN", Source::Pen },
    { "pt_mt", "KANA_DOJO_INPUT_TOUCH", Source::Touch },
    { "snvs-powerkey", "KANA_DOJO_INPUT_POWER", Source::Power },
  };
  for (const auto& [name, env, kind] : wanted) {
    const std::string path = envOr(env, findInput(name));
    const int fd = path.empty() ? -1 : display.openInput(path);
    if (fd < 0) {
      std::fprintf(stderr, "kana-dojo: input '%s' not available\n", name);
      continue;
    }
    std::fprintf(stderr, "kana-dojo: input '%s' = %s\n", name, path.c_str());
    sources.push_back({ fd, kind });
  }

  std::signal(SIGUSR1, onScreenshotSignal);
  application.start();
  application.flush();

  // Pen state
  float rawX = 0, rawY = 0;
  bool penTouching = false, penWasTouching = false, penNear = false;
  int64_t penLeftMs = 0;
  // Touch state
  std::vector<TouchSlot> slots(10);
  int slot = 0;

  std::vector<pollfd> pfds;
  for (const auto& s : sources) {
    pfds.push_back({ s.fd, POLLIN, 0 });
  }

  while (true) {
    const int64_t timeout = application.msUntilTimer();
    const int r = poll(pfds.data(), pfds.size(),
                       int(std::min<int64_t>(timeout, 60 * 60 * 1000)));
    if (r < 0 && errno != EINTR) {
      std::perror("poll");
      return 1;
    }
    for (size_t i = 0; r > 0 && i < pfds.size(); i++) {
      if ((pfds[i].revents & POLLIN) == 0) {
        continue;
      }
      RawInputEvent evs[64];
      const ssize_t n = read(pfds[i].fd, evs, sizeof(evs));
      if (n <= 0) {
        continue;
      }
      for (size_t k = 0; k < size_t(n) / sizeof(RawInputEvent); k++) {
        const auto& e = evs[k];
        switch (sources[i].kind) {
          case Source::Pen:
            if (e.type == EV_ABS_ && e.code == ABS_X_) {
              rawX = float(e.value);
            } else if (e.type == EV_ABS_ && e.code == ABS_Y_) {
              rawY = float(e.value);
            } else if (e.type == EV_KEY_ && e.code == BTN_TOUCH_) {
              penTouching = e.value != 0;
            } else if (e.type == EV_KEY_ &&
                       (e.code == BTN_TOOL_PEN_ || e.code == BTN_TOOL_RUBBER_)) {
              penNear = e.value != 0;
              if (!penNear) {
                penLeftMs = monotonicMs();
              }
            } else if (e.type == EV_SYN_ && e.code == SYN_REPORT_) {
              const float x = rawY * gfx::kWidth / kWacomMaxY;
              const float y = gfx::kHeight - rawX * gfx::kHeight / kWacomMaxX;
              if (penTouching && !penWasTouching) {
                application.penDown(x, y);
              } else if (penTouching) {
                application.penMove(x, y);
              } else if (penWasTouching) {
                application.penUp(x, y);
              }
              penWasTouching = penTouching;
            }
            break;

          case Source::Touch:
            if (e.type == EV_ABS_ && e.code == ABS_MT_SLOT_) {
              slot = std::max(0, std::min<int>(e.value, int(slots.size()) - 1));
            } else if (e.type == EV_ABS_ && e.code == ABS_MT_TRACKING_ID_) {
              auto& s = slots[slot];
              if (e.value >= 0) {
                s.id = e.value;
                s.downMs = monotonicMs();
                s.changed = true;
                s.downX = -1;
              } else if (s.id >= 0) {
                // Finger lifted: a short, still touch counts as a tap.
                const bool still = std::abs(s.x - s.downX) < 40 && std::abs(s.y - s.downY) < 40;
                const bool quick = monotonicMs() - s.downMs < 700;
                const bool penBusy = penNear || monotonicMs() - penLeftMs < 600;
                if (still && quick && !penBusy && s.downX >= 0) {
                  application.touchTap(s.x, s.y);
                }
                s.id = -1;
              }
            } else if (e.type == EV_ABS_ && e.code == ABS_MT_POSITION_X_) {
              slots[slot].x = e.value;
            } else if (e.type == EV_ABS_ && e.code == ABS_MT_POSITION_Y_) {
              slots[slot].y = gfx::kHeight - e.value;
            } else if (e.type == EV_SYN_ && e.code == SYN_REPORT_) {
              for (auto& s : slots) {
                if (s.changed && s.id >= 0) {
                  s.downX = s.x;
                  s.downY = s.y;
                  s.changed = false;
                }
              }
            }
            break;

          case Source::Power:
            if (e.type == EV_KEY_ && e.code == KEY_POWER_ && e.value == 1 &&
                monotonicMs() >= platform.ignorePowerUntil) {
              application.powerButton();
            }
            break;
        }
      }
    }
    application.tick();
    application.flush();
    if (screenshotRequested != 0) {
      screenshotRequested = 0;
      writeScreenshot(application.canvas(), envOr("KANA_DOJO_SCREENSHOT", "/tmp/kana-dojo-screen.bmp").c_str());
    }
  }
}
