#!/usr/bin/env python3
"""Capability probe for the parametric `camera` concern (SPEC-camera-config).

Enumerates the connected OAK's capabilities and writes them where the
files-first manifest's dynamic enums + `visible_when` can read them:

  $SNAP_DATA/caps/<field>/<value>.opt   -- one file per valid enum option
  $SNAP_DATA/caps/capabilities.json     -- full detected record (diagnostics)
  <config>/camera/local.env             -- HAS_STEREO/HAS_IR/HAS_IMU + CAMERA_MODEL
                                           (so visible_when can gate depth/ir/imu)

Capability source (SPEC decision): try the depthai python API to auto-detect
the live device(s); on any failure (no device, module missing) fall back to a
shipped STATIC MATRIX keyed by the operator-declared model. Either way the UI
only ever offers values the selected model supports.

Run by the `camera` files-first hook before it composes the pipeline, and at
install/post-refresh, so the enums are populated before the operator opens
Manage. Stdout is logged by the hook.
"""
import json
import os
import sys

# --- Static capability matrix (fallback + offline editing) ----------------
# Keyed by model. rgb_res/depth_res use depthai i_resolution tokens; fps lists
# the rates the on-chip encoder + sensor sustain at that class. Conservative
# supersets — the hook validates the final combo, and a live probe (below)
# overrides these with the device's true modes when depthai is importable.
MATRIX = {
    "OAK-1": dict(stereo=False, ir=False, imu=False,
                  rgb_res=["720P", "1080P", "4K"], rgb_fps=["30", "60"],
                  depth_res=[], depth_fps=[]),
    "OAK-1-LITE": dict(stereo=False, ir=False, imu=False,
                       rgb_res=["720P", "1080P", "4K"], rgb_fps=["30"],
                       depth_res=[], depth_fps=[]),
    "OAK-D": dict(stereo=True, ir=False, imu=True,
                  rgb_res=["720P", "1080P", "4K"], rgb_fps=["30", "60"],
                  depth_res=["400P", "720P", "800P"], depth_fps=["30", "60"]),
    "OAK-D-LITE": dict(stereo=True, ir=False, imu=True,
                       rgb_res=["720P", "1080P", "4K"], rgb_fps=["30"],
                       depth_res=["400P", "480P"], depth_fps=["30", "60"]),
    "OAK-D-PRO": dict(stereo=True, ir=True, imu=True,
                      rgb_res=["720P", "1080P", "4K"], rgb_fps=["30", "60"],
                      depth_res=["400P", "720P", "800P"], depth_fps=["30", "60"]),
    "OAK-D-PRO-W": dict(stereo=True, ir=True, imu=True,
                        rgb_res=["720P", "1080P", "4K"], rgb_fps=["30", "60"],
                        depth_res=["400P", "720P", "800P"], depth_fps=["30", "60"]),
}

# depthai device-name substring -> our model key (live-probe path).
DEVICE_NAME_MAP = [
    ("OAK-D-PRO-W", "OAK-D-PRO-W"), ("OAK-D-PRO", "OAK-D-PRO"),
    ("OAK-D-LITE", "OAK-D-LITE"), ("OAK-D", "OAK-D"),
    ("OAK-1-LITE", "OAK-1-LITE"), ("OAK-1", "OAK-1"),
]


def declared_model():
    """Operator-declared model: env (hook) or snapctl, default OAK-D-PRO."""
    m = os.environ.get("HUSARION_AGENT_CAMERA_MODEL", "").strip()
    if m:
        return m
    try:
        import subprocess
        v = subprocess.run(["snapctl", "get", "driver.model"],
                           capture_output=True, text=True).stdout.strip()
        if v:
            return v
    except Exception:
        pass
    return "OAK-D-PRO"


def probe_live():
    """Return (model, mxids, caps) from the live device, or None on any failure."""
    try:
        import depthai as dai  # noqa
    except Exception as e:
        print(f"camera-probe: depthai module unavailable ({e}); using static matrix")
        return None
    try:
        infos = dai.Device.getAllAvailableDevices()
        if not infos:
            print("camera-probe: no OAK device found; using static matrix")
            return None
        mxids = [getattr(i, "mxid", getattr(i, "deviceId", "")) for i in infos]
        ip_target = os.environ.get("HUSARION_AGENT_IP", "").strip()
        info = infos[0]
        # Target a specific mxid/ip if the operator selected one.
        sel = os.environ.get("HUSARION_AGENT_MXID", "").strip()
        for i in infos:
            if sel and getattr(i, "mxid", "") == sel:
                info = i
        with dai.Device(dai.OpenVINO.Version.VERSION_UNIVERSAL, info) as dev:
            name = (dev.getDeviceName() or "").upper()
            model = next((k for sub, k in DEVICE_NAME_MAP if sub in name), declared_model())
            sensors = [str(s) for s in dev.getCameraSensorNames().values()]
            stereo = len(sensors) >= 3  # color + 2 mono
            try:
                ir = bool(dev.getIrDrivers())
            except Exception:
                ir = MATRIX.get(model, {}).get("ir", False)
            base = MATRIX.get(model, MATRIX["OAK-D-PRO"])
            caps = dict(base, stereo=stereo, ir=ir,
                        imu=base.get("imu", True))
        print(f"camera-probe: live device {model} mxids={mxids} stereo={stereo} ir={ir}")
        return (model, mxids, caps)
    except Exception as e:
        print(f"camera-probe: live probe failed ({e}); using static matrix")
        return None


def write_opts(caps_dir, field, values):
    d = os.path.join(caps_dir, field)
    os.makedirs(d, exist_ok=True)
    have = set(os.listdir(d))
    want = {f"{v}.opt" for v in values}
    for f in have - want:
        try:
            os.remove(os.path.join(d, f))
        except OSError:
            pass
    for v in values:
        open(os.path.join(d, f"{v}.opt"), "w").close()


def main():
    snap_data = os.environ.get("SNAP_DATA", "/tmp")
    caps_dir = os.path.join(snap_data, "caps")
    os.makedirs(caps_dir, exist_ok=True)

    live = probe_live()
    if live:
        model, mxids, caps = live
    else:
        model = declared_model()
        caps = MATRIX.get(model, MATRIX["OAK-D-PRO"])
        mxids = []

    # Dynamic-enum option files (the manifest points its `dynamic.path` here).
    write_opts(caps_dir, "model", sorted(MATRIX.keys()))
    write_opts(caps_dir, "mxid", mxids)  # empty when offline → "any device"
    write_opts(caps_dir, "rgb_resolution", caps["rgb_res"])
    write_opts(caps_dir, "rgb_fps", caps["rgb_fps"])
    write_opts(caps_dir, "depth_resolution", caps["depth_res"])
    write_opts(caps_dir, "depth_fps", caps["depth_fps"])

    record = dict(model=model, mxids=mxids, **caps)
    with open(os.path.join(caps_dir, "capabilities.json"), "w") as f:
        json.dump(record, f, indent=2)

    # Capability flags → the concern's local.env so `visible_when` can gate the
    # depth / ir / imu cards. local scope = per-device, never synced. Merge with
    # any existing keys we don't own.
    cfg_dir = os.environ.get("HUSARION_AGENT_CONFIG_DIR", "")
    if cfg_dir:
        env_path = os.path.join(cfg_dir, "camera", "local.env")
        os.makedirs(os.path.dirname(env_path), exist_ok=True)
        owned = {"HAS_STEREO", "HAS_IR", "HAS_IMU", "DETECTED_MODEL"}
        keep = []
        if os.path.exists(env_path):
            with open(env_path) as f:
                keep = [ln for ln in f if ln.split("=", 1)[0].strip() not in owned]
        with open(env_path, "w") as f:
            f.writelines(keep)
            f.write(f"HAS_STEREO={'true' if caps['stereo'] else 'false'}\n")
            f.write(f"HAS_IR={'true' if caps['ir'] else 'false'}\n")
            f.write(f"HAS_IMU={'true' if caps['imu'] else 'false'}\n")
            f.write(f"DETECTED_MODEL={model}\n")

    print(f"camera-probe: wrote caps for {model} -> {caps_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
