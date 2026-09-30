#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <cctype>

class String {
 public:
  std::string value;
  String(const char *s = "") : value(s) {}
  String(std::string s) : value(s) {}
  const char *c_str() const { return value.c_str(); }
  size_t length() const { return value.size(); }
  void replace(const char *from, const char *to) {
    size_t pos;
    while ((pos = value.find(from)) != std::string::npos) value.replace(pos, std::string(from).size(), to);
  }
  void toLowerCase() { std::transform(value.begin(), value.end(), value.begin(), ::tolower); }
  void trim() {
    auto first = value.find_first_not_of(" \t\r\n");
    value = first == std::string::npos ? "" : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
  }
  int indexOf(char c, int start = 0) const { auto p = value.find(c, start); return p == std::string::npos ? -1 : p; }
  String substring(int start, int end = -1) const { return value.substr(start, end < 0 ? std::string::npos : end-start); }
  bool startsWith(const char *s) const { return value.rfind(s, 0) == 0; }
  friend String operator+(const String &a, const String &b) { return a.value+b.value; }
  friend bool operator==(const String &a, const String &b) { return a.value==b.value; }
};

inline unsigned long clockTicks = 0;
inline unsigned long millis() { return ++clockTicks; }
inline void delay(unsigned long n) { clockTicks += n; }
inline void yield() {}
inline struct { bool restarted = false; void restart() { restarted = true; } } ESP;
