#include "meter_display.h"

#include <stdio.h>

namespace {
constexpr uint16_t kBackground = 0x0842;
constexpr uint16_t kWhite = 0xF7BE;
constexpr uint16_t kMuted = 0x9492;
constexpr uint16_t kGrid = 0x2104;
constexpr uint16_t kAccent = 0x4E7A;
constexpr uint16_t kWarning = 0xFDAC;
constexpr uint16_t kError = 0xFAEA;
constexpr uint32_t kHistoryMs = 120000;
constexpr uint32_t kDelayedMs = 5000;
constexpr uint32_t kStaleMs = 10000;
}

bool MeterDisplay::begin() {
    if (screen_->width() != kWidth || screen_->height() != kHeight) return false;
    frame_.setColorDepth(16);
    pixels_ = static_cast<uint16_t *>(frame_.createSprite(kWidth, kHeight));
    if (!pixels_) return false;
    frame_.setTextWrap(false);
    frame_.setTextDatum(TL_DATUM);
    screen_->setSwapBytes(false); // Sprite pixels already have SPI byte order.
    screen_->fillScreen(kBackground);
    return true;
}

void MeterDisplay::add_sample(int32_t watts, uint32_t received_at) {
    history_[history_next_] = {watts, received_at};
    history_next_ = (history_next_ + 1) % kHistorySize;
    if (history_count_ < kHistorySize) ++history_count_;
}

void MeterDisplay::update(uint32_t now, const MeterDisplayState &state) {
    if (!pixels_) return;
    // Finish the previous frame before reusing its storage.
    if (dirty_strips_) {
        flush_one_strip();
        return;
    }
    if (!rendered_ || state.sample_count != rendered_sample_count_ ||
        static_cast<uint32_t>(now - rendered_at_) >= 1000) {
        render(now, state);
        mark_changed_strips();
        rendered_at_ = now;
        rendered_sample_count_ = state.sample_count;
        rendered_ = true;
        // Return to the UART loop before doing the first SPI transfer.
    }
}

void MeterDisplay::render(uint32_t now, const MeterDisplayState &state) {
    const uint32_t age_ms = now - state.sample_at;
    const char *status = "LIVE";
    uint16_t status_color = kAccent;
    if (!state.enabled) { status = "OFF"; status_color = kMuted; }
    else if (!state.meter_connected) { status = "CONNECT"; status_color = kWarning; }
    else if (!state.valid) { status = "WAIT"; status_color = kWarning; }
    else if (age_ms >= kStaleMs) { status = "STALE"; status_color = kError; }
    else if (age_ms >= kDelayedMs) { status = "DELAY"; status_color = kWarning; }

    frame_.fillSprite(kBackground);
    frame_.setTextSize(1);
    frame_.setTextColor(kAccent, kBackground);
    frame_.drawString("POWER", 10, 7, 1);
    frame_.setTextColor(status_color, kBackground);
    frame_.drawString(status, 94, 7, 1);
    frame_.fillCircle(186, 10, 2, state.wifi_connected ? kAccent : kError);
    frame_.setTextColor(state.wifi_connected ? kMuted : kError, kBackground);
    frame_.drawString("WIFI", 195, 7, 1);
    frame_.drawFastHLine(10, 21, 220, kGrid);

    char value[16];
    if (state.enabled && state.valid) snprintf(value, sizeof(value), "%ld", static_cast<long>(state.watts));
    else snprintf(value, sizeof(value), "--");
    uint8_t value_font = 7;
    const int unit_width = frame_.textWidth("W", 4);
    const int available = kWidth - 20 - unit_width - 8;
    if (frame_.textWidth(value, value_font) > available) value_font = 6;
    if (frame_.textWidth(value, value_font) > available) value_font = 4;
    const int value_width = frame_.textWidth(value, value_font);
    const int x = (kWidth - value_width - unit_width - 8) / 2;
    const int value_height = value_font == 4 ? 26 : 48;
    const uint16_t value_color = !state.enabled || !state.valid ? kMuted :
        (!state.meter_connected || age_ms >= kDelayedMs ? status_color : kWhite);
    frame_.setTextColor(value_color, kBackground);
    frame_.drawString(value, x, 76 - value_height, value_font);
    frame_.setTextColor(kMuted, kBackground);
    frame_.drawString("W", x + value_width + 8, 50, 4);

    if (state.enabled && state.valid && state.meter_connected) {
        render_history(now);
    } else {
        char message[37];
        snprintf(message, sizeof(message), "%s", !state.enabled ? "Power polling disabled" : state.meter_status);
        frame_.setTextColor(kMuted, kBackground);
        frame_.drawString(message, 10, 94, 1);
    }

    frame_.drawFastHLine(10, 118, 220, kGrid);
    char age[24] = "RX --";
    char gap[24] = "DT --";
    if (state.enabled && state.valid) {
        const uint32_t seconds = age_ms / 1000;
        if (seconds < 1000) snprintf(age, sizeof(age), "RX %lus", static_cast<unsigned long>(seconds));
        else snprintf(age, sizeof(age), "RX %lum", static_cast<unsigned long>(seconds / 60));
        if (state.sample_count > 1) snprintf(gap, sizeof(gap), "DT %.1fs", state.sample_gap_ms / 1000.0);
    }
    frame_.setTextColor(status_color, kBackground);
    frame_.drawString(age, 10, 125, 1);
    frame_.setTextColor(kMuted, kBackground);
    frame_.drawString(gap, kWidth - 10 - frame_.textWidth(gap, 1), 125, 1);
}

void MeterDisplay::render_history(uint32_t now) {
    bool found = false;
    int64_t low = 0, high = 0;
    const int first = (history_next_ - history_count_ + kHistorySize) % kHistorySize;
    for (int i = 0; i < history_count_; ++i) {
        const Sample &sample = history_[(first + i) % kHistorySize];
        if (static_cast<uint32_t>(now - sample.at) > kHistoryMs) continue;
        if (!found || sample.watts < low) low = sample.watts;
        if (!found || sample.watts > high) high = sample.watts;
        found = true;
    }
    frame_.setTextColor(kMuted, kBackground);
    frame_.drawString("2 MIN", 10, 82, 1);
    if (!found) {
        frame_.drawString("no recent samples", 100, 96, 1);
        return;
    }
    char range[32];
    snprintf(range, sizeof(range), "%ld..%ld W", static_cast<long>(low), static_cast<long>(high));
    frame_.drawString(range, kWidth - 10 - frame_.textWidth(range, 1), 82, 1);
    // Auto-range with padding; use wide arithmetic even for INT32_MIN/MAX.
    const int64_t padding = (high - low) / 10 > 10 ? (high - low) / 10 : 10;
    low -= padding;
    high += padding;
    frame_.drawFastHLine(10, 113, 220, kGrid);
    bool previous = false;
    int previous_x = 0, previous_y = 0;
    uint32_t previous_at = 0;
    for (int i = 0; i < history_count_; ++i) {
        const Sample &sample = history_[(first + i) % kHistorySize];
        const uint32_t age = now - sample.at;
        if (age > kHistoryMs) continue;
        const int x = 229 - static_cast<int>(static_cast<uint64_t>(age) * 219 / kHistoryMs);
        const int y = 112 - static_cast<int>((static_cast<int64_t>(sample.watts) - low) * 18 / (high - low));
        if (previous && static_cast<uint32_t>(sample.at - previous_at) < kStaleMs) {
            frame_.drawLine(previous_x, previous_y, x, y, kAccent);
        } else {
            frame_.drawPixel(x, y, kAccent);
        }
        previous = true;
        previous_x = x;
        previous_y = y;
        previous_at = sample.at;
    }
}

void MeterDisplay::mark_changed_strips() {
    for (int strip = 0; strip < kStrips; ++strip) {
        const int y = strip * kStripHeight;
        const int rows = y + kStripHeight <= kHeight ? kStripHeight : kHeight - y;
        uint32_t hash = 2166136261UL;
        for (int i = y * kWidth; i < (y + rows) * kWidth; ++i) {
            hash = (hash ^ pixels_[i]) * 16777619UL;
        }
        if (!rendered_ || hash != strip_hash_[strip]) {
            dirty_strips_ |= static_cast<uint16_t>(1U << strip);
            strip_hash_[strip] = hash;
        }
    }
}

void MeterDisplay::flush_one_strip() {
    for (int strip = 0; strip < kStrips; ++strip) {
        const uint16_t mask = static_cast<uint16_t>(1U << strip);
        if (!(dirty_strips_ & mask)) continue;
        const int y = strip * kStripHeight;
        const int rows = y + kStripHeight <= kHeight ? kStripHeight : kHeight - y;
        screen_->pushImage(0, y, kWidth, rows, pixels_ + y * kWidth);
        dirty_strips_ &= static_cast<uint16_t>(~mask);
        break;
    }
}
