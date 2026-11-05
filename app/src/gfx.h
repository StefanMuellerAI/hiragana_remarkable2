#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gfx {

constexpr int kWidth = 1404;
constexpr int kHeight = 1872;

constexpr uint8_t kBlack = 0x00;
constexpr uint8_t kDark = 0x50;
constexpr uint8_t kMid = 0x90;
constexpr uint8_t kLight = 0xC8;
constexpr uint8_t kFaint = 0xE4;
constexpr uint8_t kWhite = 0xFF;

// Half-open rectangle [x0, x1) x [y0, y1).
struct Rect {
  int x0 = 0, y0 = 0, x1 = 0, y1 = 0;

  static Rect xywh(int x, int y, int w, int h) { return { x, y, x + w, y + h }; }
  int w() const { return x1 - x0; }
  int h() const { return y1 - y0; }
  bool empty() const { return x1 <= x0 || y1 <= y0; }
  bool contains(int x, int y) const {
    return x >= x0 && x < x1 && y >= y0 && y < y1;
  }
  Rect united(const Rect& o) const {
    if (empty()) {
      return o;
    }
    if (o.empty()) {
      return *this;
    }
    return { std::min(x0, o.x0),
             std::min(y0, o.y0),
             std::max(x1, o.x1),
             std::max(y1, o.y1) };
  }
  Rect clipped(const Rect& o) const {
    return { std::max(x0, o.x0),
             std::max(y0, o.y0),
             std::min(x1, o.x1),
             std::min(y1, o.y1) };
  }
  Rect inset(int d) const { return { x0 + d, y0 + d, x1 - d, y1 - d }; }
  int cx() const { return (x0 + x1) / 2; }
  int cy() const { return (y0 + y1) / 2; }
};

enum class FontId : uint8_t { Regular = 0, Bold = 1, Kana = 2 };

// Pre-rendered 8-bit alpha glyphs, see tools/gen_glyphs.py.
class Fonts {
public:
  bool load(const std::string& path);

  struct Glyph {
    uint16_t w = 0, h = 0;
    int16_t xoff = 0, yoff = 0;
    uint16_t advance = 0;
    const uint8_t* bitmap = nullptr;
  };
  const Glyph* glyph(FontId id, int size, uint32_t cp) const;
  int textWidth(FontId id, int size, std::string_view utf8) const;
  int ascent(FontId id, int size) const;

private:
  struct Face {
    FontId id;
    int size, ascent, lineHeight;
    std::vector<std::pair<uint32_t, Glyph>> glyphs; // sorted by codepoint
  };
  const Face* face(FontId id, int size) const;

  std::vector<uint8_t> data_;
  std::vector<Face> faces_;
};

std::vector<uint32_t> decodeUtf8(std::string_view s);

class Canvas {
public:
  Canvas(int w = kWidth, int h = kHeight)
    : w_(w), h_(h), px_(static_cast<size_t>(w) * h, kWhite) {}

  int width() const { return w_; }
  int height() const { return h_; }
  Rect rect() const { return { 0, 0, w_, h_ }; }
  const uint8_t* data() const { return px_.data(); }
  uint8_t at(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x]; }

  void fill(Rect r, uint8_t v);
  void frame(Rect r, int thickness, uint8_t v);
  void roundRect(Rect r, int radius, uint8_t fillV, int border, uint8_t borderV);

  // Thick line with round caps. `aa` blends the edge for grey-level output.
  Rect line(float x0, float y0, float x1, float y1, float width, uint8_t v,
            bool aa = false);
  Rect polyline(const std::vector<std::pair<float, float>>& pts, float width,
                uint8_t v, bool aa = false);
  void dashedLine(int x0, int y0, int x1, int y1, int dash, int width,
                  uint8_t v);
  Rect disk(float cx, float cy, float r, uint8_t v);
  void ring(float cx, float cy, float r, float width, uint8_t v);
  void triangle(float ax, float ay, float bx, float by, float cx, float cy,
                uint8_t v);

  // Draws text with its baseline at y. Returns the drawn rect.
  Rect text(const Fonts& fonts, FontId id, int size, std::string_view utf8,
            int x, int y, uint8_t v);
  Rect textCentered(const Fonts& fonts, FontId id, int size,
                    std::string_view utf8, int cx, int y, uint8_t v);

private:
  void blend(int x, int y, uint8_t v, float alpha);

  int w_, h_;
  std::vector<uint8_t> px_;
};

} // namespace gfx
