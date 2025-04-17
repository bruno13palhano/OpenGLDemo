#include <android_native_app_glue.h>
#include <android/input.h>
#include <android/log.h>
#include <sys/time.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <math.h>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "OpenGLDemo", __VA_ARGS__))

long get_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

struct Matrix4 {
    float m[16];

    Matrix4() { identity(); }

    void identity() {
        for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
    }

    void translate(float x, float y) {
        m[12] += x; m[13] += y;
    }

    void rotate(float angle) {
        float c = cosf(angle), s = sinf(angle);
        float t0 = m[0] * c + m[4] * s, t1 = m[1] * c + m[5] * s;
        m[4] = m[0] * -s + m[4] * c; m[5] = m[1] * -s + m[5] * c;
        m[0] = t0; m[1] = t1;
    }

    void scale(float sx, float sy) {
        m[0] *= sx; m[1] *= sx; m[4] *= sy; m[5] *= sy;
    }
};

struct AppState {
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    int32_t width, height;
    int tap_count;
    int move_count;
    long last_move_ms;
    Matrix4 model;
    float last_x, last_y;
    float last_angle;
    float last_dist;
    bool is_dragging;
    bool is_rotating;
    bool is_scaling;
    GLuint program;
};

void init_opengl(AppState* state, ANativeWindow* window) {
    LOGI("init_opengl started");
    if (!window) {
        LOGI("Native window is null");
        return;
    }

    state->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (state->display == EGL_NO_DISPLAY) {
        LOGI("eglGetDisplay failed: %d", eglGetError());
        return;
    }

    EGLBoolean initResult = eglInitialize(state->display, nullptr, nullptr);
    if (!initResult) {
        LOGI("eglInitialize failed: %d", eglGetError());
        return;
    }

    EGLint configAttributes[] = {
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            EGL_NONE
    };
    EGLConfig config;
    EGLint numConfigs;
    if (!eglChooseConfig(state->display, configAttributes, &config, 1, &numConfigs)) {
        LOGI("eglChooseConfig failed: %d", eglGetError());
        return;
    }
    if (numConfigs == 0) {
        LOGI("No matching EGL config found");
        return;
    }

    EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    state->context = eglCreateContext(state->display, config, EGL_NO_CONTEXT, contextAttributes);
    if (state->context == EGL_NO_CONTEXT) {
        LOGI("eglCreateContext failed: %d", eglGetError());
        return;
    }

    state->surface = eglCreateWindowSurface(state->display, config, window, nullptr);
    if (state->surface == EGL_NO_SURFACE) {
        LOGI("eglCreateWindowSurface failed: %d", eglGetError());
        return;
    }

    if (!eglMakeCurrent(state->display, state->surface, state->surface, state->context)) {
        LOGI("eglMakeCurrent failed: %d", eglGetError());
        return;
    }

    LOGI("EGL initialized: %dx%d", state->width, state->height);
    glViewport(0, 0, state->width, state->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    const char* vertexShaderSource = R"(
        attribute vec2 aPosition;
        uniform mat4 uModel;
        void main() {
            gl_Position = uModel * vec4(aPosition, 0.0, 1.0);
        }
    )";
    GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
    glCompileShader(vertexShader);
    GLint status;
    glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar log[512];
        glGetShaderInfoLog(vertexShader, 512, nullptr, log);
        LOGI("Vertex shader error: %s", log);
        return;
    }

    const char* fragmentShaderSource = R"(
        precision mediump float;
        void main() {
            gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0);
        }
    )";
    GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
    glCompileShader(fragmentShader);
    glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar log[512];
        glGetShaderInfoLog(fragmentShader, 512, nullptr, log);
        LOGI("Fragment shader error: %s", log);
        return;
    }

    state->program = glCreateProgram();
    glAttachShader(state->program, vertexShader);
    glAttachShader(state->program, fragmentShader);
    glLinkProgram(state->program);
    glGetProgramiv(state->program, GL_LINK_STATUS, &status);
    if (status == GL_FALSE) {
        GLchar log[512];
        glGetProgramInfoLog(state->program, 512, nullptr, log);
        LOGI("Program link error: %s", log);
        return;
    }
    glUseProgram(state->program);

    GLfloat vertices[] = {
            0.0f,  0.5f,
            -0.5f, -0.5f,
            0.5f, -0.5f
    };
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    GLint positionLoc = glGetAttribLocation(state->program, "aPosition");
    glEnableVertexAttribArray(positionLoc);
    glVertexAttribPointer(positionLoc, 2, GL_FLOAT, GL_FALSE, 0, 0);
    LOGI("OpenGL initialized successfully");
}

void render(AppState* state) {
    long start = get_ms();
    glClear(GL_COLOR_BUFFER_BIT);
    GLint  modelLoc = glGetUniformLocation(state->program, "uModel");
    glUniformMatrix4fv(modelLoc, 1, GL_FALSE, state->model.m);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    eglSwapBuffers(state->display, state->surface);
    LOGI("Rendering frame, took %ld ms", get_ms() - start);
}

float get_distance(float x1, float y1, float x2, float y2) {
    return sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 -y1));
}

float get_angle(float x1, float y1, float x2, float y2) {
    return atan2f(y2 - y1, x2 - x1);
}

int32_t handle_input(struct android_app* app, AInputEvent* event) {
    AppState* state = (AppState*)app->userData;
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        float x = AMotionEvent_getX(event, 0);
        float y = AMotionEvent_getY(event, 0);
        int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        int32_t pointer_count = AMotionEvent_getPointerCount(event);

        if (pointer_count == 1) { // Drag
            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN:
                    state->tap_count++;
                    state->is_dragging = true;
                    state->last_x = x;
                    state->last_y = y;
                    LOGI("Touch down at (%f, %f), Tap count: %d", x, y, state->tap_count);
                    return 1;
                case AMOTION_EVENT_ACTION_MOVE: {
                    long now_ms = get_ms();
                    if (state->move_count % 3 == 0 && now_ms - state->last_move_ms >= 50) {
                        float dx = (x - state->last_x) / state->width * 2.0f;
                        float dy = -(y - state->last_y) / state->height * 2.0f;
                        state->model.translate(dx, dy);
                        LOGI("Touch mode at (%f, %f), Move count: %d, dx: %f, dy: %f", x, y, state->move_count, dx, dy);
                        state->last_move_ms = now_ms;
                    } else {
                        LOGI("Suppressed move event %d", state->move_count);
                    }
                    state->last_x = x;
                    state->last_y = y;
                    state->move_count++;
                    return 1;
                }
                case AMOTION_EVENT_ACTION_UP:
                    state->is_dragging = false;
                    LOGI("Touch up at (%f, %f)", x, y);
                    return 1;
                case AMOTION_EVENT_ACTION_CANCEL:
                    state->is_dragging = false;
                    LOGI("Touch cancelled at (%f, %f)", x, y);
                    return 1;
            }
        } else if (pointer_count == 2) { // Rotate and scale
            float x2 = AMotionEvent_getX(event, 1);
            float y2 = AMotionEvent_getY(event, 1);
            switch (action) {
                case AMOTION_EVENT_ACTION_POINTER_DOWN:
                    state->is_rotating = true;
                    state->is_scaling = true;
                    state->last_angle = get_angle(x, y, x2, y2);
                    state->last_dist = get_distance(x, y, x2, y2);
                    LOGI("Pointer down at (%f, %f), Pointer index: 1", x2, y2);
                    return 1;
                case AMOTION_EVENT_ACTION_MOVE: {
                    long now_ms = get_ms();
                    if (state->move_count % 3 == 0 && now_ms - state->last_move_ms >= 50) {
                        float angle = get_angle(x, y, x2, y2);
                        float dist = get_distance(x, y, x2, y2);
                        float d_angle = angle - state->last_angle;
                        float scale = dist / state->last_dist;
                        state->model.rotate(d_angle);
                        state->model.scale(scale, scale);
                        LOGI("Two-finger move: angle %f, scale %f", d_angle, scale);
                        state->last_angle = angle;
                        state->last_dist = dist;
                        state->last_move_ms = now_ms;
                    } else {
                        LOGI("Suppressed two-finger move %d", state->move_count);
                    }
                    state->move_count++;
                    return 1;
                }
                case AMOTION_EVENT_ACTION_POINTER_UP:
                    state->is_rotating = false;
                    state->is_scaling = false;
                    LOGI("Pointer up at (%f, %f), Pointer index: 1", x2, y2);
                    return 1;
                case AMOTION_EVENT_ACTION_CANCEL:
                    state->is_rotating = false;
                    state->is_scaling = false;
                    LOGI("Touch cancelled at (%f, %f)", x, y);
                    return 1;
            }
        }
        if (action == AMOTION_EVENT_ACTION_HOVER_ENTER) {
            LOGI("Hover enter at (%f, %f)", x, y);
            return 1;
        } else if (action == AMOTION_EVENT_ACTION_HOVER_MOVE) {
            LOGI("Hover move at (%f, %f)", x, y);
            return 1;
        } else if (action == AMOTION_EVENT_ACTION_HOVER_EXIT) {
            LOGI("Hover exit at (%f, %f)", x, y);
            return 1;
        }
        LOGI("Unhandled motion action: %d", action);
        return 0;
    }
    LOGI("Unhandled event type: %d", AInputEvent_getType(event));
    return 0;
}

void handle_cmd(struct android_app* app, int32_t cmd) {
    AppState* state = (AppState*)app->userData;
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            state->width = ANativeWindow_getWidth(app->window);
            state->height = ANativeWindow_getHeight(app->window);
            LOGI("Window size: %d x %d", state->width, state->height);
            init_opengl(state, app->window);
            LOGI("Window initialized!");
            break;
        case APP_CMD_GAINED_FOCUS:
            LOGI("App gained focus!");
            break;
        case APP_CMD_TERM_WINDOW:
            if (state->surface != EGL_NO_SURFACE) {
                eglDestroySurface(state->display, state->surface);
                state->surface = EGL_NO_SURFACE;
            }
            if (state->context != EGL_NO_CONTEXT) {
                eglDestroyContext(state->display, state->context);
                state->context = EGL_NO_CONTEXT;
            }
            if (state->display != EGL_NO_DISPLAY) {
                eglTerminate(state->display);
                state->display = EGL_NO_DISPLAY;
            }
            LOGI("Window terminated!");
            break;
        case APP_CMD_INPUT_CHANGED:
            LOGI("Input queue changed!");
            break;
    }
}

void android_main(struct android_app* app) {
    LOGI("android_main started");
    AppState state = {0};
    app->userData = &state;
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;
    int events;
    struct android_poll_source* source;
    while (1) {
        int result = ALooper_pollOnce(10, nullptr, &events, (void**)&source);
        if (result >= 0) {
            if (source != nullptr) {
                source->process(app, source);
            }
            if (app->inputQueue != nullptr) {
                AInputEvent* event = nullptr;
                while (AInputQueue_getEvent(app->inputQueue, &event) >= 0) {
                    if (AInputQueue_preDispatchEvent(app->inputQueue, event)) {
                        continue;
                    }
                    int32_t handled = handle_input(app, event);
                    AInputQueue_finishEvent(app->inputQueue, event, handled);
                    LOGI("Input queue has events: %d", AInputQueue_hasEvents(app->inputQueue));
                }
            }
            if (app->window != nullptr && state.display != EGL_NO_DISPLAY && state.surface != EGL_NO_SURFACE) {
                render(&state);
            }
            if (app->destroyRequested) {
                LOGI("App destroyed!");
                break;
            }
        } else if (result != ALOOPER_POLL_TIMEOUT) {
            LOGI("Poll error: %d", result);
        }
    }
}