#pragma once
inline struct {
  bool begin(const char *) { return true; }
  void addService(const char *, const char *, int) {}
  void addServiceTxt(const char *, const char *, const char *, const char *) {}
  void update() {}
} MDNS;
