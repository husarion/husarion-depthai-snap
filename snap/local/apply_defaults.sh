#!/bin/bash -e

# Idempotent: only sets a default if the param is currently unset (or empty
# string). Called by install (first install) and post-refresh (every snap
# refresh) so that new parameters introduced in a later snap revision get their
# defaults applied without clobbering user-modified values from earlier ones.

source "$SNAP/usr/bin/utils.sh"

set_default_if_unset() {
    local key="$1"
    local value="$2"
    local current
    current="$(snapctl get "$key" 2>/dev/null || true)"
    if [ -z "$current" ] || [ "$current" = "''" ]; then
        snapctl set "${key}=${value}"
        log "Applied default ${key}=${value} (was unset)"
    fi
}

set_default_if_unset driver.name oak
set_default_if_unset driver.camera-model OAK-D-PRO
set_default_if_unset driver.camera-params default
set_default_if_unset driver.ffmpeg-params default
set_default_if_unset driver.startup-delay 30
set_default_if_unset driver.enable-pointcloud false
set_default_if_unset driver.rectify-rgb true
