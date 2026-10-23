#pragma once
#include "Common.h"

// Runs the engine from the audio callback (the steadiest clock available to a desktop app) and forwards
// its events to a MIDI output with timestamps, so the OS-level sender thread releases them on schedule
// instead of whenever a UI timer happens to fire.
class MidiHost : public juce::AudioIODeviceCallback
{
public:
    MidiHost(seq::Engine& e, std::atomic<double>& bpmRef) : engine(e), bpm(bpmRef)
    {
        events.reserve(2048);
        audioError = dm.initialiseWithDefaultDevices(0, 2);
        refreshDevices();
        dm.addAudioCallback(this);
    }

    ~MidiHost() override { dm.removeAudioCallback(this); }

    juce::String audioError;
    const juce::Array<juce::MidiDeviceInfo>& getDevices() const { return devices; }
    void refreshDevices() { devices = juce::MidiOutput::getAvailableDevices(); }

    // index < 0: create a virtual port that other apps can use as a MIDI source
    bool open(int index)
    {
        std::unique_ptr<juce::MidiOutput> o;
        if (index < 0) o = juce::MidiOutput::createNewDevice("Seq");
        else if (juce::isPositiveAndBelow(index, devices.size())) o = juce::MidiOutput::openDevice(devices[index].identifier);
        if (o != nullptr) o->startBackgroundThread();
        {
            const juce::SpinLock::ScopedLockType l(outLock);
            std::swap(out, o);
        }
        return out != nullptr;   // the previous output (now in o) is destroyed outside the lock
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* d) override
    {
        sampleRate = d->getCurrentSampleRate();
        clockValid = false;
    }
    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outputChannelData, int numOutputChannels,
                                          int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int c = 0; c < numOutputChannels; ++c)
            if (outputChannelData[c] != nullptr) juce::FloatVectorOperations::clear(outputChannelData[c], numSamples);

        engine.process(numSamples, sampleRate, bpm.load(std::memory_order_relaxed), events);

        // Stable block timestamps: raw callback arrival times jitter by a fraction of a millisecond, which would
        // jitter every note. Instead advance an expected time by exactly one block per callback, nudge it very
        // slowly toward the real clock, and only resync on a gross discontinuity.
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double blockMs = 1000.0 * numSamples / sampleRate;
        if (!clockValid || std::abs(now - expected) > 30.0) { expected = now; clockValid = true; }
        else expected += 0.002 * (now - expected);
        const double startMs = expected + std::max(8.0, 2.0 * blockMs);
        expected += blockMs;

        if (events.empty()) return;
        buffer.clear();
        for (auto& e : events) buffer.addEvent(juce::MidiMessage((int) e.data[0], (int) e.data[1], (int) e.data[2]), e.sampleOffset);

        const juce::SpinLock::ScopedTryLockType l(outLock);
        if (l.isLocked() && out != nullptr) out->sendBlockOfMessages(buffer, startMs, sampleRate);
    }

private:
    seq::Engine& engine;
    std::atomic<double>& bpm;
    juce::AudioDeviceManager dm;
    juce::Array<juce::MidiDeviceInfo> devices;
    std::unique_ptr<juce::MidiOutput> out;
    juce::SpinLock outLock;
    std::vector<seq::MidiOut> events;
    juce::MidiBuffer buffer;
    double sampleRate = 44100.0, expected = 0.0;
    bool clockValid = false;
};
