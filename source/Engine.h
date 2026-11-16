// Máscara — monophonic voice manipulation engine (no JUCE dependency).
//
//  input (mono sum) ─┬─> pitch tracker (YIN on a decimated copy, refined at full rate)
//                    ├─> analysis marks: one per glottal period, aligned to the period peak
//                    └─> PSOLA synthesis voices (main + 2 doubles)
//                          pitch ratio p   -> spacing of the output grains
//                          formant ratio f -> every grain is resampled by f
//                        -> drive -> mix with the latency-aligned dry signal -> output
#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <random>
#include <vector>

namespace mascara
{
enum class Mode { transpose = 0, quantize = 1, robot = 2 };

struct Scales
{
    static constexpr int count = 8;
    static const char* name (int i);
    static uint16_t mask (int i);   // bit k set = k semitones above the key is in the scale
};

struct EngineParams
{
    float pitch = 0.0f;        // semitones -12..12 (Quantize and Robot round it)
    float formant = 0.0f;      // semitones -12..12
    bool link = false;         // formant follows the pitch knob
    Mode mode = Mode::transpose;
    int key = 0;               // 0 = C
    int scale = 0;             // index into Scales
    float retuneMs = 0.0f;     // Quantize: how fast the correction is applied, 0 = hard
    float drive = 0.0f;        // 0..1
    float mix = 1.0f;          // 0..1
    float doubleAmt = 0.0f;    // 0..1
    float doubleWidth = 0.8f;  // 0..1
    float outputDb = 0.0f;
    float tracking = 0.5f;     // 0..1 voicing sensitivity
    int midiNote = -1;         // Robot: held MIDI note, -1 = none
};

class Engine
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const EngineParams& p) { params = p; }

    // numIn / numOut: 1 or 2 channels. in and out may point to the same buffers.
    void process (const float* const* in, int numIn, float* const* out, int numOut, int n);

    int getLatency() const { return latency; }
    double getSampleRate() const { return sr; }

    // for the editor (written on the audio thread)
    std::atomic<float> shownInput { -1.0f };    // detected MIDI note, -1 = unvoiced / silence
    std::atomic<float> shownTarget { -1.0f };   // MIDI note sent out

    // for tests
    float lastGrainP = 1.0f, lastGrainF = 1.0f;

private:
    struct Estimate { double centre = 0; float period = 0; float aperiodicity = 1; bool voiced = false; };
    struct Mark { double t = 0; float period = 0; bool voiced = false; };

    struct Voice
    {
        double next = 0;            // next synthesis mark, absolute output time
        std::vector<float> acc;     // overlap-add accumulator ring, indexed by absolute time
        float cents = 0, formantSt = 0, lfoPhase = 0, lfoRate = 0, lfoDepth = 0;
        float jitterSamples = 0;
        std::vector<float> delay;   // post delay (doubles)
        int delaySamples = 0;
        bool running = true;
        bool prevVoiced = false;
        float correction = 0;       // Quantize glide state (semitones)
        bool hasCorrection = false;
        int lastNote = -1;
    };

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void lowpass (double fc, double q, double fs);
        float run (float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void pushInput (float x);
    void runDetector();
    void makeMarks();
    const Estimate& estimateAt (double t) const;
    int64_t nearestMark (double t) const;
    void synthesise (Voice& v, bool isMain);
    float targetNote (Voice& v, float midiIn, float periodSamples);
    float readCubic (double t) const;
    float drive (int ch, float x, float amount);

    EngineParams params;
    double sr = 48000;
    int latency = 0;

    // input rings, indexed by absolute sample time
    std::vector<float> raw, lp;
    int64_t ringMask = 0, written = 0;

    // tracker: anti-alias + decimation, YIN
    Biquad aa1, aa2;
    int dec = 4, decPhase = 0;
    std::vector<float> dsig, frame;
    int64_t dMask = 0, dWritten = 0;
    int yinW = 0, minLag = 0, maxLag = 0, hopD = 0, hopCounter = 0;
    std::vector<double> diff;
    std::vector<float> cmnd;

    static constexpr int estCap = 1024;
    std::array<Estimate, estCap> est {};
    int64_t estCount = 0;
    Estimate silent {};
    std::array<float, 3> recent {};
    int recentCount = 0;
    bool voicedState = false;

    static constexpr int markCap = 4096;
    std::array<Mark, markCap> marks {};
    int64_t markCount = 0;
    double nextMarkT = 0;

    double halfA = 0, halfS = 0, unvoicedHop = 0, guard = 0;

    std::array<Voice, 3> voices;
    int64_t accMask = 0, delayMask = 0;

    // drive state per output channel
    std::array<float, 2> drvPrev {}, dcX {}, dcY {};
    float dcR = 0.999f;

    std::vector<float> dryL, dryR;

    float mixS = 1, outS = 1, dblS = 0, widthS = 0.8f, driveS = 0, smoothA = 0.001f;
    std::minstd_rand rng { 12345 };
};
} // namespace mascara
