# SPEC — depthai parametric, capability-aware camera config

Outstanding-work playbook for replacing the flat `camera-params-*.yaml` preset list with a **parametric, model-aware** camera configuration: the operator selects only the values the connected OAK supports, sees them rendered as values (not decoded from filenames), and the configure hook composes the single `camera-params` YAML depthai_ros_driver actually consumes.

Delete this file once all phases ship; migrate durable invariants into `CLAUDE.md` / `ARCHITECTURE.md` as they land.

## Why

A preset filename tries to be the whole spec. That collapses once a config has two legs (raw + H.264) × two streams (RGB + depth) × resolution × fps × model compatibility — `rgb-raw-1080p30-h264-720p30-depth-raw-400p30-disp-h264-400p30` is unreadable and still can't say "needs a stereo camera." The fix: make the camera's **capabilities** the source of truth, the config a **validated, parametric choice** on top, and the UI **display the values**. The verbose explicit name becomes an auto-generated label, never hand-maintained.

## Decisions (locked via interview)

| Topic | Decision |
| -- | -- |
| Config home | **Manage-only (v1)** — Manage is the authoritative parametric editor; Operate shows a read-only summary chip. The Operate quick-switch is **deferred** (it conflates an instant source-switch with a snap reconfig + restart). |
| Shipped bundles | **Migrate all 18** current `camera-params-*.yaml` into saved bundles in the new format (auto-named by the generator). |
| No camera at probe | **Static capability matrix** keyed by the operator-set `model` populates the dynamic enums, so config stays editable offline (also the PoE-before-first-probe + new-variant fallback). |
| Device scope | **Multi-OAK in v1** — the probe enumerates all connected OAKs; a top-level `mxid` field targets one. |
| PoE | `i_ip` is a top-level `camera`-concern field the probe consumes first (no PoE auto-discovery in v1). |
| Config model | **Parametric + dynamic enums** — expose the real axes; presets are saved bundles, not the primary surface. |
| Capability source | **Probe on apply (hook)** — a depthai device query the agent runs on demand (works with the daemon stopped) writes a caps file that populates the dynamic enums. |
| Naming | **Auto-generated, fully explicit, `p`-shorthand + fps glued** — concat of the enabled capability fragments, e.g. `rgb-raw-1080p30-h264-720p30-depth-disp-h264-400p30`. |
| Storage | **One `camera` files-first concern with capability sections** (rgb / depth / imu / ir / pointcloud), composed + derived by the hook. A preset = one file = one generated name. |
| Card granularity | **Separate Manage cards** per capability — RGB, Depth, IMU, IR, Pointcloud each render as their own model-gated card. |

## Target model

### 1. Capability probe (`camera` snap, new)

A small probe the configure/apply hook runs **before** composing the YAML (works even when `husarion-depthai.daemon` is stopped):

- Uses the depthai API (python) to detect the connected device: **model** (OAK-1 / OAK-D-LITE / OAK-D-PRO / OAK-D-PRO-POE / …), **sensors** present (mono pair? IMX378 vs IMX214?), **IR** present, **IMU** present, and the **supported resolutions/fps** per sensor.
- Writes `…/camera/capabilities.json` (the dynamic-enum source).
- PoE: needs `i_ip` to reach the device → the probe reads the `camera` concern's `ip` field first (see Open Q4).
- No device reachable → fall back to a shipped **static capability matrix** keyed by the operator-declared `model` (see Open Q3).

### 2. `camera` files-first concern (manifest with sections)

One concern, rendered by the existing Manage generic renderer (`webui` `ConfigSubview`), fields backed by **dynamic enums** sourced from `capabilities.json` so only valid values appear. Sections (each a Manage card, shown only when the detected model supports it):

- **rgb** — `resolution` (enum), `fps` (enum, gated on resolution), `raw` (bool), `h264` (bool), `h264_bitrate`, optional asymmetric `enc_width/height`.
- **depth** (stereo models only) — `enabled`, `disparity_h264` (bool, 2D preview), `metric_raw` (bool, 16UC1), `resolution`, `fps`, `align_to_rgb`, `lr_check`. (`subpixel` is **derived**, not exposed — see rules.)
- **imu** (models with an IMU) — `enabled`.
- **ir** (Pro/PoE only) — `enabled`, optional `dot_brightness` / `flood_brightness`.
- **pointcloud** (stereo models) — `enabled` (RGBD pipelines only).
- top-level — `model` (auto-detected, operator-overridable), `ip` (PoE), `startup_delay`.

### 3. Compose + derive hook (`camera` snap, the heart)

The hook reads the concern, **derives** the coupled fields, deep-merges into the single `camera-params` YAML, `snap set driver.camera-params=<generated>`, and restarts the daemon. Derivation table (RGB+depth are NOT independent — they pick one `camera.i_pipeline_type`):

| Enabled streams | `i_pipeline_type` |
| -- | -- |
| RGB raw only | `RGB`, `rgb.i_low_bandwidth=false` |
| RGB H.264 only | `RGB`, `rgb.i_low_bandwidth=true` |
| RGB raw + H.264 | `husarion_depthai::pipeline_gen::RGBDual` |
| RGB (single leg) + depth | `RGBD` (+ stereo low_bandwidth for disparity-H.264) |
| RGB dual and/or depth dual | `husarion_depthai::pipeline_gen::RGBDDual` |
| Depth only (rgb lazy) | `RGBD`, stereo `i_low_bandwidth=true` |

Cross-constraints the hook enforces (reject or auto-fix + warn):

- **disparity-H.264 ⇒ `stereo.i_subpixel=false`** (8-bit disparity for the on-chip encoder; subpixel makes it 16-bit → encoder rejects it).
- **depth / pointcloud ⇒ stereo-capable model** (never OAK-1).
- **IR enabled ⇒ Pro/PoE** (no-op + warn on Lite).
- **4K ⇒ IMX378/IMX214** (per probe).
- **dual plugins ⇒** the `husarion_depthai_pipeline` part is built into the snap AND the raw stream's image_transport republisher is suppressed (`<node>.rgb.image_raw.enable_pub_plugins: ['image_transport/raw']`).
- **PoE ⇒ `i_ip` set**.

### 4. Naming generator

Deterministic label from the enabled fragments (locked grammar — `p`-shorthand, fps glued, depth legs mirror RGB):

```
rgb-<raw|h264|raw-…-h264>-<res>p<fps>[…asymmetric]
  [-depth-<raw|disp-h264|raw-…-disp-h264>-<res>p<fps>]
  [-imu-on][-ir-on]
```

e.g. `rgb-raw-1080p30-h264-720p30-depth-disp-h264-400p30-imu-on`. Same generator names shipped bundles and operator-saved presets.

### 5. Manage + Operate (webui)

- **Manage:** per-capability cards (§2) via the existing concern renderer + dynamic enums. Apply runs the compose+derive hook → snap restart (reuse the streaming-apply UX).
- **Operate (v1):** read-only summary chip of the active pipeline. The model-filtered quick-switch of saved presets is a **follow-up** — it mixes a runtime source-switch (the existing husarion_camera switcher) with a snap reconfig + restart, so it's kept out of v1.

## Phases

1. **Probe** (enumerate all OAKs + caps → `capabilities.json`) **+ static matrix fallback** (snap).
2. **`camera` concern manifest** (sections + `mxid`/`ip`) **+ compose/derive hook** (snap).
3. **Naming generator** (shared rule; snap-side for files, webui-side for display labels) **+ migrate all 18 current presets** into bundles via it.
4. **Manage cards + dynamic enums** (verify the generic renderer covers sections + model-gating; extend if needed) (agent/webui).
5. **Operate read-only summary chip** (webui). *(Quick-switch = follow-up.)*
6. **Retire the old `camera-params-*.yaml`**; docs; drift check.

## Resolved (all closed via interview — no blockers)

1. **Operate ↔ source switcher** → **Manage-only for v1**; the Operate depthai quick-switch is a follow-up (avoids mixing runtime source-switch with reconfig+restart).
2. **Shipped bundles** → **migrate all 18** existing presets into the new format.
3. **No camera at probe** → **static capability matrix** by declared `model`.
4. **PoE `i_ip`** → top-level `camera`-concern field, consumed by the probe first.
5. **Scope** → **multi-OAK in v1** (enumerate all; `mxid` field targets one).

### Multi-OAK note (Phase 1 impact)

The probe enumerates every connected OAK + its caps; the `camera` concern gains an `mxid` field (dynamic enum of detected devices). The hook passes the selected `mxid` (and `i_ip` for PoE) to the driver. Per-device config is one concern targeting one `mxid` in v1 (multiple simultaneous OAK pipelines = future work).

## Touchpoints outside this repo

- `husarion-agent` (cockpit repo) — files-first concern manifest format + dynamic-enum resolver; confirm sections + model-gating are expressible (`AGENT-files-first.md`).
- `husarion-cockpit/webui` — Manage card rendering (generic, likely no change)
  - Operate quick-switch (new).
