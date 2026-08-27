# ADR-0003: miniaudio

## Status

Accepted

## Decision

Use miniaudio as the initial audio backend.

miniaudio must be hidden behind Engine/Audio.

## Responsibilities

miniaudio handles:

- audio device access
- playback
- streaming
- mixing
- basic spatial audio

DeepRun acoustic simulation is a separate system under Simulation/Acoustics.

## Consequences

Gameplay code must not call miniaudio directly.

A future Xbox-specific backend may replace or supplement miniaudio without changing gameplay code.