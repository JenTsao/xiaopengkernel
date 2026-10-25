#include "renderer/bitmap_canvas.hpp"
#include "renderer/font_manager.hpp"
#include "renderer/text_shaper.hpp"
#include <cmath>
#include <fstream>
#include <iostream>

#ifdef ENABLE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace xiaopeng {
namespace renderer {

// Simple 8x8 font bitmap (partial ASCII)
// This is a minimal set for testing. Ideally, this would be in a separate file.
// Format: 8 bytes per character, row by row. 1 bit = pixel on.
static const uint8_t font8x8_basic[] = {
    // Space (0x20)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ! (0x21)
    0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00,
    // " (0x22)
    0x66, 0x66, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00,
    // # (0x23)
    0x6C, 0x6C, 0xFE, 0x6C, 0xFE, 0x6C, 0x6C, 0x00,
    // $ (0x24)
    0x18, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x18, 0x00,
    // % (0x25)
    0x00, 0xC6, 0xCC, 0x18, 0x30, 0x66, 0xC6, 0x00,
    // & (0x26)
    0x38, 0x6C, 0x38, 0x76, 0xDC, 0xCC, 0x76, 0x00,
    // ' (0x27)
    0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ( (0x28)
    0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00,
    // ) (0x29)
    0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00,
    // * (0x2A)
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00,
    // + (0x2B)
    0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00,
    // , (0x2C)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30,
    // - (0x2D)
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00,
    // . (0x2E)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00,
    // / (0x2F)
    0x00, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x00,
    // 0-9 (0x30-0x39)
    0x3C, 0x66, 0xC6, 0xC6, 0xC6, 0x66, 0x3C, 0x00, // 0
    0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, // 1
    0x3C, 0x66, 0x06, 0x0C, 0x30, 0x60, 0x7E, 0x00, // 2
    0x3C, 0x66, 0x06, 0x1C, 0x06, 0x66, 0x3C, 0x00, // 3
    0x0C, 0x1C, 0x3C, 0x6C, 0xCC, 0xFE, 0x0C, 0x00, // 4
    0x7E, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C, 0x00, // 5
    0x3C, 0x60, 0xC0, 0xFC, 0xC6, 0x66, 0x3C, 0x00, // 6
    0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x00, // 7
    0x3C, 0x66, 0xC6, 0x3C, 0xC6, 0x66, 0x3C, 0x00, // 8
    0x3C, 0x66, 0xC6, 0x7E, 0x06, 0x0C, 0x78, 0x00, // 9
    // : (0x3A)
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00,
    // ; (0x3B)
    0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x30,
    // < (0x3C)
    0x06, 0x18, 0x60, 0xC0, 0x60, 0x18, 0x06, 0x00,
    // = (0x3D)
    0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00,
    // > (0x3E)
    0x60, 0x18, 0x06, 0x03, 0x06, 0x18, 0x60, 0x00,
    // ? (0x3F)
    0x3C, 0x66, 0x06, 0x0C, 0x18, 0x00, 0x18, 0x00,
    // @ (0x40)
    0x3C, 0x66, 0x6E, 0x6E, 0x60, 0x62, 0x3C, 0x00,
    // A-Z (0x41-0x5A)
    0x18, 0x3C, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x00, // A
    0xFC, 0x66, 0x66, 0x7C, 0x66, 0x66, 0xFC, 0x00, // B
    0x3C, 0x66, 0xC0, 0xC0, 0xC0, 0x66, 0x3C, 0x00, // C
    0xF8, 0x6C, 0x66, 0x66, 0x66, 0x6C, 0xF8, 0x00, // D
    0xFE, 0x62, 0x68, 0x78, 0x68, 0x62, 0xFE, 0x00, // E
    0xFE, 0x62, 0x68, 0x78, 0x68, 0x60, 0xF0, 0x00, // F
    0x3C, 0x66, 0xC0, 0xC0, 0xCE, 0x66, 0x3C, 0x00, // G
    0x66, 0x66, 0x66, 0x7E, 0x66, 0x66, 0x66, 0x00, // H
    0x3C, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00, // I
    0x1E, 0x0C, 0x0C, 0x0C, 0xCC, 0xCC, 0x78, 0x00, // J
    0xE6, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0xE6, 0x00, // K
    0xF0, 0x60, 0x60, 0x60, 0x62, 0x66, 0xFE, 0x00, // L
    0xC6, 0xEE, 0xFE, 0xD6, 0xC6, 0xC6, 0xC6, 0x00, // M
    0xC6, 0xE6, 0xF6, 0xDE, 0xCE, 0xC6, 0xC6, 0x00, // N
    0x3C, 0x66, 0xC6, 0xC6, 0xC6, 0x66, 0x3C, 0x00, // O
    0xFC, 0x66, 0x66, 0x7C, 0x60, 0x60, 0xF0, 0x00, // P
    0x3C, 0x66, 0xC6, 0xC6, 0xD6, 0x7C, 0x0E, 0x00, // Q
    0xFC, 0x66, 0x66, 0x7C, 0x6C, 0x66, 0xE6, 0x00, // R
    0x3C, 0x66, 0x60, 0x3C, 0x06, 0x66, 0x3C, 0x00, // S
    0x7E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, // T
    0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x00, // U
    0x66, 0x66, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00, // V
    0xC6, 0xC6, 0xC6, 0xD6, 0xFE, 0xEE, 0xC6, 0x00, // W
    0xC6, 0x6C, 0x38, 0x10, 0x38, 0x6C, 0xC6, 0x00, // X
    0x66, 0x66, 0x66, 0x3C, 0x18, 0x18, 0x18, 0x00, // Y
    0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xFE, 0x00, // Z
    // [ (0x5B)
    0x3C, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3C, 0x00,
    // \ (0x5C)
    0x00, 0x60, 0x30, 0x18, 0x0C, 0x06, 0x00, 0x00,
    // ] (0x5D)
    0x3C, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x3C, 0x00,
    // ^ (0x5E)
    0x18, 0x3C, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00,
    // _ (0x5F)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00,
    // ` (0x60)
    0x30, 0x30, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00,
    // a-z (0x61-0x7A) - simplified (just upper case copy or small tweaks if
    // needed, but for now copying Upper case for simplicity/readability in 8x8
    // or defining lowercase)
    // Actually standard 8x8 font has distinct lowercase. Let's just use
    // lowercase approximation for now since I'm typing manually.
    // a
    0x00, 0x00, 0x3C, 0x06, 0x3E, 0x66, 0x3E, 0x00,
    // b
    0x60, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x7C, 0x00,
    // c
    0x00, 0x00, 0x3C, 0x60, 0x60, 0x60, 0x3C, 0x00,
    // d
    0x06, 0x06, 0x3E, 0x66, 0x66, 0x66, 0x3E, 0x00,
    // e
    0x00, 0x00, 0x3C, 0x66, 0x7E, 0x60, 0x3C, 0x00,
    // f
    0x1C, 0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x00,
    // g
    0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x3C,
    // h
    0x60, 0x60, 0x76, 0x66, 0x66, 0x66, 0x66, 0x00,
    // i
    0x18, 0x00, 0x38, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // j
    0x06, 0x00, 0x06, 0x06, 0x06, 0x06, 0x3C, 0x00,
    // k
    0x60, 0x60, 0x66, 0x6C, 0x78, 0x6C, 0x66, 0x00,
    // l
    0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3C, 0x00,
    // m
    0x00, 0x00, 0xEC, 0xFE, 0xD6, 0xD6, 0xD6, 0x00,
    // n
    0x00, 0x00, 0xDC, 0x66, 0x66, 0x66, 0x66, 0x00,
    // o
    0x00, 0x00, 0x3C, 0x66, 0x66, 0x66, 0x3C, 0x00,
    // p
    0x00, 0x00, 0xDC, 0x66, 0x66, 0x7C, 0x60, 0xF0,
    // q
    0x00, 0x00, 0x3E, 0x66, 0x66, 0x3E, 0x06, 0x0F,
    // r
    0x00, 0x00, 0x6E, 0x32, 0x20, 0x20, 0x20, 0x00,
    // s
    0x00, 0x00, 0x3E, 0x60, 0x3C, 0x06, 0x7C, 0x00,
    // t
    0x30, 0x78, 0x30, 0x30, 0x30, 0x30, 0x1C, 0x00,
    // u
    0x00, 0x00, 0x66, 0x66, 0x66, 0x66, 0x3E, 0x00,
    // v
    0x00, 0x00, 0x66, 0x66, 0x66, 0x3C, 0x18, 0x00,
    // w
    0x00, 0x00, 0xC6, 0xD6, 0xD6, 0xFE, 0x6C, 0x00,
    // x
    0x00, 0x00, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0x00,
    // y
    0x00, 0x00, 0x66, 0x66, 0x66, 0x3E, 0x0C, 0x38,
    // z
    0x00, 0x00, 0x7E, 0x0C, 0x18, 0x30, 0x7E, 0x00,
    // { (0x7B)
    0x0C, 0x18, 0x18, 0x70, 0x18, 0x18, 0x0C, 0x00,
    // | (0x7C)
    0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x00,
    // } (0x7D)
    0x30, 0x18, 0x18, 0x0E, 0x18, 0x18, 0x30, 0x00,
    // ~ (0x7E)
    0x31, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

BitmapCanvas::BitmapCanvas(int width, int height)
    : width_(width), height_(height) {
  buffer_.resize(width * height, 0); // Init black transparent
  // Default clip = full canvas
  clipStack_.push_back({0, 0, width, height});
}

bool BitmapCanvas::isInClip(int x, int y) const {
  if (clipStack_.empty())
    return true;
  const auto &clip = clipStack_.back();
  return x >= clip.x && x < clip.x + clip.width && y >= clip.y &&
         y < clip.y + clip.height;
}

void BitmapCanvas::intersectWithClip(int &x, int &y, int &width,
                                     int &height) const {
  if (clipStack_.empty())
    return;
  const auto &clip = clipStack_.back();
  int newX = std::max(x, clip.x);
  int newY = std::max(y, clip.y);
  int newRight = std::min(x + width, clip.x + clip.width);
  int newBottom = std::min(y + height, clip.y + clip.height);
  if (newRight <= newX || newBottom <= newY) {
    // No intersection
    width = 0;
    height = 0;
    return;
  }
  x = newX;
  y = newY;
  width = newRight - newX;
  height = newBottom - newY;
}

void BitmapCanvas::pushClipRect(int x, int y, int width, int height) {
  if (clipStack_.empty()) {
    clipStack_.push_back({x, y, width, height});
    return;
  }
  // Intersect with current clip
  const auto &current = clipStack_.back();
  int newX = std::max(x, current.x);
  int newY = std::max(y, current.y);
  int newRight = std::min(x + width, current.x + current.width);
  int newBottom = std::min(y + height, current.y + current.height);
  if (newRight <= newX || newBottom <= newY) {
    // Empty clip
    clipStack_.push_back({0, 0, 0, 0});
    return;
  }
  clipStack_.push_back(
      {newX, newY, newRight - newX, newBottom - newY});
}

void BitmapCanvas::popClipRect() {
  if (clipStack_.size() > 1) {
    clipStack_.pop_back();
  }
}

void BitmapCanvas::resize(int width, int height) {
  width_ = width;
  height_ = height;
  buffer_.resize(width * height);
}

void BitmapCanvas::clear(Color color) {
  // SDL_PIXELFORMAT_ARGB8888 expects [B, G, R, A] in memory for little-endian
  uint32_t val = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
  std::fill(buffer_.begin(), buffer_.end(), val);
}

void BitmapCanvas::setPixel(int x, int y, Color color) {
  if (x < 0 || x >= width_ || y < 0 || y >= height_)
    return;
  if (!isInClip(x, y))
    return;

  // Simple opaque support for now (no blending in setPixel)
  // [B, G, R, A] for SDL_PIXELFORMAT_ARGB8888
  uint32_t val = (color.a << 24) | (color.r << 16) | (color.g << 8) | color.b;
  buffer_[y * width_ + x] = val;
}

void BitmapCanvas::blendPixel(int x, int y, Color color) {
  if (x < 0 || x >= width_ || y < 0 || y >= height_)
    return;
  if (!isInClip(x, y))
    return;

  if (color.a == 0)
    return;
  if (color.a == 255) {
    setPixel(x, y, color);
    return;
  }

  uint32_t bg = buffer_[y * width_ + x];
  // Existing pixel format: (a << 24) | (r << 16) | (g << 8) | b
  // So b is LSB, r is 3rd byte.
  uint8_t bg_b = bg & 0xFF;
  uint8_t bg_g = (bg >> 8) & 0xFF;
  uint8_t bg_r = (bg >> 16) & 0xFF;
  uint8_t bg_a = (bg >> 24) & 0xFF;

  float alpha = color.a / 255.0f;
  float inv_alpha = 1.0f - alpha;

  uint8_t r = static_cast<uint8_t>(color.r * alpha + bg_r * inv_alpha);
  uint8_t g = static_cast<uint8_t>(color.g * alpha + bg_g * inv_alpha);
  uint8_t b = static_cast<uint8_t>(color.b * alpha + bg_b * inv_alpha);
  uint8_t a = static_cast<uint8_t>(color.a + bg_a * inv_alpha); // Approx

  uint32_t val = (a << 24) | (r << 16) | (g << 8) | b;
  buffer_[y * width_ + x] = val;
}

void BitmapCanvas::setFont(const std::string &family, int size) {
  currentFontFamily_ = family;
  currentFontSize_ = size;
}

void BitmapCanvas::drawTextVector(int x, int y, const std::string &text,
                                  Color color) {
#ifdef ENABLE_FREETYPE
  auto &fontManager = FontManager::instance();
  if (!fontManager.initialize()) {
    drawText(x, y, text, color, 1);
    return;
  }

  auto font = fontManager.getFont(currentFontFamily_);
  if (!font) {
    // Try system font discovery
    fontManager.loadSystemFont(currentFontFamily_);
    font = fontManager.getFont(currentFontFamily_);
  }
  if (!font) {
    // Try generic fallback names
    font = fontManager.getFont("sans-serif");
  }

  if (!font) {
    // Fallback to bitmap
    drawText(x, y, text, color, 1);
    return;
  }

  TextShaper shaper;
  auto glyphs = shaper.shape(text, font, currentFontSize_);

  int penX = x;
  int penY = y;

  // Adjust for baseline. FreeType/HarfBuzz gives coordinates relative to
  // baseline. HTML/Canvas usually draws from top-left. Let's use ascender to
  // shift down.
  if (font->ftFace) {
    penY += (font->ftFace->size->metrics.ascender >> 6);
  }

  for (const auto &glyph : glyphs) {
    if (FT_Load_Glyph(font->ftFace, glyph.index, FT_LOAD_RENDER)) {
      continue;
    }

    FT_Bitmap &bitmap = font->ftFace->glyph->bitmap;
    // bitmap_left is distance from pen position to left border of the glyph
    // bitmap bitmap_top is distance from baseline to top border of the glyph
    // bitmap
    int glyphX =
        penX + font->ftFace->glyph->bitmap_left + (glyph.x_offset >> 6);
    int glyphY = penY - font->ftFace->glyph->bitmap_top - (glyph.y_offset >> 6);

    // The buffer layout depends on the pixel mode: mono fonts (common for
    // CJK at small sizes) pack 8 pixels per byte, and the old code read
    // width bytes per row, past the end of the glyph bitmap
    const int pitch = bitmap.pitch;
    for (unsigned int row = 0; row < bitmap.rows; ++row) {
      const unsigned char *line = bitmap.buffer + row * pitch;
      for (unsigned int col = 0; col < bitmap.width; ++col) {
        int alpha = 0;
        switch (bitmap.pixel_mode) {
        case FT_PIXEL_MODE_MONO:
          alpha = (line[col >> 3] & (0x80 >> (col & 7))) ? 255 : 0;
          break;
        case FT_PIXEL_MODE_GRAY:
          alpha = line[col];
          break;
        case FT_PIXEL_MODE_BGRA:
          alpha = line[col * 4 + 3];
          break;
        default:
          alpha = 0;
          break;
        }
        if (alpha > 0) {
          Color c = color;
          // Modulate alpha
          c.a = static_cast<uint8_t>((static_cast<int>(c.a) * alpha) / 255);
          blendPixel(glyphX + col, glyphY + row, c);
        }
      }
    }

    penX += (glyph.x_advance >> 6);
    penY += (glyph.y_advance >> 6);
  }
#else
  // FreeType disabled: fallback to bitmap font
  drawText(x, y, text, color, 1);
#endif
}

void BitmapCanvas::drawImage(const Image &image, int x, int y, int destW,
                             int destH) {
  if (!image.isValid() || destW <= 0 || destH <= 0)
    return;

  // Simple nearest neighbor scaling
  // Loop over destination pixels
  for (int j = 0; j < destH; ++j) {
    int dstY = y + j;
    if (dstY < 0 || dstY >= height_)
      continue;

    for (int i = 0; i < destW; ++i) {
      int dstX = x + i;
      if (dstX < 0 || dstX >= width_)
        continue;

      // Map to source coordinates
      int srcX = (i * image.width()) / destW;
      int srcY = (j * image.height()) / destH;

      // Clamp source coords
      if (srcX >= image.width())
        srcX = image.width() - 1;
      if (srcY >= image.height())
        srcY = image.height() - 1;

      const uint8_t *srcData = image.data();
      int srcIndex = (srcY * image.width() + srcX) * image.channels();

      Color color;
      color.r = srcData[srcIndex];
      color.g = srcData[srcIndex + 1];
      color.b = srcData[srcIndex + 2];

      if (image.channels() == 4) {
        color.a = srcData[srcIndex + 3];
        // Alpha blend
        blendPixel(dstX, dstY, color);
      } else {
        color.a = 255;
        setPixel(dstX, dstY, color);
      }
    }
  }
}

void BitmapCanvas::fillRect(int x, int y, int w, int h, Color color) {
  int cx = x, cy = y, cw = w, ch = h;
  intersectWithClip(cx, cy, cw, ch);
  if (cw <= 0 || ch <= 0)
    return;
  // blendPixel keeps alpha: semi-transparent backgrounds/borders used to
  // overwrite the pixels and keep their source alpha
  for (int j = cy; j < cy + ch; ++j) {
    for (int i = cx; i < cx + cw; ++i) {
      blendPixel(i, j, color);
    }
  }
}

void BitmapCanvas::drawRect(int x, int y, int w, int h, Color color) {
  fillRect(x, y, w, 1, color);         // Top
  fillRect(x, y + h - 1, w, 1, color); // Bottom
  fillRect(x, y, 1, h, color);         // Left
  fillRect(x + w - 1, y, 1, h, color); // Right
}

void BitmapCanvas::drawLine(int x1, int y1, int x2, int y2, Color color) {
  // Simple Bresenham-style line drawing (1px wide)
  int dx = std::abs(x2 - x1);
  int dy = std::abs(y2 - y1);
  int sx = x1 < x2 ? 1 : -1;
  int sy = y1 < y2 ? 1 : -1;
  int err = dx - dy;

  while (true) {
    setPixel(x1, y1, color);
    if (x1 == x2 && y1 == y2)
      break;
    int e2 = 2 * err;
    if (e2 > -dy) {
      err -= dy;
      x1 += sx;
    }
    if (e2 < dx) {
      err += dx;
      y1 += sy;
    }
  }
}

void BitmapCanvas::drawText(int x, int y, const std::string &text, Color color,
                            int scale) {
  int curX = x;
  int curY = y;

  for (char c : text) {
    // Cast to unsigned char to prevent negative values when c > 127
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc < 0x20 || uc > 0x7E) {
      uc = '?';
    }

    int index = static_cast<int>(uc) - 0x20;
    const uint8_t *glyph = &font8x8_basic[index * 8];

    for (int row = 0; row < 8; ++row) {
      uint8_t bits = glyph[row];
      for (int col = 0; col < 8; ++col) {
        if (bits & (0x80 >> col)) {
          // Draw scaled pixel
          if (scale == 1) {
            setPixel(curX + col, curY + row, color);
          } else {
            fillRect(curX + col * scale, curY + row * scale, scale, scale,
                     color);
          }
        }
      }
    }

    curX += 8 * scale; // Monospace advance
  }
}

bool BitmapCanvas::saveToPPM(const std::string &filename) {
  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs)
    return false;

  // P3 (ASCII) or P6 (Binary). P3 is easier to debug.
  ofs << "P3\n";
  ofs << width_ << " " << height_ << "\n";
  ofs << "255\n"; // Maxval

  for (uint32_t pixel : buffer_) {
    // Pixel format: (a << 24) | (r << 16) | (g << 8) | b  =  0xAARRGGBB
    int r = (pixel >> 16) & 0xFF;
    int g = (pixel >> 8) & 0xFF;
    int b = pixel & 0xFF;

    ofs << r << " " << g << " " << b << "\n";
  }

  return true;
}

} // namespace renderer
} // namespace xiaopeng
