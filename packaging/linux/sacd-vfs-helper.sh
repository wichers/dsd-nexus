#!/bin/sh
# sacd-vfs-helper.sh - Build and exec sacd-vfs from config
# Sourced by systemd via EnvironmentFile, executed by ExecStart.

set -e

CONF="/etc/sacd-vfs/sacd-vfs.conf"
if [ -f "$CONF" ]; then
    . "$CONF"
fi

# Validate required settings
if [ -z "$SOURCE_DIR" ] || [ -z "$MOUNT_DIR" ]; then
    echo "Error: SOURCE_DIR and MOUNT_DIR must be set in $CONF" >&2
    exit 1
fi

if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: Source directory does not exist: $SOURCE_DIR" >&2
    exit 1
fi

# Build option string
OPTS=""

if [ "${STEREO:-yes}" = "no" ]; then
    OPTS="$OPTS -o no_stereo"
fi

if [ "${MULTICHANNEL:-yes}" = "no" ]; then
    OPTS="$OPTS -o no_multichannel"
fi

if [ -n "$THREADS" ] && [ "$THREADS" != "0" ]; then
    OPTS="$OPTS -o threads=$THREADS"
fi

if [ -n "$CACHE_TIMEOUT" ] && [ "$CACHE_TIMEOUT" != "0" ]; then
    OPTS="$OPTS -o cache_timeout=$CACHE_TIMEOUT"
fi

if [ -n "$MAX_ISOS" ] && [ "$MAX_ISOS" != "0" ]; then
    OPTS="$OPTS -o max_isos=$MAX_ISOS"
fi

V="${VERBOSE:-0}"
i=0
while [ "$i" -lt "$V" ]; do
    OPTS="$OPTS -v"
    i=$((i + 1))
done

if [ -n "$EXTRA_OPTS" ]; then
    OPTS="$OPTS $EXTRA_OPTS"
fi

# Run in foreground (systemd manages the process)
exec /usr/bin/sacd-vfs -f $OPTS "$SOURCE_DIR" "$MOUNT_DIR"
