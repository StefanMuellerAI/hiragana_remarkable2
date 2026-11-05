#include "progress.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace kana {

Progress::Progress(std::string path) : path_(std::move(path)) {}

bool
Progress::load() {
  std::ifstream in(path_);
  if (!in) {
    return false;
  }
  std::string line;
  while (std::getline(in, line)) {
    std::istringstream ss(line);
    std::string kind, key;
    std::getline(ss, kind, '\t');
    std::getline(ss, key, '\t');
    if (kind == "card") {
      CardState c;
      ss >> c.box >> c.dueDay >> c.right >> c.wrong;
      if (!key.empty()) {
        cards_[key] = c;
      }
    } else if (kind == "set") {
      std::string value;
      std::getline(ss, value);
      settings_[key] = value;
    }
  }
  return true;
}

bool
Progress::save() const {
  const std::string tmp = path_ + ".tmp";
  {
    std::ofstream out(tmp, std::ios::trunc);
    if (!out) {
      return false;
    }
    out << "# kana-dojo progress v1\n";
    for (const auto& [k, v] : settings_) {
      out << "set\t" << k << '\t' << v << '\n';
    }
    for (const auto& [k, c] : cards_) {
      out << "card\t" << k << '\t' << c.box << ' ' << c.dueDay << ' '
          << c.right << ' ' << c.wrong << '\n';
    }
    out.flush();
    if (!out) {
      return false;
    }
  }
  // Atomic replace so a sudden power-off can't corrupt the file.
  return std::rename(tmp.c_str(), path_.c_str()) == 0;
}

CardState
Progress::get(const std::string& glyph) const {
  auto it = cards_.find(glyph);
  return it == cards_.end() ? CardState{} : it->second;
}

int
Progress::intervalDays(int box) {
  static const int days[] = { 0, 0, 1, 3, 7, 14, 30 };
  return days[std::max(0, std::min(box, kMaxBox))];
}

void
Progress::markLearnt(const std::string& glyph, int today) {
  auto& c = cards_[glyph];
  if (c.box == 0) {
    c.box = 1;
    c.dueDay = today;
  }
}

void
Progress::recordPractice(const std::string& glyph, bool ok) {
  auto& c = cards_[glyph];
  (ok ? c.right : c.wrong)++;
  if (c.box == 0 && ok) {
    c.box = 1;
  }
}

void
Progress::recordTest(const std::string& glyph, bool ok, int today) {
  auto& c = cards_[glyph];
  if (ok) {
    c.right++;
    // Only promote when it was actually due, so cramming doesn't skip boxes.
    if (c.box == 0 || c.dueDay <= today) {
      c.box = std::min(kMaxBox, c.box + 1);
      c.dueDay = today + intervalDays(c.box);
    }
  } else {
    c.wrong++;
    c.box = 1;
    c.dueDay = today;
  }
}

bool
Progress::isDue(const std::string& glyph, int today) const {
  const CardState c = get(glyph);
  return c.box > 0 && c.dueDay <= today;
}

std::string
Progress::setting(const std::string& key, const std::string& def) const {
  auto it = settings_.find(key);
  return it == settings_.end() ? def : it->second;
}

void
Progress::setSetting(const std::string& key, const std::string& value) {
  settings_[key] = value;
}

} // namespace kana
