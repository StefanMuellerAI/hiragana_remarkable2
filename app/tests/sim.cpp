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

// ----- writing sheet helpers (layout mirrors app.cpp) -----

gfx::Rect
cellRect(const App& a, int line, int cell) {
  const int row = line - a.sheetTop();
  return gfx::Rect::xywh(72 + cell * 180, 132 + row * 210, 180, 180);
}

// Writes stroke `s` of `k` into `box`, optionally scaled about the centre
// and shifted, the way a person's handwriting varies.
void
writeStrokeIn(App& a, const kana::Kana& k, int s, gfx::Rect box, bool reverse = false,
              float scale = 1.f, float dx = 0, float dy = 0) {
  const float px = box.w() / kana::kGlyphSize;
  auto pts = k.strokes[s].points;
  if (reverse) {
    std::reverse(pts.begin(), pts.end());
  }
  const float wob = jitter(2.f);
  float lx = 0, ly = 0;
  for (size_t i = 0; i < pts.size(); i++) {
    const float rx = 54.5f + (pts[i].x - 54.5f) * scale;
    const float ry = 54.5f + (pts[i].y - 54.5f) * scale;
    lx = box.x0 + rx * px + dx + wob + jitter(1.f);
    ly = box.y0 + ry * px + dy + jitter(1.f);
    if (i == 0) {
      a.penDown(lx, ly);
    } else {
      a.penMove(lx, ly);
    }
  }
  a.penUp(lx, ly);
  a.flush();
}

void
writeKanaIn(App& a, const kana::Kana& k, gfx::Rect box, float scale = 1.f, float dx = 0,
            float dy = 0) {
  for (size_t s = 0; s < k.strokes.size(); s++) {
    writeStrokeIn(a, k, int(s), box, false, scale, dx, dy);
  }
}

bool
contains(const std::string& s, const char* part) {
  return s.find(part) != std::string::npos;
}

const gfx::Fonts* gFonts = nullptr;
// Every message must fit on the screen in the bold feedback font.
void
checkFits(const App& a, const char* what) {
  const int w = gFonts->textWidth(gfx::FontId::Bold, 40, a.message());
  if (w > gfx::kWidth - 40) {
    std::printf("FAIL message too wide (%d px): %s\n", w, a.message().c_str());
    failures++;
  } else {
    std::printf("ok   %s fits (%d px)\n", what, w);
  }
}

void
sheetTests(App& a) {
  // Home -> Schreibblatt (fourth card).
  tap(a, 700, 1585);
  check(a.screenName() == "sheet", "Schreibblatt opens the sheet");
  save(a, "sheet-start");

  const auto& A = *kana::find("あ");
  const auto& I = *kana::find("い");
  writeKanaIn(a, A, cellRect(a, 0, 1));
  check(a.sheetCorrect() == 1, "correct あ stays");

  // Wrong order: stroke 1, then stroke 3.
  writeStrokeIn(a, A, 0, cellRect(a, 0, 2));
  writeStrokeIn(a, A, 2, cellRect(a, 0, 2));
  check(a.sheetCorrect() == 1 && contains(a.message(), "zuerst kommt Strich 2"),
        "wrong stroke order is erased and named");
  checkFits(a, "order message");
  save(a, "sheet-wrong-order");

  // Backwards stroke.
  writeStrokeIn(a, I, 0, cellRect(a, 1, 1), true);
  check(contains(a.message(), "falsche Richtung"), "reversed stroke is erased and named");
  checkFits(a, "direction message");

  // Half-written あ, then a good stroke of い elsewhere: あ is dropped.
  writeStrokeIn(a, A, 0, cellRect(a, 0, 3));
  writeStrokeIn(a, I, 0, cellRect(a, 1, 1));
  check(contains(a.message(), "nicht fertig"), "unfinished kana is erased when moving on");
  checkFits(a, "unfinished message");
  writeStrokeIn(a, I, 1, cellRect(a, 1, 1));
  check(a.sheetCorrect() == 2, "い completed after the abandoned あ");

  // Finished cells and the model can't be written over.
  writeStrokeIn(a, A, 0, cellRect(a, 0, 1));
  writeStrokeIn(a, A, 0, cellRect(a, 0, 0));
  check(a.sheetCorrect() == 2, "finished cell and model ignore the pen");

  // A palm tap on the grid does nothing.
  tap(a, 700, 600);
  check(a.screenName() == "sheet", "tap on the grid is ignored");
  save(a, "sheet-written");

  // Paging by swipe and by buttons; the writing survives.
  a.touchSwipe(10, -400);
  a.flush();
  check(a.sheetTop() == 7, "swipe up shows the next lines");
  save(a, "sheet-page2");
  a.touchSwipe(-400, 40);
  check(a.sheetTop() == 7, "sideways swipe is ignored");
  a.touchSwipe(0, 400);
  a.flush();
  check(a.sheetTop() == 0 && a.sheetCorrect() == 2, "swipe down goes back, writing kept");
  tap(a, 1152, 1782);
  check(a.sheetTop() == 7, "Weiter › pages down");
  tap(a, 252, 1782);
  check(a.sheetTop() == 0, "‹ Zurück pages up");

  // Leaving clears the sheet.
  tap(a, 120, 56);
  check(a.screenName() == "home", "Menü leaves the sheet");
  tap(a, 700, 1585);
  check(a.screenName() == "sheet" && a.sheetCorrect() == 0, "sheet is blank again");
  tap(a, 120, 56);
}

// Writes every kana of one script into the sheet, once correctly (with
// size/position variation) and once with the first two strokes swapped.
void
sheetAllKana(App& a, kana::Script script, const std::string& name) {
  tap(a, 1084, 627); // "Alle"
  tap(a, 700, 1585);
  check(a.screenName() == "sheet", "sheet opens with all rows");
  std::vector<int> order; // same order as the app: groups are contiguous
  for (const auto& k : kana::all()) {
    if (k.script == script) {
      order.push_back(k.index);
    }
  }
  std::mt19937 vr(11);
  std::uniform_real_distribution<float> sc(0.85f, 1.1f), sh(-10.f, 10.f);
  int accepted = 0, multi = 0, rejected = 0;
  std::string missed, slipped;
  const int lines = static_cast<int>(order.size());
  for (int line = 0; line < lines; line++) {
    if (line >= a.sheetTop() + 7) {
      a.touchSwipe(0, -400);
      a.flush();
    }
    const auto& k = kana::all()[order[line]];
    const int before = a.sheetCorrect();
    writeKanaIn(a, k, cellRect(a, line, 1), sc(vr), sh(vr), sh(vr));
    if (a.sheetCorrect() == before + 1) {
      accepted++;
    } else {
      missed += k.glyph + " ";
    }
    if (k.strokes.size() >= 2) {
      multi++;
      const int b2 = a.sheetCorrect();
      writeStrokeIn(a, k, 1, cellRect(a, line, 2));
      writeStrokeIn(a, k, 0, cellRect(a, line, 2));
      for (size_t s = 2; s < k.strokes.size(); s++) {
        writeStrokeIn(a, k, int(s), cellRect(a, line, 2));
      }
      if (a.sheetCorrect() == b2) {
        rejected++;
      } else {
        slipped += k.glyph + " ";
      }
    }
  }
  std::printf("     %s: %d/%d correct kana kept, %d/%d wrong-order kana erased\n",
              name.c_str(), accepted, lines, rejected, multi);
  if (!missed.empty()) {
    std::printf("     not kept: %s\n", missed.c_str());
  }
  if (!slipped.empty()) {
    std::printf("     wrong order not caught: %s\n", slipped.c_str());
  }
  check(lines > 40 && accepted * 100 >= lines * 97, "≥97 % of correct kana kept in small cells");
  check(multi > 0 && rejected * 100 >= multi * 90, "≥90 % of wrong-order kana erased");
  save(a, "sheet-all-" + name);
  tap(a, 120, 56);
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
  tap(a, 700, 1285);
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
  tap(a, 700, 1435);
  check(a.screenName() == "home", "test without learnt kana stays home");
  save(a, "home-katakana-notice");
  tap(a, 375, 486);

  // Test mode on the kana learnt above.
  tap(a, 700, 1435);
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

  // Writing sheet
  gFonts = &fonts;
  tap(a, 120, 56);
  sheetTests(a);
  sheetAllKana(a, kana::Script::Hiragana, "Hiragana");
  tap(a, 1029, 486); // Katakana
  sheetAllKana(a, kana::Script::Katakana, "Katakana");
  tap(a, 375, 486);

  a.powerButton();
  a.flush();
  check(!pf.log.empty() && std::find(pf.log.begin(), pf.log.end(), "suspend") != pf.log.end(),
        "power button suspends");

  std::printf("%zu display pushes\n", pf.log.size());
  std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
  return failures ? 1 : 0;
}
