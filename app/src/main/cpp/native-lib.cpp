#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <string>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "OpenGLDemo", __VA_ARGS__))

// Shader sources
const char* vertexShaderSource = R"(
    attribute vec4 aPosition;
    void main() {
        gl_Position = aPosition;
    }
)";

const char* fragmentShaderSource = R"(
    precision mediump float;
    void main() {
        gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red triangle
    }
)";

// Vertex struct
struct Vertex {
    float x, y, z;
};

// OpenGL state
struct Engine {
    GLuint program;
    GLint positionLoc;
    bool running;
    int frameCount;
    float scale;
};

// Global engine instance
static struct Engine g_engine;

// Compile shader
GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        LOGI("Shader compilation failed: %s", infoLog);
    }
    return shader;
}

// Initialize EGL
void initEGL(struct android_app* app, EGLDisplay* display, EGLSurface* surface, EGLContext* context) {
    *display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(*display, nullptr, nullptr);

    const EGLint configAttributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_NONE
    };
    EGLConfig config;
    EGLint  numConfigs;
    eglChooseConfig(*display, configAttributes, &config, 1, &numConfigs);

    *surface = eglCreateWindowSurface(*display, config, app->window, nullptr);

    const EGLint contextAttributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    *context = eglCreateContext(*display, config, EGL_NO_CONTEXT, contextAttributes);
    eglMakeCurrent(*display, *surface, *surface, *context);
}

// Initialize shaders
void initShaders(GLuint* program, GLint* positionLoc) {
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    *program = glCreateProgram();
    glAttachShader(*program, vertexShader);
    glAttachShader(*program, fragmentShader);
    glLinkProgram(*program);

    GLint success;
    glGetProgramiv(*program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(*program, 512, nullptr, infoLog);
        LOGI("Program linking failed: %s", infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    *positionLoc = glGetAttribLocation(*program, "aPosition");
}

// Initialize OpenGL
void init(struct android_app* app) {
    g_engine.running = true;
    g_engine.frameCount = 0;
    g_engine.scale = 1.0f;

    EGLDisplay  display;
    EGLSurface  surface;
    EGLContext  context;
    initEGL(app, &display, &surface, &context);
    initShaders(&g_engine.program, &g_engine.positionLoc);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void printVertex(const Vertex& v) {
    LOGI("Vertex: x=%f, y=%f, z=%f", v.x, v.y, v.z);
}

// Render frame
void draw() {
    if (!g_engine.running) return;

    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(g_engine.program);

    // Triangle vertices (x, y, z)
    Vertex vertices[] = {
            {0.0f,  0.5f, 0.0f},  // Top
            {-0.5f, -0.5f, 0.0f},  // Bottom-left
            {0.5f, -0.5f, 0.0f}   // Bottom-right
    };

    for(int i = 0; i < 3; i++) {
        printVertex(vertices[i]);
    }

    glVertexAttribPointer(g_engine.positionLoc, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), vertices);
    glEnableVertexAttribArray(g_engine.positionLoc);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(g_engine.positionLoc);

    // Swap buffers (handled by eglSwapBuffers in a real app, simplified here)
    EGLDisplay display = eglGetCurrentDisplay();
    EGLSurface surface = eglGetCurrentSurface(EGL_DRAW);
    eglSwapBuffers(display, surface);
}

// Handle app events
void handle_cmd(struct android_app* app, int32_t cmd) {
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window != nullptr) {
                init(app);
                draw();
            }
            break;
        case APP_CMD_TERM_WINDOW:
            g_engine.running = false;
            glDeleteProgram(g_engine.program);
            break;
    }
}

// Main entry point
void android_main(struct android_app* app) {
    app->onAppCmd = handle_cmd;

    int events;
    struct android_poll_source* source;
    while (true) {
        // Use ALooper_pollOnce with a timeout of 0 to avoid blocking
        int ident = ALooper_pollOnce(0, nullptr, &events, (void**)&source);
        if (ident >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->destroyRequested != 0) {
                return;
            }
        }
        if (g_engine.running) {
            LOGI("Frame: %d, Scale: %f", g_engine.frameCount++, g_engine.scale);
            draw();
        }
    }
}