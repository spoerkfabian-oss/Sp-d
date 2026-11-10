# Sp-d

Simcade racing prototype in Unity, built around feeling extremely fast and a bit dangerous.

## Run it
1. Install Unity Hub and a Unity 6 LTS editor.
2. In the Hub choose Add > Add project from disk, pick this folder and select the Unity 6 editor.
   If the Hub rejects the folder, create a new empty "3D (Built-In Render Pipeline)" project instead and copy `Assets/Scripts` into it.
3. Open any scene (the empty default one is fine) and press Play. The track, car, camera and HUD are created from code.

Controls: W/S throttle and brake (S reverses at standstill), A/D steer, Space handbrake, R reset to track.

If the console reports that `UnityEngine.Input` is disabled, set Project Settings > Player > Active Input Handling to "Both".

## Code
| File | Purpose |
|---|---|
| `Assets/Scripts/GameBootstrap.cs` | Builds the scene at Play |
| `Assets/Scripts/CarController.cs` | Raycast suspension, friction-circle tyres, downforce |
| `Assets/Scripts/ChaseCamera.cs` | Speed feel: FOV, camera drop, shake, streaks |
| `Assets/Scripts/Track.cs` | Circuit mesh from control points |
| `Assets/Scripts/LapTimer.cs` | Lap timing, reset, HUD |

`Source/MyGame` holds the earlier Unreal speed-FX component, kept for reference only.
