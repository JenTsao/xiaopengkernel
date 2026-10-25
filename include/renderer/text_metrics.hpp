#pragma once

#include <cstdint>
#include <string>
#include <vector>

#ifdef ENABLE_FREETYPE
#include "renderer/font_manager.hpp"
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace xiaopeng {
namespace renderer {

struct TextMeasurement {
  float width = 0;
  float height = 0;
  float ascender = 0;
  float descender = 0;
};

/// Provides text measurement for the layout engine.
/// Uses FreeType when available, otherwise falls back to 8x8 bitmap metrics.
class TextMetrics {
public:
  static TextMetrics &instance() {
    static TextMetrics inst;
    return inst;
  }

  /// Measure a word's pixel width and height at the given font size.
  TextMeasurement measureWord(const std::string &word,
                              const std::string &fontFamily, int fontSize) {
    (void)fontFamily; // Suppress unused warning - fontFamily used in FreeType path
    TextMeasurement m;
#ifdef ENABLE_FREETYPE
    auto &fm = FontManager::instance();
    if (!fm.initialize()) {
      return fallbackMeasure(word, fontSize);
    }
    auto font = fm.getFont(fontFamily);
    if (!font) {
      fm.loadSystemFont(fontFamily);
      font = fm.getFont(fontFamily);
    }
    if (!font || !font->ftFace) {
      return fallbackMeasure(word, fontSize);
    }

    FT_Set_Pixel_Sizes(font->ftFace, 0, fontSize);
    // One glyph per code point: treating each UTF-8 byte as a character made
    // "你好" measure like six Latin glyphs while drawing shapes two
    float totalWidth = 0;
    for (uint32_t cp : decodeUtf8(word)) {
      if (FT_Load_Char(font->ftFace, cp, FT_LOAD_NO_BITMAP)) {
        totalWidth += fontSize * 0.5f; // fallback
        continue;
      }
      totalWidth += (font->ftFace->glyph->advance.x >> 6);
    }

    m.width = totalWidth;
    m.ascender = static_cast<float>(font->ftFace->size->metrics.ascender >> 6);
    m.descender =
        static_cast<float>(font->ftFace->size->metrics.descender >> 6);
    m.height = m.ascender - m.descender;
    return m;
#else
    return fallbackMeasure(word, fontSize);
#endif
  }

  /// Average character width for a given font.
  float charWidth(const std::string &fontFamily, int fontSize) {
#ifdef ENABLE_FREETYPE
    auto &fm = FontManager::instance();
    if (!fm.initialize())
      return static_cast<float>(fontSize) * 0.5f;
    auto font = fm.getFont(fontFamily);
    if (!font) {
      fm.loadSystemFont(fontFamily);
      font = fm.getFont(fontFamily);
    }
    if (!font || !font->ftFace)
      return static_cast<float>(fontSize) * 0.5f;

    FT_Set_Pixel_Sizes(font->ftFace, 0, fontSize);
    // Use 'x' as reference character
    if (FT_Load_Char(font->ftFace, 'x', FT_LOAD_NO_BITMAP) == 0) {
      return static_cast<float>(font->ftFace->glyph->advance.x >> 6);
    }
    return static_cast<float>(fontSize) * 0.5f;
#else
    (void)fontFamily;
    return 8.0f * (fontSize / 16.0f);
#endif
  }

  /// Compute line height for a given font.
  float lineHeight(const std::string &fontFamily, int fontSize) {
#ifdef ENABLE_FREETYPE
    auto &fm = FontManager::instance();
    if (!fm.initialize())
      return static_cast<float>(fontSize) * 1.2f;
    auto font = fm.getFont(fontFamily);
    if (!font) {
      fm.loadSystemFont(fontFamily);
      font = fm.getFont(fontFamily);
    }
    if (!font || !font->ftFace)
      return static_cast<float>(fontSize) * 1.2f;

    FT_Set_Pixel_Sizes(font->ftFace, 0, fontSize);
    float asc = static_cast<float>(font->ftFace->size->metrics.ascender >> 6);
    float desc = static_cast<float>(font->ftFace->size->metrics.descender >> 6);
    return asc - desc;
#else
    (void)fontFamily;
    return 20.0f * (fontSize / 16.0f);
#endif
  }

  /// Measure space width
  float spaceWidth(const std::string &fontFamily, int fontSize) {
#ifdef ENABLE_FREETYPE
    auto &fm = FontManager::instance();
    if (!fm.initialize())
      return static_cast<float>(fontSize) * 0.25f;
    auto font = fm.getFont(fontFamily);
    if (!font) {
      fm.loadSystemFont(fontFamily);
      font = fm.getFont(fontFamily);
    }
    if (!font || !font->ftFace)
      return static_cast<float>(fontSize) * 0.25f;

    FT_Set_Pixel_Sizes(font->ftFace, 0, fontSize);
    if (FT_Load_Char(font->ftFace, ' ', FT_LOAD_NO_BITMAP) == 0) {
      return static_cast<float>(font->ftFace->glyph->advance.x >> 6);
    }
    return static_cast<float>(fontSize) * 0.25f;
#else
    (void)fontFamily;
    return 8.0f * (fontSize / 16.0f);
#endif
  }

private:
  TextMetrics() = default;

  TextMeasurement fallbackMeasure(const std::string &word, int fontSize) {
    TextMeasurement m;
    float scale = static_cast<float>(fontSize) / 16.0f;
    m.width = static_cast<float>(decodeUtf8(word).size()) * 8.0f * scale;
    m.height = 16.0f * scale;
    m.ascender = 12.0f * scale;
    m.descender = -4.0f * scale;
    return m;
  }

  // UTF-8 → code points; malformed bytes decode as U+FFFD
  static std::vector<uint32_t> decodeUtf8(const std::string &text) {
    std::vector<uint32_t> out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      uint32_t cp = 0xFFFD;
      size_t length = 1;
      if (c < 0x80) {
        cp = c;
      } else if ((c & 0xE0) == 0xC0) {
        length = 2;
        cp = c & 0x1F;
      } else if ((c & 0xF0) == 0xE0) {
        length = 3;
        cp = c & 0x0F;
      } else if ((c & 0xF8) == 0xF0) {
        length = 4;
        cp = c & 0x07;
      }
      if (length > 1) {
        if (i + length > text.size()) {
          cp = 0xFFFD;
          length = 1;
        } else {
          for (size_t k = 1; k < length; ++k) {
            const unsigned char next = static_cast<unsigned char>(text[i + k]);
            if ((next & 0xC0) != 0x80) {
              cp = 0xFFFD;
              length = k; // resume at the offending byte
              break;
            }
            cp = (cp << 6) | (next & 0x3F);
          }
        }
      }
      out.push_back(cp);
      i += length;
    }
    return out;
  }
};

} // namespace renderer
} // namespace xiaopeng
