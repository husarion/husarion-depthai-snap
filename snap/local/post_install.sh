#!/usr/bin/env bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root"
   exit 1
fi

# Run once on EVERY install: raw-usb/hardware-observe auto-connect on store
# installs but not on sideloads, and shm-plug↔shm-slot (needed for
# ros.transport=shm) is never auto-connected. No restart here — the daemon
# ships disabled and is started by `husarion-depthai.start` once driver.model
# is set.
sudo snap connect husarion-depthai:raw-usb
sudo snap connect husarion-depthai:hardware-observe
sudo snap connect husarion-depthai:shm-plug husarion-depthai:shm-slot
