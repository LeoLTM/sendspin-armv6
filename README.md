# sendspin-armv6

Headless [Sendspin](https://github.com/Sendspin/sendspin-cpp) audio client for ARMv6 devices like the **Raspberry Pi Zero W**.

Plays music through available ALSA devices. No display, no controls, just audio output as a systemd daemon.

## How it works

- Uses [sendspin-cpp](https://github.com/Sendspin/sendspin-cpp) for the Sendspin protocol, audio decoding, and time synchronization
- Pipes decoded PCM to `aplay` (ALSA), no PortAudio dependency
- Cross-compiled for ARMv6 in GitHub Actions, no local toolchain needed

## Installation

> You can just run the `scripts/install.sh` script to do all of this automatically, but the manual steps are documented below if you want to understand what's going on or do it yourself.
> ```bash
>  curl -fsSL https://raw.githubusercontent.com/LeoLTM/sendspin-armv6/main/scripts/install.sh \
>  | sudo bash
> ```

### 1. Download the binary

Grab the latest release from the [Releases](../../releases) page:

```bash
wget https://github.com/LeoLTM/sendspin-armv6/releases/latest/download/sendspin-armv6-linux-armv6-release.tar.gz
mkdir sendspin-armv6 && tar -xzf sendspin-armv6-linux-armv6-release.tar.gz -C sendspin-armv6
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
# Optional: set the ALSA device for audio output (see next section)
device = plughw:1,0
# Optional: set initial volume (0-100)
initial_volume = 80
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

The **Pi Zero W has no built-in 3.5mm audio jack**. You need an external USB soundcard (or a DAC HAT). Any cheap USB audio adapter works.

### Find your device

Plug in the USB soundcard, then list available devices:

```bash
aplay -l
```

Example output:
```
**** List of PLAYBACK Hardware Devices ****
card 0: b1 [bcm2835 HDMI 1], device 0: ...
card 1: Device [USB Audio Device], device 0: USB Audio [USB Audio]
```

The USB soundcard in this example is `card 1, device 0` → device string is `plughw:1,0`.

### Configure the device

Set the `device` key in `/etc/sendspin-armv6.conf`:

```ini
device = plughw:1,0
```

Then restart the service:

```bash
sudo systemctl restart sendspin-armv6
```

### Set the USB soundcard as default (optional)

If you want all system audio to use the USB soundcard, add to `/etc/asound.conf`:

```conf
defaults.pcm.card 1
defaults.ctl.card 1
```

With this in place the `device` config key can be left unset.

## Configuration reference

| Key | Required | Default | Description |
|---|---|---|---|
| `server_url` | **yes** | — | WebSocket URL of the Sendspin server |
| `name` | no | `sendspin-armv6` | Friendly name shown in the Sendspin UI |
| `log_level` | no | `info` | `none`, `error`, `warn`, `info`, `debug`, `verbose` |
| `device` | no | Default system audio device (`aplay -L`) | ALSA device string for audio output (e.g. `plughw:1,0`) |
| `initial_volume` | no | Server-controlled | Initial hardware volume at startup (0–100) |

## Upgrading

Your config at `/etc/sendspin-armv6.conf` is **never touched** during an upgrade.

### Upgrade script

```bash
curl -fsSL https://raw.githubusercontent.com/LeoLTM/sendspin-armv6/main/scripts/upgrade.sh \
  | sudo bash
```

### Manual upgrade

```bash
# 1. Download the new release (same as installation step 1)
wget https://github.com/LeoLTM/sendspin-armv6/releases/latest/download/sendspin-armv6-linux-armv6-release.tar.gz
mkdir sendspin-tmp && tar -xzf sendspin-armv6-linux-armv6-release.tar.gz -C sendspin-tmp

# 2. Stop, replace binary, reload service file, start
sudo systemctl stop sendspin-armv6
sudo cp sendspin-tmp/sendspin-armv6 /usr/local/bin/
sudo cp sendspin-tmp/sendspin-armv6.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl start sendspin-armv6
```

## Uninstall

### Uninstall script

```bash
curl -fsSL https://raw.githubusercontent.com/LeoLTM/sendspin-armv6/main/scripts/uninstall.sh \
  | sudo bash
```

### Manual uninstall

```bash
sudo systemctl stop sendspin-armv6
sudo systemctl disable sendspin-armv6
sudo rm /usr/local/bin/sendspin-armv6
sudo rm /etc/systemd/system/sendspin-armv6.service
sudo systemctl daemon-reload

# Optional: remove config file if you don't plan to reinstall or want to start fresh
sudo rm /etc/sendspin-armv6.conf
```

## Troubleshooting

**Crash-loop / SEGV at startup** — the service stops restarting after 5 rapid failures. To re-enable after fixing the issue:

```bash
sudo systemctl reset-failed sendspin-armv6
sudo systemctl start sendspin-armv6
```

Common causes:
- Wrong architecture binary (e.g. ARMv6 on ARMv7) - make sure your device is ARMv6
- USB soundcard not connected or `aplay` not available — plug in the soundcard and check `aplay -l`
- Network not ready — check that the Pi can reach the server IP before the service starts
- Wrong `server_url` in the config — verify with `curl http://<ip>:8928/`

**No audio / aplay errors** — run `aplay -l` to find the correct device name and set it with `device = plughw:X,Y` in the config.

## Building from source

> I never tested this locally because I didn't want to mess with the local toolchain. Feel free to open a PR if you have a better local build setup.

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
