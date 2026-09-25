# sendspin-armv6

Headless [Sendspin](https://github.com/Sendspin/sendspin-cpp) audio client for ARMv6 devices like the **Raspberry Pi Zero W**.

Plays music through available ALSA devices. No controls, just audio output as a systemd daemon, with an optional small OLED screen showing what's playing.

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
server_url = ws://192.168.1.10:8927/sendspin
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

## OLED display (optional)

A cheap 128x64 (or 128x32) I2C OLED can show the current track: title, artist, album, the stream format (e.g. `FLAC 44.1kHz 16-bit`), a progress bar with elapsed/total time, play/pause state and volume. Long titles scroll. The stream format line is shown on 128x64 panels only. The display is **disabled by default**, so without one the player runs headless and doesn't request track metadata from the server at all.

Supported panels: **SSD1306** (most 0.96" modules) and **SH1106** (most 1.3" modules). Any 4-pin I2C module (`VCC GND SCL SDA`) works.

### Wiring (Pi Zero W)

| OLED pin | Pi header pin |
|---|---|
| VCC | Pin 1 (3.3V) |
| GND | Pin 6 (GND) |
| SDA | Pin 3 (GPIO 2 / SDA) |
| SCL | Pin 5 (GPIO 3 / SCL) |

### Enable I2C

```bash
sudo raspi-config nonint do_i2c 0   # or: raspi-config → Interface Options → I2C
```

Optionally speed up the bus for smoother scrolling by adding this line to `/boot/firmware/config.txt` (`/boot/config.txt` on older releases), then reboot:

```ini
dtparam=i2c_arm_baudrate=400000
```

Check that the display is detected (it usually shows up as `3c`):

```bash
sudo apt install i2c-tools
i2cdetect -y 1
```

### Configure

Add to `/etc/sendspin-armv6.conf` and restart the service:

```ini
display = ssd1306          # or sh1106 for 1.3" panels; none to disable
display_i2c_address = 3c   # as shown by i2cdetect
#display_height = 32       # for 128x32 panels
#display_rotate = 180      # if mounted upside down
```

The panel switches off after `display_sleep` seconds (default 300) without playback to prevent OLED burn-in, and wakes up when music starts.

Drawing runs in a lowest-priority background thread and only sends changed parts of the screen, so it doesn't interfere with audio playback. If the display is missing or stops responding, the error is logged and the display is retried every 10 seconds while playback continues normally.

To turn the display off again, set `display = none` (or remove the line) and restart the service.

## Configuration reference

| Key | Required | Default | Description |
|---|---|---|---|
| `server_url` | **yes** | — | WebSocket URL of the Sendspin server |
| `name` | no | `sendspin-armv6` | Friendly name shown in the Sendspin UI |
| `log_level` | no | `info` | `none`, `error`, `warn`, `info`, `debug`, `verbose` |
| `device` | no | Default system audio device (`aplay -L`) | ALSA device string for audio output (e.g. `plughw:1,0`) |
| `initial_volume` | no | Server-controlled | Initial hardware volume at startup (0–100) |
| `idle_timeout` | no | `0` (disabled) | Idle timeout in seconds before releasing the audio device |
| `display` | no | `none` | OLED display: `none`, `ssd1306` or `sh1106` |
| `display_i2c_bus` | no | `1` | I2C bus number (`/dev/i2c-N`) |
| `display_i2c_address` | no | `3c` | I2C address in hex, as shown by `i2cdetect` |
| `display_height` | no | `64` | Panel height: `64` or `32` |
| `display_rotate` | no | `0` | `0` or `180` degrees |
| `display_contrast` | no | `128` | Brightness (0–255) |
| `display_sleep` | no | `300` | Seconds without playback before the panel turns off (`0` = never) |

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
- Wrong `server_url` in the config — verify with `curl http://<ip>:8927/`

**Display stays blank** — check `journalctl -u sendspin-armv6` for `Display:` messages. Make sure I2C is enabled and the address in `i2cdetect -y 1` matches `display_i2c_address`. A 1.3" panel that shows noise or is shifted by two pixels needs `display = sh1106`.

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
