#include "kana.h"

#include <cstdint>

namespace kana {
namespace {

struct KanaPoint {
  float x, y;
};
struct KanaStrokeDef {
  uint16_t first, count;
  float lx, ly;
};
struct KanaDef {
  const char* glyph;
  const char* romaji;
  uint8_t script, group;
  uint16_t firstStroke, strokeCount;
};
struct KanaGroupDef {
  const char* name;
  uint8_t script;
  uint16_t firstKana, count;
};

#include "kana_data.inc"

struct Tables {
  std::vector<Kana> kana;
  std::vector<Group> groups;

  Tables() {
    for (const auto& def : kKanaDefs) {
      Kana k;
      k.index = static_cast<int>(kana.size());
      k.glyph = def.glyph;
      k.romaji = def.romaji;
      k.script = static_cast<Script>(def.script);
      k.group = def.group;
      for (int s = 0; s < def.strokeCount; s++) {
        const auto& sd = kKanaStrokes[def.firstStroke + s];
        Stroke stroke;
        stroke.label = { sd.lx, sd.ly };
        for (int p = 0; p < sd.count; p++) {
          const auto& kp = kKanaPoints[sd.first + p];
          stroke.points.push_back({ kp.x, kp.y });
        }
        k.strokes.push_back(std::move(stroke));
      }
      kana.push_back(std::move(k));
    }
    for (const auto& def : kKanaGroups) {
      Group g;
      g.index = static_cast<int>(groups.size());
      g.name = def.name;
      g.script = static_cast<Script>(def.script);
      for (int i = 0; i < def.count; i++) {
        g.kana.push_back(def.firstKana + i);
      }
      groups.push_back(std::move(g));
    }
  }
};

const Tables&
tables() {
  static const Tables t;
  return t;
}

} // namespace

const std::vector<Kana>&
all() {
  return tables().kana;
}

const std::vector<Group>&
groups() {
  return tables().groups;
}

const Kana*
find(const std::string& glyph) {
  for (const auto& k : all()) {
    if (k.glyph == glyph) {
      return &k;
    }
  }
  return nullptr;
}

float
length(const Line& line) {
  float total = 0;
  for (size_t i = 1; i < line.size(); i++) {
    total += dist(line[i - 1], line[i]);
  }
  return total;
}

Pt
centroid(const Line& line) {
  Pt c;
  if (line.empty()) {
    return c;
  }
  for (const auto& p : line) {
    c = c + p;
  }
  return c * (1.f / static_cast<float>(line.size()));
}

Line
resample(const Line& line, int n) {
  Line out;
  if (line.empty() || n < 2) {
    return out;
  }
  const float total = length(line);
  if (total <= 0.f || line.size() == 1) {
    return Line(n, line.front());
  }
  out.reserve(n);
  out.push_back(line.front());
  const float step = total / static_cast<float>(n - 1);
  float carried = 0; // distance already walked along the current segment
  size_t i = 1;
  float need = step;
  while (static_cast<int>(out.size()) < n - 1 && i < line.size()) {
    const float seg = dist(line[i - 1], line[i]) - carried;
    if (seg >= need) {
      carried += need;
      const float segLen = dist(line[i - 1], line[i]);
      const float t = segLen > 0 ? carried / segLen : 0.f;
      out.push_back(line[i - 1] + (line[i] - line[i - 1]) * t);
      need = step;
    } else {
      need -= seg;
      carried = 0;
      i++;
    }
  }
  while (static_cast<int>(out.size()) < n) {
    out.push_back(line.back());
  }
  out.back() = line.back();
  return out;
}

} // namespace kana
