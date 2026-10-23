#pragma once
#include "PatchStore.h"

// ------------------------------------------------------------------------------------------ ElementBox
// One cell of a row. Drag up/down to change, double-click to type, Cmd-drag to reposition, Option-click to delete.
class ElementBox : public juce::Component
{
public:
    enum class Kind { Int, Note, Flag };

    Kind kind = Kind::Int;
    int minV = 0, maxV = 999;
    juce::String flagText = "R";
    bool active = false;       // playhead
    bool dropTarget = false;   // while Cmd-dragging another element over this one

    std::function<void(int)> onValue;
    std::function<void()> onGestureEnd, onClicked, onDeleteRequest;
    std::function<void(const juce::MouseEvent&)> onReorderDrag, onReorderEnd;

    int value() const { return val; }
    void setValue(int v) { if (v != val) { val = v; repaint(); } }
    void setActive(bool a) { if (a != active) { active = a; repaint(); } }
    void setDropTarget(bool d) { if (d != dropTarget) { dropTarget = d; repaint(); } }

    static juce::String noteName(int midi)
    {
        static const char* names[12] = { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
        midi = juce::jlimit(0, 127, midi);
        return juce::String(names[midi % 12]) + juce::String(midi / 12 - 1);   // C4 = 60
    }

    static bool parseNote(juce::String s, int& result)
    {
        s = s.trim();
        if (s.isEmpty()) return false;
        if (s.containsOnly("-0123456789")) { result = juce::jlimit(0, 127, s.getIntValue()); return true; }
        static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
        const auto c = juce::CharacterFunctions::toUpperCase(s[0]);
        if (c < 'A' || c > 'G') return false;
        int semis = base[c - 'A'];
        int i = 1;
        while (i < s.length() && (s[i] == '#' || s[i] == 'b')) { semis += (s[i] == '#') ? 1 : -1; ++i; }
        const juce::String oct = s.substring(i);
        if (oct.isEmpty() || !oct.containsOnly("-0123456789")) return false;
        result = juce::jlimit(0, 127, (oct.getIntValue() + 1) * 12 + semis);
        return true;
    }

    juce::String text() const
    {
        switch (kind)
        {
            case Kind::Int:  return juce::String(val);
            case Kind::Note: return noteName(val);
            case Kind::Flag: return val != 0 ? flagText : juce::String();
        }
        return {};
    }

    void paint(juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(active ? juce::Colour(0xffffd27a) : juce::Colour(0xfff4f4f4));
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(dropTarget ? juce::Colour(0xff3b82f6) : juce::Colour(0xffb5b5b5));
        g.drawRoundedRectangle(r, 6.0f, dropTarget ? 2.0f : 1.0f);
        g.setColour(juce::Colours::black);
        g.setFont(15.0f);
        g.drawText(text(), getLocalBounds(), juce::Justification::centred);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (editor != nullptr) return;
        if (onClicked) onClicked();
        mode = Mode::None;
        if (e.mods.isAltDown())          mode = Mode::Delete;
        else if (e.mods.isCommandDown()) mode = Mode::Reorder;
        else if (kind == Kind::Flag)     mode = Mode::Toggle;
        else { mode = Mode::Drag; dragStart = val; }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        if (mode == Mode::Drag)
            set(juce::jlimit(minV, maxV, dragStart + (int) std::lround(-e.getDistanceFromDragStartY() / 6.0)));
        else if (mode == Mode::Reorder && onReorderDrag)
            onReorderDrag(e);
    }

    void mouseUp(const juce::MouseEvent& e) override
    {
        const Mode m = mode;
        mode = Mode::None;
        if (m == Mode::Toggle && !e.mouseWasDraggedSinceMouseDown()) set(val != 0 ? 0 : 1);
        if (m == Mode::Reorder && onReorderEnd) onReorderEnd(e);
        if (m != Mode::None && onGestureEnd) onGestureEnd();
        if (m == Mode::Delete && onDeleteRequest) onDeleteRequest();   // last: the row is rebuilt asynchronously
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override
    {
        if (kind != Kind::Flag && !e.mods.isCommandDown() && !e.mods.isAltDown()) beginEdit();
    }

private:
    enum class Mode { None, Drag, Toggle, Reorder, Delete };
    Mode mode = Mode::None;
    int val = 0, dragStart = 0;
    std::unique_ptr<juce::TextEditor> editor;

    void set(int v)
    {
        if (v == val) return;
        val = v;
        repaint();
        if (onValue) onValue(v);
    }

    void beginEdit()
    {
        editor = std::make_unique<juce::TextEditor>();
        addAndMakeVisible(*editor);
        editor->setBounds(getLocalBounds().reduced(3));
        editor->setJustification(juce::Justification::centred);
        editor->setText(text(), false);
        editor->selectAll();
        editor->grabKeyboardFocus();
        editor->onReturnKey = [this] { finishEdit(true); };
        editor->onEscapeKey = [this] { finishEdit(false); };
        editor->onFocusLost = [this] { finishEdit(true); };
    }

    void finishEdit(bool accept)
    {
        if (editor == nullptr) return;
        const juce::String txt = editor->getText();
        editor->onReturnKey = nullptr; editor->onEscapeKey = nullptr; editor->onFocusLost = nullptr;
        removeChildComponent(editor.get());
        std::shared_ptr<juce::TextEditor> dying(std::move(editor));   // destroyed after this callback has returned
        juce::MessageManager::callAsync([dying] {});

        if (!accept) return;
        int v = 0;
        bool ok = false;
        if (kind == Kind::Note) ok = parseNote(txt, v);
        else if (txt.trim().isNotEmpty() && txt.trim().containsOnly("-0123456789")) { v = txt.trim().getIntValue(); ok = true; }
        if (ok) { set(juce::jlimit(minV, maxV, v)); if (onGestureEnd) onGestureEnd(); }
    }
};

// ------------------------------------------------------------------------------------------ RowHandle
class RowHandle : public juce::Component
{
public:
    bool current = false;
    std::function<void()> onPress;
    std::function<void(const juce::MouseEvent&)> onDrop;

    RowHandle() { setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    void paint(juce::Graphics& g) override
    {
        g.setColour(current ? juce::Colour(0xff3b82f6) : juce::Colour(0xffaaaaaa));
        g.fillEllipse(getLocalBounds().toFloat().reduced(3.0f));
    }
    void mouseDown(const juce::MouseEvent&) override { if (onPress) onPress(); }
    void mouseUp(const juce::MouseEvent& e) override { if (e.mouseWasDraggedSinceMouseDown() && onDrop) onDrop(e); }
};

// ------------------------------------------------------------------------------------------ ModuleSection
// A collapsible module: header, optional control strip, N rows of elements (each with its button row),
// and a footer with row-level buttons.
class ModuleSection : public juce::Component
{
public:
    using Getter = std::function<seq::Module<int>&(seq::Patch&)>;

    struct Spec
    {
        ElementBox::Kind kind = ElementBox::Kind::Int;
        int minV = 1, maxV = 999, defaultValue = 1;
        juce::String flagText = "R";
        bool rowOps = true;                                // add row / remove row / copy
        std::function<int(const seq::Patch&)> autoCount;   // set: shows an "auto" button (Order)
        bool bold = true;
        bool activeIsRow = false;                          // playhead index is a row (chords), not a single element
    };

    std::function<void()> onHeightChanged;

    ModuleSection(PatchStore& s, juce::String t, Getter g, Spec sp)
        : store(s), title(std::move(t)), getMod(std::move(g)), spec(std::move(sp))
    {
        if (spec.rowOps)
        {
            addFooter("add row", 80, [this] {
                edit("", [this](seq::Module<int>& m) {
                    const int at = juce::jlimit(0, (int) m.rows.size(), currentRow + 1);
                    m.rows.insert(m.rows.begin() + at, std::vector<int>{ spec.defaultValue });
                    currentRow = at;
                });
            });
            addFooter("remove row", 90, [this] {
                edit("", [this](seq::Module<int>& m) {
                    if (m.rows.size() > 1 && currentRow < (int) m.rows.size()) m.rows.erase(m.rows.begin() + currentRow);
                });
            });
            addFooter("copy", 60, [this] {
                edit("", [this](seq::Module<int>& m) {
                    if (currentRow < (int) m.rows.size()) { auto copy = m.rows[(size_t) currentRow]; m.rows.insert(m.rows.begin() + currentRow + 1, copy); ++currentRow; }
                });
            });
        }
        if (spec.autoCount)
            addFooter("auto", 60, [this] {
                store.modify("", [this](seq::Patch& p) {
                    const int n = std::max(1, spec.autoCount(p));
                    std::vector<int> row;
                    for (int i = 1; i <= n; ++i) row.push_back(i);
                    getMod(p).rows.push_back(row);
                });
            });
        rebuild();
    }

    // ---- control strip (units, combos, axis box) ----
    void addUnitPickers(std::function<seq::Unit&(seq::Patch&)> getUnit)
    {
        addLabel("t-unit:", 50);
        auto* denom = new juce::ComboBox();
        const char* names[7] = { "1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64" };
        for (int i = 0; i < 7; ++i) denom->addItem(names[i], i + 1);
        denom->setSize(80, 24);
        denom->onChange = [this, denom, getUnit] {
            const int d = 1 << (denom->getSelectedId() - 1);
            store.modify("", [getUnit, d](seq::Patch& p) { getUnit(p).denom = d; });
        };
        auto* mod = new juce::ComboBox();
        mod->addItem("<none>", 1); mod->addItem("triplet", 2); mod->addItem("dotted", 3);
        mod->setSize(100, 24);
        mod->onChange = [this, mod, getUnit] {
            const auto m = (seq::Mod) (mod->getSelectedId() - 1);
            store.modify("", [getUnit, m](seq::Patch& p) { getUnit(p).mod = m; });
        };
        addExtra(denom); addExtra(mod);
        refreshers.push_back([this, denom, mod, getUnit] {
            const seq::Unit& u = getUnit(store.peek());
            int idx = 0; while ((1 << idx) < u.denom && idx < 6) ++idx;
            denom->setSelectedId(idx + 1, juce::dontSendNotification);
            mod->setSelectedId((int) u.mod + 1, juce::dontSendNotification);
        });
    }

    void addCombo(const juce::String& label, int labelW, const juce::StringArray& items, int width,
                  std::function<int(const seq::Patch&)> get, std::function<void(seq::Patch&, int)> set)
    {
        addLabel(label, labelW);
        auto* cb = new juce::ComboBox();
        for (int i = 0; i < items.size(); ++i) cb->addItem(items[i], i + 1);
        cb->setSize(width, 24);
        cb->onChange = [this, cb, set] {
            const int idx = cb->getSelectedId() - 1;
            store.modify("", [set, idx](seq::Patch& p) { set(p, idx); });
        };
        addExtra(cb);
        refreshers.push_back([this, cb, get] { cb->setSelectedId(get(store.get()) + 1, juce::dontSendNotification); });
    }

    void addNoteBox(const juce::String& label, int labelW, std::function<int(const seq::Patch&)> get,
                    std::function<void(seq::Patch&, int)> set)
    {
        addLabel(label, labelW);
        auto* box = new ElementBox();
        box->kind = ElementBox::Kind::Note; box->minV = 0; box->maxV = 127;
        box->setSize(56, 26);
        box->onValue = [this, set](int v) { store.modify("axis", [set, v](seq::Patch& p) { set(p, v); }); };
        box->onGestureEnd = [this] { store.endGesture(); };
        addExtra(box);
        refreshers.push_back([box, this, get] { box->setValue(get(store.get())); });
    }

    // ---- state ----
    void refresh()
    {
        const auto& m = getMod(store.peek());
        std::vector<int> sig;
        for (auto& r : m.rows) sig.push_back((int) r.size());
        if (sig != signature) { rebuild(); if (onHeightChanged) onHeightChanged(); }
        else updateValues();
        for (auto& f : refreshers) f();
    }

    void setActive(int flat)
    {
        if (flat == activeFlat) return;
        activeFlat = flat;
        int base = 0;
        for (int r = 0; r < rowsUI.size(); ++r)
        {
            auto* ru = rowsUI[r];
            for (int c = 0; c < ru->boxes.size(); ++c)
                ru->boxes[c]->setActive(flat >= 0 && (spec.activeIsRow ? r == flat : base + c == flat));
            base += ru->boxes.size();
        }
    }

    // Positions all children for width W and returns the height this section needs.
    int layoutHeight(int W)
    {
        const int headerH = 26, boxW = 52, boxH = 30, gap = 8, handle = 16;
        for (auto* e : extras) e->setVisible(expanded);
        for (auto* ru : rowsUI) setRowVisible(*ru, expanded);
        for (auto* f : footer) f->setVisible(expanded);
        if (!expanded) return headerH + 2;

        int y = headerH;
        if (!extras.isEmpty())
        {
            int x = 0;
            for (auto* e : extras)
            {
                if (x + e->getWidth() > W && x > 0) { x = 0; y += 30; }
                e->setTopLeftPosition(x, y + (30 - e->getHeight()) / 2);
                x += e->getWidth() + 6;
            }
            y += 32;
        }
        for (auto* ru : rowsUI)
        {
            ru->top = y;
            int x = 0, ly = y;
            for (auto* b : ru->boxes)
            {
                if (x + boxW > W - handle - 4 && x > 0) { x = 0; ly += boxH + 8; }
                b->setBounds(x, ly, boxW, boxH);
                x += boxW + gap;
            }
            if (ru->handle != nullptr) ru->handle->setBounds(x, ly + (boxH - handle) / 2, handle, handle);
            y = ly + boxH + 8;
            int bx = 0;
            for (auto* bt : ru->btns) { bt->setTopLeftPosition(bx, y); bx += bt->getWidth() + 6; }
            y += 24 + 8;
            ru->bottom = y;
        }
        int fx = 0;
        for (auto* f : footer) { f->setTopLeftPosition(fx, y); fx += f->getWidth() + 6; }
        if (!footer.isEmpty()) y += 24 + 6;
        return y + 4;
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
        g.setFont(juce::Font(15.0f, spec.bold ? juce::Font::bold : juce::Font::plain));
        g.drawText(title, 20, 0, getWidth() - 20, 26, juce::Justification::centredLeft);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        if (e.y < 26) { expanded = !expanded; resized(); repaint(); if (onHeightChanged) onHeightChanged(); }
    }

private:
    struct RowUI
    {
        juce::OwnedArray<ElementBox> boxes;
        juce::OwnedArray<juce::TextButton> btns;
        std::unique_ptr<RowHandle> handle;
        int top = 0, bottom = 0;
    };

    PatchStore& store;
    juce::String title;
    Getter getMod;
    Spec spec;
    bool expanded = true;
    int currentRow = 0, activeFlat = -1;
    std::vector<int> signature;
    juce::OwnedArray<RowUI> rowsUI;
    juce::OwnedArray<juce::Component> extras;
    juce::OwnedArray<juce::TextButton> footer;
    std::vector<std::function<void()>> refreshers;

    void edit(const juce::String& key, const std::function<void(seq::Module<int>&)>& fn)
    {
        store.modify(key, [this, fn](seq::Patch& p) { fn(getMod(p)); });
    }

    void addExtra(juce::Component* c) { extras.add(c); addAndMakeVisible(c); }
    void addLabel(const juce::String& text, int w)
    {
        auto* l = new juce::Label();
        l->setText(text, juce::dontSendNotification);
        l->setSize(w, 24);
        addExtra(l);
    }
    void addFooter(const juce::String& text, int w, std::function<void()> cb)
    {
        auto* b = footer.add(new juce::TextButton(text));
        b->setSize(w, 24);
        b->onClick = std::move(cb);
        addAndMakeVisible(b);
    }
    void setRowVisible(RowUI& ru, bool v)
    {
        for (auto* b : ru.boxes) b->setVisible(v);
        for (auto* b : ru.btns) b->setVisible(v);
        if (ru.handle != nullptr) ru.handle->setVisible(v);
    }

    int nearestBox(int r, juce::Point<int> p) const
    {
        int best = 0; float bestD = 1.0e9f;
        auto& boxes = rowsUI[r]->boxes;
        for (int i = 0; i < boxes.size(); ++i)
        {
            const float d = boxes[i]->getBounds().getCentre().toFloat().getDistanceFrom(p.toFloat());
            if (d < bestD) { bestD = d; best = i; }
        }
        return best;
    }

    void showDropTarget(int r, int idx)
    {
        auto& boxes = rowsUI[r]->boxes;
        for (int i = 0; i < boxes.size(); ++i) boxes[i]->setDropTarget(i == idx);
    }

    int rowAtY(int y) const
    {
        for (int r = 0; r < rowsUI.size(); ++r) if (y < rowsUI[r]->bottom) return r;
        return rowsUI.size() - 1;
    }

    void addRowButton(RowUI& ru, const juce::String& text, int w, std::function<void()> cb)
    {
        auto* b = ru.btns.add(new juce::TextButton(text));
        b->setSize(w, 24);
        b->onClick = std::move(cb);
        addAndMakeVisible(b);
    }

    void rebuild()
    {
        rowsUI.clear();
        const auto& m = getMod(store.peek());
        signature.clear();
        for (auto& r : m.rows) signature.push_back((int) r.size());
        currentRow = juce::jlimit(0, std::max(0, (int) m.rows.size() - 1), currentRow);

        for (int r = 0; r < (int) m.rows.size(); ++r)
        {
            auto* ru = rowsUI.add(new RowUI());
            const juce::String keyBase = title + "_r" + juce::String(r) + "_c";

            for (int c = 0; c < (int) m.rows[(size_t) r].size(); ++c)
            {
                auto* b = ru->boxes.add(new ElementBox());
                b->kind = spec.kind; b->minV = spec.minV; b->maxV = spec.maxV; b->flagText = spec.flagText;
                b->onClicked = [this, r] { setCurrentRow(r); };
                b->onValue = [this, r, c, keyBase](int v) {
                    store.modify(keyBase + juce::String(c), [this, r, c, v](seq::Patch& p) {
                        auto& mm = getMod(p);
                        if (r < (int) mm.rows.size() && c < (int) mm.rows[(size_t) r].size()) mm.rows[(size_t) r][(size_t) c] = v;
                    });
                };
                b->onGestureEnd = [this] { store.endGesture(); };
                b->onDeleteRequest = [this, r, c] {
                    edit("", [r, c](seq::Module<int>& mm) {
                        if (r < (int) mm.rows.size() && mm.rows[(size_t) r].size() > 1 && c < (int) mm.rows[(size_t) r].size())
                            mm.rows[(size_t) r].erase(mm.rows[(size_t) r].begin() + c);
                    });
                };
                b->onReorderDrag = [this, r](const juce::MouseEvent& e) { showDropTarget(r, nearestBox(r, e.getEventRelativeTo(this).getPosition())); };
                b->onReorderEnd = [this, r, c](const juce::MouseEvent& e) {
                    const int to = nearestBox(r, e.getEventRelativeTo(this).getPosition());
                    showDropTarget(r, -1);
                    if (to == c) return;
                    edit("", [r, c, to](seq::Module<int>& mm) {
                        if (r >= (int) mm.rows.size()) return;
                        auto& row = mm.rows[(size_t) r];
                        if (c >= (int) row.size() || to >= (int) row.size()) return;
                        const int v = row[(size_t) c];
                        row.erase(row.begin() + c);
                        row.insert(row.begin() + to, v);
                    });
                };
                addAndMakeVisible(b);
            }

            auto rowEdit = [this, r](std::function<void(std::vector<int>&)> fn) {
                setCurrentRow(r);
                edit("", [r, fn](seq::Module<int>& mm) { if (r < (int) mm.rows.size()) fn(mm.rows[(size_t) r]); });
            };
            const int def = spec.defaultValue;
            const bool lastCopy = spec.kind == ElementBox::Kind::Note;
            addRowButton(*ru, "-", 30, [rowEdit] { rowEdit([](std::vector<int>& v) { if (v.size() > 1) v.pop_back(); }); });
            addRowButton(*ru, "+", 30, [rowEdit, def, lastCopy] { rowEdit([def, lastCopy](std::vector<int>& v) { v.push_back(lastCopy && !v.empty() ? v.back() : def); }); });
            addRowButton(*ru, "rev", 40, [rowEdit] { rowEdit([](std::vector<int>& v) { std::reverse(v.begin(), v.end()); }); });
            addRowButton(*ru, "<<", 36, [rowEdit] { rowEdit([](std::vector<int>& v) { if (!v.empty()) std::rotate(v.begin(), v.begin() + 1, v.end()); }); });
            addRowButton(*ru, ">>", 36, [rowEdit] { rowEdit([](std::vector<int>& v) { if (!v.empty()) std::rotate(v.begin(), v.end() - 1, v.end()); }); });

            if (spec.rowOps)
            {
                ru->handle = std::make_unique<RowHandle>();
                ru->handle->onPress = [this, r] { setCurrentRow(r); };
                ru->handle->onDrop = [this, r](const juce::MouseEvent& e) {
                    const int to = rowAtY(e.getEventRelativeTo(this).getPosition().y);
                    if (to == r) return;
                    edit("", [r, to](seq::Module<int>& mm) {
                        if (r >= (int) mm.rows.size() || to >= (int) mm.rows.size()) return;
                        auto row = mm.rows[(size_t) r];
                        mm.rows.erase(mm.rows.begin() + r);
                        mm.rows.insert(mm.rows.begin() + to, row);
                    });
                    currentRow = to;
                };
                addAndMakeVisible(*ru->handle);
            }
        }
        updateValues();
        activeFlat = -2;   // force the next setActive() to repaint
        resized();
    }

    void setCurrentRow(int r)
    {
        currentRow = r;
        for (int i = 0; i < rowsUI.size(); ++i)
            if (rowsUI[i]->handle != nullptr) { rowsUI[i]->handle->current = (i == r); rowsUI[i]->handle->repaint(); }
    }

    void updateValues()
    {
        const auto& m = getMod(store.peek());
        for (int r = 0; r < rowsUI.size() && r < (int) m.rows.size(); ++r)
            for (int c = 0; c < rowsUI[r]->boxes.size() && c < (int) m.rows[(size_t) r].size(); ++c)
                rowsUI[r]->boxes[c]->setValue(m.rows[(size_t) r][(size_t) c]);
        setCurrentRow(currentRow);
    }
};
