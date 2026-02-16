if [ $1 -eq 0 ]; then
    # Full removal (not upgrade)
    systemctl stop sacd-vfs.service 2>/dev/null || true
    systemctl disable sacd-vfs.service 2>/dev/null || true

    # Unmount if still mounted
    if [ -f /etc/sacd-vfs/sacd-vfs.conf ]; then
        . /etc/sacd-vfs/sacd-vfs.conf 2>/dev/null || true
        if [ -n "$MOUNT_DIR" ] && mountpoint -q "$MOUNT_DIR" 2>/dev/null; then
            fusermount3 -uz "$MOUNT_DIR" 2>/dev/null || true
        fi
    fi
fi
