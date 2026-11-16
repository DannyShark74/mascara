// Offline checks for the Máscara engine on a synthetic voice.
//   test            run every check, exit code 1 on failure
//   test --wav dir  also write the rendered examples as 24-bit WAV files
#include "../source/Engine.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace mascara;
static constexpr double PI = 3.14159265358979323846;
static int failures = 0;
static std::string wavDir;

static void check (bool ok, const char* what, double value, const char* unit = "")
{
    std::printf ("  [%s] %-58s %9.3f %s\n", ok ? " ok " : "FAIL", what, value, unit);
    if (! ok) ++failures;
}

// ---------------------------------------------------------------- synthetic voice
struct Reson
{
    double a1 = 0, a2 = 0, g = 1, y1 = 0, y2 = 0;
    Reson (double f, double bw, double sr)
    {
        const double r = std::exp (-PI * bw / sr);
        a1 = 2 * r * std::cos (2 * PI * f / sr);
        a2 = -r * r;
        g = 1 - r;
    }
    double run (double x) { const double y = g * x + a1 * y1 + a2 * y2; y2 = y1; y1 = y; return y; }
};

struct VoiceSpec
{
    std::function<double (double)> f0;     // Hz as a function of time (s)
    double dur = 3.0;
    bool consonants = false;               // "s" bursts and short gaps
    double formantScale = 1.0;
};

static std::vector<float> makeVoice (double sr, const VoiceSpec& s)
{
    const int n = (int) (s.dur * sr);
    std::vector<float> out ((size_t) n);
    const double fs[4] = { 730, 1090, 2440, 3400 }, bw[4] = { 90, 110, 170, 250 };
    std::vector<Reson> rs;
    for (int i = 0; i < 4; ++i) rs.emplace_back (fs[i] * s.formantScale, bw[i], sr);
    std::mt19937 rng (7);
    std::normal_distribution<double> nd (0, 1);
    double ph = 0, prev = 0, hpX = 0, hpY = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        ph += s.f0 (t) / sr;
        if (ph >= 1) ph -= 1;
        // Rosenberg glottal pulse, open quotient 0.6
        const double oq = 0.6, tp = 0.4 * oq * 2;
        double gl = 0;
        if (ph < tp) gl = 0.5 * (1 - std::cos (PI * ph / tp));
        else if (ph < oq * 1.33) gl = std::cos (PI * (ph - tp) / (2 * (oq * 1.33 - tp)));
        double v = gl + 0.01 * nd (rng);
        for (auto& r : rs) v = r.run (v);
        const double rad = v - prev;   // lip radiation
        prev = v;
        double y = rad;
        if (s.consonants)
        {
            const double cyc = std::fmod (t, 1.0);
            if (cyc > 0.70 && cyc < 0.82)        // "s"
            {
                const double w = nd (rng);
                const double hp = 0.7 * (hpY + w - hpX);
                hpX = w; hpY = hp;
                y = 0.25 * hp * std::sin (PI * (cyc - 0.70) / 0.12);
            }
            else if (cyc >= 0.82 && cyc < 0.90) y = 0;   // gap
        }
        out[(size_t) i] = (float) y;
    }
    float peak = 0;
    for (float v : out) peak = std::max (peak, std::abs (v));
    for (auto& v : out) v *= 0.25f / peak;
    return out;
}

// ---------------------------------------------------------------- analysis helpers
static void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2 * PI / (double) len);
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> wn (1);
            for (size_t j = 0; j < len / 2; ++j, wn *= w)
            {
                const auto u = a[i + j], v = a[i + j + len / 2] * wn;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
            }
        }
    }
}

// independent full-rate pitch estimate (normalised difference function)
static std::vector<double> pitchTrack (const std::vector<float>& x, double sr, double from, double to)
{
    std::vector<double> midi;
    const int W = (int) (0.025 * sr), maxL = (int) (sr / 60), minL = (int) (sr / 1100);
    std::vector<double> d ((size_t) maxL + 2);
    for (double t = from; t + (W + maxL) / sr < to; t += 0.01)
    {
        const int s = (int) (t * sr);
        double run = 0, energy = 0;
        for (int j = 0; j < W; ++j) energy += (double) x[(size_t) (s + j)] * x[(size_t) (s + j)];
        if (energy / W < 1e-6) continue;
        int best = -1;
        std::vector<double> cm ((size_t) maxL + 2, 1.0);
        for (int L = 1; L <= maxL; ++L)
        {
            double a = 0;
            for (int j = 0; j < W; ++j) { const double e = x[(size_t) (s + j)] - x[(size_t) (s + j + L)]; a += e * e; }
            d[(size_t) L] = a;
            run += a;
            cm[(size_t) L] = a * L / run;
        }
        for (int L = minL; L <= maxL; ++L)
            if (cm[(size_t) L] < 0.12)
            {
                while (L + 1 <= maxL && cm[(size_t) L + 1] < cm[(size_t) L]) ++L;
                best = L;
                break;
            }
        if (best < 0) continue;
        double tf = best;
        const double a = d[(size_t) best - 1], b = d[(size_t) best], c = d[(size_t) best + 1], den = a - 2 * b + c;
        if (den > 0) tf += 0.5 * (a - c) / den;
        midi.push_back (69 + 12 * std::log2 (sr / tf / 440));
    }
    return midi;
}

static double median (std::vector<double> v)
{
    if (v.empty()) return -999;
    std::sort (v.begin(), v.end());
    return v[v.size() / 2];
}

static double spread (const std::vector<double>& v, double around)   // 90th percentile abs deviation
{
    std::vector<double> d;
    for (double x : v) d.push_back (std::abs (x - around));
    std::sort (d.begin(), d.end());
    return d.empty() ? 999 : d[(size_t) (d.size() * 9 / 10)];
}

// spectral centroid of the smoothed envelope between 150 Hz and 5 kHz, averaged over frames
static double centroid (const std::vector<float>& x, double sr, double from, double to)
{
    const int N = 4096;
    double num = 0, den = 0;
    for (double t = from; t + N / sr < to; t += 0.05)
    {
        std::vector<std::complex<double>> a ((size_t) N);
        const int s = (int) (t * sr);
        for (int i = 0; i < N; ++i)
            a[(size_t) i] = x[(size_t) (s + i)] * (0.5 - 0.5 * std::cos (2 * PI * i / (N - 1)));
        fft (a);
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * sr / N;
            if (f < 150 || f > 5000) continue;
            const double p = std::norm (a[(size_t) k]);
            num += f * p;
            den += p;
        }
    }
    return num / std::max (den, 1e-30);
}

static double rms (const std::vector<float>& x, double sr, double from, double to)
{
    double s = 0;
    int n = 0;
    for (int i = (int) (from * sr); i < (int) (to * sr) && i < (int) x.size(); ++i, ++n) s += (double) x[(size_t) i] * x[(size_t) i];
    return std::sqrt (s / std::max (n, 1));
}

static void writeWav (const std::string& name, const std::vector<float>& l, const std::vector<float>& r, double sr)
{
    if (wavDir.empty()) return;
    const std::string path = wavDir + "/" + name + ".wav";
    FILE* f = std::fopen (path.c_str(), "wb");
    if (! f) return;
    const uint32_t n = (uint32_t) l.size(), bytes = n * 6;
    auto u32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto u16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); u32 (36 + bytes); std::fwrite ("WAVEfmt ", 1, 8, f);
    u32 (16); u16 (1); u16 (2); u32 ((uint32_t) sr); u32 ((uint32_t) sr * 6); u16 (6); u16 (24);
    std::fwrite ("data", 1, 4, f); u32 (bytes);
    for (uint32_t i = 0; i < n; ++i)
        for (float v : { l[i], r[i] })
        {
            const int s = (int) std::lround (std::clamp (v, -1.0f, 1.0f) * 8388607.0f);
            const uint8_t b[3] = { (uint8_t) (s & 255), (uint8_t) ((s >> 8) & 255), (uint8_t) ((s >> 16) & 255) };
            std::fwrite (b, 1, 3, f);
        }
    std::fclose (f);
}

struct Rendered { std::vector<float> l, r; int latency = 0; double seconds = 0; };

static Rendered render (const std::vector<float>& in, double sr, EngineParams p, int block = 256)
{
    Engine e;
    e.setParams (p);
    e.prepare (sr);
    Rendered out;
    out.l.resize (in.size());
    out.r.resize (in.size());
    out.latency = e.getLatency();
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t pos = 0; pos < in.size(); pos += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, in.size() - pos);
        const float* ins[1] = { in.data() + pos };
        float* outs[2] = { out.l.data() + pos, out.r.data() + pos };
        e.process (ins, 1, outs, 2, n);
    }
    out.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return out;
}

static std::vector<float> shiftBack (const std::vector<float>& x, int latency)
{
    std::vector<float> y (x.size(), 0.0f);
    for (size_t i = (size_t) latency; i < x.size(); ++i) y[i - (size_t) latency] = x[i];
    return y;
}

static bool finite (const Rendered& r)
{
    for (size_t i = 0; i < r.l.size(); ++i)
        if (! std::isfinite (r.l[i]) || ! std::isfinite (r.r[i]) || std::abs (r.l[i]) > 4.0f) return false;
    return true;
}

// ---------------------------------------------------------------- checks
static void runAt (double sr)
{
    std::printf ("\n=== %.0f Hz ===\n", sr);
    Engine probe;
    probe.prepare (sr);
    std::printf ("  latency %d samples (%.1f ms)\n", probe.getLatency(), 1000.0 * probe.getLatency() / sr);

    const double baseHz = 196.0;   // G3
    const double baseMidi = 69 + 12 * std::log2 (baseHz / 440);
    VoiceSpec flat { [=] (double) { return baseHz; }, 3.0, false };
    const auto v = makeVoice (sr, flat);
    const double vin = rms (v, sr, 0.5, 2.8), cin = centroid (v, sr, 0.5, 2.5);

    // transpose, formants kept; level and formant position averaged over three voices
    // (a single voice is biased by where its harmonics happen to fall on the formants)
    for (float st : { -12.0f, -7.0f, -3.0f, 0.0f, 4.0f, 7.0f, 12.0f })
    {
        double lvSum = 0, cSum = 0, errMax = 0, sprMax = 0;
        for (double hz : { 165.0, 196.0, 230.0 })
        {
            const double bm = 69 + 12 * std::log2 (hz / 440);
            VoiceSpec vs { [=] (double) { return hz; }, 2.5, false };
            const auto vv = makeVoice (sr, vs);
            EngineParams p; p.pitch = st;
            auto r = render (vv, sr, p);
            auto y = shiftBack (r.l, r.latency);
            const auto tr = pitchTrack (y, sr, 0.5, 2.2);
            const double m = median (tr);
            errMax = std::max (errMax, std::abs (m - bm - st) * 100);
            sprMax = std::max (sprMax, spread (tr, m) * 100);
            cSum += centroid (y, sr, 0.5, 2.0) / centroid (vv, sr, 0.5, 2.0);
            lvSum += 20 * std::log10 (rms (y, sr, 0.5, 2.2) / rms (vv, sr, 0.5, 2.2));
            if (hz == 196.0 && (st == 7.0f || st == -12.0f)) writeWav ("transpose" + std::to_string ((int) st), r.l, r.r, sr);
        }
        char what[96];
        std::snprintf (what, sizeof what, "transpose %+3.0f st: worst pitch error (cents)", st);
        check (errMax < 8, what, errMax);
        std::snprintf (what, sizeof what, "transpose %+3.0f st: worst pitch steadiness, p90 (cents)", st);
        check (sprMax < 15, what, sprMax);
        std::snprintf (what, sizeof what, "transpose %+3.0f st: formant centroid ratio (mean)", st);
        check (cSum / 3 > 0.88 && cSum / 3 < 1.25, what, cSum / 3);
        std::snprintf (what, sizeof what, "transpose %+3.0f st: level change, mean (dB)", st);
        check (std::abs (lvSum / 3) < 2.5, what, lvSum / 3);
    }

    // formant only, pitch kept
    for (float fs : { -12.0f, -5.0f, 5.0f, 12.0f })
    {
        EngineParams p; p.formant = fs;
        auto r = render (v, sr, p);
        auto y = shiftBack (r.l, r.latency);
        const double m = median (pitchTrack (y, sr, 0.5, 2.7));
        char what[96];
        std::snprintf (what, sizeof what, "formant %+3.0f st: pitch error (cents)", fs);
        check (std::abs ((m - baseMidi) * 100) < 8, what, (m - baseMidi) * 100);
        const double want = std::pow (2.0, fs / 12.0), c = centroid (y, sr, 0.5, 2.5) / cin;
        std::snprintf (what, sizeof what, "formant %+3.0f st: centroid ratio (want %.2f)", fs, want);
        check (c / want > 0.8 && c / want < 1.25, what, c);
        const double lv = 20 * std::log10 (rms (y, sr, 0.5, 2.7) / vin);
        std::snprintf (what, sizeof what, "formant %+3.0f st: level change (dB)", fs);
        check (std::abs (lv) < 3.0, what, lv);
    }

    // link: chipmunk = plain resampling
    {
        EngineParams p; p.pitch = 7; p.link = true;
        auto r = render (v, sr, p);
        auto y = shiftBack (r.l, r.latency);
        const double m = median (pitchTrack (y, sr, 0.5, 2.7));
        check (std::abs ((m - baseMidi - 7) * 100) < 8, "link +7: pitch error (cents)", (m - baseMidi - 7) * 100);
        const double c = centroid (y, sr, 0.5, 2.5) / cin;
        check (c > 1.5 * 0.82 && c < 1.5 * 1.2, "link +7: centroid ratio (want 1.50)", c);
    }

    // quantize: off-pitch voice with vibrato and a slow slide snaps to the scale
    {
        VoiceSpec off { [=] (double t) { return 220.0 * std::pow (2.0, (0.35 + 0.25 * std::sin (2 * PI * 5.5 * t)) / 12.0); }, 3.0, false };
        const auto vo = makeVoice (sr, off);
        EngineParams p; p.mode = Mode::quantize; p.key = 9; p.scale = 2;   // A minor
        auto r = render (vo, sr, p);
        auto y = shiftBack (r.l, r.latency);
        const auto tr = pitchTrack (y, sr, 0.5, 2.7);
        const double m = median (tr);
        check (std::abs ((m - 57) * 100) < 10, "quantize A minor: lands on A3 (cents off)", (m - 57) * 100);
        check (spread (tr, 57) * 100 < 15, "quantize hard: vibrato removed, p90 (cents)", spread (tr, 57) * 100);
        writeWav ("quantize", r.l, r.r, sr);

        // in A minor, A# (58.4) must go to B (59)
        VoiceSpec bb { [=] (double) { return 440.0 * std::pow (2.0, (58.4 - 69) / 12.0); }, 2.0, false };
        auto r2 = render (makeVoice (sr, bb), sr, p);
        const double m2 = median (pitchTrack (shiftBack (r2.l, r2.latency), sr, 0.5, 1.7));
        check (std::abs ((m2 - 59) * 100) < 10, "quantize A minor: 58.4 -> B3 (cents off)", (m2 - 59) * 100);

        p.retuneMs = 120;
        auto r3 = render (vo, sr, p);
        const auto tr3 = pitchTrack (shiftBack (r3.l, r3.latency), sr, 0.5, 2.7);
        const double s3 = spread (tr3, median (tr3)) * 100;
        check (s3 > 4 && s3 < 30, "quantize 120 ms: some vibrato survives, p90 (cents)", s3);
    }

    // robot: fixed note from the key, and from MIDI
    {
        VoiceSpec gl { [=] (double t) { return 150.0 * std::pow (2.0, (4.0 * t / 3.0) / 12.0); }, 3.0, false };
        const auto vg = makeVoice (sr, gl);
        EngineParams p; p.mode = Mode::robot; p.key = 0;
        auto r = render (vg, sr, p);
        const auto tr = pitchTrack (shiftBack (r.l, r.latency), sr, 0.5, 2.7);
        check (std::abs ((median (tr) - 48) * 100) < 10, "robot key C: monotone C3 (cents off)", (median (tr) - 48) * 100);
        check (spread (tr, 48) * 100 < 12, "robot: steady, p90 (cents)", spread (tr, 48) * 100);
        p.midiNote = 55;
        auto r2 = render (vg, sr, p);
        const double m2 = median (pitchTrack (shiftBack (r2.l, r2.latency), sr, 0.5, 2.7));
        check (std::abs ((m2 - 55) * 100) < 10, "robot MIDI G3 (cents off)", (m2 - 55) * 100);
        writeWav ("robot", r.l, r.r, sr);
    }

    // consonants, gaps and the doubler; everything finite, consonants pass through
    {
        VoiceSpec cs { [=] (double t) { return 180.0 * (1 + 0.02 * std::sin (2 * PI * 5 * t)); }, 4.0, true };
        const auto vc = makeVoice (sr, cs);
        EngineParams p; p.pitch = 5; p.formant = -3;
        auto r = render (vc, sr, p);
        check (finite (r), "consonants: output finite and bounded", 0);
        auto y = shiftBack (r.l, r.latency);
        const double sIn = rms (vc, sr, 1.72, 1.80), sOut = rms (y, sr, 1.72, 1.80);
        check (std::abs (20 * std::log10 (sOut / sIn)) < 3, "consonants: 's' level kept (dB)", 20 * std::log10 (sOut / sIn));
        const double gapOut = rms (y, sr, 1.835, 1.885);
        check (gapOut < 0.01, "gaps stay quiet (rms)", gapOut);

        p.doubleAmt = 1; p.doubleWidth = 1;
        auto d = render (vc, sr, p);
        check (finite (d), "doubler: output finite", 0);
        double num = 0, dl = 0, dr = 0;
        for (size_t i = (size_t) (0.5 * sr); i < (size_t) (3.5 * sr); ++i)
        {
            num += (double) d.l[i] * d.r[i]; dl += (double) d.l[i] * d.l[i]; dr += (double) d.r[i] * d.r[i];
        }
        const double corr = num / std::sqrt (dl * dr);
        check (corr > 0.2 && corr < 0.95, "doubler: stereo correlation (wide but mono safe)", corr);
        const double lvl = 20 * std::log10 (std::sqrt ((dl + dr) / 2 / (3.0 * sr)) / rms (r.l, sr, 0.5, 3.5));
        check (std::abs (lvl) < 3, "doubler: level change vs single voice (dB)", lvl);
        writeWav ("double", d.l, d.r, sr);

        // CPU with everything on
        p.drive = 0.6f; p.mode = Mode::quantize;
        auto c = render (vc, sr, p);
        const double rt = c.seconds / 4.0 * 100;
        check (rt < 6, "CPU, all on (percent of one core, real time)", rt);
        check (finite (c), "drive: output finite", 0);
    }

    // drive keeps level in a sane range
    {
        for (float dr : { 0.3f, 1.0f })
        {
            EngineParams p; p.drive = dr;
            auto r = render (v, sr, p);
            const double lv = 20 * std::log10 (rms (shiftBack (r.l, r.latency), sr, 0.5, 2.7) / vin);
            char what[96];
            std::snprintf (what, sizeof what, "drive %.0f%%: level change (dB)", dr * 100);
            check (lv > -4 && lv < 5, what, lv);
        }
    }

    // mix 0 = dry signal exactly, delayed by the latency
    {
        EngineParams p; p.mix = 0; p.pitch = 7;
        auto r = render (v, sr, p);
        double err = 0;
        for (size_t i = (size_t) r.latency; i < v.size(); ++i) err = std::max (err, (double) std::abs (r.l[i] - v[i - (size_t) r.latency]));
        check (err < 1e-6, "mix 0: dry is the input delayed by the latency (max err)", err);
    }

    // silence in, silence out
    {
        std::vector<float> z ((size_t) sr, 0.0f);
        EngineParams p; p.pitch = 12; p.doubleAmt = 1; p.drive = 1;
        auto r = render (z, sr, p);
        double mx = 0;
        for (float x : r.l) mx = std::max (mx, (double) std::abs (x));
        check (mx < 1e-6, "silence in -> silence out", mx);
    }

    // block size independence
    {
        EngineParams p; p.pitch = 3; p.doubleAmt = 0.5f;
        auto a = render (v, sr, p, 64), b = render (v, sr, p, 1000);
        double mx = 0;
        for (size_t i = 0; i < a.l.size(); ++i) mx = std::max (mx, (double) std::abs (a.l[i] - b.l[i]));
        check (mx < 1e-6, "block size 64 vs 1000: identical output", mx);
    }
}

// ---------------------------------------------------------------- real speech (optional)
// test --speech file.wav  (mono or stereo, 16-bit or 32-bit float PCM, 48 kHz)
static std::vector<float> readWav (const std::string& path, double& srOut)
{
    std::vector<float> out;
    FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return out;
    char id[4];
    uint32_t sz;
    uint16_t fmt = 1, ch = 1, bits = 16;
    uint32_t rate = 48000;
    if (std::fread (id, 1, 4, f) != 4 || std::fread (&sz, 4, 1, f) != 1 || std::fread (id, 1, 4, f) != 4) { std::fclose (f); return out; }
    while (std::fread (id, 1, 4, f) == 4 && std::fread (&sz, 4, 1, f) == 1)
    {
        if (std::memcmp (id, "fmt ", 4) == 0)
        {
            std::vector<uint8_t> b (std::max<uint32_t> (sz, 26));
            if (std::fread (b.data(), 1, sz, f) != sz) break;
            std::memcpy (&fmt, &b[0], 2); std::memcpy (&ch, &b[2], 2); std::memcpy (&rate, &b[4], 4); std::memcpy (&bits, &b[14], 2);
            if (fmt == 0xFFFE && sz >= 26) std::memcpy (&fmt, &b[24], 2);
        }
        else if (std::memcmp (id, "data", 4) == 0)
        {
            const uint32_t frames = sz / (ch * (bits / 8));
            for (uint32_t i = 0; i < frames; ++i)
            {
                double acc = 0;
                for (int c = 0; c < ch; ++c)
                {
                    if (fmt == 3 && bits == 32) { float v = 0; if (std::fread (&v, 4, 1, f) == 1) acc += v; }
                    else if (bits == 16) { int16_t v = 0; if (std::fread (&v, 2, 1, f) == 1) acc += v / 32768.0; }
                    else if (bits == 24) { uint8_t b3[3] = {}; if (std::fread (b3, 1, 3, f) == 3) acc += ((int32_t) ((uint32_t) b3[0] << 8 | (uint32_t) b3[1] << 16 | (uint32_t) b3[2] << 24) >> 8) / 8388608.0; }
                }
                out.push_back ((float) (acc / ch));
            }
            break;
        }
        else std::fseek (f, (long) (sz + (sz & 1)), SEEK_CUR);
    }
    std::fclose (f);
    srOut = rate;
    float pk = 0;
    for (float v : out) pk = std::max (pk, std::abs (v));
    if (pk > 0) for (auto& v : out) v *= 0.3f / pk;
    return out;
}

static void runSpeech (const std::string& path)
{
    double sr = 0;
    const auto v = readWav (path, sr);
    std::printf ("\n=== speech: %s (%.2f s, %.0f Hz) ===\n", path.c_str(), v.size() / std::max (sr, 1.0), sr);
    if (v.size() < 4800 || std::abs (sr - 48000) > 1) { check (false, "speech file readable at 48 kHz", sr); return; }
    const double dur = v.size() / sr;
    struct Case { const char* name; EngineParams p; double maxBad; double maxP90; };
    std::vector<Case> cases;
    { EngineParams p; cases.push_back ({ "unchanged", p, 2, 5 }); }
    { EngineParams p; p.pitch = 7; cases.push_back ({ "+7 st", p, 8, 45 }); }
    { EngineParams p; p.pitch = -12; cases.push_back ({ "-12 st", p, 10, 60 }); }
    { EngineParams p; p.pitch = 12; cases.push_back ({ "+12 st", p, 10, 60 }); }
    { EngineParams p; p.formant = 6; cases.push_back ({ "formant +6", p, 8, 40 }); }
    { EngineParams p; p.formant = -6; cases.push_back ({ "formant -6", p, 8, 40 }); }
    { EngineParams p; p.mode = Mode::quantize; cases.push_back ({ "quantize", p, 8, 45 }); }
    { EngineParams p; p.mode = Mode::robot; cases.push_back ({ "robot C3", p, 8, 30 }); }
    for (auto& c : cases)
    {
        auto r = render (v, sr, c.p);
        check (finite (r), (std::string (c.name) + ": output finite").c_str(), 0);
        auto y = shiftBack (r.l, r.latency);
        std::vector<double> errs;
        int bad = 0;
        for (double t = 0.05; t + 0.06 < dur; t += 0.01)
        {
            auto a = pitchTrack (v, sr, t, t + 0.04175), b = pitchTrack (y, sr, t, t + 0.04175);
            if (a.size() != 1 || b.size() != 1) continue;
            double want = a[0] + c.p.pitch;
            if (c.p.mode == Mode::quantize) want = std::round (b[0]);   // must sit on a semitone
            if (c.p.mode == Mode::robot) want = 48;
            const double e = std::abs (b[0] - want) * 100;
            errs.push_back (e);
            if (e > 50) ++bad;
        }
        std::sort (errs.begin(), errs.end());
        const double pBad = errs.empty() ? 100 : 100.0 * bad / errs.size();
        const double p90 = errs.empty() ? 999 : errs[errs.size() * 9 / 10];
        char what[96];
        std::snprintf (what, sizeof what, "%s: frames off by > 50 cents (%%, of %zu)", c.name, errs.size());
        check (errs.size() >= 6 && pBad <= c.maxBad, what, pBad);
        std::snprintf (what, sizeof what, "%s: p90 pitch error (cents)", c.name);
        check (p90 <= c.maxP90, what, p90);
    }
}

int main (int argc, char** argv)
{
    std::vector<std::string> speech;
    bool synthetic = true;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp (argv[i], "--wav") == 0 && i + 1 < argc) wavDir = argv[++i];
        else if (std::strcmp (argv[i], "--speech") == 0 && i + 1 < argc) speech.push_back (argv[++i]);
        else if (std::strcmp (argv[i], "--speech-only") == 0) synthetic = false;
    }
    if (synthetic)
        for (double sr : { 48000.0, 44100.0, 96000.0 })
            runAt (sr);
    for (auto& s : speech) runSpeech (s);
    std::printf ("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
