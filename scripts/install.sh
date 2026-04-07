#!/bin/bash
# Install sendspin-armv6 from the latest release.
# Usage: curl -fsSL .../install.sh | sudo bash
# An example config is written to /etc/sendspin-armv6.conf if one does not
# already exist. Edit it before starting the service.
set -euo pipefail

BINARY=/usr/local/bin/sendspin-armv6
CONFIG_FILE=/etc/sendspin-armv6.conf
SERVICE_FILE=/etc/systemd/system/sendspin-armv6.service
SERVICE=sendspin-armv6
REPO="LeoLTM/sendspin-armv6"
ARCHIVE=sendspin-armv6-linux-armv6-release.tar.gz

if [[ $EUID -ne 0 ]]; then
    echo "Error: run as root (e.g. sudo bash install.sh)" >&2
    exit 1
fi

if [[ -f "$BINARY" ]]; then
    echo "Error: $BINARY already exists. Use upgrade.sh to update an existing installation." >&2
    exit 1
fi

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

echo "==> Downloading latest release..."
curl -fsSL "https://github.com/$REPO/releases/latest/download/$ARCHIVE" \
    -o "$TMP/$ARCHIVE"

echo "==> Extracting..."
tar -xzf "$TMP/$ARCHIVE" -C "$TMP"

echo "==> Installing binary..."
install -m 755 "$TMP/sendspin-armv6" "$BINARY"

echo "==> Installing service file..."
install -m 644 "$TMP/sendspin-armv6.service" "$SERVICE_FILE"
systemctl daemon-reload

if [[ -f "$CONFIG_FILE" ]]; then
    echo "==> Config already exists at $CONFIG_FILE — leaving it untouched."
else
    echo "==> Installing default config..."
    install -m 644 "$TMP/sendspin-armv6.conf" "$CONFIG_FILE"
    echo ""
    echo "IMPORTANT: Edit $CONFIG_FILE before starting the service."
    echo "  At minimum, set server_url to your Sendspin server address."
fi

echo "==> Enabling service..."
systemctl enable "$SERVICE"

echo ""
echo "Installation complete. Start the service with:"
echo "  sudo systemctl start $SERVICE"
echo ""
echo "Check status with:"
echo "  sudo systemctl status $SERVICE"
echo "  journalctl -u $SERVICE -f"
