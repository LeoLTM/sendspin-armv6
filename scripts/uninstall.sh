#!/bin/bash
# Uninstall sendspin-armv6 and remove the daemon completely.
# Usage: sudo bash uninstall.sh
set -euo pipefail

BINARY=/usr/local/bin/sendspin-armv6
CONFIG_FILE=/etc/sendspin-armv6.conf
SERVICE_FILE=/etc/systemd/system/sendspin-armv6.service
SERVICE=sendspin-armv6

if [[ $EUID -ne 0 ]]; then
    echo "Error: run as root (e.g. sudo bash uninstall.sh)" >&2
    exit 1
fi

# Prompt to keep or remove the config file.
KEEP_CONFIG=true
if [[ -f "$CONFIG_FILE" ]]; then
    echo -n "Keep config file at $CONFIG_FILE? [Y/n] "
    read -r REPLY </dev/tty
    if [[ "$REPLY" =~ ^[Nn]$ ]]; then
        KEEP_CONFIG=false
    fi
fi

echo "==> Stopping and disabling service..."
systemctl stop "$SERVICE" 2>/dev/null || true
systemctl disable "$SERVICE" 2>/dev/null || true

echo "==> Removing service file..."
rm -f "$SERVICE_FILE"
systemctl daemon-reload
systemctl reset-failed "$SERVICE" 2>/dev/null || true

echo "==> Removing binary..."
rm -f "$BINARY"

if [[ "$KEEP_CONFIG" == false ]]; then
    echo "==> Removing config..."
    rm -f "$CONFIG_FILE"
else
    echo "==> Config kept at $CONFIG_FILE"
fi

echo ""
echo "Uninstall complete."
