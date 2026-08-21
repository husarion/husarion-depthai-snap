# CLAUDE.md — AI guidelines for `husarion-depthai-snap`

> Targeted at AI agents (you and future sessions). Update on every meaningful architectural change.

## Project context

A snap (Snap Store: [`husarion-depthai`](https://snapcraft.io/husarion-depthai)) packaging the **Luxonis OAK-x** camera driver (DepthAI) as a ROS 2 node plus a Husarion-style configuration layer (DDS, namespace, parameters via `snap set`).

- **Target users**: operators of Husarion robots (ROSbot, Panther, …) — after `sudo snap install`, setup is 3 commands: `post_install.sh` + `snap set driver.model=<model>` + `.start` (the daemon ships install-mode: disable and requires driver.model).
- **ROS distros**: `jazzy` (`core24`) only — `humble` support dropped 2026-08 (EOL). The snap manifest is still generated from a Jinja template (single distro today, kept templated in case a future distro, e.g. `lyrical`, gets added).
- **Architectures**: `amd64` + `arm64` (mainly Raspberry Pi 5 on the robots).
- **Confinement**: `strict`. Required plugs: `raw-usb`, `hardware-observe`, `network`, `network-bind`, `shared-memory`.

## Architecture — the bare minimum to remember

```
snapcraft_template.yaml.jinja2  ──► render_template.py ──► snap/snapcraft.yaml (ARTIFACT, gitignored)
        ▲ single source of truth
        │
   (just build / CI: jazzy only)

snap/local/*               ──dump──►  $SNAP/usr/bin, $SNAP/usr/share/husarion-depthai/config
husarion-snap-common@0.13.0 ──dump──►  $SNAP/usr/bin, $SNAP/usr/share/husarion-snap-common/config

apps:
  daemon (systemd, restart-condition: always)  command-chain: ros_setup.sh
  husarion-depthai (foreground)                command-chain: check_daemon_running.sh, ros_setup.sh
  start / stop / restart                       wrappers around `snapctl start|stop|restart .daemon`
```

Detailed diagram → [ARCHITECTURE.md](ARCHITECTURE.md).

## Working rules — what to stick to

1. **Never edit `snap/snapcraft.yaml` by hand.** It's a regenerated artifact from [snapcraft_template.yaml.jinja2](snapcraft_template.yaml.jinja2). It's `.gitignore`'d. Direct edits get overwritten on the next `just build`.
2. **Every Jinja change → re-render.** After editing `snapcraft_template.yaml.jinja2` run `just build` (or `./render_template.py snapcraft_template.yaml.jinja2 snap/snapcraft.yaml` with `ROS_DISTRO` set) before building anything.
3. **`husarion-snap-common` is pinned to tag `0.13.0`.** Provides validators, the `ros.env` generator, DDS XML, the `start/stop/restart_launcher.sh` wrappers. Editing its scripts requires changes in the external repo plus an optional pin bump — do not patch locally.
4. **Default `driver.*` values live in 4 places.** Keep them in sync:
   - [snap/local/apply_defaults.sh](snap/local/apply_defaults.sh) — `set_default_if_unset driver.X <default>` (called by install AND post-refresh hooks; idempotent so refreshes don't clobber user values)
   - [snap/hooks/configure](snap/hooks/configure) — `VALID_DRIVER_KEYS` + validators
   - [snap/local/launcher.sh](snap/local/launcher.sh) — `OPTIONS` (passthrough to `ros2 launch`)
   - [snap/local/depthai.launch.py](snap/local/depthai.launch.py) — `DeclareLaunchArgument(..., default_value=...)`
   - optionally the description in `description:` in the Jinja template (markdown shown in Snap Store)
5. **YAML files in `snap/local/` land in `${SNAP_DATA}`** (= `/var/snap/husarion-depthai/current/`). The install hook copies them on first install; the `post-refresh` hook overwrites **only the bundled presets** on every `snap refresh` — user-added YAMLs survive (snapd performs `$SNAP_DATA` data migration per revision). Legacy customs from `${SNAP_COMMON}` are one-shot migrated by post-refresh.
6. **`restart-condition: always` on the daemon is not cosmetic.** The startup-delay workaround (see Pitfalls) used to rely on `exit 0` after the first sleep so systemd would restart the daemon — only the second iteration actually started ROS. After the move to the uptime check, the workaround no longer needs it, but the property remains as a general crash-recovery safeguard.
7. **`grade: stable` is always set** ([snapcraft_template.yaml.jinja2:45](snapcraft_template.yaml.jinja2#L45)) regardless of channel. The version is `<upstream depthai-ros release>-<build date>` — the `apt-cache policy ros-{distro}-depthai-ros-driver` Candidate cut at the Debian revision, plus a date injected once per CI run (`BUILD_DATE` → `render_template.py`, computed in publish.yaml's `build-date` job so both arches agree). See [ARCHITECTURE.md](ARCHITECTURE.md) §D3b.
8. **Touching anything user-facing? Test on the target platform (RPi5/ARM64).** Some bugs (e.g. `fix-execstack` on `libamdhip64.so*`) only manifest on a specific architecture.

## Conventions

### Naming

- **Snap parameters** in two namespaces: `driver.*` (DepthAI-specific) and `ros.*` (transport / domain id / namespace). Boundary: `driver.*` is handled by `snap/hooks/configure`, `ros.*` by `configure_hook_ros.sh` from snap-common.
- **Snap keys** in kebab-case (`driver.pointcloud`, `driver.startup-delay`). The launcher converts them to snake_case for `ros2 launch` arguments ([launcher.sh:36](snap/local/launcher.sh#L36)).
- **Preset files**: `camera-params-<NAME>.yaml` in `${SNAP_DATA}`. The inline check in the configure hook assumes that exact format — don't change it. `NAME=autogenerated` is special (see "camera-params: autogenerated vs custom"); anything else is a hand-authored file.
- **ROS topics**: `/<namespace>/<name>/rgb/image_raw[/compressed]`, `/<ns>/<name>/stereo/image_raw[/compressed]`, `/<ns>/<name>/points`. `<name>` is `driver.name` (default `oak`).

### Bash scripts (in `snap/local/` and snap-common)

- Shebang: `#!/bin/bash -e`
- `source $SNAP/usr/bin/utils.sh` at the top (logging + validators)
- Logging: `log "..."` (syslog only) or `log_and_echo "..."` (syslog + stderr). Syslog tag: `${SNAP_NAME}`.
- Parameter validation: always via `validate_*` from `utils.sh` (regex/option/keys/config_param/float/number/path/peers_list/ipv4_addr/ipv6_addr/hostname). Don't write ad-hoc validators.

### Code style

- YAML: 2 spaces, `---` at the top of ROS configuration files.
- Python (launch files): 4 spaces, type hints optional. `snake_case` variables. `launch.*`/`launch_ros.*` imports sorted.
- No "what" comments — only "why" (e.g. `# workaround for the booting issue`).

## Commands

### Local iteration

```bash
# Full cycle: clean + render + build + install + post_install
just iterate

# Build only (after editing Jinja)
just build

# Clear snapcraft cache
just clean
```

### After build (`just build`):

- Produces `husarion-depthai_<version>_<arch>.snap` (~543 MB) in the project directory.
- `just install` unpacks it (`unsquashfs`) and runs `sudo snap try squashfs-root/`.
- After install: `sudo /var/snap/husarion-depthai/common/post_install.sh` connects plugs (raw-usb, hardware-observe, shm-plug↔shm-slot); then `snap set husarion-depthai driver.model=…` + `husarion-depthai.start`.

### Operations on the running snap (on the target host)

```bash
sudo snap set husarion-depthai ros.namespace=robot
sudo snap set husarion-depthai driver.model=OAK-D-PRO
sudo snap set husarion-depthai driver.depth=true                     # adds metric depth + on-chip H.264 disparity preview
sudo snap set husarion-depthai driver.pointcloud=true                # PCL (requires driver.depth=true)
sudo snap set husarion-depthai driver.resolution=1080P
sudo snap set husarion-depthai driver.fps=30
sudo snap set husarion-depthai driver.imu=true
sudo snap set husarion-depthai driver.ir=true
sudo snap set husarion-depthai driver.ip=10.15.20.6                  # PoE
sudo snap set husarion-depthai ros.transport=udp-lo-cyclone

sudo husarion-depthai.start         # enable + start daemon
sudo husarion-depthai.stop          # disable + stop daemon
sudo husarion-depthai.restart       # restart daemon
husarion-depthai                    # foreground (must be .stop'd first)

journalctl -t husarion-depthai -f   # logs (tag = SNAP_NAME)
snap logs husarion-depthai.daemon -f
```

### Publish to Snap Store

```bash
just prepare-store-credentials   # export login → exported.txt
just publish                     # upload + release on jazzy/edge
```

In practice CI does this ([.github/workflows/publish.yaml](.github/workflows/publish.yaml)) on push to `main` (`/edge`) or on tag (`/candidate`).

### Build on a weaker machine (LXD OOM)

```bash
just swap-enable    # 8 GB swap + lower swappiness
just swap-disable   # rollback
just remove-lxd-cache   # free space from snapcraft LXD containers
```

## Pitfalls and constraints

### `driver.startup-delay` + uptime check (workaround for "1st startup after reboot")

- Default `30` (seconds). Range `0..120`. Set to `''` or `0` to disable.
- Empirically: 30s is enough for most x86 hosts and RPi5; some slow-booting systems may need 45-60s. If after reboot you see depthai `X_LINK_ERROR` ~20s after "Camera ready", USB stack hasn't stabilized — bump higher.
- Mechanics ([snap/local/launcher.sh:63-71](snap/local/launcher.sh#L63)):
  - Reads `/proc/uptime` (available under strict confinement without any plug).
  - If uptime < `STARTUP_DELAY + 60s` → treat as "fresh boot" → `sleep ${STARTUP_DELAY}`.
  - Otherwise → no sleep.
- Consequence: only the process starting ~a minute after system boot pays the delay. Manual `snap restart`, refresh, or daemon crash after normal uptime do not introduce a delay — no risk of a `sleep → fail → restart → sleep` loop.
- The `+ 60s` window is arbitrary but practical: if the daemon starts 80s after boot, the system is already stable.
- **Earlier version** (up to `ecf72d9`, 2025-11-27) used a flag file in `$SNAP_DATA` + `exit 0` + `restart-condition: always` — simplified in this iteration. `restart-condition: always` stays on the daemon as general post-crash resilience, no longer tied to the workaround.
- **Status**: heuristic workaround — re-check it on every `ros-{distro}-depthai-ros` bump (see "Workflow → Before bumping apt dependencies").

### Two apps sharing the same `launcher.sh`

- `daemon` (systemd) and `husarion-depthai` (foreground) both invoke the same `launcher.sh`.
- `husarion-depthai` additionally has [check_daemon_running.sh](https://github.com/husarion/husarion-snap-common/blob/0.13.0/local-ros/check_daemon_running.sh) in its command-chain — it detects a live daemon and tells the user to stop it first.
- The foreground does not respect `restart-condition` or the flag-file logic (because daemon and foreground may have separate `$SNAP_DATA`? — to be confirmed, but the code uses `$SNAP_DATA`, which is per-snap, not per-app).

### `pointcloud` requires the RGBD pipeline — but NOT the device-side Sync node

- `driver.pointcloud=true` loads `depth_image_proc::PointCloudXyzrgbNode` ([depthai.launch.py](snap/local/depthai.launch.py)) — a launch-topology decision (whether to load an extra composable node), not a `depthai_ros_driver` parameter, so it stays a launch arg rather than a `generate_camera_params.sh` key.
- **Do NOT force `pipeline_gen.i_enable_sync=True` for this** (a prior version of this launch file did, mirroring upstream `camera.launch.py`'s `pointcloud.launch.py` pattern) — **it crashes the daemon**: `PipelineGenerator::createPipeline` (v2.12.2-jazzy) unconditionally instantiates a `dai_nodes::Sync` node when `i_enable_sync=true`, then links in whatever each active node's `getPublishers()` returns. `RGBDual`/`DepthDual` ([rgb_dual.cpp](ros/husarion_depthai_pipeline/src/rgb_dual.cpp)/[depth_dual.cpp](ros/husarion_depthai_pipeline/src/depth_dual.cpp)) **always** return `{}` from `getPublishers()` — a deliberate choice, since this dual-output pipeline's raw/encoder taps are inherently unsynced — so the Sync node gets zero linked inputs and the device pipeline build fails with `terminate ... Sync(17) - No inputs`, killing the container (confirmed live, 2026-08: crash-loop on every `driver.pointcloud=true`). `rgb.i_synced`/`stereo.i_synced` are equally inert — neither is read anywhere in `husarion_depthai_pipeline`. Since this snap's `camera-params` is unconditionally the custom dual plugin (never a stock `RGB`/`RGBD` type — see below), this override could never do anything but crash; it isn't a "nice-to-have off by default", it's flat-out incompatible and must stay removed. `PointCloudXyzrgbNode`'s own `message_filters::ApproximateTime` sync (ROS-side, independent of depthai's device Sync) is what actually keeps RGB/depth frames matched.
- PCL needs a depth-producing pipeline; it's a no-op if the active `camera-params` file doesn't have one (e.g. a custom RGB-only file — see below).
- **Part of the `rectify-rgb`/`depth`/`pointcloud` dependency chain** — see below.

### `rectify-rgb` / `depth` / `pointcloud` dependency chain (2026-08)

Three keys form a strict chain — `rectify-rgb ← depth ← pointcloud` — enforced in `snap/hooks/configure`:

1. Turning a **lower** link OFF cascades OFF everything above it: `rectify-rgb=false` ⟹ `depth=false` ⟹ `pointcloud=false`.
2. Turning a **higher** link ON cascades ON everything below it: `pointcloud=true` ⟹ `depth=true` ⟹ `rectify-rgb=true` (verified against upstream — see below). Rejected outright — not cascaded — if the model has no stereo pair.

**Why this needs a persisted state file, not just a snapshot of the current values**: a snapshot alone can't tell "`pointcloud=true` was just requested, `depth` hasn't caught up in THIS run yet" apart from "`depth=false` was just requested, `pointcloud` is stale from an earlier run" — both produce the exact identical `{depth: false, pointcloud: true}` snapshot, yet the correct action is opposite in each case (cascade depth ON in the first, cascade pointcloud OFF in the second). An earlier version of this logic picked a fixed check-order ("off always wins") to sidestep the ambiguity — that broke the ON direction outright: `pointcloud=true` from a fresh install immediately cascaded itself back OFF, because `depth` still read `false` in that same run. The fix compares against `${SNAP_DATA}/.camera-chain-state` (`PREV_RECT`/`PREV_DEPTH`/`PREV_POINTCLOUD` — the end-state written by the previous successful `configure` run) to see which link **actually** just flipped, and cascades only from a real `true→false` (cascade off) or `(not true)→true` (cascade on) transition. Missing/empty prev-state (first run ever, e.g. right after install) is treated as "no transition happened" for the off-checks and "already off" for the on-checks — safe because `apply_defaults.sh` always seeds a consistent baseline (`rectify-rgb=true`, `depth=false`, `pointcloud=false`) before `configure` ever runs for real.

Worked examples (each line is a separate `snap set` call, in sequence):

- `pointcloud=true` (fresh install, everything else at its default) → cascades `depth=true` → which itself cascades `rectify-rgb=true` (already `true` here, so a no-op). One `snap set` turns on all three where needed.
- From `{rect:true, depth:true, pointcloud:true}`, `depth=false` → `pointcloud` auto-disables. No error, no re-forcing `depth` back to `true` (the old, fought-the-operator behavior).
- From the same steady state, `rectify-rgb=false` → both `depth` and `pointcloud` auto-disable.
- `snap set driver.rectify-rgb=false driver.pointcloud=true` in ONE command (contradictory) → `rectify-rgb=false` wins, all three end up `false` — not a half-applied state.
- An unrelated `snap set driver.resolution=1080P` → no cascade fires, no log noise (none of the three chain keys changed).

Why `pointcloud` needs `depth`: verified against upstream `depthai-ros`'s own `pointcloud.launch.py`/`rgbd_pcl.launch.py`, which always pair `PointCloudXyzrgbNode` with an RGBD `params_file` (`pcl.yaml`/`rgbd.yaml`) — the node remaps its depth input from `<name>/stereo/image_raw`, a topic that doesn't exist without depth. "Pointcloud without depth" isn't a real, supported mode upstream either.

### `camera-params`: autogenerated vs custom

`driver.camera-params` has exactly two modes (2026-08 design):

- **`autogenerated`** (the default) — [generate_camera_params.sh](snap/local/generate_camera_params.sh), invoked by `configure` on **every** `snap set`, compiles `driver.depth`/`resolution`/`fps`/`imu`/`ir`/`ip` into `${SNAP_DATA}/camera-params-autogenerated.yaml` from scratch, always the custom `husarion_depthai_pipeline` dual plugin (`RGBDual`/`RGBDDual` — never a stock `RGB`/`RGBD` type). No separate compose step to drift out of sync: the file IS the current `driver.*` state, always.
- **anything else** — a hand-authored file. `configure` detects `camera-params != autogenerated` and skips generation entirely (logs it, so it's never a silent no-op) — `depth`/`resolution`/`fps`/`imu`/`ir`/`ip` stop having ANY effect, and that file is used completely verbatim. This is the escape hatch for anything the exposed keys can't express (asymmetric raw/encoder sizing, manual exposure for SLAM, hand-tuned `align_depth`/`lr_check`/`i_nn_type`, ...) — copy `camera-params-autogenerated.yaml` to a new name and hand-edit freely (README "Camera config").
- **Why this split, not a launch-time override** (superseded 2026-08 design — `depthai.launch.py` used to parse the resolved `params_file` at launch and merge override dicts, via `resolve_depth_override()`/`resolve_rgb_overrides()`/`resolve_sensor_overrides()`, now deleted): a runtime override only ever covers the specific fields it was written for, and — worse — it ALSO clobbered a hand-authored custom file's own values for those same fields (e.g. a custom `i_pipeline_type: RGBD` file still got forced back to RGB by the `depth=false` default) unless the operator remembered to mirror every field. The autogenerated/custom split removes that footgun entirely: custom means fully hands-off, no exceptions, and `depthai.launch.py` goes back to loading `params_file` verbatim — no YAML parsing at launch, no pipeline-family state machine.
- Legacy migration: `driver.camera-params=default` (the pre-2026-08 single bundled preset, deleted) auto-migrates to `autogenerated` in `configure` (equivalent behavior, always-current instead of static). A host still pointed at an even older named preset (`oak-d-pro` etc., deleted 2026-08 alongside the rest of the original 18) is **untouched** by this migration and by generation — it's already "custom" by the two-mode split above, so it just keeps working off its surviving `$SNAP_DATA` copy.
- **One source of truth for cockpit too**: the `husarion-agent-extras` `camera` concern (`snap/husarion-agent-extras/config-seed/`) is a thin pass-through — its manifest fields map 1:1 onto these same `driver.*` keys via `snapctl set` (see `config-seed/hooks/camera`), and it force-pins `driver.camera-params=autogenerated` on every run so Manage always drives the generated file even if a headless `snap set` previously switched to a custom one. Its `RGB_RESOLUTION` manifest field default (`720P`) is kept in sync with `apply_defaults.sh` — if you change one, change the other.

### `depth`/`resolution`/`fps`/`imu`/`ir`/`ip` — what each key does

- `driver.depth` (default `false`) sets `camera.i_pipeline_type` to `husarion_depthai::pipeline_gen::RGBDDual` (true) or `RGBDual` (false). Turning it on also forces `stereo.i_subpixel=false` (the on-chip disparity-H.264 leg needs 8-bit input) and sets `<name>.stereo.image_raw.enable_pub_plugins: ['image_transport/raw', 'image_transport/compressedDepth']` — the metric raw depth publisher's own lazy republisher allowlist. `image_transport/raw` avoids a topic collision with the on-chip encoder tap (both otherwise share the `.../stereo/image_raw` base); `image_transport/compressedDepth` is an ADDED lazy **software** PNG depth compressor (2026-08) — zero cost unless a consumer actually subscribes to `.../stereo/image_raw/compressedDepth`. Deliberately not `image_transport/compressed`/`theora` — both assume 8-bit color and mishandle 16-bit depth.
- `driver.resolution` (default `720P`) / `driver.fps` (default `30`) set `rgb.i_resolution` / `rgb.i_fps` (+ the H.264 keyframe cadence). `720P`/`30` was picked as the universal safe baseline — every OAK-x model supports it (unlike `1080P`/`4K` or `60` fps, which are sensor/USB-bandwidth-dependent).
- `driver.imu` (default `false`) sets `pipeline_gen.i_enable_imu`, **not** `camera.i_enable_imu` — that name exists as a param declaration in `camera_param_handler.cpp` but is never read anywhere in depthai-ros v2.12.2-jazzy (confirmed by tracing `PipelineGenerator::createPipeline`); the actual gate is `pipeline_gen.i_enable_imu` (default `true` upstream — an unconditional `snap set driver.imu=false` closing it is why this key exists at all, since the driver would otherwise always turn the IMU on regardless of `camera.i_enable_imu`). Bug found and fixed 2026-08: the generated config previously wrote the dead `camera.i_enable_imu` key, so `/oak/imu/data` kept publishing at ~89 Hz no matter what `driver.imu` was set to — confirmed live (`ros2 param get /oak camera.i_enable_imu` correctly showed `false`, yet the topic still streamed real data).
- `driver.ir` (default `false`) sets `camera.i_enable_ir` — genuinely read in `camera.cpp`'s `Camera::setIR()` (verified at `v2.12.2-jazzy`), unlike IMU above: a **single switch driving both** the IR laser dot projector (helps stereo depth correlation in low-light/textureless scenes) **and** the IR floodlight (general scene illumination) together, each at upstream's fixed default intensity (`r_laser_dot_intensity`/`r_floodlight_intensity` = 0.6) — we don't expose those intensities separately.
- `driver.ip` (no default — the one exception, unset = USB) sets `camera.i_ip` (PoE).
- **Capability-gated in `configure`**: `depth`/`pointcloud`/`imu`/`ir` are rejected outright against the wrong `driver.model` (same static per-model lists as `camera-probe.py`'s `MATRIX`) before `generate_camera_params.sh` ever runs. `resolution`/`fps`/`ip` are NOT model-gated here (too many valid per-model combos to duplicate in bash) — an unsupported combo fails at the driver, not at `snap set` time; the cockpit's dynamic enum (fed by `camera-probe.py`'s per-model lists) keeps Manage from offering it.
- **What `depth=true` does NOT do**: synthesize fully tuned stereo params beyond subpixel/republisher — driver defaults for `align_depth`/`lr_check`. For different tuning (e.g. manual exposure for SLAM), switch to a custom preset (see above).

### Frame IDs are `<driver.name>_*` and cannot carry `ros.namespace`

Comes up whenever someone wants the camera's frames namespaced. **There is no way to do it, and no `driver.tf-prefix`-style key can be added** — verified against upstream `v2.12.2-jazzy` and the robot URDF:

- Image / camera_info headers get `frame_id = tfPrefix + "_" + <socket> + "_camera_optical_frame"`, and upstream's `sensor_helpers::tfPrefix()` returns **the ROS node name** unless `camera.i_publish_tf_from_calibration` is true, in which case it returns `camera.i_tf_base_frame`. No preset here sets either, so the effective frame is `<driver.name>_rgb_camera_optical_frame` — `oak_rgb_camera_optical_frame` by default.
- That already matches the robot URDF: `husarion_components_description`'s `luxonis_depthai` macro passes `camera_name=<component_name>` (default `oak`) to `depthai_descriptions`' `depthai_camera`, which names its links `${camera_name}_rgb_camera_optical_frame` — **plain, no namespace**. Only `base_frame` and the Gazebo sensor blocks take a `<namespace>/` prefix.
- **There is no lever to namespace them.** ROS node names cannot contain `/`, so the only other path is `i_tf_base_frame`, read *only* when `i_publish_tf_from_calibration=true` — the path deliberately dropped from this snap (coverage table in [ARCHITECTURE.md](ARCHITECTURE.md): "Husarion uses own robot URDF; would conflict"). Enabling it would publish a second TF tree for frames the robot's `robot_state_publisher` already owns.
- Cross-snap consequence: this is why `husarion-rplidar` does **not** derive its `frame_id` from the namespace either (its `driver.frame-id` is forwarded verbatim, defaulting to plain `laser`) — the two snaps keep matching argument sets, and both stamp the plain tree that `robot_state_publisher` publishes. An operator can still hand-write `driver.frame-id=<namespace>/laser` there for the bridged global tree, but then the scan and the camera no longer resolve in one tree — so don't, if anything correlates them (e.g. the WebUI lidar→camera overlay).

### Data layout: `${SNAP_DATA}` vs `${SNAP_COMMON}`

- **`${SNAP_DATA}` = `/var/snap/husarion-depthai/current/`** (per-revision, copied by snapd on refresh):
  - `camera-params-autogenerated.yaml` — regenerated by `configure` (`generate_camera_params.sh`) on every `snap set`, not by `post-refresh` — there's nothing left to "re-bundle" on a refresh, generation is always current. Custom-named `camera-params-*.yaml` files are the operator's own; nothing in this snap touches them.
  - `.camera-chain-state` — end-state of `rectify-rgb`/`depth`/`pointcloud` from the last successful `configure` run, written by `configure` itself (`PREV_RECT`/`PREV_DEPTH`/`PREV_POINTCLOUD`, shell-sourced). Lets that run's before/after diff detect which link in the dependency chain actually just flipped — see "rectify-rgb / depth / pointcloud dependency chain" below. Not user-facing; safe to delete (rebuilds itself, treating the loss as "no transition happened" on the next run).
  - `.startup-delay-done` (legacy) — unused after the move to the uptime check; may linger.
- **`${SNAP_COMMON}` = `/var/snap/husarion-depthai/common/`** (cross-revision, NOT copied by snapd):
  - RMW profiles (`rmw/<impl>/*.xml`) from snap-common — regenerated by `configure_hook_ros.sh` on every `ros.*` change.
  - `ros.env`, `ros_snap_args`, `manage_ros_env.sh` — written by `configure_hook_ros.sh`.
  - `post_install.sh` — user-facing helper (copied by the install hook).
- **When to put data where**: `${SNAP_DATA}` when the schema/format may change between snap versions (yaml presets — schema is tied to the depthai-ros version). `${SNAP_COMMON}` when the data is version-independent and should survive refresh untouched (DDS XML, user-managed env).

### The snap installs `rmw_fastrtps_cpp`, `rmw_cyclonedds_cpp`, and `rmw_zenoh_cpp`

- The `ros2-{distro}-ros-base` extension pulls FastDDS as the default RMW.
- We explicitly add `ros-{distro}-rmw-fastrtps-cpp`, `ros-{distro}-rmw-cyclonedds-cpp`, and `ros-{distro}-rmw-zenoh-cpp` to stage-packages — so `ros.transport=rmw_cyclonedds_cpp`/`udp-lo-cyclone` works, and zenoh works through the agent chain.
- depthai is a plain rclcpp node (no bridge), so the configure hook sets `HSC_ALLOW_ZENOH=1` to opt out of snap-common's zenoh gate.
- The RMW choice happens at runtime via `RMW_IMPLEMENTATION` (exported in `${SNAP_COMMON}/ros.env` by the configure hook).

### `libamdhip64.so*` execstack — nothing to fix, don't re-add the part

A `fix-execstack` part used to run `execstack -c` on `libamdhip64.so*` (added 2024-08, dropped 2026-08). Don't bring it back — it was never needed and never even worked:

- **It was a silent no-op.** Store revision 167 (`jazzy/edge`) ships `usr/lib/x86_64-linux-gnu/libamdhip64.so.5` with `PT_GNU_STACK = RWE` — i.e. unpatched. The `if [ -f "$f" ]` guard swallowed the unexpanded glob, so ~2 years of builds packed, passed store review, and ran in production with execstack ON.
- **Nothing loads the library.** A `DT_NEEDED` scan over all 1348 staged `.so` files finds exactly one dependent: `usr/lib/x86_64-linux-gnu/ucx/libucx_perftest_rocm.so.0.0.0`, a UCX perftest plugin the depthai node never touches. The old claim "pulled in via OpenCV / cv-bridge / ffmpeg" was wrong — none of those link it.
- **Store review doesn't flag it.** `review-tools` whitelists `libamdhip64.so.5.*` in `reviewtools/overrides.py` ([MR merged 2024-07](https://code.launchpad.net/~gbeuzeboc/review-tools/+git/review-tools/+merge/469447), commit `e9c125b`) — ROCm shipped execstack by accident and Canonical exempted it.
- Upstream `libamdhip64-5` (noble, `5.7.1-3`) still has `RWE` as of 2026-08, so don't use "is it RW yet" as the trigger to re-add anything — the library being unused is the reason it doesn't matter.
- Bonus: dropping the part also drops the apt `execstack` build-package, which is unavailable on some `core24` hosts.

If a genuinely-loaded lib ever shows up with execstack ON (`readelf -lW <so> | grep GNU_STACK` → `RWE`), fix it in the part that stages it — and verify the flags in the packed `.snap`, not just in the build log.

### Local `review-tools` always FAILs on `shm-slot` — that one is expected

`review-tools.snap-review husarion-depthai_*.snap` (the only local way to preview store review; `snapcraft pack` does not run it) always ends with:

```
declaration-snap-v2:slots_installation:shm-slot:shared-memory
  human review required due to 'deny-installation' constraint (snap-type)
```

Not a regression. The `shared-memory` **slot** hits a `deny-installation` constraint in snapd's base declaration, and the local tool has no access to the per-snap declaration that lifts it. Ours grants it (`snap known snap-declaration series=16 snap-id=0TB3PBfK8MA4Skr4Ggzy3MrD7dbwmc4Q` → `slots: shared-memory: allow-installation` for `slot-names: [shm-slot]`, authority `canonical`, since 2024-09-04), which is why store uploads pass. Read a local review as "anything *besides* `shm-slot`?" — if a genuinely new snap-declaration constraint appears, the grant does not cover it and the upload will fail.

### Why the stock OAK chip H.264 encoder path is legacy now (`i_low_bandwidth`, no raw)

The Movidius VPU has a dedicated H.264/H.265 encoder, exposed by stock `depthai_ros_driver` via `rgb.i_low_bandwidth=true`. Measured locally on x86: daemon CPU drops from ~127% (libx264 active) to ~1.3% — meaningful for RPi5/ARM64. Historically we still defaulted to software libx264, because the STOCK driver makes raw and encoded outputs mutually exclusive:

- **Driver makes raw and encoded outputs exclusive.** When `low_bandwidth=true` with `i_publish_compressed=true`, the depthai_ros_driver stops publishing raw `/oak/rgb/image_raw` (sensor_msgs/Image bgr8) — only `/oak/rgb/image_raw/compressed` remains (chip H.264 as `ffmpeg_image_transport_msgs/FFMPEGPacket`, despite the `/compressed` suffix). Topic origin: `img_pub.cpp` in upstream luxonis/depthai-ros — verified at v2.12.2 and v3.2.1; not slated for change.
- **Pipeline breakage cascades from the missing raw topic.** `image_proc::RectifyNode` has no input → no `/oak/rgb/image_rect`. `depth_image_proc::PointCloudXyzrgbNode` needs raw RGB for textured PCL → broken.
- **Open upstream bugs**: [#717](https://github.com/luxonis/depthai-ros/issues/717) (`low_bandwidth=true` + `stereo.i_synced=true` causes RGB corruption — and we set `i_synced=true` whenever `pointcloud=true`) and [#687](https://github.com/luxonis/depthai-ros/issues/687) (`i_publish_compressed=false` triggers host-side stride decode bug).
- **`i_low_bandwidth_ffmpeg_encoder` is dead.** Declared in `sensor_param_handler.cpp` with default `"libx264"`, never read back — purely a metadata label in the FFMPEGPacket's `encoding` field. Don't waste time tuning it.

**This is why `camera-params-autogenerated.yaml` uses the custom `husarion_depthai_pipeline` plugin (`ros/`) instead, unconditionally, not as an opt-in preset.** It taps one `ColorCamera.video` output to BOTH a raw XLinkOut AND the on-chip VideoEncoder, publishing raw `/image_raw` (autonomy/rect/PCL) AND on-chip H.264 `/image_raw/ffmpeg` (telepresence, ~0 host CPU) simultaneously — no fork, no host encode, no new snap interface, and none of the exclusivity bugs above (bug #687's `i_publish_compressed=false` trap is moot — the plugin always publishes it). There is no more separate "chip-encoder-only, no raw" preset to opt into; if you genuinely need the stock exclusive path (e.g. absolute minimum CPU, no autonomy consumer at all), switch to a custom preset with `i_pipeline_type: RGB` + `rgb.i_low_bandwidth: true` (README "Camera config", CLAUDE.md "camera-params: autogenerated vs custom").

### The on-chip `FFMPEGPacket` is NOT the real `ffmpeg_image_transport` plugin

Easy to conflate — verified in `ros/husarion_depthai_pipeline/package.xml` and `driver.ffmpeg-params`: two genuinely separate things, now also sharing the conventional topic suffix (`.../ffmpeg`, aligned 2026-08 — was `.../compressed`, which by ROS convention means JPEG/PNG via `compressed_image_transport`, not H.264):

- **On-chip H.264** (`.../rgb/image_raw/ffmpeg`, always on; `.../stereo/image_raw/ffmpeg` too when `driver.depth=true`): our custom `husarion_depthai_pipeline` plugin builds these messages itself from the OAK VPU's H.264 bitstream and publishes them on a **plain `rclcpp::Publisher<FFMPEGPacket>`** — verified in upstream `depthai-ros`'s `img_pub.cpp` (`create_publisher<FFMPEGPacket>(pubConfig.topicName + pubConfig.compressedTopicSuffix, ...)`), so the topic suffix is just a string, no image_transport plugin machinery involved for this leg at all. Depends on `ffmpeg_image_transport_msgs` (the message *definitions* package only) — **not** on `ros-{distro}-ffmpeg-image-transport` (the actual software-encoder plugin). Zero host CPU for the encode. Only covers the RAW (pre-rectification) frame — there is no hardware path for the rectified image.
- **The real `ffmpeg_image_transport`** (the `ros-{distro}-ffmpeg-image-transport` + `ffmpeg` apt packages, configured by `driver.ffmpeg-params`): a genuine `image_transport::PublisherPlugin` doing **software** encoding (libx264 by default) on the host CPU — the same ~127%-of-a-core cost the on-chip path exists to avoid for the raw stream. Removed 2026-08 on the assumption on-chip H.264 covered every streaming need, then **reinstated the same day** once it became clear it doesn't cover `image_rect` (`image_proc::RectifyNode`'s rectified output) — the on-chip encoder taps the raw frame only, so a compressed *rectified* stream has no hardware path at all. Scoped narrowly: `ffmpeg-params-default.yaml` configures ONLY `image_rect.ffmpeg.*`, not the raw leg (that's the on-chip path above, unrelated).
- **Consuming the on-chip topic requires knowing it's not a "real" transport-negotiated stream** — a subscriber has to know to `create_subscription<FFMPEGPacket>(".../image_raw/ffmpeg", ...)` directly (or use an `image_transport::Subscriber` configured for the `ffmpeg` transport, which will happily decode it — the message type and semantics match what that transport expects, we just don't publish through its `PublisherPlugin` machinery). `.../image_rect/ffmpeg`, by contrast, IS a real transport-negotiated stream (goes through the actual plugin) — a plain `image_transport::Subscriber` on `ffmpeg` transport just works. The husarion-cockpit consumer needs its own config update to key off `.../ffmpeg` instead of `.../compressed` for the on-chip leg (tracked/owned in that repo, not here).

## Workflow

### Before bumping apt dependencies (especially `ros-{distro}-depthai-ros`)

Every build pulls a fresh `ros-{distro}-depthai-ros` via `apt-cache policy … | Candidate`. Before publishing a new revision to `jazzy/edge` (i.e. before merging to `main`):

1. Check upstream release notes:
   - [`luxonis/depthai-ros`](https://github.com/luxonis/depthai-ros/releases)
   - [`luxonis/depthai-core`](https://github.com/luxonis/depthai-core/releases) — does the "1st startup after reboot" issue still exist (or have new regressions appeared)?
2. Smoke test on the target platform (RPi5) with the workaround disabled:
   ```bash
   sudo snap set husarion-depthai driver.startup-delay=0
   sudo reboot
   # after reboot:
   ros2 topic list   # does /<ns>/oak/rgb/image_raw appear within ~5s
   ros2 topic hz /<ns>/oak/rgb/image_raw
   ```
3. If an upstream fix is confirmed → remove:
   - the `STARTUP_DELAY` block from [snap/local/launcher.sh](snap/local/launcher.sh)
   - `set_default_if_unset driver.startup-delay 30` from [snap/local/apply_defaults.sh](snap/local/apply_defaults.sh)
   - `"startup-delay"` from `VALID_DRIVER_KEYS` and the `validate_number` call in [snap/hooks/configure](snap/hooks/configure)
   - consider removing `restart-condition: always` from [snapcraft_template.yaml.jinja2](snapcraft_template.yaml.jinja2) (it's an independently healthy daemon property — separate decision)
4. Update the "Pitfalls → `driver.startup-delay`" section in this file.

### How to add a new `driver.X` parameter

1. [snap/hooks/configure](snap/hooks/configure): add the name to `VALID_DRIVER_KEYS` + add a `validate_*` for X.
2. [snap/local/apply_defaults.sh](snap/local/apply_defaults.sh): add `set_default_if_unset driver.X <default>` (unless X should genuinely have no default, like `driver.ip` — see below). This script is run by both the install hook (first install) and the post-refresh hook (every snap refresh), so existing instances upgrading to a snap revision that introduced X will get the default automatically — without overwriting other user-set values.
3. If X should become part of the compiled depthai_ros_driver config: add it to [snap/local/generate_camera_params.sh](snap/local/generate_camera_params.sh) (read via `snapctl get driver.X`, write the corresponding `i_*` key). If X is instead a launch-topology decision (like `pointcloud` — whether to load an extra composable node), add it to [snap/local/launcher.sh](snap/local/launcher.sh)'s `OPTIONS` and to [snap/local/depthai.launch.py](snap/local/depthai.launch.py) (`DeclareLaunchArgument` + use in `launch_setup`) instead.
4. (optional) Document it under `description:` in [snapcraft_template.yaml.jinja2](snapcraft_template.yaml.jinja2) (visible in Snap Store).
5. Build (`just iterate`), install on the target platform, verify `sudo snap set husarion-depthai driver.X=foo`.

Boolean toggles (e.g. `pointcloud`): use `validate_option "driver.X" VALID_BOOL_OPTIONS[@]` (the `VALID_BOOL_OPTIONS=("true" "false")` definition is already at the bottom of [snap/hooks/configure](snap/hooks/configure)). Use `--allow-unset` (`validate_option`/`validate_number`/`validate_ipv4_addr --allow-unset "driver.X" ...`) whenever the key CAN be legitimately unset at runtime — that's true for every key that goes into `generate_camera_params.sh` (`depth`/`resolution`/`fps`/`imu`/`ir`/`ip`), even the ones that also get a concrete `apply_defaults.sh` default (`depth`/`resolution`/`fps`/`imu`/`ir`): allow-unset is what makes the generation script's own "leave this field out" branch reachable via an explicit `snap unset`. Only `driver.model` and `driver.ip` skip `apply_defaults.sh` entirely (genuinely no default). If the new key is capability-dependent (like `depth`/`imu`/`ir`), add a static per-model gate in `configure` too — see the `IMU_REQUESTED`/`IR_REQUESTED` blocks for the pattern (mirrors `camera-probe.py`'s `MATRIX`).

### How to add a new YAML preset (`camera-params-foo.yaml`)

This is for a **custom, hand-authored** preset (`driver.camera-params=foo`, anything other than `autogenerated`) — see "camera-params: autogenerated vs custom" above. There's nothing to add for the autogenerated one; that's generated code, not a file you author.

1. On the target host: `cp .../camera-params-autogenerated.yaml .../camera-params-foo.yaml`, hand-edit it, `snap set husarion-depthai driver.camera-params=foo`.
2. Note: validation in the configure hook is an inline `[ -f "${SNAP_DATA}/camera-params-${value}.yaml" ]` check — nothing to add snap-side, any `<NAME>` passes if the file exists on the host.
3. This is a per-host, per-operator file — it is never bundled in the snap, never shipped from this repo, and never touched by `configure`/`post-refresh`/`generate_camera_params.sh` once `camera-params` points at it.

**History**: before 2026-08 this snap bundled up to 18 named static presets (per-resolution/fps H.264 variants, PoE, SLAM tuning, dual-depth, disparity-only...) shipped from `snap/local/` and refreshed by `post-refresh`. They were collapsed into the single generated `camera-params-autogenerated.yaml` plus the `driver.depth`/`resolution`/`fps`/`imu`/`ir`/`ip` keys above. Deleting the bundled files was safe for any then-existing install: `post-refresh` only ever overwrote files present in the NEW revision's bundle, never deleted ones missing from it (snapd's `$SNAP_DATA` migration copies the whole directory forward) — so a host still pointed at an old preset name kept its working copy, now simply a "custom" file per the two-mode split (untouched by generation, forever).

### How to add a new DDS transport (XML)

- DDS XMLs live in **snap-common**, not in this repo. Editing requires a PR to `husarion/husarion-snap-common`, then bumping the pin in the Jinja template.
- After adding `rmw/fastdds/foo.xml` the user runs `sudo snap set husarion-depthai ros.transport=fastdds/foo`. `validate_config_param` in `configure_hook_ros.sh` will catch a missing file.

### How to fix a bug

- Find the relevant file (usually `launcher.sh`, `depthai.launch.py`, a `*.yaml` in `snap/local/`, or the Jinja template).
- Reproduce on the target platform (`just iterate` on ARM64 if the bug is ARM-only).
- Test = `journalctl -t husarion-depthai -f` + `ros2 topic list/echo/hz`.
- No unit tests. Validation = `snap install --dangerous` in CI.

### How to debug

- `DEPTHAI_DEBUG=1` → `log_level=debug` in `depthai.launch.py`.
- `journalctl -t husarion-depthai -f` (tag = `SNAP_NAME`).
- `snap services husarion-depthai` — daemon state.
- `snap connections husarion-depthai` — which plugs are connected (raw-usb, hardware-observe, shm-plug↔shm-slot are MUST-have).
- `snap get -d husarion-depthai` — dump of all parameters.
- `${SNAP_COMMON}/ros.env` — exactly what the configure hook exported.
- `${SNAP_COMMON}/ros_snap_args` — single-line summary of the current `ros.*` configuration.
- `${SNAP_DATA}/camera-params-*.yaml` — runtime presets (where launcher.sh reads them from); `camera-params-autogenerated.yaml` reflects the exact current `driver.*` values (`cat` it, don't guess).

## What NOT to do

- **Don't commit `snap/snapcraft.yaml`** — it's gitignored and regenerated.
- **Don't remove `restart-condition: always`** without thinking — it protects the daemon from staying down after a crash. (After the move to the uptime check the workaround no longer needs it, but the standalone benefit remains.)
- **Don't add a `driver.X` parameter in just one place** — it'll bite you (validate passes, but the launcher won't forward it, or vice versa).
- **Don't patch snap-common files locally** (`utils.sh`, `configure_hook_ros.sh`, `rmw/<impl>/*.xml`) — the next build overwrites everything. Edit the external repo and bump the pin.
- **Don't use `extensions:` without `SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1`** — `just`/CI set it for you. Running `snapcraft` in a terminal manually will fail.
- **Don't change the naming format of `camera-params-<NAME>.yaml`** (and `rmw/<impl>/<NAME>.xml`) — all regexes in `validate_config_param` assume it. `autogenerated` is a reserved `<NAME>` — don't repurpose it for a hand-authored file.
- **Don't hand-edit `camera-params-autogenerated.yaml`** — it's overwritten on the next `snap set` (any key, not just camera-related ones — `configure` regenerates it every run while `camera-params=autogenerated`). Copy it to a new name first (README "Camera config").
- **Don't commit `husarion-depthai*.snap`** or `squashfs-root/` (they're in `.gitignore` but can grow to ~600 MB on disk — remember `just clean` / `just remove`).
- **Don't push directly to `main`** — use PRs (CI publishes on push to main, so casual commits go straight to `jazzy/edge` in the Snap Store).
