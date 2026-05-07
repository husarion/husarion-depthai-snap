# husarion-depthai-snap

<p align="center">
  <img src="snap/gui/icon.png" width="150px">
</p>

Snap for OAK-x cameras customized for Husarion robots.

[![Get it from the Snap Store](https://snapcraft.io/static/images/badges/en/snap-store-black.svg)](https://snapcraft.io/husarion-depthai)

## Apps

| app | description |
| - | - |
| `husarion-depthai.start` | Enable + start the `husarion-depthai.daemon` service |
| `husarion-depthai.stop` | Disable + stop the daemon |
| `husarion-depthai.restart` | Restart the daemon |
| `husarion-depthai` | Run in the foreground (stop the daemon first) |

## Configuration

Two parameter namespaces — `driver.*` (DepthAI) and `ros.*` (DDS / namespace / domain id):

```bash
# DepthAI
sudo snap set husarion-depthai driver.camera-model=OAK-D-PRO
sudo snap set husarion-depthai driver.camera-params=oak-d-pro
sudo snap set husarion-depthai driver.ffmpeg-params=default
sudo snap set husarion-depthai driver.enable-pointcloud=true   # PointCloudXyzrgbNode (RGBD presets only)
sudo snap set husarion-depthai driver.startup-delay=30         # seconds to sleep on fresh boot (USB cold-start workaround)

# ROS / DDS
sudo snap set husarion-depthai ros.namespace=robot
sudo snap set husarion-depthai ros.domain-id=0
sudo snap set husarion-depthai ros.transport=udp-lo            # or: udp, shm, builtin, rmw_cyclonedds_cpp
```

Dump current settings: `snap get -d husarion-depthai`

## Camera presets

Bundled preset YAMLs live in `/var/snap/husarion-depthai/current/`:

| preset | hardware | pipeline | notes |
| - | - | - | - |
| `default` | any OAK-x | RGB | 1280×720 @ 30 fps, raw RGB over USB |
| `oak-1-lite` | OAK-1-LITE | RGB | single-sensor IMX214 |
| `oak-d-pro` | OAK-D-PRO | RGBD + IMU + IR | subpixel + lr_check + align_depth |
| `oak-d-pro-poe` | OAK-D-PRO-POE | RGBD + IMU + IR | MJPEG quality 50, default IP `10.15.20.6` |
| `oak-d-pro-slam` | OAK-D-PRO | RGBD + IMU + IR | manual exposure for VIO/SLAM feature trackers |

Add your own preset:

```bash
sudo cp /var/snap/husarion-depthai/current/camera-params-default.yaml \
        /var/snap/husarion-depthai/current/camera-params-myconfig.yaml
sudo vim /var/snap/husarion-depthai/current/camera-params-myconfig.yaml
sudo snap set husarion-depthai driver.camera-params=myconfig
```

Bundled presets are overwritten on every `snap refresh`. User-added files (any name not in the bundled list) are preserved.

## FFmpeg image transport

The driver publishes both raw (`/<ns>/<name>/rgb/image_raw`) and ffmpeg-encoded (`/<ns>/<name>/rgb/image_raw/ffmpeg`) variants. Encoder defaults are in `/var/snap/husarion-depthai/current/ffmpeg-params-default.yaml`:

```yaml
ffmpeg_image_transport:
  encoding: libx264
  preset: ultrafast
  tune: zerolatency
```

Available encoders: `ffmpeg -encoders`. Available presets per encoder: `ffmpeg -h encoder=libx264`.

To use a custom config:

```bash
sudo cp /var/snap/husarion-depthai/current/ffmpeg-params-default.yaml \
        /var/snap/husarion-depthai/current/ffmpeg-params-myconfig.yaml
sudo vim /var/snap/husarion-depthai/current/ffmpeg-params-myconfig.yaml
sudo snap set husarion-depthai driver.ffmpeg-params=myconfig
```

## Logs and debugging

```bash
journalctl -t husarion-depthai -f              # tag = SNAP_NAME
snap logs husarion-depthai.daemon -f
snap services husarion-depthai
snap connections husarion-depthai              # raw-usb, hardware-observe, shm-plug↔shm-slot must be connected
cat /var/snap/husarion-depthai/common/ros.env  # exact env exported by the configure hook
```

`DEPTHAI_DEBUG=1` enables `log_level=debug` in the launch file.

## Architecture and contributing

See [ARCHITECTURE.md](ARCHITECTURE.md) for the snap layout and [CLAUDE.md](CLAUDE.md) for the working rules (template re-render, parameter wiring, pitfalls).
