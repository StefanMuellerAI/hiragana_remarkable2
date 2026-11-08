#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace kana {

struct Pt {
  float x = 0, y = 0;
};

inline Pt operator+(Pt a, Pt b) { return { a.x + b.x, a.y + b.y }; }
inline Pt operator-(Pt a, Pt b) { return { a.x - b.x, a.y - b.y }; }
inline Pt operator*(Pt a, float s) { return { a.x * s, a.y * s }; }
inline float dist(Pt a, Pt b) { return std::hypot(a.x - b.x, a.y - b.y); }

using Line = std::vector<Pt>;

// Reference glyphs live in KanjiVG's 109x109 coordinate space.
constexpr float kGlyphSize = 109.f;

enum class Script { Hiragana = 0, Katakana = 1, Kanji = 2 };

struct Stroke {
  Line points;
  Pt label; // where KanjiVG places the stroke number
};

// A character to write: a kana or (Script::Kanji) a kanji.
struct Kana {
  int index = 0;
  std::string glyph;    // UTF-8
  std::string romaji;   // kana: reading, "-" for the long vowel mark;
                        // kanji: the German meaning, which takes the
                        // reading's place in prompts, sheet and game
  std::string readings; // kanji only, e.g. "やま・サン" (kun in hiragana, on in katakana)
  Script script = Script::Hiragana;
  int group = 0;
  std::vector<Stroke> strokes;
};

struct Group {
  int index = 0;
  std::string name; // romaji of the first kana, e.g. "ka"
  Script script = Script::Hiragana;
  std::vector<int> kana; // indices into all()
};

const std::vector<Kana>& all();
const std::vector<Group>& groups();
const Kana* find(const std::string& glyph);

// Geometry helpers shared by the checker and renderer.
float length(const Line& line);
Pt centroid(const Line& line);
Line resample(const Line& line, int n);

} // namespace kana
