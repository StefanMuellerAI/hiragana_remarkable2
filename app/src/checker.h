#pragma once

#include "kana.h"

namespace kana {

struct StrokeResult {
  enum Kind {
    Good,        // right stroke, right direction
    Noise,       // accidental tap, ignore it
    Reversed,    // right stroke, written backwards
    OtherStroke, // a later stroke of this kana, written too early
    WrongShape,  // doesn't match the expected stroke
    Extra,       // kana already complete
  };
  Kind kind = WrongShape;
  int expected = 0; // stroke index that was due
  int matched = -1; // for OtherStroke: which stroke it looks like
  float quality = 0; // 0..1, only meaningful for Good
};

const char* describe(StrokeResult::Kind kind);

// Checks one handwritten stroke at a time against the reference kana.
// All coordinates are in the 109x109 reference space.
class Checker {
public:
  explicit Checker(const Kana& kana);

  // Evaluates `stroke` as the next stroke and, if Good, accepts it.
  StrokeResult add(const Line& stroke);

  // Accepts the expected stroke as given (used by the "show me" hint).
  void skip();

  int done() const { return static_cast<int>(accepted_.size()); }
  bool complete() const { return done() >= static_cast<int>(ref_.size()); }
  float averageQuality() const;
  int mistakes() const { return mistakes_; }

  // Shift between the user's writing and the reference, learnt from the
  // strokes accepted so far, so a character written off-centre still works.
  Pt offset() const { return offset_; }

private:
  struct Match {
    float shape = 1e9f; // centroid-aligned mean distance
    float shapeRev = 1e9f;
    float maxDev = 1e9f; // worst single point, catches local differences
    float maxDevRev = 1e9f;
    float turnDiff = 0;   // degrees, total turning vs. reference
    float turnDiffRev = 0;
    float lenRatio = 1;   // user/reference length, corrected by scale
    float pos = 1e9f; // centroid distance after offset correction
    float tolerance = 0;
    bool directional = true;
    bool fits() const;
    bool fitsReversed() const;
  };
  Match match(int refIndex, const Line& user) const;

  const Kana& kana_;
  std::vector<Line> ref_; // resampled reference strokes
  std::vector<float> refLength_;
  std::vector<float> refTurn_;
  std::vector<float> scales_;
  float scale_ = 1;
  std::vector<float> accepted_;
  std::vector<Pt> offsets_;
  Pt offset_;
  int mistakes_ = 0;
};

} // namespace kana
