#!/bin/sh -e

log() {
    local message="$1"
    # Log the message with logger
    logger -t "${SNAP_NAME}" "stop: $message"
}

log "Start ${SNAP_NAME}.daemon service"
snapctl start --enable ${SNAP_NAME}.daemon 2>&1 || true

