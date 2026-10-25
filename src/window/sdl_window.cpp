#include <cstring>
#include <iostream>
#include <window/sdl_window.hpp>

#ifdef ENABLE_OPENGL
#include <GL/gl.h>
#endif

namespace xiaopeng::window {

SdlWindow::SdlWindow() {}

SdlWindow::~SdlWindow() {
#ifdef ENABLE_SDL
  if (m_texture)
    SDL_DestroyTexture(m_texture);
  if (m_renderer)
    SDL_DestroyRenderer(m_renderer);
  if (m_window)
    SDL_DestroyWindow(m_window);

  if (s_sdlRefCount > 0) {
    s_sdlRefCount--;
    if (s_sdlRefCount == 0) {
      SDL_Quit();
    }
  }
#endif
}

bool SdlWindow::initialize(int width, int height, const std::string &title) {
#ifdef ENABLE_SDL
  if (s_sdlRefCount == 0) {
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      std::cerr << "SDL could not initialize! SDL_Error: " << SDL_GetError()
                << std::endl;
      return false;
    }
  }
  s_sdlRefCount++;

#ifdef ENABLE_OPENGL
  // Create window with OpenGL support
  m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, width, height,
                              SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
#else
  // Create window without OpenGL
  m_window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_UNDEFINED,
                              SDL_WINDOWPOS_UNDEFINED, width, height,
                              SDL_WINDOW_SHOWN);
#endif

  if (!m_window) {
    std::cerr << "Window could not be created! SDL_Error: " << SDL_GetError()
              << std::endl;
    return false;
  }

#ifdef ENABLE_OPENGL
  // Create OpenGL context
  SDL_GLContext glContext = SDL_GL_CreateContext(m_window);
  if (!glContext) {
    std::cerr << "OpenGL context could not be created! SDL_Error: "
              << SDL_GetError() << std::endl;
    return false;
  }

  // Set OpenGL attributes
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

  std::cout << "[SdlWindow] OpenGL context created successfully" << std::endl;
#else
  // Create SDL renderer for software rendering
  m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED);
  if (!m_renderer) {
    std::cerr << "Renderer could not be created! SDL_Error: " << SDL_GetError()
              << std::endl;
    return false;
  }

  m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_STREAMING, width, height);
  if (!m_texture) {
    std::cerr << "Texture could not be created! SDL_Error: " << SDL_GetError()
              << std::endl;
    return false;
  }
#endif

  m_width = width;
  m_height = height;
  m_isOpen = true;

  // Enable SDL text input to capture keyboard text (for form controls)
  SDL_StartTextInput();

  return true;
#else
  (void)width;
  (void)height;
  (void)title;
  std::cerr << "SDL2 support is not enabled in this build." << std::endl;
  return false;
#endif
}

void SdlWindow::drawBuffer(const xiaopeng::renderer::BitmapCanvas &canvas) {
#ifdef ENABLE_SDL
  if (!m_isOpen)
    return;

#ifdef ENABLE_OPENGL
  (void)canvas;
  // For OpenGL rendering, we need to use a different approach
  // This method should not be called when OpenGL is enabled
  std::cerr
      << "drawBuffer called with OpenGL enabled - use drawOpenGLTexture instead"
      << std::endl;
#else
  void *pixels;
  int pitch;
  if (SDL_LockTexture(m_texture, nullptr, &pixels, &pitch) < 0) {
    std::cerr << "Failed to lock texture: " << SDL_GetError() << std::endl;
    return;
  }

  // Copy canvas buffer to texture
  const auto &buffer = canvas.buffer();
  memcpy(pixels, buffer.data(), buffer.size() * sizeof(uint32_t));

  SDL_UnlockTexture(m_texture);

  SDL_RenderClear(m_renderer);
  SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
  SDL_RenderPresent(m_renderer);
#endif
#endif
}

void SdlWindow::drawOpenGLTexture(GLuint texture) {
#ifdef ENABLE_SDL
#ifdef ENABLE_OPENGL
  if (!m_isOpen || texture == 0)
    return;

  // Bind the texture
  glBindTexture(GL_TEXTURE_2D, texture);

  // Enable texturing
  glEnable(GL_TEXTURE_2D);

  // Set up orthographic projection
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0, m_width, m_height, 0, -1, 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  // Draw a quad with the texture. The framebuffer is rendered with y down,
  // so texel row 0 is the bottom of the image: flip v or the window shows
  // the frame upside down.
  glBegin(GL_QUADS);
  glTexCoord2f(0, 1);
  glVertex2f(0, 0);
  glTexCoord2f(1, 1);
  glVertex2f(m_width, 0);
  glTexCoord2f(1, 0);
  glVertex2f(m_width, m_height);
  glTexCoord2f(0, 0);
  glVertex2f(0, m_height);
  glEnd();

  // Leave clean state: the canvas texture must not modulate the next
  // frame's solid-colour quads
  glDisable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, 0);

  // Swap buffers
  SDL_GL_SwapWindow(m_window);
#endif
#endif
}

bool SdlWindow::processEvents() {
#ifdef ENABLE_SDL
  SDL_Event e;
  while (SDL_PollEvent(&e) != 0) {
    if (e.type == SDL_QUIT) {
      m_isOpen = false;
      return false;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN) {
      MouseEvent me;
      me.x = e.button.x;
      me.y = e.button.y;
      switch (e.button.button) {
      case SDL_BUTTON_LEFT:
        me.button = MouseButton::Left;
        break;
      case SDL_BUTTON_RIGHT:
        me.button = MouseButton::Right;
        break;
      case SDL_BUTTON_MIDDLE:
        me.button = MouseButton::Middle;
        break;
      default:
        me.button = MouseButton::Left;
        break;
      }
      m_pendingClicks.push_back(me);
    } else if (e.type == SDL_TEXTINPUT) {
      // Collect typed characters into our queue
      for (const char *p = e.text.text; *p != '\0'; ++p) {
        m_pendingKeys.push_back(*p);
      }
    }
  }
  return true;
#else
  return false;
#endif
}

std::vector<MouseEvent> SdlWindow::pendingMouseClicks() {
  auto clicks = std::move(m_pendingClicks);
  m_pendingClicks.clear();
  return clicks;
}

std::vector<char> SdlWindow::pendingKeyInputs() {
  auto keys = std::move(m_pendingKeys);
  m_pendingKeys.clear();
  return keys;
}

} // namespace xiaopeng::window

// Define static member
int xiaopeng::window::SdlWindow::s_sdlRefCount = 0;
