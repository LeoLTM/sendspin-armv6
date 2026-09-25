#pragma once

#include <cstdint>
#include <initializer_list>

/// Minimal driver for SSD1306 / SH1106 monochrome OLED panels over Linux
/// i2c-dev (/dev/i2c-N).  No external libraries required.
class Ssd1306 {
  public:
    enum class Controller { SSD1306, SH1106 };

    static constexpr int WIDTH = 128;

    Ssd1306() = default;
    ~Ssd1306();

    Ssd1306(const Ssd1306&) = delete;
    Ssd1306& operator=(const Ssd1306&) = delete;

    /// Open /dev/i2c-<bus>, run the controller init sequence and turn the
    /// panel on (blank).  height is 64 or 32.
    bool open(int bus, int address, Controller controller, int height, bool rotate180,
              uint8_t contrast);

    /// Turn the panel off and close the bus.  Safe to call when not open.
    void close();

    bool is_open() const { return fd_ >= 0; }

    /// Write one 8-pixel-tall page (WIDTH bytes, bit 0 = top row).
    bool write_page(int page, const uint8_t* data);

    /// Panel power (sleep) without losing its RAM contents.
    bool set_display_on(bool on);

  private:
    bool command(std::initializer_list<uint8_t> cmds);

    int fd_{-1};
    Controller controller_{Controller::SSD1306};
};
