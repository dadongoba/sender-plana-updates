import unreal, math, sys

IDLE = "/Game/Characters/MetahumanTaro/Anims/Taro_Idle_Breathe"
OUT_DIR = "/Game/Characters/MetahumanTaro/Anims/Talk"
FPS = 30
eal = unreal.EditorAssetLibrary
opts = unreal.AnimPoseEvaluationOptions()
E = unreal.AnimPoseExtensions
idle_seq = unreal.load_asset(IDLE)
IDLE_LEN = idle_seq.get_play_length()
skel = idle_seq.get_editor_property("skeleton")


def smooth(x):
    x = max(0.0, min(1.0, x))
    return x * x * (3.0 - 2.0 * x)


def nlerp(a, b, t):
    x = a.x + (b.x - a.x) * t
    y = a.y + (b.y - a.y) * t
    z = a.z + (b.z - a.z) * t
    w = a.w + (b.w - a.w) * t
    n = math.sqrt(x * x + y * y + z * z + w * w)
    if n < 1e-8:
        return b
    return unreal.Quat(x=x / n, y=y / n, z=z / n, w=w / n)


def ensure_asset(name):
    path = OUT_DIR + "/" + name
    a = unreal.load_asset(path)
    if a is None:
        f = unreal.AnimSequenceFactory()
        f.set_editor_property("target_skeleton", skel)
        a = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, OUT_DIR, unreal.AnimSequence, f)
    return a


def build(src_path, out_name, start_t, length, fade_in, fade_out):
    src = unreal.load_asset(src_path)
    if src is None:
        print("MISSING_SRC", src_path)
        return
    frames = int(round(length * FPS))
    keys = frames + 1

    talk_poses = [src.get_anim_pose_at_time(start_t + min(i / float(FPS), length - 1e-4), opts) for i in range(keys)]
    idle_poses = [idle_seq.get_anim_pose_at_time((i / float(FPS)) % IDLE_LEN, opts) for i in range(keys)]
    bones = [str(n) for n in E.get_bone_names(talk_poses[0])]

    weights = []
    for i in range(keys):
        t = i / float(FPS)
        w = 1.0
        if fade_in > 0 and t < fade_in:
            w = smooth(t / fade_in)
        if fade_out > 0 and t > length - fade_out:
            w = min(w, smooth((length - t) / fade_out))
        weights.append(w)
    blend_frames = sum(1 for w in weights if w < 0.999)

    dst = ensure_asset(out_name)
    ctrl = dst.controller
    ctrl.remove_all_bone_tracks()
    ctrl.set_frame_rate(unreal.FrameRate(numerator=FPS, denominator=1))
    ctrl.set_number_of_frames(unreal.FrameNumber(value=frames))

    written = 0
    for bone in bones:
        try:
            E.get_bone_pose(talk_poses[0], bone)
        except Exception:
            continue
        poss, rots, scals = [], [], []
        for i in range(keys):
            bt = E.get_bone_pose(talk_poses[i], bone)
            w = weights[i]
            if w >= 0.999:
                q, tr, sc = bt.rotation, bt.translation, bt.scale3d
            else:
                try:
                    bi = E.get_bone_pose(idle_poses[i], bone)
                    q = nlerp(bi.rotation, bt.rotation, w)
                    tr = unreal.Vector(
                        bi.translation.x + (bt.translation.x - bi.translation.x) * w,
                        bi.translation.y + (bt.translation.y - bi.translation.y) * w,
                        bi.translation.z + (bt.translation.z - bi.translation.z) * w)
                    sc = unreal.Vector(
                        bi.scale3d.x + (bt.scale3d.x - bi.scale3d.x) * w,
                        bi.scale3d.y + (bt.scale3d.y - bi.scale3d.y) * w,
                        bi.scale3d.z + (bt.scale3d.z - bi.scale3d.z) * w)
                except Exception:
                    q, tr, sc = bt.rotation, bt.translation, bt.scale3d
            poss.append(unreal.Vector(tr.x, tr.y, tr.z))
            rots.append(unreal.Quat(x=q.x, y=q.y, z=q.z, w=q.w))
            scals.append(unreal.Vector(sc.x, sc.y, sc.z))
        ctrl.add_bone_track(unreal.Name(bone))
        ctrl.set_bone_track_keys(unreal.Name(bone), poss, rots, scals)
        written += 1

    eal.save_loaded_asset(dst, only_if_is_dirty=False)
    print("BUILT %-22s bones=%d keys=%d blendframes=%d len=%.2f" %
          (out_name, written, keys, blend_frames, dst.get_play_length()))


JOBS = [
    ("/Game/_talkbuild/Convo_02_Up_Beat_Loop", "Taro_Talk_Question", 0.0, 3.6, 0.45, 0.55),
    ("/Game/_talkbuild/Convo_07_Sad_Loop", "Taro_Talk_Doubt", 0.0, 4.6, 0.45, 0.60),
    ("/Game/Characters/MetahumanTaro/Anims/Talk/Taro_TalkMocap_Listening", "Taro_Listen_Loop", 0.0, 8.0, 0.50, 0.50),
]

which = sys.argv[1] if len(sys.argv) > 1 else "all"
for src, name, st, ln, fi, fo in JOBS:
    if which in ("all", name):
        build(src, name, st, ln, fi, fo)
print("DONE")
