#!/bin/sh -e

log() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "stop: $message"
}

log "Stop ${SNAP_NAME}.daemon service"
snapctl stop --disable ${SNAP_NAME}.daemon 2>&1 || true
