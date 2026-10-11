/*
 * harness.h — horde 2's engine harness: the one interface a robustness tool uses
 * to drive ANY enrolled engine without knowing which engine it is. ROADMAP B448
 * (Blind-Spot Armor) phase 2, package P2, ruled in ADR-209; the proposal is
 * docs/strategy/blind-spot-armor-phase2.md (section 2 rule 4, section 3 row P2).
 * The interface is documented for its users in h2/harness/README.md.
 *
 * WHAT IS HERE.
 *   Adapter   what an enrolled engine looks like from outside: prepare, set,
 *             notes, render, and the limiter's input recovered from its output.
 *   Job       one render request in the job language (below), and runJob(),
 *             which plays it through an adapter and hands the samples to a sink.
 *   Three CONTROL adapters (control/silent, control/tone, control/block). They
 *             are not engines. They exist so every check built on this harness can
 *             prove, on every run, that it can read silence as silence, a known
 *             level as that level, and a block-size dependence as one (the repo's
 *             rule for any probe: LIBRARY L0016, L0032).
 *
 * This file includes no engine. Each engine's adapter is its own header
 * (adapter_<id>.h), and adapters.h is the table of them.
 *
 * THE JOB LANGUAGE (a text file; one job after another):
 *   H2HARNESS 1                      header, once
 *   JOB <name, the rest of the line>
 *   engine <adapter id>
 *   sr <sample rate>
 *   seed <uint32>                    the host-seeded random stream
 *   block <n>                        host block size, frames (default 128)
 *   block vary <max>                 block sizes drawn 1..max from a mulberry32
 *                                    stream seeded with seed ^ kBlockSeedXor
 *   pre <0|1>                        also emit the pre-limiter bus (default 0)
 *   begin                            builds a FRESH instance; commands follow
 *   set <key> <number> | sets <key> <string> | snap
 *   on <note> <freq> <vel> | off <note> | panic
 *   render <frames>
 *   END
 * The command words are the scenario language's own (tools/h2_scenarios.mjs), so a
 * script means the same thing to the parity check and to this harness. Numbers are
 * read with strtod, so JS's shortest round-trip spelling gives the exact double.
 * A `render` is cut into host blocks, and the last block of each `render` is
 * shortened to fit: a note or a set lands on its exact frame at ANY block size.
 *
 * DETERMINISM. No clock is read and nothing is drawn except from the seeded
 * mulberry32 stream above. The same job gives the same bytes from the same binary.
 * A digest is keyed to the build that made it (h2/README.md), so the harness prints
 * digests and pins none.
 *
 * NOT REAL TIME. This is a test harness: prepare() allocates, and the sink writes
 * files. The engine's own render() is unchanged by it.
 */
#pragma once

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace horde2::harness {

// ---- the adapter ---------------------------------------------------------------
// One per enrolled engine. Every call is made from one thread, in job order.
class Adapter {
 public:
  virtual ~Adapter() = default;
  // A fresh instance at this rate, with the host random stream seeded. Everything
  // an earlier job left behind is gone after this call.
  virtual void prepare(double sampleRate, uint32_t seed) = 0;
  // One parameter. False means the engine has no such key; the job counts those
  // and the caller decides what a non-zero count means.
  virtual bool set(const char* key, double value) = 0;
  virtual bool setString(const char* key, const char* value) = 0;
  // Start AT the values set so far instead of gliding in from the defaults.
  virtual void snap() = 0;
  virtual void noteOn(int note, double freq, double vel) = 0;
  virtual void noteOff(int note) = 0;
  virtual void panic() = 0;
  // The engine's output, after its limiter. Stereo, n frames.
  virtual void render(double* L, double* R, int n) = 0;
  // The limiter's INPUT for one output sample. An adapter with no limiter returns
  // the sample. Where the output sits on the limiter's rail the input cannot be
  // recovered and this returns +-infinity; a NaN stays a NaN. Each adapter's header
  // states its own range and precision.
  virtual double preLimiter(double out) const = 0;
};

// ---- the seeded stream -----------------------------------------------------------
// mulberry32, the repo's one PRNG (the same arithmetic as h2/engine/js.h and
// tools/h2_scenarios.mjs). A copy on purpose: this file includes no engine.
struct Mulberry32 {
  uint32_t a = 0;
  double next() {
    a = a + 0x6D2B79F5u;
    uint32_t t = (a ^ (a >> 15)) * (1u | a);
    t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
  }
};
// Keeps the block-size stream apart from the engine's host stream, which is
// seeded with the job's seed itself.
constexpr uint32_t kBlockSeedXor = 0x9E3779B9u;

// ---- a job -----------------------------------------------------------------------
struct Cmd { std::string op, key, str; double a = 0, b = 0, c = 0; };
struct Job {
  std::string name, engine;
  double sr = 48000;
  uint32_t seed = 0;
  int block = 128;      // host block size; with `vary`, the largest size drawn
  bool vary = false;
  bool pre = false;
  std::vector<Cmd> cmds;
};

// What a run saw besides its samples.
struct JobInfo {
  uint64_t frames = 0;
  int unknownKeys = 0;
  std::string firstUnknown;
  uint64_t digest = 14695981039346656037ull;   // FNV-1a 64 offset basis
};

// Receives each rendered stretch: n frames of output and, when the job asked for
// it, the pre-limiter bus (null otherwise).
class Sink {
 public:
  virtual ~Sink() = default;
  virtual void write(const double* L, const double* R, const double* preL, const double* preR, int n) = 0;
};

// FNV-1a 64 over the OUTPUT samples' bytes, left then right per frame. The
// pre-limiter bus is left out, so the digest does not depend on `pre`, on the host
// block size or on how the sink chunks: two renders with equal digests made the
// same output samples in the same order.
inline void digestFrames(uint64_t& h, const double* L, const double* R, int n) {
  for (int i = 0; i < n; i++) {
    for (const double* p : {L + i, R + i}) {
      unsigned char b[sizeof(double)];
      std::memcpy(b, p, sizeof b);
      for (unsigned char c : b) { h ^= c; h *= 1099511628211ull; }
    }
  }
}

// Plays `job` through `a`. The adapter is prepared here, so one adapter object can
// run any number of jobs. Rendering is cut into stretches of at most kStretch
// frames so a long job needs constant memory.
constexpr int kStretch = 1 << 15;
inline void runJob(Adapter& a, const Job& job, Sink& sink, JobInfo& info) {
  a.prepare(job.sr, job.seed);
  Mulberry32 blocks{job.seed ^ kBlockSeedXor};
  const int maxBlock = job.block < 1 ? 1 : job.block;
  std::vector<double> L(kStretch), R(kStretch), pL(job.pre ? kStretch : 0), pR(job.pre ? kStretch : 0);
  for (const Cmd& m : job.cmds) {
    if (m.op == "set" || m.op == "sets") {
      const bool known = m.op == "set" ? a.set(m.key.c_str(), m.a) : a.setString(m.key.c_str(), m.str.c_str());
      if (!known && !info.unknownKeys++) info.firstUnknown = m.key;
    }
    else if (m.op == "snap") a.snap();
    else if (m.op == "on") a.noteOn(static_cast<int>(m.a), m.b, m.c);
    else if (m.op == "off") a.noteOff(static_cast<int>(m.a));
    else if (m.op == "panic") a.panic();
    else if (m.op == "render") {
      uint64_t left = static_cast<uint64_t>(m.a);
      while (left > 0) {
        const int stretch = left < static_cast<uint64_t>(kStretch) ? static_cast<int>(left) : kStretch;
        for (int done = 0; done < stretch;) {
          int n = job.vary ? 1 + static_cast<int>(blocks.next() * maxBlock) : maxBlock;
          if (n > maxBlock) n = maxBlock;   // next() < 1, so this never acts; it bounds the cast
          if (n > stretch - done) n = stretch - done;
          a.render(L.data() + done, R.data() + done, n);
          done += n;
        }
        if (job.pre) for (int i = 0; i < stretch; i++) { pL[i] = a.preLimiter(L[i]); pR[i] = a.preLimiter(R[i]); }
        digestFrames(info.digest, L.data(), R.data(), stretch);
        sink.write(L.data(), R.data(), job.pre ? pL.data() : nullptr, job.pre ? pR.data() : nullptr, stretch);
        info.frames += static_cast<uint64_t>(stretch);
        left -= static_cast<uint64_t>(stretch);
      }
    }
  }
}

// ---- reading the job language ------------------------------------------------------
inline bool readLine(FILE* f, std::string& out) {
  out.clear();
  int ch;
  while ((ch = std::fgetc(f)) != EOF) { if (ch == '\n') return true; if (ch != '\r') out.push_back(static_cast<char>(ch)); }
  return !out.empty();
}
inline std::string word(const std::string& s, size_t& p) {
  while (p < s.size() && s[p] == ' ') p++;
  const size_t b = p;
  while (p < s.size() && s[p] != ' ') p++;
  return s.substr(b, p - b);
}
inline std::string rest(const std::string& s, size_t p) { return p < s.size() ? s.substr(p + 1) : std::string(); }
inline double num(const std::string& s, size_t& p) { return std::strtod(word(s, p).c_str(), nullptr); }

// Reads one job, starting at the line after `JOB <name>`. Returns false with `err`
// set on a line it does not know: a misspelt command must stop the run, never be
// skipped, or a job would quietly render something other than what was asked.
inline bool readJob(FILE* f, Job& job, std::string& err) {
  std::string line;
  bool begun = false;
  for (;;) {
    if (!readLine(f, line)) { err = "job '" + job.name + "': the file ended before END"; return false; }
    size_t p = 0;
    const std::string op = word(line, p);
    if (op == "END") {
      if (!begun) { err = "job '" + job.name + "': END before begin"; return false; }
      return true;
    }
    if (!begun) {
      if (op == "engine") job.engine = word(line, p);
      else if (op == "sr") job.sr = num(line, p);
      else if (op == "seed") job.seed = static_cast<uint32_t>(std::strtoul(word(line, p).c_str(), nullptr, 10));
      else if (op == "block") {
        std::string w = word(line, p);
        if (w == "vary") { job.vary = true; w = word(line, p); }
        job.block = std::atoi(w.c_str());
        if (job.block < 1) { err = "job '" + job.name + "': block must be 1 or more"; return false; }
      }
      else if (op == "pre") job.pre = std::atoi(word(line, p).c_str()) != 0;
      else if (op == "begin") {
        if (job.engine.empty()) { err = "job '" + job.name + "': begin before engine"; return false; }
        begun = true;
      }
      else { err = "job '" + job.name + "': unknown header line '" + line + "'"; return false; }
      continue;
    }
    Cmd c;
    c.op = op;
    if (op == "set") { c.key = word(line, p); c.a = num(line, p); }
    else if (op == "sets") { c.key = word(line, p); c.str = rest(line, p); }
    else if (op == "on") { c.a = num(line, p); c.b = num(line, p); c.c = num(line, p); }
    else if (op == "off" || op == "render") { c.a = num(line, p); }
    else if (op != "snap" && op != "panic") { err = "job '" + job.name + "': unknown command '" + line + "'"; return false; }
    if (op == "render" && !(c.a >= 0)) { err = "job '" + job.name + "': render needs a frame count of 0 or more"; return false; }
    job.cmds.push_back(c);
  }
}

// ---- the control adapters ----------------------------------------------------------
// NOT engines, and never enrolled. A check built on the harness renders them beside
// the real adapters so that its own verdicts are tested on every run.

// Returns silence whatever it is asked. A must-read-loud check that passes this
// adapter is broken.
class SilentControl final : public Adapter {
 public:
  void prepare(double, uint32_t) override {}
  bool set(const char*, double) override { return true; }
  bool setString(const char*, const char*) override { return true; }
  void snap() override {}
  void noteOn(int, double, double) override {}
  void noteOff(int) override {}
  void panic() override {}
  void render(double* L, double* R, int n) override { for (int i = 0; i < n; i++) L[i] = R[i] = 0; }
  double preLimiter(double out) const override { return out; }
};

// A sine at the newest held note's frequency while any note is held, and exact
// zeros otherwise: amplitude kToneAmp on the left and HALF that on the right. No
// limiter, no envelope, no parameters, so its levels are known in closed form (RMS
// = amplitude / sqrt 2). A feature bridge that misreads level, swaps or misaligns
// the channels, or reads sound where no note is held, shows here.
class ToneControl final : public Adapter {
 public:
  static constexpr double kToneAmp = 0.5;
  void prepare(double sampleRate, uint32_t) override { sr = sampleRate; phase = 0; held.clear(); }
  bool set(const char*, double) override { return true; }
  bool setString(const char*, const char*) override { return true; }
  void snap() override {}
  void noteOn(int note, double freq, double) override { held.push_back({note, freq}); }
  void noteOff(int note) override {
    for (size_t i = 0; i < held.size();) { if (held[i].note == note) held.erase(held.begin() + static_cast<long>(i)); else i++; }
  }
  void panic() override { held.clear(); }
  void render(double* L, double* R, int n) override {
    for (int i = 0; i < n; i++) {
      if (held.empty()) { L[i] = R[i] = 0; phase = 0; continue; }
      L[i] = kToneAmp * std::sin(kTau * phase);
      R[i] = L[i] / 2;
      phase += held.back().freq / sr;
      phase -= std::floor(phase);
    }
  }
  double preLimiter(double out) const override { return out; }

 private:
  static constexpr double kTau = 6.283185307179586;
  struct Held { int note; double freq; };
  double sr = 48000, phase = 0;
  std::vector<Held> held;
};

// Block-size DEPENDENT on purpose: every frame of a render call carries that call's
// length on the left and the call's index on the right. "Bit-identical at every
// host block size" means nothing unless the block size really reaches the adapter;
// this is the adapter whose output must CHANGE with it, and whose samples say
// exactly how the harness cut the render.
class BlockControl final : public Adapter {
 public:
  void prepare(double, uint32_t) override { calls = 0; }
  bool set(const char*, double) override { return true; }
  bool setString(const char*, const char*) override { return true; }
  void snap() override {}
  void noteOn(int, double, double) override {}
  void noteOff(int) override {}
  void panic() override {}
  void render(double* L, double* R, int n) override {
    for (int i = 0; i < n; i++) { L[i] = n; R[i] = calls; }
    calls++;
  }
  double preLimiter(double out) const override { return out; }

 private:
  int calls = 0;
};

}  // namespace horde2::harness
