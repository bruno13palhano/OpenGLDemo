#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <android/log.h>
#include <android_native_app_glue.h>
#include <string>
#include <vector>
#include <cassert>
#include <cmath>
#include <memory>

#define LOGI(...) ((void)__android_log_print(ANDROID_LOG_INFO, "OpenGLDemo", __VA_ARGS__))

// Shader sources
const char* vertexShaderSource = R"(
    uniform mat4 uTransform;
    attribute vec4 aPosition;
    attribute vec2 aTexCoord;
    varying vec2 vTexCoord;
    void main() {
        gl_Position = uTransform * aPosition;
        vTexCoord = aTexCoord;
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
    uniform sampler2D uTexture;
    varying  vec2 vTexCoord;
    void main() {
        gl_FragColor = texture2D(uTexture, vTexCoord);
        if (gl_FragColor.a < 0.1) gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0); // Green fallback
    }
)";

const char* fragmentShaderSource3 = R"(
    precision mediump float;
    void main() {
        gl_FragColor = vec4(0.0, 0.0, 1.0, 1.0); // Blue
    }
)";

// Vertex struct
struct Vertex {
    float x, y, z;
    float u, v; // Texture coordinates
};

class Triangle {
public:
    GLuint vbo; // Vertex Buffer Object
    int vertexCount;
    Vertex vertices[3];
    GLuint program; // Own program for unique colors

    Triangle(float offsetX, float scale, const char* fragShader, const char* name)
        : vertexCount(3), program(0) {
        vertices[0] = {scale * (0.0f + offsetX),  scale * 0.5f, 0.0f, 0.0f, 0.0f}; // Top
        vertices[1] = {scale * (-0.5f + offsetX), scale * -0.5f, 0.0f, 0.0f, 1.0f}; // Bottom-left
        vertices[2] = {scale * (0.5f + offsetX),  scale * -0.5f, 0.0f, 1.0f, 1.0f}; // Bottom-right

        // Log vertices once
        for (int i = 0; i < vertexCount; i++) {
            LOGI("%s Vertex %d: x=%f, y=%f, z=%f",name, i, vertices[i].x, vertices[i].y, vertices[i].z);
        }

        swapVertices(2, 0);
        vbo = 0;
    }

    void init(const char* vertShader, const char* fragShader);

    ~Triangle() {
        if (vbo != 0) {
            glDeleteBuffers(1, &vbo);
        }
        if (program != 0) {
            glDeleteProgram(program);
        }
    }

    void draw(float* matrix);

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
    std::vector<std::unique_ptr<Triangle>> triangles;
    EGLDisplay display;
    EGLSurface surface;
    EGLContext context;
    GLuint texture;
    float zoom;
    float targetZoom;
    float panX, panY;
    float lastPinchDistance;
};

// Global engine instance
static struct Engine g_engine;


void Triangle::init(const char *vertShader, const char *fragShader) {
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

void Triangle::draw(float *matrix) {
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

    GLint  textCoordLoc = glGetAttribLocation(program, "aTexCoord");
    if (textCoordLoc != -1) {
        glVertexAttribPointer(textCoordLoc, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float )));
        glEnableVertexAttribArray(textCoordLoc);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, g_engine.texture);
        GLint textureLoc = glGetUniformLocation(program, "uTexture");
        glUniform1i(textureLoc, 0);
    }

    glDrawArrays(GL_TRIANGLES, 0, vertexCount);
    glDisableVertexAttribArray(positionLoc);
    if (textCoordLoc != -1) glDisableVertexAttribArray(textCoordLoc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

// Create a simple 2x2 texture
void createTexture() {
    // 2x2 gradient: red (top-left), green (others)
    unsigned char pixels[] = {
            255, 0, 0, 255, // Red
            0, 255, 0, 255, // Green
            0, 255, 0, 255, // Green
            0, 255, 0, 255 // Green
    };

    glGenTextures(1, &g_engine.texture);
    glBindTexture(GL_TEXTURE_2D, g_engine.texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
}

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
    g_engine.zoom = 1.0f;
    g_engine.panX = 0.0f;
    g_engine.panY = 0.0f;
    g_engine.targetZoom = 0.0f;
    g_engine.lastPinchDistance = 0.0f;

    initEGL(app);

    g_engine.triangles.push_back(std::make_unique<Triangle>(0.0f, g_engine.scale, fragmentShaderSource, "Triangle1"));
    g_engine.triangles.push_back(std::make_unique<Triangle>(1.0f, g_engine.scale, fragmentShaderSource2, "Triangle2"));
    g_engine.triangles.push_back(std::make_unique<Triangle>(-1.0f, g_engine.scale, fragmentShaderSource, "Triangle3"));
    g_engine.triangles.push_back(std::make_unique<Triangle>(0.5f, g_engine.scale, fragmentShaderSource3, "Triangle4"));

    for (const auto& triangle : g_engine.triangles) {
        triangle->init(vertexShaderSource, triangle.get() == g_engine.triangles[3].get() ? fragmentShaderSource3 : triangle.get() == g_engine.triangles[0].get() || triangle.get() == g_engine.triangles[2].get() ? fragmentShaderSource : fragmentShaderSource2);
    }

    createTexture();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void multiplyMatrix(float* result, const float* a, const float* b) {
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            result[i * 4 + j] = a[i * 4 + 0] * b[0 * 4 + j] +
                                a[i * 4 + 1] * b[1 * 4 + j] +
                                a[i * 4 + 2] * b[2 * 4 + j] +
                                a[i * 4 + 3] * b[3 * 4 + j];
        }
    }
}

// Render frame
void draw() {
    if (!g_engine.running) return;

    glClear(GL_COLOR_BUFFER_BIT);

    float rotation = g_engine.rotationFrame * 0.01f;
    float cameraMatrix[16] = {
            g_engine.zoom, 0.0f, 0.0f, g_engine.panX,
            0.0f, g_engine.zoom, 0.0f, g_engine.panY,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
    };

    for (const auto& triangle : g_engine.triangles) {
        if (triangle) {
            float theta = triangle.get() == g_engine.triangles[0].get() ? rotation : -rotation;
            float modelMatrix[16] = {
                    cosf(theta), -sinf(theta), 0.0f, 0.0f,
                    sinf(theta), cosf(theta), 0.0f, triangle.get() == g_engine.triangles[0].get() ? 0.2f : 0.0f,
                    0.0f, 0.0f, 1.0f, 0.0f,
                    0.0f, 0.0f, 0.0f, 1.0f
            };
            float finalMatrix[16];
            multiplyMatrix(finalMatrix, cameraMatrix, modelMatrix);
            triangle->draw(finalMatrix);
        }
    }

    eglSwapBuffers(g_engine.display, g_engine.surface);

    if (g_engine.frameCount % 600 == 300) g_engine.paused = true;
    if (g_engine.frameCount % 600 == 0 && g_engine.frameCount != 0) g_engine.paused = false;

    if (!g_engine.paused) g_engine.rotationFrame++;

    g_engine.zoom += (g_engine.targetZoom - g_engine.zoom) * 0.1f;

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
            g_engine.triangles.clear();
            if (g_engine.texture != 0) {
                glDeleteTextures(1, &g_engine.texture);
                g_engine.texture = 0;
            }
            g_engine.running = false;
            eglDestroyContext(g_engine.display, g_engine.context);
            eglDestroySurface(g_engine.display, g_engine.surface);
            eglTerminate(g_engine.display);
            break;
    }
}

int32_t handle_input(struct android_app* app, AInputEvent* event) {
    if (AInputEvent_getType(event) == AINPUT_EVENT_TYPE_MOTION) {
        int32_t  action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        if (action == AMOTION_EVENT_ACTION_DOWN) {
            g_engine.paused = !g_engine.paused;
            LOGI("Touch toggled pause: %d", g_engine.paused);
            return 1;
        }
        if (action == AMOTION_EVENT_ACTION_POINTER_DOWN || action == AMOTION_EVENT_ACTION_MOVE) {
            int pointerCount = AMotionEvent_getPointerCount(event);
            if (pointerCount >= 2) {
                float x0 = AMotionEvent_getX(event, 0);
                float y0 = AMotionEvent_getY(event, 0);
                float x1 = AMotionEvent_getX(event, 1);
                float y1 = AMotionEvent_getY(event, 1);
                float distance = sqrtf((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0));
                float zoomDelta = (distance - g_engine.lastPinchDistance) * 0.001f;
                g_engine.targetZoom += zoomDelta;
                g_engine.lastPinchDistance = distance;
                if (g_engine.targetZoom < 1.0f) g_engine.targetZoom = 1.0f;
                if (g_engine.targetZoom > 2.0f) g_engine.targetZoom = 2.0f;
                LOGI("Zoom: %.2f", g_engine.zoom);
                return 1;
            }
            if (action == AMOTION_EVENT_ACTION_MOVE && pointerCount == 1) {
                float x = AMotionEvent_getX(event, 0);
                float y = AMotionEvent_getY(event, 0);
                static float lastX = x, lastY = y;
                g_engine.panX += (x - lastX) * 0.001f / g_engine.zoom;
                g_engine.panY += (y - lastY) * 0.001f / g_engine.zoom;
                lastX  = x; lastY = y;
                LOGI("Pan: %.2f, %.2f", g_engine.panX, g_engine.panY);
                return 1;
            }
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
            LOGI("Frame: %u, Rotation: %.2f, Paused: %d", g_engine.frameCount, g_engine.rotationFrame * 0.01f, g_engine.paused);
            draw();
        }
    }
}