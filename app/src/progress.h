#pragma once

#include <map>
#include <string>
#include <vector>

namespace kana {

// Leitner-style spaced repetition state, persisted as a small TSV file.
struct CardState {
  int box = 0;     // 0 = never learnt, 1..kMaxBox
  int dueDay = 0;  // days since epoch
  int right = 0;
  int wrong = 0;
};

class Progress {
public:
  static constexpr int kMaxBox = 6;

  explicit Progress(std::string path);

  bool load();
  bool save() const;

  CardState get(const std::string& glyph) const;

  void markLearnt(const std::string& glyph, int today);  // after "Lernen"
  void recordPractice(const std::string& glyph, bool ok); // stats only
  void recordTest(const std::string& glyph, bool ok, int today);

  bool isDue(const std::string& glyph, int today) const;
  static int intervalDays(int box);

  // Settings live in the same file so there's only one thing to back up.
  std::string setting(const std::string& key, const std::string& def) const;
  void setSetting(const std::string& key, const std::string& value);

private:
  std::string path_;
  std::map<std::string, CardState> cards_;
  std::map<std::string, std::string> settings_;
};

} // namespace kana
