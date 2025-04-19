#include <android_native_app_glue.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <cstring>
#include <android/log.h>
#include <android/input.h>
#include <cmath>
#include <vector>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "OpenGLDemo", __VA_ARGS__))
#define DEG2RAD(x) ((x) * 3.14159265f / 180.0f)

class Triangle {
public:
    GLuint vbo = 0;
    float offsetX = 0.0f;
    float angle = 0.0f;

    explicit Triangle(float offsetX, float* colors): offsetX(offsetX) {
        // Each vertex: x, y, r, g, b, a
        float data[] = {
                0.0f + offsetX, 0.5f, colors[0], colors[1], colors[2], 1.0f,
                -0.5f + offsetX, -0.5f, colors[3], colors[4], colors[5], 1.0f,
                0.5f + offsetX, -0.5f, colors[6], colors[7], colors[8], 1.0f
        };

        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(data), data, GL_DYNAMIC_DRAW);
    }

    void updateColor(float r, float g, float b) {
        float newData[] = {
            0.0f + offsetX, 0.5f, r, g, b, 1.0f,
            -0.5f + offsetX, -0.5f, r * 0.5f, g * 0.5f, b * 0.5f, 1.0f,
            0.5f + offsetX, -0.5f, r * 0.25f, g * 0.25f, b * 0.25f, 1.0f
        };
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferSubData(GL_ARRAY_BUFFER, 0, sizeof(newData), newData);
    }

    void draw(GLuint program) {
        float rad = DEG2RAD(angle);
        float cosA = cosf(rad);
        float sinA = sinf(rad);
        float mvp[16] = {
                cosA, sinA, 0, 0,
                -sinA, cosA, 0, 0,
                0,    0, 1, 0,
                0,   0, 0, 1
        };

        GLint mvpLoc = glGetUniformLocation(program, "uMVP");
        glUniformMatrix4fv(mvpLoc, 1, GL_FALSE, mvp);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);

        GLint posLoc = glGetAttribLocation(program, "a_Position");
        GLint colorLoc = glGetAttribLocation(program, "a_Color");

        glEnableVertexAttribArray(posLoc);
        glVertexAttribPointer(posLoc, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)0);

        glEnableVertexAttribArray(colorLoc);
        glVertexAttribPointer(colorLoc, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 6, (void*)(sizeof(float) * 2));

        glDrawArrays(GL_TRIANGLES, 0, 3);

        glDisableVertexAttribArray(posLoc);
        glDisableVertexAttribArray(colorLoc);
    }

    ~Triangle() {
        if (vbo) glDeleteBuffers(1, &vbo);
    }
};

struct Engine {
    EGLDisplay display = EGL_NO_DISPLAY;
    EGLSurface surface = EGL_NO_SURFACE;
    EGLContext context = EGL_NO_CONTEXT;
    int width = 0;
    int height = 0;
    GLuint program = 0;
    std::vector<Triangle*> triangles;
};

const char* vertexShaderSource = R"(
    uniform mat4 uMVP;
    attribute vec4 a_Position;
    attribute vec4 a_Color;
    varying vec4 v_Color;

    void main() {
        v_Color = a_Color;
        gl_Position = uMVP * a_Position;
    }
)";

const char* fragmentShaderSource = R"(
    precision mediump float;
    varying vec4 v_Color;

    void main() {
        gl_FragColor = v_Color;
    }
)";

const char* fragmentShaderSource2 = R"(
    precision mediump float;

    void main() {
        gl_FragColor = vec4(1.0, 1.0, 0.0, 1.0);
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

GLuint createProgram(const char* vertexShaderSrc, const char* fragmentShaderSrc) {
    GLuint vert = loadShader(GL_VERTEX_SHADER, vertexShaderSrc);
    GLuint frag = loadShader(GL_FRAGMENT_SHADER, fragmentShaderSrc);
    GLuint program = glCreateProgram();
    glAttachShader(program, vert);
    glAttachShader(program, frag);
    glBindAttribLocation(program, 0, "a_Position");
    glBindAttribLocation(program, 1, "a_Color");
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

    engine->program = createProgram(vertexShaderSource, fragmentShaderSource);

    float colors1[] = {
            1.0f, 0.0f, 0.0f, // Top vertex
            0.0f, 1.0f, 0.0f, // Bottom left
            0.0f, 0.0f, 1.0f  // Bottom right
    };

    float colors2[] = {
            1.0f, 1.0f, 0.0f,
            0.0f, 1.0f, 1.0f,
            0.0f, 0.5f, 1.0f
    };

    float colors3[] = {
            1.0f, 1.0f, 1.0f,
            0.5f, 0.5f, 0.5f,
            0.0f, 0.0f, 0.0f
    };

    engine->triangles.push_back(new Triangle(-0.8f, colors1));
    engine->triangles.push_back(new Triangle(0.0f, colors2));
    engine->triangles.push_back(new Triangle(0.8f, colors3));
}

void engine_draw_frame(Engine* engine) {
    if (engine->display == nullptr) return;

    glClearColor(0.0f, 0.0f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(engine->program);

    for (auto* triangle : engine->triangles) {
        triangle->angle += 1.0f; // Animate rotation
        triangle->draw(engine->program);
    }

    eglSwapBuffers(engine->display, engine->surface);
}

void engine_term_display(Engine* engine) {
    for (auto* triangle : engine->triangles) {
        delete triangle;
    }
    engine->triangles.clear();

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

    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION &&
        AMotionEvent_getAction(event) == AMOTION_EVENT_ACTION_DOWN) {

        float x = AMotionEvent_getX(event, 0);
        float width = engine->width;

        // Convert x into hue (0.0 to 1.0)
        float hue = x / width;

        static int triangleIndex = 0;
        triangleIndex = (triangleIndex + 1) % engine->triangles.size();

        // Convert hue to RGB
        float r, g, b;
        float h = hue * 6.0f;
        float c = 1.0f;
        float xcol = c * (1.0f - fabs(fmod(h, 2.0f) - 1.0f));
        if (h < 1)      { r = c; g = xcol; b = 0; }
        else if (h < 2) { r = xcol; g = c; b = 0; }
        else if (h < 3) { r = 0; g = c; b = xcol; }
        else if (h < 4) { r = 0; g = xcol; b = c; }
        else if (h < 5) { r = xcol; g = 0; b = c; }
        else            { r = c; g = 0; b = xcol; }

        engine->triangles[triangleIndex]->updateColor(r, g, b);

        LOGI("handle_input: Touch at x=%f, updated triangle %d with color RGB(%f, %f, %f)", x, triangleIndex, r, g, b);

        return 1; // Event handled
    }

    return 0; // Event not handled
}

void handle_cmd(android_app* app, int32_t cmd) {
    Engine* engine = (Engine*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            if (app->window) {
                engine_init_display(engine, app->window);
            }
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

    int events;
    android_poll_source* source;
    // Pick triangle index (e.. rotate between 0, 1, 2)
    static int triangleIndex = 0;

    while (true) {
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
