#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <fcntl.h>       //NOLINT
#include <stdio.h>       //NOLINT
#include <stdlib.h>      //NOLINT
#include <unistd.h>      //NOLINT
#include <cstring>
#include <sys/mman.h>

int open_cam() {
    return open("/dev/video-evs0", O_RDWR);
}

void set_format(int fd) {
    struct v4l2_format format;
    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.width = 1920;
    format.fmt.pix_mp.height = 1536;
    format.fmt.pix_mp.field = V4L2_FIELD_NONE;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_UYVY;

    ioctl(fd, VIDIOC_S_FMT, &format);
}

void* bufs[2];
int dma_fds[2];

void* init_camera(int fd) {
    struct v4l2_requestbuffers rqbufs;
    memset(&rqbufs, 0, sizeof(rqbufs));
    rqbufs.count = 2;
    rqbufs.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    rqbufs.memory = V4L2_MEMORY_MMAP;
    int ret = ioctl(fd, VIDIOC_REQBUFS, &rqbufs);
    if (ret != 0) {
        printf("reqbuf failed: \n");
    }

    for (int i = 0; i < 2; ++i) {
        /*init v4l2 buf */
        struct v4l2_buffer buf;
        struct v4l2_plane planes[3];
        memset(&buf, 0, sizeof buf);
        memset(&planes, 0, sizeof(planes));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.m.planes = planes;
        buf.flags = V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
        buf.length = 1;
        buf.reserved = 0;
        ioctl(fd, VIDIOC_QUERYBUF, &buf);

        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        expbuf.index = 0;
        if (ioctl(fd, VIDIOC_EXPBUF, &expbuf) == -1) {
            printf("VIDIOC_EXPBUF\n");
            return nullptr;
        }

        int dmafd = expbuf.fd;
        dma_fds[i] = dmafd;
        void* map_shm = mmap(NULL, buf.m.planes[0].length, PROT_READ, MAP_SHARED, dmafd, 0);
        if (map_shm == MAP_FAILED) {
            printf("mmap failed\n");
            return nullptr;
        }
        bufs[i] = map_shm;

        ioctl(fd, VIDIOC_QBUF, &buf);
    }

    return nullptr;
}

void start_cam(int fd) {
    enum v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    int ret = ioctl(fd, VIDIOC_STREAMON, &buffer_type);
    printf("start_cam ret %d\n", ret);
}


void stop_cam(int fd) {
    enum v4l2_buf_type buffer_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    ioctl(fd, VIDIOC_STREAMOFF, &buffer_type);
}

int deque_frame(int fd) {
    /*init v4l2 buf */
    struct v4l2_buffer buf;
    struct v4l2_plane planes[3];
    memset(&buf, 0, sizeof buf);
    memset(&planes, 0, sizeof(planes));
    buf.index = 0;
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = planes;
    buf.flags = V4L2_BUF_FLAG_TSTAMP_SRC_EOF;
    buf.length = 1;
    buf.reserved = 0;

    int ret = ioctl(fd, VIDIOC_DQBUF, &buf);
    if (ret != 0) {
        printf("deque buf failed\n");
    }
    return buf.index;
}

void save(void* buf, int len) {
    const char* _fName = "camera_uyvy.yuv";
    FILE* fp = nullptr;
    fp = fopen(_fName, "wb");
    if (fp == nullptr) {
        printf("save failed! %s \n", _fName);
    }

    if (fp) {
        fwrite(buf, 1, len, fp);
        fflush(fp);
        fclose(fp);
    }
}


#include <EGL/egl.h>       //NOLINT
#include <EGL/eglext.h>    //NOLINT
#include "gbm.h"           //NOLINT
#include <xf86drm.h>       //NOLINT
#include <xf86drmMode.h>   //NOLINT

#include "GLES2/gl2.h"     //NOLINT
#include "GLES2/gl2ext.h"  //NOLINT

EGLDisplay gEGLDisplay;
EGLSurface gEGLSurface;

static void initEGL(){
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    if (fd <= 0) {
        printf("open drm failed\n");
    }
    auto gbm_dev = gbm_create_device(fd);
    if (!gbm_dev) {
        printf("gbm failed\n");
    }
    gEGLDisplay = eglGetDisplay(reinterpret_cast<NativeDisplayType>(gbm_dev));
    if (gEGLDisplay == nullptr || gEGLDisplay == EGL_NO_DISPLAY) {
        printf("gEGLDisplay is null!!!\n");
    }

    gEGLDisplay = eglGetDisplay(reinterpret_cast<NativeDisplayType>(gbm_dev));
    // whatever version used
    if (EGL_FALSE == eglInitialize(gEGLDisplay, /*major=*/nullptr, /*minor=*/nullptr)) {
        printf("fucking eglinit failed\n");
    }
    if (EGL_FALSE == eglBindAPI(EGL_OPENGL_ES_API)) {
        printf("fucking bindapi failed\n");
    }
    // create an OpenGL context
    EGLint attributes[] = {EGL_SURFACE_TYPE,
                           EGL_WINDOW_BIT,
                           EGL_RED_SIZE,
                           8,
                           EGL_GREEN_SIZE,
                           8,
                           EGL_BLUE_SIZE,
                           8,
                           EGL_ALPHA_SIZE,
                           8,
                           EGL_RENDERABLE_TYPE,
                           EGL_OPENGL_ES3_BIT_KHR,
                           EGL_NONE};
    EGLConfig config;
    EGLint num_config;
    if (EGL_FALSE == eglChooseConfig(gEGLDisplay, attributes, &config, 1, &num_config)) {
        printf("eglChooseConfig failed \n");
    }

    auto gs = gbm_surface_create(gbm_dev, 1920, 1080,
                       GBM_BO_FORMAT_XRGB8888,
                       GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    gEGLSurface = eglCreateWindowSurface(gEGLDisplay, config, reinterpret_cast<EGLNativeWindowType>(gs), NULL);
    if (!gs || gEGLSurface == EGL_NO_SURFACE) {
        printf("eglCreateWindowSurface, \n");
    }

    const EGLint attrib_list[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    auto egl_context = eglCreateContext(gEGLDisplay, config, EGL_NO_CONTEXT, /*attrib_list=*/attrib_list);
    if (!egl_context) {
        printf("eglCreateContext failed \n");
    }

    if (EGL_FALSE ==
        eglMakeCurrent(gEGLDisplay, gEGLSurface, gEGLSurface, egl_context)) {
        printf("make current failed\n");
    }
}

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>

// start for triangle
static const char kVertexShader[] =
        "#version 320 es\n"
        "precision highp float;\n"
        "layout(location=0) in vec2 position;\n"
        "layout(location=1) in vec2 inTexCoord;\n"
        "out vec2 outTexCoord;\n"
        "void main() {\n"
        "gl_Position = vec4(position, 0.0, 1.0);\n"
        "outTexCoord = inTexCoord;\n"
        "}";

#define GL_FRAGMENT_PRECISION_HIGH 1
static const char kFragmentShader[] =
        "#version 320 es\n"
        "precision highp float;\n"
        "in vec2 outTexCoord;\n"
        "out vec4 out_color;\n"
        "uniform sampler2D in_texture;\n"
        "float r(float y, float v) {\n"
        "return y + 1.13983 * (v-128.0);\n"
        "}\n"
        "float g( float y, float u, float v) {\n"
        "return (y - 0.39465*(u - 128.0) - 0.58060 * (v - 128.0));\n"
        "}\n"
        "float b(float y, float u) {\n"
        "return (y + 2.03211*(u - 128.0));\n"
        "}\n"
        "vec4 translate(vec2 src) {\n"
        "float sw = 1920.0;\n"
        "float x = floor(src.x * sw + 0.1);\n"
        "float alpha = floor(mod(x, 2.0) + 0.1);\n"
        "float x2 = floor(x - (1.0 - alpha) * 0.5) / sw;\n"
        "vec4 color = texture(in_texture, vec2(x2, src.y));\n"
        "vec3 temp = color.grb * vec3(alpha) + color.arb * (1.0 - alpha);\n"
        "vec3 c = vec3(temp.r-0.062745)*1.164;\n"
        "c += (temp.b - 0.5) * vec3(1.596, -0.813, 0.0);\n"
        "c += (temp.g - 0.5) * vec3(0.0, -0.392, 2.017);\n"
        "return vec4(c, 1.0);\n"
        "}\n"
        "void main() {\n"
        "out_color = translate(outTexCoord);\n"
        "}";

GLuint loadShader(GLenum shader_type, const char* shader_source) {
    GLuint shader = glCreateShader(shader_type);
    if (shader) {
        glShaderSource(shader, 1, &shader_source, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint info_len = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &info_len);
            if (info_len) {
                char* buf = reinterpret_cast<char*>(malloc(info_len));
                if (buf) {
                    glGetShaderInfoLog(shader, info_len, NULL, buf);
                    printf("Could not Compile Shader %d:\n%s\n", shader_type, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* vertex_source, const char* fragment_source) {
    GLuint vertex_shader = loadShader(GL_VERTEX_SHADER, vertex_source);
    if (!vertex_shader) {
        return 0;
    }
    GLuint fragment_shader = loadShader(GL_FRAGMENT_SHADER, fragment_source);
    if (!fragment_shader) {
        return 0;
    }
    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertex_shader);
        glAttachShader(program, fragment_shader);
        glLinkProgram(program);
        GLint link_status = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &link_status);
        if (link_status != GL_TRUE) {
            GLint buf_length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &buf_length);
            if (buf_length) {
                char* buf = reinterpret_cast<char*>(malloc(buf_length));
                if (buf) {
                    glGetProgramInfoLog(program, buf_length, NULL, buf);
                    printf("Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

const GLfloat kTriangleVertices[] = {-1.0, 1.0, 0.0, 1.0,
        1.0, 1.0, 1.0, 1.0,
        1.0, -1.0, 1.0, 0.0,
        -1.0, -1.0, 0.0, 0.0};

const GLuint kIndices[]= {3, 2, 1, 1, 0, 3};
GLuint triangle_program;
GLuint VBO, VAO, EBO;
GLuint texture;

void initgl(int index) {
    triangle_program = createProgram(kVertexShader, kFragmentShader);
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kTriangleVertices), kTriangleVertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kIndices), kIndices, GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (GLvoid*)(2*sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0); // Unbind VAO

    // Load and create a texture
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture); // All upcoming GL_TEXTURE_2D operations now have effect on this texture object
    // Set the texture wrapping parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);	// Set texture wrapping to GL_REPEAT (usually basic wrapping method)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // Set texture filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Load image, create texture and generate mipmaps
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1920 / 2, 1080, 0, GL_RGBA, GL_UNSIGNED_BYTE, bufs[index]);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

static void draw(int i){
    const int W = 1280;
    const int H = 1024;
    glViewport(0, 0, W, H);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(triangle_program);
    // Bind Texture
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindVertexArray(VAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    if (i == 2) {
        glFinish();
        int size = (int)(W * H * 4.0f);
        void* temp_t0 = malloc(size);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, temp_t0);
        const char* _fName = "screen.rgba";
        FILE* fp = nullptr;
        fp = fopen(_fName, "wb");
        if (fp == nullptr) {
            printf("save failed! %s \n", _fName);
        }
        if (fp) {
            fwrite(temp_t0, 1, size, fp);
            fflush(fp);
            fclose(fp);
        }
    }

}

int main() {
    int fd = open_cam();
    set_format(fd);
    printf("set format ok \n");
    init_camera(fd);
    printf("init camera ok \n");
    start_cam(fd);
    printf("start camera ok \n");
    int index = deque_frame(fd);
    printf("deque frame ok \n");

    save(bufs[index], 1920*1536*2);
    printf("save ok \n");
    stop_cam(fd);
    printf("stop ok \n");
    initEGL();

    initgl(index);
    for(int i = 0; i >=0; ++i) {
        sleep(1);
        draw(i);
    }
    return 0;
}
