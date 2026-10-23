# Seq (JUCE standalone), milestone 1: Melody + Bass pages

Build (macOS, needs CMake 3.22+ and Xcode; the first configure downloads JUCE 8.0.4):

    cd app
    cmake -B build -G Xcode
    open build/SeqApp.xcodeproj      # or: cmake --build build --config Release

The engine lives in ../seq (no JUCE dependency) and is tested separately:

    cd seq && g++ -std=c++17 -O2 Engine.cpp EngineTests.cpp -o tests && ./tests

Gestures: drag a box up/down to change it, double-click to type, Cmd-drag to reposition,
Option-click to delete, drag the dot at the end of a row to reorder rows.
Keys: Space = play/stop, Cmd-Z / Cmd-Shift-Z = undo/redo, Cmd-S = save preset, Cmd-O = load preset.
Presets are .json files holding every page plus the BPM (not mute/solo or the MIDI output).
