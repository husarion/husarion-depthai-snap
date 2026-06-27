# husarion-depthai snap — operator guide

On-device usage reference for the **installed** `husarion-depthai` snap, written for AI assistants and operators working on the robot. It is regenerated from the snap on every install/refresh — do not edit it (changes are overwritten).

## What this is

ROS 2 driver for Luxonis OAK-x depth cameras on Husarion robots. Publishes RGB (and, when enabled, depth + pointcloud) under `<namespace>/<name>/...` — default node name `oak`, so e.g. `/rosbot/oak/rgb/image_raw`.

## First run

```bash
sudo /var/snap/husarion-depthai/common/post_install.sh       # one-time setup
sudo snap set husarion-depthai driver.camera-model=<model>   # e.g. OAK-D-PRO
sudo husarion-depthai.start
```

## Configuration — two ways

1. **Husarion cockpit (recommended in a cockpit install).** The ROS network (RMW, `ROS_DOMAIN_ID`, namespace) is set on the chain **owner** — the `rosbot` snap or the cockpit host — from the WebUI **Manage** tab, and propagates here automatically. Do **not** hand-set `ros.*` here in a cockpit install. The operator camera/streaming UI ingests this snap's image topic.
2. **Direct / standalone:** `sudo snap set husarion-depthai <key>=<value>`:
   - `driver.name` — node name + topic prefix.
   - `driver.camera-model` — e.g. `OAK-D`, `OAK-D-PRO`, `OAK-1` (hardware is auto-detected; this is for validation).
   - `driver.camera-params` — preset; **`default` has NO depth**, use e.g. `oak-d-pro` for an RGBD pipeline.
   - `driver.enable-pointcloud`, `driver.rectify-rgb`, `driver.startup-delay`.
   - `ros.namespace`, `ros.domain-id`, `ros.transport`.

## ROS network / RMW config locations

- Effective ROS env: `/var/snap/husarion-depthai/common/ros.env`.
- RMW profile files: `/var/snap/husarion-depthai/common/rmw/`, and the chain config under `/var/snap/husarion-depthai/common/husarion-agent/config/`.
- `ros.transport` selects FastDDS / CycloneDDS profiles only. **Zenoh reaches this snap through the agent chain** from the owner, not via `ros.transport`.

## Camera pipeline config locations (camera-params)

The depthai_ros_driver pipeline YAMLs are snap-shipped to **`/var/snap/husarion-depthai/current/`** (read-only, reset on refresh; the durable source is this repo at `snap/local/`). `driver.camera-params=<preset>` selects one:

- `camera-params-<preset>.yaml` — RGB/stereo pipeline: resolution, fps, on-chip encoder, IMU/IR/NN, low-bandwidth. Presets shipped: `default`, `oak-1-lite`, `oak-d-pro`, `oak-d-pro-poe`, `oak-d-pro-slam`, the `rgb-h264-*` family, `depth-disp-h264` (chip H.264 of the disparity view), and the **`rgb-raw-*` family + `rgbd-rgb-h264-720p-depth-raw-disp-h264`** (custom-plugin dual output — see below).
- `ffmpeg-params-default.yaml` — HOST-side `image_transport` encoder for the `.../image_raw/ffmpeg` topic (libx264, software, on the host CPU).

Encoder paths for the RGB feed — **three archetypes** (matters for streaming fps / CPU):

- **raw + host re-encode** (`default`, `oak-1-lite`, `oak-d-pro*`): `i_low_bandwidth: false` → publishes RAW bgr8 on `.../oak/rgb/image_raw` (~2.7 MB/frame @720p). H.264 only exists if something re-encodes on the **host** (libx264 → the `.../ffmpeg` topic). Heavy — a downstream re-encoder (e.g. the cockpit camera node) is **ROS-transport-bound** on the ~83 MB/s raw firehose (DDS fragment reassembly saturates a core → ~3–11 fps). The codec is NOT the bottleneck; the raw transport is.
- **chip-encoded only** (`rgb-h264-*`): `i_low_bandwidth: true` → the OAK chip's **hardware** VideoEncoder emits H.264 on `.../oak/rgb/image_raw/compressed` (a `ffmpeg_image_transport_msgs/FFMPEGPacket`); the **raw topic is dropped** and there is no host encode. Pair with `driver.rectify-rgb=false` + `driver.enable-pointcloud=false`. **Verified on x86 (2026-06-19):** daemon CPU steady at ~4.7% (vs ~127% for host libx264).
- **dual output — raw AND chip H.264 at once** (`rgb-raw-*`): the stock driver can't do this (low_bandwidth is either/or). Custom plugin — see the dedicated section below.

### Dual-output presets + the custom pipeline plugin (`rgb-raw-*`)

`rgb-raw-*` presets publish **raw `.../oak/rgb/image_raw` AND on-chip H.264 `.../oak/rgb/image_raw/compressed` simultaneously** from one camera — autonomy (raw) + telepresence (chip H.264) at the same time.

- **CUSTOM pluginlib plugin, not stock depthai, NOT a fork:** `husarion_depthai_pipeline` (class `husarion_depthai::pipeline_gen::RGBDual`), an external ament package built into this snap (durable source: the snap repo `ros/husarion_depthai_pipeline/`), registered via pluginlib so the stock `depthai_ros_driver` **autoloads it on demand** when a preset sets `camera.i_pipeline_type: 'husarion_depthai::pipeline_gen::RGBDual'`. Presets without that key never load it. **If the snap is rebuilt/refreshed WITHOUT the plugin part (`husarion-depthai-pipeline` in snapcraft), these presets fail to load.**
- **Under the hood:** taps one `ColorCamera.video` NV12 output to both a raw XLinkOut (→ `image_raw`) and the OAK hardware VideoEncoder (→ `image_raw/compressed`). One dai output fanned to two consumers. No host encode, no `/dev/dri`, no new snap interface.
- **Verified live on an OAK-1 (USB3 SUPER, 2026-06-22):** raw 1280×720 + on-chip H.264 both at **30 fps**, daemon **~12.7% CPU** — and that 12.7% is the host **NV12→bgr8** conversion for the raw topic (the encode is on-chip ≈free); contrast the raw-then-host-reencode path at ~98% of a core (transport-bound, above).
- **Unlike `rgb-h264-*`, the FFMPEGPacket `encoding` token is the truthful `h264`** (the plugin sets it), not the stale `libx264` the stock low-bandwidth path reports. Provenance is decided by the `/compressed` suffix regardless, so both badge `sensor`.
- **Presets:** `rgb-raw-720p-h264-720p`, `rgb-raw-1080p-h264-1080p`, `rgb-raw-360p-h264-360p`, and asymmetric `rgb-raw-1080p-h264-720p` / `rgb-raw-360p-h264-720p` (raw and encoder sized **independently** via an on-chip `ImageManip` resize — `rgb.i_raw_*` / `rgb.i_enc_*`). `rgbd-rgb-h264-720p-depth-raw-disp-h264` (class `RGBDDual` + `DepthDual` node) adds the depth path — RGB raw+H.264 PLUS depth 16UC1 raw (`…/stereo/image_raw`, metric) + disparity grayscale-H.264 view (`…/stereo/image_raw/compressed`, a 2D depth preview). **Verified on an OAK-D-LITE (2026-06-22)**; needs a stereo OAK-D-class camera. The disparity-view encoder needs **8-bit** input, so the preset MUST set `stereo.i_subpixel=false` (subpixel → 16-bit disparity → the VideoEncoder rejects it; the metric depth raw stays 16-bit regardless).
- **Each dual preset suppresses the raw stream's image_transport republishers** (`oak.rgb.image_raw.enable_pub_plugins: ['image_transport/raw']`). Without it the raw publisher's lazy CompressedImage republisher claims the **same** `.../image_raw/compressed` name as the on-chip FFMPEGPacket → two types on one topic, and `ros2 topic hz` / consumers subscribe to the wrong (empty) one. With it, `/compressed` carries only the on-chip H.264.
- **Cockpit consumption (verified, zero cockpit change):** the camera node auto-follows the OAK base topic and prefers `.../compressed` (passthrough, ~0 host CPU, badge `sensor-h264`) while autonomy reads the raw — exactly like `rgb-h264-720p30`. All five rgb-raw-\* presets decode in the cockpit at full fps with the `sensor` badge.

> **Two gotchas when verifying chip vs host encode** (both bit a reviewer in 2026-06):
>
> - The `FFMPEGPacket.encoding` token reads **`libx264` even on the chip path** — it is dead metadata (`i_low_bandwidth_ffmpeg_encoder` is declared but never read back; see CLAUDE.md "Pitfalls"). Do **not** read it as "host re-encode". Use **daemon CPU** as the signal: ~few % = chip, ~100%+ = host libx264.
> - `ros2 topic list` may still show `.../oak/rgb/image_raw` even though the chip path drops it — a *subscriber* (e.g. the cockpit camera node holding its previous source) keeps the topic name alive. Check `ros2 topic info` **Publisher count** (0 under `rgb-h264-720p30`), not mere presence.

## Chaining (how it gets its network config)

Embeds `husarion-agent` as a chain **follower (leaf)**. Installed next to a `rosbot` snap (or a cockpit host) it auto-joins over the `agent-chain` content interface and inherits the owner's network config — nothing to configure. Set the network on the **owner**; it cascades here.

**Custom RMW profiles** are added on the **owner** (the rosbot snap, no cockpit needed): drop the XML in rosbot's `husarion-agent/config/rmw/<impl>/` and `snap set rosbot ros.transport=…` — the file and selection propagate here in ~0.5 s. Don't add a profile or set `ros.transport` here; the owner reconciles a local change away.

## Apps

`husarion-depthai.start` · `husarion-depthai.stop` · `husarion-depthai.restart`.

## Gotchas

- Big frames (1280x720 RGB ≈ 2.7 MB) fragment over UDP; a busy subscriber needs a large DDS receive buffer. A Husarion cockpit install raises the host `net.core.rmem_max` for this — on a bare standalone host, raise it yourself if a subscriber drops frames.
- Depth is off unless `driver.camera-params` selects an RGBD preset.

## Logs

```bash
sudo snap logs husarion-depthai -f
```
