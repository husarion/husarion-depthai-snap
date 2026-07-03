#!/bin/bash -e

source "$SNAP/usr/bin/utils.sh"

# Model gate first (rosbot's driver.model pattern). The value itself is
# validation/cockpit metadata — the driver autodetects the physical device —
# but the operator must declare it before the driver starts.
CAMERA_MODEL="$(snapctl get driver.model)"
if [ -z "$CAMERA_MODEL" ]; then
  log_and_echo "Error: camera model is not set. Pick your model first:"
  log_and_echo "    \033[1msudo snap set ${SNAP_NAME} driver.model=<OAK-1|OAK-1-LITE|OAK-D|OAK-D-LITE|OAK-D-PRO|OAK-D-PRO-W>\033[0m"
  exit 1
fi

# post_install gate. Unlike rosbot there is no marker file: post_install.sh
# only connects plugs (no udev/apparmor steps), so is-connected IS the real
# signal — and store installs auto-connect these plugs, so fleets that never
# ran the script keep starting after a refresh.
for plug in raw-usb hardware-observe; do
  if ! snapctl is-connected "$plug"; then
    log_and_echo "Error: snap plug '$plug' is not connected. Please run:"
    log_and_echo "    \033[1msudo ${SNAP_COMMON}/post_install.sh\033[0m"
    exit 1
  fi
done

# Shared memory needs shm-plug↔shm-slot, which the store never auto-connects.
# Hard-fail on explicit shm tokens; for any other transport warn only — a
# custom RMW profile may still use SHM internally (and builtin FastDDS tries
# SHM by default), but hard-failing would take down refreshed fleets that
# never ran post_install.sh.
if ! snapctl is-connected shm-plug; then
  ROS_TRANSPORT="$(snapctl get ros.transport)"
  case "$ROS_TRANSPORT" in
  *shm*)
    log_and_echo "Error: ros.transport='${ROS_TRANSPORT}' needs the shared-memory connection. Please run:"
    log_and_echo "    \033[1msudo ${SNAP_COMMON}/post_install.sh\033[0m"
    exit 1
    ;;
  *)
    log_and_echo "Warning: shm-plug is not connected — if your RMW profile uses shared memory, it will fail. Please run:"
    log_and_echo "    \033[1msudo ${SNAP_COMMON}/post_install.sh\033[0m"
    ;;
  esac
fi

# Iterate over the snap parameters and retrieve their value.
# If a value is set, it is forwarded to the launch file.

OPTIONS=(
  name
  enable-pointcloud
  rectify-rgb
)

LAUNCH_OPTIONS=()

CAMERA_PARAMS="$(snapctl get driver.camera-params)"
log_and_echo "Starting OAK camera (model: ${CAMERA_MODEL}, camera-params: ${CAMERA_PARAMS})"

# Chip-encoded H.264 presets that publish NO raw RGB (i_low_bandwidth=true) leave
# the rectify + point-cloud nodes with no input. Force both off regardless of snap
# config so selecting one from the cockpit Manage tab can never produce a half-broken
# pipeline. Matches the raw-less families: `rgb-h264-*` (chip-only RGB, all res/fps
# variants) and `depth-disp-h264` (disparity-view stream). NOT the dual `rgb-raw-*`
# presets — those DO publish raw, so rectify/PCL are left to the operator's config.
case "${CAMERA_PARAMS}" in
  rgb-h264-* | depth-disp-h264) NO_RAW_RGB=true ;;
  *)                            NO_RAW_RGB=false ;;
esac
for OPTION in "${OPTIONS[@]}"; do
  VALUE="$(snapctl get "driver.${OPTION}")"
  if [ "${NO_RAW_RGB}" = "true" ]; then
    case "${OPTION}" in
      rectify-rgb | enable-pointcloud) VALUE="false" ;;
    esac
  fi
  if [ -n "${VALUE}" ]; then
    OPTION_WITH_UNDERSCORE="${OPTION//-/_}"
    LAUNCH_OPTIONS+=("${OPTION_WITH_UNDERSCORE}:=${VALUE}")
  fi
done

# Check if ros.namespace is set and not empty
ROS_NAMESPACE="$(snapctl get ros.namespace)"
if [ -n "${ROS_NAMESPACE}" ]; then
    LAUNCH_OPTIONS+=("namespace:=${ROS_NAMESPACE}")
fi

LAUNCH_OPTIONS+=("params_file:=${SNAP_DATA}/camera-params-${CAMERA_PARAMS}.yaml")

FFMPEG_PARAMS="$(snapctl get driver.ffmpeg-params)"
LAUNCH_OPTIONS+=("ffmpeg_params_file:=${SNAP_DATA}/ffmpeg-params-${FFMPEG_PARAMS}.yaml")

# watch the log with: "journalctl -t husarion-depthai"
log_and_echo "Running with options: ${LAUNCH_OPTIONS[*]}"

# Workaround for first-startup-after-reboot race in depthai-ros: the camera
# sometimes fails to come up if the driver starts within seconds of system boot.
# We delay only on a "fresh boot" (low /proc/uptime), so subsequent daemon
# restarts don't pay the cost and don't risk a retry loop on a broken launch.
#
# TODO: verify against upstream on every depthai-ros bump:
#   https://github.com/luxonis/depthai-ros/releases — check if first-start race is fixed.
#   Smoke test: snap set husarion-depthai driver.startup-delay=0; reboot; expect topics within ~5s.
STARTUP_DELAY="$(snapctl get driver.startup-delay)"
if [ "${STARTUP_DELAY:-0}" -gt 0 ]; then
  UPTIME=$(awk '{print int($1)}' /proc/uptime)
  if [ "${UPTIME}" -lt $((STARTUP_DELAY + 60)) ]; then
    echo "[$(date +"%T")] fresh boot (uptime=${UPTIME}s) — sleeping ${STARTUP_DELAY}s"
    sleep "${STARTUP_DELAY}"
    echo "[$(date +"%T")] done"
  fi
fi

ros2 launch "$SNAP/usr/bin/depthai.launch.py" "${LAUNCH_OPTIONS[@]}"
