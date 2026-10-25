#pragma once
#include "Widgets.h"

// One percussion part: MIDI note, step length, and a single row of blank / X steps.
class PartSection : public juce::Component
{
public:
    std::function<void()> onHeightChanged;

    PartSection(PatchStore& s, int idx) : store(s), index(idx)
    {
        noteBox.kind = ElementBox::Kind::Note; noteBox.minV = 0; noteBox.maxV = 127;
        noteBox.setSize(56, 26);
        noteBox.onValue = [this](int v) { editPart("note" + juce::String(index), [v](seq::PercPart& p) { p.note = v; }); };
        noteBox.onGestureEnd = [this] { store.endGesture(); };
        addAndMakeVisible(noteBox);

        const char* names[7] = { "1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        for (int i = 0; i < 7; ++i) denom.addItem(names[i], i + 1);
        denom.setSize(80, 24);
        denom.onChange = [this] {
            const int d = 1 << (denom.getSelectedId() - 1);
            editPart("", [d](seq::PercPart& p) { p.unit.denom = d; });
        };
        mod.addItem("<none>", 1); mod.addItem("triplet", 2); mod.addItem("dotted", 3);
        mod.setSize(100, 24);
        mod.onChange = [this] {
            const auto m = (seq::Mod) (mod.getSelectedId() - 1);
            editPart("", [m](seq::PercPart& p) { p.unit.mod = m; });
        };
        addAndMakeVisible(denom);
        addAndMakeVisible(mod);

        addButton("-", 28, [this] { editPart("", [](seq::PercPart& p) { if (p.steps.size() > 1) p.steps.pop_back(); }); });
        addButton("+", 28, [this] { editPart("", [](seq::PercPart& p) { p.steps.push_back(0); }); });
        addButton("-8", 34, [this] {
            editPart("", [](seq::PercPart& p) {
                const size_t keep = p.steps.size() > 8 ? p.steps.size() - 8 : 1;
                p.steps.resize(keep);
            });
        });
        addButton("+8", 34, [this] { editPart("", [](seq::PercPart& p) { p.steps.resize(p.steps.size() + 8, 0); }); });
        addButton("rev", 40, [this] { editPart("", [](seq::PercPart& p) { std::reverse(p.steps.begin(), p.steps.end()); }); });
        addButton("<<", 36, [this] {
            editPart("", [](seq::PercPart& p) { if (!p.steps.empty()) std::rotate(p.steps.begin(), p.steps.begin() + 1, p.steps.end()); });
        });
        addButton(">>", 36, [this] {
            editPart("", [](seq::PercPart& p) { if (!p.steps.empty()) std::rotate(p.steps.begin(), p.steps.end() - 1, p.steps.end()); });
        });
        addButton("copy", 50, [this] {   // duplicates the row: the steps are repeated once at the end of the row
            editPart("", [](seq::PercPart& p) {
                const auto copy = p.steps;
                if (p.steps.size() + copy.size() <= 512) p.steps.insert(p.steps.end(), copy.begin(), copy.end());
            });
        });
    }

    void refresh()
    {
        const auto& parts = store.get().perc.parts;
        if (index >= (int) parts.size()) return;
        const auto& part = parts[(size_t) index];
        noteBox.setValue(part.note);
        int di = 0; while ((1 << di) < part.unit.denom && di < 6) ++di;
        denom.setSelectedId(di + 1, juce::dontSendNotification);
        mod.setSelectedId((int) part.unit.mod + 1, juce::dontSendNotification);
        if ((int) part.steps.size() != stepCount) { rebuildSteps((int) part.steps.size()); if (onHeightChanged) onHeightChanged(); }
        for (int i = 0; i < steps.size() && i < (int) part.steps.size(); ++i) steps[i]->setValue(part.steps[(size_t) i]);
    }

    void setActiveStep(int idx)
    {
        if (idx == activeStep) return;
        activeStep = idx;
        for (int i = 0; i < steps.size(); ++i) steps[i]->setActive(i == idx);
    }

    int layoutHeight(int W)
    {
        const int headerH = 26, boxW = 34, boxH = 28, gap = 6;
        noteBox.setVisible(expanded); denom.setVisible(expanded); mod.setVisible(expanded);
        for (auto* b : steps) b->setVisible(expanded);
        for (auto* b : btns) b->setVisible(expanded);
        if (!expanded) return headerH + 2;

        int y = headerH;
        noteBox.setTopLeftPosition(0, y + 3);
        denom.setTopLeftPosition(66, y + 4);
        mod.setTopLeftPosition(66 + 88, y + 4);
        y += 38;

        int x = 0, ly = y;
        for (auto* b : steps)
        {
            if (x + boxW > W && x > 0) { x = 0; ly += boxH + 6; }
            b->setBounds(x, ly, boxW, boxH);
            x += boxW + gap;
        }
        y = ly + boxH + 8;

        int bx = 0;
        for (auto* b : btns)
        {
            if (bx + b->getWidth() > W && bx > 0) { bx = 0; y += 30; }
            b->setTopLeftPosition(bx, y);
            bx += b->getWidth() + 6;
        }
        return y + 24 + 8;
    }

    void resized() override { layoutHeight(getWidth()); }

    void paint(juce::Graphics& g) override
    {
        const float cx = 8.0f, cy = 13.0f;
        juce::Path tri;
        if (expanded) tri.addTriangle(cx - 5, cy - 3, cx + 5, cy - 3, cx, cy + 4);
        else          tri.addTriangle(cx - 3, cy - 5, cx - 3, cy + 5, cx + 4, cy);
        g.setColour(juce::Colour(0xff666666));
        g.fillPath(tri);
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(15.0f, juce::Font::bold));
        g.drawText("Part " + juce::String(index + 1), 20, 0, getWidth() - 20, 26, juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.y < 26) { expanded = !expanded; resized(); repaint(); if (onHeightChanged) onHeightChanged(); }
    }

private:
    PatchStore& store;
    const int index;
    bool expanded = true;
    int stepCount = -1, activeStep = -2;
    ElementBox noteBox;
    juce::ComboBox denom, mod;
    juce::OwnedArray<ElementBox> steps;
    juce::OwnedArray<juce::TextButton> btns;

    void editPart(const juce::String& key, const std::function<void(seq::PercPart&)>& fn)
    {
        store.modify(key, [this, fn](seq::Patch& p) {
            if (index < (int) p.perc.parts.size()) fn(p.perc.parts[(size_t) index]);
        });
    }

    void addButton(const juce::String& text, int w, std::function<void()> cb)
    {
        auto* b = btns.add(new juce::TextButton(text));
        b->setSize(w, 24);
        b->onClick = std::move(cb);
        addAndMakeVisible(b);
    }

    int nearestBox(juce::Point<int> p) const
    {
        int best = 0; float bestD = 1.0e9f;
        for (int i = 0; i < steps.size(); ++i)
        {
            const float d = steps[i]->getBounds().getCentre().toFloat().getDistanceFrom(p.toFloat());
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void showDropTarget(int idx) { for (int i = 0; i < steps.size(); ++i) steps[i]->setDropTarget(i == idx); }

    void rebuildSteps(int n)
    {
        steps.clear();
        stepCount = n;
        activeStep = -2;
        for (int i = 0; i < n; ++i)
        {
            auto* b = steps.add(new ElementBox());
            b->kind = ElementBox::Kind::Flag; b->minV = 0; b->maxV = 1; b->flagText = "X";
            b->onValue = [this, i](int v) {
                editPart("part" + juce::String(index) + "_" + juce::String(i), [i, v](seq::PercPart& p) {
                    if (i < (int) p.steps.size()) p.steps[(size_t) i] = (uint8_t) v;
                });
            };
            b->onGestureEnd = [this] { store.endGesture(); };
            b->onDeleteRequest = [this, i] {
                editPart("", [i](seq::PercPart& p) { if (p.steps.size() > 1 && i < (int) p.steps.size()) p.steps.erase(p.steps.begin() + i); });
            };
            b->onReorderDrag = [this](const juce::MouseEvent& e) { showDropTarget(nearestBox(e.getEventRelativeTo(this).getPosition())); };
            b->onReorderEnd = [this, i](const juce::MouseEvent& e) {
                const int to = nearestBox(e.getEventRelativeTo(this).getPosition());
                showDropTarget(-1);
                if (to == i) return;
                editPart("", [i, to](seq::PercPart& p) {
                    if (i >= (int) p.steps.size() || to >= (int) p.steps.size()) return;
                    const uint8_t v = p.steps[(size_t) i];
                    p.steps.erase(p.steps.begin() + i);
                    p.steps.insert(p.steps.begin() + to, v);
                });
            };
            addAndMakeVisible(b);
        }
        resized();
    }
};

// ------------------------------------------------------------------------------------------ page
class PercussionPage : public juce::Component, private juce::ChangeListener, private juce::Timer
{
public:
    PercussionPage(PatchStore& s, seq::Engine& e) : store(s), engine(e)
    {
        addAndMakeVisible(chanLabel); chanLabel.setText("Channel", juce::dontSendNotification);
        for (int i = 1; i <= 16; ++i) channel.addItem(juce::String(i), i);
        channel.onChange = [this] {
            const int v = channel.getSelectedId();
            store.modify("", [v](seq::Patch& p) { p.perc.channel = v; });
        };
        addAndMakeVisible(channel);

        addAndMakeVisible(tempoLabel); tempoLabel.setText("Tempo", juce::dontSendNotification);
        tempo.addItem("normal", 1); tempo.addItem("half-time", 2); tempo.addItem("double-time", 3);
        tempo.onChange = [this] {
            const auto t = (seq::TempoMode) (tempo.getSelectedId() - 1);
            store.modify("", [t](seq::Patch& p) { p.perc.tempo = t; });
        };
        addAndMakeVisible(tempo);

        addAndMakeVisible(gateLabel); gateLabel.setText("Gate", juce::dontSendNotification);
        const char* names[7] = { "1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        for (int i = 0; i < 7; ++i) gateDenom.addItem(names[i], i + 1);
        gateDenom.onChange = [this] {
            const int d = 1 << (gateDenom.getSelectedId() - 1);
            store.modify("", [d](seq::Patch& p) { p.perc.gate.denom = d; });
        };
        gateMod.addItem("<none>", 1); gateMod.addItem("triplet", 2); gateMod.addItem("dotted", 3);
        gateMod.onChange = [this] {
            const auto m = (seq::Mod) (gateMod.getSelectedId() - 1);
            store.modify("", [m](seq::Patch& p) { p.perc.gate.mod = m; });
        };
        addAndMakeVisible(gateDenom);
        addAndMakeVisible(gateMod);

        addAndMakeVisible(velLabel); velLabel.setText("Velocity", juce::dontSendNotification);
        velocity.setSliderStyle(juce::Slider::LinearHorizontal);
        velocity.setTextBoxStyle(juce::Slider::TextBoxRight, false, 40, 22);
        velocity.setRange(1, 127, 1);
        velocity.onValueChange = [this] {
            const int v = (int) velocity.getValue();
            store.modify("percvel", [v](seq::Patch& p) { p.perc.velocity = v; });
        };
        velocity.onDragEnd = [this] { store.endGesture(); };
        addAndMakeVisible(velocity);

        for (auto* b : { &muteBtn, &soloBtn })
        {
            b->setClickingTogglesState(true);
            b->setColour(juce::TextButton::buttonOnColourId, b == &muteBtn ? juce::Colour(0xffe5484d) : juce::Colour(0xfff5a623));
            addAndMakeVisible(*b);
        }
        muteBtn.onClick = [this] { const bool v = muteBtn.getToggleState(); store.modify("", [v](seq::Patch& p) { p.perc.mute = v; }); };
        soloBtn.onClick = [this] { const bool v = soloBtn.getToggleState(); store.modify("", [v](seq::Patch& p) { p.perc.solo = v; }); };
        addAndMakeVisible(cycleLabel);
        cycleLabel.setJustificationType(juce::Justification::centredRight);

        addAndMakeVisible(viewport);
        viewport.setViewedComponent(&content, false);
        viewport.setScrollBarsShown(true, false);

        content.addAndMakeVisible(addPartBtn);
        content.addAndMakeVisible(removePartBtn);
        addPartBtn.onClick = [this] {
            store.modify("", [](seq::Patch& p) {
                if ((int) p.perc.parts.size() < seq::kMaxParts) p.perc.parts.push_back(seq::PercPart{});
            });
        };
        removePartBtn.onClick = [this] {
            store.modify("", [](seq::Patch& p) { if (p.perc.parts.size() > 1) p.perc.parts.pop_back(); });
        };

        store.addChangeListener(this);
        refreshAll();
        startTimerHz(30);
    }

    ~PercussionPage() override
    {
        stopTimer();
        store.removeChangeListener(this);
        viewport.setViewedComponent(nullptr, false);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        {
            auto row = r.removeFromTop(34).reduced(8, 4);
            auto place = [&row](juce::Component& c, int w) { c.setBounds(row.removeFromLeft(w)); row.removeFromLeft(6); };
            place(chanLabel, 58); place(channel, 64);
            place(tempoLabel, 50); place(tempo, 110);
            place(gateLabel, 40); place(gateDenom, 80); place(gateMod, 100);
        }
        {
            auto row = r.removeFromTop(34).reduced(8, 4);
            auto place = [&row](juce::Component& c, int w) { c.setBounds(row.removeFromLeft(w)); row.removeFromLeft(6); };
            place(velLabel, 62); place(velocity, 190);
            place(muteBtn, 30); place(soloBtn, 30);
            cycleLabel.setBounds(row);
        }
        viewport.setBounds(r);
        layoutContent();
    }

private:
    PatchStore& store;
    seq::Engine& engine;

    juce::Label chanLabel, tempoLabel, gateLabel, velLabel, cycleLabel;
    juce::ComboBox channel, tempo, gateDenom, gateMod;
    juce::Slider velocity;
    juce::TextButton muteBtn { "M" }, soloBtn { "S" };
    juce::TextButton addPartBtn { "add part" }, removePartBtn { "remove part" };

    juce::Viewport viewport;
    juce::Component content;
    juce::OwnedArray<PartSection> sections;

    void layoutContent()
    {
        const int W = std::max(200, viewport.getMaximumVisibleWidth() - 4);
        int y = 4;
        for (auto* s : sections)
        {
            const int h = s->layoutHeight(W - 16);
            s->setBounds(8, y, W - 16, h);
            y += h + 8;
        }
        addPartBtn.setBounds(8, y, 80, 26);
        removePartBtn.setBounds(96, y, 96, 26);
        y += 36;
        content.setSize(W, y);
    }

    void refreshAll()
    {
        const auto& pp = store.get().perc;
        channel.setSelectedId(pp.channel, juce::dontSendNotification);
        tempo.setSelectedId((int) pp.tempo + 1, juce::dontSendNotification);
        int gi = 0; while ((1 << gi) < pp.gate.denom && gi < 6) ++gi;
        gateDenom.setSelectedId(gi + 1, juce::dontSendNotification);
        gateMod.setSelectedId((int) pp.gate.mod + 1, juce::dontSendNotification);
        velocity.setValue(pp.velocity, juce::dontSendNotification);
        muteBtn.setToggleState(pp.mute, juce::dontSendNotification);
        soloBtn.setToggleState(pp.solo, juce::dontSendNotification);

        const int n = (int) pp.parts.size();
        while (sections.size() < n)
        {
            auto* s = sections.add(new PartSection(store, sections.size()));
            s->onHeightChanged = [this] { layoutContent(); };
            content.addAndMakeVisible(s);
        }
        while (sections.size() > n) sections.removeLast();
        for (auto* s : sections) s->refresh();

        const double bars = (double) seq::percCycleTicks(pp) / (double) (4 * seq::PPQ);
        juce::String text;
        if (bars <= 0.0)          text = "cycle: -";
        else if (bars > 100000.0) text = "cycle: > 100000 bars";
        else if (bars < 1.0)      text = "cycle: " + juce::String(bars * 4.0, 2) + " beats";
        else                      text = "cycle: " + juce::String(bars, 2) + " bars";
        cycleLabel.setText(text, juce::dontSendNotification);
        layoutContent();
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override { refreshAll(); }

    void timerCallback() override
    {
        const bool running = engine.isRunning();
        for (int i = 0; i < sections.size(); ++i)
            sections[i]->setActiveStep(running ? engine.partViewIndex(i) : -1);
    }
};
