#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace mascara
{
namespace
{
constexpr double pi = 3.14159265358979323846;
constexpr double minF0 = 65.0, maxF0 = 1000.0;

int64_t pow2AtLeast (double n)
{
    int64_t p = 1;
    while ((double) p < n) p <<= 1;
    return p;
}

float midiFromPeriod (double periodSamples, double sr)
{
    return (float) (69.0 + 12.0 * std::log2 (sr / periodSamples / 440.0));
}

// log(cosh(x)) without overflow
double logCosh (double x)
{
    const double a = std::abs (x);
    return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942;
}
} // namespace

//==============================================================================
const char* Scales::name (int i)
{
    static const char* names[count] = { "Chromatic", "Major", "Minor", "Harmonic Minor",
                                        "Dorian", "Major Pentatonic", "Minor Pentatonic", "Blues" };
    return names[std::clamp (i, 0, count - 1)];
}

uint16_t Scales::mask (int i)
{
    auto m = [] (std::initializer_list<int> steps)
    {
        uint16_t r = 0;
        for (int s : steps) r = (uint16_t) (r | (1u << s));
        return r;
    };
    switch (std::clamp (i, 0, count - 1))
    {
        case 1: return m ({ 0, 2, 4, 5, 7, 9, 11 });
        case 2: return m ({ 0, 2, 3, 5, 7, 8, 10 });
        case 3: return m ({ 0, 2, 3, 5, 7, 8, 11 });
        case 4: return m ({ 0, 2, 3, 5, 7, 9, 10 });
        case 5: return m ({ 0, 2, 4, 7, 9 });
        case 6: return m ({ 0, 3, 5, 7, 10 });
        case 7: return m ({ 0, 3, 5, 6, 7, 10 });
        default: return 0x0fff;
    }
}

//==============================================================================
void Engine::Biquad::lowpass (double fc, double q, double fs)
{
    const double w = 2.0 * pi * fc / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al;
    b0 = (float) ((1.0 - c) * 0.5 / a0);
    b1 = (float) ((1.0 - c) / a0);
    b2 = b0;
    a1 = (float) (-2.0 * c / a0);
    a2 = (float) ((1.0 - al) / a0);
    z1 = z2 = 0;
}

void Engine::prepare (double sampleRate)
{
    sr = sampleRate;
    dec = std::max (1, (int) std::lround (sr / 12000.0));
    const double fsd = sr / dec;
    aa1.lowpass (std::min (3000.0, 0.3 * fsd), 0.54119610, sr);
    aa2.lowpass (std::min (3000.0, 0.3 * fsd), 1.30656296, sr);

    minLag = std::max (2, (int) std::floor (fsd / maxF0));
    maxLag = (int) std::ceil (fsd / minF0);
    yinW = (int) std::ceil (fsd * 0.016);
    hopD = std::max (1, (int) std::lround (fsd * 0.0027));
    dsig.assign ((size_t) pow2AtLeast (4.0 * (yinW + maxLag + hopD)), 0.0f);
    dMask = (int64_t) dsig.size() - 1;
    frame.assign ((size_t) (yinW + maxLag + 1), 0.0f);
    diff.assign ((size_t) maxLag + 2, 0.0);
    cmnd.assign ((size_t) maxLag + 2, 1.0f);

    const double maxPeriod = sr / minF0;
    halfA = maxPeriod;                       // analysis half grain = one period
    halfS = 0.0125 * sr;                     // longest synthesis half grain
    unvoicedHop = 0.004 * sr;
    // Marks are made as soon as the tracker has an estimate centred on them (guard).
    // Synthesis reads ~12 ms behind the newest mark, so the mark after the one in use
    // usually exists already and its real spacing can be used (see synthesise()).
    const double detectLag = 0.5 * (yinW + maxLag) * dec + 2.0 * dec;
    guard = detectLag + 2.0;
    const double behind = std::max (0.012 * sr, 1.5 * maxPeriod - guard + 4.0);
    latency = (int) std::ceil (halfS + guard + behind + 2.0);

    raw.assign ((size_t) pow2AtLeast (0.5 * sr), 0.0f);
    lp.assign (raw.size(), 0.0f);
    ringMask = (int64_t) raw.size() - 1;

    const auto accSize = (size_t) pow2AtLeast (4.0 * halfS + 0.05 * sr);
    accMask = (int64_t) accSize - 1;
    const auto delSize = (size_t) pow2AtLeast (0.06 * sr);
    delayMask = (int64_t) delSize - 1;
    for (auto& v : voices)
    {
        v.acc.assign (accSize, 0.0f);
        v.delay.assign (delSize, 0.0f);
    }
    // doubles: opposite detune and formant offsets, different delays and drift
    voices[1].cents = -9.0f; voices[1].formantSt = 0.25f;  voices[1].lfoRate = 0.13f; voices[1].lfoDepth = 4.0f;
    voices[1].delaySamples = (int) (0.013 * sr); voices[1].jitterSamples = (float) (0.003 * sr);
    voices[2].cents = 11.0f; voices[2].formantSt = -0.35f; voices[2].lfoRate = 0.19f; voices[2].lfoDepth = 5.0f;
    voices[2].delaySamples = (int) (0.021 * sr); voices[2].jitterSamples = (float) (0.003 * sr);
    voices[2].lfoPhase = 0.37f;

    dryL.assign ((size_t) pow2AtLeast (latency + 8.0), 0.0f);
    dryR.assign (dryL.size(), 0.0f);
    dcR = (float) std::exp (-2.0 * pi * 12.0 / sr);
    smoothA = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));
    reset();
}

void Engine::reset()
{
    std::fill (raw.begin(), raw.end(), 0.0f);
    std::fill (lp.begin(), lp.end(), 0.0f);
    std::fill (dsig.begin(), dsig.end(), 0.0f);
    std::fill (dryL.begin(), dryL.end(), 0.0f);
    std::fill (dryR.begin(), dryR.end(), 0.0f);
    aa1.z1 = aa1.z2 = aa2.z1 = aa2.z2 = 0;
    written = 0; dWritten = 0; decPhase = 0; hopCounter = 0;
    estCount = 0; recentCount = 0; voicedState = false;
    markCount = 0; nextMarkT = 0;
    for (auto& v : voices)
    {
        std::fill (v.acc.begin(), v.acc.end(), 0.0f);
        std::fill (v.delay.begin(), v.delay.end(), 0.0f);
        v.next = 0; v.prevVoiced = false; v.hasCorrection = false; v.correction = 0; v.lastNote = -1; v.running = true;
    }
    drvPrev = {}; dcX = {}; dcY = {};
    mixS = params.mix;
    outS = std::pow (10.0f, params.outputDb / 20.0f);
    dblS = params.doubleAmt;
    widthS = params.doubleWidth;
    driveS = params.drive;
    shownInput = -1.0f; shownTarget = -1.0f;
}

//==============================================================================
void Engine::pushInput (float x)
{
    const float y = aa2.run (aa1.run (x));
    raw[(size_t) (written & ringMask)] = x;
    lp[(size_t) (written & ringMask)] = y;
    ++written;
    if (++decPhase >= dec)
    {
        decPhase = 0;
        dsig[(size_t) (dWritten & dMask)] = y;
        ++dWritten;
        if (++hopCounter >= hopD && dWritten >= yinW + maxLag + 1)
        {
            hopCounter = 0;
            runDetector();
        }
    }
}

void Engine::runDetector()
{
    const int span = yinW + maxLag;
    const int64_t s = dWritten - span;
    double energy = 0;
    for (int i = 0; i < span; ++i)
    {
        frame[(size_t) i] = dsig[(size_t) ((s + i) & dMask)];
        energy += (double) frame[(size_t) i] * frame[(size_t) i];
    }
    const double rms = std::sqrt (energy / span);

    // difference function and its cumulative mean normalised form
    double running = 0;
    cmnd[0] = 1.0f;
    for (int tau = 1; tau <= maxLag; ++tau)
    {
        double d = 0;
        const float* a = frame.data();
        const float* b = frame.data() + tau;
        for (int j = 0; j < yinW; ++j)
        {
            const float e = a[j] - b[j];
            d += (double) e * e;
        }
        diff[(size_t) tau] = d;
        running += d;
        cmnd[(size_t) tau] = running > 0 ? (float) (d * tau / running) : 1.0f;
    }

    const float tr = std::clamp (params.tracking, 0.0f, 1.0f);
    const float thr = (0.06f + 0.26f * tr) * (voicedState ? 1.35f : 1.0f);
    const double gate = std::pow (10.0, (-48.0 - 22.0 * tr) / 20.0);

    int tau = -1;
    for (int t = minLag; t <= maxLag; ++t)
        if (cmnd[(size_t) t] < thr)
        {
            while (t + 1 <= maxLag && cmnd[(size_t) t + 1] < cmnd[(size_t) t]) ++t;
            tau = t;
            break;
        }
    // octave-down slip: the dip one octave up is almost as good and matches the previous period
    if (tau > 0 && voicedState && recentCount > 0)
    {
        const float prev = recent[(size_t) ((recentCount - 1) % 3)] / (float) dec;
        if (tau > 1.8f * prev && tau < 2.2f * prev)
        {
            int h = std::clamp ((int) std::lround (tau * 0.5f), minLag + 1, maxLag - 1);
            while (h > minLag + 1 && cmnd[(size_t) h - 1] < cmnd[(size_t) h]) --h;
            while (h < maxLag - 1 && cmnd[(size_t) h + 1] < cmnd[(size_t) h]) ++h;
            if (std::abs (h - tau * 0.5f) < 0.15f * tau && cmnd[(size_t) h] < std::max (thr * 1.6f, cmnd[(size_t) tau] + 0.1f))
                tau = h;
        }
    }
    float ap = 1.0f;
    if (tau < 0)
    {
        tau = minLag;
        for (int t = minLag; t <= maxLag; ++t)
            if (cmnd[(size_t) t] < cmnd[(size_t) tau]) tau = t;
    }
    ap = cmnd[(size_t) tau];

    Estimate e;
    e.centre = (double) (s * dec) + 0.5 * span * dec;
    e.aperiodicity = ap;
    e.voiced = ap < thr && rms > gate;

    if (e.voiced)
    {
        // parabolic refinement on the decimated difference function
        double tf = tau;
        if (tau > 1 && tau < maxLag)
        {
            const double a = diff[(size_t) tau - 1], b = diff[(size_t) tau], c = diff[(size_t) tau + 1];
            const double den = a - 2.0 * b + c;
            if (den > 0) tf += std::clamp (0.5 * (a - c) / den, -0.5, 0.5);
        }
        double period = tf * dec;

        // refine at full rate around the decimated estimate
        if (dec > 1)
        {
            const int64_t a0 = s * dec;
            const int wf = yinW * dec;
            const int stride = std::max (1, dec / 4);
            const int lo = std::max (2, (int) std::floor (period) - dec);
            const int hi = std::min ((int) (maxLag * dec), (int) std::ceil (period) + dec);
            double best = 1e300, dm1 = 0, dp1 = 0;
            int bestL = -1;
            std::vector<double> dl ((size_t) (hi - lo + 1));
            for (int L = lo; L <= hi; ++L)
            {
                double d = 0;
                for (int j = 0; j < wf; j += stride)
                {
                    const float x0 = lp[(size_t) ((a0 + j) & ringMask)];
                    const float x1 = lp[(size_t) ((a0 + j + L) & ringMask)];
                    d += (double) (x0 - x1) * (x0 - x1);
                }
                dl[(size_t) (L - lo)] = d;
                if (d < best) { best = d; bestL = L; }
            }
            if (bestL > lo && bestL < hi)
            {
                dm1 = dl[(size_t) (bestL - 1 - lo)];
                dp1 = dl[(size_t) (bestL + 1 - lo)];
                const double den = dm1 - 2.0 * best + dp1;
                period = bestL + (den > 0 ? std::clamp (0.5 * (dm1 - dp1) / den, -0.5, 0.5) : 0.0);
            }
            else if (bestL > 0)
                period = bestL;
        }

        // median of the last three voiced periods removes single octave slips
        recent[(size_t) (recentCount % 3)] = (float) period;
        ++recentCount;
        if (recentCount >= 3)
        {
            auto r = recent;
            std::sort (r.begin(), r.end());
            // only replace if the current value is an outlier (keeps fast glides intact)
            if (std::abs (period / r[1] - 1.0) > 0.12) period = r[1];
        }
        e.period = (float) period;
    }
    else
    {
        recentCount = 0;
        e.period = 0;
    }
    voicedState = e.voiced;
    est[(size_t) (estCount % estCap)] = e;
    ++estCount;
}

const Engine::Estimate& Engine::estimateAt (double t) const
{
    if (estCount == 0) return silent;
    const int64_t first = std::max<int64_t> (0, estCount - estCap);
    const double c0 = est[(size_t) (first % estCap)].centre;
    const double hop = (double) hopD * dec;
    int64_t k = first + (int64_t) std::llround ((t - c0) / hop);
    k = std::clamp (k, first, estCount - 1);
    return est[(size_t) (k % estCap)];
}

void Engine::makeMarks()
{
    while (nextMarkT <= (double) written - guard - 1.0)
    {
        const auto& e = estimateAt (nextMarkT);
        Mark m;
        if (e.voiced && e.period > 1.0f)
        {
            const double T = e.period;
            const Mark* prev = markCount > 0 ? &marks[(size_t) ((markCount - 1) % markCap)] : nullptr;
            const bool prevVoiced = prev != nullptr && prev->voiced && nextMarkT - prev->t < 1.6 * T;
            int64_t best;
            if (prevVoiced)
            {
                // the position whose surrounding period looks most like the previous one
                // (normalised cross-correlation); stays on the same point of the waveform
                const auto half = (int64_t) std::lround (0.5 * T);
                const auto lo = (int64_t) std::ceil (nextMarkT - 0.2 * T), hi = (int64_t) std::floor (nextMarkT + 0.2 * T);
                if (hi + half > written - 3) break;    // wait for the data
                const auto p0 = (int64_t) prev->t;
                const int stride = std::max (1, dec / 2);
                best = lo;
                double bestScore = -1e30;
                for (int64_t c = lo; c <= hi; ++c)
                {
                    double xy = 0, yy = 1e-12;
                    for (int64_t j = -half; j < half; j += stride)
                    {
                        const float a = lp[(size_t) ((p0 + j) & ringMask)], b = lp[(size_t) ((c + j) & ringMask)];
                        xy += (double) a * b;
                        yy += (double) b * b;
                    }
                    const double score = xy / std::sqrt (yy);
                    if (score > bestScore) { bestScore = score; best = c; }
                }
            }
            else
            {
                // onset: anchor on the largest peak of the first period
                const auto lo = (int64_t) std::ceil (nextMarkT), hi = (int64_t) std::floor (nextMarkT + T - 1.0);
                if (hi > written - 3) break;
                best = lo;
                float bestV = -1e30f;
                for (int64_t i = lo; i <= hi; ++i)
                {
                    const float v = lp[(size_t) (i & ringMask)];
                    if (v > bestV) { bestV = v; best = i; }
                }
            }
            m.t = (double) best;
            m.period = (float) T;
            m.voiced = true;
            nextMarkT = m.t + T;
        }
        else
        {
            m.t = nextMarkT;
            m.period = (float) unvoicedHop;
            m.voiced = false;
            nextMarkT += unvoicedHop;
        }
        marks[(size_t) (markCount % markCap)] = m;
        ++markCount;
    }
}

int64_t Engine::nearestMark (double t) const
{
    if (markCount == 0) return -1;
    int64_t lo = std::max<int64_t> (0, markCount - markCap), hi = markCount - 1;
    if (marks[(size_t) (hi % markCap)].t <= t) return hi;
    if (marks[(size_t) (lo % markCap)].t >= t) return lo;
    while (hi - lo > 1)
    {
        const int64_t mid = (lo + hi) / 2;
        if (marks[(size_t) (mid % markCap)].t <= t) lo = mid; else hi = mid;
    }
    const double dl = t - marks[(size_t) (lo % markCap)].t, dh = marks[(size_t) (hi % markCap)].t - t;
    return dl <= dh ? lo : hi;
}

float Engine::readCubic (double t) const
{
    const double fl = std::floor (t);
    const auto i = (int64_t) fl;
    const float f = (float) (t - fl);
    const float y0 = raw[(size_t) ((i - 1) & ringMask)], y1 = raw[(size_t) (i & ringMask)];
    const float y2 = raw[(size_t) ((i + 1) & ringMask)], y3 = raw[(size_t) ((i + 2) & ringMask)];
    const float c1 = 0.5f * (y2 - y0);
    const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
    return ((c3 * f + c2) * f + c1) * f + y1;
}

//==============================================================================
float Engine::targetNote (Voice& v, float midiIn, float periodSamples)
{
    switch (params.mode)
    {
        case Mode::transpose:
            return midiIn + params.pitch;

        case Mode::quantize:
        {
            const uint16_t mask = Scales::mask (params.scale);
            auto inScale = [&] (int note) { return (mask >> (((note - params.key) % 12 + 12) % 12)) & 1u; };
            const int base = (int) std::floor (midiIn);
            int nearest = -1;
            float bestD = 1e9f;
            for (int n = base - 6; n <= base + 7; ++n)
                if (inScale (n) && std::abs (midiIn - (float) n) < bestD) { bestD = std::abs (midiIn - (float) n); nearest = n; }
            // hysteresis: stay on the previous note unless the new one is clearly closer
            if (v.lastNote >= 0 && nearest != v.lastNote && inScale (v.lastNote)
                && std::abs (midiIn - (float) v.lastNote) - bestD < 0.18f)
                nearest = v.lastNote;
            v.lastNote = nearest;
            const float goal = (float) nearest - midiIn;
            if (params.retuneMs < 0.5f || ! v.hasCorrection)
                v.correction = goal;
            else
            {
                const float a = 1.0f - std::exp (-periodSamples / (params.retuneMs * 0.001f * (float) sr));
                v.correction += (goal - v.correction) * a;
            }
            v.hasCorrection = true;
            return midiIn + v.correction + std::round (params.pitch);
        }

        case Mode::robot:
        default:
            if (params.midiNote >= 0) return (float) params.midiNote;
            return 48.0f + (float) params.key + std::round (params.pitch);
    }
}

void Engine::synthesise (Voice& v, bool isMain)
{
    const double r = (double) written - 1.0;        // output sample read this step
    const float knobFormant = params.formant + (params.link ? (params.mode == Mode::transpose ? params.pitch : std::round (params.pitch)) : 0.0f);

    while (v.next - halfS <= r + 1.0)
    {
        const double ta = v.next - latency;
        const int64_t k = nearestMark (ta);
        const Mark* m = k >= 0 ? &marks[(size_t) (k % markCap)] : nullptr;
        bool voiced = m != nullptr && m->voiced && std::abs (m->t - ta) < m->period;

        // voiced onset: jump the synthesis clock forward so this grain sits exactly on the
        // first period mark at or after ta (never backwards: that output is already out)
        if (voiced && ! v.prevVoiced)
        {
            int64_t a = k;
            if (marks[(size_t) (a % markCap)].t < ta) ++a;
            if (a < markCount && marks[(size_t) (a % markCap)].voiced)
            {
                v.prevVoiced = true;
                v.next = marks[(size_t) (a % markCap)].t + latency;
                continue;
            }
        }
        v.prevVoiced = voiced;

        const float f = std::clamp (std::pow (2.0f, (knobFormant + v.formantSt) / 12.0f), 0.5f, 2.0f);
        double centre, hop, halfL;
        float gain = 1.0f, p = 1.0f;
        if (voiced)
        {
            const float Ta = m->period;
            centre = m->t;
            // Step by the real distance to the next analysis mark (not the estimated period):
            // the synthesis clock then stays locked to the input and no period is doubled or skipped.
            // If the next mark is not there yet, the previous spacing is used: the errors of that
            // guess telescope (s[k-1] - s[k] summed over k), so the clock still cannot drift away.
            double spacing = Ta;
            auto plausible = [&] (double s) { return s > 0.6 * Ta && s < 1.5 * Ta; };
            const Mark* nx = k + 1 < markCount ? &marks[(size_t) ((k + 1) % markCap)] : nullptr;
            const Mark* pv = k - 1 >= std::max<int64_t> (0, markCount - markCap) ? &marks[(size_t) ((k - 1) % markCap)] : nullptr;
            if (nx != nullptr && nx->voiced && plausible (nx->t - m->t)) spacing = nx->t - m->t;
            else if (pv != nullptr && pv->voiced && plausible (m->t - pv->t)) spacing = m->t - pv->t;

            // the note decision uses the smooth tracker estimate; the ratio is taken against the
            // real local period, so Quantize / Robot output exactly the target note
            const float midiIn = midiFromPeriod (Ta, sr);
            const float target = targetNote (v, midiIn, Ta);
            const float midiLocal = params.mode == Mode::transpose ? midiIn : midiFromPeriod (spacing, sr);
            const float lfo = v.lfoDepth * std::sin (2.0f * (float) pi * v.lfoPhase);
            p = std::clamp (std::pow (2.0f, (target - midiLocal) / 12.0f + (v.cents + lfo) / 1200.0f), 0.25f, 4.0f);
            hop = spacing / p;
            // at unison (within a cent) pull the output periods onto the input periods: 0 st is transparent
            const double nearUnison = std::clamp (1.0 - std::abs (std::log2 ((double) p)) * 1200.0, 0.0, 1.0);
            hop += nearUnison * std::clamp (0.5 * (m->t - ta), -0.25 * spacing, 0.25 * spacing);
            halfL = std::min ((double) Ta / f, halfS);
            gain = std::sqrt (f / p);
            if (isMain)
            {
                shownInput.store (midiIn, std::memory_order_relaxed);
                shownTarget.store (target, std::memory_order_relaxed);
            }
        }
        else
        {
            std::uniform_real_distribution<float> u (-1.0f, 1.0f);
            centre = ta + (v.jitterSamples > 0 ? u (rng) * v.jitterSamples : 0.0f);
            hop = unvoicedHop;
            halfL = unvoicedHop;
            v.hasCorrection = false;
            if (isMain && m != nullptr && ! m->voiced)
            {
                shownInput.store (-1.0f, std::memory_order_relaxed);
                shownTarget.store (-1.0f, std::memory_order_relaxed);
            }
        }
        if (isMain) { lastGrainP = p; lastGrainF = f; }

        const auto k0 = (int64_t) std::ceil (v.next - halfL), k1 = (int64_t) std::floor (v.next + halfL);
        const double invHalf = 1.0 / halfL;
        for (int64_t i = k0; i <= k1; ++i)
        {
            const double rel = (double) i - v.next;
            const float w = (float) (0.5 + 0.5 * std::cos (pi * rel * invHalf));
            v.acc[(size_t) (i & accMask)] += w * gain * readCubic (centre + rel * f);
        }
        v.next += hop;
        if (! isMain)
            v.lfoPhase = std::fmod (v.lfoPhase + v.lfoRate * (float) (hop / sr), 1.0f);
    }
}

float Engine::drive (int ch, float x, float amount)
{
    if (amount < 1.0e-4f) { drvPrev[(size_t) ch] = x; return x; }
    const float pre = std::pow (10.0f, amount * 24.0f / 20.0f);
    const double b = 0.25 * amount;          // asymmetry: even harmonics, tube-like
    const double tb = std::tanh (b);
    const double u = (double) x * pre, up = (double) drvPrev[(size_t) ch] * pre;
    drvPrev[(size_t) ch] = x;
    double y;
    const double du = u - up;
    if (std::abs (du) > 1.0e-5)
        y = ((logCosh (u + b) - u * tb) - (logCosh (up + b) - up * tb)) / du;   // antiderivative anti-aliasing
    else
        y = std::tanh (0.5 * (u + up) + b) - tb;
    // remove the DC the asymmetry creates
    const float yf = (float) y;
    const float hp = yf - dcX[(size_t) ch] + dcR * dcY[(size_t) ch];
    dcX[(size_t) ch] = yf;
    dcY[(size_t) ch] = hp;
    const float makeup = std::pow (pre, -0.72f);
    const float wet = hp * makeup;
    const float blend = std::min (1.0f, amount * 6.0f);    // gentle start from a clean signal
    return x + (wet - x) * blend;
}

//==============================================================================
void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int n)
{
    const float mixT = std::clamp (params.mix, 0.0f, 1.0f);
    const float outT = std::pow (10.0f, params.outputDb / 20.0f);
    const float dblT = std::clamp (params.doubleAmt, 0.0f, 1.0f);
    const float widthT = std::clamp (params.doubleWidth, 0.0f, 1.0f);
    const float driveT = std::clamp (params.drive, 0.0f, 1.0f);
    const auto dryMask = (int64_t) dryL.size() - 1;

    for (int i = 0; i < n; ++i)
    {
        const float xl = in[0][i];
        const float xr = numIn > 1 ? in[1][i] : xl;
        dryL[(size_t) (written & dryMask)] = xl;
        dryR[(size_t) (written & dryMask)] = xr;
        pushInput (0.5f * (xl + xr));
        makeMarks();

        mixS += (mixT - mixS) * smoothA;
        outS += (outT - outS) * smoothA;
        dblS += (dblT - dblS) * smoothA;
        widthS += (widthT - widthS) * smoothA;
        driveS += (driveT - driveS) * smoothA;

        synthesise (voices[0], true);
        const bool doublesOn = dblT > 0.0f || dblS > 1.0e-4f;
        for (int d = 1; d < 3; ++d)
        {
            auto& v = voices[(size_t) d];
            if (doublesOn)
            {
                if (! v.running)
                {
                    std::fill (v.acc.begin(), v.acc.end(), 0.0f);
                    v.next = voices[0].next;
                    v.running = true;
                }
                synthesise (v, false);
            }
            else
                v.running = false;
        }

        const int64_t rIdx = written - 1;
        float main = voices[0].acc[(size_t) (rIdx & accMask)];
        voices[0].acc[(size_t) (rIdx & accMask)] = 0.0f;
        float dl = 0.0f, dr = 0.0f;
        for (int d = 1; d < 3; ++d)
        {
            auto& v = voices[(size_t) d];
            float s = 0.0f;
            if (v.running)
            {
                s = v.acc[(size_t) (rIdx & accMask)];
                v.acc[(size_t) (rIdx & accMask)] = 0.0f;
            }
            v.delay[(size_t) (rIdx & delayMask)] = s;
            const float delayed = v.delay[(size_t) ((rIdx - v.delaySamples) & delayMask)];
            const float pos = (d == 1 ? -1.0f : 1.0f) * widthS;               // -1 left .. +1 right
            const float ang = (pos + 1.0f) * 0.25f * (float) pi;
            dl += delayed * std::cos (ang);
            dr += delayed * std::sin (ang);
        }
        const float dblGain = 0.75f * dblS;
        float wl = main + dblGain * dl;
        float wr = main + dblGain * dr;
        // keep the overall level roughly constant as the doubles come in
        const float norm = 1.0f / std::sqrt (1.0f + 0.6f * dblS);
        wl = drive (0, wl * norm, driveS);
        wr = drive (1, wr * norm, driveS);

        const int64_t dIdx = written - 1 - latency;
        const float dryl = dryL[(size_t) (dIdx & dryMask)], dryr = dryR[(size_t) (dIdx & dryMask)];
        const float ol = (dryl + (wl - dryl) * mixS) * outS;
        const float orr = (dryr + (wr - dryr) * mixS) * outS;
        if (numOut > 1) { out[0][i] = ol; out[1][i] = orr; }
        else out[0][i] = 0.5f * (ol + orr);
    }
}
} // namespace mascara
