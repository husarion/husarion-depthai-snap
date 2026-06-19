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

- `camera-params-<preset>.yaml` — RGB/stereo pipeline: resolution, fps, on-chip encoder, IMU/IR/NN, low-bandwidth. Presets shipped: `default`, `oak-1-lite`, `oak-d-pro`, `oak-d-pro-poe`, `oak-d-pro-slam`, `streaming-h264`.
- `ffmpeg-params-default.yaml` — HOST-side `image_transport` encoder for the `.../image_raw/ffmpeg` topic (libx264, software, on the host CPU).

Encoder paths for the RGB feed (matters for streaming fps / CPU):

- `default` → publishes RAW bgr8 1280x720 on `.../oak/rgb/image_raw` (~2.7 MB/frame); any H.264 is a **host** re-encode (libx264). Heavy — a downstream re-encoder (e.g. the cockpit camera node) is CPU-bound here (~11 fps).
- `streaming-h264` → `i_low_bandwidth: true` → the OAK chip's **hardware** VideoEncoder emits H.264 on `.../oak/rgb/image_raw/compressed` (a `ffmpeg_image_transport_msgs/FFMPEGPacket`); the raw topic is dropped and there is no host encode. Pair with `driver.rectify-rgb=false` + `driver.enable-pointcloud=false`.

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
