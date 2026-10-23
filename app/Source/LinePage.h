#pragma once
#include "Widgets.h"

// Melody (role 0), Harmony (role 1) and Bass (role 2) share this page. Harmony differs in three ways:
// Notes are chord rows, Order picks a chord row, and there is a Change module (how long a chord row stays eligible).
class LinePage : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    LinePage(PatchStore& s, seq::Engine& e, int roleIndex) : store(s), engine(e), role(roleIndex)
    {
        // ---- top strip ----
        addAndMakeVisible(chanLabel); chanLabel.setText("Channel", juce::dontSendNotification);
        for (int i = 1; i <= 16; ++i) channel.addItem(juce::String(i), i);
        channel.onChange = [this] {
            const int v = channel.getSelectedId();
            store.modify("", [this, v](seq::Patch& p) { pageRef(p, role).channel = v; });
        };
        addAndMakeVisible(channel);

        addAndMakeVisible(octLabel); octLabel.setText("Octave", juce::dontSendNotification);
        const int octs[5] = { 24, 12, 0, -12, -24 };
        const char* octNames[5] = { "+24", "+12", "0", "-12", "-24" };
        for (int i = 0; i < 5; ++i) octave.addItem(octNames[i], i + 1);
        octave.onChange = [this, octs] {
            const int v = octs[juce::jlimit(0, 4, octave.getSelectedId() - 1)];
            store.modify("", [this, v](seq::Patch& p) { pageRef(p, role).octave = v; });
        };
        addAndMakeVisible(octave);

        addAndMakeVisible(velLabel); velLabel.setText("Velocity", juce::dontSendNotification);
        velocity.setSliderStyle(juce::Slider::LinearHorizontal);
        velocity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 22);
        velocity.setRange(1, 127, 1);
        velocity.onValueChange = [this] {
            const int v = (int) velocity.getValue();
            store.modify("vel" + juce::String(role), [this, v](seq::Patch& p) { pageRef(p, role).velocity = v; });
        };
        velocity.onDragEnd = [this] { store.endGesture(); };
        addAndMakeVisible(velocity);

        for (auto* b : { &muteBtn, &soloBtn })
        {
            b->setClickingTogglesState(true);
            b->setColour(juce::TextButton::buttonOnColourId, b == &muteBtn ? juce::Colour(0xffe5484d) : juce::Colour(0xfff5a623));
            addAndMakeVisible(*b);
        }
        muteBtn.onClick = [this] {
            const bool v = muteBtn.getToggleState();
            store.modify("", [this, v](seq::Patch& p) { pageRef(p, role).mute = v; });
        };
        soloBtn.onClick = [this] {
            const bool v = soloBtn.getToggleState();
            store.modify("", [this, v](seq::Patch& p) { pageRef(p, role).solo = v; });
        };
        addAndMakeVisible(cycleLabel);
        cycleLabel.setJustificationType(juce::Justification::centredRight);

        // ---- modules ----
        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);

        using Mod = seq::Module<int>;
        const int r = role;
        const bool harm = (role == 1);
        auto make = [this](const juce::String& title, ModuleSection::Getter g, ModuleSection::Spec sp, int view) -> ModuleSection* {
            auto* sec = new ModuleSection(store, title, std::move(g), std::move(sp));
            sec->onHeightChanged = [this] { layoutContent(); };
            sections.add(sec);
            content.addAndMakeVisible(sec);
            viewOf.push_back(view);
            return sec;
        };

        ModuleSection::Spec noteSpec;  noteSpec.kind = ElementBox::Kind::Note; noteSpec.minV = 0; noteSpec.maxV = 127;
        noteSpec.defaultValue = harm ? 48 : 60; noteSpec.rowOps = harm; noteSpec.activeIsRow = harm;
        notesNamed = make(harm ? "Chords" : "Notes", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).notes; }, noteSpec, seq::Engine::VNotes);

        ModuleSection::Spec idxSpec;
        if (harm) { idxSpec.minV = -48; idxSpec.maxV = 48; idxSpec.defaultValue = 0; idxSpec.rowOps = true; idxSpec.activeIsRow = true; }
        else      { idxSpec.minV = 1;   idxSpec.maxV = 16; idxSpec.defaultValue = 1; idxSpec.rowOps = false; }
        notesIndex = make(harm ? "Chords (pitch integers, right-most = anchor note)" : "Chord tone (index into Chords)",
                          [r](seq::Patch& p) -> Mod& { return pageRef(p, r).intNotes; }, idxSpec, seq::Engine::VNotes);

        ModuleSection::Spec orderSpec; orderSpec.minV = 1; orderSpec.maxV = 999; orderSpec.defaultValue = 1;
        orderSpec.autoCount = [r](const seq::Patch& pt) {
            const auto& pg = pageRef(pt, r);
            const auto& m = usesIndexNotes(pt, r) ? pg.intNotes : pg.notes;
            if (r == 1) return (int) m.rows.size();                       // harmony: Order picks chord rows
            int n = 0; for (auto& row : m.rows) n += (int) row.size();
            return n;
        };
        make("Order", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).order; }, orderSpec, seq::Engine::VOrder);

        ModuleSection::Spec rhySpec;   rhySpec.minV = 1; rhySpec.maxV = 99; rhySpec.defaultValue = 1;
        if (harm)
        {
            auto* change = make("Change", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).change; }, rhySpec, seq::Engine::VChange);
            change->addUnitPickers([r](seq::Patch& p) -> seq::Unit& { return pageRef(p, r).change.unit; });
        }
        auto* rhythm = make("Rhythm", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).rhythm; }, rhySpec, seq::Engine::VRhythm);
        rhythm->addUnitPickers([r](seq::Patch& p) -> seq::Unit& { return pageRef(p, r).rhythm.unit; });

        ModuleSection::Spec restSpec;  restSpec.kind = ElementBox::Kind::Flag; restSpec.minV = 0; restSpec.maxV = 1; restSpec.defaultValue = 0; restSpec.flagText = "R";
        auto* rests = make("Rests", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).rests; }, restSpec, seq::Engine::VRests);
        if (!harm)   // chords are chosen by the Change clock, so "eat note" has no meaning on the Harmony page
            rests->addCombo("eat note?", 70, juce::StringArray{ "no", "yes" }, 80,
                            [r](const seq::Patch& pt) { return pageRef(pt, r).eatNote ? 1 : 0; },
                            [r](seq::Patch& pt, int i) { pageRef(pt, r).eatNote = (i == 1); });

        ModuleSection::Spec trSpec;    trSpec.minV = -48; trSpec.maxV = 48; trSpec.defaultValue = 0;
        make("Transpose", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).transpose; }, trSpec, seq::Engine::VTranspose);

        auto* trr = make("Transpose Rhythm", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).transposeRhythm; }, rhySpec, seq::Engine::VTransposeRhythm);
        trr->addUnitPickers([r](seq::Patch& p) -> seq::Unit& { return pageRef(p, r).transposeRhythm.unit; });

        ModuleSection::Spec invSpec;   invSpec.kind = ElementBox::Kind::Flag; invSpec.minV = 0; invSpec.maxV = 1; invSpec.defaultValue = 0; invSpec.flagText = "I";
        auto* inv = make("Inverse", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).inverse; }, invSpec, seq::Engine::VInverse);
        inv->addNoteBox("axis:", 42, [r](const seq::Patch& p) { return pageRef(p, r).axis; },
                        [r](seq::Patch& p, int v) { pageRef(p, r).axis = v; });
        inv->addCombo("invert", 48, juce::StringArray{ "before transpose", "after transpose" }, 150,
                      [r](const seq::Patch& p) { return pageRef(p, r).invertBeforeTranspose ? 0 : 1; },
                      [r](seq::Patch& p, int i) { pageRef(p, r).invertBeforeTranspose = (i == 0); });

        auto* ir = make("Inverse Rhythm", [r](seq::Patch& p) -> Mod& { return pageRef(p, r).inverseRhythm; }, rhySpec, seq::Engine::VInverseRhythm);
        ir->addUnitPickers([r](seq::Patch& p) -> seq::Unit& { return pageRef(p, r).inverseRhythm.unit; });

        store.addChangeListener(this);
        refreshAll();
        startTimerHz(30);
    }

    ~LinePage() override
    {
        stopTimer();
        store.removeChangeListener(this);
        viewport.setViewedComponent(nullptr, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop(34).reduced(8, 4);
        auto place = [&top](juce::Component& c, int w) { c.setBounds(top.removeFromLeft(w)); top.removeFromLeft(6); };
        place(chanLabel, 58); place(channel, 64);
        place(octLabel, 52);  place(octave, 76);
        place(velLabel, 62);  place(velocity, 190);
        place(muteBtn, 30);   place(soloBtn, 30);
        cycleLabel.setBounds(top);
        viewport.setBounds(r);
        layoutContent();
    }

private:
    PatchStore& store;
    seq::Engine& engine;
    const int role;

    juce::Label chanLabel, octLabel, velLabel, cycleLabel;
    juce::ComboBox channel, octave;
    juce::Slider velocity;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<ModuleSection> sections;
    std::vector<int> viewOf;
    ModuleSection *notesNamed = nullptr, *notesIndex = nullptr;

    // Which notes module is in force: named pitches, or the "background" integer module
    // (chord-tone indexes for Melody/Bass following Harmony, pitch integers for an anchored Harmony).
    static bool usesIndexNotes(const seq::Patch& pt, int r)
    {
        return r == 0 ? seq::melodyFollowsHarmony(pt.mode)
             : r == 2 ? seq::bassFollowsHarmony(pt.mode)
                      : seq::harmonyAnchorRole(pt.mode) >= 0;
    }

    bool sectionShown(ModuleSection* s) const
    {
        if (s == notesNamed) return !usesIndexNotes(store.get(), role);
        if (s == notesIndex) return usesIndexNotes(store.get(), role);
        return true;
    }

    void layoutContent()
    {
        const int W = std::max(200, viewport.getMaximumVisibleWidth() - 4);
        int y = 4;
        for (auto* s : sections)
        {
            const bool show = sectionShown(s);
            s->setVisible(show);
            if (!show) continue;
            const int h = s->layoutHeight(W - 16);
            s->setBounds(8, y, W - 16, h);
            y += h + 8;
        }
        content.setSize(W, y);
    }

    void refreshAll()
    {
        const auto& p = store.get();
        const auto& pg = pageRef(p, role);
        channel.setSelectedId(pg.channel, juce::dontSendNotification);
        const int octIdx = pg.octave == 24 ? 1 : pg.octave == 12 ? 2 : pg.octave == -12 ? 4 : pg.octave == -24 ? 5 : 3;
        octave.setSelectedId(octIdx, juce::dontSendNotification);
        velocity.setValue(pg.velocity, juce::dontSendNotification);
        muteBtn.setToggleState(pg.mute, juce::dontSendNotification);
        soloBtn.setToggleState(pg.solo, juce::dontSendNotification);
        for (auto* s : sections) s->refresh();

        const double bars = (double) seq::pageCycleTicks(pg, role == 1) / (double) (4 * seq::PPQ);
        juce::String text;
        if (bars <= 0.0)            text = "cycle: -";
        else if (bars > 100000.0)   text = "cycle: > 100000 bars";
        else if (bars < 1.0)        text = "cycle: " + juce::String(bars * 4.0, 2) + " beats";
        else                        text = "cycle: " + juce::String(bars, 2) + " bars";
        cycleLabel.setText(text, juce::dontSendNotification);
        layoutContent();
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refreshAll(); }

    void timerCallback() override
    {
        const bool running = engine.isRunning();
        for (int i = 0; i < sections.size(); ++i)
            sections[i]->setActive(running && sectionShown(sections[i]) ? engine.viewIndex(role, viewOf[(size_t) i]) : -1);
    }
};
