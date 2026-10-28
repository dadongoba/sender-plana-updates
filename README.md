# sender Plan A updates

Incremental changes for an existing Unreal Engine 5.8 project. This repository is an update bundle, not a standalone game project.

Original game assets, Blueprint binaries, third-party plugin source files, engine files, build outputs, caches, logs, saves, and local machine configuration are excluded.

## What's new in this revision

### Full runtime module source

`Source/` now carries the complete `sender` module (36 files) instead of only the module skeleton: backpack, cable car, camera spawn, fragment, ending, jump tuner, mesh merge, pose bake, sense gate, traffic, weapon stripper and zipline subsystems, the Slate ending overlay, and ending reveal tests.

`sender/sender.Build.cs` gained the corresponding dependencies:

- `InputCore`, `UMG`, `Slate`, `SlateCore` (Slate ending overlay)
- `GameplayAbilities`, `GameplayTags`, `GameplayTasks` (traffic collision must zero the player's GAS Health attribute)
- editor-only: `MeshMergeUtilities`, `MeshDescription`, `StaticMeshDescription`, `AssetRegistry`, `UnrealEd` (mesh merge helper)

The project must have the GameplayAbilities plugin enabled for the module to build.

### Animation-driven talking interaction

`SenderEndingSubsystem` no longer drives NPC talking through a fixed 3.2s timer that cycled through variants. It now assigns animation per dialogue page:

| Condition | Animation |
| --- | --- |
| `PageIndex == 3` (Taro's confrontational line) | `Taro_Talk_Question` |
| Other pages where Taro speaks | `Taro_Talk_Doubt` |
| Pages where the player speaks (Taro listens) | `Taro_Listen_Loop` (looping) |
| Outside the dialogue state | `Taro_Idle_Breathe` (looping) |

Rebuilt API surface:

- removed `PlayTalkingVariant`, `TalkTimer`, `TalkVariant`, `TalkingIdle`, `TalkAnimations`
- added `EnsureTalkingAssets()`, `PlayIdleAnimation()`, and the `TalkQuestion` / `TalkDoubt` / `ListenLoop` properties
- `StartEnding` and `RestorePresentation` now call `PlayIdleAnimation()` so the listen loop cannot leak into a non-dialogue state

Animations are played as single-node playback on the skeletal mesh component, not through an AnimBP slot. The AnimBP on this character has no slot node, so montage routing is not available; blending is baked offline instead (see `scripts/`).

## Animation assets

`*.uasset` and `*.umap` are force-added against `.gitignore` for the assets this update needs, so the bundle stays rebuildable instead of code-only. The code paths above reference these, and they are present in the repository:

| Asset path | Length | Purpose |
| --- | --- | --- |
| `/Game/Characters/MetahumanTaro/Anims/Taro_Idle_Breathe` | 4.00 s | Breathing idle; also the mesh component's default fallback |
| `/Game/Characters/MetahumanTaro/Anims/Talk/Taro_Talk_Question` | 3.60 s | Page 4 confrontational line |
| `/Game/Characters/MetahumanTaro/Anims/Talk/Taro_Talk_Doubt` | 4.60 s | Page 11 hesitant line |
| `/Game/Characters/MetahumanTaro/Anims/Talk/Taro_Listen_Loop` | 8.00 s | Listening loop during the player's monologue pages |
| `/Game/Characters/MetahumanTaro/Mocap/Retarget/RTG_TaroTalk` | — | IK Retargeter used to retarget the mocap clips onto the Taro skeleton |

`Taro_Talk_Question` also exists as a 3.20 s leftover under `Anims/` (not `Anims/Talk/`); that one is an early placeholder and is not referenced by code.

The retarget sources live under `Mocap/`: `Convo_10_Big_Idea`, `Convo_11_Listening` and `Convo_12_Gesturing_Up_C_Center_Loop`, plus `SK_PackMannequin` and the `IK_TaroBody` / `IK_PackMannequin` rigs that `RTG_TaroTalk` maps between.

`Content/Pedestrian_Convo_01/` is the third-party conversation mocap pack those clips were imported from, committed as its own revision so it can be reverted independently. No license to it is granted by this repository.

### Component default slot

The ending character's mesh component (`TaroMesh-Combined` on `ALS_Taro_CharacterBP_C_2`) had its `animation_data.anim_to_play` pointing at `Taro_ArmsCrossed`, a 2-frame A-pose reference that was never a real animation. It now points at `Taro_Idle_Breathe` with `saved_looping` enabled. This change is baked into `senderv02.umap` in this bundle.

Note that this slot is inert at runtime: the component's `animation_mode` is `AnimationBlueprint`, driven by `SK_MetahumanTaro_Combined_AnimBP`, so the slot only matters when something switches the component to single-node playback. See the known issue below.

## Regenerating the animations

`scripts/` contains the offline baking scripts used with Unreal's Python plugin:

- `build_idle_breathe.py` — builds `Taro_Idle_Breathe` from a base pose (9 bones plus a chest scale term). Note: its `BASE_SEQ` constant still points at the removed `Taro_ArmsCrossed`; retarget it at the skeleton's reference pose or any clean idle before running.
- `blend_v2.py` — bakes a mocap clip's opening into a talking animation: extracts the head of the source clip, then cross-fades the first and last 0.45–0.6 s to the idle pose with a smoothstep curve, so a hard `PlayAnimation` call does not pop.

Both scripts run through Unreal's remote Python execution against a live editor. They write via `AnimationDataController`; after `create_asset`, the asset must be flushed with `EditorAssetLibrary.save_loaded_asset`, otherwise it exists only in memory and disappears on editor restart.

## Known issue

Starting the interaction still produces a visible hitch on the first frame — the arms snap upward before the animation settles.

The baked clips begin at the idle pose (0.00° deviation measured at the first frame, 1.5–1.7° at the tail), so the pop is not authored into the curves. The likely cause is the playback path itself: the mesh component runs in `AnimationBlueprint` mode, and `PlayAnimation` forces a switch to single-node mode. That switch re-initialises the pose, so for one frame the character leaves the AnimBP's idle and lands on the first frame of the baked clip, while the AnimBP is still evaluating.

The clean fix is to add a slot node to `SK_MetahumanTaro_Combined_AnimBP` and drive the clips through a montage, which blends instead of switching modes. That requires editing the AnimBP asset and is not done yet.

## Previous changes

- `Source/`: runtime module registration and a Blueprintable `SenderActor` with a BlueprintNativeEvent `Interact`.
- `patches/enable-cpp-module.patch`: adds only the runtime module registration to the existing project descriptor.
- `patches/cable-raytracing-build-timing.patch`: schedules cable ray tracing builds during render-thread vertex updates, avoids duplicate pending builds, and references evicted geometry without submitting an instance prematurely. The plugin patch contains only changed lines and minimal context. No license for the original project or third-party plugin is granted by this repository.

## Apply to the existing project

Back up the project and close Unreal Editor first. Copy `Source/` into the project root. From that root, apply the two patches with `git apply <path-to-patch>` (works outside a Git checkout). If the module registration or cable fix is already present, do not reapply it. Earlier Blueprint-only generated rules under `Intermediate/Source` may conflict with the new targets; back up and move that generated directory outside `Intermediate` before regenerating project files.

Generate project files using the installed engine's `Engine/Build/BatchFiles/Build.bat -projectfiles -project=<absolute-project-path> -game -rocket`. Build with `Build.bat senderEditor Win64 Development -Project=<absolute-project-path> -WaitMutex -NoHotReloadFromIDE`. Open the project after a successful build.

To create a mixed C++ and Blueprint actor, create a Blueprint subclass of `SenderActor` and override `Interact`. Existing Blueprint inheritance and the map GameMode are unchanged by this bundle.

## Validation performed

- UE 5.8.3, Windows, Development Editor / Win64: compilation and project-file generation succeeded.
- After rebuilding the cable plugin and restarting the editor, PIE ran for over 30 seconds with no pending-build ray tracing ensure in the current log. This covers only the loaded scene.
- Runtime inspection confirmed that using Default Player Start spawns the character above the floor instead of an obstructed origin position.
- Talking interaction confirmed working in PIE: the per-page animation assignment plays the intended clip, and the baked clips were measured to start at 0.00° deviation from the idle pose and end within 1.7°.

## Camera diagnosis: pending manual change

The player controller sets its camera manager as the view target. `BlueprintUpdateCamera` only enters the ALS camera calculation when its target has the `ALS_Character` actor tag, but the camera manager's default actor tags were empty. Suggested manual adjustment: add `ALS_Character` under Class Defaults > Actor > Tags in `ALS_PlayerCameraManager`, compile/save, and run PIE again. This adjustment was not performed or verified; no modified camera Blueprint is included. Keep the editor play spawn option on Default Player Start.
