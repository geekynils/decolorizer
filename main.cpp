#ifdef __APPLE__
    #include <TargetConditionals.h>
    #ifdef TARGET_OS_MAC
        #define MAC
    #endif
#endif

#include "sokol_app.h"
#include "sokol_gfx.h"
#include "sokol_time.h"
#include "sokol_glue.h"

#include "imgui.h"
#include "imgui_font.h"

#define SOKOL_IMGUI_IMPL
#include "sokol_imgui.h"

#if defined(__EMSCRIPTEN__)
#include <emscripten/bind.h>
#include <emscripten/fetch.h>
#endif

#include "shader.glsl.h" // generated from shader.glsl using shdc

#ifdef MAC
#include "mac_utils.h"
#endif

#include "image.h"
#include "canny.h"

#include <iomanip>
#include <iostream>
#include <sstream>

#include <ctime>

using std::string;
using std::ostringstream;
using std::put_time;
using std::time;
using std::localtime;
using std::cout;
using std::cerr;
using std::endl;

static uint64_t last_time = 0;

static sg_pass_action pass_action;

// high and low threshold
float th = 0.1, tl = 0.3;

bool keepGrayscale = false;
bool keepGrayscale_checkbox = keepGrayscale;

BlurSetting blur = BLUR1_4;
BlurSetting blur_radioButtons = blur;

// th values set by the sliders.
float th_slider = th, tl_slider = tl;

string statusMessage = "Drop an image (PNG or JPEG) to decolorize it!";

EdgeFinder edgeFinder;

typedef struct point_t {
    float x;
    float y;
} Point;

// MARK: Prototypes

void resetPos();
void findEdgesAndMakeTexture();

// MARK: Helpers

string getDateTimeString() {
    auto t = time(nullptr);
    auto tm = *localtime(&t);
    ostringstream os;
    os << put_time(&tm, "%H_%M-%d_%b_%Y");
    return os.str();
}

float calcDist(Point a, Point b) {
    float q = fabs(a.x - b.x);
    float r = fabs(a.y - b.y);
    return sqrtf(q*q+r*r);
}

// MARK: web specific functionality

#if defined(__EMSCRIPTEN__)

#define MAX_FILE_SIZE 5000000 // 5 MB

typedef enum {
    LOADSTATE_UNKNOWN = 0,
    LOADSTATE_SUCCESS,
    LOADSTATE_FAILED,
    LOADSTATE_FILE_TOO_BIG,
} loadstate_t;

static struct {
    loadstate_t state;
    int size;
    uint8_t buffer[MAX_FILE_SIZE];
} web_load_ctx;

void startDownload() {
    Image edgeImage = invert(edgeFinder.getLines().toUint8());
    string fn = "decolorized-" + getDateTimeString() + ".png";
    if (!edgeImage.writePng(fn)) {
        cerr << "Failed to write " + fn + "!" << endl;
        return;
    }
    emscripten::val::global("window").call<void>(
      "offerFileAsDownload",
      fn,
      string("image/png")
    );
}

void openLink(const string& url) {
    emscripten::val::global("window").call<void>("openLink", url);
}

extern "C" {
void notifyFileAvailable() {
    cout << "file available" << endl;
    FILE* fp = fopen("uploaded", "rb");
    if (!fp) {
        cerr << "Failed to read uploaded file!" << endl;
        return;
    }
    fseek(fp, 0, SEEK_END);
    size_t size = ftell(fp);
    rewind(fp);
    uint8_t *buf = static_cast<uint8_t*>(malloc(size));
    fread(buf, size, 1, fp);
    edgeFinder.readImage(buf, size);
    free(buf);
    findEdgesAndMakeTexture();
}

}

void imguiLink(const char* linkText, const char* linkUrl) {
    const ImVec4 linkBlue = ImVec4(0.66f, 0.76f, 1.0f, 1.0f);
    ImGui::TextColored(linkBlue, linkText);
    if (ImGui::IsItemClicked()) { openLink(linkUrl); }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip((string("open ") + linkUrl).c_str());
    }
}

void loadImageFromBuffer(const uint8_t *buf, int sz) {
    if (!edgeFinder.readImage(buf, sz)) {
        statusMessage = "Could not load buffer!";
    } else {
        statusMessage = "Adjust the thresholds for an optimal result. You can resize the image using the scroll wheel.";
    }
    findEdgesAndMakeTexture();
    resetPos();
}

void downloadSucceeded(emscripten_fetch_t *fetch) {
    loadImageFromBuffer(reinterpret_cast<const uint8_t *>(fetch->data), fetch->numBytes);
    emscripten_fetch_close(fetch);
}

void downloadFailed(emscripten_fetch_t *fetch) {
    cerr << "Downlading " << fetch->url << " failed, HTTP failure status code: " << fetch->status << endl;
    emscripten_fetch_close(fetch);
}

void loadImage(const string& url) {
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.onsuccess = downloadSucceeded;
    attr.onerror = downloadFailed;
    emscripten_fetch(&attr, url.c_str());
}

void emsc_load_callback(const sapp_html5_fetch_response* response) {
    if (response->succeeded) {
        web_load_ctx.state = LOADSTATE_SUCCESS;
        web_load_ctx.size = (int) response->fetched_size;
        loadImageFromBuffer(reinterpret_cast<const uint8_t *>(response->buffer_ptr),  web_load_ctx.size);
        findEdgesAndMakeTexture();
        resetPos();
    } else if (SAPP_HTML5_FETCH_ERROR_BUFFER_TOO_SMALL == response->error_code) {
        statusMessage = "Dropped file is too big!";
        web_load_ctx.state = LOADSTATE_FILE_TOO_BIG;
    } else {
        statusMessage = "Failed to load dropped file!";
        web_load_ctx.state = LOADSTATE_FAILED;
    }
}

void dropWeb() {
    sapp_html5_fetch_request request = {
        .dropped_file_index = 0,
        .callback = emsc_load_callback,
        .buffer_ptr = web_load_ctx.buffer,
        .buffer_size = sizeof(web_load_ctx.buffer),
    };
    sapp_html5_fetch_dropped_file(&request);
}
#else

// MARK: native specific functionality

void dropNative() {
    string droppedPath = sapp_get_dropped_file_path(0);
    if (!edgeFinder.readImage(droppedPath)) {
        statusMessage = "Could not load " + droppedPath;
        return;
    } else {
        statusMessage = "Loaded " + droppedPath + " Adjust the thresholds for an optimal result. You can resize the image using the scroll wheel.";
    }
    findEdgesAndMakeTexture();
    resetPos();
}
#endif

static struct {
    sg_pipeline pip;
    sg_bindings bind;
    sg_pass_action pass_action;
    int texture_pixel_w;
    int texture_pixel_h;
    Point pos =  { -1, 1};
    float scale = 1;
    bool loaded = false;
    bool firstDraw = true;
    Point prevTouchPoints[2]; // keep track of touch points for pinch to zoom
} state;


void resetPos() {
    state.pos.x = -1;
    state.pos.y = 1;
}

void updatePosFromMouse(float dxmouse, float dymouse) {
    float dx = (dxmouse / sapp_widthf()) * 2;
    float dy = -((dymouse / sapp_heightf()) * 2);
    
    state.pos.x += dx;
    state.pos.y += dy;
}

void deleteImage(sg_image handle) {
    sg_uninit_image(handle);
    sg_dealloc_image(handle);
}

void initTextureRendering() {
    state.bind.vertex_buffers[0] = sg_alloc_buffer();
    
    sg_buffer_desc desc = {
        .size = sizeof(float) * 40,
        .usage = SG_USAGE_DYNAMIC,
        .label = "bounds"
    };
    
    sg_init_buffer(state.bind.vertex_buffers[0], &desc);
    
    // We need 2 triangles for a square, this makes 6 indexes.
    uint16_t indices[] = {0, 1, 2, 0, 2, 3,
                          4, 5, 6, 4, 6, 7};

    sg_buffer_desc indexBufferDesc = {
        .type = SG_BUFFERTYPE_INDEXBUFFER,
        .data = SG_RANGE(indices),
        .label = "indices"
    };
    
    state.bind.index_buffer = sg_make_buffer(&indexBufferDesc);
    
    // NOTE texture_shader_desc(...) is defined in texture.glsl.h
    const sg_shader_desc *shader_desc = texture_shader_desc(sg_query_backend());
    
    sg_pipeline_desc pipelineDesc = {
        .shader = sg_make_shader(shader_desc),
        .index_type = SG_INDEXTYPE_UINT16,
        .label = "pipeline"
    };
    
    // C++ does not allow nested designators.
    pipelineDesc.layout.attrs[ATTR_vs_pos].format = SG_VERTEXFORMAT_FLOAT3;
    pipelineDesc.layout.attrs[ATTR_vs_texcoord0].format = SG_VERTEXFORMAT_FLOAT2;
    state.pip = sg_make_pipeline(&pipelineDesc);
}

void init() {
    // setup sokol-gfx, sokol-time and  
    sg_desc desc = { };
    desc.context = sapp_sgcontext();
    sg_setup(&desc);
    stm_setup();

    simgui_desc_t simgui_desc = { };
    simgui_desc.no_default_font = true;
    simgui_desc.dpi_scale = sapp_dpi_scale();
    simgui_setup(&simgui_desc);
    
    // Taken from https://github.com/floooh/sokol-samples/blob/master/sapp/imgui-highdpi-sapp.cc:
    // configure Dear ImGui with our own embedded font
    auto& io = ImGui::GetIO();
    ImFontConfig fontCfg;
    fontCfg.FontDataOwnedByAtlas = false;
    fontCfg.OversampleH = 2;
    fontCfg.OversampleV = 2;
    fontCfg.RasterizerMultiply = 1.5f;
    io.Fonts->AddFontFromMemoryTTF(dump_font, sizeof(dump_font), 16.0f, &fontCfg);

    // create font texture for the custom font
    unsigned char* font_pixels;
    int font_width, font_height;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);
    sg_image_desc img_desc = { };
    img_desc.width = font_width;
    img_desc.height = font_height;
    img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    img_desc.min_filter = SG_FILTER_LINEAR;
    img_desc.mag_filter = SG_FILTER_LINEAR;
    img_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    img_desc.data.subimage[0][0].ptr = font_pixels;
    img_desc.data.subimage[0][0].size = font_width * font_height * 4;
    io.Fonts->TexID = (ImTextureID)(uintptr_t) sg_make_image(&img_desc).id;
    
    ImGui::GetStyle().WindowRounding = 5.0f;
    ImGui::GetStyle().FrameRounding = 5.0f;
    ImGui::GetStyle().GrabRounding = 5.0f;
    
    initTextureRendering();
    
    // initial clear color
    pass_action.colors[0].action = SG_ACTION_CLEAR;
    pass_action.colors[0].value = { 1.0f, 1.f, 1.f, 1.0f };

    #ifdef MAC
    initialize_mac_menu("Decolorizer");
    #endif
}

sg_image makeTexture(const Image& image) {
    sg_image texture = sg_alloc_image();
    sg_image_desc texture_desc = {
        .width = image.getWidth(),
        .height = image.getHeight(),
        .usage = SG_USAGE_DYNAMIC,
        // WebGL note: Only pixel format that works.
        .pixel_format = SG_PIXELFORMAT_RGBA8,
        // linear filtering looks better for edge images
        .min_filter = SG_FILTER_LINEAR,
        .mag_filter = SG_FILTER_LINEAR,
        // WebGL note: Non power of two images must have wrap set to CLAMP_TO_EDGE
        //             otherwise you will get a black texture.
        .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
        .wrap_v = SG_WRAP_CLAMP_TO_EDGE
    };
    sg_init_image(texture, &texture_desc);
    return texture;
}

// Creates a rectangle for mapping a texture. Array must be 20 elements long.
void make_vertex_rect(float x, float y, float w, float h, float *vertex_positions) {
    /*  -1.0,+1.0             +1.0,+1.0
        +----------------------+
        |                      |
        |                      |
        |                      |
        +----------------------+
        -1.0,-1.0             +1.0,-1.0 */
    float new_positions[20] = {
        // We start at the top left and go in clockwise direction.
        //  x,     y,        z,    u,    v
            x,     y,     0.0f, 0.0f, 0.0f,
            x + w, y,     0.0f, 1.0f, 0.0f,
            x + w, y - h, 0.0f, 1.0f, 1.0f,
            x,     y - h, 0.0f, 0.0f, 1.0f
    };
    for (int i = 0; i < 20; ++i) { vertex_positions[i] = new_positions[i]; }
}

void update_vertices() {
    float pixelScaleW = 2.0f / sapp_widthf() * sapp_dpi_scale();
    float pixelScaleH = 2.0f / sapp_heightf() * sapp_dpi_scale();
    float w = state.texture_pixel_w * pixelScaleW;
    float h = state.texture_pixel_h * pixelScaleH;
    w *= state.scale;
    h *= state.scale;
    float bounds[20];
    make_vertex_rect(state.pos.x, state.pos.y, w, h, bounds);
    sg_range range = SG_RANGE(bounds);
    sg_update_buffer(state.bind.vertex_buffers[0], &range);
}

/// Invert the image and stretch it to RGBA, then update the texture buffer.
void updateEdgeTexture(const Image& lineImage) {
    assert(lineImage.getNumChannels() == 1);
    size_t len = lineImage.getWidth() * lineImage.getHeight() * 4;
    size_t sz = len * sizeof(uint8_t);
    
    uint8_t * __restrict tex_ptr = static_cast<uint8_t*>(malloc(sz));
    const uint8_t * __restrict ptr = lineImage.data();
    for (size_t i = 0; i < len; ++i) { tex_ptr[i] = 255 - ptr[i / 4]; }
    
    sg_image_data data = { };
    data.subimage[0][0] = (sg_range){tex_ptr, sz};
    sg_update_image(state.bind.fs_images[SLOT_tex], &data);
    free(tex_ptr);
}

void findEdgesAndMakeTexture() {
    edgeFinder.calcGrads(maxAbsChannel, blur);
    edgeFinder.calcNonMaxSuppression(th, tl, keepGrayscale);
    Image lineImage = edgeFinder.getLines().toUint8();
    
    if (state.loaded) {
        deleteImage(state.bind.fs_images[SLOT_tex]);
    }
    state.bind.fs_images[SLOT_tex] = makeTexture(lineImage);
    updateEdgeTexture(lineImage);
    state.texture_pixel_w = lineImage.getWidth();
    state.texture_pixel_h = lineImage.getHeight();
    state.loaded = true;
}

void frame() {
    const int width = sapp_width();
    const int height = sapp_height();
    
    const double delta_time = stm_sec(stm_laptime(&last_time));
    simgui_new_frame(width, height, delta_time);
    
    if (state.firstDraw) {
        state.firstDraw = false;
        int winw = 360, winh = 360;
        int margin = 60;
        ImGui::SetNextWindowSize(ImVec2(winw, winh));
        ImGui::SetNextWindowPos(ImVec2(width/sapp_dpi_scale() - winw - margin, margin));
    }
    
    ImGui::Begin("Decolorizer");
    ImGui::TextWrapped("%s", statusMessage.c_str());
#if defined(__EMSCRIPTEN__)
    ImGui::Text("Example images:");
    
    ImVec4 linkBlue = ImVec4(0.66f, 0.76f, 1.0f, 1.0f);
    
    ImGui::Bullet();
    imguiLink("Forest lizzard", "https://en.wikipedia.org/wiki/Canny_edge_detector#/media/File:Large_Scaled_Forest_Lizard.jpg");
    ImGui::SameLine();
    ImGui::Text("from Wikipedia");
    ImGui::SameLine();
    ImGui::TextColored(linkBlue, "decolorize");
    if (ImGui::IsItemClicked()) {
        loadImage("lizzardWiki.jpg");
    }
    
    ImGui::Bullet();
    imguiLink("Lego biker", "legoBiker.png");
    ImGui::SameLine();
    ImGui::TextColored(linkBlue, "decolorize");
    if (ImGui::IsItemClicked()) {
        loadImage("legoBiker.png");
    }
    
    ImGui::Bullet();
    imguiLink("Cute dragon drawing", "https://opengameart.org/content/cute-dragon-0");
    ImGui::SameLine();
    ImGui::Text("from open game art");
    ImGui::SameLine();
    ImGui::TextColored(linkBlue, "decolorize");
    if (ImGui::IsItemClicked()) {
        loadImage("cuteDragon.png");
    }
    
    /* If I only could figure out how to open the file chooser in Safari.
    if (ImGui::Button("load image")) {
        emscripten::val::global("window").call<void>("showFileChooser");
    }*/
    
#endif
    ImGui::Separator();
    ImGui::BeginDisabled(!state.loaded);
    
    ImGui::SliderFloat("high threshold", &th_slider, 0.0f, 1.0f);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Only edge pixels from the top qantiles are included. The\n"
            "threshold for the top quantiles can be set here. Reduce\n"
            "this if there is too much noise in the picture, increase if\n"
            "important edges are missing.");
    }
    
    ImGui::SliderFloat("low threshold", &tl_slider, 0.0f, 1.0f);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Pixels from above the lower threshold are included as well\n"
            "well if touch at least one pixel that is above the higher\n"
            "threshold.");
    }
    
    ImGui::Checkbox("grayscale lines", &keepGrayscale_checkbox);
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Instead of black lines keep the grayscale values. The\n"
            "grayscale values correspond to the strength of the edge.");
    }
    
    ImGui::Text("Blurring:");
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Blur the image before edge detection. This helps to remove\n"
            "noise.");
    }
    int *ptr = reinterpret_cast<int*>(&blur_radioButtons);
    ImGui::RadioButton("none",   ptr, NO_BLUR); ImGui::SameLine();
    ImGui::RadioButton("normal", ptr, BLUR1_4); ImGui::SameLine();
    ImGui::RadioButton("extra",  ptr, BLUR2);

#if defined(__EMSCRIPTEN__)
    if (ImGui::Button("download image")) {
        startDownload();
    }
#endif
    ImGui::EndDisabled();
#if defined(__EMSCRIPTEN__)
    ImGui::Separator();
    if (ImGui::Button("feedback / ideas")) {
        emscripten::val::global("window").call<void>("sendEmail");
    }
#endif
    
    ImGui::End();
    
    sg_begin_default_pass(&pass_action, width, height);
    
    if (state.loaded) {
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            bool updateLines = false;
            if (tl != tl_slider || th != th_slider) {
                tl = tl_slider;
                th = th_slider;
                updateLines = true;
            } else if (keepGrayscale_checkbox != keepGrayscale) {
                keepGrayscale = keepGrayscale_checkbox;
                updateLines = true;
            } else if (blur_radioButtons != blur) {
                blur = blur_radioButtons;
                edgeFinder.calcGrads(maxAbsChannel, blur);
                updateLines = true;
            }
            if (updateLines) {
                edgeFinder.calcNonMaxSuppression(th, tl, keepGrayscale);
                Image lineImage = edgeFinder.getLines().toUint8();
                updateEdgeTexture(lineImage);
            }
            
        }
        update_vertices();
        sg_apply_pipeline(state.pip);
        sg_apply_bindings(&state.bind);
        sg_draw(0, 12, 1); // base_element, # elements, # instances.
    }
    
    simgui_render();
    sg_end_pass();
    sg_commit();
}

void cleanup() {
    simgui_shutdown();
    if (state.loaded) {
        deleteImage(state.bind.fs_images[SLOT_tex]);
    }
    sg_shutdown();
}

void setScale(float mouseYscroll) {
    state.scale += mouseYscroll * 0.1f;
    state.scale = clamp(state.scale, 0.25f, 4.0f);
}

void setScaleFromTouch(float fingerDist) {
    state.scale += fingerDist * 0.0002f;
    state.scale = clamp(state.scale, 0.25f, 4.0f);
}

void input(const sapp_event* ev) {
    if (simgui_handle_event(ev)) {
        return;
    }
    switch (ev->type) {
        case SAPP_EVENTTYPE_FILES_DROPPED: {
#if defined(__EMSCRIPTEN__)
            dropWeb();
#else
            dropNative();
#endif
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_SCROLL: {
            setScale(ev->scroll_y);
            break;
        }
        case SAPP_EVENTTYPE_MOUSE_MOVE: {
            if (ev->modifiers & SAPP_MODIFIER_LMB) {
                updatePosFromMouse(ev->mouse_dx, ev->mouse_dy);
            }
            break;
        }
        // Touch event handling
        case SAPP_EVENTTYPE_TOUCHES_BEGAN: {
            if (ev->num_touches == 2) {
                for (int i = 0; i < 2; ++i) {
                    state.prevTouchPoints[i] = { ev->touches[i].pos_x, ev->touches[i].pos_y};
                }
            }
            break;
        }
        case SAPP_EVENTTYPE_TOUCHES_MOVED: {
            if (ev->num_touches == 2) {
                float oldDist = calcDist(state.prevTouchPoints[0], state.prevTouchPoints[1]);
                Point a = {ev->touches[0].pos_x, ev->touches[0].pos_y};
                Point b = {ev->touches[1].pos_x, ev->touches[1].pos_y};
                float dist = calcDist(a, b);
                setScaleFromTouch(dist - oldDist);
            }
        }
            
        default: {
            // pass
        }
    }
}

sapp_desc sokol_main(int argc, char* argv[]) {
    (void)argc; (void)argv;
    sapp_desc desc = { };
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = input;
    desc.width = 1024;
    desc.height = 768;
    desc.gl_force_gles2 = true;
    desc.enable_dragndrop = true;
    desc.window_title = "Decolorizer";
    desc.ios_keyboard_resizes_canvas = false;
    // desc.icon.sokol_default = true;
    desc.high_dpi = true;
    return desc;
}
