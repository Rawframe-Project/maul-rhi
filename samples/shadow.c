// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Reversed-Z depth with a shadow map. Depth runs from 1 near to 0 far,
// cleared to 0 and tested with "greater", which spreads a float depth
// buffer's precision evenly over distance. A first pass draws the floor
// and an occluder above it into a shadow map from the light, depth
// only; a second, from the same view, draws a red square near the
// camera and then the floor, which the square hides, and shades the
// floor by comparing its depth with the shadow map's through a
// comparison sampler: dark under the occluder, white elsewhere.

#include "harness.h"

#include "shaders/shadow_depth_container.h"
#include "shaders/shadow_scene_container.h"

#include <stdio.h>

// The targets' and the shadow map's width and height.
#define SIZE 64

// Squares of two triangles each, their positions and depths: the floor
// far below, the occluder over its middle, and a red square near the
// camera in the lower right.
static const float kVertices[18][3] = {
    {-1.0f, -1.0f, 0.2f},  {1.0f, -1.0f, 0.2f},   {-1.0f, 1.0f, 0.2f},    {-1.0f, 1.0f, 0.2f},
    {1.0f, -1.0f, 0.2f},   {1.0f, 1.0f, 0.2f},    {-0.25f, -0.25f, 0.8f}, {0.25f, -0.25f, 0.8f},
    {-0.25f, 0.25f, 0.8f}, {-0.25f, 0.25f, 0.8f}, {0.25f, -0.25f, 0.8f},  {0.25f, 0.25f, 0.8f},
    {0.5f, -0.9f, 0.9f},   {0.9f, -0.9f, 0.9f},   {0.5f, -0.5f, 0.9f},    {0.5f, -0.5f, 0.9f},
    {0.9f, -0.9f, 0.9f},   {0.9f, -0.5f, 0.9f},
};
enum
{
    FLOOR = 0,
    OCCLUDER = 6,
    SQUARE = 12,
};
static const float kRed[4] = {1.0f, 0.0f, 0.0f, 1.0f};

typedef struct Scene
{
    mrhiShaderId depthShader;
    mrhiShaderId sceneShader;
    mrhiGraphicsPipelineId depthOnly;
    mrhiGraphicsPipelineId lit;
    mrhiGraphicsPipelineId solid;
    mrhiSamplerId comparison;
} Scene;

// A reversed-Z pipeline over the vertices: depth written and tested with
// "greater"; no color target without a fragment entry.
static mrhiGraphicsPipelineId MakePipeline(Sample* sample, mrhiShaderId shader,
                                           const char* fragment, size_t fragmentLength)
{
    static const mrhiVertexBufferLayout buffer = {.stride = sizeof(kVertices[0])};
    static const mrhiVertexAttribute position = {.format = mrhi_vertexFloat32x3};
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "place";
    def.vertexEntryLength = 5;
    def.fragmentEntry = fragment;
    def.fragmentEntryLength = fragmentLength;
    def.vertexBuffers = &buffer;
    def.vertexBufferCount = 1;
    def.vertexAttributes = &position;
    def.vertexAttributeCount = 1;
    def.depthStencilFormat = mrhi_formatDepth32Float;
    def.depthWrite = true;
    def.depthCompare = mrhi_compareGreater;
    def.colorTargetCount = fragment != nullptr ? 1 : 0;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    return SampleGraphics(sample, &def);
}

static void MakeScene(Sample* sample, Scene* scene)
{
    scene->depthShader =
        SampleShader(sample, s_shadow_depthContainer, sizeof(s_shadow_depthContainer));
    scene->sceneShader =
        SampleShader(sample, s_shadow_sceneContainer, sizeof(s_shadow_sceneContainer));
    scene->depthOnly = MakePipeline(sample, scene->depthShader, nullptr, 0);
    scene->lit = MakePipeline(sample, scene->sceneShader, "lit", 3);
    scene->solid = MakePipeline(sample, scene->sceneShader, "solid", 5);
    mrhiSamplerDef def = mrhiDefaultSamplerDef();
    def.compare = mrhi_compareGreaterEqual;
    SampleCheck(sample, mrhiCreateSampler(sample->device, &def, &scene->comparison) == mrhi_success,
                "a comparison sampler");
}

static mrhiResourceId DeclareTexture(Sample* sample, mrhiFormat format)
{
    mrhiTextureDef def = mrhiDefaultTextureDef();
    def.format = format;
    def.width = SIZE;
    def.height = SIZE;
    mrhiResourceId texture = {0};
    SampleCheck(sample, mrhiDeclareTexture(sample->device, &def, &texture) == mrhi_success,
                "a declared texture");
    return texture;
}

static mrhiAccess Whole(mrhiResourceId resource, mrhiAccessKind kind)
{
    return (mrhiAccess){
        .resource = resource,
        .kind = kind,
        .range = {.mipCount = 1, .layerCount = 1},
    };
}

// A pass with a depth target cleared to 0, the far end under reversed-Z.
static mrhiPassId DepthPass(Sample* sample, mrhiResourceId color, mrhiResourceId depth,
                            const mrhiAccess* accesses, uint32_t count)
{
    mrhiPassDef def = mrhiDefaultPassDef();
    if (color.index1 != 0)
    {
        def.colorTargets[0] = (mrhiColorTarget){
            .resource = color,
            .load = mrhi_loadClear,
            .store = mrhi_storeKeep,
            .clear = {0.0f, 0.0f, 0.0f, 1.0f},
        };
        def.colorTargetCount = 1;
    }
    def.depthTarget = (mrhiDepthTarget){
        .resource = depth,
        .depthLoad = mrhi_loadClear,
        .depthStore = mrhi_storeKeep,
        .clearDepth = 0.0f,
    };
    def.accesses = accesses;
    def.accessCount = count;
    mrhiPassId pass = {0};
    SampleCheck(sample, mrhiAddPass(sample->device, &def, &pass) == mrhi_success, "a depth pass");
    return pass;
}

// The main pass: the red square first, then the floor behind it, lit
// through the shadow map.
static void DrawScene(Sample* sample, const Scene* scene, mrhiPassId pass, mrhiResourceId vertices,
                      mrhiResourceId shadowMap)
{
    mrhiDevice* device = sample->device;
    const mrhiBinding table[2] = {
        {.slot = 0, .resource = shadowMap, .range = {.mipCount = 1, .layerCount = 1}},
        {.slot = 1, .sampler = scene->comparison},
    };
    SampleCheck(sample,
                mrhiBeginPass(device, pass) == mrhi_success &&
                    mrhiSetVertexBuffer(device, pass, 0, vertices, 0, sizeof(kVertices)) ==
                        mrhi_success &&
                    mrhiSetGraphicsPipeline(device, pass, scene->solid) == mrhi_success &&
                    mrhiSetBindings(device, pass, 0, table, 2) == mrhi_success &&
                    mrhiSetRootBlock(device, pass, 0, kRed, sizeof(kRed)) == mrhi_success &&
                    mrhiDraw(device, pass, 6, 1, SQUARE, 0) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, pass, scene->lit) == mrhi_success &&
                    mrhiDraw(device, pass, 6, 1, FLOOR, 0) == mrhi_success &&
                    mrhiEndPass(device, pass) == mrhi_success,
                "the scene drawn");
}

// Records the frame: the vertices uploaded, the shadow map drawn, the
// scene drawn and read back.
static mrhiRequestId Record(Sample* sample, const Scene* scene)
{
    mrhiDevice* device = sample->device;
    mrhiFrameDef frame = mrhiDefaultFrameDef();
    SampleCheck(sample, mrhiBeginFrame(device, &frame) == mrhi_success, "a frame");
    mrhiBufferDef bufferDef = mrhiDefaultBufferDef();
    bufferDef.size = sizeof(kVertices);
    mrhiResourceId vertices = {0};
    SampleCheck(sample, mrhiDeclareBuffer(device, &bufferDef, &vertices) == mrhi_success,
                "a vertex buffer");
    mrhiResourceId shadowMap = DeclareTexture(sample, mrhi_formatDepth32Float);
    mrhiResourceId depth = DeclareTexture(sample, mrhi_formatDepth32Float);
    mrhiResourceId color = DeclareTexture(sample, mrhi_formatRgba8Unorm);
    const mrhiAccess upload = Whole(vertices, mrhi_accessCopyDestination);
    mrhiPassDef uploadDef = mrhiDefaultPassDef();
    uploadDef.passClass = mrhi_passTransfer;
    uploadDef.accesses = &upload;
    uploadDef.accessCount = 1;
    mrhiPassId uploading = {0};
    SampleCheck(sample, mrhiAddPass(device, &uploadDef, &uploading) == mrhi_success, "a pass");
    const mrhiAccess drawn = Whole(vertices, mrhi_accessVertex);
    mrhiPassId shadow = DepthPass(sample, (mrhiResourceId){0}, shadowMap, &drawn, 1);
    const mrhiAccess reads[2] = {drawn, Whole(shadowMap, mrhi_accessSampled)};
    mrhiPassId main = DepthPass(sample, color, depth, reads, 2);
    const mrhiAccess read = Whole(color, mrhi_accessCopySource);
    mrhiPassDef readDef = mrhiDefaultPassDef();
    readDef.passClass = mrhi_passTransfer;
    readDef.accesses = &read;
    readDef.accessCount = 1;
    readDef.neverCull = true;
    mrhiPassId reading = {0};
    SampleCheck(sample,
                mrhiAddPass(device, &readDef, &reading) == mrhi_success &&
                    mrhiCompileFrame(device) == mrhi_success,
                "the frame compiled");
    SampleCheck(sample,
                mrhiBeginPass(device, uploading) == mrhi_success &&
                    mrhiWriteBuffer(device, uploading, vertices, 0, kVertices, sizeof(kVertices)) ==
                        mrhi_success &&
                    mrhiEndPass(device, uploading) == mrhi_success,
                "the vertices uploaded");
    SampleCheck(sample,
                mrhiBeginPass(device, shadow) == mrhi_success &&
                    mrhiSetGraphicsPipeline(device, shadow, scene->depthOnly) == mrhi_success &&
                    mrhiSetVertexBuffer(device, shadow, 0, vertices, 0, sizeof(kVertices)) ==
                        mrhi_success &&
                    mrhiDraw(device, shadow, 12, 1, FLOOR, 0) == mrhi_success &&
                    mrhiEndPass(device, shadow) == mrhi_success,
                "the shadow map drawn");
    DrawScene(sample, scene, main, vertices, shadowMap);
    const mrhiTextureCopy source = {.resource = color};
    const mrhiExtent3d extent = {SIZE, SIZE, 1};
    mrhiRequestId pixels = {0};
    SampleCheck(sample,
                mrhiBeginPass(device, reading) == mrhi_success &&
                    mrhiReadTexture(device, reading, &source, &extent, &pixels) == mrhi_success &&
                    mrhiEndPass(device, reading) == mrhi_success,
                "the scene read");
    return pixels;
}

// Dark under the occluder, red on the square, white elsewhere.
static void Check(Sample* sample, const uint8_t* image)
{
    static const uint8_t dark[4] = {64, 64, 64, 255};
    static const uint8_t white[4] = {255, 255, 255, 255};
    static const uint8_t red[4] = {255, 0, 0, 255};
    static const struct
    {
        int x;
        int y;
        const uint8_t* color;
    } probes[] = {
        {32, 32, dark},  {27, 27, dark},  {37, 37, dark},  {12, 12, white}, {52, 20, white},
        {20, 52, white}, {32, 21, white}, {44, 54, white}, {54, 54, red},   {58, 50, red},
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); ++i)
    {
        char what[48];
        snprintf(what, sizeof(what), "pixel (%d, %d)", probes[i].x, probes[i].y);
        SampleCheck(sample,
                    SampleNear(&image[(probes[i].y * SIZE + probes[i].x) * 4], probes[i].color, 1),
                    what);
    }
}

int main(void)
{
    Sample sample;
    int opened = SampleOpen(&sample, nullptr);
    if (opened != 0)
    {
        return opened;
    }
    Scene scene = {0};
    MakeScene(&sample, &scene);
    mrhiRequestId pixels = Record(&sample, &scene);
    static uint8_t image[SIZE * SIZE * 4];
    if (SampleFinish(&sample) && SampleTake(&sample, pixels, image, sizeof(image)))
    {
        Check(&sample, image);
    }
    mrhiDevice* device = sample.device;
    SampleCheck(&sample,
                mrhiDestroyGraphicsPipeline(device, scene.depthOnly) == mrhi_success &&
                    mrhiDestroyGraphicsPipeline(device, scene.lit) == mrhi_success &&
                    mrhiDestroyGraphicsPipeline(device, scene.solid) == mrhi_success &&
                    mrhiDestroySampler(device, scene.comparison) == mrhi_success &&
                    mrhiDestroyShader(device, scene.depthShader) == mrhi_success &&
                    mrhiDestroyShader(device, scene.sceneShader) == mrhi_success,
                "the scene destroyed");
    return SampleClose(&sample);
}
