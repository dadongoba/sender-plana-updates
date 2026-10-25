# sender Plan A updates

Incremental changes for an existing Unreal Engine 5.8 project. This repository is an update bundle, not a standalone game project.

## Included changes

- `Source/`: new `sender` runtime module, Game and Editor targets, and a Blueprintable `SenderActor` with a BlueprintNativeEvent `Interact`.
- `patches/enable-cpp-module.patch`: adds only the runtime module registration to the existing project descriptor.
- `patches/cable-raytracing-build-timing.patch`: schedules cable ray tracing builds during render-thread vertex updates, avoids duplicate pending builds, and references evicted geometry without submitting an instance prematurely.

Original game assets, Blueprint binaries, third-party plugin source files, engine files, build outputs, caches, logs, saves, and local machine configuration are excluded. The plugin patch contains only changed lines and minimal context. No license for the original project or third-party plugin is granted by this repository.

## Apply to the existing project

Back up the project and close Unreal Editor first. Copy `Source/` into the project root. From that root, apply the two patches with `git apply <path-to-patch>` (works outside a Git checkout). If the module registration or cable fix is already present, do not reapply it. Earlier Blueprint-only generated rules under `Intermediate/Source` may conflict with the new targets; back up and move that generated directory outside `Intermediate` before regenerating project files.

Generate project files using the installed engine's `Engine/Build/BatchFiles/Build.bat -projectfiles -project=<absolute-project-path> -game -rocket`. Build with `Build.bat senderEditor Win64 Development -Project=<absolute-project-path> -WaitMutex -NoHotReloadFromIDE`. Open the project after a successful build.

To create a mixed C++ and Blueprint actor, create a Blueprint subclass of `SenderActor` and override `Interact`. Existing Blueprint inheritance and the map GameMode are unchanged by this bundle.

## Validation performed

- UE 5.8.3, Windows, Development Editor / Win64: compilation and project-file generation succeeded.
- After rebuilding the cable plugin and restarting the editor, PIE ran for over 30 seconds with no pending-build ray tracing ensure in the current log. This covers only the loaded scene.
- Runtime inspection confirmed that using Default Player Start spawns the character above the floor instead of an obstructed origin position.

## Camera diagnosis: pending manual change

The player controller sets its camera manager as the view target. `BlueprintUpdateCamera` only enters the ALS camera calculation when its target has the `ALS_Character` actor tag, but the camera manager's default actor tags were empty. Suggested manual adjustment: add `ALS_Character` under Class Defaults > Actor > Tags in `ALS_PlayerCameraManager`, compile/save, and run PIE again. This adjustment was not performed or verified; no modified camera Blueprint is included. Keep the editor play spawn option on Default Player Start.
