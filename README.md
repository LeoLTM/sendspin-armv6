# sendspin-armv6

Headless [Sendspin](https://github.com/Sendspin/sendspin-cpp) audio client for ARMv6 devices like the **Raspberry Pi Zero W**.

Plays music through the 3.5mm audio jack. No display, no controls — just audio output as a systemd daemon.

## How it works

- Uses [sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) for the Sendspin protocol, audio decoding, and time synchronization
- Pipes decoded PCM to `aplay` (ALSA) — no PortAudio dependency
- Cross-compiled for ARMv6 in GitHub Actions — no local toolchain needed

## Installation

### 1. Download the binary

Grab the latest release from the [Releases](../../releases) page:

```bash
wget https://github.com/<owner>/sendspin-armv6/releases/latest/download/sendspin-armv6-linux-armv6.tar.gz
mkdir sendspin-armv6 && tar -xzf sendspin-armv6-linux-armv6.tar.gz -C sendspin-armv6
```

### 2. Install the binary

```bash
sudo cp sendspin-armv6/sendspin-armv6 /usr/local/bin/
sudo chmod +x /usr/local/bin/sendspin-armv6
```

### 3. Configure

```bash
sudo cp sendspin-armv6/sendspin-armv6.conf /etc/sendspin-armv6.conf
sudo nano /etc/sendspin-armv6.conf
```

Set `server_url` to your Sendspin server's WebSocket URL:

```ini
server_url = ws://192.168.1.10:8928/sendspin
name = Living Room Pi
log_level = info
```

### 4. Set up the systemd daemon

```bash
sudo cp sendspin-armv6/sendspin-armv6.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now sendspin-armv6
```

### 5. Verify

```bash
sudo systemctl status sendspin-armv6
journalctl -u sendspin-armv6 -f
```

## Audio output

The Pi Zero W outputs audio through the built-in 3.5mm jack by default. Make sure analog audio is enabled:

```bash
# Force audio through 3.5mm jack (not HDMI)
sudo raspi-config nonint do_audio 1

# Verify aplay is available
aplay -l
```

## Configuration reference

| Key | Required | Default | Description |
|---|---|---|---|
| `server_url` | **yes** | — | WebSocket URL of the Sendspin server |
| `name` | no | `sendspin-armv6` | Friendly name shown in the Sendspin UI |
| `log_level` | no | `info` | `none`, `error`, `warn`, `info`, `debug`, `verbose` |

## Building from source

Building happens in GitHub Actions (cross-compilation for ARMv6). Push to `main` or create a tag to trigger a build.

To build locally (requires the ARM cross-compiler):

```bash
sudo apt install gcc-arm-linux-gnueabihf g++-arm-linux-gnueabihf cmake ninja-build

cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-armv6.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -G Ninja

cmake --build build
```

The binary will be at `build/sendspin-armv6`.

## License

Apache-2.0
