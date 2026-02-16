if [ $1 -eq 1 ]; then
    # Fresh install — read config for mount dir and user
    CONF="/etc/sacd-vfs/sacd-vfs.conf"
    MOUNT_DIR=/mnt/sacd
    RUN_USER=root
    [ -f "$CONF" ] && . "$CONF" 2>/dev/null || true
    mkdir -p "$MOUNT_DIR"
    chown "$RUN_USER" "$MOUNT_DIR"

    # Write systemd override for User/Group
    mkdir -p /etc/systemd/system/sacd-vfs.service.d
    cat > /etc/systemd/system/sacd-vfs.service.d/user.conf <<OVERRIDE
[Service]
User=$RUN_USER
Group=$RUN_USER
OVERRIDE

    systemctl daemon-reload
    systemctl enable sacd-vfs.service
    echo ""
    echo "============================================"
    echo "  sacd-vfs installed successfully"
    echo "============================================"
    echo ""
    echo "  Configure: /etc/sacd-vfs/sacd-vfs.conf"
    echo "  Then run:  systemctl start sacd-vfs"
    echo ""
fi
if [ $1 -ge 2 ]; then
    # Upgrade
    systemctl daemon-reload
    systemctl try-restart sacd-vfs.service 2>/dev/null || true
fi
