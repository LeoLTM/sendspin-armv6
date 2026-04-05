#!/bin/bash
# Upgrade sendspin-armv6 to the latest release.
# Usage: curl -fsSL .../upgrade.sh | sudo bash
# Your config at /etc/sendspin-armv6.conf is never modified.
set -euo pipefail

BINARY=/usr/local/bin/sendspin-armv6
SERVICE_FILE=/etc/systemd/system/sendspin-armv6.service
SERVICE=sendspin-armv6
REPO="LeoLTM/sendspin-armv6"
ARCHIVE=sendspin-armv6-linux-armv6.tar.gz

if [[ $EUID -ne 0 ]]; then
    echo "Error: run as root (e.g. sudo bash upgrade.sh)" >&2
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "==> Downloading latest release..."
curl -fsSL "https://github.com/$REPO/releases/latest/download/$ARCHIVE" \
    -o "$TMP/$ARCHIVE"

echo "==> Extracting..."
tar -xzf "$TMP/$ARCHIVE" -C "$TMP"

echo "==> Stopping service..."
systemctl stop "$SERVICE" 2>/dev/null || true

echo "==> Installing binary..."
install -m 755 "$TMP/sendspin-armv6" "$BINARY"

echo "==> Installing service file..."
install -m 644 "$TMP/sendspin-armv6.service" "$SERVICE_FILE"
systemctl daemon-reload

echo "==> Starting service..."
systemctl reset-failed "$SERVICE" 2>/dev/null || true
systemctl start "$SERVICE"

echo ""
echo "Done. Check status with:"
echo "  sudo systemctl status $SERVICE"
echo "  journalctl -u $SERVICE -f"
