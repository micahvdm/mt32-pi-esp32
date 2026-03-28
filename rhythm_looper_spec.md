# Rhythm Looper Specification (mt32-pi-esp32)

## Overview
This document defines the full design and implementation details for a **channel-10 quantized rhythm looper** integrated into the mt32-pi-esp32 project.

The looper is designed as a **Core 0 real-time MIDI subsystem** that captures, quantizes, loops, and optionally exports drum patterns.

---

## Core Features
- Record MIDI channel 10 (drums)
- Quantized recording and loop closure
- Real-time looping playback
- MIDI CC control (CC108 default)
- Export loop to Standard MIDI File (.mid)
- Web UI integration
- Future support for overdub, multiple loops, sync

---

## Architecture

### New Class
CRhythmLooper

### Files
- include/rhythmlooper.h
- src/rhythmlooper.cpp

### Ownership
Owned by CMT32Pi:
CRhythmLooper m_RhythmLooper;

### Threading
- Runs entirely on Core 0
- No audio thread interaction

---

## State Machine

Idle → Armed → Recording → Playing → Overdubbing → StoppedWithLoop

### Transitions
- Idle + press → Armed
- Armed + first note → Recording
- Recording + press → Playing
- Playing + press → Stop

---

## MIDI Control

Default:
[midi_cc_map]
cc108=looper_arm_stop

---

## Event Model

struct TLoopEvent
{
    u32 nTick;
    u32 nMessage;
};

---

## Timing

PPQN: 480  
Default BPM: 120  

Quantization:
tick = ((tick + step/2) / step) * step

---

## Recording

- Channel 10 only
- Note on/off only
- Start on first note
- Stop on CC108 press

---

## Playback

tick = (now - start) % loopLength

Routing:
if mixer:
    router
else:
    synth

---

## Integration Points

- OnShortMessage(): capture
- UpdateMIDI(): playback

---

## Config

[rhythm_looper]
enabled=1
channel=10
bpm=120
quantize=16
max_bars=8

---

## Web UI

- Enable toggle
- BPM
- Quantize
- Controls: Arm / Stop / Save

---

## Save to MIDI

- SMF Type 0
- PPQN 480
- Path: SD:loops

---

## Safety

- Prevent stuck notes
- Bounded memory
- Quantized events

---

## Future

- Overdub
- Multiple loops
- Sync
- Tap tempo

---

## Summary

A Core 0 MIDI looper for drums with quantized recording and playback, integrated into existing routing.
