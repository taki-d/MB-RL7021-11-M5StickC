#pragma once

// Host framebuffer adapter. Font data is read from the installed M5 library,
// so previews exercise the firmware renderer with the same glyphs and widths.
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace display_test_fonts {
#include <Fonts/glcdfont.c>
#include <Fonts/Font32rle.c>
#include <Fonts/Font64rle.c>
#include <Fonts/Font7srle.c>
}

constexpr int TL_DATUM = 0;
inline bool &fail_sprite_allocation() { static bool fail = false; return fail; }
inline uint16_t swap16(uint16_t value) { return (value >> 8) | (value << 8); }

class TFT_eSPI {
public:
    struct Text { std::string value; int x, y, font; uint16_t color; };
    std::vector<Text> texts;
    std::vector<uint16_t> pixels;
    int pushes = 0;
    explicit TFT_eSPI(int w = 240, int h = 135) : pixels(w * h), w_(w), h_(h) {}
    int width() const { return w_; }
    int height() const { return h_; }
    void setSwapBytes(bool swap) { assert(!swap); }
    void setTextWrap(bool wrap) { assert(!wrap); }
    void setTextDatum(int datum) { assert(datum == TL_DATUM); }
    void setTextSize(int size) { assert(size == 1); }
    void setTextColor(uint16_t fg, uint16_t bg) { fg_ = fg; bg_ = bg; }
    void fillScreen(uint16_t color) { std::fill(pixels.begin(), pixels.end(), swap16(color)); }
    void pushImage(int x, int y, int w, int h, const uint16_t *data) {
        assert(x == 0 && w == w_ && h <= 16 && y >= 0 && y + h <= h_);
        std::copy(data, data + w * h, pixels.begin() + y * w_);
        ++pushes;
    }
    void drawPixel(int x, int y, uint16_t color) {
        assert(x >= 0 && x < w_ && y >= 0 && y < h_);
        pixels[y * w_ + x] = swap16(color);
    }
    void fillCircle(int x, int y, int r, uint16_t color) {
        for (int dy = -r; dy <= r; ++dy)
            for (int dx = -r; dx <= r; ++dx)
                if (dx * dx + dy * dy <= r * r) drawPixel(x + dx, y + dy, color);
    }
    void drawFastHLine(int x, int y, int w, uint16_t color) {
        for (int dx = 0; dx < w; ++dx) drawPixel(x + dx, y, color);
    }
    void drawLine(int x, int y, int end_x, int end_y, uint16_t color) {
        const int dx = std::abs(end_x - x), sx = x < end_x ? 1 : -1;
        const int dy = -std::abs(end_y - y), sy = y < end_y ? 1 : -1;
        int error = dx + dy;
        for (;;) {
            drawPixel(x, y, color);
            if (x == end_x && y == end_y) break;
            const int doubled = 2 * error;
            if (doubled >= dy) { error += dy; x += sx; }
            if (doubled <= dx) { error += dx; y += sy; }
        }
    }
    int textWidth(const char *text, int face) const {
        int result = 0;
        for (; *text; ++text) result += char_width(*text, face);
        return result;
    }
    void drawString(const char *text, int x, int y, int face) {
        const int height = face == 1 ? 8 : face == 4 ? 26 : 48;
        assert(x >= 0 && y >= 0 && x + textWidth(text, face) <= w_ && y + height <= h_);
        texts.push_back({text, x, y, face, fg_});
        for (; *text; ++text) {
            const int width = char_width(*text, face);
            if (face == 1) {
                for (int cx = 0; cx < width; ++cx)
                    for (int cy = 0; cy < height; ++cy) {
                        const bool bit = cx < 5 && (display_test_fonts::font[static_cast<unsigned char>(*text) * 5 + cx] & (1U << cy));
                        drawPixel(x + cx, y + cy, bit ? fg_ : bg_);
                    }
            } else {
                const auto *data = face == 7 ? display_test_fonts::chrtbl_f7s[*text - 32] :
                    face == 6 ? display_test_fonts::chrtbl_f64[*text - 32] : display_test_fonts::chrtbl_f32[*text - 32];
                int pixel = 0;
                while (pixel < width * height) {
                    const uint8_t run = *data++;
                    for (int n = 0; n <= (run & 0x7f) && pixel < width * height; ++n, ++pixel)
                        drawPixel(x + pixel % width, y + pixel / width, (run & 0x80) ? fg_ : bg_);
                }
            }
            x += width;
        }
    }

protected:
    int w_, h_;
    uint16_t fg_ = 0xffff, bg_ = 0;
    static int char_width(char c, int face) {
        assert(c >= 32 && c <= 126);
        if (face == 1) return 6;
        if (face == 7) return display_test_fonts::widtbl_f7s[c - 32];
        if (face == 6) return display_test_fonts::widtbl_f64[c - 32];
        assert(face == 4);
        return display_test_fonts::widtbl_f32[c - 32];
    }
};

class TFT_eSprite : public TFT_eSPI {
public:
    explicit TFT_eSprite(TFT_eSPI *) {}
    void setColorDepth(int depth) { assert(depth == 16); }
    void *createSprite(int w, int h) {
        if (fail_sprite_allocation()) return nullptr;
        w_ = w; h_ = h; pixels.resize(w * h);
        return pixels.data();
    }
    void fillSprite(uint16_t color) { texts.clear(); fillScreen(color); }
};
