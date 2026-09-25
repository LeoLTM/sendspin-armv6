#include "ssd1306.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

#if __has_include(<linux/i2c-dev.h>)
#include <linux/i2c-dev.h>
#endif
#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif

namespace {

// First byte of every I2C transaction tells the controller what follows.
constexpr uint8_t CONTROL_COMMANDS = 0x00;
constexpr uint8_t CONTROL_DATA = 0x40;

// The SH1106 has 132 columns of RAM; 128-pixel panels are wired to 2..129.
constexpr uint8_t SH1106_COLUMN_OFFSET = 2;

}  // namespace

Ssd1306::~Ssd1306() { close(); }

bool Ssd1306::open(int bus, int address, Controller controller, int height, bool rotate180,
                   uint8_t contrast) {
    close();
    controller_ = controller;

    char path[32];
    snprintf(path, sizeof(path), "/dev/i2c-%d", bus);
    fd_ = ::open(path, O_RDWR | O_CLOEXEC);
    if (fd_ < 0) {
        fprintf(stderr, "Display: cannot open %s: %s\n", path, strerror(errno));
        return false;
    }
    if (ioctl(fd_, I2C_SLAVE, address) < 0) {
        fprintf(stderr, "Display: cannot select I2C address 0x%02X: %s\n", address,
                strerror(errno));
        ::close(fd_);
        fd_ = -1;
        return false;
    }

    const uint8_t mux = static_cast<uint8_t>(height - 1);
    const uint8_t com_pins = height == 32 ? 0x02 : 0x12;
    const uint8_t seg_remap = rotate180 ? 0xA0 : 0xA1;
    const uint8_t com_scan = rotate180 ? 0xC0 : 0xC8;

    bool ok;
    if (controller == Controller::SH1106) {
        ok = command({
            0xAE,              // display off
            0xD5, 0x80,        // clock divide / oscillator
            0xA8, mux,         // multiplex ratio
            0xD3, 0x00,        // display offset
            0x40,              // start line 0
            0xAD, 0x8B,        // DC-DC converter on
            seg_remap,         // segment remap (horizontal flip)
            com_scan,          // COM scan direction (vertical flip)
            0xDA, com_pins,    // COM pin configuration
            0x81, contrast,    // contrast
            0xD9, 0x22,        // pre-charge period
            0xDB, 0x35,        // VCOMH deselect level
            0xA4,              // display follows RAM
            0xA6,              // normal (not inverted)
        });
    } else {
        ok = command({
            0xAE,              // display off
            0xD5, 0x80,        // clock divide / oscillator
            0xA8, mux,         // multiplex ratio
            0xD3, 0x00,        // display offset
            0x40,              // start line 0
            0x8D, 0x14,        // charge pump on
            0x20, 0x02,        // page addressing mode (same scheme as SH1106)
            seg_remap,         // segment remap (horizontal flip)
            com_scan,          // COM scan direction (vertical flip)
            0xDA, com_pins,    // COM pin configuration
            0x81, contrast,    // contrast
            0xD9, 0xF1,        // pre-charge period
            0xDB, 0x40,        // VCOMH deselect level
            0xA4,              // display follows RAM
            0xA6,              // normal (not inverted)
        });
    }

    // Blank the RAM before switching on so no power-up garbage is shown.
    uint8_t blank[WIDTH] = {};
    for (int p = 0; ok && p < height / 8; ++p) ok = write_page(p, blank);
    if (ok) ok = set_display_on(true);

    if (!ok) {
        fprintf(stderr, "Display: no response at I2C address 0x%02X on bus %d\n", address, bus);
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

void Ssd1306::close() {
    if (fd_ < 0) return;
    set_display_on(false);
    ::close(fd_);
    fd_ = -1;
}

bool Ssd1306::write_page(int page, const uint8_t* data) {
    uint8_t col = controller_ == Controller::SH1106 ? SH1106_COLUMN_OFFSET : 0;
    if (!command({static_cast<uint8_t>(0xB0 | page), static_cast<uint8_t>(col & 0x0F),
                  static_cast<uint8_t>(0x10 | (col >> 4))})) {
        return false;
    }

    uint8_t buf[WIDTH + 1];
    buf[0] = CONTROL_DATA;
    memcpy(buf + 1, data, WIDTH);
    return ::write(fd_, buf, sizeof(buf)) == static_cast<ssize_t>(sizeof(buf));
}

bool Ssd1306::set_display_on(bool on) { return command({static_cast<uint8_t>(on ? 0xAF : 0xAE)}); }

bool Ssd1306::command(std::initializer_list<uint8_t> cmds) {
    if (fd_ < 0) return false;
    uint8_t buf[32];
    if (cmds.size() + 1 > sizeof(buf)) return false;
    buf[0] = CONTROL_COMMANDS;
    memcpy(buf + 1, cmds.begin(), cmds.size());
    auto len = static_cast<ssize_t>(cmds.size() + 1);
    return ::write(fd_, buf, static_cast<size_t>(len)) == len;
}
