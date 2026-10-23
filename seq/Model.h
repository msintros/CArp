// Model.h - plain data describing a patch. No JUCE, no threading, no I/O.
// The UI edits the row form (`rows`), then Patch::finalize() flattens it for the engine.
#pragma once
#include <cstdint>
#include <vector>

namespace seq {

constexpr int64_t PPQ = 960;          // ticks per quarter; every 1..1/64, triplet and dotted value is an integer
constexpr int64_t WHOLE = PPQ * 4;
constexpr int kMaxChord = 16;
constexpr int kMaxParts = 64;

enum class Mod : uint8_t { None, Triplet, Dotted };
enum class TempoMode : uint8_t { Normal, Half, Double };

struct Unit {                          // the two rhythm drop-downs
    int denom = 4;                     // 1,2,4,8,16,32,64
    Mod mod = Mod::None;
    int64_t ticks() const {
        const int64_t t = WHOLE / denom;
        return mod == Mod::Triplet ? t * 2 / 3 : mod == Mod::Dotted ? t * 3 / 2 : t;
    }
};

template <class T>
struct Module {
    std::vector<std::vector<T>> rows;  // editing form (what the user sees)
    std::vector<T> flat;               // engine form: rows concatenated
    Module() = default;
    explicit Module(T v) : rows{{v}}, flat{v} {}
    void rebuild() { flat.clear(); for (auto& r : rows) flat.insert(flat.end(), r.begin(), r.end()); }
    size_t size() const { return flat.size(); }
    // Indexing always wraps. Caller guarantees size() > 0 (Page::valid checks this).
    const T& at(int64_t i) const {
        const int64_t n = (int64_t)flat.size();
        return flat[(size_t)(((i % n) + n) % n)];
    }
};

struct TimedModule : Module<int> {     // Rhythm, Change, Transpose Rhythm, Inverse Rhythm
    Unit unit;
    TimedModule() = default;
    explicit TimedModule(int v) : Module<int>(v) {}
    int64_t dur(int64_t i) const { const int v = at(i); return (v < 1 ? 1 : v) * unit.ticks(); }
};

struct Page {                          // Melody, Harmony and Bass share one structure
    int channel = 1;
    int octave = 0;                    // semitones: -24, -12, 0, 12, 24
    int velocity = 100;
    bool mute = false, solo = false;

    Module<int> notes;                 // MIDI pitches. Melody/Bass: all rows concatenated. Harmony: each row is a chord
    Module<int> intNotes;              // kept "in the background": chord-tone indexes (M/B following H) or
                                       // pitch integers (Harmony anchored to M or B)
    Module<int> order{1};              // Melody/Bass: 1-based index into notes. Harmony: 1-based chord row
    TimedModule change{1};             // Harmony only: how long the current chord row is eligible
    TimedModule rhythm{1};
    Module<int> rests{0};              // 0 = play, 1 = rest
    bool eatNote = false;              // true: a rest consumes the note
    Module<int> transpose{0};
    TimedModule transposeRhythm{1};
    Module<int> inverse{0};            // 0/1
    TimedModule inverseRhythm{1};
    int axis = 60;
    bool invertBeforeTranspose = true;

    void rebuildAll() {
        notes.rebuild(); intNotes.rebuild(); order.rebuild(); change.rebuild(); rhythm.rebuild();
        rests.rebuild(); transpose.rebuild(); transposeRhythm.rebuild(); inverse.rebuild(); inverseRhythm.rebuild();
    }
    bool valid(bool harmony) const {
        if (!order.size() || !rhythm.size() || !rests.size() || !transpose.size() ||
            !transposeRhythm.size() || !inverse.size() || !inverseRhythm.size()) return false;
        if (harmony) return change.size() && !notes.rows.empty() && !intNotes.rows.empty();
        return notes.size() && intNotes.size();
    }
};

struct PercPart {
    int note = 36;
    Unit unit;
    std::vector<uint8_t> steps{0};     // 0 blank, 1 = X
};

struct PercPage {
    int channel = 10, velocity = 100;
    bool mute = false, solo = false;
    TempoMode tempo = TempoMode::Normal;
    Unit gate{16, Mod::None};          // absolute hit length; independent of the global Gate
    std::vector<PercPart> parts;
};

// Play modes, in the order of the drop-down:
// 0 M H B   1 M<-H   2 H->B   3 M<-H->B   4 M->H   5 M->H->B   6 H<-B   7 M<-H<-B
inline bool melodyFollowsHarmony(int m) { return m == 1 || m == 3 || m == 7; }
inline bool bassFollowsHarmony(int m)   { return m == 2 || m == 3 || m == 5; }
inline int  harmonyAnchorRole(int m)    { return (m == 4 || m == 5) ? 0 : (m == 6 || m == 7) ? 2 : -1; } // 0=M, 2=B

struct Patch {
    int mode = 0;
    double gate = 0.9;                 // global gate, 0..1
    int64_t quantizeTicks = 0;         // live-edit quantize: 0 immediate, PPQ beat, 4*PPQ bar
    Page melody, harmony, bass;
    PercPage perc;
    void finalize() { melody.rebuildAll(); harmony.rebuildAll(); bass.rebuildAll(); }
};

Patch makeDefaultPatch();

}  // namespace seq
