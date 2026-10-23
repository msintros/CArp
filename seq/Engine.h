// Engine.h - deterministic, tick-based sequencing core. Real-time safe in process().
#pragma once
#include "Model.h"
#include <atomic>
#include <limits>

namespace seq {

constexpr int64_t NEVER = std::numeric_limits<int64_t>::max();

struct TickEvent { int64_t tick; uint8_t status, d1, d2; };
struct MidiOut   { int sampleOffset; uint8_t data[3]; };
enum class Cmd : int { None = 0, Start, Pause, Stop, Panic };

// Cycle readout: ticks until the page's pattern repeats exactly (saturates at 2^60).
// Exact when no page uses "eat note"; otherwise a close estimate. Divide by 4*PPQ for bars.
uint64_t pageCycleTicks(const Page& p, bool harmony);
uint64_t percCycleTicks(const PercPage& p);

class Engine {
public:
    Engine();
    ~Engine();

    // ---- control / UI thread ----
    void install(Patch* p);               // setup only, while audio is stopped. Takes ownership.
    void post(Patch* p);                  // live edit; lock-free. Takes ownership. Applied per quantizeTicks.
    void collectGarbage();                // call periodically from the UI thread
    void command(Cmd c) { cmd_.store((int)c); }

    // ---- audio thread ----
    void process(int numSamples, double sampleRate, double bpm, std::vector<MidiOut>& out);

    // ---- lower level, used by tests ----
    void reset();
    void runUntil(int64_t endTick, std::vector<TickEvent>& out);
    bool isRunning() const { return running_.load(); }

    // Playhead for the UI: flat index of the element each module used at its latest onset (-1 = none).
    enum View { VNotes, VOrder, VRhythm, VRests, VTranspose, VTransposeRhythm, VInverse, VInverseRhythm, VChange, VCount };
    int viewIndex(int role, int v) const { return view_[role][v].load(std::memory_order_relaxed); }
    // Percussion playhead: index of the step each part played most recently (-1 = none).
    int partViewIndex(int part) const { return partView_[part].load(std::memory_order_relaxed); }

private:
    struct PageState {
        int64_t next = 0, rCur = 0, restCur = 0, oCur = 0;
        int64_t tCur = 0, trCur = 0, tEnd = NEVER, iCur = 0, irCur = 0, iEnd = NEVER;
        int64_t chCur = 0, chEnd = NEVER;                 // harmony Change clock
        int chord[kMaxChord] = {}; int chordN = 0;        // last chord actually played (dependents read this)
        int base[kMaxChord] = {};  int baseN = 0;         // anchored chord in effect
        bool pending = true;                              // re-anchor on the next source note
    };
    struct PartState { int64_t next = NEVER, cur = 0; bool live = false; };
    struct On { int64_t off; uint8_t ch, pitch; };

    const Page& pageOf(int r) const;
    void initPage(int r, int64_t at);
    void syncParts(int64_t at);
    int64_t nextTick() const;
    void adopt();
    void pollMailbox();
    void applyCommand(int c);
    void retire(const Patch* p);
    void onset(int r, int64_t t);
    void onChange();
    void advanceTimed(int r, int64_t t);
    void anchorHarmony(int pitch);
    void percussion(int64_t t);
    bool audible(bool mute, bool solo) const;
    void noteOn(int ch, int pitch, int vel, int64_t t, int64_t off);
    void flushOffs(int64_t t);
    void allOff(std::vector<TickEvent>& o, int64_t t);

    const Patch* cur_ = nullptr;
    const Patch* staged_ = nullptr;
    int64_t adoptTick_ = 0;
    std::atomic<const Patch*> mail_{nullptr};
    std::atomic<const Patch*> trash_[16];
    std::atomic<unsigned> tHead_{0}, tTail_{0};
    std::atomic<int> cmd_{0};

    PageState st_[3];
    PartState ps_[kMaxParts];
    std::vector<On> on_;
    std::vector<TickEvent>* out_ = nullptr;
    std::vector<TickEvent> tmp_, cmdOut_;
    int64_t now_ = 0, pos_ = 0;
    double fpos_ = 0;
    std::atomic<bool> running_{false};
    bool paused_ = false;
    std::atomic<int> view_[3][VCount];
    std::atomic<int> partView_[kMaxParts];
};

}  // namespace seq
