#include "checker.h"

#include <algorithm>
#include <cmath>

namespace kana {
namespace {


constexpr int kSamples = 32;
constexpr float kPosTolerance = 17.f;
constexpr float kMinDirectionalLength = 14.f;
constexpr float kMaxDevFactor = 2.5f;
constexpr float kTurnTolerance = 100.f; // degrees
constexpr float kMinLenRatio = 0.62f;
constexpr float kMaxLenRatio = 1.45f;
constexpr float kMinLenForRatio = 20.f;
constexpr float kMinLenForTurn = 30.f;
constexpr float kPi = 3.14159265f;
// Kanji only: a stroke that fits the due stroke still counts as a later
// one if that later stroke matches clearly better (score below this share).
// Catches swapped neighbours among close parallel strokes (書, 電, 聞 …).
#ifndef KANJI_BETTER_FACTOR
#define KANJI_BETTER_FACTOR 0.7f
#endif
constexpr float kKanjiBetterFactor = KANJI_BETTER_FACTOR;

// Total signed turning along the stroke in degrees; averages a few segment
// directions at each end so pen jitter there doesn't dominate.
float
turning(const Line& line) {
  constexpr int n = 24;
  const Line r = resample(line, n);
  std::vector<float> a;
  float prev = 0;
  for (int i = 1; i < n; i++) {
    float t = std::atan2(r[i].y - r[i - 1].y, r[i].x - r[i - 1].x);
    if (!a.empty()) {
      while (t - prev > kPi) {
        t -= 2 * kPi;
      }
      while (t - prev < -kPi) {
        t += 2 * kPi;
      }
    }
    a.push_back(t);
    prev = t;
  }
  const size_t m = a.size();
  const float start = (a[0] + a[1] + a[2]) / 3.f;
  const float end = (a[m - 1] + a[m - 2] + a[m - 3]) / 3.f;
  return (end - start) * 180.f / kPi;
}

float
clamp01(float v) {
  return std::max(0.f, std::min(1.f, v));
}

} // namespace

const char*
describe(StrokeResult::Kind kind) {
  switch (kind) {
    case StrokeResult::Good:
      return "good";
    case StrokeResult::Noise:
      return "noise";
    case StrokeResult::Reversed:
      return "reversed";
    case StrokeResult::OtherStroke:
      return "other-stroke";
    case StrokeResult::WrongShape:
      return "wrong-shape";
    case StrokeResult::Extra:
      return "extra";
  }
  return "?";
}

Checker::Checker(const Kana& kana) : kana_(kana) {
  for (const auto& s : kana.strokes) {
    ref_.push_back(resample(s.points, kSamples));
    refLength_.push_back(length(s.points));
    refTurn_.push_back(turning(s.points));
  }
}

bool
Checker::Match::fits() const {
  return shape <= tolerance && maxDev <= tolerance * kMaxDevFactor &&
         pos <= kPosTolerance && turnDiff <= kTurnTolerance &&
         lenRatio >= kMinLenRatio && lenRatio <= kMaxLenRatio;
}

bool
Checker::Match::fitsReversed() const {
  return directional && shapeRev <= tolerance &&
         maxDevRev <= tolerance * kMaxDevFactor && pos <= kPosTolerance &&
         turnDiffRev <= kTurnTolerance && lenRatio >= kMinLenRatio &&
         lenRatio <= kMaxLenRatio && shapeRev < shape * 0.7f;
}

Checker::Match
Checker::match(int refIndex, const Line& user) const {
  Match m;
  const Line& r = ref_[refIndex];
  Line u = resample(user, kSamples);
  for (auto& p : u) {
    p = p - offset_;
  }
  const Pt cr = centroid(r);
  const Pt cu = centroid(u);

  float fwd = 0, rev = 0, maxFwd = 0, maxRev = 0;
  for (int k = 0; k < kSamples; k++) {
    const Pt rk = r[k] - cr;
    const float df = dist(u[k] - cu, rk);
    const float dr = dist(u[kSamples - 1 - k] - cu, rk);
    fwd += df;
    rev += dr;
    maxFwd = std::max(maxFwd, df);
    maxRev = std::max(maxRev, dr);
  }
  m.shape = fwd / kSamples;
  m.shapeRev = rev / kSamples;
  m.maxDev = maxFwd;
  m.maxDevRev = maxRev;
  m.pos = dist(cu, cr);

  const float len = refLength_[refIndex];
  if (len >= kMinLenForTurn) {
    const float t = turning(user);
    m.turnDiff = std::fabs(t - refTurn_[refIndex]);
    m.turnDiffRev = std::fabs(-t - refTurn_[refIndex]);
  }
  if (len >= kMinLenForRatio) {
    m.lenRatio = length(user) / (len * scale_);
  }
  m.tolerance = std::max(6.f, std::min(20.f, 4.f + 0.11f * len));
  m.directional = len >= kMinDirectionalLength;
  if (!m.directional && m.shapeRev < m.shape) {
    m.shape = m.shapeRev;
    m.maxDev = m.maxDevRev;
  }
  return m;
}

StrokeResult
Checker::add(const Line& stroke) {
  StrokeResult res;
  res.expected = done();
  if (complete()) {
    res.kind = StrokeResult::Extra;
    return res;
  }

  const float refLen = refLength_[res.expected];
  if (stroke.size() < 2 || length(stroke) < std::max(2.f, 0.15f * refLen)) {
    // Too short for the stroke that's due. It may still be a small later
    // stroke written too early, like a dakuten dot; only then is it a
    // mistake rather than an accidental touch.
    const float len = stroke.size() < 2 ? 0.f : length(stroke);
    for (int j = res.expected + 1; j < static_cast<int>(ref_.size()); j++) {
      if (len < std::max(2.f, 0.15f * refLength_[j])) {
        continue;
      }
      const Match mj = match(j, stroke);
      if (mj.fits() || mj.fitsReversed()) {
        mistakes_++;
        res.kind = StrokeResult::OtherStroke;
        res.matched = j;
        return res;
      }
    }
    res.kind = StrokeResult::Noise;
    return res;
  }

  const Match m = match(res.expected, stroke);
  if (m.fits() && kana_.script == Script::Kanji) {
    const float due = m.shape / m.tolerance + m.pos / kPosTolerance;
    float best = due * kKanjiBetterFactor;
    for (int j = res.expected + 1; j < static_cast<int>(ref_.size()); j++) {
      const Match mj = match(j, stroke);
      const float score = mj.shape / mj.tolerance + mj.pos / kPosTolerance;
      if (mj.fits() && score < best) {
        best = score;
        res.matched = j;
      }
    }
    if (res.matched >= 0) {
      mistakes_++;
      res.kind = StrokeResult::OtherStroke;
      return res;
    }
  }
  if (m.fits()) {
    res.kind = StrokeResult::Good;
    res.quality = clamp01(1.f - 0.6f * (m.shape / m.tolerance) -
                          0.4f * (m.pos / kPosTolerance));
    accepted_.push_back(res.quality);

    // Learn the user's global shift from the accepted strokes.
    const Pt cr = centroid(ref_[res.expected]);
    const Pt cu = centroid(resample(stroke, kSamples));
    offsets_.push_back(cu - cr);
    Pt sum;
    for (const auto& o : offsets_) {
      sum = sum + o;
    }
    offset_ = sum * (1.f / static_cast<float>(offsets_.size()));

    if (refLen >= kMinLenForRatio) {
      scales_.push_back(length(stroke) / refLen);
      float total = 0;
      for (float v : scales_) {
        total += v;
      }
      scale_ = total / static_cast<float>(scales_.size());
    }
    return res;
  }

  mistakes_++;
  if (m.fitsReversed()) {
    res.kind = StrokeResult::Reversed;
    return res;
  }

  float best = 1e9f;
  for (int j = res.expected + 1; j < static_cast<int>(ref_.size()); j++) {
    const Match mj = match(j, stroke);
    if ((mj.fits() || mj.fitsReversed())) {
      const float score =
        std::min(mj.shape, mj.shapeRev) / mj.tolerance + mj.pos / kPosTolerance;
      if (score < best) {
        best = score;
        res.matched = j;
      }
    }
  }
  res.kind =
    res.matched >= 0 ? StrokeResult::OtherStroke : StrokeResult::WrongShape;
  return res;
}

void
Checker::skip() {
  if (!complete()) {
    accepted_.push_back(0.f);
    mistakes_++;
  }
}

float
Checker::averageQuality() const {
  if (accepted_.empty()) {
    return 0;
  }
  float sum = 0;
  for (float q : accepted_) {
    sum += q;
  }
  return sum / static_cast<float>(accepted_.size());
}

} // namespace kana
