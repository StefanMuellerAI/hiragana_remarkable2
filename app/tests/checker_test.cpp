// Simulates sloppy handwriting from the reference strokes and checks that the
// checker accepts correct writing and flags reversed / out-of-order strokes.
#include "../src/checker.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <random>

using namespace kana;

namespace {

std::mt19937 rng(1234);

float
uni(float a, float b) {
  return std::uniform_real_distribution<float>(a, b)(rng);
}

struct Hand {
  float scale, rot, tx, ty;
};

Hand
randomHand(float sloppiness) {
  return { 1.f + uni(-0.13f, 0.13f) * sloppiness,
           uni(-0.07f, 0.07f) * sloppiness,
           uni(-9, 9) * sloppiness,
           uni(-9, 9) * sloppiness };
}

// Turns a reference stroke into a plausible handwritten one.
Line
write(const Line& ref, const Hand& h, float sloppiness) {
  const Pt c{ 54.5f, 54.5f };
  const float s = 1.f + uni(-0.1f, 0.1f) * sloppiness;
  const Pt shift{ uni(-3.5f, 3.5f) * sloppiness, uni(-3.5f, 3.5f) * sloppiness };
  const float phase = uni(0, 6.28f), freq = uni(0.5f, 2.f);
  const float amp = 1.6f * sloppiness;
  const Pt sc = centroid(ref);

  Line out;
  const int n = static_cast<int>(ref.size());
  for (int i = 0; i < n; i++) {
    const float t = n > 1 ? float(i) / float(n - 1) : 0.f;
    Pt p = sc + (ref[i] - sc) * s + shift;
    p.x += amp * std::sin(phase + 6.28f * freq * t) + uni(-0.4f, 0.4f);
    p.y += amp * std::cos(phase * 1.3f + 6.28f * freq * t) + uni(-0.4f, 0.4f);
    Pt d = p - c;
    const float cs = std::cos(h.rot), sn = std::sin(h.rot);
    p = c + Pt{ d.x * cs - d.y * sn, d.x * sn + d.y * cs } * h.scale;
    p.x += h.tx;
    p.y += h.ty;
    out.push_back(p);
  }
  // Pen samples arrive unevenly spaced.
  Line uneven;
  for (size_t i = 0; i < out.size(); i++) {
    if (i == 0 || i + 1 == out.size() || uni(0, 1) < 0.6f) {
      uneven.push_back(out[i]);
    }
  }
  return uneven;
}

Line
reversed(Line l) {
  return Line(l.rbegin(), l.rend());
}

int failures = 0;

void
expect(bool ok, const char* what, double rate, double needed) {
  std::printf("%-46s %6.2f%%  (need >= %.0f%%) %s\n",
              what,
              rate * 100,
              needed * 100,
              ok ? "ok" : "FAIL");
  if (!ok) {
    failures++;
  }
}

} // namespace

int
main() {
  // Kana and kanji are tested separately: sections 1–6 see exactly the kana
  // they always saw (same order, same random numbers), kanji follow in 7.
  std::vector<Kana> kanaList, kanjiList;
  for (const auto& k : all()) {
    (k.script == Script::Kanji ? kanjiList : kanaList).push_back(k);
  }
  std::printf("%zu kana loaded\n", kanaList.size());

  // 1) Correct writing should pass.
  {
    int strokes = 0, good = 0;
    std::map<std::string, int> badPerKana;
    for (int trial = 0; trial < 40; trial++) {
      for (const auto& k : kanaList) {
        Hand h = randomHand(1.f);
        Checker c(k);
        for (const auto& s : k.strokes) {
          auto r = c.add(write(s.points, h, 1.f));
          strokes++;
          if (r.kind == StrokeResult::Good) {
            good++;
          } else {
            badPerKana[k.glyph]++;
            c.skip();
          }
        }
      }
    }
    const double rate = double(good) / strokes;
    expect(rate >= 0.985, "sloppy correct strokes accepted", rate, 0.985);
    for (auto& [g, n] : badPerKana) {
      if (n > 3) {
        std::printf("    rejected often: %s x%d\n", g.c_str(), n);
      }
    }
  }

  // 2) Reversed strokes should be flagged (not accepted).
  {
    int tested = 0, flagged = 0, called = 0;
    for (int trial = 0; trial < 20; trial++) {
      for (const auto& k : kanaList) {
        Hand h = randomHand(1.f);
        for (size_t si = 0; si < k.strokes.size(); si++) {
          if (length(k.strokes[si].points) < 14) {
            continue;
          }
          Checker c(k);
          for (size_t p = 0; p < si; p++) {
            c.add(write(k.strokes[p].points, h, 1.f));
          }
          if (c.done() != static_cast<int>(si)) {
            continue;
          }
          auto r = c.add(reversed(write(k.strokes[si].points, h, 1.f)));
          tested++;
          flagged += r.kind != StrokeResult::Good;
          called += r.kind == StrokeResult::Reversed;
        }
      }
    }
    expect(double(flagged) / tested >= 0.97,
           "reversed strokes not accepted",
           double(flagged) / tested,
           0.97);
    expect(double(called) / tested >= 0.85,
           "reversed strokes reported as 'reversed'",
           double(called) / tested,
           0.85);
  }

  // 3) Writing the next stroke too early should be flagged.
  {
    int tested = 0, flagged = 0, named = 0;
    for (int trial = 0; trial < 20; trial++) {
      for (const auto& k : kanaList) {
        if (k.strokes.size() < 2) {
          continue;
        }
        Hand h = randomHand(1.f);
        Checker c(k);
        auto r = c.add(write(k.strokes[1].points, h, 1.f));
        tested++;
        flagged += r.kind != StrokeResult::Good;
        named += r.kind == StrokeResult::OtherStroke && r.matched == 1;
      }
    }
    expect(double(flagged) / tested >= 0.9,
           "stroke 2 written first is flagged",
           double(flagged) / tested,
           0.9);
    expect(double(named) / tested >= 0.75,
           "... and identified as stroke 2",
           double(named) / tested,
           0.75);
  }

  // 3b) A dakuten / handakuten (the last strokes of the ga…pa rows) written
  // before the kana itself is a stroke-order mistake, not a stray touch.
  {
    int tested = 0, called = 0;
    for (int trial = 0; trial < 20; trial++) {
      for (const auto& k : kanaList) {
        const std::string& g = groups()[k.group].name;
        if (g != "ga" && g != "za" && g != "da" && g != "ba" && g != "pa") {
          continue;
        }
        Hand h = randomHand(1.f);
        Checker c(k);
        auto r = c.add(write(k.strokes.back().points, h, 1.f));
        tested++;
        called += r.kind == StrokeResult::OtherStroke;
      }
    }
    expect(tested > 0 && double(called) / tested >= 0.95,
           "dakuten written first is a mistake",
           double(called) / tested,
           0.95);
  }

  // 3c) Tiny touches stay harmless.
  {
    int tested = 0, noise = 0;
    for (const auto& k : kanaList) {
      Checker c(k);
      const Pt p = k.strokes.front().points.front();
      auto r = c.add(Line{ p, p + Pt{ 0.8f, 0.6f } });
      tested++;
      noise += r.kind == StrokeResult::Noise && c.mistakes() == 0;
    }
    expect(double(noise) / tested >= 1.0, "a tiny touch is ignored", double(noise) / tested, 1.0);
  }

  // 4) Confusable pairs: writing one when the other is asked must fail.
  {
    const std::pair<const char*, const char*> pairs[] = {
      { "ソ", "ン" }, { "シ", "ツ" }, { "ぬ", "め" }, { "わ", "れ" },
      { "る", "ろ" }, { "ね", "わ" }, { "シ", "ミ" }, { "ク", "ケ" },
      { "は", "ほ" }, { "い", "こ" }, { "コ", "ユ" }, { "ナ", "メ" },
    };
    for (auto [want, written] : pairs) {
      const Kana* a = find(want);
      const Kana* b = find(written);
      constexpr int trials = 200;
      int passed = 0;
      for (int trial = 0; trial < trials; trial++) {
        Hand h = randomHand(1.f);
        Checker c(*a);
        int good = 0;
        for (const auto& s : b->strokes) {
          good += c.add(write(s.points, h, 1.f)).kind == StrokeResult::Good;
        }
        passed += c.complete() && c.mistakes() == 0;
      }
      char label[96];
      std::snprintf(label, sizeof label, "%s asked, %s written: caught", want, written);
      const double rate = 1.0 - double(passed) / trials;
      // コ/ユ differ only in proportions, so allow a few misses there.
      const double need = std::string(want) == "コ" ? 0.85 : 0.98;
      expect(rate >= need, label, rate, need);
    }
  }

  // 5) Very sloppy (1.6x) writing: how forgiving is it? Informational.
  {
    int strokes = 0, good = 0;
    for (int trial = 0; trial < 10; trial++) {
      for (const auto& k : kanaList) {
        Hand h = randomHand(1.6f);
        Checker c(k);
        for (const auto& s : k.strokes) {
          auto r = c.add(write(s.points, h, 1.6f));
          strokes++;
          if (r.kind == StrokeResult::Good) {
            good++;
          } else {
            c.skip();
          }
        }
      }
    }
    std::printf("%-46s %6.2f%%  (info)\n",
                "very sloppy strokes accepted",
                100.0 * good / strokes);
  }

  // 6) Resample keeps endpoints and spacing.
  {
    Line l{ { 0, 0 }, { 10, 0 }, { 10, 10 } };
    Line r = resample(l, 5);
    const bool ok = r.size() == 5 && dist(r.front(), { 0, 0 }) < 1e-4f &&
                    dist(r.back(), { 10, 10 }) < 1e-4f &&
                    dist(r[2], { 10, 0 }) < 1e-3f;
    expect(ok, "resample endpoints", ok ? 1 : 0, 1);
  }

  // 7) Kanji (JLPT N5): many strokes, often parallel and close together.
  std::printf("\n%zu kanji loaded\n", kanjiList.size());
  {
    int strokes = 0, good = 0;
    std::map<std::string, int> badPerKanji;
    for (int trial = 0; trial < 40; trial++) {
      for (const auto& k : kanjiList) {
        Hand h = randomHand(1.f);
        Checker c(k);
        for (const auto& s : k.strokes) {
          auto r = c.add(write(s.points, h, 1.f));
          strokes++;
          if (r.kind == StrokeResult::Good) {
            good++;
          } else {
            badPerKanji[k.glyph]++;
            c.skip();
          }
        }
      }
    }
    const double rate = double(good) / strokes;
    expect(rate >= 0.98, "kanji: sloppy correct strokes accepted", rate, 0.98);
    for (auto& [g, n] : badPerKanji) {
      if (n > 6) {
        std::printf("    rejected often: %s x%d\n", g.c_str(), n);
      }
    }
  }
  {
    int tested = 0, flagged = 0;
    for (int trial = 0; trial < 20; trial++) {
      for (const auto& k : kanjiList) {
        Hand h = randomHand(1.f);
        for (size_t si = 0; si < k.strokes.size(); si++) {
          if (length(k.strokes[si].points) < 14) {
            continue;
          }
          Checker c(k);
          for (size_t p = 0; p < si; p++) {
            c.add(write(k.strokes[p].points, h, 1.f));
          }
          if (c.done() != static_cast<int>(si)) {
            continue;
          }
          tested++;
          flagged += c.add(reversed(write(k.strokes[si].points, h, 1.f))).kind != StrokeResult::Good;
        }
      }
    }
    expect(double(flagged) / tested >= 0.97, "kanji: reversed strokes not accepted",
           double(flagged) / tested, 0.97);
  }
  {
    // The next stroke written instead of the due one, at every position.
    int tested = 0, flagged = 0;
    std::map<std::string, int> missedPerKanji;
    for (int trial = 0; trial < 20; trial++) {
      for (const auto& k : kanjiList) {
        Hand h = randomHand(1.f);
        for (size_t si = 0; si + 1 < k.strokes.size(); si++) {
          Checker c(k);
          for (size_t p = 0; p < si; p++) {
            c.add(write(k.strokes[p].points, h, 1.f));
          }
          if (c.done() != static_cast<int>(si)) {
            continue;
          }
          tested++;
          const bool caught =
            c.add(write(k.strokes[si + 1].points, h, 1.f)).kind != StrokeResult::Good;
          flagged += caught;
          if (!caught) {
            missedPerKanji[k.glyph]++;
          }
        }
      }
    }
    expect(double(flagged) / tested >= 0.98, "kanji: two neighbouring strokes swapped",
           double(flagged) / tested, 0.98);
    for (auto& [g, n] : missedPerKanji) {
      if (n > 10) {
        std::printf("    often missed: %s x%d\n", g.c_str(), n);
      }
    }
  }
  {
    const std::pair<const char*, const char*> pairs[] = {
      { "右", "左" }, { "左", "右" }, { "人", "入" }, { "入", "人" }, { "大", "天" },
      { "日", "白" }, { "白", "百" }, { "千", "午" }, { "土", "上" }, { "十", "千" },
    };
    for (auto [want, written] : pairs) {
      const Kana* a = find(want);
      const Kana* b = find(written);
      constexpr int trials = 200;
      int passed = 0;
      for (int trial = 0; trial < trials; trial++) {
        Hand h = randomHand(1.f);
        Checker c(*a);
        for (const auto& s : b->strokes) {
          c.add(write(s.points, h, 1.f));
        }
        passed += c.complete() && c.mistakes() == 0;
      }
      char label[96];
      std::snprintf(label, sizeof label, "kanji: %s asked, %s written: caught", want, written);
      const double rate = 1.0 - double(passed) / trials;
      expect(rate >= 0.98, label, rate, 0.98);
    }
  }
  {
    int strokes = 0, good = 0;
    for (int trial = 0; trial < 10; trial++) {
      for (const auto& k : kanjiList) {
        Hand h = randomHand(1.6f);
        Checker c(k);
        for (const auto& s : k.strokes) {
          auto r = c.add(write(s.points, h, 1.6f));
          strokes++;
          if (r.kind == StrokeResult::Good) {
            good++;
          } else {
            c.skip();
          }
        }
      }
    }
    std::printf("%-46s %6.2f%%  (info)\n", "kanji: very sloppy strokes accepted",
                100.0 * good / strokes);
  }

  std::printf(failures ? "\n%d FAILED\n" : "\nall passed\n", failures);
  return failures ? 1 : 0;
}
