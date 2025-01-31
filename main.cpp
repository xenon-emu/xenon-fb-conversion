#include <iostream>
#include <fstream>
#include <vector>
#include <stdint.h>
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <SDL3/SDL.h>
#define GL_GLEXT_PROTOTYPES
#include <KHR/khrplatform.h>
#include <glad/glad.h>
#define TILE(x) ((((x) + 31) >> 5) << 5)

SDL_Window* window;
SDL_GLContext context;
GLuint texture, shaderProgram, pixelBuffer;
GLuint quadVAO, quadVBO, renderShaderProgram;
int resWidth = TILE(1280);
int resHeight = TILE(720);

int initSdl(const char* windowName, int w, int h, SDL_WindowFlags flags)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
	{
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return 1;
    }
	
	window = SDL_CreateWindow(windowName, w, h, flags);

    if (!window)
	{
        SDL_Log("Couldn't create window/renderer: %s", SDL_GetError());
        return 1;
    }
	return 0;
}

constexpr const char* vertexShaderSource = R"(
#version 430 core
layout (location = 0) in vec2 i_pos;
layout (location = 1) in vec2 i_texture_coord;
out vec2 o_texture_coord;
void main() {
  gl_Position = vec4(i_pos, 0.0, 1.0);
  o_texture_coord = i_texture_coord;
}
)";
constexpr const char* fragmentShaderSource = R"(
#version 430 core
in vec2 o_texture_coord;
out vec4 o_color;
uniform usampler2D u_texture;

void main() {
    uint pixel = texelFetch(u_texture, ivec2(gl_FragCoord.xy), 0).r;
    float b = float((pixel >> 24) & 0xFF) / 255.0;
    float g = float((pixel >> 16) & 0xFF) / 255.0;
    float r = float((pixel >> 8)  & 0xFF) / 255.0;
    float a = float((pixel >> 0)  & 0xFF) / 255.0;
    o_color = vec4(r, g, b, a);
}
)";
constexpr const char* computeShaderSource = R"(
#version 430 core

layout (local_size_x = 16, local_size_y = 16) in;
layout (r32ui, binding = 0) uniform writeonly uimage2D o_texture;
layout (std430, binding = 1) buffer pixel_buffer {
    uint pixel_data[];
};

uniform int resWidth;
uniform int resHeight;

void main() {
    ivec2 texel_pos = ivec2(gl_GlobalInvocationID.xy);

    double d_index = texel_pos.y * resWidth + texel_pos.x;
    int index = int(d_index);

    uint packedColor = pixel_data[index];
    imageStore(o_texture, texel_pos, uvec4(packedColor));
}
)";

void compileShader(GLuint shader, const char* source)
{
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    int success;
    char infoLog[512];
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success)
    {
        glGetShaderInfoLog(shader, 512, NULL, infoLog);
        std::cout << "Shader Compilation Error:\n" << infoLog << std::endl;
    }
}

GLuint createShaderProgram(const char* vertex, const char* fragment)
{
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

void initShaders()
{
    GLuint computeShader = glCreateShader(GL_COMPUTE_SHADER);
    compileShader(computeShader, computeShaderSource);
    shaderProgram = glCreateProgram();
    glAttachShader(shaderProgram, computeShader);
    glLinkProgram(shaderProgram);
    glDeleteShader(computeShader);
    renderShaderProgram = createShaderProgram(vertexShaderSource, fragmentShaderSource);
}

void initTexture()
{
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, resWidth, resHeight);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
}

#define COLOR(r, g, b, a) ( \
(b) << 24 | \
(g) << 16 | \
(r) << 8  | \
(a) << 0    \
)
int pitch = resWidth * resHeight;
std::vector<uint32_t> pixels(pitch, COLOR(0, 0, 0, 255)); // Init with white
void initPixelBuffer()
{
	glGenBuffers(1, &pixelBuffer);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer);
	glBufferData(GL_SHADER_STORAGE_BUFFER, pixels.size(), pixels.data(), GL_DYNAMIC_DRAW);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void initQuad()
{
    constexpr float quadVertices[]
	{
        // Positions   Texture coords
        -1.0f, -1.0f,  0.0f, 1.0f,
        1.0f , -1.0f,  1.0f, 1.0f,
        1.0f ,  1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 0.0f
    };

    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
}

void computeDispatch()
{
    glUseProgram(shaderProgram);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pixelBuffer);
    glUniform1i(glGetUniformLocation(shaderProgram, "resWidth"), resWidth);
    glUniform1i(glGetUniformLocation(shaderProgram, "resHeight"), resHeight);
    glDispatchCompute(resWidth / 16, resHeight / 16, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void initOpenGL()
{
    //SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);  
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);

    context = SDL_GL_CreateContext(window);
    if (!context)
    {
        SDL_Log("Failed to create OpenGL context: %s", SDL_GetError());
        exit(-1);
    }

    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress))
    {
        SDL_Log("Failed to initialize GLAD");
        exit(-1);
    }
    initShaders();
    initTexture();
    initPixelBuffer();
    initQuad();
    glViewport(0, 0, resWidth, resHeight);
	glDisable(GL_BLEND);
    //glEnable(GL_BLEND);
    //glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
	glClearColor(0.7f, 0.7f, 0.7f, 1.f);

    SDL_GL_SetSwapInterval(1);
}

void updatePixelBuffer(std::vector<uint32_t>& newPixelData)
{
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, pixelBuffer);
    glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, newPixelData.size(), newPixelData.data());
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

static inline int xeFbConvert(const int resWidth, const int addr) {
  const int y = addr / (resWidth * 4);
  const int x = addr % (resWidth * 4) / 4;
  const uint64_t offset =
      ((((y & ~31) * resWidth) + (x & ~31) * 32) +
       (((x & 3) + ((y & 1) << 2) + ((x & 28) << 1) + ((y & 30) << 5)) ^
        ((y & 8) << 2))) *
      4;
  return offset;
}

#define XE_PIXEL_TO_STD_ADDR(x, y) (y * resWidth + x) * 4
#define XE_PIXEL_TO_XE_ADDR(x, y)                                              \
  xeFbConvert(resWidth, XE_PIXEL_TO_STD_ADDR(x, y))

void ConvertFramebufferCPU(std::vector<uint32_t>& outputBuffer, uint8_t* xeFramebuffer, int resWidth, int resHeight)
{
    int stdPixPos = 0;
    int xePixPos = 0;
    for (int y = 0; y < resHeight; y++)
    {
        for (int x = 0; x < resWidth; x++)
        {       
            stdPixPos = XE_PIXEL_TO_STD_ADDR(x, y);
            xePixPos = XE_PIXEL_TO_XE_ADDR(x, y);
            uint8_t b = xeFramebuffer[xePixPos];
            uint8_t r = xeFramebuffer[xePixPos + 1];
            uint8_t g = xeFramebuffer[xePixPos + 2];
            uint8_t a = xeFramebuffer[xePixPos + 3];
            outputBuffer[stdPixPos / 4] = COLOR(r, g, b, 255);
        }
    }
}

std::unique_ptr<uint8_t[]> buffer = std::make_unique<uint8_t[]>(pitch * 4);
void render()
{
    uint8_t* fbPointer = buffer.get();
    ConvertFramebufferCPU(pixels, fbPointer, resWidth, resHeight);
	updatePixelBuffer(pixels);
    computeDispatch();
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(renderShaderProgram);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(quadVAO);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    SDL_GL_SwapWindow(window);
}

int main()
{
    std::cout << "Width: " << resWidth << std::endl;
    std::cout << "Height: " << resHeight << std::endl;
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL;
    if (initSdl("OpenGL Window", resWidth, resHeight, flags) != 0)
        return 1;
        
    std::ifstream f("fbmem.bin", std::ios::in | std::ios::binary);
    if (!f)
    {
        std::cout << "Failed to open framebuffer dump" << std::endl;
    }
    else
    {
        f.read(reinterpret_cast<char*>(buffer.get()), pitch * 4);
    }
    f.close();

    initOpenGL();
    
    bool running = true;
    SDL_Event event;
    while (running)
    {
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
                running = false;
        }
        
        render();
    }
    
    SDL_GL_DestroyContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
	return 0;
}