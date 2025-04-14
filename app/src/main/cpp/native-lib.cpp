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

// OpenGL state
struct Engine {
    GLuint program;
    GLint positionLoc;
    bool running;
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

// Initialize OpenGL
void init(struct android_app* app) {
    g_engine.running = true;

    // Set up OpenGL ES context
    EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(display, nullptr, nullptr);

    const EGLint configAttributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_RED_SIZE, 8,
            EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    eglChooseConfig(display, configAttributes, &config, 1, &numConfigs);

    EGLSurface surface = eglCreateWindowSurface(display, config, app->window, nullptr);

    const EGLint contextAttributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes);
    eglMakeCurrent(display, surface, surface, context);

    // Create and link shader program
    GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    g_engine.program = glCreateProgram();
    glAttachShader(g_engine.program, vertexShader);
    glAttachShader(g_engine.program, fragmentShader);
    glLinkProgram(g_engine.program);

    GLint success;
    glGetProgramiv(g_engine.program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(g_engine.program, 512, nullptr, infoLog);
        LOGI("Program linking failed: %s", infoLog);
    }

    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);

    g_engine.positionLoc = glGetAttribLocation(g_engine.program, "aPosition");
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

// Render frame
void draw() {
    if (!g_engine.running) return;

    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(g_engine.program);

    // Triangle vertices (x, y, z)
    GLfloat vertices[] = {
            0.0f,  0.5f, 0.0f,  // Top
            -0.5f, -0.5f, 0.0f,  // Bottom-left
            0.5f, -0.5f, 0.0f   // Bottom-right
    };

    glVertexAttribPointer(g_engine.positionLoc, 3, GL_FLOAT, GL_FALSE, 0, vertices);
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
            draw();
        }
    }
}