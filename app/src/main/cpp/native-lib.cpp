#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cstring>
#include <android/log.h>
#include <android/input.h>
#include <cmath>

#define DEG2RAD(x) ((x) * 3.14159265f / 180.0f)

struct Engine {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    GLuint program = 0;
    GLuint vbo = 0;
    float angle = 0.0f;
    float lastX = 0.0f;
    bool isTouching = false;
};

const char* vertexShaderSource = R"(
    uniform mat4 uMVP;
    attribute vec4 a_Position;
    void main() {
        gl_Position = uMVP * a_Position;
    }
)";

const char* fragmentShaderSource = R"(
    precision mediump float;
    void main() {
        gl_FragColor = vec4(1.0, 0.4, 0.0, 1.0); // Orange
    }
)";

GLuint loadShader(GLenum type, const char* src) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        char log[512];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        __android_log_print(ANDROID_LOG_ERROR, "Triangle", "Shader compile failed: %s", log);
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

GLuint createProgram() {
    GLuint vert = loadShader(GL_VERTEX_SHADER, vertexShaderSource);
    GLuint frag = loadShader(GL_FRAGMENT_SHADER, fragmentShaderSource);
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glBindAttribLocation(program, 0, "a_Position");
    glLinkProgram(program);
    return program;
}

void engine_init_display(Engine* engine, ANativeWindow* window) {
    const EGLint configAttribs[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_BLUE_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_RED_SIZE, 8,
            EGL_NONE
    };

    EGLint format, numConfigs;
    EGLConfig config;
    engine->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    eglInitialize(engine->display, nullptr, nullptr);
    eglChooseConfig(engine->display, configAttribs, &config, 1, &numConfigs);
    eglGetConfigAttrib(engine->display, config, EGL_NATIVE_VISUAL_ID, &format);
    ANativeWindow_setBuffersGeometry(window, 0, 0, format);
    engine->surface = eglCreateWindowSurface(engine->display, config, window, nullptr);
    const EGLint ctxAttribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    engine->context = eglCreateContext(engine->display, config, nullptr, ctxAttribs);
    eglMakeCurrent(engine->display, engine->surface, engine->surface, engine->context);

    eglQuerySurface(engine->display, engine->surface, EGL_WIDTH, &engine->width);
    eglQuerySurface(engine->display, engine->surface, EGL_HEIGHT, &engine->height);
    glViewport(0, 0, engine->width, engine->height);

    // Set up triangle
    float vertices[] = {
            0.0f,  0.5f, // top
            -0.5f, -0.5f, // bottom left
            0.5f, -0.5f  // bottom right
    };

    glGenBuffers(1, &engine->vbo);
    glBindBuffer(GL_ARRAY_BUFFER, engine->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    engine->program = createProgram();
}

void engine_draw_frame(Engine* engine) {
    if (engine->display == nullptr) return;

    glClearColor(0.0f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    glUseProgram(engine->program);

    if (engine->angle > 360.0f) engine->angle -= 360.0f;
    if (engine->angle < 0.0f) engine->angle += 360.0f;

    float rad = DEG2RAD(engine->angle);
    float cosA = cosf(rad);
    float sinA = sinf(rad);
    float mvp[16] = {
            cosA, sinA, 0, 0,
            -sinA, cosA, 0, 0,
            0, 0, 1, 0,
            0, 0, 0, 1
    };

    GLint mvpLoc = glGetUniformLocation(engine->program, "uMVP");
    glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

    glBindBuffer(GL_ARRAY_BUFFER, engine->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDisableVertexAttribArray(0);

    eglSwapBuffers(engine->display, engine->surface);
}

void engine_term_display(Engine* engine) {
    if (engine->vbo) glDeleteBuffers(1, &engine->vbo);
    if (engine->program) glDeleteProgram(engine->program);
    if (engine->display != EGL_NO_DISPLAY) {
        eglMakeCurrent(engine->display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (engine->context != EGL_NO_CONTEXT)
            eglDestroyContext(engine->display, engine->context);
        if (engine->surface != EGL_NO_SURFACE)
            eglDestroySurface(engine->display, engine->surface);
        eglTerminate(engine->display);
    }
    engine->display = EGL_NO_DISPLAY;
    engine->context = EGL_NO_CONTEXT;
    engine->surface = EGL_NO_SURFACE;
}

int32_t handle_input(android_app* app, AInputEvent* event) {
    Engine* engine = (Engine*)app->userData;

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        float x = AMotionEvent_getX(event, 0);

        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
                engine->isTouching = true;
                engine->lastX = x;
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                if (engine->isTouching) {
                    float dx = x - engine->lastX;
                    engine->angle += dx;  // Simple mapping: pixels to degrees
                    engine->lastX = x;
                }
                break;
            case AMOTION_EVENT_ACTION_UP:
                engine->isTouching = false;
                break;
        }
        return 1;
    }
    return 0;
}

void handle_cmd(android_app* app, int32_t cmd) {
    Engine* engine = (Engine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) engine_init_display(engine, app->window);
            break;
        case APP_CMD_TERM_WINDOW:
            engine_term_display(engine);
            break;
    }
}

void android_main(struct android_app* app) {
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

    Engine engine = {};
    app->userData = &engine;

    while (true) {
        int events;
        android_poll_source* source;
        while (ALooper_pollOnce(0, nullptr, &events, (void**)&source) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                engine_term_display(&engine);
                return;
            }
        }
        engine_draw_frame(&engine);
    }
}
