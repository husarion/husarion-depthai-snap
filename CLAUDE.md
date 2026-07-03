# CLAUDE.md — AI guidelines for `husarion-depthai-snap`

> Targeted at AI agents (you and future sessions). Update on every meaningful architectural change.

## Project context

A snap (Snap Store: [`husarion-depthai`](https://snapcraft.io/husarion-depthai)) packaging the **Luxonis OAK-x** camera driver (DepthAI) as a ROS 2 node plus a Husarion-style configuration layer (DDS, namespace, parameters via `snap set`).

- **Target users**: operators of Husarion robots (ROSbot, Panther, …) — after `sudo snap install`, setup is 3 commands: `post_install.sh` + `snap set driver.model=<model>` + `.start` (the daemon ships install-mode: disable and requires driver.model).
- **ROS distros**: `humble` (`core22`) + `jazzy` (`core24`). The snap manifest is shared — generated from a single Jinja template per ROS distro.
- **Architectures**: `amd64` + `arm64` (mainly Raspberry Pi 5 on the robots).
- **Confinement**: `strict`. Required plugs: `raw-usb`, `hardware-observe`, `network`, `network-bind`, `shared-memory`.

## Architecture — the bare minimum to remember

```
snapcraft_template.yaml.jinja2  ──► render_template.py ──► snap/snapcraft.yaml (ARTIFACT, gitignored)
        ▲ single source of truth
        │
   (just build: ROS_DISTRO=humble|jazzy / CI: jazzy only)

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
7. **`grade: stable` is always set** ([snapcraft_template.yaml.jinja2:45](snapcraft_template.yaml.jinja2#L45)) regardless of channel. The version is taken from `apt-cache policy ros-{distro}-depthai-ros-driver` (Candidate).
8. **Touching anything user-facing? Test on the target platform (RPi5/ARM64).** Some bugs (e.g. `fix-execstack` on `libamdhip64.so*`) only manifest on a specific architecture.

## Conventions

### Naming

- **Snap parameters** in two namespaces: `driver.*` (DepthAI-specific) and `ros.*` (transport / domain id / namespace). Boundary: `driver.*` is handled by `snap/hooks/configure`, `ros.*` by `configure_hook_ros.sh` from snap-common.
- **Snap keys** in kebab-case (`driver.enable-pointcloud`, `driver.startup-delay`). The launcher converts them to snake_case for `ros2 launch` arguments ([launcher.sh:36](snap/local/launcher.sh#L36)).
- **Preset files**: `camera-params-<NAME>.yaml` and `ffmpeg-params-<NAME>.yaml` in `${SNAP_DATA}`. The inline check in the configure hook assumes that exact format — don't change it.
- **ROS topics**: `/<namespace>/<name>/rgb/image_raw[/ffmpeg]`, `/<ns>/<name>/stereo/image_raw`, `/<ns>/<name>/points`. `<name>` is `driver.name` (default `oak`).

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
just iterate jazzy        # or: just iterate humble

# Build only (after editing Jinja)
just build jazzy

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
sudo snap set husarion-depthai driver.camera-params=oak-d-pro       # USB; preset from /var/snap/husarion-depthai/current/
sudo snap set husarion-depthai driver.camera-params=oak-d-pro-poe   # PoE
sudo snap set husarion-depthai driver.camera-params=oak-1-lite      # OAK-1-LITE (RGB only)
sudo snap set husarion-depthai driver.enable-pointcloud=true        # PCL (requires RGBD pipeline)
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
just publish                     # upload + release on <distro>/edge
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

### `enable-pointcloud` requires the RGBD pipeline + sync

- `driver.enable-pointcloud=true` loads `depth_image_proc::PointCloudXyzrgbNode` AND forces the parameters `pipeline_gen.i_enable_sync=True`, `rgb.i_synced=True`, `stereo.i_synced=True` (mirrors upstream `camera.launch.py`).
- These overrides are injected at launch level — they overwrite values from `camera-params-*.yaml`. If you want to control sync manually, set `enable-pointcloud=false` and add sync to your own preset.
- PCL makes no sense for RGB-only presets (`default`, `oak-1-lite`) — `i_pipeline_type=RGB` does not produce `/<name>/stereo/image_raw` in the first place. Before flipping `enable-pointcloud=true`, make sure the preset is RGBD (`oak-d-pro`, `oak-d-pro-poe`).

### Data layout: `${SNAP_DATA}` vs `${SNAP_COMMON}`

- **`${SNAP_DATA}` = `/var/snap/husarion-depthai/current/`** (per-revision, copied by snapd on refresh):
  - `camera-params-*.yaml`, `ffmpeg-params-*.yaml` — bundled presets overwritten by the `post-refresh` hook; user customs survive.
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

### `fix-execstack` for `libamdhip64.so*`

- The `fix-execstack` part ([snapcraft_template.yaml.jinja2:249](snapcraft_template.yaml.jinja2#L249)) clears execstack on `libamdhip64.so*` (pulled in as a transitive dependency; blocks startup under strict confinement).
- If you add a new library with execstack ON — add it to the `choosen_files` list.
- `execstack` is unavailable on `core24` hosts in some configurations — then the build needs `swap-enable` or a beefier machine.

### Demo (`demo/`) — unofficial

- `demo/compose.yaml` + `demo/rviz.launch.py` — container with RViz + an `ffmpeg→raw` decoder. Ad-hoc test only. Do not treat as part of the supported user workflow; may be removed.

### Why default doesn't use the OAK chip's hardware H.264 encoder

The Movidius VPU on the OAK has a dedicated H.264/H.265 encoder, exposed by `depthai_ros_driver` via `rgb.i_low_bandwidth=true` + `rgb.i_low_bandwidth_profile=2` (H264_MAIN). Measured locally on x86: daemon CPU drops from ~127% (libx264 active) to ~1.3% — meaningful for RPi5/ARM64. We still default to software libx264. Reasons:

- **Driver makes raw and encoded outputs exclusive.** When `low_bandwidth=true` with `i_publish_compressed=true`, the depthai_ros_driver stops publishing raw `/oak/rgb/image_raw` (sensor_msgs/Image bgr8). Confirmed empirically: only `/oak/rgb/image_raw/compressed` remains, carrying chip H.264 as `ffmpeg_image_transport_msgs/FFMPEGPacket` (despite the `/compressed` suffix). Topic origin: `img_pub.cpp` in upstream luxonis/depthai-ros — verified at v2.12.2 and v3.2.1; not slated for change.
- **Pipeline breakage cascades from the missing raw topic.** `image_proc::RectifyNode` has no input → no `/oak/rgb/image_rect`. `depth_image_proc::PointCloudXyzrgbNode` needs raw RGB for textured PCL → broken. Same goes for `image_rect/*`, `image_raw/{ffmpeg,theora,zstd}` transport variants — none of them appear.
- **Open upstream bugs**: [#717](https://github.com/luxonis/depthai-ros/issues/717) (`low_bandwidth=true` + `stereo.i_synced=true` causes RGB corruption — and we set `i_synced=true` whenever `enable-pointcloud=true`) and [#687](https://github.com/luxonis/depthai-ros/issues/687) (`i_publish_compressed=false` triggers host-side stride decode bug).
- **`i_low_bandwidth_ffmpeg_encoder` is dead.** Declared in `sensor_param_handler.cpp` with default `"libx264"`, never read back. Only ends up as a string label in the FFMPEGPacket's `encoding` field — purely metadata, doesn't switch encoders. Don't waste time tuning it.

The chip encoder is still useful for streaming-only deployments (no rectified image / PCL needed). Available as opt-in via `driver.camera-params=rgb-h264-720p30` + `driver.rectify-rgb=false`. See `camera-params-rgb-h264-720p30.yaml` in the preset list below.

**The raw/encoded exclusivity above is the stock-driver limitation — our custom `husarion_depthai_pipeline` plugin (`ros/`) escapes it.** It taps one `ColorCamera.video` output to BOTH a raw XLinkOut AND the on-chip VideoEncoder, so the `rgb-raw-*-h264-*` / `rgbd-*` presets publish raw `/image_raw` (for autonomy/rect/PCL) AND on-chip H.264 `/image_raw/compressed` (telepresence, ~0 host CPU) simultaneously — no fork, no host encode, no new snap interface. That's now the recommended way to get the chip encoder's CPU win without losing the raw pipeline; the bullets above only apply to the stock `i_low_bandwidth` path. (Bug #687's `i_publish_compressed=false` trap is moot — the plugin always publishes compressed.)

## Workflow

### Before bumping apt dependencies (especially `ros-{distro}-depthai-ros`)

Every build pulls a fresh `ros-{distro}-depthai-ros` via `apt-cache policy … | Candidate`. Before publishing a new revision to `<distro>/edge` (i.e. before merging to `main`):

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
2. [snap/local/apply_defaults.sh](snap/local/apply_defaults.sh): add `set_default_if_unset driver.X <default>`. This script is run by both the install hook (first install) and the post-refresh hook (every snap refresh), so existing instances upgrading to a snap revision that introduced X will get the default automatically — without overwriting other user-set values.
3. [snap/local/launcher.sh](snap/local/launcher.sh): add `X` to `OPTIONS` (if it should be forwarded to `ros2 launch`).
4. [snap/local/depthai.launch.py](snap/local/depthai.launch.py): add `DeclareLaunchArgument("X", default_value=...)` and use it in `launch_setup`.
5. (optional) Document it under `description:` in [snapcraft_template.yaml.jinja2](snapcraft_template.yaml.jinja2) (visible in Snap Store).
6. Build (`just iterate <distro>`), install on the target platform, verify `sudo snap set husarion-depthai driver.X=foo`.

Boolean toggles (e.g. `enable-pointcloud`): use `validate_option "driver.X" VALID_BOOL_OPTIONS[@]` (the `VALID_BOOL_OPTIONS=("true" "false")` definition is already at the bottom of [snap/hooks/configure](snap/hooks/configure)).

### How to add a new YAML preset (`camera-params-foo.yaml`)

1. Add the file to `snap/local/`.
2. Build the snap. The file lands in `$SNAP/usr/share/husarion-depthai/config/`.
3. On a fresh install the install-hook copies it to `${SNAP_DATA}/`. On existing installs the `post-refresh` hook will overwrite at the next `snap refresh`.
4. Note: validation in the configure hook is an inline `[ -f "${SNAP_DATA}/camera-params-${value}.yaml" ]` check — nothing to add, any `<NAME>` passes if the file exists.

Available presets (1280x720 @ 30 fps, low-latency queue=4):

- `camera-params-default.yaml` — RGB-only, raw stream over USB, no IMU/IR. Works for any OAK-x.
- `camera-params-oak-1-lite.yaml` — explicit RGB-only for OAK-1-LITE (single sensor, IMX214).
- `camera-params-oak-d-pro.yaml` — RGBD + IMU + IR projector + flood, USB; stereo with subpixel + lr_check + align_depth.
- `camera-params-oak-d-pro-poe.yaml` — same as above + `i_ip: 10.15.20.6` (PoE; MJPEG quality 50 to fit Ethernet bandwidth; if your IP differs — copy on the host into a new preset).
- `camera-params-oak-d-pro-slam.yaml` — oak-d-pro tuned for SLAM/VIO: manual exposure (no jumps to break feature trackers) + lower ISO. Tune `r_exposure`/`r_iso` per environment (defaults aimed at indoor lit office).
- `camera-params-rgb-h264-720p30.yaml` — **chip-side H.264 encoder only** (`i_low_bandwidth=true`, profile 0 = H264_BASELINE, ~2.5 Mbps, ~1 s GOP). ~98% daemon CPU win vs libx264 but drops raw `/image_raw`, rect, depth, and PCL — streaming-only use. fps/res variants: `rgb-h264-{360p30,360p60,720p60,1080p60,4k30}`. The launcher force-disables rectify/PCL for both raw-less families (`rgb-h264-*` and `depth-disp-h264`). See "Pitfalls → Why default doesn't use the OAK chip's hardware H.264 encoder".
- `camera-params-depth-disp-h264.yaml` — **chip-side H.264 of the 8-bit disparity only** (`i_pipeline_type=RGBD`, `stereo.i_low_bandwidth=true`, `i_subpixel=false`): a lossy 2D depth *view* on `/stereo/image_raw/compressed`, RGB lazy. Needs a stereo OAK. For external depth consumers — the cockpit has no depth viewer.
- `camera-params-rgb-raw-720p-h264-720p.yaml` (+ `-1080p-h264-1080p`, `-360p-h264-360p`, asymmetric `-1080p-h264-720p`, `-360p-h264-720p`) — **dual output: raw `/image_raw` AND on-chip H.264 `/image_raw/compressed` at once**, via the custom `husarion_depthai_pipeline` pluginlib plugin in `ros/` (selected by `camera.i_pipeline_type`, NOT a fork). Filename = format: `rgb-raw-<res>` is the raw leg, `h264-<res>` the encoder leg (independent sizing via on-chip ImageManip). Each suppresses the raw stream's image_transport republishers so `/compressed` carries only the on-chip FFMPEGPacket.
- `camera-params-rgbd-rgb-h264-720p-depth-raw-disp-h264.yaml` — RGB dual + **depth dual** (`RGBDDual`): depth 16UC1 raw + disparity grayscale-H.264 *view* on `/stereo/image_raw{,/compressed}`. Needs a stereo OAK (verified OAK-D-LITE). `stereo.i_subpixel` is forced false in the node (8-bit disparity for the encoder).

### How to add a new DDS transport (XML)

- DDS XMLs live in **snap-common**, not in this repo. Editing requires a PR to `husarion/husarion-snap-common`, then bumping the pin in the Jinja template.
- After adding `rmw/fastdds/foo.xml` the user runs `sudo snap set husarion-depthai ros.transport=fastdds/foo`. `validate_config_param` in `configure_hook_ros.sh` will catch a missing file.

### How to fix a bug

- Find the relevant file (usually `launcher.sh`, `depthai.launch.py`, a `*.yaml` in `snap/local/`, or the Jinja template).
- Reproduce on the target platform (`just iterate jazzy` on ARM64 if the bug is ARM-only).
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
- `${SNAP_DATA}/camera-params-*.yaml`, `ffmpeg-params-*.yaml` — runtime presets (where launcher.sh reads them from).

## What NOT to do

- **Don't commit `snap/snapcraft.yaml`** — it's gitignored and regenerated.
- **Don't remove `restart-condition: always`** without thinking — it protects the daemon from staying down after a crash. (After the move to the uptime check the workaround no longer needs it, but the standalone benefit remains.)
- **Don't add a `driver.X` parameter in just one place** — it'll bite you (validate passes, but the launcher won't forward it, or vice versa).
- **Don't patch snap-common files locally** (`utils.sh`, `configure_hook_ros.sh`, `rmw/<impl>/*.xml`) — the next build overwrites everything. Edit the external repo and bump the pin.
- **Don't use `extensions:` without `SNAPCRAFT_ENABLE_EXPERIMENTAL_EXTENSIONS=1`** — `just`/CI set it for you. Running `snapcraft` in a terminal manually will fail.
- **Don't change the naming format of `camera-params-<NAME>.yaml` / `ffmpeg-params-<NAME>.yaml` (and `rmw/<impl>/<NAME>.xml`)** — all regexes in `validate_config_param` assume it.
- **Don't commit `husarion-depthai*.snap`** or `squashfs-root/` (they're in `.gitignore` but can grow to ~600 MB on disk — remember `just clean` / `just remove`).
- **Don't push directly to `main`** — use PRs (CI publishes on push to main, so casual commits go straight to `<distro>/edge` in the Snap Store).
