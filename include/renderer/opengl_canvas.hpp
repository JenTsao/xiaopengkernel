#pragma once

#include "canvas.hpp"
#include "image.hpp"
#include <string>
#include <vector>
#include <memory>

#ifdef ENABLE_OPENGL
// Include OpenGL headers via SDL for portability
#ifdef ENABLE_SDL
#include <SDL.h>
#include <SDL_opengl.h>
#else
#ifdef _WIN32
#include <windows.h>
#endif
#include <GL/gl.h>
#endif
#endif

#ifndef ENABLE_OPENGL
// Provide a stub GLuint type when OpenGL is not available
typedef unsigned int GLuint;
#endif

namespace xiaopeng {
namespace renderer {

class OpenGLCanvas : public Canvas {
public:
  OpenGLCanvas(int width, int height);
  ~OpenGLCanvas() override;

  void resize(int width, int height) override;
  void clear(Color color) override;

  void fillRect(int x, int y, int width, int height, Color color) override;
  void drawRect(int x, int y, int width, int height, Color color) override;
  void drawLine(int x1, int y1, int x2, int y2, Color color) override;
  void drawText(int x, int y, const std::string &text, Color color,
                int scale = 1) override;

  void setFont(const std::string &family, int size) override;
  void drawTextVector(int x, int y, const std::string &text,
                      Color color) override;

  void drawImage(const Image &image, int x, int y, int width,
                 int height) override;

  int width() const override { return width_; }
  int height() const override { return height_; }

  void pushClipRect(int x, int y, int width, int height) override;
  void popClipRect() override;

  // OpenGL-specific methods
  GLuint getTexture() const { return texture_; }
  // False when the framebuffer/texture could not be created (missing GL
  // extensions, incomplete FBO); GL drawing must not run then
  bool isReady() const { return glReady_; }
  void bindTexture();
  void unbindTexture();

private:
  int width_;
  int height_;
  GLuint texture_ = 0;
  GLuint framebuffer_ = 0;
  bool glReady_ = false;
  GLuint renderbuffer_ = 0;
  
  // For software fallback (when OpenGL is not available)
  std::vector<uint32_t> fallbackBuffer_;
  
  // Font state
  std::string currentFontFamily_ = "Arial";
  int currentFontSize_ = 16;

  // Clipping
  struct ClipRect {
    int x, y, width, height;
  };
  std::vector<ClipRect> clipStack_;

  void initializeOpenGL();
  void cleanupOpenGL();
  void updateTexture(const void* data);
  
  // Software rendering fallback
  void setPixel(int x, int y, Color color);
  void blendPixel(int x, int y, Color color);
  void fillRectSoftware(int x, int y, int width, int height, Color color);
  bool isInClip(int x, int y) const;
  void intersectWithClip(int& x, int& y, int& width, int& height) const;
};

} // namespace renderer
} // namespace xiaopeng