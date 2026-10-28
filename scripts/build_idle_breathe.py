import unreal, math

BASE_SEQ = "/Game/Characters/MetahumanTaro/Anims/Taro_ArmsCrossed"
OUT_DIR   = "/Game/Characters/MetahumanTaro/Anims"
OUT_NAME  = "Taro_Idle_Breathe"
FPS       = 30
SECONDS   = 4.0
FRAMES    = int(SECONDS * FPS)
KEYS      = FRAMES + 1

eal = unreal.EditorAssetLibrary
seq_path = OUT_DIR + "/" + OUT_NAME
base = unreal.load_asset(BASE_SEQ)
skel = base.get_editor_property("skeleton")
asset = unreal.load_asset(seq_path)
if asset is None:
    factory = unreal.AnimSequenceFactory()
    factory.set_editor_property("target_skeleton", skel)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(OUT_NAME, OUT_DIR, unreal.AnimSequence, factory)

opts = unreal.AnimPoseEvaluationOptions()
E = unreal.AnimPoseExtensions
pose0 = base.get_anim_pose_at_time(0.0, opts)

def lq(b): 
    r = E.get_bone_pose(pose0, b).rotation
    return unreal.Quat(x=r.x, y=r.y, z=r.z, w=r.w)
def lt(b):
    t = E.get_bone_pose(pose0, b).translation
    return unreal.Vector(t.x, t.y, t.z)

ctrl = asset.controller
ctrl.remove_all_bone_tracks()
ctrl.set_frame_rate(unreal.FrameRate(numerator=FPS, denominator=1))
ctrl.set_number_of_frames(unreal.FrameNumber(value=FRAMES))

TRACKS = {
    "spine_01":  dict(pitch=1.30, phase=0.00, yaw=0.00, yc=1, sx=1.000),
    "spine_02":  dict(pitch=1.10, phase=-0.45, yaw=0.00, yc=1, sx=1.010),
    "spine_03":  dict(pitch=0.85, phase=-0.90, yaw=0.00, yc=1, sx=1.010),
    "neck_01":   dict(pitch=-0.70, phase=-1.20, yaw=0.00, yc=1, sx=1.000),
    "head":      dict(pitch=0.55, phase=-1.50, yaw=2.30, yc=2, sx=1.000),
    "clavicle_l":dict(pitch=1.40, phase=0.20, yaw=0.00, yc=1, sx=1.000),
    "clavicle_r":dict(pitch=1.40, phase=0.20, yaw=0.00, yc=1, sx=1.000),
    "upperarm_l":dict(pitch=0.90, phase=-0.60, yaw=0.00, yc=1, sx=1.000),
    "upperarm_r":dict(pitch=0.90, phase=-0.60, yaw=0.00, yc=1, sx=1.000),
}

for bone, c in TRACKS.items():
    ctrl.add_bone_track(unreal.Name(bone))
    q0 = lq(bone); t0 = lt(bone)
    poss, rots, scals = [], [], []
    for i in range(KEYS):
        u = i / float(FRAMES)
        p = 2.0 * math.pi * u
        s = math.sin(p + c["phase"])
        pitch = c["pitch"] * s
        yaw = c["yaw"] * math.sin(c["yc"] * p + c["phase"])
        q = unreal.MathLibrary.multiply_quat_quat(
                q0, unreal.MathLibrary.conv_rotator_to_quaternion(unreal.Rotator(0.0, pitch, yaw)))
        poss.append(t0); rots.append(q); scals.append(unreal.Vector(c["sx"], 1.0, 1.0))
    ctrl.set_bone_track_keys(unreal.Name(bone), poss, rots, scals)

eal.save_loaded_asset(asset, only_if_is_dirty=False)
print("BUILT len=%.3f keys=%d" % (asset.get_play_length(), KEYS))
