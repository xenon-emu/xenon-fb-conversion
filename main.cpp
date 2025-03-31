// Copyright 2025 Xenon Emulator Project

#include <SDL3/SDL.h>

#include <fstream>
#include <iostream>
#include <vector>

#define GL_GLEXT_PROTOTYPES
extern "C" {
#include <KHR/khrplatform.h>
#include <glad/glad.h>
}

#define TILE(x) ((((x) + 31) >> 5) << 5)

constexpr int resWidth = TILE(1280);
constexpr int resHeight = TILE(720);
constexpr int internalWidth = 1280;
constexpr int internalHeight = 720;

SDL_Window* window;
SDL_GLContext context;
GLuint texture, shaderProgram, pixelBuffer;
GLuint dummyVAO, renderShaderProgram;

int initSDL(const char* windowName, const int w, const int h, SDL_WindowFlags flags) {
  if (!SDL_Init(SDL_INIT_VIDEO)) {
    SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
    return 1;
  }

  window = SDL_CreateWindow(windowName, w, h, flags);

  if (!window) {
    SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
    return 1;
  }

  return 0;
}

constexpr const char* vertexShaderSource = R"(
#version 430 core

out vec2 o_texture_coord;

// https://www.gamedev.net/forums/topic/609917-full-screen-quad-without-vertex-buffer/
// HOWEVER, the OpenGL spec needs a VAO still. This means we can get away with using less data at least
void main() {
  o_texture_coord = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  gl_Position = vec4(o_texture_coord * vec2(2.0f, -2.0f) + vec2(-1.0f, 1.0f), 0.0f, 1.0f);
})";

constexpr const char* fragmentShaderSource = R"(
#version 430 core

in vec2 o_texture_coord;

out vec4 o_color;

uniform usampler2D u_texture;
void main() {
  uint pixel = texture(u_texture, o_texture_coord).r;
  // Gotta love BE vs LE (X360 works in BGRA, so we work in ARGB)
  float a = float((pixel >> 24) & 0xFF) / 255.0;
  float r = float((pixel >> 16) & 0xFF) / 255.0;
  float g = float((pixel >> 8) & 0xFF) / 255.0;
  float b = float((pixel >> 0) & 0xFF) / 255.0;
  o_color = vec4(r, g, b, a);
})";

constexpr const char* computeShaderSource = R"(
#version 430 core

layout (local_size_x = 16, local_size_y = 16) in;

layout (r32ui, binding = 0) uniform writeonly uimage2D o_texture;
layout (std430, binding = 1) buffer pixel_buffer
{
  uint pixel_data[];
};

uniform int internalWidth;
uniform int internalHeight;

uniform int resWidth;
uniform int resHeight;

// This is black magic to convert tiles to linear, just don't touch it
int xeFbConvert(int width, int addr) {
  int y = addr / (width * 4);
  int x = (addr % (width * 4)) / 4;
  return ((((y & ~31) * width) + (x & ~31) * 32) +
         (((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^ 
         ((y & 8) << 2)));
}

#define TILE(x) ((x + 31) >> 5) << 5

void main() {
  ivec2 texel_pos = ivec2(gl_GlobalInvocationID.xy);
  // OOB check, but shouldn't be needed
  if (texel_pos.x >= resWidth || texel_pos.y >= resHeight)
    return;

  // Precalc whatever it would be with extra sizing for 32x32 tiles
  const int tiledWidth = TILE(internalWidth);
  const int tiledHeight = TILE(internalHeight);

  // Scale accordingly
  const float scaleX = tiledWidth / float(resWidth);
  const float scaleY = tiledHeight / float(resHeight);

  // Map to source resolution
  int srcX = int(float(texel_pos.x) * scaleX);
  int srcY = int(float(texel_pos.y) * scaleY);

  // God only knows how this indexing works
  int stdIndex = (srcY * tiledWidth + srcX);
  int xeIndex = xeFbConvert(tiledWidth, stdIndex * 4);

  uint packedColor = pixel_data[xeIndex];
  imageStore(o_texture, texel_pos, uvec4(packedColor, 0, 0, 0));
})";

void compileShader(GLuint shader, const char* source) {
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);

  int success;
  char infoLog[512];
  glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
  if (!success) {
    glGetShaderInfoLog(shader, 512, NULL, infoLog);
    std::cout << "Shader Compilation Error:\n" << infoLog << std::endl;
  }
}

GLuint createShaderProgram(const char* vertex, const char* fragment) {
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  compileShader(vertexShader, vertex);
  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  compileShader(fragmentShader, fragment);
  GLuint program = glCreateProgram();
  glAttachShader(program, vertexShader);
  glAttachShader(program, fragmentShader);
  glLinkProgram(program);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  return program;
}

void initShaders() {
  GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
  compileShader(computeShader, computeShaderSource);
  shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, computeShader);
  glLinkProgram(shaderProgram);
  glDeleteShader(computeShader);
  renderShaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource);
}

void initTexture() {
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, resWidth, resHeight);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
  glBindBuffer(GL_TEXTURE_2D, 0);
}

// ARGB (Console is BGRA)
#define COLOR(r, g, b, a) ((a) << 24 | (r) << 16 | (g) << 8 | (b) << 0)

constexpr int pitch = resWidth * resHeight * sizeof(const uint32_t);
const std::vector<uint32_t> pixels(pitch, COLOR(30, 30, 30, 255)); // Init with dark grey

void initPixelBuffer() {
  glGenBuffers(1, &pixelBuffer);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer);
  glBufferData(GL_SHADER_STORAGE_BUFFER, pixels.size(), pixels.data(), GL_DYNAMIC_DRAW);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void computeDispatch() {
  glUseProgram(shaderProgram);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer);
  glUniform1i(glGetUniformLocation(shaderProgram, "internalWidth"), internalWidth);
  glUniform1i(glGetUniformLocation(shaderProgram, "internalHeight"), internalHeight);
  glUniform1i(glGetUniformLocation(shaderProgram, "resWidth"), resWidth);
  glUniform1i(glGetUniformLocation(shaderProgram, "resHeight"), resHeight);
  glDispatchCompute(resWidth / 16, resHeight / 16, 1);
  glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void initOpenGL() {
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

  context = SDL_GL_CreateContext(window);

  if (!context) {
    SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
    exit(-1);
  }

  if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
    SDL_Log("Failed to initialize GLAD");
    exit(-1);
  }

  initShaders();
  initTexture();
  initPixelBuffer();
  // Creat a dummy VAO
  glGenVertexArrays(1, &dummyVAO);
  // Setup viewport
  glViewport(0, 0, resWidth, resHeight);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);

  glClearColor(0.7f, 0.7f, 0.7f, 1.f);

  SDL_GL_SetSwapInterval(0);
  SDL_SetWindowFullscreen(window, false);
}

void passPixelBuffer(uint32_t* data, size_t size) {
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelBuffer);
  glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, pitch, data);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

const std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(pitch * 4);

void render() {
  // Send over the buffer with the 360fb that is swizzled
  passPixelBuffer(reinterpret_cast<uint32_t*>(buffer.get()), pitch);

  // Dispatch compute shader to unswizzle data
  computeDispatch();

  // Stop anything from updating texture after finishing CS
  glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);

  // Draw fullscreen rect
  glUseProgram(renderShaderProgram);
  glBindTexture(GL_TEXTURE_2D, texture);
  glBindVertexArray(dummyVAO);
  glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

  // Swap
  SDL_GL_SwapWindow(window);
}

void shutdownRender() {
  glDeleteProgram(shaderProgram);
  SDL_GL_DestroyContext(context);
  SDL_DestroyWindow(window);
}

int main() {
  std::cout << "Width: " << resWidth << std::endl;
  std::cout << "Height: " << resHeight << std::endl;
  SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
  if (initSDL("Xenon FB Conversion", resWidth, resHeight, flags) != 0) {
    return 1;
  }

  std::ifstream f("fbmem.bin", std::ios::in | std::ios::binary);

  if (!f) {
    std::cout << "Failed to open framebuffer dump!" << std::endl;
  } else {
    f.read(reinterpret_cast<char*>(buffer.get()), pitch * 4);
  }
  f.close();

  initOpenGL();

  bool running = true;
  SDL_Event event;
  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT)
        running = false;
    }
    render();
  }

  shutdownRender();
  SDL_Quit();
  return 0;
}
