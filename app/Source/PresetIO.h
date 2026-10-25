#pragma once
#include "Common.h"

// Preset files are plain JSON:  { "format": "seq-preset", "version": 1, "bpm": 120, "patch": { ... } }
// Loading starts from the default patch and overwrites only what the file contains, so presets from
// older versions keep loading when new settings are added later. Every value is range-checked.
// Not saved: mute/solo (performance state, so a preset can't load silently muted), MIDI output device.
namespace PresetIO
{
using juce::var;

constexpr int kVersion = 1;

// ------------------------------------------------------------------ writing
inline var rowsToVar(const std::vector<std::vector<int>>& rows)
{
    juce::Array<var> outer;
    for (auto& r : rows)
    {
        juce::Array<var> inner;
        for (int v : r) inner.add(v);
        outer.add(var(inner));
    }
    return var(outer);
}

inline var unitToVar(const seq::Unit& u)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("denom", u.denom);
    o->setProperty("mod", (int) u.mod);
    return var(o);
}

inline var timedToVar(const seq::TimedModule& m)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("rows", rowsToVar(m.rows));
    o->setProperty("unit", unitToVar(m.unit));
    return var(o);
}

inline var pageToVar(const seq::Page& p)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("channel", p.channel);
    o->setProperty("octave", p.octave);
    o->setProperty("velocity", p.velocity);
    o->setProperty("axis", p.axis);
    o->setProperty("eatNote", p.eatNote);
    o->setProperty("invertBeforeTranspose", p.invertBeforeTranspose);
    o->setProperty("notes", rowsToVar(p.notes.rows));
    o->setProperty("intNotes", rowsToVar(p.intNotes.rows));
    o->setProperty("order", rowsToVar(p.order.rows));
    o->setProperty("rests", rowsToVar(p.rests.rows));
    o->setProperty("transpose", rowsToVar(p.transpose.rows));
    o->setProperty("inverse", rowsToVar(p.inverse.rows));
    o->setProperty("change", timedToVar(p.change));
    o->setProperty("rhythm", timedToVar(p.rhythm));
    o->setProperty("transposeRhythm", timedToVar(p.transposeRhythm));
    o->setProperty("inverseRhythm", timedToVar(p.inverseRhythm));
    return var(o);
}

inline var percToVar(const seq::PercPage& pp)
{
    auto* o = new juce::DynamicObject();
    o->setProperty("channel", pp.channel);
    o->setProperty("velocity", pp.velocity);
    o->setProperty("tempo", (int) pp.tempo);
    o->setProperty("gate", unitToVar(pp.gate));
    juce::Array<var> parts;
    for (auto& part : pp.parts)
    {
        auto* po = new juce::DynamicObject();
        po->setProperty("note", part.note);
        po->setProperty("unit", unitToVar(part.unit));
        juce::Array<var> steps;
        for (auto s : part.steps) steps.add((int) s);
        po->setProperty("steps", var(steps));
        parts.add(var(po));
    }
    o->setProperty("parts", var(parts));
    return var(o);
}

inline var toVar(const seq::Patch& p, double bpm)
{
    auto* patch = new juce::DynamicObject();
    patch->setProperty("mode", p.mode);
    patch->setProperty("gate", p.gate);
    patch->setProperty("quantizeTicks", (int) p.quantizeTicks);
    patch->setProperty("melody", pageToVar(p.melody));
    patch->setProperty("harmony", pageToVar(p.harmony));
    patch->setProperty("bass", pageToVar(p.bass));
    patch->setProperty("percussion", percToVar(p.perc));

    auto* root = new juce::DynamicObject();
    root->setProperty("format", "seq-preset");
    root->setProperty("version", kVersion);
    root->setProperty("bpm", bpm);
    root->setProperty("patch", var(patch));
    return var(root);
}

// ------------------------------------------------------------------ reading
inline int getInt(const var& o, const char* key, int def) { return (int) o.getProperty(key, def); }

inline void readUnit(const var& v, seq::Unit& u)
{
    if (!v.isObject()) return;
    const int d = getInt(v, "denom", u.denom);
    if (d == 1 || d == 2 || d == 4 || d == 8 || d == 16 || d == 32 || d == 64) u.denom = d;
    u.mod = (seq::Mod) juce::jlimit(0, 2, getInt(v, "mod", (int) u.mod));
}

// A missing property keeps the default (returns true); a present but malformed one is an error.
inline bool readRows(const var& v, seq::Module<int>& m, int lo, int hi)
{
    if (v.isVoid()) return true;
    auto* arr = v.getArray();
    if (arr == nullptr) return false;
    std::vector<std::vector<int>> rows;
    for (auto& rv : *arr)
    {
        auto* ra = rv.getArray();
        if (ra == nullptr) return false;
        std::vector<int> row;
        for (auto& e : *ra)
        {
            if (!(e.isInt() || e.isInt64() || e.isDouble())) return false;
            row.push_back(juce::jlimit(lo, hi, (int) e));
        }
        if (!row.empty()) rows.push_back(std::move(row));
    }
    if (rows.empty()) return false;
    m.rows = std::move(rows);
    return true;
}

inline bool readTimed(const var& v, seq::TimedModule& m)
{
    if (v.isVoid()) return true;
    if (!v.isObject()) return false;
    if (!readRows(v.getProperty("rows", var()), m, 1, 999)) return false;
    readUnit(v.getProperty("unit", var()), m.unit);
    return true;
}

inline bool readPage(const var& v, seq::Page& p, const char* name, juce::String& err)
{
    if (v.isVoid()) return true;
    if (!v.isObject()) { err = juce::String(name) + " is not a valid page."; return false; }

    p.channel = juce::jlimit(1, 16, getInt(v, "channel", p.channel));
    const int oct = getInt(v, "octave", p.octave);
    p.octave = (oct == 24 || oct == 12 || oct == -12 || oct == -24) ? oct : 0;
    p.velocity = juce::jlimit(1, 127, getInt(v, "velocity", p.velocity));
    p.axis = juce::jlimit(0, 127, getInt(v, "axis", p.axis));
    p.eatNote = (bool) v.getProperty("eatNote", p.eatNote);
    p.invertBeforeTranspose = (bool) v.getProperty("invertBeforeTranspose", p.invertBeforeTranspose);

    struct Field { const char* key; seq::Module<int>* mod; int lo, hi; };
    const Field fields[] = {
        { "notes", &p.notes, 0, 127 },      { "intNotes", &p.intNotes, -96, 96 }, { "order", &p.order, 1, 999 },
        { "rests", &p.rests, 0, 1 },        { "transpose", &p.transpose, -96, 96 }, { "inverse", &p.inverse, 0, 1 } };
    for (auto& f : fields)
        if (!readRows(v.getProperty(f.key, var()), *f.mod, f.lo, f.hi))
        {
            err = juce::String(name) + ": the \"" + f.key + "\" data is malformed.";
            return false;
        }

    struct TimedField { const char* key; seq::TimedModule* mod; };
    const TimedField timed[] = { { "change", &p.change }, { "rhythm", &p.rhythm },
                                 { "transposeRhythm", &p.transposeRhythm }, { "inverseRhythm", &p.inverseRhythm } };
    for (auto& f : timed)
        if (!readTimed(v.getProperty(f.key, var()), *f.mod))
        {
            err = juce::String(name) + ": the \"" + f.key + "\" data is malformed.";
            return false;
        }
    return true;
}

inline bool readPerc(const var& v, seq::PercPage& pp, juce::String& err)
{
    if (v.isVoid()) return true;
    if (!v.isObject()) { err = "The percussion data is not valid."; return false; }

    pp.channel = juce::jlimit(1, 16, getInt(v, "channel", pp.channel));
    pp.velocity = juce::jlimit(1, 127, getInt(v, "velocity", pp.velocity));
    pp.tempo = (seq::TempoMode) juce::jlimit(0, 2, getInt(v, "tempo", (int) pp.tempo));
    readUnit(v.getProperty("gate", var()), pp.gate);

    const var parts = v.getProperty("parts", var());
    if (parts.isVoid()) return true;
    auto* arr = parts.getArray();
    if (arr == nullptr || arr->isEmpty()) { err = "The percussion parts are missing or malformed."; return false; }

    std::vector<seq::PercPart> out;
    for (auto& pv : *arr)
    {
        if ((int) out.size() >= seq::kMaxParts) break;
        if (!pv.isObject()) { err = "A percussion part is malformed."; return false; }
        seq::PercPart part;
        part.note = juce::jlimit(0, 127, getInt(pv, "note", part.note));
        readUnit(pv.getProperty("unit", var()), part.unit);
        const var st = pv.getProperty("steps", var());
        if (auto* sa = st.getArray())
        {
            part.steps.clear();
            for (auto& e : *sa) part.steps.push_back((int) e != 0 ? 1 : 0);
            if (part.steps.empty()) part.steps.push_back(0);
        }
        out.push_back(std::move(part));
    }
    pp.parts = std::move(out);
    return true;
}

inline bool fromVar(const var& root, seq::Patch& out, double& bpm, juce::String& err)
{
    if (!root.isObject() || root.getProperty("format", var()).toString() != "seq-preset")
    {
        err = "This file isn't a CArp preset.";
        return false;
    }
    const int version = getInt(root, "version", 0);
    if (version < 1 || version > kVersion)
    {
        err = "This preset was saved by a different version of CArp (version " + juce::String(version) + ").";
        return false;
    }
    const var pv = root.getProperty("patch", var());
    if (!pv.isObject()) { err = "The preset contains no patch data."; return false; }

    out = seq::makeDefaultPatch();
    out.mode = juce::jlimit(0, 7, getInt(pv, "mode", out.mode));
    out.gate = juce::jlimit(0.01, 1.0, (double) pv.getProperty("gate", out.gate));
    const int q = getInt(pv, "quantizeTicks", 0);
    out.quantizeTicks = (q == seq::PPQ || q == 4 * seq::PPQ) ? q : 0;
    bpm = juce::jlimit(20.0, 300.0, (double) root.getProperty("bpm", 120.0));

    if (!readPage(pv.getProperty("melody", var()), out.melody, "Melody", err)) return false;
    if (!readPage(pv.getProperty("harmony", var()), out.harmony, "Chords", err)) return false;
    if (!readPage(pv.getProperty("bass", var()), out.bass, "Bass", err)) return false;
    if (!readPerc(pv.getProperty("percussion", var()), out.perc, err)) return false;

    out.finalize();
    return true;
}

}  // namespace PresetIO
