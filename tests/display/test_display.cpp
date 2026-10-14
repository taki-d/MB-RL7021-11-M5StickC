#include <M5Display.h>
#include <cstdio>
#include <cstring>
#include <limits>
#define private public
#include "meter_display.h"
#undef private

static bool has_text(const MeterDisplay &view, const char *text) {
    for (const auto &item : view.frame_.texts) if (item.value == text) return true;
    return false;
}

static void present(MeterDisplay &view, TFT_eSPI &screen, uint32_t now, const MeterDisplayState &state) {
    view.update(now, state);
    for (int i = 0; i < 10; ++i) {
        const int before = screen.pushes;
        view.update(now, state);
        assert(screen.pushes - before <= 1);
    }
    assert(!view.dirty_strips_);
}

static void save(const TFT_eSPI &screen, const char *directory, const char *name) {
    if (!directory) return;
    const std::string path = std::string(directory) + "/" + name + ".ppm";
    FILE *file = fopen(path.c_str(), "wb");
    assert(file);
    fprintf(file, "P6\n240 135\n255\n");
    for (uint16_t raw : screen.pixels) {
        const uint16_t color = swap16(raw);
        fputc(((color >> 11) & 31) * 255 / 31, file);
        fputc(((color >> 5) & 63) * 255 / 63, file);
        fputc((color & 31) * 255 / 31, file);
    }
    fclose(file);
}

int main(int argc, char **argv) {
    const char *output = argc > 1 ? argv[1] : nullptr;
    TFT_eSPI screen;
    MeterDisplay view(&screen);
    assert(view.begin());
    MeterDisplayState state;
    state.meter_status = "scanning";
    present(view, screen, 0, state);
    assert(has_text(view, "--") && has_text(view, "CONNECT") && has_text(view, "scanning"));
    save(screen, output, "connecting");
    state.meter_connected = true;
    present(view, screen, 1000, state);
    assert(has_text(view, "WAIT"));
    state.valid = true; state.wifi_connected = true; state.watts = 351;
    state.sample_count = 51; state.sample_gap_ms = 2400; state.sample_at = 120000;
    for (uint32_t i = 0; i < 50; ++i) view.add_sample(300 + (i * 37) % 180, i * 2400);
    view.add_sample(351, state.sample_at);
    present(view, screen, 121000, state);
    assert(has_text(view, "LIVE") && has_text(view, "351") && has_text(view, "RX 1s"));
    assert(has_text(view, "DT 2.4s"));
    save(screen, output, "live");
    const int pushes = screen.pushes;
    present(view, screen, 121500, state);
    assert(screen.pushes == pushes); // No unchanged frame sent.
    present(view, screen, 125000, state);
    assert(has_text(view, "DELAY"));
    present(view, screen, 130000, state);
    assert(has_text(view, "STALE") && has_text(view, "351"));
    save(screen, output, "stale");
    state.meter_connected = false;
    present(view, screen, 131000, state);
    assert(has_text(view, "CONNECT") && has_text(view, "351"));
    state.enabled = false;
    present(view, screen, 132000, state);
    assert(has_text(view, "OFF") && has_text(view, "--"));
    state.enabled = true; state.meter_connected = true;
    // Digits shrink to fit, including negative/large values, without clipping.
    for (int32_t watts : {0, -351, 9999, 100000, INT32_MIN, INT32_MAX}) {
        state.watts = watts; ++state.sample_count;
        view.add_sample(watts, state.sample_at);
        present(view, screen, 133000, state);
    }
    // History ring rollover and constant-value scaling must be safe too.
    for (uint32_t i = 0; i < 90; ++i) view.add_sample(351, 134000 + i * 2400);
    assert(view.history_count_ == 64);
    state.sample_at = 347600; state.watts = 351; ++state.sample_count;
    present(view, screen, 348000, state);
    save(screen, output, "constant");
    // millis() rollover preserves sample age; expired history is not redrawn.
    state.sample_at = UINT32_MAX - 499;
    present(view, screen, 500, state);
    assert(has_text(view, "RX 1s") && has_text(view, "LIVE"));
    assert(has_text(view, "no recent samples"));
    fail_sprite_allocation() = true;
    MeterDisplay failed(&screen);
    assert(!failed.begin());
    failed.update(0, state);
    puts("display tests passed");
}
