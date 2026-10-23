#include "Engine.h"
#include <cstdio>
#include <string>
using namespace seq;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)

struct Note { int64_t tick; int pitch; };

static std::vector<TickEvent> run(Engine& e, Patch* p, int64_t end) {
    e.install(p);
    std::vector<TickEvent> o;
    e.runUntil(end, o);
    return o;
}
static std::vector<Note> ons(const std::vector<TickEvent>& ev, int ch) {
    std::vector<Note> r;
    for (auto& e : ev) if (e.status == (0x90 | (ch - 1))) r.push_back({e.tick, e.d1});
    return r;
}
static Patch base() { return makeDefaultPatch(); }
static void setRows(Module<int>& m, std::vector<std::vector<int>> rows) { m.rows = std::move(rows); }

int main() {
    // 1. defaults: C4 every quarter, 90% gate
    {
        Engine e; auto ev = run(e, new Patch(base()), 3840);
        auto m = ons(ev, 1);
        CHECK(m.size() == 4 && m[0].tick == 0 && m[1].tick == 960 && m[3].pitch == 60);
        bool off = false; for (auto& x : ev) if (x.status == 0x80 && x.tick == 864) off = true;
        CHECK(off);
    }
    // 2. interference of rhythm length 2 against notes/order length 3
    {
        Patch p = base(); setRows(p.melody.notes, {{60, 62, 64}}); setRows(p.melody.order, {{1, 2, 3}});
        setRows(p.melody.rhythm, {{1, 2}});
        Engine e; auto m = ons(run(e, new Patch(p), 7000), 1);
        const int64_t t[] = {0, 960, 2880, 3840, 5760, 6720}; const int pc[] = {60, 62, 64, 60, 62, 64};
        CHECK(m.size() == 6);
        for (size_t i = 0; i < m.size() && i < 6; ++i) CHECK(m[i].tick == t[i] && m[i].pitch == pc[i]);
    }
    // 3. rests: preserve vs eat
    {
        Patch p = base(); setRows(p.melody.notes, {{60, 62, 64}}); setRows(p.melody.order, {{1, 2, 3}});
        setRows(p.melody.rests, {{0, 1}});
        Engine e; auto a = ons(run(e, new Patch(p), 7000), 1);
        CHECK(a.size() >= 4 && a[0].pitch == 60 && a[1].pitch == 62 && a[2].pitch == 64 && a[3].pitch == 60);
        CHECK(a[1].tick == 1920);
        p.melody.eatNote = true;
        auto b = ons(run(e, new Patch(p), 7000), 1);
        CHECK(b.size() >= 3 && b[0].pitch == 60 && b[1].pitch == 64 && b[2].pitch == 62);
    }
    // 4. inversion before / after transposition
    {
        Patch p = base(); setRows(p.melody.notes, {{64}}); setRows(p.melody.inverse, {{1}}); setRows(p.melody.transpose, {{2}});
        Engine e; CHECK(ons(run(e, new Patch(p), 100), 1)[0].pitch == 58);       // 2*60-64+2
        p.melody.invertBeforeTranspose = false;
        CHECK(ons(run(e, new Patch(p), 100), 1)[0].pitch == 54);                 // 2*60-(64+2)
    }
    // 5. anchored chord, built top-down: [2 5 8 14] on F#4 -> F#3 A3 C4 F#4
    {
        Patch p = base(); p.mode = 4; setRows(p.melody.notes, {{66}});
        setRows(p.harmony.intNotes, {{2, 5, 8, 14}});
        Engine e; auto h = ons(run(e, new Patch(p), 100), 2);
        CHECK(h.size() == 4 && h[0].pitch == 54 && h[1].pitch == 57 && h[2].pitch == 60 && h[3].pitch == 66 && h[0].tick == 0);
    }
    // 6. Change only queues: new chord waits for the next Rhythm opportunity
    {
        Patch p = base(); setRows(p.harmony.notes, {{48}, {50}}); setRows(p.harmony.order, {{1, 2}});
        setRows(p.harmony.change, {{3}}); setRows(p.harmony.rhythm, {{2}});   // change every 2880, hit every 1920
        Engine e; auto h = ons(run(e, new Patch(p), 6000), 2);
        CHECK(h.size() == 4);
        const int64_t t[] = {0, 1920, 3840, 5760}; const int pc[] = {48, 48, 50, 48};
        for (size_t i = 0; i < h.size() && i < 4; ++i) CHECK(h[i].tick == t[i] && h[i].pitch == pc[i]);
    }
    // 7. melody reads chord tones from harmony (mode 1); wraps
    {
        Patch p = base(); p.mode = 1; setRows(p.harmony.notes, {{48, 52, 55}});
        setRows(p.melody.intNotes, {{1, 2, 3, 4}}); setRows(p.melody.order, {{1, 2, 3, 4}});
        Engine e; auto m = ons(run(e, new Patch(p), 3840), 1);
        CHECK(m.size() == 4 && m[0].pitch == 48 && m[1].pitch == 52 && m[2].pitch == 55 && m[3].pitch == 48);
    }
    // 8. mute / solo still advance state; solo silences others
    {
        Patch p = base(); p.melody.solo = true;
        Engine e; auto ev = run(e, new Patch(p), 3840);
        CHECK(!ons(ev, 1).empty() && ons(ev, 2).empty() && ons(ev, 3).empty());
    }
    // 9. percussion: tempo modes and absolute gate
    {
        Patch p = base(); p.perc.parts[0].unit.denom = 8; p.perc.parts[0].steps = {1, 0, 1, 0};
        Engine e; auto a = ons(run(e, new Patch(p), 1920), 10);
        CHECK(a.size() == 2 && a[0].tick == 0 && a[1].tick == 960);
        p.perc.tempo = TempoMode::Half; a = ons(run(e, new Patch(p), 3840), 10);
        CHECK(a.size() == 2 && a[1].tick == 1920);
        p.perc.tempo = TempoMode::Double; auto ev = run(e, new Patch(p), 960);
        a = ons(ev, 10); CHECK(a.size() >= 2 && a[1].tick == 480);
        bool off = false; for (auto& x : ev) if (x.status == 0x89 && x.tick == 240) off = true;
        CHECK(off);
    }
    // 10. cycle readout
    {
        Patch p = base(); p.finalize();
        CHECK(pageCycleTicks(p.melody, false) == 960);
        setRows(p.melody.rhythm, {{1, 2}}); setRows(p.melody.order, {{1, 2, 3}}); p.finalize();
        CHECK(pageCycleTicks(p.melody, false) == 8640);   // lcm(2,3)=6 notes -> 3 rhythm cycles of 2880
    }
    // 11. sample-accurate timing through process(), irregular block sizes
    {
        Engine e; e.command(Cmd::Start);
        std::vector<MidiOut> out; std::vector<int64_t> hits; int64_t start = 0;
        const int sizes[] = {64, 137, 512, 1000, 33, 480, 2048};
        int total = 0, k = 0;
        while (total < 96000) {
            const int n = sizes[k++ % 7];
            e.process(n, 48000.0, 120.0, out);
            for (auto& m : out) if (m.data[0] == 0x90) hits.push_back(start + m.sampleOffset);
            start += n; total += n;
        }
        CHECK(hits.size() >= 4);
        for (size_t i = 0; i < hits.size() && i < 4; ++i) CHECK(hits[i] == (int64_t)i * 24000);
    }
    // 12. stop sends note-offs; live patch swap is picked up
    {
        Engine e; e.command(Cmd::Start);
        std::vector<MidiOut> out; e.process(100, 48000.0, 120.0, out);
        e.command(Cmd::Stop); e.process(100, 48000.0, 120.0, out);
        bool off = false; for (auto& m : out) if (m.data[0] == 0x80) off = true;
        CHECK(off && !e.isRunning());
        e.command(Cmd::Start); e.process(10, 48000.0, 120.0, out);
        Patch* p = new Patch(base()); setRows(p->melody.notes, {{72}});
        e.post(p); e.process(48000, 48000.0, 120.0, out);
        bool seen72 = false; for (auto& m : out) if (m.data[0] == 0x90 && m.data[1] == 72) seen72 = true;
        CHECK(seen72);
        e.collectGarbage();
    }
    // 13. transposition is read at note onsets (skips steps shorter than a note) while its playhead keeps true time
    {
        Patch p = base(); setRows(p.melody.rhythm, {{2}});
        setRows(p.melody.transpose, {{0, 1, 2, 3}}); setRows(p.melody.transposeRhythm, {{1}});
        Engine e; auto m = ons(run(e, new Patch(p), 7680), 1);
        CHECK(m.size() == 4 && m[0].pitch == 60 && m[1].pitch == 62 && m[2].pitch == 60 && m[3].pitch == 62);
        std::vector<TickEvent> o; e.install(new Patch(p)); e.runUntil(960 * 3 + 1, o);   // one long note so far
        CHECK(e.viewIndex(0, Engine::VTranspose) == 3);
    }
    // 14. Change/Order playheads follow the Change clock even when Rhythm has not played a new chord yet
    {
        Patch p = base(); setRows(p.harmony.notes, {{48}, {50}}); setRows(p.harmony.order, {{1, 2}});
        setRows(p.harmony.change, {{1, 1, 1, 1}}); setRows(p.harmony.rhythm, {{8}});   // one long chord hit
        Engine e; std::vector<TickEvent> o; e.install(new Patch(p)); e.runUntil(960 * 3 + 1, o);
        CHECK(e.viewIndex(1, Engine::VChange) == 3);
        CHECK(e.viewIndex(1, Engine::VOrder) == 1);      // order cursor 3 wraps over 2 elements
    }
    // 15. percussion playhead steps through every step (blank or not) on the part's own clock
    {
        Patch p = base(); p.perc.parts[0].unit.denom = 8; p.perc.parts[0].steps = {1, 0, 1, 0};
        Engine e; std::vector<TickEvent> o; e.install(new Patch(p)); e.runUntil(481, o);   // steps at 0 and 480
        CHECK(e.partViewIndex(0) == 1);
    }
    std::printf(failures ? "%d failure(s)\n" : "all tests passed\n", failures);
    return failures ? 1 : 0;
}
