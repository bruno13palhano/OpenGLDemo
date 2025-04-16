#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "OpenGLDemo", __VA_ARGS__))

// Shader sources
const char* vertexShaderSource = R"(
    uniform mat4 uTransform;
    attribute vec4 aPosition;
    void main() {
        gl_Position = uTransform * aPosition;
    }
)";

const char* fragmentShaderSource = R"(
    precision mediump float;
    void main() {
        gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); // Red triangle
    }
)";

const char* fragmentShaderSource2 = R"(
    precision mediump float;
    void main() {
        gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); // Green triangle
    }
)";

// Vertex struct
struct Vertex {
    float x, y, z;
};

class Triangle {
public:
    GLuint vbo; // Vertex Buffer Object
    int vertexCount;
    Vertex vertices[3];
    GLuint program; // Own program for unique colors

    Triangle(float offsetX, float scale, const char* fragShader, const char* name)
        : vertexCount(3), program(0) {
        vertices[0] = {scale * (0.0f + offsetX),  scale * 0.5f, 0.0f}; // Top
        vertices[1] = {scale * (-0.5f + offsetX), scale * -0.5f, 0.0f}; // Bottom-left
        vertices[2] = {scale * (0.5f + offsetX),  scale * -0.5f, 0.0f}; // Bottom-right

        // Log vertices once
        for (int i = 0; i < vertexCount; i++) {
            LOGI("%s Vertex %d: x=%f, y=%f, z=%f",name, i, vertices[i].x, vertices[i].y, vertices[i].z);
        }

        swapVertices(2, 0);
        vbo = 0;
    }

    void init(const char* vertShader, const char* fragShader) {
        // Compile shaders
        GLuint vertexShader = compileShader(GL_VERTEX_SHADER, vertShader);
        GLuint fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragShader);
        program = glCreateProgram();
        assert(program != 0);
        glAttachShader(program, vertexShader);
        glAttachShader(program, fragmentShader);
        glLinkProgram(program);

        GLint success;
        glGetProgramiv(program, GL_LINK_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetProgramInfoLog(program, 512, nullptr, infoLog);
            LOGI("Program linking failed: %s", infoLog);
        }

        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);

        // Create VBO
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    ~Triangle() {
        if (vbo != 0) {
            glDeleteBuffers(1, &vbo);
        }
        if (program != 0) {
            glDeleteProgram(program);
        }
    }

    void draw(float* matrix) {
        if (vbo == 0 || program == 0) {
            LOGI("Error: VBO or program not initialized");
            return;
        }

        glUseProgram(program);

        // Set rotation matrix
        GLint transformLoc = glGetUniformLocation(program, "uTransform");
        assert(transformLoc != -1);

        glUniformMatrix4fv(transformLoc, 1, GL_FALSE, matrix);

        GLint positionLoc = glGetAttribLocation(program, "aPosition");
        assert(positionLoc != -1);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glVertexAttribPointer(positionLoc, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
        glEnableVertexAttribArray(positionLoc);
        glDrawArrays(GL_TRIANGLES, 0, vertexCount);
        glDisableVertexAttribArray(positionLoc);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

private:
    void swapVertices(int i, int j) {
        Vertex temp = vertices[i];
        vertices[i] = vertices[j];
        vertices[j] = temp;
        LOGI("Swapped vertices %d and %d", i, j);
    }

    GLuint compileShader(GLenum type, const char* source) {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint success;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
        if (!success) {
            char infoLog[512];
            glGetShaderInfoLog(shader, 512, nullptr, infoLog);
            LOGI("%s Shader compilation failed: %s", type == GL_VERTEX_SHADER ? "Vertex" : "Fragment", infoLog);
        }
        return shader;
    }
};

// OpenGL state
struct Engine {
    bool running;
    unsigned int frameCount;
    float scale;
    bool paused;
    unsigned int rotationFrame;
    std::vector<Triangle*> triangles;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
};

// Global engine instance
static struct Engine g_engine;

// Initialize EGL
void initEGL(struct android_app* app) {
    g_engine.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    assert(g_engine.display != EGL_NO_DISPLAY);
    eglInitialize(g_engine.display, nullptr, nullptr);

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
    eglChooseConfig(g_engine.display, configAttributes, &config, 1, &numConfigs);

    g_engine.surface = eglCreateWindowSurface(g_engine.display, config, app->window, nullptr);
    assert(g_engine.surface != EGL_NO_SURFACE);

    const EGLint contextAttributes[] = {
            EGL_CONTEXT_CLIENT_VERSION, 2,
            EGL_NONE
    };
    g_engine.context = eglCreateContext(g_engine.display, config, EGL_NO_CONTEXT, contextAttributes);
    assert(g_engine.context != EGL_NO_CONTEXT);
    eglMakeCurrent(g_engine.display, g_engine.surface, g_engine.surface, g_engine.context);
}

// Initialize OpenGL
void init(struct android_app* app) {
    g_engine.running = true;
    g_engine.frameCount = 0;
    g_engine.scale = 0.5f;
    g_engine.paused = false;
    g_engine.rotationFrame = 0;

    initEGL(app);

    g_engine.triangles.push_back(new Triangle(0.0f, g_engine.scale, fragmentShaderSource, "Triangle1"));
    g_engine.triangles.push_back(new Triangle(1.0f, g_engine.scale, fragmentShaderSource2, "Triangle2"));
    g_engine.triangles.push_back(new Triangle(-1.0f, g_engine.scale, fragmentShaderSource, "Triangle3"));

    for (Triangle* triangle : g_engine.triangles) {
        triangle->init(vertexShaderSource,triangle == g_engine.triangles[0] || triangle == g_engine.triangles[2] ? fragmentShaderSource : fragmentShaderSource2);
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void setRotationMatrix(float* matrix, float theta, float yTranslate) {
    matrix[0] = cosf(theta); matrix[1] = -sinf(theta); matrix[2] = 0.0f; matrix[3] = 0.0f;
    matrix[4] = sinf(theta); matrix[5] = cosf(theta); matrix[6] = 0.0f; matrix[7] = yTranslate;
    matrix[8] = 0.0f; matrix[9] = 0.0f; matrix[10] = 1.0f; matrix[11] = 0.0f;
    matrix[12] = 0.0f; matrix[13] = 0.0f; matrix[14] = 0.0f; matrix[15] = 1.0f;
}

// Render frame
void draw() {
    if (!g_engine.running) return;

    glClear(GL_COLOR_BUFFER_BIT);

    float rotation = g_engine.rotationFrame * 0.01f;
    for (Triangle* triangle : g_engine.triangles) {
        if (triangle != nullptr) {
            float theta = triangle == g_engine.triangles[0] ? rotation : -rotation;
            float matrix[16];
            setRotationMatrix(matrix, theta, triangle == g_engine.triangles[0] ? 0.2f : 0.0f);
            triangle->draw(matrix);
        }
    }

    eglSwapBuffers(g_engine.display, g_engine.surface);

    if (g_engine.frameCount % 600 == 300) g_engine.paused = true;
    if (g_engine.frameCount % 600 == 0 && g_engine.frameCount != 0) g_engine.paused = false;

    if (!g_engine.paused) g_engine.rotationFrame++;

    g_engine.frameCount++;
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
            for (Triangle* triangle : g_engine.triangles) {
                delete triangle;
            }
            g_engine.triangles.clear();
            g_engine.running = false;
            eglDestroyContext(g_engine.display, g_engine.context);
            eglDestroySurface(g_engine.display, g_engine.surface);
            eglTerminate(g_engine.display);
            break;
    }
}

int32_t handle_input(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        if (AMotionEvent_getAction(event) == AMOTION_EVENT_ACTION_DOWN) {
            g_engine.paused = !g_engine.paused;
            LOGI("Touch toggled pause: %d", g_engine.paused);
            return 1;
        }
    }
    return 0;
}

// Main entry point
void android_main(struct android_app* app) {
    app->onAppCmd = handle_cmd;
    app->onInputEvent = handle_input;

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
            LOGI("Frame: %u, Rotation: %2.f, Paused: %d", g_engine.frameCount, g_engine.rotationFrame * 0.01f, g_engine.paused);
            draw();
        }
    }
}