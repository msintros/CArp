#pragma once
#include "Common.h"

inline seq::Page& pageRef(seq::Patch& p, int role) { return role == 0 ? p.melody : role == 1 ? p.harmony : p.bass; }
inline const seq::Page& pageRef(const seq::Patch& p, int role) { return role == 0 ? p.melody : role == 1 ? p.harmony : p.bass; }

// The UI-side master copy of the patch. Every edit goes through modify(): it records undo history,
// flattens the rows and hands a snapshot to the engine (lock-free). Message thread only.
class PatchStore : public juce::ChangeBroadcaster
{
public:
    explicit PatchStore(seq::Engine& e) : engine(e)
    {
        current = seq::makeDefaultPatch();
        publish(false);
        markClean();
    }

    // "Unsaved changes" tracking: every published edit (including undo/redo) bumps the version.
    bool isDirty() const { return version != cleanVersion; }
    void markClean() { cleanVersion = version; }
    void touch() { ++version; }   // for state that lives outside the patch but is saved with it (BPM)

    const seq::Patch& get() const { return current; }
    seq::Patch& peek() { return current; }   // read access for widgets that need non-const module refs. Do not edit through this.

    // Edits sharing a non-empty key within 400 ms (a drag, for instance) collapse into one undo step.
    void modify(const juce::String& key, const std::function<void(seq::Patch&)>& fn)
    {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const bool merge = key.isNotEmpty() && key == lastKey && (now - lastTime) < 400.0;
        if (!merge)
        {
            undoStack.push_back(current);
            if (undoStack.size() > 200) undoStack.erase(undoStack.begin());
            redoStack.clear();
        }
        lastKey = key;
        lastTime = now;
        fn(current);
        publish();
    }

    void endGesture() { lastKey = juce::String(); }

    bool undo()
    {
        if (undoStack.empty()) return false;
        redoStack.push_back(current);
        current = undoStack.back();
        undoStack.pop_back();
        lastKey = juce::String();
        publish();
        return true;
    }

    bool redo()
    {
        if (redoStack.empty()) return false;
        undoStack.push_back(current);
        current = redoStack.back();
        redoStack.pop_back();
        lastKey = juce::String();
        publish();
        return true;
    }

private:
    void publish(bool notify = true)
    {
        ++version;
        current.finalize();
        engine.post(new seq::Patch(current));
        if (notify) sendChangeMessage();
    }

    seq::Engine& engine;
    seq::Patch current;
    std::vector<seq::Patch> undoStack, redoStack;
    juce::String lastKey;
    double lastTime = 0.0;
    unsigned long version = 0, cleanVersion = 0;
};
