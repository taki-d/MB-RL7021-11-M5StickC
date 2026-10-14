#pragma once

#include <M5Display.h>
#include <stdint.h>

struct MeterDisplayState {
    bool enabled = true;
    bool valid = false;
    int32_t watts = 0;
    uint32_t sample_at = 0;
    uint32_t sample_gap_ms = 0;
    uint32_t sample_count = 0;
    bool wifi_connected = false;
    bool meter_connected = false;
    const char *meter_status = "starting";
};

// One reusable framebuffer; only changed 16-row strips go over SPI, at most
// one strip per loop. No drawing or allocation is performed in HTTP callbacks.
class MeterDisplay {
public:
    explicit MeterDisplay(TFT_eSPI *screen) : screen_(screen), frame_(screen) {}
    bool begin();
    void add_sample(int32_t watts, uint32_t received_at);
    void update(uint32_t now, const MeterDisplayState &state);

private:
    static constexpr int kWidth = 240;
    static constexpr int kHeight = 135;
    static constexpr int kStripHeight = 16;
    static constexpr int kStrips = (kHeight + kStripHeight - 1) / kStripHeight;
    static constexpr int kHistorySize = 64;
    struct Sample { int32_t watts; uint32_t at; };

    TFT_eSPI *screen_;
    TFT_eSprite frame_;
    uint16_t *pixels_ = nullptr;
    Sample history_[kHistorySize] = {};
    int history_next_ = 0;
    int history_count_ = 0;
    uint32_t strip_hash_[kStrips] = {};
    uint16_t dirty_strips_ = 0;
    bool rendered_ = false;
    uint32_t rendered_at_ = 0;
    uint32_t rendered_sample_count_ = 0;

    void render(uint32_t now, const MeterDisplayState &state);
    void render_history(uint32_t now);
    void mark_changed_strips();
    void flush_one_strip();
};
