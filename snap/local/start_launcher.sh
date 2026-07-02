#!/bin/bash -e

source $SNAP/usr/bin/utils.sh

if [ "$(id -u)" -ne 0 ]; then
    log_and_echo "Error: This script must be run as root."
    exit 1
fi

for plug in raw-usb hardware-observe; do
  if ! snapctl is-connected "$plug"; then
    log_and_echo "Error: snap plug '$plug' is not connected. Please run:"
    log_and_echo "    \033[1msudo ${SNAP_COMMON}/post_install.sh\033[0m"
    exit 1
  fi
done

MODEL="$(snapctl get driver.model)"
if [ -z "$MODEL" ]; then
  log_and_echo "Error: camera model is not set. Pick your model first:"
  log_and_echo "    \033[1msudo snap set ${SNAP_NAME} driver.model=<OAK-1|OAK-1-LITE|OAK-D|OAK-D-LITE|OAK-D-PRO|OAK-D-PRO-W>\033[0m"
  exit 1
fi

log_and_echo "Starting camera model: \033[1m$MODEL\033[0m"

snapctl start --enable ${SNAP_NAME}.daemon 2>&1 || true
