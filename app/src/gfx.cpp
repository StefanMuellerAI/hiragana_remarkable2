#include "gfx.h"

#include <cmath>
#include <cstring>
#include <fstream>

namespace gfx {
namespace {

template<typename T>
T
readLE(const uint8_t* p) {
  T v;
  std::memcpy(&v, p, sizeof(T)); // device and host are both little endian
  return v;
}

float
segmentDistance(float px, float py, float ax, float ay, float bx, float by) {
  const float dx = bx - ax, dy = by - ay;
  const float len2 = dx * dx + dy * dy;
  float t = len2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / len2 : 0.f;
  t = std::max(0.f, std::min(1.f, t));
  return std::hypot(px - (ax + t * dx), py - (ay + t * dy));
}

} // namespace

std::vector<uint32_t>
decodeUtf8(std::string_view s) {
  std::vector<uint32_t> out;
  for (size_t i = 0; i < s.size();) {
    const auto c = static_cast<uint8_t>(s[i]);
    uint32_t cp = c;
    int extra = 0;
    if (c >= 0xF0) {
      cp = c & 0x07;
      extra = 3;
    } else if (c >= 0xE0) {
      cp = c & 0x0F;
      extra = 2;
    } else if (c >= 0xC0) {
      cp = c & 0x1F;
      extra = 1;
    }
    i++;
    for (int k = 0; k < extra && i < s.size(); k++, i++) {
      cp = (cp << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);
    }
    out.push_back(cp);
  }
  return out;
}

bool
Fonts::load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    return false;
  }
  data_.assign(std::istreambuf_iterator<char>(in), {});
  if (data_.size() < 8 || std::memcmp(data_.data(), "KGLY", 4) != 0) {
    return false;
  }
  const uint8_t* p = data_.data() + 4;
  const uint32_t count = readLE<uint32_t>(p);
  p += 4;

  struct Pending {
    size_t face;
    size_t glyph;
    uint32_t offset;
  };
  std::vector<Pending> pending;
  for (uint32_t f = 0; f < count; f++) {
    Face face;
    face.id = static_cast<FontId>(p[0]);
    face.size = readLE<uint16_t>(p + 2);
    face.ascent = readLE<int16_t>(p + 4);
    face.lineHeight = readLE<int16_t>(p + 6);
    const uint32_t n = readLE<uint32_t>(p + 8);
    p += 12;
    for (uint32_t g = 0; g < n; g++) {
      Glyph glyph;
      const uint32_t cp = readLE<uint32_t>(p);
      glyph.w = readLE<uint16_t>(p + 4);
      glyph.h = readLE<uint16_t>(p + 6);
      glyph.xoff = readLE<int16_t>(p + 8);
      glyph.yoff = readLE<int16_t>(p + 10);
      glyph.advance = readLE<uint16_t>(p + 12);
      pending.push_back({ faces_.size(), face.glyphs.size(), readLE<uint32_t>(p + 16) });
      face.glyphs.emplace_back(cp, glyph);
      p += 20;
    }
    faces_.push_back(std::move(face));
  }
  const uint8_t* bitmaps = p;
  for (const auto& pd : pending) {
    faces_[pd.face].glyphs[pd.glyph].second.bitmap = bitmaps + pd.offset;
  }
  for (auto& face : faces_) {
    std::sort(face.glyphs.begin(), face.glyphs.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
  }
  return true;
}

const Fonts::Face*
Fonts::face(FontId id, int size) const {
  const Face* best = nullptr;
  for (const auto& f : faces_) {
    if (f.id != id) {
      continue;
    }
    if (best == nullptr || std::abs(f.size - size) < std::abs(best->size - size)) {
      best = &f;
    }
  }
  return best;
}

const Fonts::Glyph*
Fonts::glyph(FontId id, int size, uint32_t cp) const {
  const Face* f = face(id, size);
  if (f == nullptr) {
    return nullptr;
  }
  auto it = std::lower_bound(
    f->glyphs.begin(), f->glyphs.end(), cp,
    [](const auto& g, uint32_t c) { return g.first < c; });
  if (it == f->glyphs.end() || it->first != cp) {
    return nullptr;
  }
  return &it->second;
}

int
Fonts::textWidth(FontId id, int size, std::string_view utf8) const {
  int w = 0;
  for (uint32_t cp : decodeUtf8(utf8)) {
    if (const Glyph* g = glyph(id, size, cp)) {
      w += g->advance;
    }
  }
  return w;
}

int
Fonts::ascent(FontId id, int size) const {
  const Face* f = face(id, size);
  return f ? f->ascent : size;
}

void
Canvas::fill(Rect r, uint8_t v) {
  r = r.clipped(rect());
  for (int y = r.y0; y < r.y1; y++) {
    std::memset(&px_[static_cast<size_t>(y) * w_ + r.x0], v, r.w());
  }
}

void
Canvas::frame(Rect r, int t, uint8_t v) {
  fill({ r.x0, r.y0, r.x1, r.y0 + t }, v);
  fill({ r.x0, r.y1 - t, r.x1, r.y1 }, v);
  fill({ r.x0, r.y0, r.x0 + t, r.y1 }, v);
  fill({ r.x1 - t, r.y0, r.x1, r.y1 }, v);
}

void
Canvas::roundRect(Rect r, int radius, uint8_t fillV, int border, uint8_t borderV) {
  for (int y = std::max(0, r.y0); y < std::min(h_, r.y1); y++) {
    for (int x = std::max(0, r.x0); x < std::min(w_, r.x1); x++) {
      // Distance outside the rounded corner, if any.
      const float cx = std::min(std::max(float(x) + 0.5f, float(r.x0 + radius)),
                                float(r.x1 - radius));
      const float cy = std::min(std::max(float(y) + 0.5f, float(r.y0 + radius)),
                                float(r.y1 - radius));
      const float d = std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy);
      if (d > radius) {
        continue;
      }
      const bool edge = d > radius - border || x < r.x0 + border ||
                        x >= r.x1 - border || y < r.y0 + border ||
                        y >= r.y1 - border;
      px_[static_cast<size_t>(y) * w_ + x] = edge ? borderV : fillV;
    }
  }
}

void
Canvas::blend(int x, int y, uint8_t v, float a) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_ || a <= 0.f) {
    return;
  }
  auto& p = px_[static_cast<size_t>(y) * w_ + x];
  if (a >= 1.f) {
    p = v;
    return;
  }
  p = static_cast<uint8_t>(std::lround(p * (1.f - a) + v * a));
}

Rect
Canvas::line(float x0, float y0, float x1, float y1, float width, uint8_t v,
             bool aa) {
  const float r = width / 2.f;
  Rect box{ int(std::floor(std::min(x0, x1) - r - 1)),
            int(std::floor(std::min(y0, y1) - r - 1)),
            int(std::ceil(std::max(x0, x1) + r + 2)),
            int(std::ceil(std::max(y0, y1) + r + 2)) };
  box = box.clipped(rect());
  for (int y = box.y0; y < box.y1; y++) {
    for (int x = box.x0; x < box.x1; x++) {
      const float d = segmentDistance(float(x) + 0.5f, float(y) + 0.5f, x0, y0, x1, y1);
      if (aa) {
        blend(x, y, v, std::max(0.f, std::min(1.f, r + 0.5f - d)));
      } else if (d <= r) {
        px_[static_cast<size_t>(y) * w_ + x] = v;
      }
    }
  }
  return box;
}

Rect
Canvas::polyline(const std::vector<std::pair<float, float>>& pts, float width,
                 uint8_t v, bool aa) {
  Rect dirty;
  for (size_t i = 1; i < pts.size(); i++) {
    dirty = dirty.united(line(pts[i - 1].first, pts[i - 1].second, pts[i].first,
                              pts[i].second, width, v, aa));
  }
  if (pts.size() == 1) {
    dirty = disk(pts[0].first, pts[0].second, width / 2, v);
  }
  return dirty;
}

void
Canvas::dashedLine(int x0, int y0, int x1, int y1, int dash, int width, uint8_t v) {
  const float len = std::hypot(float(x1 - x0), float(y1 - y0));
  const int n = std::max(1, int(len / dash));
  for (int i = 0; i < n; i += 2) {
    const float a = float(i) / n, b = float(std::min(i + 1, n)) / n;
    const float ax = x0 + (x1 - x0) * a, ay = y0 + (y1 - y0) * a;
    const float bx = x0 + (x1 - x0) * b, by = y0 + (y1 - y0) * b;
    fill({ int(std::min(ax, bx)) - width / 2, int(std::min(ay, by)) - width / 2,
           int(std::max(ax, bx)) + (width + 1) / 2,
           int(std::max(ay, by)) + (width + 1) / 2 },
         v);
  }
}

Rect
Canvas::disk(float cx, float cy, float r, uint8_t v) {
  Rect box{ int(cx - r - 1), int(cy - r - 1), int(cx + r + 2), int(cy + r + 2) };
  box = box.clipped(rect());
  for (int y = box.y0; y < box.y1; y++) {
    for (int x = box.x0; x < box.x1; x++) {
      const float d = std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy);
      blend(x, y, v, std::max(0.f, std::min(1.f, r + 0.5f - d)));
    }
  }
  return box;
}

void
Canvas::ring(float cx, float cy, float r, float width, uint8_t v) {
  Rect box{ int(cx - r - width), int(cy - r - width), int(cx + r + width + 1),
            int(cy + r + width + 1) };
  box = box.clipped(rect());
  for (int y = box.y0; y < box.y1; y++) {
    for (int x = box.x0; x < box.x1; x++) {
      const float d = std::fabs(std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy) - r);
      blend(x, y, v, std::max(0.f, std::min(1.f, width / 2 + 0.5f - d)));
    }
  }
}

void
Canvas::triangle(float ax, float ay, float bx, float by, float cx, float cy,
                 uint8_t v) {
  const int minX = std::max(0, int(std::min({ ax, bx, cx })));
  const int maxX = std::min(w_ - 1, int(std::max({ ax, bx, cx })) + 1);
  const int minY = std::max(0, int(std::min({ ay, by, cy })));
  const int maxY = std::min(h_ - 1, int(std::max({ ay, by, cy })) + 1);
  auto edge = [](float x0, float y0, float x1, float y1, float x, float y) {
    return (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
  };
  const float area = edge(ax, ay, bx, by, cx, cy);
  if (std::fabs(area) < 1e-3f) {
    return;
  }
  for (int y = minY; y <= maxY; y++) {
    for (int x = minX; x <= maxX; x++) {
      const float px = float(x) + 0.5f, py = float(y) + 0.5f;
      const float w0 = edge(bx, by, cx, cy, px, py) / area;
      const float w1 = edge(cx, cy, ax, ay, px, py) / area;
      const float w2 = edge(ax, ay, bx, by, px, py) / area;
      if (w0 >= 0 && w1 >= 0 && w2 >= 0) {
        px_[static_cast<size_t>(y) * w_ + x] = v;
      }
    }
  }
}

Rect
Canvas::text(const Fonts& fonts, FontId id, int size, std::string_view utf8,
             int x, int y, uint8_t v) {
  Rect dirty;
  for (uint32_t cp : decodeUtf8(utf8)) {
    const auto* g = fonts.glyph(id, size, cp);
    if (g == nullptr) {
      continue;
    }
    const int gx = x + g->xoff, gy = y + g->yoff;
    for (int j = 0; j < g->h; j++) {
      for (int i = 0; i < g->w; i++) {
        const uint8_t a = g->bitmap[j * g->w + i];
        if (a != 0) {
          blend(gx + i, gy + j, v, a / 255.f);
        }
      }
    }
    dirty = dirty.united(Rect::xywh(gx, gy, g->w, g->h));
    x += g->advance;
  }
  return dirty.clipped(rect());
}

Rect
Canvas::textCentered(const Fonts& fonts, FontId id, int size,
                     std::string_view utf8, int cx, int y, uint8_t v) {
  return text(fonts, id, size, utf8, cx - fonts.textWidth(id, size, utf8) / 2, y, v);
}

} // namespace gfx
