# Máscara

Monophonic voice manipulation plugin (VST3 / AU, macOS universal): independent pitch and
formant shifting, hard-tune quantizing to a key and scale, a robot monotone that can follow
MIDI, a built-in vocal doubler and a drive stage. Made for lead vocals, backing vocals and
doubles; it tracks one note at a time, so it is not meant for chords or choirs.

## Controls

| Section | Control | What it does |
|---|---|---|
| Voice | Pitch | -12..+12 semitones. In Quantize it moves in whole semitones; in Robot it picks the note. |
| | Formant | -12..+12 semitones of throat size, independent of pitch. |
| | Link | Formant follows Pitch (plain "tape speed" chipmunk / slowed voice). |
| Mode | Transpose | Shifts the voice and keeps its melody and vibrato. |
| | Quantize | Snaps every note to the nearest note of Key + Scale (8 scales). Retune 0 = hard robotic tune, higher = slower correction that keeps some vibrato. |
| | Robot | One fixed note: the Key root in octave 3 plus the Note knob, or the MIDI note being held. |
| Double | Amount, Width | Two extra voices, detuned (±9..11 cents with slow drift), with small formant offsets and 13 / 21 ms delays, panned by Width. |
| Drive & Output | Drive, Mix, Output | Asymmetric (tube-like) anti-aliased saturation, dry/wet with a latency-aligned dry, output gain. |
| Tracking | Sens | How readily a sound counts as a sung note. Higher catches quiet and breathy notes. |

18 factory presets (Modern Double, Chipmunk, Monster, Helium, Hard Tune, Robot, ...) are in the
preset menu and in the host's program list.

## How it works

| Stage | What it does |
|---|---|
| Tracker | YIN on a 12 kHz copy (anti-aliased, 2.7 ms hop, 65 Hz - 1 kHz), refined on the full-rate signal with a parabolic minimum; octave-down slips are corrected against the previous period, single outliers by a median of three. |
| Period marks | One mark per glottal period. The first mark of a note sits on the largest peak, every next one at the position whose period correlates best with the previous one, so marks stay on the same point of the waveform. |
| Synthesis (TD-PSOLA) | Each output grain is two input periods around a mark under a Hann window. Grains are spaced by the real mark spacing divided by the pitch ratio, so the output clock stays locked to the input (no doubled or dropped periods); at 0 st the output periods sit on the input periods and the voice passes almost unchanged. The formant shift resamples each grain by the formant ratio. Unvoiced sounds (s, sh, breaths) are overlap-added unchanged in pitch. |
| Quantize / Robot | The note decision uses the smooth tracker estimate, the ratio is taken against the real local period, so the output lands exactly on the target note. |
| Output | Doubles, drive (first-order antiderivative anti-aliasing, DC blocker), mix with the dry signal delayed by the plugin latency. |

Latency is about 40 ms at any sample rate and is reported to the host, which compensates it.

## Building

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMASCARA_BUILD_TESTS=ON
cmake --build build --target Mascara_VST3 MascaraTest MascaraHostCheck
build/MascaraTest                                   # synthetic voice at 44.1 / 48 / 96 kHz
build/MascaraTest --speech-only --speech voice.wav  # real recording, 48 kHz mono or stereo
```

`MascaraTest` checks pitch accuracy and steadiness, formant position, level, quantize and robot
notes, consonants, the doubler, drive, dry alignment, silence, block-size independence and CPU.
`MascaraHostCheck` loads the processor like a host (bus layouts, every preset, state round trip,
MIDI) and writes a PNG of the editor.

CI (GitHub Actions, macOS) builds a universal VST3 + AU, runs both checks (also on two public
speech recordings), builds and tests the `.pkg` installer, validates with pluginval (strictness 10)
and auval and publishes the installer to the `downloads` branch.
