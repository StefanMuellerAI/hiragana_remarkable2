#pragma once

#include "checker.h"
#include "gfx.h"
#include "kana.h"
#include "progress.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace app {

enum class Refresh {
  Ink,  // fast black/white pen update
  Ui,   // greyscale partial update without flashing
  Full, // full flash, clears ghosting
};

// Everything the app needs from the device (or from the test harness).
struct Platform {
  virtual ~Platform() = default;
  virtual int64_t nowMs() = 0;
  virtual int today() = 0;
  virtual int batteryPercent() = 0; // -1 if unknown
  virtual void push(const gfx::Canvas& canvas, gfx::Rect r, Refresh mode) = 0;
  virtual void suspend() = 0; // returns after wake-up
  virtual void powerOff() = 0;
  virtual void reboot() = 0;
  virtual void startOriginalSoftware() = 0;
};

class App {
public:
  App(Platform& platform, const gfx::Fonts& fonts, kana::Progress& progress);

  void start();

  void penDown(float x, float y);
  void penMove(float x, float y);
  void penUp(float x, float y);
  void touchTap(int x, int y);
  // Finger moved by (dx, dy) between touch-down and lift-off.
  void touchSwipe(int dx, int dy);
  void powerButton();
  void tick();

  // Milliseconds until the next timer wants tick(), or -1.
  int64_t msUntilTimer() const;

  // Sends all pending screen changes to the platform.
  void flush();

  const gfx::Canvas& canvas() const { return canvas_; }
  std::string screenName() const;

  // For the test harness.
  const std::string& message() const { return message_; }
  int sheetCorrect() const;
  int sheetTop() const { return sheetTop_; }

private:
  enum class Screen { Home, Practice, Summary, Progress, System, Sheet };
  enum class Mode { Learn, Practice, Test };

  struct Button {
    gfx::Rect r;
    std::function<void()> action;
  };

  struct Result {
    int kana;
    int mistakes;
    float quality;
  };

  // Navigation
  void show(Screen s);
  void redraw();
  void startSession(Mode mode, std::vector<int> queue);
  void startMode(Mode mode);
  void loadKana();
  void nextKana();
  void finishKana();
  void sleep();

  // Drawing
  void drawHome();
  void drawPractice();
  void drawPrompt();
  void drawBox();
  void drawFeedback(const std::string& msg, bool bold);
  void drawSummary();
  void drawProgress();
  void drawSystem();
  void drawHeader(const std::string& title, const std::string& right);
  void drawButton(gfx::Rect r, const std::string& label, bool primary,
                  std::function<void()> action, const std::string& sub = "");
  void drawKanaStrokes(const kana::Kana& k, gfx::Rect box, int upto,
                       int highlight, float width, uint8_t done, uint8_t cur);
  void drawRefStroke(const kana::Line& ref, float width, uint8_t v, bool arrow);
  void drawRefStrokeIn(const kana::Line& ref, gfx::Rect box, float width, uint8_t v,
                       bool arrow);
  void drawCheckMark(int cx, int cy, int size, uint8_t v);
  void invalidate(gfx::Rect r, Refresh mode);

  // Practice helpers
  bool inBox(float x, float y) const;
  kana::Pt toRef(float x, float y) const;
  std::pair<float, float> toScreen(kana::Pt p) const;
  void clearHint();
  void handleStroke();
  std::string promptLabel(const kana::Kana& k) const;
  std::vector<int> selectedKana() const;
  int dueCount() const;

  // Writing sheet ("Schreibblatt"): one line per kana, the model on the
  // left and empty cells to write it again and again. Only correctly
  // written kana stay; nothing is saved when the sheet is left.
  struct SheetCell {
    std::vector<std::vector<std::pair<float, float>>> ink; // screen coords
    std::unique_ptr<kana::Checker> checker;
    bool done = false;
  };
  struct SheetLine {
    int kana = 0;
    std::vector<SheetCell> cells; // [0] is the model
  };
  void openSheet();
  void drawSheet();
  gfx::Rect drawSheetLine(int line);
  gfx::Rect sheetCellRect(int line, int cell) const;
  gfx::Rect sheetBand(int line) const;
  gfx::Rect sheetGrid() const;
  bool sheetHit(float x, float y, int& line, int& cell) const;
  bool sheetVisible(int line) const;
  void sheetPage(int delta);
  void eraseSheetCell(int line, int cell);
  void handleSheetStroke();
  void sheetMessage(const std::string& msg, bool bold);
  std::string kanaName(const kana::Kana& k) const;

  Platform& platform_;
  const gfx::Fonts& fonts_;
  kana::Progress& progress_;
  gfx::Canvas canvas_;

  Screen screen_ = Screen::Home;
  std::vector<Button> buttons_;
  int pressedButton_ = -1;

  struct Pending {
    gfx::Rect r;
    Refresh mode;
  };
  std::vector<Pending> pending_;

  // Home selection
  kana::Script script_ = kana::Script::Hiragana;
  std::vector<bool> groupSelected_;

  // Practice session
  Mode mode_ = Mode::Learn;
  std::vector<int> queue_;
  size_t pos_ = 0;
  std::unique_ptr<kana::Checker> checker_;
  std::vector<std::vector<std::pair<float, float>>> inked_; // screen coords
  std::vector<std::pair<float, float>> current_;
  bool penInBox_ = false;
  std::vector<std::pair<float, float>> wrongStroke_;
  int hintStroke_ = -1;
  bool hintUsed_ = false;
  bool kanaDone_ = false;
  std::vector<Result> results_;
  std::vector<int> requeued_;
  int64_t autoAdvanceAt_ = -1;
  int kanaSinceFlash_ = 0;

  int64_t lastInputMs_ = 0;
  std::string homeNotice_;
  Mode summaryMode_ = Mode::Learn;
  std::string message_; // last feedback line

  // Writing sheet
  std::vector<SheetLine> sheet_;
  int sheetTop_ = 0;             // first visible line
  bool sheetPen_ = false;        // a stroke is being written on the sheet
  int sheetPenLine_ = -1, sheetPenCell_ = -1;
  int activeLine_ = -1, activeCell_ = -1; // cell with an unfinished kana
  int sheetHintLine_ = -1, sheetHintStroke_ = -1;
  bool sheetMessageShown_ = false; // an error message is on screen
  int sheetPagesSinceFlash_ = 0;
};

} // namespace app
