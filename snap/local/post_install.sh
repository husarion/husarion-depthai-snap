#!/usr/bin/env bash

if [[ $EUID -ne 0 ]]; then
   echo "This script must be run as root" 
   exit 1
fi

sudo snap connect husarion-depthai:raw-usb
sudo snap connect husarion-depthai:hardware-observe
sudo snap connect husarion-depthai:shm-plug husarion-depthai:shm-slot

sudo husarion-depthai.restart
