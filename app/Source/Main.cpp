#include "Common.h"
#include "PatchStore.h"
#include "MidiHost.h"
#include "LinePage.h"
#include "PercussionPage.h"
#include "PresetIO.h"
#include <BinaryData.h>

// ------------------------------------------------------------------------------------------ tab bar
class TabBar : public juce::Component
{
public:
    std::function<void(int)> onSelect;
    int selected = 0;

    void paint(juce::Graphics& g) override
    {
        static const char* names[4] = { "MELODY", "CHORDS", "BASS", "PERCUSSION" };
        const juce::Font font(26.0f, juce::Font::bold);
        int x = 10;
        for (int i = 0; i < 4; ++i)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText(font, names[i], 0.0f, 0.0f);
            const int w = (int) std::ceil(ga.getBoundingBox(0, -1, true).getWidth());
            hit[(size_t) i] = juce::Rectangle<int>(x, 0, w, getHeight());
            g.setFont(font);
            g.setColour(i == selected ? juce::Colour(0xff222222) : juce::Colour(0xff8a8a8a));
            g.drawText(names[i], x, 0, w + 2, getHeight(), juce::Justification::centredLeft);
            x += w + 14;
            if (i < 3) { g.setColour(juce::Colour(0xffb0b0b0)); g.fillRect(x - 8, getHeight() / 2 - 7, 1, 14); }
            x += 12;
        }
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 4; ++i)
            if (hit[(size_t) i].contains(e.getPosition())) { selected = i; repaint(); if (onSelect) onSelect(i); return; }
    }

private:
    std::array<juce::Rectangle<int>, 4> hit;
};

// ------------------------------------------------------------------------------------------ main component
class MainComponent : public juce::Component, private juce::ChangeListener
{
public:
    MainComponent()
    {
        setWantsKeyboardFocus(true);

        addAndMakeVisible(tabs);
        tabs.onSelect = [this](int) { showPage(); };

        // MIDI out
        addAndMakeVisible(midiLabel); midiLabel.setText("MIDI Out", juce::dontSendNotification);
        addAndMakeVisible(midiBox);
        addAndMakeVisible(refreshBtn);
        refreshBtn.onClick = [this] { midi.refreshDevices(); fillMidiBox(false); };
        midiBox.onChange = [this] {
            const int id = midiBox.getSelectedId();
            if (id > 0) midi.open(id == 1 ? -1 : id - 2);
        };
        fillMidiBox(true);

        // presets bar
        for (auto* b : { &newBtn, &saveBtn, &loadBtn, &undoBtn, &redoBtn }) addAndMakeVisible(*b);
        newBtn.onClick  = [this] { newPreset(); };
        saveBtn.onClick = [this] { savePreset(); };
        loadBtn.onClick = [this] { loadPreset(); };
        undoBtn.onClick = [this] { store.undo(); };
        redoBtn.onClick = [this] { store.redo(); };

        addAndMakeVisible(modeLabel); modeLabel.setText("Mode", juce::dontSendNotification);
        const char* modes[8] = { "M    H    B", "M <- H    B", "M    H -> B", "M <- H -> B",
                                 "M -> H    B", "M -> H -> B", "M    H <- B", "M <- H <- B" };
        for (int i = 0; i < 8; ++i) modeBox.addItem(modes[i], i + 1);
        modeBox.onChange = [this] {
            const int m = modeBox.getSelectedId() - 1;
            store.modify("", [m](seq::Patch& p) { p.mode = m; });
        };
        addAndMakeVisible(modeBox);

        // transport
        for (auto* b : { &playBtn, &pauseBtn, &stopBtn, &panicBtn }) addAndMakeVisible(*b);
        playBtn.onClick  = [this] { engine.command(seq::Cmd::Start); };
        pauseBtn.onClick = [this] { engine.command(seq::Cmd::Pause); };
        stopBtn.onClick  = [this] { engine.command(seq::Cmd::Stop); };
        panicBtn.onClick = [this] { engine.command(seq::Cmd::Panic); };

        addAndMakeVisible(bpmLabel); bpmLabel.setText("BPM", juce::dontSendNotification);
        bpmSlider.setSliderStyle(juce::Slider::IncDecButtons);
        bpmSlider.setIncDecButtonsMode(juce::Slider::incDecButtonsNotDraggable);
        bpmSlider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 50, 24);
        bpmSlider.setRange(20.0, 300.0, 1.0);
        bpmSlider.setValue(120.0, juce::dontSendNotification);
        bpmSlider.onValueChange = [this] { bpm.store(bpmSlider.getValue()); store.touch(); };
        addAndMakeVisible(bpmSlider);

        addAndMakeVisible(editsLabel); editsLabel.setText("Edits apply", juce::dontSendNotification);
        editsBox.addItem("immediately", 1); editsBox.addItem("next beat", 2); editsBox.addItem("next bar", 3);
        editsBox.onChange = [this] {
            const int64_t q[3] = { 0, seq::PPQ, 4 * seq::PPQ };
            const int64_t v = q[juce::jlimit(0, 2, editsBox.getSelectedId() - 1)];
            store.modify("", [v](seq::Patch& p) { p.quantizeTicks = v; });
        };
        addAndMakeVisible(editsBox);

        addAndMakeVisible(gateLabel); gateLabel.setText("Gate", juce::dontSendNotification);
        gateSlider.setSliderStyle(juce::Slider::LinearHorizontal);
        gateSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 24);
        gateSlider.setRange(1.0, 100.0, 1.0);
        gateSlider.setTextValueSuffix("%");
        gateSlider.onValueChange = [this] {
            const double g = gateSlider.getValue() / 100.0;
            store.modify("gate", [g](seq::Patch& p) { p.gate = g; });
        };
        gateSlider.onDragEnd = [this] { store.endGesture(); };
        addAndMakeVisible(gateSlider);

        addAndMakeVisible(status);
        status.setColour(juce::Label::textColourId, juce::Colours::red);
        status.setText(midi.audioError, juce::dontSendNotification);

        // pages
        addAndMakeVisible(melody); addAndMakeVisible(harmony); addAndMakeVisible(bass);
        addAndMakeVisible(percussion);

        store.addChangeListener(this);
        syncFromStore();
        showPage();
        setSize(980, 900);
    }

    ~MainComponent() override { store.removeChangeListener(this); }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xfffafafa));
        g.setColour(juce::Colour(0xffd8d8d8));
        for (int y : { 48, 156 }) g.fillRect(0, y, getWidth(), 1);   // header separators

        if (!brandBounds.isEmpty())
        {
            auto b = brandBounds;
            const auto icon = b.removeFromLeft(b.getHeight()).reduced(2);
            if (brandImage.isValid())
                g.drawImage(brandImage, icon.toFloat(), juce::RectanglePlacement::centred);
            g.setColour(juce::Colour(0xff12344d));
            g.setFont(juce::Font(juce::FontOptions(26.0f, juce::Font::bold)));
            g.drawText("CArp", b.withTrimmedLeft(6), juce::Justification::centredLeft);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto header = r.removeFromTop(48);
        brandBounds = header.removeFromRight(130).reduced(6, 5);
        tabs.setBounds(header);

        auto row = [&r](int h) { auto a = r.removeFromTop(h).reduced(8, 3); return a; };
        {   auto a = row(32);   // MIDI out + mode
            midiLabel.setBounds(a.removeFromLeft(64));
            midiBox.setBounds(a.removeFromLeft(260)); a.removeFromLeft(8);
            refreshBtn.setBounds(a.removeFromLeft(70));
            auto right = a.removeFromRight(230);
            modeLabel.setBounds(right.removeFromLeft(44)); modeBox.setBounds(right);
        }
        {   auto a = row(32);   // presets + undo/redo
            for (auto* b : { &newBtn, &saveBtn, &loadBtn }) { b->setBounds(a.removeFromLeft(62)); a.removeFromLeft(8); }
            a.removeFromLeft(16);
            undoBtn.setBounds(a.removeFromLeft(62)); a.removeFromLeft(8);
            redoBtn.setBounds(a.removeFromLeft(62));
            status.setBounds(a);
        }
        {   auto a = row(34);   // transport
            for (auto* b : { &playBtn, &pauseBtn, &stopBtn }) { b->setBounds(a.removeFromLeft(58)); a.removeFromLeft(8); }
            a.removeFromLeft(10);
            bpmLabel.setBounds(a.removeFromLeft(36));
            bpmSlider.setBounds(a.removeFromLeft(96)); a.removeFromLeft(14);
            panicBtn.setBounds(a.removeFromLeft(62)); a.removeFromLeft(14);
            editsLabel.setBounds(a.removeFromLeft(74)); editsBox.setBounds(a.removeFromLeft(110));
            auto right = a.removeFromRight(300);
            gateLabel.setBounds(right.removeFromLeft(40)); gateSlider.setBounds(right);
        }
        r.removeFromTop(4);
        for (auto* c : std::initializer_list<juce::Component*>{ &melody, &harmony, &bass, &percussion }) c->setBounds(r);
    }

    bool keyPressed(const juce::KeyPress& k) override
    {
        const auto m = k.getModifiers();
        if (m.isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
        {
            if (m.isShiftDown()) store.redo(); else store.undo();
            return true;
        }
        if (m.isCommandDown() && (k.getKeyCode() == 'S' || k.getKeyCode() == 's')) { savePreset(); return true; }
        if (m.isCommandDown() && (k.getKeyCode() == 'O' || k.getKeyCode() == 'o')) { loadPreset(); return true; }
        if (k.isKeyCode(juce::KeyPress::spaceKey))
        {
            engine.command(engine.isRunning() ? seq::Cmd::Stop : seq::Cmd::Start);
            return true;
        }
        return false;
    }

    void mouseDown(const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // Quit (Cmd-Q or the close button): offer to save unsaved changes first.
    void requestQuit() { confirmDiscard([] { juce::JUCEApplication::getInstance()->quit(); }); }

private:
    // Declaration order matters: the MIDI host (audio callback) must be destroyed before the engine it drives.
    seq::Engine engine;
    PatchStore store { engine };
    std::atomic<double> bpm { 120.0 };
    MidiHost midi { engine, bpm };

    TabBar tabs;
    juce::Label midiLabel, modeLabel, bpmLabel, editsLabel, gateLabel, status;
    juce::ComboBox midiBox, modeBox, editsBox;
    juce::TextButton refreshBtn { "Refresh" }, newBtn { "New..." }, saveBtn { "Save..." }, loadBtn { "Load..." },
                     undoBtn { "Undo" }, redoBtn { "Redo" },
                     playBtn { "Play" }, pauseBtn { "Pause" }, stopBtn { "Stop" }, panicBtn { "Panic" };
    juce::Slider bpmSlider, gateSlider;
    LinePage melody { store, engine, 0 }, harmony { store, engine, 1 }, bass { store, engine, 2 };
    PercussionPage percussion { store, engine };

    juce::Image brandImage { juce::ImageCache::getFromMemory(BinaryData::carp_header_png, BinaryData::carp_header_pngSize) };
    juce::Rectangle<int> brandBounds;

    std::unique_ptr<juce::FileChooser> chooser;     // must outlive the async dialog
    juce::File lastDir { juce::File::getSpecialLocation(juce::File::userDocumentsDirectory) };

    static void alert(const juce::String& title, const juce::String& message)
    {
        juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon, title, message);
    }

    // Asks whether to save unsaved changes, then runs `proceed` (unless the user cancels).
    void confirmDiscard(std::function<void()> proceed)
    {
        if (!store.isDirty()) { proceed(); return; }
        juce::Component::SafePointer<MainComponent> self(this);
        const auto options = juce::MessageBoxOptions::makeOptionsYesNoCancel(
            juce::MessageBoxIconType::QuestionIcon, "Save the current preset?",
            "You have unsaved changes. Would you like to save them first?", "Save", "Don't Save", "Cancel", this);
        juce::AlertWindow::showAsync(options, [self, proceed](int result) {
            if (self == nullptr) return;
            if (result == 1) self->savePreset(proceed);      // Save, then continue once the file is written
            else if (result == 2) proceed();                 // Don't Save
            // anything else: Cancel
        });
    }

    void newPreset()
    {
        confirmDiscard([this] {
            store.modify("", [](seq::Patch& p) { p = seq::makeDefaultPatch(); });   // undoable
            bpmSlider.setValue(120.0);
            store.markClean();
        });
    }

    void savePreset(std::function<void()> onSaved = {})
    {
        chooser = std::make_unique<juce::FileChooser>("Save preset", lastDir.getChildFile("preset.json"), "*.json");
        juce::Component::SafePointer<MainComponent> self(this);
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
                                 | juce::FileBrowserComponent::warnAboutOverwriting,
                             [self, onSaved](const juce::FileChooser& fc) {
                                 if (self == nullptr) return;
                                 auto f = fc.getResult();
                                 if (f == juce::File()) return;                       // cancelled
                                 if (!f.hasFileExtension("json")) f = f.withFileExtension("json");
                                 self->lastDir = f.getParentDirectory();
                                 const auto text = juce::JSON::toString(PresetIO::toVar(self->store.get(), self->bpm.load()), false);
                                 if (!f.replaceWithText(text, false, false, "\n"))
                                 {
                                     alert("Couldn't save preset", "Could not write to " + f.getFullPathName());
                                     return;
                                 }
                                 self->store.markClean();
                                 if (onSaved) onSaved();
                             });
    }

    void loadPreset()
    {
        confirmDiscard([this] { chooseAndLoad(); });
    }

    void chooseAndLoad()
    {
        chooser = std::make_unique<juce::FileChooser>("Load preset", lastDir, "*.json");
        juce::Component::SafePointer<MainComponent> self(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                             [self](const juce::FileChooser& fc) {
                                 if (self == nullptr) return;
                                 const auto f = fc.getResult();
                                 if (f == juce::File()) return;                       // cancelled
                                 self->lastDir = f.getParentDirectory();

                                 juce::var root;
                                 const auto parsed = juce::JSON::parse(f.loadFileAsString(), root);
                                 if (parsed.failed()) { alert("Couldn't load preset", "That file isn't valid JSON: " + parsed.getErrorMessage()); return; }

                                 seq::Patch loaded;
                                 double newBpm = 120.0;
                                 juce::String err;
                                 if (!PresetIO::fromVar(root, loaded, newBpm, err)) { alert("Couldn't load preset", err); return; }

                                 self->store.modify("", [loaded](seq::Patch& p) { p = loaded; });   // undoable
                                 self->bpmSlider.setValue(newBpm);
                                 self->store.markClean();
                             });
    }

    void showPage()
    {
        const int p = tabs.selected;
        melody.setVisible(p == 0);
        harmony.setVisible(p == 1);
        bass.setVisible(p == 2);
        percussion.setVisible(p == 3);
    }

    void fillMidiBox(bool selectDefault)
    {
        const auto previous = midiBox.getText();
        midiBox.clear(juce::dontSendNotification);
        midiBox.addItem("Virtual port \"CArp\"", 1);
        const auto& devs = midi.getDevices();
        int pick = 1;
        for (int i = 0; i < devs.size(); ++i)
        {
            midiBox.addItem(devs[i].name, i + 2);
            if (selectDefault && devs[i].name.containsIgnoreCase("IAC")) pick = i + 2;
            if (!selectDefault && devs[i].name == previous) pick = i + 2;
        }
        midiBox.setSelectedId(pick, selectDefault ? juce::sendNotification : juce::dontSendNotification);
    }

    void syncFromStore()
    {
        const auto& p = store.get();
        modeBox.setSelectedId(p.mode + 1, juce::dontSendNotification);
        gateSlider.setValue(p.gate * 100.0, juce::dontSendNotification);
        editsBox.setSelectedId(p.quantizeTicks == 0 ? 1 : p.quantizeTicks == seq::PPQ ? 2 : 3, juce::dontSendNotification);
        undoBtn.repaint(); redoBtn.repaint();
    }

    void changeListenerCallback(juce::ChangeBroadcaster*) override { syncFromStore(); }
};

// ------------------------------------------------------------------------------------------ application
class CArpApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "CArp"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise(const juce::String&) override
    {
        // JUCE's default scheme is dark (light text), which vanished against our light background.
        if (auto* lf = dynamic_cast<juce::LookAndFeel_V4*>(&juce::LookAndFeel::getDefaultLookAndFeel()))
            lf->setColourScheme(juce::LookAndFeel_V4::getLightColourScheme());
        window = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { window.reset(); }
    void systemRequestedQuit() override
    {
        if (window != nullptr)
            if (auto* main = dynamic_cast<MainComponent*>(window->getContentComponent())) { main->requestQuit(); return; }
        quit();
    }

private:
    class MainWindow : public juce::DocumentWindow
    {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(name, juce::Colour(0xfffafafa), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent(), true);
            setResizable(true, true);
            setResizeLimits(760, 520, 4000, 4000);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION(CArpApplication)
