// SPDX-License-Identifier: GPL-3.0-or-later
// ROM-free helper for a LAB-ONLY emulated-frame input script. Not production.
#ifndef INCLUDED_TEST_INDY_FRAME_INPUTS_H
#define INCLUDED_TEST_INDY_FRAME_INPUTS_H
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>

namespace IndyFrameInputs {
struct Input { unsigned steer=128, accel=255, brake=0; };
struct Segment { unsigned actor=0; uint64_t begin=0, end=0; Input input; };

// actor,begin,end,steer,accel,brake;...  Half-open frame ranges; missing
// ranges use the explicit neutral/full-gas default. Reject ambiguous scripts.
inline bool Parse(const std::string &text, std::vector<Segment> &out)
{
  out.clear();
  if (text.empty()) return false;
  std::istringstream all(text); std::string chunk;
  while (std::getline(all, chunk, ';')) {
    if (chunk.empty()) { out.clear(); return false; }
    std::istringstream row(chunk); std::string field; std::vector<uint64_t> n;
    while (std::getline(row, field, ',')) {
      if (field.empty() || field.size()>10) { out.clear(); return false; }
      uint64_t v=0;
      for (char c:field) { if (c<'0'||c>'9') {out.clear(); return false;} v=v*10+(c-'0'); }
      n.push_back(v);
    }
    if (n.size()!=6 || chunk.back()==',' || n[0]>1 || n[1]>=n[2] || n[2]>100000 ||
        n[3]>255 || n[4]>255 || n[5]>255) { out.clear(); return false; }
    Segment s{unsigned(n[0]),n[1],n[2],{unsigned(n[3]),unsigned(n[4]),unsigned(n[5])}};
    for (const auto &p:out)
      if (p.actor==s.actor && s.begin<p.end && p.begin<s.end) {out.clear(); return false;}
    out.push_back(s);
  }
  if (text.back()==';') {out.clear(); return false;}
  return !out.empty();
}
inline Input At(const std::vector<Segment> &segments, unsigned actor, uint64_t frame)
{
  for (const auto &s:segments)
    if (s.actor==actor && s.begin<=frame && frame<s.end) return s.input;
  return {};
}
}
#endif
