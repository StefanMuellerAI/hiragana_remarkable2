#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <random>

using gfx::FontId;
using gfx::Rect;

namespace app {
namespace {

constexpr int W = gfx::kWidth;
constexpr int H = gfx::kHeight;
const Rect kBox = Rect::xywh(152, 470, 1100, 1100);
const Rect kFeedback = { 0, 1585, W, 1708 };
constexpr float kInkWidth = 10.f;
constexpr int64_t kSleepAfterMs = 10 * 60 * 1000;
constexpr int64_t kAutoAdvanceMs = 1300;
constexpr int kFlashEvery = 6;
constexpr size_t kSessionSize = 20;

// Writing sheet layout: per line the model plus `cols` cells. Kanji get
// bigger cells (and so fewer per line and page) than kana.
struct SheetLayout {
  int cell, cols, rows, pitch; // px, cells to write in, lines per page, line pitch
  int x0() const { return (gfx::kWidth - (cols + 1) * cell) / 2; }
  int y0() const { return 132; }
};
constexpr SheetLayout kKanaSheet{ 180, 6, 7, 210 };  // about 2 cm per cell
constexpr SheetLayout kKanjiSheet{ 210, 5, 6, 240 }; // about 2.4 cm per cell
constexpr int kSheetSpill = 10; // ink may leave its cell by this much
constexpr float kSheetInk = 6.f;
constexpr int kSheetFlashEvery = 3;
const char* const kSheetHelp = "Schreib das Zeichen in die freien Kästchen seiner Zeile.";

const SheetLayout&
sheetLayout(kana::Script s) {
  return s == kana::Script::Kanji ? kKanjiSheet : kKanaSheet;
}

const char* const kModeNames[] = { "Lernen", "Üben", "Test" };

std::string
scriptName(kana::Script s) {
  switch (s) {
    case kana::Script::Hiragana:
      return "Hiragana";
    case kana::Script::Katakana:
      return "Katakana";
    case kana::Script::Kanji:
      return "Kanji N5";
  }
  return "";
}

// Stored in progress.tsv; "H" and "K" are what older versions wrote.
std::string
scriptKey(kana::Script s) {
  switch (s) {
    case kana::Script::Hiragana:
      return "H";
    case kana::Script::Katakana:
      return "K";
    case kana::Script::Kanji:
      return "J";
  }
  return "H";
}

} // namespace

App::App(Platform& platform, const gfx::Fonts& fonts, kana::Progress& progress)
  : platform_(platform), fonts_(fonts), progress_(progress) {}

void
App::start() {
  const std::string key = progress_.setting("script", "H");
  script_ = key == "K"   ? kana::Script::Katakana
            : key == "J" ? kana::Script::Kanji
                         : kana::Script::Hiragana;
  lastInputMs_ = platform_.nowMs();
  show(Screen::Home);
}

std::string
App::screenName() const {
  switch (screen_) {
    case Screen::Home:
      return "home";
    case Screen::Practice:
      return "practice";
    case Screen::Summary:
      return "summary";
    case Screen::Progress:
      return "progress";
    case Screen::System:
      return "system";
    case Screen::Sheet:
      return "sheet";
    case Screen::Quiz:
      return "quiz";
  }
  return "?";
}

// ---------------------------------------------------------------- drawing --

void
App::invalidate(Rect r, Refresh mode) {
  r = r.clipped(canvas_.rect());
  if (!r.empty()) {
    pending_.push_back({ r, mode });
  }
}

void
App::flush() {
  if (pending_.empty()) {
    return;
  }
  // One full refresh covers everything else.
  for (const auto& p : pending_) {
    if (p.mode == Refresh::Full) {
      pending_.clear();
      platform_.push(canvas_, canvas_.rect(), Refresh::Full);
      return;
    }
  }
  Rect ui, ink;
  for (const auto& p : pending_) {
    (p.mode == Refresh::Ui ? ui : ink) = (p.mode == Refresh::Ui ? ui : ink).united(p.r);
  }
  pending_.clear();
  if (!ink.empty()) {
    platform_.push(canvas_, ink, Refresh::Ink);
  }
  if (!ui.empty()) {
    platform_.push(canvas_, ui, Refresh::Ui);
  }
}

void
App::show(Screen s) {
  screen_ = s;
  pressedButton_ = -1;
  redraw();
  invalidate(canvas_.rect(), Refresh::Full);
}

void
App::redraw() {
  buttons_.clear();
  canvas_.fill(canvas_.rect(), gfx::kWhite);
  switch (screen_) {
    case Screen::Home:
      drawHome();
      break;
    case Screen::Practice:
      drawPractice();
      break;
    case Screen::Summary:
      drawSummary();
      break;
    case Screen::Progress:
      drawProgress();
      break;
    case Screen::System:
      drawSystem();
      break;
    case Screen::Sheet:
      drawSheet();
      break;
    case Screen::Quiz:
      drawQuiz();
      break;
  }
}

void
App::drawButton(Rect r, const std::string& label, bool primary,
                std::function<void()> action, const std::string& sub) {
  const uint8_t bg = primary ? gfx::kBlack : gfx::kWhite;
  const uint8_t fg = primary ? gfx::kWhite : gfx::kBlack;
  canvas_.roundRect(r, 18, bg, 4, gfx::kBlack);
  if (sub.empty()) {
    canvas_.textCentered(fonts_, FontId::Bold, 40, label, r.cx(), r.cy() + 14, fg);
  } else {
    canvas_.textCentered(fonts_, FontId::Bold, 40, label, r.cx(), r.cy() - 4, fg);
    canvas_.textCentered(fonts_, FontId::Regular, 26, sub, r.cx(), r.cy() + 36, fg);
  }
  buttons_.push_back({ r, std::move(action) });
}

void
App::drawHeader(const std::string& title, const std::string& right) {
  drawButton(Rect::xywh(24, 18, 210, 78), "‹ Menü", false, [this] {
    show(Screen::Home);
  });
  canvas_.textCentered(fonts_, FontId::Bold, 40, title, W / 2, 72, gfx::kBlack);
  if (!right.empty()) {
    const int w = fonts_.textWidth(FontId::Regular, 40, right);
    canvas_.text(fonts_, FontId::Regular, 40, right, W - 40 - w, 72, gfx::kBlack);
  }
  canvas_.fill({ 0, 112, W, 115 }, gfx::kLight);
}

void
App::drawCheckMark(int cx, int cy, int s, uint8_t v) {
  canvas_.line(cx - s * 0.5f, cy, cx - s * 0.15f, cy + s * 0.35f, s * 0.16f, v, true);
  canvas_.line(cx - s * 0.15f, cy + s * 0.35f, cx + s * 0.55f, cy - s * 0.4f,
               s * 0.16f, v, true);
}

void
App::drawHome() {
  const int battery = platform_.batteryPercent();
  if (battery >= 0) {
    const std::string b = "Akku " + std::to_string(battery) + " %";
    const int w = fonts_.textWidth(FontId::Regular, 26, b);
    canvas_.text(fonts_, FontId::Regular, 26, b, W - 40 - w, 50, gfx::kDark);
  }
  canvas_.textCentered(fonts_, FontId::Kana, 150, "かな道場", W / 2, 235, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Regular, 32,
                       "Kana-Dojo · Hiragana, Katakana und Kanji schreiben lernen",
                       W / 2, 305, gfx::kDark);

  // Script toggle: every mode works on the full set of the chosen script.
  canvas_.text(fonts_, FontId::Bold, 32, "Schrift", 60, 392, gfx::kBlack);
  const char* const scriptGlyphs[] = { "ひらがな", "カタカナ", "漢字" };
  for (int i = 0; i < 3; i++) {
    const auto s = static_cast<kana::Script>(i);
    const Rect r = Rect::xywh(60 + i * 436, 412, 412, 150);
    const bool sel = s == script_;
    canvas_.roundRect(r, 18, sel ? gfx::kBlack : gfx::kWhite, 4, gfx::kBlack);
    const uint8_t fg = sel ? gfx::kWhite : gfx::kBlack;
    canvas_.textCentered(fonts_, FontId::Kana, 64, scriptGlyphs[i], r.cx(), r.y0 + 82, fg);
    canvas_.textCentered(fonts_, FontId::Regular, 32, scriptName(s), r.cx(),
                         r.y0 + 128, fg);
    buttons_.push_back({ r, [this, s] {
                          script_ = s;
                          progress_.setSetting("script", scriptKey(s));
                          progress_.save();
                          redraw();
                          invalidate(canvas_.rect(), Refresh::Ui);
                        } });
  }

  // Modes
  canvas_.text(fonts_, FontId::Bold, 32, "Üben", 60, 650, gfx::kBlack);
  const int n = static_cast<int>(scriptKana().size());
  const int due = dueCount();
  struct ModeInfo {
    const char* title;
    const char* desc;
    std::string right;
    std::function<void()> action;
  };
  const ModeInfo modes[] = {
    { "Lernen", "Mit Vorlage nachfahren, Strich für Strich",
      std::to_string(n) + " Zeichen", [this] { startMode(Mode::Learn); } },
    { "Üben", "Aus dem Gedächtnis schreiben, mit Korrektur",
      std::to_string(std::min<int>(n, kSessionSize)) + " Zeichen",
      [this] { startMode(Mode::Practice); } },
    { "Test", "Lernkartei: fällige Zeichen wiederholen", std::to_string(due) + " fällig",
      [this] { startMode(Mode::Test); } },
    { "Schreibblatt", "Zeile für Zeile schreiben – Fehler werden gelöscht",
      std::to_string(n) + " Zeilen", [this] { openSheet(); } },
    { "Zuordnen",
      script_ == kana::Script::Kanji ? "Spiel: Zeichen und Bedeutung richtig zuordnen"
                                     : "Spiel: Zeichen und Lautschrift richtig zuordnen",
      std::to_string(n) + " Fragen", [this] { startQuiz(scriptKana()); } },
  };
  for (int i = 0; i < 5; i++) {
    const Rect r = Rect::xywh(60, 672 + i * 196, 1284, 176);
    canvas_.roundRect(r, 18, gfx::kWhite, 4, gfx::kBlack);
    canvas_.text(fonts_, FontId::Bold, 52, modes[i].title, r.x0 + 40, r.y0 + 78, gfx::kBlack);
    canvas_.text(fonts_, FontId::Regular, 32, modes[i].desc, r.x0 + 40, r.y0 + 132, gfx::kDark);
    const int w = fonts_.textWidth(FontId::Bold, 40, modes[i].right);
    canvas_.text(fonts_, FontId::Bold, 40, modes[i].right, r.x1 - 50 - w, r.y0 + 102,
                 gfx::kBlack);
    buttons_.push_back({ r, modes[i].action });
  }

  if (!homeNotice_.empty()) {
    canvas_.textCentered(fonts_, FontId::Bold, 32, homeNotice_, W / 2, 1694, gfx::kBlack);
    homeNotice_.clear();
  }

  drawButton(Rect::xywh(60, 1716, 630, 126), "Fortschritt", false,
             [this] { show(Screen::Progress); });
  drawButton(Rect::xywh(714, 1716, 630, 126), "System", false,
             [this] { show(Screen::System); });
}

std::string
App::promptLabel(const kana::Kana& k) const {
  if (k.romaji == "-") {
    return "ー";
  }
  return k.romaji;
}

void
App::drawKanaStrokes(const kana::Kana& k, Rect box, int upto, int highlight,
                     float width, uint8_t done, uint8_t cur) {
  const float s = box.w() / kana::kGlyphSize;
  for (int i = 0; i < upto && i < static_cast<int>(k.strokes.size()); i++) {
    std::vector<std::pair<float, float>> pts;
    for (const auto& p : k.strokes[i].points) {
      pts.emplace_back(box.x0 + p.x * s, box.y0 + p.y * s);
    }
    canvas_.polyline(pts, width, i == highlight ? cur : done, true);
  }
}

void
App::drawPrompt() {
  const auto& k = kana::all()[queue_[pos_]];
  const Rect area{ 0, 118, W, 462 };
  canvas_.fill(area, gfx::kWhite);

  if (k.script == kana::Script::Kanji) {
    // Meaning big, readings and stroke count below, the kanji on the right;
    // up to 14 strokes, so the stroke-order strip gets its own full row.
    const int n = static_cast<int>(k.strokes.size());
    const bool showGlyph = mode_ == Mode::Learn || kanaDone_;
    const int maxW = W - 160 - (showGlyph ? 240 : 0);
    int size = 40;
    for (int s : { 72, 52 }) {
      if (fonts_.textWidth(FontId::Bold, s, k.romaji) <= maxW) {
        size = s;
        break;
      }
    }
    canvas_.text(fonts_, FontId::Bold, size, k.romaji, 80, 216, gfx::kBlack);
    canvas_.text(fonts_, FontId::Kana, 44, k.readings, 84, 282, gfx::kDark);
    if (mode_ != Mode::Test) {
      canvas_.text(fonts_, FontId::Regular, 32,
                   std::to_string(n) + (n == 1 ? " Strich" : " Striche"), 84, 328, gfx::kDark);
    }
    if (showGlyph) {
      const int w = fonts_.textWidth(FontId::Kana, 150, k.glyph);
      canvas_.text(fonts_, FontId::Kana, 150, k.glyph, W - 80 - w, 300, gfx::kBlack);
    }
    if (mode_ == Mode::Learn) {
      const int gap = 8;
      const int box = std::min(86, (W - 120 - (n - 1) * gap) / n);
      for (int i = 0; i < n; i++) {
        const Rect r = Rect::xywh(60 + i * (box + gap), 350, box, box);
        canvas_.frame(r, 2, gfx::kLight);
        drawKanaStrokes(k, r, i + 1, i, 3.5f, gfx::kMid, gfx::kBlack);
        const auto& start = k.strokes[i].points.front();
        const float s = box / kana::kGlyphSize;
        canvas_.disk(r.x0 + start.x * s, r.y0 + start.y * s, 4, gfx::kBlack);
      }
    }
    return;
  }

  const std::string label = promptLabel(k);
  const bool longMark = k.romaji == "-";
  const FontId labelFont = longMark ? FontId::Kana : FontId::Bold;
  canvas_.text(fonts_, labelFont, 110, label, 80, 300, gfx::kBlack);

  std::string sub;
  if (kana::groups()[k.group].name == "klein") {
    sub = "klein geschrieben";
  } else if (longMark) {
    sub = "Langvokal-Strich";
  }
  if (mode_ != Mode::Test) {
    const int n = static_cast<int>(k.strokes.size());
    sub += (sub.empty() ? "" : " · ") + std::to_string(n) + (n == 1 ? " Strich" : " Striche");
  }
  canvas_.text(fonts_, FontId::Regular, 32, sub, 84, 372, gfx::kDark);

  const bool showGlyph = mode_ == Mode::Learn || kanaDone_;
  if (showGlyph && !longMark) {
    const int x = 80 + fonts_.textWidth(labelFont, 110, label) + 70;
    canvas_.text(fonts_, FontId::Kana, 150, k.glyph, x, 318, gfx::kBlack);
  }

  if (mode_ == Mode::Learn) {
    const int n = static_cast<int>(k.strokes.size());
    const int size = 100, gap = 12;
    int x = W - 60 - n * (size + gap) + gap;
    canvas_.text(fonts_, FontId::Regular, 26, "Strichfolge", x, 196, gfx::kDark);
    for (int i = 0; i < n; i++) {
      const Rect r = Rect::xywh(x + i * (size + gap), 214, size, size);
      canvas_.frame(r, 2, gfx::kLight);
      drawKanaStrokes(k, r, i + 1, i, 4.5f, gfx::kMid, gfx::kBlack);
      const auto& start = k.strokes[i].points.front();
      const float s = size / kana::kGlyphSize;
      canvas_.disk(r.x0 + start.x * s, r.y0 + start.y * s, 5, gfx::kBlack);
    }
  }
}

std::pair<float, float>
App::toScreen(kana::Pt p) const {
  const float s = kBox.w() / kana::kGlyphSize;
  return { kBox.x0 + p.x * s, kBox.y0 + p.y * s };
}

kana::Pt
App::toRef(float x, float y) const {
  const float s = kBox.w() / kana::kGlyphSize;
  return { (x - kBox.x0) / s, (y - kBox.y0) / s };
}

bool
App::inBox(float x, float y) const {
  return kBox.inset(4).contains(int(x), int(y));
}

void
App::drawRefStroke(const kana::Line& ref, float width, uint8_t v, bool arrow) {
  drawRefStrokeIn(ref, kBox, width, v, arrow);
}

void
App::drawRefStrokeIn(const kana::Line& ref, Rect box, float width, uint8_t v,
                     bool arrow) {
  const float s = box.w() / kana::kGlyphSize;
  std::vector<std::pair<float, float>> pts;
  for (const auto& p : ref) {
    pts.emplace_back(box.x0 + p.x * s, box.y0 + p.y * s);
  }
  canvas_.polyline(pts, width, v, true);
  canvas_.disk(pts.front().first, pts.front().second, width * 1.1f, v);
  if (arrow && pts.size() >= 4) {
    const auto& a = pts[pts.size() - 4];
    const auto& b = pts.back();
    float dx = b.first - a.first, dy = b.second - a.second;
    const float len = std::hypot(dx, dy);
    if (len > 0.1f) {
      dx /= len;
      dy /= len;
      const float L = width * 3.2f, Wd = width * 1.9f;
      const float tipX = b.first + dx * L * 0.6f, tipY = b.second + dy * L * 0.6f;
      canvas_.triangle(tipX, tipY, tipX - dx * L - dy * Wd, tipY - dy * L + dx * Wd,
                       tipX - dx * L + dy * Wd, tipY - dy * L - dx * Wd, v);
    }
  }
}

void
App::drawBox() {
  const auto& k = kana::all()[queue_[pos_]];
  canvas_.fill(kBox, gfx::kWhite);
  canvas_.frame(kBox, 4, gfx::kBlack);
  canvas_.dashedLine(kBox.cx(), kBox.y0 + 8, kBox.cx(), kBox.y1 - 8, 22, 3, gfx::kLight);
  canvas_.dashedLine(kBox.x0 + 8, kBox.cy(), kBox.x1 - 8, kBox.cy(), 22, 3, gfx::kLight);

  const int done = checker_ ? checker_->done() : 0;
  if (mode_ == Mode::Learn) {
    drawKanaStrokes(k, kBox, static_cast<int>(k.strokes.size()), -1, 30.f,
                    gfx::kFaint, gfx::kFaint);
    for (int i = 0; i < static_cast<int>(k.strokes.size()); i++) {
      const bool next = i == done && !kanaDone_;
      const auto start = toScreen(k.strokes[i].points.front());
      canvas_.disk(start.first, start.second, next ? 13.f : 8.f,
                   next ? gfx::kBlack : gfx::kMid);
      const auto lp = toScreen(k.strokes[i].label);
      canvas_.text(fonts_, FontId::Bold, next ? 40 : 32, std::to_string(i + 1),
                   int(lp.first) - 8, int(lp.second) + 6, next ? gfx::kBlack : gfx::kMid);
    }
  }

  for (const auto& stroke : inked_) {
    canvas_.polyline(stroke, kInkWidth, gfx::kBlack);
  }
  if (!wrongStroke_.empty()) {
    canvas_.polyline(wrongStroke_, kInkWidth, gfx::kLight);
  }
  if (hintStroke_ >= 0 && hintStroke_ < static_cast<int>(k.strokes.size())) {
    drawRefStroke(k.strokes[hintStroke_].points, 12.f, gfx::kDark, true);
  }
  if (kanaDone_ && mode_ != Mode::Learn) {
    for (const auto& s : k.strokes) {
      std::vector<std::pair<float, float>> pts;
      for (const auto& p : s.points) {
        pts.push_back(toScreen(p));
      }
      canvas_.polyline(pts, 4.f, gfx::kMid, true);
    }
  }
}

void
App::drawFeedback(const std::string& msg, bool bold) {
  message_ = msg;
  canvas_.fill(kFeedback, gfx::kWhite);
  // Long messages (kanji meanings) step down to 32 pt so they stay on screen.
  const FontId font = bold ? FontId::Bold : FontId::Regular;
  const int size = fonts_.textWidth(font, 40, msg) <= W - 40 ? 40 : 32;
  canvas_.textCentered(fonts_, font, size, msg, W / 2, 1662, gfx::kBlack);
  invalidate(kFeedback, Refresh::Ui);
}

void
App::drawPractice() {
  const std::string title =
    std::string(kModeNames[static_cast<int>(mode_)]) + " · " + scriptName(script_);
  drawHeader(title, std::to_string(pos_ + 1) + " / " + std::to_string(queue_.size()));
  drawPrompt();
  drawBox();
  drawFeedback(mode_ == Mode::Learn ? "Fahre die Vorlage nach – beginne beim schwarzen Punkt."
                                    : "Schreibe das Zeichen ins Feld.",
               false);

  drawButton(Rect::xywh(52, 1722, 400, 120), "Löschen", false, [this] {
    loadKana();
    drawBox();
    drawPrompt();
    invalidate(kBox, Refresh::Ui);
    invalidate({ 0, 118, W, 462 }, Refresh::Ui);
    drawFeedback("Neuer Versuch.", false);
  });
  drawButton(Rect::xywh(502, 1722, 400, 120), "Tipp", false, [this] {
    if (kanaDone_ || !checker_) {
      return;
    }
    hintUsed_ = true;
    hintStroke_ = checker_->done();
    drawBox();
    invalidate(kBox, Refresh::Ui);
    drawFeedback("Strich " + std::to_string(hintStroke_ + 1) +
                   ": hier beginnen, in Pfeilrichtung ziehen.",
                 false);
  });
  drawButton(Rect::xywh(952, 1722, 400, 120), "Weiter ›", true, [this] { nextKana(); });
}

void
App::drawSummary() {
  drawHeader("Runde beendet", "");
  int clean = 0;
  float quality = 0;
  std::vector<int> wrong;
  for (const auto& r : results_) {
    if (r.mistakes == 0) {
      clean++;
    } else if (std::find(wrong.begin(), wrong.end(), r.kana) == wrong.end()) {
      wrong.push_back(r.kana);
    }
    quality += r.quality;
  }
  const int n = static_cast<int>(results_.size());
  drawCheckMark(W / 2, 260, 150, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Bold, 72, "Geschafft!", W / 2, 470, gfx::kBlack);
  const std::string lines[] = {
    std::to_string(n) + " Zeichen geschrieben",
    std::to_string(clean) + " davon fehlerfrei",
    "Schreibqualität im Schnitt: " +
      std::to_string(n ? int(std::lround(100 * quality / n)) : 0) + " %",
  };
  for (int i = 0; i < 3; i++) {
    canvas_.textCentered(fonts_, FontId::Regular, 40, lines[i], W / 2, 580 + i * 62,
                         gfx::kBlack);
  }
  if (!wrong.empty()) {
    canvas_.textCentered(fonts_, FontId::Bold, 40, "Noch einmal üben:", W / 2, 860,
                         gfx::kBlack);
    const int per = 8, cell = 140;
    for (size_t i = 0; i < wrong.size() && i < 24; i++) {
      const int rowN = std::min<int>(per, static_cast<int>(wrong.size()) - int(i / per) * per);
      const int x0 = W / 2 - rowN * cell / 2;
      canvas_.textCentered(fonts_, FontId::Kana, 96, kana::all()[wrong[i]].glyph,
                           x0 + int(i % per) * cell + cell / 2,
                           1000 + int(i / per) * 140, gfx::kBlack);
    }
    drawButton(Rect::xywh(60, 1540, 1284, 130), "Fehler üben", true,
               [this, wrong] { startSession(Mode::Practice, wrong); });
  }
  const Mode again = summaryMode_;
  drawButton(Rect::xywh(60, 1700, 630, 130), "Nochmal", false,
             [this, again] { startMode(again); });
  drawButton(Rect::xywh(714, 1700, 630, 130), "Menü", wrong.empty(),
             [this] { show(Screen::Home); });
}

void
App::drawProgress() {
  drawHeader("Fortschritt", "");
  for (int i = 0; i < 3; i++) {
    const auto s = static_cast<kana::Script>(i);
    const Rect r = Rect::xywh(60 + i * 436, 140, 412, 90);
    const bool sel = s == script_;
    canvas_.roundRect(r, 16, sel ? gfx::kBlack : gfx::kWhite, 4, gfx::kBlack);
    canvas_.textCentered(fonts_, FontId::Bold, 40, scriptName(s), r.cx(), r.cy() + 14,
                         sel ? gfx::kWhite : gfx::kBlack);
    buttons_.push_back({ r, [this, s] {
                          script_ = s;
                          redraw();
                          invalidate(canvas_.rect(), Refresh::Ui);
                        } });
  }

  const int today = platform_.today();
  int learnt = 0, solid = 0, due = 0, total = 0;
  // One character with its Lernkartei box (1–6) underneath.
  auto cell = [&](const kana::Kana& k, int cx, int gy) {
    const auto c = progress_.get(k.glyph);
    total++;
    learnt += c.box > 0;
    solid += c.box >= 4;
    due += progress_.isDue(k.glyph, today);
    canvas_.textCentered(fonts_, FontId::Kana, 96, k.glyph, cx, gy + 100,
                         c.box > 0 ? gfx::kBlack : gfx::kLight);
    for (int b = 0; b < kana::Progress::kMaxBox; b++) {
      const Rect seg = Rect::xywh(cx - 51 + b * 18, gy + 128, 14, 14);
      if (b < c.box) {
        canvas_.fill(seg, gfx::kBlack);
      } else {
        canvas_.frame(seg, 2, gfx::kLight);
      }
    }
  };
  if (script_ == kana::Script::Kanji) {
    // Kanji: a plain grid, ten per row.
    int i = 0;
    for (int ki : scriptKana()) {
      cell(kana::all()[ki], 60 + (i % 10) * 128 + 64, 330 + (i / 10) * 160);
      i++;
    }
  } else {
    // Kana: their rows (a, ka, sa …) in two columns.
    int column = 0, row = 0;
    for (const auto& g : kana::groups()) {
      if (g.script != script_) {
        continue;
      }
      const int gx = 60 + column * 654, gy = 330 + row * 172;
      for (size_t i = 0; i < g.kana.size(); i++) {
        cell(kana::all()[g.kana[i]], gx + 62 + int(i) * 126, gy);
      }
      if (++row == 8) {
        row = 0;
        column++;
      }
    }
  }
  const std::string summary = "Gelernt " + std::to_string(learnt) + " von " +
                              std::to_string(total) + " · sicher " +
                              std::to_string(solid) + " · heute fällig " +
                              std::to_string(due);
  canvas_.textCentered(fonts_, FontId::Regular, 36, summary, W / 2, 292, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Regular, 26,
                       "Kästchen = Stufe in der Lernkartei (1–6). Grau = noch nicht gelernt.",
                       W / 2, 1820, gfx::kDark);
}

void
App::drawSystem() {
  drawHeader("System", "");
  drawButton(Rect::xywh(60, 180, 1284, 170), "Original reMarkable-Software starten", false,
             [this] {
               drawFeedback("Starte die Original-Software …", true);
               flush();
               platform_.startOriginalSoftware();
             },
             "bis zum nächsten Neustart – danach startet wieder Kana-Dojo");
  drawButton(Rect::xywh(60, 380, 1284, 150), "Neu starten", false, [this] {
    drawFeedback("Starte neu …", true);
    flush();
    platform_.reboot();
  });
  drawButton(Rect::xywh(60, 560, 1284, 150), "Ausschalten", false, [this] {
    canvas_.fill(canvas_.rect(), gfx::kWhite);
    canvas_.textCentered(fonts_, FontId::Kana, 150, "かな道場", W / 2, 820, gfx::kBlack);
    canvas_.textCentered(fonts_, FontId::Regular, 52, "ausgeschaltet", W / 2, 960, gfx::kDark);
    invalidate(canvas_.rect(), Refresh::Full);
    flush();
    platform_.powerOff();
  });

  const char* info[] = {
    "Kana-Dojo 1.4 – Schreibtrainer für Hiragana, Katakana und Kanji",
    "Strichdaten: KanjiVG (Ulrich Apel, CC BY-SA 3.0)",
    "Schriften: Klee One, Noto Sans (SIL Open Font License)",
    "Display-Treiber: rm2fb / rM2-stuff von timower (GPL-3.0)",
    "Ein-/Ausschaltknopf: Ruhezustand. Nach 10 Minuten ohne Eingabe schläft das Gerät.",
  };
  for (int i = 0; i < 5; i++) {
    canvas_.text(fonts_, FontId::Regular, 30, info[i], 60, 860 + i * 56, gfx::kDark);
  }
}

// ---------------------------------------------------------------- session --

// All kana of the script chosen on the home screen, in table order.
std::vector<int>
App::scriptKana() const {
  std::vector<int> out;
  for (const auto& k : kana::all()) {
    if (k.script == script_) {
      out.push_back(k.index);
    }
  }
  return out;
}

int
App::dueCount() const {
  const int today = platform_.today();
  int n = 0;
  for (int i : scriptKana()) {
    n += progress_.isDue(kana::all()[i].glyph, today);
  }
  return n;
}

void
App::startMode(Mode mode) {
  std::vector<int> sel = scriptKana();
  std::mt19937 rng(static_cast<uint32_t>(platform_.nowMs()));
  std::vector<int> queue;
  if (mode == Mode::Learn) {
    queue = sel;
  } else if (mode == Mode::Practice) {
    std::shuffle(sel.begin(), sel.end(), rng);
    sel.resize(std::min(sel.size(), kSessionSize));
    queue = sel;
  } else {
    const int today = platform_.today();
    std::vector<int> due, learnt;
    for (int i : sel) {
      const auto c = progress_.get(kana::all()[i].glyph);
      if (c.box > 0 && c.dueDay <= today) {
        due.push_back(i);
      } else if (c.box > 0) {
        learnt.push_back(i);
      }
    }
    std::shuffle(due.begin(), due.end(), rng);
    auto byBox = [this](int a, int b) {
      return progress_.get(kana::all()[a].glyph).box <
             progress_.get(kana::all()[b].glyph).box;
    };
    std::stable_sort(due.begin(), due.end(), byBox);
    queue = due;
    // Top up a short test with the weakest learnt kana.
    std::shuffle(learnt.begin(), learnt.end(), rng);
    std::stable_sort(learnt.begin(), learnt.end(), byBox);
    for (int i : learnt) {
      if (queue.size() >= 10) {
        break;
      }
      queue.push_back(i);
    }
    if (queue.empty()) {
      homeNotice_ = "Noch nichts gelernt – starte mit „Lernen“.";
      redraw();
      invalidate(canvas_.rect(), Refresh::Ui);
      return;
    }
    if (queue.size() > kSessionSize) {
      queue.resize(kSessionSize);
    }
    std::shuffle(queue.begin(), queue.end(), rng);
  }
  startSession(mode, queue);
}

void
App::startSession(Mode mode, std::vector<int> queue) {
  if (queue.empty()) {
    return;
  }
  mode_ = mode;
  summaryMode_ = mode;
  queue_ = std::move(queue);
  pos_ = 0;
  results_.clear();
  requeued_.clear();
  kanaSinceFlash_ = 0;
  loadKana();
  show(Screen::Practice);
}

void
App::loadKana() {
  checker_ = std::make_unique<kana::Checker>(kana::all()[queue_[pos_]]);
  inked_.clear();
  current_.clear();
  wrongStroke_.clear();
  hintStroke_ = -1;
  hintUsed_ = false;
  kanaDone_ = false;
  penInBox_ = false;
  autoAdvanceAt_ = -1;
}

void
App::nextKana() {
  autoAdvanceAt_ = -1;
  if (!kanaDone_) {
    // Skipped: counts as not known.
    const auto& k = kana::all()[queue_[pos_]];
    if (mode_ == Mode::Test) {
      progress_.recordTest(k.glyph, false, platform_.today());
      progress_.save();
    }
    if (mode_ != Mode::Learn) {
      results_.push_back({ k.index, 1, 0.f });
    }
  }
  pos_++;
  if (pos_ >= queue_.size()) {
    show(Screen::Summary);
    return;
  }
  loadKana();
  redraw();
  const bool flash = ++kanaSinceFlash_ >= kFlashEvery;
  if (flash) {
    kanaSinceFlash_ = 0;
  }
  invalidate(canvas_.rect(), flash ? Refresh::Full : Refresh::Ui);
}

void
App::finishKana() {
  kanaDone_ = true;
  const auto& k = kana::all()[queue_[pos_]];
  const int mistakes = checker_->mistakes() + (hintUsed_ ? 1 : 0);
  const float quality = checker_->averageQuality();
  const bool ok = mistakes == 0;
  const int today = platform_.today();

  switch (mode_) {
    case Mode::Learn:
      progress_.markLearnt(k.glyph, today);
      break;
    case Mode::Practice:
      progress_.recordPractice(k.glyph, ok);
      if (!ok && std::find(requeued_.begin(), requeued_.end(), k.index) == requeued_.end()) {
        requeued_.push_back(k.index);
        queue_.push_back(k.index);
      }
      break;
    case Mode::Test:
      progress_.recordTest(k.glyph, ok, today);
      break;
  }
  progress_.save();
  results_.push_back({ k.index, mistakes, quality });

  std::string msg;
  if (ok && quality >= 0.7f) {
    msg = "Ausgezeichnet!";
  } else if (ok) {
    msg = "Richtig!";
  } else {
    msg = "Geschafft – mit " + std::to_string(mistakes) +
          (mistakes == 1 ? " Fehler." : " Fehlern.");
    if (mode_ != Mode::Test) {
      msg += " Schreib es zur Übung nochmal.";
    }
  }
  drawPrompt();
  drawBox();
  invalidate({ 0, 118, W, 462 }, Refresh::Ui);
  invalidate(kBox, Refresh::Ui);
  drawFeedback(msg, true);
  if (ok) {
    autoAdvanceAt_ = platform_.nowMs() + kAutoAdvanceMs;
  }
}

void
App::clearHint() {
  if (wrongStroke_.empty() && hintStroke_ < 0) {
    return;
  }
  wrongStroke_.clear();
  hintStroke_ = -1;
  drawBox();
  invalidate(kBox, Refresh::Ui);
}

void
App::handleStroke() {
  const auto& k = kana::all()[queue_[pos_]];
  kana::Line line;
  for (const auto& [x, y] : current_) {
    line.push_back(toRef(x, y));
  }
  const auto res = checker_->add(line);
  const std::string n = std::to_string(res.expected + 1);
  // One line per stroke in the log, to diagnose pen problems remotely.
  std::fprintf(stderr, "stroke %s #%d -> %s (start %.0f,%.0f end %.0f,%.0f, %zu pts)\n",
               k.glyph.c_str(), res.expected + 1, kana::describe(res.kind),
               current_.front().first, current_.front().second, current_.back().first,
               current_.back().second, current_.size());

  switch (res.kind) {
    case kana::StrokeResult::Good:
      inked_.push_back(current_);
      current_.clear();
      if (checker_->complete()) {
        finishKana();
        return;
      }
      if (mode_ == Mode::Learn) {
        drawBox(); // moves the "next stroke" marker
        invalidate(kBox, Refresh::Ui);
      }
      drawFeedback("Strich " + n + " von " + std::to_string(k.strokes.size()) + " richtig.",
                   false);
      return;
    case kana::StrokeResult::Noise:
    case kana::StrokeResult::Extra:
      current_.clear();
      drawBox();
      invalidate(kBox, Refresh::Ui);
      return;
    case kana::StrokeResult::Reversed:
      drawFeedback("Strich " + n + ": falsche Richtung. So geht's – nochmal!", true);
      break;
    case kana::StrokeResult::OtherStroke:
      drawFeedback("Das war Strich " + std::to_string(res.matched + 1) + " – zuerst kommt Strich " +
                     n + ".",
                   true);
      break;
    case kana::StrokeResult::WrongShape:
      drawFeedback("Strich " + n + " passt nicht. So geht's – nochmal!", true);
      break;
  }
  wrongStroke_ = current_;
  current_.clear();
  hintStroke_ = res.expected;
  drawBox();
  invalidate(kBox, Refresh::Ui);
}

// ------------------------------------------------------------------ sheet --

std::string
App::kanaName(const kana::Kana& k) const {
  if (k.romaji == "-") {
    return "„Langvokal-Strich“";
  }
  if (kana::groups()[k.group].name == "klein") {
    return "„klein " + k.romaji + "“";
  }
  return "„" + k.romaji + "“";
}

int
App::sheetCorrect() const {
  int n = 0;
  for (const auto& line : sheet_) {
    for (const auto& c : line.cells) {
      n += c.done;
    }
  }
  return n;
}

bool
App::sheetLineFull(int line) const {
  const auto& cells = sheet_[line].cells;
  return std::all_of(cells.begin() + 1, cells.end(),
                     [](const SheetCell& c) { return c.done; });
}

// Lines are written in order: only the first line that isn't full takes
// strokes, the ones below it unlock one by one.
int
App::sheetCurrentLine() const {
  for (int i = 0; i < static_cast<int>(sheet_.size()); i++) {
    if (!sheetLineFull(i)) {
      return i;
    }
  }
  return static_cast<int>(sheet_.size());
}

void
App::openSheet() {
  sheet_.clear();
  const SheetLayout& G = sheetLayout(script_);
  for (int i : scriptKana()) {
    SheetLine line;
    line.kana = i;
    line.cells.resize(G.cols + 1);
    sheet_.push_back(std::move(line));
  }
  sheetTop_ = 0;
  sheetPen_ = false;
  sheetPenLine_ = sheetPenCell_ = -1;
  activeLine_ = activeCell_ = -1;
  sheetHintLine_ = sheetHintStroke_ = -1;
  sheetMessageShown_ = false;
  sheetPagesSinceFlash_ = 0;
  message_.clear();
  current_.clear();
  show(Screen::Sheet);
}

bool
App::sheetVisible(int line) const {
  return line >= sheetTop_ && line < sheetTop_ + sheetLayout(script_).rows &&
         line < static_cast<int>(sheet_.size());
}

Rect
App::sheetCellRect(int line, int cell) const {
  const SheetLayout& G = sheetLayout(script_);
  const int row = line - sheetTop_;
  return Rect::xywh(G.x0() + cell * G.cell, G.y0() + row * G.pitch, G.cell, G.cell);
}

Rect
App::sheetBand(int line) const {
  const SheetLayout& G = sheetLayout(script_);
  const int gap = (G.pitch - G.cell) / 2;
  const int y = G.y0() + (line - sheetTop_) * G.pitch;
  return { 0, y - gap, W, y + G.cell + gap };
}

Rect
App::sheetGrid() const {
  const SheetLayout& G = sheetLayout(script_);
  return { G.x0() - kSheetSpill, G.y0() - kSheetSpill,
           G.x0() + (G.cols + 1) * G.cell + kSheetSpill,
           G.y0() + (G.rows - 1) * G.pitch + G.cell + kSheetSpill };
}

bool
App::sheetHit(float x, float y, int& line, int& cell) const {
  const SheetLayout& G = sheetLayout(script_);
  for (int row = 0; row < G.rows; row++) {
    const int l = sheetTop_ + row;
    if (l >= static_cast<int>(sheet_.size())) {
      break;
    }
    const int y0 = G.y0() + row * G.pitch;
    const Rect r = { G.x0(), y0, G.x0() + (G.cols + 1) * G.cell, y0 + G.cell };
    if (r.contains(int(x), int(y))) {
      line = l;
      cell = (int(x) - G.x0()) / G.cell;
      return true;
    }
  }
  return false;
}

Rect
App::drawSheetLine(int line) {
  if (!sheetVisible(line)) {
    return {};
  }
  const Rect band = sheetBand(line);
  canvas_.fill(band, gfx::kWhite);
  const SheetLayout& G = sheetLayout(script_);
  const auto& L = sheet_[line];
  const auto& k = kana::all()[L.kana];
  const int strokes = static_cast<int>(k.strokes.size());
  const int current = sheetCurrentLine();
  const bool locked = line > current; // greyed out until the lines above are full

  // The model, written once at the start of the line. After a mistake it
  // shows the stroke that was due, with its direction.
  const Rect model = sheetCellRect(line, 0);
  canvas_.fill(model, gfx::kFaint);
  if (k.romaji != "-") {
    const std::string label =
      kana::groups()[k.group].name == "klein" ? "klein " + k.romaji : k.romaji;
    canvas_.text(fonts_, FontId::Regular, 26, label, model.x0 + 10, model.y0 + 30,
                 locked ? gfx::kMid : gfx::kDark);
  }
  if (line == sheetHintLine_ && sheetHintStroke_ >= 0 && sheetHintStroke_ < strokes) {
    drawKanaStrokes(k, model, strokes, -1, 7.f, gfx::kLight, gfx::kLight);
    drawRefStrokeIn(k.strokes[sheetHintStroke_].points, model, 8.f, gfx::kBlack, true);
  } else {
    const uint8_t v = locked ? gfx::kMid : gfx::kBlack;
    drawKanaStrokes(k, model, strokes, -1, 8.f, v, v);
  }

  // Empty cells with a light centre cross, like a practice sheet. The line
  // being written gets a bold frame, locked lines a faint one.
  for (int c = 1; c <= G.cols; c++) {
    const Rect r = sheetCellRect(line, c);
    if (!locked) {
      canvas_.dashedLine(r.cx(), r.y0 + 6, r.cx(), r.y1 - 6, 10, 2, gfx::kLight);
      canvas_.dashedLine(r.x0 + 6, r.cy(), r.x1 - 6, r.cy(), 10, 2, gfx::kLight);
    }
    canvas_.frame(r, 1, locked ? gfx::kLight : gfx::kMid);
  }
  const uint8_t frameV = locked ? gfx::kLight : line == current ? gfx::kBlack : gfx::kDark;
  canvas_.frame({ model.x0, model.y0, model.x0 + (G.cols + 1) * G.cell, model.y1 },
                line == current ? 5 : 3, frameV);
  canvas_.fill({ model.x1 - 1, model.y0, model.x1 + 2, model.y1 }, frameV);

  for (int c = 1; c <= G.cols; c++) {
    const auto& cell = L.cells[c];
    for (const auto& stroke : cell.ink) {
      canvas_.polyline(stroke, kSheetInk, gfx::kBlack);
    }
    if (cell.done) {
      const Rect r = sheetCellRect(line, c);
      drawCheckMark(r.x1 - 22, r.y0 + 20, 22, gfx::kDark);
    }
  }
  return band;
}

void
App::drawSheet() {
  const int total = static_cast<int>(sheet_.size());
  const int rows = sheetLayout(script_).rows;
  const int pages = std::max(1, (total + rows - 1) / rows);
  const int page = sheetTop_ / rows + 1;
  drawHeader("Schreibblatt · " + scriptName(script_), "");
  for (int row = 0; row < rows; row++) {
    drawSheetLine(sheetTop_ + row);
  }
  drawFeedback(message_.empty() ? kSheetHelp : message_, sheetMessageShown_);

  auto nav = [this](Rect r, const std::string& label, bool enabled, bool primary,
                    std::function<void()> action) {
    if (enabled) {
      drawButton(r, label, primary, std::move(action));
    } else {
      canvas_.roundRect(r, 18, gfx::kWhite, 3, gfx::kLight);
      canvas_.textCentered(fonts_, FontId::Bold, 40, label, r.cx(), r.cy() + 14, gfx::kLight);
    }
  };
  nav(Rect::xywh(52, 1722, 400, 120), "‹ Zurück", page > 1, false, [this] { sheetPage(-1); });
  canvas_.textCentered(fonts_, FontId::Regular, 40,
                       "Seite " + std::to_string(page) + " / " + std::to_string(pages), W / 2,
                       1780, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Regular, 26, "oder mit dem Finger wischen", W / 2, 1826,
                       gfx::kDark);
  nav(Rect::xywh(952, 1722, 400, 120), "Weiter ›", page < pages, true, [this] { sheetPage(1); });
}

void
App::sheetPage(int delta) {
  const int total = static_cast<int>(sheet_.size());
  if (total == 0) {
    return;
  }
  const int rows = sheetLayout(script_).rows;
  const int last = ((total - 1) / rows) * rows;
  const int top = std::max(0, std::min(last, sheetTop_ + delta * rows));
  if (top == sheetTop_) {
    return;
  }
  sheetTop_ = top;
  sheetPen_ = false;
  current_.clear();
  sheetHintLine_ = sheetHintStroke_ = -1;
  sheetMessageShown_ = false;
  message_.clear();
  redraw();
  const bool flash = ++sheetPagesSinceFlash_ >= kSheetFlashEvery;
  if (flash) {
    sheetPagesSinceFlash_ = 0;
  }
  invalidate(canvas_.rect(), flash ? Refresh::Full : Refresh::Ui);
}

void
App::sheetMessage(const std::string& msg, bool bold) {
  drawFeedback(msg, bold);
  sheetMessageShown_ = bold;
}

void
App::eraseSheetCell(int line, int cell) {
  auto& c = sheet_[line].cells[cell];
  c.ink.clear();
  c.checker.reset();
  c.done = false;
  if (line == activeLine_ && cell == activeCell_) {
    activeLine_ = activeCell_ = -1;
  }
  invalidate(drawSheetLine(line), Refresh::Ui);
}

void
App::handleSheetStroke() {
  const int line = sheetPenLine_, c = sheetPenCell_;
  sheetPenLine_ = sheetPenCell_ = -1;
  const SheetLayout& G = sheetLayout(script_);
  if (line < 0 || line >= static_cast<int>(sheet_.size()) || c < 1 || c > G.cols) {
    current_.clear();
    return;
  }
  auto& cell = sheet_[line].cells[c];
  const auto& k = kana::all()[sheet_[line].kana];
  const Rect r = sheetCellRect(line, c);
  const float s = G.cell / kana::kGlyphSize;
  kana::Line stroke;
  for (const auto& [x, y] : current_) {
    stroke.push_back({ (x - r.x0) / s, (y - r.y0) / s });
  }
  if (!cell.checker) {
    cell.checker = std::make_unique<kana::Checker>(k);
  }
  const auto res = cell.checker->add(stroke);
  std::fprintf(stderr,
               "sheet %s line %d cell %d stroke #%d -> %s (start %.0f,%.0f end %.0f,%.0f, %zu pts)\n",
               k.glyph.c_str(), line + 1, c, res.expected + 1, kana::describe(res.kind),
               current_.front().first, current_.front().second, current_.back().first,
               current_.back().second, current_.size());

  if (res.kind == kana::StrokeResult::Noise || res.kind == kana::StrokeResult::Extra) {
    current_.clear();
    invalidate(drawSheetLine(line), Refresh::Ui); // removes the dot
    return;
  }

  // Starting somewhere else ends the kana that was left half-written.
  std::string abandoned;
  if (activeLine_ >= 0 && (activeLine_ != line || activeCell_ != c)) {
    abandoned =
      kanaName(kana::all()[sheet_[activeLine_].kana]) + " war noch nicht fertig – gelöscht.";
    eraseSheetCell(activeLine_, activeCell_);
  }
  // The stroke hint belongs to the line being worked on.
  if (sheetHintLine_ >= 0 && sheetHintLine_ != line) {
    const int old = sheetHintLine_;
    sheetHintLine_ = sheetHintStroke_ = -1;
    invalidate(drawSheetLine(old), Refresh::Ui);
  }

  if (res.kind == kana::StrokeResult::Good) {
    cell.ink.push_back(current_);
    current_.clear();
    if (cell.checker->complete()) {
      cell.done = true;
      activeLine_ = activeCell_ = -1;
      if (sheetHintLine_ == line) {
        sheetHintLine_ = sheetHintStroke_ = -1;
      }
      invalidate(drawSheetLine(line), Refresh::Ui);
      if (sheetLineFull(line)) {
        // Line done: the next one unlocks.
        const int next = line + 1;
        std::string msg = "Zeile " + kanaName(k) + " geschafft";
        if (next >= static_cast<int>(sheet_.size())) {
          msg = "Alle Zeilen geschafft – super!";
        } else if (sheetVisible(next)) {
          msg += " – weiter mit " + kanaName(kana::all()[sheet_[next].kana]) + ".";
          invalidate(drawSheetLine(next), Refresh::Ui);
        } else {
          msg += " – weiter auf der nächsten Seite.";
        }
        sheetMessage(msg, true);
        return;
      }
    } else {
      activeLine_ = line;
      activeCell_ = c;
    }
    if (!abandoned.empty()) {
      sheetMessage(abandoned, true);
    }
    return;
  }

  // Wrong stroke: the whole kana in this cell doesn't count.
  const std::string n = std::to_string(res.expected + 1);
  const std::string name = kanaName(k);
  std::string msg;
  switch (res.kind) {
    case kana::StrokeResult::Reversed:
      msg = name + " Strich " + n + ": falsche Richtung – gelöscht.";
      break;
    case kana::StrokeResult::OtherStroke:
      msg = name + ": Das war Strich " + std::to_string(res.matched + 1) +
            " – zuerst kommt Strich " + n + ". Gelöscht.";
      break;
    default:
      msg = name + " Strich " + n + " passt nicht – gelöscht.";
      break;
  }
  current_.clear();
  cell.ink.clear();
  cell.checker.reset();
  cell.done = false;
  if (activeLine_ == line && activeCell_ == c) {
    activeLine_ = activeCell_ = -1;
  }
  sheetHintLine_ = line;
  sheetHintStroke_ = res.expected;
  invalidate(drawSheetLine(line), Refresh::Ui);
  sheetMessage(msg, true);
}

// ------------------------------------------------------------------- quiz --

namespace {

const Rect kQuizBig = Rect::xywh((W - 600) / 2, 220, 600, 600);
constexpr int kQuizOptY = 930, kQuizOptW = 408, kQuizOptH = 430, kQuizOptGap = 30;
constexpr int64_t kQuizAdvanceMs = 900;
constexpr int kQuizFlashEvery = 8;

Rect
quizOptionRect(int i) {
  return Rect::xywh(60 + i * (kQuizOptW + kQuizOptGap), kQuizOptY, kQuizOptW, kQuizOptH);
}

} // namespace

std::string
App::quizLabel(const kana::Kana& k) const {
  if (k.romaji == "-") {
    return "Langvokal";
  }
  if (kana::groups()[k.group].name == "klein") {
    return "klein " + k.romaji;
  }
  return k.romaji;
}

std::vector<std::string>
App::quizOptionLabels() const {
  std::vector<std::string> out;
  for (int i : quizOptions_) {
    const auto& k = kana::all()[i];
    out.push_back(quizShowKana_ ? quizLabel(k) : k.glyph);
  }
  return out;
}

std::string
App::quizTargetLabel() const {
  if (quizEnd_ || quizPos_ >= quizQueue_.size()) {
    return "";
  }
  const auto& k = kana::all()[quizQueue_[quizPos_]];
  return quizShowKana_ ? k.glyph : quizLabel(k);
}

void
App::startQuiz(std::vector<int> queue) {
  if (queue.empty()) {
    return;
  }
  quizSeed_ = quizSeed_ * 2654435761u + static_cast<uint32_t>(platform_.nowMs());
  std::mt19937 rng(quizSeed_);
  std::shuffle(queue.begin(), queue.end(), rng);
  quizQueue_ = std::move(queue);
  quizPos_ = 0;
  quizRight_ = quizWrong_ = 0;
  quizMistakes_.clear();
  quizEnd_ = false;
  quizAdvanceAt_ = -1;
  quizSinceFlash_ = 0;
  message_.clear();
  makeQuizQuestion();
  show(Screen::Quiz);
}

void
App::makeQuizQuestion() {
  std::mt19937 rng(quizSeed_ + static_cast<uint32_t>(quizPos_) * 7919u);
  const int target = quizQueue_[quizPos_];
  const auto& tk = kana::all()[target];
  const std::string label = quizLabel(tk);
  quizShowKana_ = (rng() & 1u) != 0;

  // Wrong answers: other kana of the same script with a different reading,
  // so there's never a second right answer.
  std::vector<int> wrong;
  std::vector<std::string> seen{ label };
  for (const auto& k : kana::all()) {
    const std::string l = quizLabel(k);
    if (k.script == tk.script && std::find(seen.begin(), seen.end(), l) == seen.end()) {
      seen.push_back(l);
      wrong.push_back(k.index);
    }
  }
  std::shuffle(wrong.begin(), wrong.end(), rng);
  quizOptions_ = { target, wrong[0], wrong[1] };
  std::shuffle(quizOptions_.begin(), quizOptions_.end(), rng);
  quizCorrect_ = static_cast<int>(
    std::find(quizOptions_.begin(), quizOptions_.end(), target) - quizOptions_.begin());
  quizChosen_ = -1;
}

void
App::answerQuiz(int option) {
  if (quizEnd_) {
    return;
  }
  if (quizChosen_ >= 0) {
    nextQuizQuestion(); // tapping again after the answer moves on
    return;
  }
  quizChosen_ = option;
  const auto& k = kana::all()[quizQueue_[quizPos_]];
  const bool ok = option == quizCorrect_;
  if (ok) {
    quizRight_++;
    message_ = "Richtig!";
    quizAdvanceAt_ = platform_.nowMs() + kQuizAdvanceMs;
  } else {
    quizWrong_++;
    if (std::find(quizMistakes_.begin(), quizMistakes_.end(), k.index) == quizMistakes_.end()) {
      quizMistakes_.push_back(k.index);
    }
    message_ = quizShowKana_ ? "Falsch – richtig ist „" + quizLabel(k) + "“."
                             : "Falsch – richtig ist das schwarze Feld.";
  }
  std::fprintf(stderr, "quiz %s (%s) -> %s\n", k.glyph.c_str(),
               quizShowKana_ ? "Zeichen gezeigt" : "Lautschrift gezeigt", ok ? "richtig" : "falsch");
  redraw();
  invalidate({ 0, kQuizOptY - 10, W, H }, Refresh::Ui);
}

void
App::nextQuizQuestion() {
  quizAdvanceAt_ = -1;
  if (quizEnd_) {
    return;
  }
  quizPos_++;
  message_.clear();
  if (quizPos_ >= quizQueue_.size()) {
    quizEnd_ = true;
    redraw();
    invalidate(canvas_.rect(), Refresh::Full);
    return;
  }
  makeQuizQuestion();
  redraw();
  const bool flash = ++quizSinceFlash_ >= kQuizFlashEvery;
  if (flash) {
    quizSinceFlash_ = 0;
  }
  invalidate(canvas_.rect(), flash ? Refresh::Full : Refresh::Ui);
}

void
App::drawQuizOption(int i, Rect r) {
  const auto& k = kana::all()[quizOptions_[i]];
  const bool answered = quizChosen_ >= 0;
  const bool right = answered && i == quizCorrect_;
  const bool chosenWrong = answered && i == quizChosen_ && !right;
  const uint8_t bg = right ? gfx::kBlack : chosenWrong ? gfx::kFaint : gfx::kWhite;
  const uint8_t fg = right ? gfx::kWhite : answered ? gfx::kMid : gfx::kBlack;
  canvas_.roundRect(r, 24, bg, 4, answered && !right ? gfx::kMid : gfx::kBlack);
  if (quizShowKana_) {
    const std::string label = quizLabel(k);
    // Kanji meanings: one size for all three answers, so a long word
    // doesn't look less likely. Kana keep their own size per answer.
    std::vector<std::string> sizeFor{ label };
    if (k.script == kana::Script::Kanji) {
      sizeFor.clear();
      for (int o : quizOptions_) {
        sizeFor.push_back(quizLabel(kana::all()[o]));
      }
    }
    int size = 32;
    for (int s : { 72, 52, 40 }) {
      if (std::all_of(sizeFor.begin(), sizeFor.end(), [&](const std::string& l) {
            return fonts_.textWidth(FontId::Bold, s, l) <= r.w() - 40;
          })) {
        size = s;
        break;
      }
    }
    canvas_.textCentered(fonts_, FontId::Bold, size, label, r.cx(), r.cy() + size * 35 / 100,
                         fg);
  } else {
    const int box = 300;
    const Rect b = Rect::xywh(r.cx() - box / 2, r.cy() - box / 2 - 10, box, box);
    drawKanaStrokes(k, b, static_cast<int>(k.strokes.size()), -1, 13.f, fg, fg);
  }
  if (right) {
    drawCheckMark(r.x1 - 52, r.y0 + 48, 44, gfx::kWhite);
  }
  if (chosenWrong) {
    canvas_.textCentered(fonts_, FontId::Regular, 26, "deine Wahl", r.cx(), r.y1 - 24,
                         gfx::kDark);
  }
  buttons_.push_back({ r, [this, i] { answerQuiz(i); } });
}

void
App::drawQuiz() {
  if (quizEnd_) {
    drawQuizEnd();
    return;
  }
  drawHeader("Zuordnen · " + scriptName(script_),
             std::to_string(quizPos_ + 1) + " / " + std::to_string(quizQueue_.size()));
  const auto& k = kana::all()[quizQueue_[quizPos_]];
  const bool kanji = k.script == kana::Script::Kanji;
  canvas_.roundRect(kQuizBig, 24, gfx::kFaint, 2, gfx::kLight);
  if (quizShowKana_) {
    canvas_.textCentered(fonts_, FontId::Regular, 40,
                         kanji ? "Was bedeutet dieses Zeichen?" : "Wie liest man dieses Zeichen?",
                         W / 2, 186, gfx::kDark);
    drawKanaStrokes(k, kQuizBig.inset(30), static_cast<int>(k.strokes.size()), -1, 22.f,
                    gfx::kBlack, gfx::kBlack);
  } else {
    canvas_.textCentered(fonts_, FontId::Regular, 40,
                         kanji ? "Welches Zeichen bedeutet das?" : "Welches Zeichen ist das?",
                         W / 2, 186, gfx::kDark);
    const std::string label = quizLabel(k);
    int size = 52;
    for (int s : { 110, 72 }) {
      if (fonts_.textWidth(FontId::Bold, s, label) <= kQuizBig.w() - 40) {
        size = s;
        break;
      }
    }
    canvas_.textCentered(fonts_, FontId::Bold, size, label, W / 2,
                         kQuizBig.cy() + size * 35 / 100, gfx::kBlack);
  }
  if (kanji && quizChosen_ >= 0) {
    // After the answer: how the kanji is read.
    canvas_.textCentered(fonts_, FontId::Kana, 44, k.glyph + "  " + k.readings, W / 2, 884,
                         gfx::kDark);
  }
  for (int i = 0; i < 3; i++) {
    drawQuizOption(i, quizOptionRect(i));
  }
  drawFeedback(message_.empty() ? "Tippe auf die richtige Antwort." : message_,
               !message_.empty());
  canvas_.text(fonts_, FontId::Regular, 40,
               std::to_string(quizRight_) + " richtig · " + std::to_string(quizWrong_) + " falsch",
               60, 1796, gfx::kBlack);
  if (quizChosen_ >= 0) {
    drawButton(Rect::xywh(952, 1722, 400, 120), "Weiter ›", true,
               [this] { nextQuizQuestion(); });
  }
}

void
App::drawQuizEnd() {
  drawHeader("Zuordnen · " + scriptName(script_), "");
  const int n = quizRight_ + quizWrong_;
  drawCheckMark(W / 2, 270, 150, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Bold, 72, "Runde beendet", W / 2, 470, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Regular, 40,
                       std::to_string(quizRight_) + " von " + std::to_string(n) + " richtig",
                       W / 2, 570, gfx::kBlack);
  if (!quizMistakes_.empty()) {
    canvas_.textCentered(fonts_, FontId::Bold, 40, "Diese Zeichen noch einmal üben:", W / 2,
                         700, gfx::kBlack);
    const int per = 8, cell = 140;
    const int count = std::min<int>(static_cast<int>(quizMistakes_.size()), 40);
    for (int i = 0; i < count; i++) {
      const int rowN = std::min(per, count - (i / per) * per);
      const int x0 = W / 2 - rowN * cell / 2;
      canvas_.textCentered(fonts_, FontId::Kana, 96, kana::all()[quizMistakes_[i]].glyph,
                           x0 + (i % per) * cell + cell / 2, 840 + (i / per) * 140,
                           gfx::kBlack);
    }
    const std::vector<int> wrong = quizMistakes_;
    drawButton(Rect::xywh(60, 1540, 1284, 130), "Fehler nochmal", true,
               [this, wrong] { startQuiz(wrong); });
  } else {
    canvas_.textCentered(fonts_, FontId::Bold, 52, "Alles richtig – sehr gut!", W / 2, 720,
                         gfx::kBlack);
  }
  drawButton(Rect::xywh(60, 1700, 630, 130), "Neue Runde", quizMistakes_.empty(),
             [this] { startQuiz(scriptKana()); });
  drawButton(Rect::xywh(714, 1700, 630, 130), "Menü", false, [this] { show(Screen::Home); });
}

// ------------------------------------------------------------------ input --

void
App::penDown(float x, float y) {
  lastInputMs_ = platform_.nowMs();
  int line = -1, cell = -1;
  if (screen_ == Screen::Sheet && sheetHit(x, y, line, cell)) {
    pressedButton_ = -1;
    if (cell < 1 || sheet_[line].cells[cell].done) {
      return; // the model and finished kana can't be written over
    }
    const int current = sheetCurrentLine();
    if (line > current) {
      sheetMessage("Erst Zeile " + kanaName(kana::all()[sheet_[current].kana]) +
                     " komplett richtig schreiben.",
                   true);
      return;
    }
    if (sheetMessageShown_) {
      sheetMessage(kSheetHelp, false);
    }
    sheetPen_ = true;
    sheetPenLine_ = line;
    sheetPenCell_ = cell;
    current_ = { { x, y } };
    invalidate(canvas_.disk(x, y, kSheetInk / 2, gfx::kBlack), Refresh::Ink);
    return;
  }
  if (screen_ == Screen::Practice && inBox(x, y)) {
    if (kanaDone_) {
      if (mode_ == Mode::Test) {
        return;
      }
      // Writing again after finishing = another round on the same kana.
      loadKana();
      drawPrompt();
      drawBox();
      invalidate({ 0, 118, W, 462 }, Refresh::Ui);
      invalidate(kBox, Refresh::Ui);
      drawFeedback("Noch einmal.", false);
    }
    autoAdvanceAt_ = -1;
    clearHint();
    penInBox_ = true;
    current_ = { { x, y } };
    invalidate(canvas_.disk(x, y, kInkWidth / 2, gfx::kBlack), Refresh::Ink);
    return;
  }
  pressedButton_ = -1;
  for (size_t i = 0; i < buttons_.size(); i++) {
    if (buttons_[i].r.contains(int(x), int(y))) {
      pressedButton_ = static_cast<int>(i);
    }
  }
}

void
App::penMove(float x, float y) {
  if (sheetPen_) {
    const Rect r = sheetCellRect(sheetPenLine_, sheetPenCell_).inset(-kSheetSpill);
    x = std::max(float(r.x0), std::min(float(r.x1 - 1), x));
    y = std::max(float(r.y0), std::min(float(r.y1 - 1), y));
    const auto [px, py] = current_.back();
    if (std::hypot(x - px, y - py) < 1.f) {
      return;
    }
    current_.emplace_back(x, y);
    invalidate(canvas_.line(px, py, x, y, kSheetInk, gfx::kBlack), Refresh::Ink);
    return;
  }
  if (!penInBox_) {
    return;
  }
  const Rect inner = kBox.inset(6);
  x = std::max(float(inner.x0), std::min(float(inner.x1 - 1), x));
  y = std::max(float(inner.y0), std::min(float(inner.y1 - 1), y));
  const auto [px, py] = current_.back();
  if (std::hypot(x - px, y - py) < 1.5f) {
    return;
  }
  current_.emplace_back(x, y);
  invalidate(canvas_.line(px, py, x, y, kInkWidth, gfx::kBlack), Refresh::Ink);
}

void
App::penUp(float x, float y) {
  lastInputMs_ = platform_.nowMs();
  if (sheetPen_) {
    sheetPen_ = false;
    handleSheetStroke();
    return;
  }
  if (penInBox_) {
    penInBox_ = false;
    handleStroke();
    return;
  }
  if (pressedButton_ >= 0 && pressedButton_ < static_cast<int>(buttons_.size()) &&
      buttons_[pressedButton_].r.contains(int(x), int(y))) {
    auto action = buttons_[pressedButton_].action;
    pressedButton_ = -1;
    action();
  }
  pressedButton_ = -1;
}

void
App::touchTap(int x, int y) {
  lastInputMs_ = platform_.nowMs();
  if (penInBox_ || (screen_ == Screen::Practice && inBox(float(x), float(y)))) {
    return; // palm on the writing area
  }
  if (sheetPen_ || (screen_ == Screen::Sheet && sheetGrid().contains(x, y))) {
    return;
  }
  for (const auto& b : buttons_) {
    if (b.r.contains(x, y)) {
      auto action = b.action;
      action();
      return;
    }
  }
}

void
App::touchSwipe(int dx, int dy) {
  lastInputMs_ = platform_.nowMs();
  if (screen_ != Screen::Sheet || sheetPen_) {
    return;
  }
  if (std::abs(dy) < 150 || std::abs(dy) < 2 * std::abs(dx)) {
    return; // not a clear vertical swipe
  }
  sheetPage(dy < 0 ? 1 : -1); // finger up = further down the sheet
}

void
App::powerButton() {
  sleep();
}

void
App::sleep() {
  penInBox_ = false;
  sheetPen_ = false;
  sheetPenLine_ = sheetPenCell_ = -1;
  current_.clear();
  canvas_.fill(canvas_.rect(), gfx::kWhite);
  canvas_.textCentered(fonts_, FontId::Kana, 150, "かな道場", W / 2, 820, gfx::kBlack);
  canvas_.textCentered(fonts_, FontId::Regular, 52, "schläft", W / 2, 960, gfx::kDark);
  canvas_.textCentered(fonts_, FontId::Regular, 32,
                       "Zum Aufwecken den Einschaltknopf drücken", W / 2, 1040, gfx::kDark);
  pending_.clear();
  invalidate(canvas_.rect(), Refresh::Full);
  flush();
  platform_.suspend();
  lastInputMs_ = platform_.nowMs();
  redraw();
  invalidate(canvas_.rect(), Refresh::Full);
}

void
App::tick() {
  const int64_t now = platform_.nowMs();
  // Timers only act on the screen that set them (the menu may have been
  // opened in the meantime).
  if (autoAdvanceAt_ >= 0 && now >= autoAdvanceAt_) {
    if (screen_ == Screen::Practice) {
      nextKana();
    } else {
      autoAdvanceAt_ = -1;
    }
  }
  if (quizAdvanceAt_ >= 0 && now >= quizAdvanceAt_) {
    quizAdvanceAt_ = -1;
    if (screen_ == Screen::Quiz) {
      nextQuizQuestion();
    }
  }
  if (now - lastInputMs_ >= kSleepAfterMs) {
    sleep();
  }
}

int64_t
App::msUntilTimer() const {
  const int64_t now = platform_.nowMs();
  int64_t next = lastInputMs_ + kSleepAfterMs - now;
  if (autoAdvanceAt_ >= 0) {
    next = std::min(next, autoAdvanceAt_ - now);
  }
  if (quizAdvanceAt_ >= 0) {
    next = std::min(next, quizAdvanceAt_ - now);
  }
  return std::max<int64_t>(0, next);
}

} // namespace app
