#include "Engine.h"
#include <algorithm>
#include <cmath>

namespace seq {

static inline int64_t wrapIdx(int64_t i, int64_t n) { return ((i % n) + n) % n; }

Patch makeDefaultPatch() {
    Patch p;
    p.melody.channel = 1;  p.melody.notes  = Module<int>(60); p.melody.intNotes  = Module<int>(1);  // C4
    p.harmony.channel = 2; p.harmony.notes = Module<int>(48); p.harmony.intNotes = Module<int>(0);  // C3
    p.bass.channel = 3;    p.bass.notes    = Module<int>(36); p.bass.intNotes    = Module<int>(1);  // C2
    p.perc.channel = 10;
    for (int n : {36, 38, 44, 46}) { PercPart pt; pt.note = n; p.perc.parts.push_back(pt); }       // C2 D2 Ab2 Bb2
    p.finalize();
    return p;
}

// ------------------------------------------------------------------ cycle readout
static constexpr uint64_t kCap = 1ull << 60;
static uint64_t lcm64(uint64_t a, uint64_t b) {
    if (!a || !b) return a ? a : b;
    uint64_t x = a, y = b; while (y) { const uint64_t t = x % y; x = y; y = t; }
    const unsigned __int128 l = (unsigned __int128)(a / x) * b;
    return l > kCap ? kCap : (uint64_t)l;
}
static uint64_t sumDur(const TimedModule& m, uint64_t count) {
    if (count > 1000000) return kCap;
    uint64_t s = 0; for (uint64_t i = 0; i < count; ++i) s += (uint64_t)m.dur((int64_t)i);
    return s > kCap ? kCap : s;
}
static uint64_t pairPeriod(const Module<int>& a, const TimedModule& b) {
    return sumDur(b, lcm64(a.size(), b.size()));
}

uint64_t pageCycleTicks(const Page& p, bool harmony) {
    if (!p.valid(harmony)) return 0;
    uint64_t perNotes = lcm64(p.rhythm.size(), p.rests.size());
    if (!harmony) perNotes = lcm64(perNotes, p.order.size());
    uint64_t period = perNotes / p.rhythm.size() * sumDur(p.rhythm, p.rhythm.size());
    period = lcm64(period, pairPeriod(p.transpose, p.transposeRhythm));
    period = lcm64(period, pairPeriod(p.inverse, p.inverseRhythm));
    if (harmony) period = lcm64(period, pairPeriod(p.order, p.change));
    return period;
}

uint64_t percCycleTicks(const PercPage& p) {
    uint64_t period = 0;
    for (auto& part : p.parts) {
        int64_t step = part.unit.ticks();
        step = p.tempo == TempoMode::Half ? step * 2 : p.tempo == TempoMode::Double ? std::max<int64_t>(1, step / 2) : step;
        period = lcm64(period, std::max<uint64_t>(1, part.steps.size()) * (uint64_t)step);
    }
    return period;
}

// ------------------------------------------------------------------ lifetime
Engine::Engine() {
    for (auto& t : trash_) t.store(nullptr);
    on_.reserve(1024); tmp_.reserve(2048); cmdOut_.reserve(256);
    install(new Patch(makeDefaultPatch()));
}

Engine::~Engine() {
    delete cur_; delete staged_; delete mail_.exchange(nullptr);
    collectGarbage();
}

void Engine::install(Patch* p) {
    p->finalize();
    delete cur_;
    cur_ = p;
    reset();
}

void Engine::post(Patch* p) {
    p->finalize();
    delete mail_.exchange(p);            // an unconsumed older edit is simply superseded
    collectGarbage();
}

void Engine::retire(const Patch* p) {    // audio thread: never frees memory
    const unsigned h = tHead_.load(std::memory_order_relaxed);
    if (h - tTail_.load(std::memory_order_acquire) >= 16) return;  // ring full: leak rather than block
    trash_[h % 16].store(p, std::memory_order_relaxed);
    tHead_.store(h + 1, std::memory_order_release);
}

void Engine::collectGarbage() {
    unsigned t = tTail_.load(std::memory_order_relaxed);
    while (t != tHead_.load(std::memory_order_acquire)) {
        delete trash_[t % 16].load(std::memory_order_relaxed);
        tTail_.store(++t, std::memory_order_release);
    }
}

// ------------------------------------------------------------------ state
const Page& Engine::pageOf(int r) const { return r == 0 ? cur_->melody : r == 1 ? cur_->harmony : cur_->bass; }

void Engine::initPage(int r, int64_t at) {
    const Page& pg = pageOf(r);
    PageState& s = st_[r];
    s = PageState{};
    if (!pg.valid(r == 1)) { s.next = NEVER; return; }
    s.next = at;
    s.tEnd = at + pg.transposeRhythm.dur(0);
    s.iEnd = at + pg.inverseRhythm.dur(0);
    if (r == 1) s.chEnd = at + pg.change.dur(0);
}

void Engine::syncParts(int64_t at) {
    const auto& parts = cur_->perc.parts;
    for (int i = 0; i < kMaxParts; ++i) {
        if (i < (int)parts.size()) { if (!ps_[i].live) { ps_[i].live = true; ps_[i].next = at; ps_[i].cur = 0; } }
        else { ps_[i].live = false; ps_[i].next = NEVER; }
    }
}

void Engine::reset() {
    on_.clear();
    for (int r = 0; r < 3; ++r) initPage(r, 0);
    for (auto& p : ps_) p = PartState{};
    syncParts(0);
    now_ = pos_ = 0; fpos_ = 0;
    for (auto& row : view_) for (auto& v : row) v.store(-1, std::memory_order_relaxed);
    for (auto& v : partView_) v.store(-1, std::memory_order_relaxed);
}

void Engine::adopt() {
    const int oldMode = cur_->mode;
    retire(cur_);
    cur_ = staged_; staged_ = nullptr;
    for (int r = 0; r < 3; ++r) {
        const bool ok = pageOf(r).valid(r == 1);
        if (!ok) { st_[r].next = NEVER; st_[r].chEnd = NEVER; st_[r].tEnd = NEVER; st_[r].iEnd = NEVER; }
        else if (st_[r].next == NEVER) initPage(r, now_);
    }
    if (oldMode != cur_->mode) {         // note data is kept in the patch; only the runtime derivations reset
        for (auto& s : st_) { s.chordN = 0; s.baseN = 0; }
        st_[1].pending = true;
    }
    syncParts(now_);
}

void Engine::pollMailbox() {
    if (const Patch* p = mail_.exchange(nullptr)) {
        if (staged_) retire(staged_);
        staged_ = p;
        const int64_t base = (int64_t)std::ceil(fpos_), q = std::max<int64_t>(0, p->quantizeTicks);
        adoptTick_ = q > 0 ? ((base + q - 1) / q) * q : base;
    }
    if (staged_ && !running_) { now_ = (int64_t)std::ceil(fpos_); adopt(); }
}

// ------------------------------------------------------------------ MIDI helpers
bool Engine::audible(bool mute, bool solo) const {
    const Patch& P = *cur_;
    const bool any = P.melody.solo || P.harmony.solo || P.bass.solo || P.perc.solo;
    return !mute && (!any || solo);
}

void Engine::noteOn(int ch, int pitch, int vel, int64_t t, int64_t off) {
    if (pitch < 0 || pitch > 127) return;
    const uint8_t c = (uint8_t)((std::clamp(ch, 1, 16)) - 1);
    for (size_t i = 0; i < on_.size(); ++i)            // same pitch still sounding: close it first
        if (on_[i].ch == c && on_[i].pitch == pitch) {
            out_->push_back({t, (uint8_t)(0x80 | c), (uint8_t)pitch, 0});
            on_.erase(on_.begin() + (long)i); break;
        }
    out_->push_back({t, (uint8_t)(0x90 | c), (uint8_t)pitch, (uint8_t)std::clamp(vel, 1, 127)});
    on_.push_back({off, c, (uint8_t)pitch});
}

void Engine::flushOffs(int64_t t) {
    for (size_t i = 0; i < on_.size();) {
        if (on_[i].off <= t) {
            out_->push_back({on_[i].off < t ? t : on_[i].off, (uint8_t)(0x80 | on_[i].ch), on_[i].pitch, 0});
            on_.erase(on_.begin() + (long)i);
        } else ++i;
    }
}

void Engine::allOff(std::vector<TickEvent>& o, int64_t t) {
    for (auto& n : on_) o.push_back({t, (uint8_t)(0x80 | n.ch), n.pitch, 0});
    on_.clear();
}

void Engine::applyCommand(int c) {
    const int64_t t = (int64_t)std::ceil(fpos_);
    switch ((Cmd)c) {
        case Cmd::Start: if (!running_) { if (!paused_) reset(); running_ = true; paused_ = false; } break;
        case Cmd::Pause: if (running_) { allOff(cmdOut_, t); running_ = false; paused_ = true; } break;
        case Cmd::Stop:  allOff(cmdOut_, t); reset(); running_ = false; paused_ = false; break;
        case Cmd::Panic:
            allOff(cmdOut_, t);
            for (int ch = 0; ch < 16; ++ch) {
                cmdOut_.push_back({t, (uint8_t)(0xB0 | ch), 123, 0});   // all notes off
                cmdOut_.push_back({t, (uint8_t)(0xB0 | ch), 120, 0});   // all sound off
            }
            break;
        default: break;
    }
}

// ------------------------------------------------------------------ scheduling core
int64_t Engine::nextTick() const {
    int64_t t = NEVER;
    for (auto& n : on_) t = std::min(t, n.off);
    for (auto& s : st_) t = std::min({t, s.next, s.chEnd, s.tEnd, s.iEnd});
    for (auto& p : ps_) if (p.live) t = std::min(t, p.next);
    if (staged_) t = std::min(t, adoptTick_);
    return t;
}

void Engine::onChange() {
    PageState& H = st_[1];
    ++H.oCur; ++H.chCur;
    H.chEnd += cur_->harmony.change.dur(H.chCur);
    if (harmonyAnchorRole(cur_->mode) >= 0) H.pending = true;   // chord is queued; it is anchored by the next source note
    // Change and Order playheads follow the Change clock itself, not the next chord hit. (The Chords row highlight
    // keeps showing the chord that is actually sounding, which only updates when Rhythm plays.)
    const Page& hp = cur_->harmony;
    if (hp.change.size()) view_[1][VChange].store((int)wrapIdx(H.chCur, (int64_t)hp.change.size()), std::memory_order_relaxed);
    if (hp.order.size())  view_[1][VOrder].store((int)wrapIdx(H.oCur, (int64_t)hp.order.size()), std::memory_order_relaxed);
}

// Transpose and Inverse follow their own clocks: each step advances exactly when its duration ends,
// whether or not a note happens to start then. Notes simply read the value in force at their onset.
void Engine::advanceTimed(int r, int64_t t) {
    const Page& pg = pageOf(r);
    PageState& s = st_[r];
    auto pub = [&](int v, int64_t idx, size_t size) {
        view_[r][v].store(size ? (int)wrapIdx(idx, (int64_t)size) : -1, std::memory_order_relaxed);
    };
    while (t >= s.tEnd) {
        ++s.tCur; ++s.trCur; s.tEnd += pg.transposeRhythm.dur(s.trCur);
        pub(VTranspose, s.tCur, pg.transpose.size()); pub(VTransposeRhythm, s.trCur, pg.transposeRhythm.size());
    }
    while (t >= s.iEnd) {
        ++s.iCur; ++s.irCur; s.iEnd += pg.inverseRhythm.dur(s.irCur);
        pub(VInverse, s.iCur, pg.inverse.size()); pub(VInverseRhythm, s.irCur, pg.inverseRhythm.size());
    }
}

void Engine::anchorHarmony(int pitch) {
    PageState& H = st_[1];
    const Page& hp = cur_->harmony;
    const auto& rows = hp.intNotes.rows;
    const auto& row = rows[(size_t)wrapIdx(hp.order.at(H.oCur) - 1, (int64_t)rows.size())];
    H.pending = false;
    if (row.empty()) return;
    const int n = (int)std::min<size_t>(row.size(), kMaxChord);
    for (int i = 0; i < n; ++i) H.base[i] = pitch - (row.back() - row[(size_t)i]);   // built top-down from the anchor
    H.baseN = n;
}

void Engine::onset(int r, int64_t t) {
    const Patch& P = *cur_;
    const Page& pg = pageOf(r);
    PageState& s = st_[r];
    const bool harm = (r == 1);
    if (!pg.valid(harm)) { s.next = NEVER; return; }
    const bool follows = (r == 0 && melodyFollowsHarmony(P.mode)) || (r == 2 && bassFollowsHarmony(P.mode));
    const bool anchored = harm && harmonyAnchorRole(P.mode) >= 0;

    const int64_t d = pg.rhythm.dur(s.rCur++);
    s.next = t + d;
    const bool rest = pg.rests.at(s.restCur++) != 0;
    while (t >= s.tEnd) { ++s.tCur; ++s.trCur; s.tEnd += pg.transposeRhythm.dur(s.trCur); }
    while (t >= s.iEnd) { ++s.iCur; ++s.irCur; s.iEnd += pg.inverseRhythm.dur(s.irCur); }

    int src[kMaxChord]; int n = 0;
    if (harm) {
        if (anchored) { n = s.baseN; for (int i = 0; i < n; ++i) src[i] = s.base[i]; }
        else {
            const auto& rows = pg.notes.rows;
            const auto& row = rows[(size_t)wrapIdx(pg.order.at(s.oCur) - 1, (int64_t)rows.size())];
            n = (int)std::min<size_t>(row.size(), kMaxChord);
            for (int i = 0; i < n; ++i) src[i] = row[(size_t)i];
        }
    } else if (follows) {
        const PageState& H = st_[1];
        if (H.chordN) {
            const int v = pg.intNotes.at(pg.order.at(s.oCur) - 1);       // chord-tone index, 1-based, wraps
            src[0] = H.chord[wrapIdx(v - 1, H.chordN)]; n = 1;
        }
    } else { src[0] = pg.notes.at(pg.order.at(s.oCur) - 1); n = 1; }

    {   // publish playheads for the UI (relaxed, cosmetic)
        auto pub = [&](int v, int64_t idx, size_t size) {
            view_[r][v].store(size ? (int)wrapIdx(idx, (int64_t)size) : -1, std::memory_order_relaxed);
        };
        pub(VRhythm, s.rCur - 1, pg.rhythm.size());
        pub(VRests, s.restCur - 1, pg.rests.size());
        pub(VTranspose, s.tCur, pg.transpose.size());
        pub(VTransposeRhythm, s.trCur, pg.transposeRhythm.size());
        pub(VInverse, s.iCur, pg.inverse.size());
        pub(VInverseRhythm, s.irCur, pg.inverseRhythm.size());
        pub(VOrder, s.oCur, pg.order.size());
        if (harm) {
            pub(VChange, s.chCur, pg.change.size());
            pub(VNotes, pg.order.at(s.oCur) - 1, anchored ? pg.intNotes.rows.size() : pg.notes.rows.size());
        } else {
            pub(VNotes, pg.order.at(s.oCur) - 1, follows ? pg.intNotes.size() : pg.notes.size());
        }
    }

    if (rest) { if (!harm && pg.eatNote) ++s.oCur; return; }
    if (n == 0) return;                                                  // nothing to play yet: behaves as a held rest
    if (!harm) ++s.oCur;

    const int tr = pg.transpose.at(s.tCur);
    const bool inv = pg.inverse.at(s.iCur) != 0;
    int outP[kMaxChord]; int m = 0;
    for (int i = 0; i < n; ++i) {
        int q = src[i];
        if (inv) q = pg.invertBeforeTranspose ? 2 * pg.axis - q + tr : 2 * pg.axis - (q + tr);
        else q += tr;
        q += pg.octave;
        if (q >= 0 && q <= 127) outP[m++] = q;
    }
    if (!m) return;

    if (r == harmonyAnchorRole(P.mode) && st_[1].pending) anchorHarmony(outP[0]);
    for (int i = 0; i < m; ++i) s.chord[i] = outP[i];
    s.chordN = m;

    if (!audible(pg.mute, pg.solo)) return;
    const double g = std::clamp(P.gate, 0.01, 1.0);
    const int64_t len = std::max<int64_t>(1, (int64_t)((double)d * g + 1e-6));
    for (int i = 0; i < m; ++i) noteOn(pg.channel, outP[i], pg.velocity, t, t + len);
}

void Engine::percussion(int64_t t) {
    const PercPage& pp = cur_->perc;
    const int count = (int)std::min<size_t>(pp.parts.size(), kMaxParts);
    for (int i = 0; i < count; ++i) {
        PartState& s = ps_[i];
        if (!s.live || s.next != t) continue;
        const PercPart& part = pp.parts[(size_t)i];
        int64_t step = part.unit.ticks();
        step = pp.tempo == TempoMode::Half ? step * 2 : pp.tempo == TempoMode::Double ? std::max<int64_t>(1, step / 2) : step;
        if (!part.steps.empty()) partView_[i].store((int)wrapIdx(s.cur, (int64_t)part.steps.size()), std::memory_order_relaxed);
        if (!part.steps.empty() && part.steps[(size_t)wrapIdx(s.cur, (int64_t)part.steps.size())] &&
            audible(pp.mute, pp.solo))
            noteOn(pp.channel, part.note, pp.velocity, t, t + std::max<int64_t>(1, pp.gate.ticks()));
        ++s.cur;
        s.next = t + step;
    }
}

void Engine::runUntil(int64_t end, std::vector<TickEvent>& out) {
    out_ = &out;
    for (;;) {
        const int64_t t = nextTick();
        if (t >= end) break;
        now_ = t;
        if (staged_ && adoptTick_ <= t) adopt();
        flushOffs(t);
        if (st_[1].chEnd == t) onChange();
        for (int r = 0; r < 3; ++r) if (st_[r].tEnd <= t || st_[r].iEnd <= t) advanceTimed(r, t);
        // dependency order: sources before the pages that read them
        const int a = harmonyAnchorRole(cur_->mode);
        int seq[3];
        if (a == 2) { seq[0] = 2; seq[1] = 1; seq[2] = 0; }
        else if (a == 0) { seq[0] = 0; seq[1] = 1; seq[2] = 2; }
        else { seq[0] = 1; seq[1] = 0; seq[2] = 2; }
        for (int k = 0; k < 3; ++k) if (st_[seq[k]].next == t) onset(seq[k], t);
        percussion(t);
    }
    pos_ = end;
    out_ = nullptr;
}

// ------------------------------------------------------------------ audio entry point
void Engine::process(int n, double sr, double bpm, std::vector<MidiOut>& out) {
    out.clear(); cmdOut_.clear(); tmp_.clear();
    if (const int c = cmd_.exchange(0)) applyCommand(c);
    pollMailbox();
    for (auto& e : cmdOut_) out.push_back({0, {e.status, e.d1, e.d2}});
    if (!running_ || n <= 0) return;
    const double tps = std::clamp(bpm, 20.0, 400.0) * (double)PPQ / (60.0 * sr);   // ticks per sample
    const double p1 = fpos_ + n * tps;
    runUntil((int64_t)std::ceil(p1), tmp_);
    for (auto& e : tmp_) {
        const long o = std::clamp(std::lround(((double)e.tick - fpos_) / tps), 0L, (long)n - 1);
        out.push_back({(int)o, {e.status, e.d1, e.d2}});
    }
    fpos_ = p1;
}

}  // namespace seq
