# Sp-d: simcade racing game

Project memory for Claude sessions. Keep it short and current.

## Owner hardware
- Desktop: GTX 1070 Ti (8 GB), Intel i5 from 2012 (stated as "i5 3750", probably i5-3570: 4 cores, no AVX2). 24 GB DDR3-1600 CL9.
- Laptop: Lenovo V17 G2 ITL (82NX), 11th-gen Intel, integrated or MX-class GPU, 16 GB DDR4-2666 CL18.

## Decisions
- Engine: Unity 6 LTS, chosen by hardware fit. Unreal 5 is too heavy for the 2012 CPU.
- Built-in render pipeline and legacy Input Manager, no packages, so the repo opens without setup.
- Everything is built from code at runtime (`GameBootstrap`), because Claude cannot use the editor.
- `Source/MyGame` is the old Unreal SpeedFX component, kept for reference; its ideas are ported in `ChaseCamera.cs`.

## Game vision
Simcade, single focus: feeling extremely fast and a bit dangerous. Goal is a whole game, built in slices.

## Owner preferences
- Short answers, ask about ambiguities before starting, save tokens.
- Self-review work and flag what should be corrected before it is published.

## Status
- Slice 1 (car, track, chase camera, lap timer) written blind, never compiled or run in Unity.
