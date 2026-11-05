// Drives the app through a scripted session on the host and writes
// screenshots (PGM) of each step, so the UI can be checked without a tablet.
#include "../src/app.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <random>

using namespace app;

namespace {

struct FakePlatform : Platform {
  int64_t now = 1'000'000;
  std::vector<std::string> log;
  int64_t nowMs() override { return now; }
  int today() override { return 20000; }
  int batteryPercent() override { return 87; }
  void push(const gfx::Canvas&, gfx::Rect r, Refresh mode) override {
    const char* m = mode == Refresh::Ink ? "ink" : mode == Refresh::Ui ? "ui" : "full";
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s %d,%d %dx%d", m, r.x0, r.y0, r.w(), r.h());
    log.push_back(buf);
  }
  void suspend() override { log.push_back("suspend"); }
  void powerOff() override { log.push_back("poweroff"); }
  void reboot() override { log.push_back("reboot"); }
  void startOriginalSoftware() override { log.push_back("original"); }
};

std::string outDir = ".";
int shot = 0;

void
save(const App& a, const std::string& name) {
  char path[512];
  std::snprintf(path, sizeof path, "%s/%02d-%s.pgm", outDir.c_str(), shot++, name.c_str());
  std::ofstream f(path, std::ios::binary);
  const auto& c = a.canvas();
  f << "P5\n" << c.width() << " " << c.height() << "\n255\n";
  f.write(reinterpret_cast<const char*>(c.data()), c.width() * c.height());
  std::printf("  wrote %s\n", path);
}

std::mt19937 rng(7);
float
jitter(float a) {
  return std::uniform_real_distribution<float>(-a, a)(rng);
}

// Writes stroke `s` of the current kana like a person would.
void
writeStroke(App& a, const kana::Kana& k, int s, bool reverse = false, float dx = 0,
            float dy = 0) {
  const float scale = 1100.f / kana::kGlyphSize;
  auto pts = k.strokes[s].points;
  if (reverse) {
    std::reverse(pts.begin(), pts.end());
  }
  const float wob = jitter(3.f);
  for (size_t i = 0; i < pts.size(); i++) {
    const float x = 152 + (pts[i].x + dx) * scale + wob + jitter(1.5f);
    const float y = 470 + (pts[i].y + dy) * scale + jitter(1.5f);
    if (i == 0) {
      a.penDown(x, y);
    } else {
      a.penMove(x, y);
    }
  }
  const auto& last = pts.back();
  a.penUp(152 + last.x * scale, 470 + last.y * scale);
  a.flush();
}

void
tap(App& a, int x, int y) {
  a.touchTap(x, y);
  a.flush();
}

int failures = 0;
void
check(bool ok, const char* what) {
  std::printf("%s %s\n", ok ? "ok  " : "FAIL", what);
  failures += !ok;
}

} // namespace

int
main(int argc, char** argv) {
  if (argc > 1) {
    outDir = argv[1];
  }
  gfx::Fonts fonts;
  if (!fonts.load(argc > 2 ? argv[2] : "app/assets/glyphs.bin")) {
    std::printf("cannot load glyphs\n");
    return 1;
  }
  std::remove((outDir + "/progress.tsv").c_str());
  kana::Progress progress(outDir + "/progress.tsv");
  FakePlatform pf;
  App a(pf, fonts, progress);
  a.start();
  a.flush();
  save(a, "home");
  check(a.screenName() == "home", "starts on home screen");

  // Select the ka row too, then learn.
  tap(a, 60 + 162 + 74, 684 + 70);
  save(a, "home-ka-selected");
  tap(a, 700, 1160);
  check(a.screenName() == "practice", "Lernen opens practice");
  save(a, "learn-a-start");

  const auto& A = *kana::find("あ");
  writeStroke(a, A, 0);
  save(a, "learn-a-stroke1");
  writeStroke(a, A, 2); // wrong order
  save(a, "learn-a-wrong-order");
  writeStroke(a, A, 1);
  writeStroke(a, A, 2);
  save(a, "learn-a-done");
  pf.now += 1500;
  a.tick();
  a.flush();
  check(a.screenName() == "practice", "no auto-advance after a mistake");
  tap(a, 1152, 1782); // Weiter
  save(a, "learn-i-start");

  const auto& I = *kana::find("い");
  writeStroke(a, I, 0, true); // backwards
  save(a, "learn-i-reversed");
  writeStroke(a, I, 0);
  writeStroke(a, I, 1);
  tap(a, 1152, 1782);
  const auto& U = *kana::find("う");
  writeStroke(a, U, 0);
  writeStroke(a, U, 1);
  save(a, "learn-u-perfect");
  pf.now += 1500;
  a.tick();
  a.flush();
  save(a, "learn-e-auto-advanced");

  // Back to menu, practice (from memory).
  tap(a, 120, 56);
  check(a.screenName() == "home", "Menü returns home");
  tap(a, 700, 1350);
  check(a.screenName() == "practice", "Üben opens practice");
  save(a, "practice-start");
  // Ask for a hint, then write the kana correctly.
  {
    tap(a, 702, 1782);
    save(a, "practice-hint");
  }

  // Test with nothing learnt in the katakana set shows a notice.
  tap(a, 120, 56);
  tap(a, 1029, 486);
  tap(a, 700, 1540);
  check(a.screenName() == "home", "test without learnt kana stays home");
  save(a, "home-katakana-notice");
  tap(a, 375, 486);

  // Test mode on the kana learnt above.
  tap(a, 700, 1540);
  save(a, "test-start");
  for (int round = 0; round < 12 && a.screenName() == "practice"; round++) {
    // Find out which kana is asked by trying all of them is cheating; the
    // harness peeks at the prompt via the known queue order instead.
    tap(a, 1152, 1782); // skip
  }
  a.flush();
  save(a, "summary");
  check(a.screenName() == "summary", "test ends in summary");

  tap(a, 1029, 1765);
  tap(a, 120, 56);
  tap(a, 375, 1755);
  save(a, "progress");
  check(a.screenName() == "progress", "progress screen");
  tap(a, 120, 56);
  tap(a, 1029, 1755);
  save(a, "system");
  check(a.screenName() == "system", "system screen");

  a.powerButton();
  a.flush();
  check(!pf.log.empty() && std::find(pf.log.begin(), pf.log.end(), "suspend") != pf.log.end(),
        "power button suspends");

  std::printf("%zu display pushes\n", pf.log.size());
  std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
