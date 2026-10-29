// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Sirac Ozmen
//
// Fuzzes graphics and compute pipeline defs, which a program fills from
// its own data, on the test driver. Each input starts from a def that
// is valid, and its bytes then change one state at a time, so that
// mutations reach each check instead of stopping at the first. The
// shader is the conformance container or one built from the input,
// whose vertex inputs, color outputs, inter-stage variables, fragment
// builtins and constants the reflection rules read. Every call must
// answer a status, count misuse only for invalid input or a bound
// passed, name each refusal of the def and each device or adapter limit
// it passes, and answer capacity only past a device limit the adapter's
// would hold; a pipeline that is made
// must satisfy WebGPU's rules on the def alone, be answered ready once,
// and be destroyed.
// Arrays longer than their limit are given only up to it, as the core
// refuses them unread.

#include "capabilities_core.h"
#include "test_container.h"

#include "maul-rhi/device.h"
#include "maul-rhi/instance.h"
#include "maul-rhi/pipeline.h"
#include "maul-rhi/test.h"
#include "shaders/conformance_container.h"

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void Expect(bool condition)
{
    if (!condition)
    {
        abort();
    }
}

// The input, read in order; past its end every byte is 0.
typedef struct Input
{
    const uint8_t* data;
    size_t size;
    size_t at;
} Input;

static uint8_t Byte(Input* in)
{
    return in->at < in->size ? in->data[in->at++] : 0;
}

static uint32_t Word(Input* in)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i)
    {
        value |= (uint32_t)Byte(in) << (8 * i);
    }
    return value;
}

// A size or count: small values often, any value sometimes.
static uint32_t Size(Input* in)
{
    uint8_t b = Byte(in);
    return b < 0xF0 ? b : Word(in);
}

// A float from the edges WebGPU's rules care about, or any bits.
static float Float(Input* in)
{
    static const float edges[] = {0.0f,     -0.0f,     1.0f,   -1.0f,   0.5f,     16.0f,
                                  INFINITY, -INFINITY, NAN,    FLT_MAX, -FLT_MAX, FLT_MIN,
                                  1e-45f,   1e30f,     -1e30f, 2.0f};
    uint8_t b = Byte(in);
    if (b < sizeof(edges) / sizeof(edges[0]))
    {
        return edges[b];
    }
    uint32_t bits = Word(in);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// A constant's value from the edges of its types, or any bits.
static double Double(Input* in)
{
    static const double edges[] = {
        0.0,
        1.0,
        -1.0,
        0.5,
        2147483647.0,
        2147483648.0,
        -2147483648.0,
        -2147483649.0,
        4294967295.0,
        4294967296.0,
        (double)FLT_MAX,
        (double)FLT_MAX * 2.0,
        -(double)FLT_MAX * 2.0,
        (double)INFINITY,
        (double)NAN,
        -0.0,
    };
    uint8_t b = Byte(in);
    if (b < sizeof(edges) / sizeof(edges[0]))
    {
        return edges[b];
    }
    uint64_t bits = (uint64_t)Word(in) | (uint64_t)Word(in) << 32;
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

// The memory one input's def points at, each block exactly its size so
// that ASan sees any read past it, freed after the input.
static void* s_owned[256];
static uint32_t s_ownedCount;

static void* Own(size_t bytes)
{
    if (s_ownedCount == sizeof(s_owned) / sizeof(s_owned[0]))
    {
        return nullptr;
    }
    void* memory = malloc(bytes);
    Expect(memory != nullptr || bytes == 0);
    s_owned[s_ownedCount++] = memory;
    return memory;
}

static void FreeOwned(void)
{
    for (uint32_t i = 0; i < s_ownedCount; ++i)
    {
        free(s_owned[i]);
    }
    s_ownedCount = 0;
}

// A name: one of the containers' entries and near misses, NULL with a
// length, or bytes of the input.
static void Name(Input* in, const char** nameOut, size_t* lengthOut)
{
    static const char* const names[] = {"vs", "fs", "cs", "vp", "fr", "ca", "v", "vsx"};
    uint8_t b = Byte(in);
    if (b < 0x80)
    {
        *nameOut = names[b % 8];
        *lengthOut = strlen(*nameOut);
        return;
    }
    if (b < 0x90)
    {
        *nameOut = nullptr;
        *lengthOut = b & 0x0F;
        return;
    }
    size_t length = b & 0x3F;
    char* name = Own(length);
    for (size_t i = 0; name != nullptr && i < length; ++i)
    {
        name[i] = (char)Byte(in);
    }
    *nameOut = name;
    *lengthOut = name != nullptr ? length : 0;
}

// The values of a def's constants, up to 8 of a longer list.
static const mrhiConstantValue* Constants(Input* in, uint32_t* countOut)
{
    uint32_t count = Size(in);
    uint32_t given = count < 8 ? count : 8;
    mrhiConstantValue* values = Byte(in) % 16 == 0 ? nullptr : Own(given * sizeof(*values));
    for (uint32_t i = 0; values != nullptr && i < given; ++i)
    {
        values[i] = (mrhiConstantValue){.id = Byte(in) % 12, .value = Double(in)};
    }
    *countOut = count;
    return values;
}

// A device of the test driver, the conformance container on it, and the
// id of a shader it has destroyed.
typedef struct Device
{
    mrhiInstance* instance;
    mrhiDevice* device;
    mrhiFeatures features;
    mrhiLimits limits;
    mrhiShaderId conformance;
    mrhiShaderId destroyed;
} Device;

// A device with dual-source blending, unclipped depth and four views, on
// an adapter with more views and vertex state than it asked for, and
// one with no optional feature.
static Device s_devices[2];

static mrhiShaderId MakeShader(mrhiDevice* device, const uint8_t* bytes, size_t size)
{
    mrhiShaderDef def = mrhiDefaultShaderDef();
    def.bytes = bytes;
    def.byteCount = size;
    mrhiShaderId shader = {0};
    mrhiResult status = mrhiCreateShader(device, &def, &shader);
    Expect(status == mrhi_success || status == mrhi_errorFormat ||
           status == mrhi_errorUnsupported || status == mrhi_errorCapacity ||
           status == mrhi_errorVersion);
    return shader;
}

static void Open(Device* open, bool featured)
{
    static mrhiTestAdapter adapters[2];
    static mrhiTestDriverDef drivers[2];
    mrhiTestAdapter* adapter = &adapters[featured];
    *adapter = (mrhiTestAdapter){
        .info = {.driver = mrhi_driverTest, .kind = mrhi_adapterDiscrete},
        .limits = mrhiDefaultLimits(),
    };
    mrhiDeviceDef deviceDef = mrhiDefaultDeviceDef();
    if (featured)
    {
        adapter->features.dualSourceBlending = true;
        adapter->features.unclippedDepth = true;
        adapter->features.multiview = true;
        adapter->limits.multiviewViews = 8;
        adapter->limits.vertexBuffers = 16;
        adapter->limits.vertexAttributes = 32;
        adapter->limits.vertexStride = 4096;
        deviceDef.features = adapter->features;
        deviceDef.limits.multiviewViews = 4;
    }
    deviceDef.budgets.diagnostics = 8;
    drivers[featured] = (mrhiTestDriverDef){
        .chain = {.next = nullptr, .type = mrhi_structTestDriver},
        .adapters = adapter,
        .adapterCount = 1,
    };
    mrhiInstanceDef def = mrhiDefaultInstanceDef();
    def.next = &drivers[featured].chain;
    Expect(mrhiCreateInstance(&def, &open->instance) == mrhi_success);
    mrhiAdapterRequestDef search = mrhiDefaultAdapterRequestDef();
    mrhiRequestId request;
    mrhiInstanceNotification record;
    Expect(mrhiRequestAdapters(open->instance, &search, &request) == mrhi_success &&
           mrhiNextInstanceNotification(open->instance, &record) == mrhi_success);
    size_t count = 0;
    Expect(mrhiGetAdapters(open->instance, &deviceDef.adapter, 1, &count) == mrhi_success &&
           count == 1);
    Expect(mrhiCreateDevice(open->instance, &deviceDef, &open->device, &request) == mrhi_success &&
           mrhiNextInstanceNotification(open->instance, &record) == mrhi_success);
    Expect(mrhiGetDeviceFeatures(open->device, &open->features) == mrhi_success &&
           mrhiGetDeviceLimits(open->device, &open->limits) == mrhi_success);
    open->conformance =
        MakeShader(open->device, s_conformanceContainer, sizeof(s_conformanceContainer));
    open->destroyed =
        MakeShader(open->device, s_conformanceContainer, sizeof(s_conformanceContainer));
    Expect(open->conformance.index1 != 0 &&
           mrhiDestroyShader(open->device, open->destroyed) == mrhi_success);
}

// Rewrites the default built container's interface from the input:
// the vertex entry's inputs and outputs, the fragment entry's inputs,
// color outputs and builtins, and the constants, of values mostly in
// range so that most containers parse.
static void Interface(Input* in)
{
    Reset();
    s_sections[INPUTS].size = 0;
    s_sections[OUTPUTS].size = 0;
    s_sections[VARIABLES].size = 0;
    s_sections[CONSTANTS].size = 0;
    uint32_t inputs = Byte(in) % 4;
    uint32_t outputs = Byte(in) % 4;
    uint32_t vertexVariables = Byte(in) % 4;
    uint32_t fragmentVariables = Byte(in) % 4;
    for (uint32_t i = 0; i < inputs; ++i)
    {
        Variable(INPUTS, i, Byte(in) % 20, Byte(in) % 6, Byte(in) % 6, 0, 0);
    }
    for (uint32_t i = 0; i < outputs; ++i)
    {
        Variable(OUTPUTS, i, Byte(in) % 10, Byte(in) % 6, Byte(in) % 6, Byte(in) % 3, 0);
    }
    for (uint32_t i = 0; i < vertexVariables + fragmentVariables; ++i)
    {
        Variable(VARIABLES, i, Byte(in) % 20, Byte(in) % 6, Byte(in) % 6, Byte(in) % 5,
                 Byte(in) % 7);
    }
    uint8_t* vertex = Record(ENTRIES, 0, 48);
    Put16(vertex + 26, (uint16_t)inputs);
    Put16(vertex + 34, (uint16_t)vertexVariables);
    uint8_t* fragment = Record(ENTRIES, 1, 48);
    Put16(fragment + 30, (uint16_t)outputs);
    Put16(fragment + 32, (uint16_t)vertexVariables);
    Put16(fragment + 34, (uint16_t)fragmentVariables);
    // Every fragment builtin but the view index, which would need the
    // WGSL section dropped.
    Put32(fragment + 36, Byte(in) & 0x3Fu);
    for (uint32_t i = 0, count = Byte(in) % 4; i < count; ++i)
    {
        uint8_t* constant = Record(CONSTANTS, i, 16);
        Put32(constant, Byte(in) % 12);
        constant[4] = Byte(in) % 6;
        constant[12] = Byte(in) % 2;
    }
    Assemble();
}

// The pipeline's state the next input byte changes.
enum
{
    OP_VERTEX_ENTRY,
    OP_FRAGMENT_ENTRY,
    OP_BUFFERS,
    OP_ATTRIBUTES,
    OP_PRIMITIVE,
    OP_DEPTH,
    OP_STENCIL,
    OP_MULTISAMPLE,
    OP_TARGET_COUNT,
    OP_TARGET,
    OP_CONSTANTS,
    OP_VIEWS,
    OP_HEAD,
    OPS,
};

// The head of a def: its label, cookie, extensions and shader.
typedef struct Head
{
    uint32_t* cookie;
    const mrhiChain** next;
    const char** label;
    size_t* labelLength;
    mrhiShaderId* shader;
} Head;

static void ChangeHead(Input* in, const Device* device, Head head)
{
    static mrhiChain chain;
    switch (Byte(in) % 5)
    {
    case 0:
    {
        size_t length = Size(in) % 300;
        char* label = Byte(in) % 16 == 0 ? nullptr : Own(length);
        for (size_t i = 0; label != nullptr && i < length; ++i)
        {
            label[i] = (char)Byte(in);
        }
        *head.label = label;
        *head.labelLength = length;
        break;
    }
    case 1:
        *head.cookie = Word(in);
        break;
    case 2:
        chain = (mrhiChain){.next = nullptr, .type = Word(in)};
        *head.next = &chain;
        break;
    case 3:
        *head.shader = device->destroyed;
        break;
    default:
        *head.shader = (mrhiShaderId){Word(in), Word(in)};
        break;
    }
}

// The vertex buffers, up to the device's limit of a longer list.
static void ChangeBuffers(Input* in, const Device* device, mrhiGraphicsPipelineDef* def)
{
    uint32_t count = Size(in);
    uint32_t limit = device->limits.vertexBuffers;
    uint32_t given = count < limit ? count : limit;
    mrhiVertexBufferLayout* buffers = Byte(in) % 16 == 0 ? nullptr : Own(given * sizeof(*buffers));
    for (uint32_t i = 0; buffers != nullptr && i < given; ++i)
    {
        buffers[i] = (mrhiVertexBufferLayout){.stride = Size(in), .stepMode = Byte(in) % 3};
    }
    def->vertexBuffers = buffers;
    def->vertexBufferCount = count;
}

// The vertex attributes, up to the device's limit of a longer list.
static void ChangeAttributes(Input* in, const Device* device, mrhiGraphicsPipelineDef* def)
{
    uint32_t count = Size(in);
    uint32_t limit = device->limits.vertexAttributes;
    uint32_t given = count < limit ? count : limit;
    mrhiVertexAttribute* attributes =
        Byte(in) % 16 == 0 ? nullptr : Own(given * sizeof(*attributes));
    for (uint32_t i = 0; attributes != nullptr && i < given; ++i)
    {
        attributes[i] = (mrhiVertexAttribute){
            .buffer = Size(in),
            .location = Size(in),
            .format = Byte(in) % 43,
            .offset = Size(in),
        };
    }
    def->vertexAttributes = attributes;
    def->vertexAttributeCount = count;
}

static mrhiStencilFace Stencil(Input* in)
{
    return (mrhiStencilFace){
        .compare = Byte(in) % 10,
        .failOp = Byte(in) % 9,
        .depthFailOp = Byte(in) % 9,
        .passOp = Byte(in) % 9,
    };
}

static mrhiBlendComponent Blend(Input* in)
{
    return (mrhiBlendComponent){Byte(in) % 18, Byte(in) % 18, Byte(in) % 6};
}

static void ChangeTarget(Input* in, mrhiGraphicsPipelineDef* def)
{
    mrhiColorTargetState* target = &def->colorTargets[Byte(in) % MRHI_MAX_COLOR_TARGETS];
    target->format = Byte(in) % 72;
    target->blend = Byte(in) % 2 != 0;
    target->color = Blend(in);
    target->alpha = Blend(in);
    target->writeMask = Byte(in) % 32;
}

// Changes one state of a graphics def as the input says.
static void ChangeGraphics(Input* in, const Device* device, mrhiGraphicsPipelineDef* def)
{
    switch (Byte(in) % OPS)
    {
    case OP_VERTEX_ENTRY:
        Name(in, &def->vertexEntry, &def->vertexEntryLength);
        break;
    case OP_FRAGMENT_ENTRY:
        Name(in, &def->fragmentEntry, &def->fragmentEntryLength);
        break;
    case OP_BUFFERS:
        ChangeBuffers(in, device, def);
        break;
    case OP_ATTRIBUTES:
        ChangeAttributes(in, device, def);
        break;
    case OP_PRIMITIVE:
        def->topology = Byte(in) % 6;
        def->stripIndexFormat = Byte(in) % 4;
        def->frontFace = Byte(in) % 3;
        def->cullMode = Byte(in) % 4;
        def->unclippedDepth = Byte(in) % 2 != 0;
        break;
    case OP_DEPTH:
        def->depthStencilFormat = Byte(in) % 72;
        def->depthWrite = Byte(in) % 2 != 0;
        def->depthCompare = Byte(in) % 10;
        def->depthBias = (int32_t)Size(in);
        def->depthBiasSlopeScale = Float(in);
        def->depthBiasClamp = Float(in);
        break;
    case OP_STENCIL:
        def->stencilFront = Stencil(in);
        def->stencilBack = Stencil(in);
        def->stencilReadMask = Byte(in) % 2 != 0 ? UINT32_MAX : Word(in);
        def->stencilWriteMask = Byte(in) % 2 != 0 ? UINT32_MAX : Word(in);
        break;
    case OP_MULTISAMPLE:
        def->sampleCount = Size(in);
        def->sampleMask = Word(in);
        def->alphaToCoverage = Byte(in) % 2 != 0;
        break;
    case OP_TARGET_COUNT:
        def->colorTargetCount = Size(in);
        break;
    case OP_TARGET:
        ChangeTarget(in, def);
        break;
    case OP_CONSTANTS:
        def->constants = Constants(in, &def->constantCount);
        break;
    case OP_VIEWS:
        def->viewCount = Size(in);
        break;
    default:
        ChangeHead(in, device,
                   (Head){&def->cookie, &def->next, &def->label, &def->labelLength, &def->shader});
        break;
    }
}

// Changes one state of a compute def as the input says.
static void ChangeCompute(Input* in, const Device* device, mrhiComputePipelineDef* def)
{
    switch (Byte(in) % 3)
    {
    case 0:
        Name(in, &def->entry, &def->entryLength);
        break;
    case 1:
        def->constants = Constants(in, &def->constantCount);
        break;
    default:
        ChangeHead(in, device,
                   (Head){&def->cookie, &def->next, &def->label, &def->labelLength, &def->shader});
        break;
    }
}

// Whether given constant values name each id at most once, with an
// array for any count and no more than any container here declares.
static bool KeepsConstants(const mrhiConstantValue* values, uint32_t count)
{
    bool rules = (values != nullptr || count == 0) && count <= 3;
    for (uint32_t i = 0; rules && i < count; ++i)
    {
        for (uint32_t j = 0; j < i; ++j)
        {
            rules = rules && values[j].id != values[i].id;
        }
    }
    return rules;
}

// Whether an accepted def's vertex buffers and attributes keep WebGPU's
// rules: within the limits, strides and offsets aligned, each attribute
// in its buffer's element, and one attribute per location.
static bool KeepsVertexRules(const Device* device, const mrhiGraphicsPipelineDef* def)
{
    const mrhiLimits* limits = &device->limits;
    bool rules = def->vertexBufferCount <= limits->vertexBuffers &&
                 def->vertexAttributeCount <= limits->vertexAttributes;
    for (uint32_t i = 0; rules && i < def->vertexBufferCount; ++i)
    {
        const mrhiVertexBufferLayout* buffer = &def->vertexBuffers[i];
        rules = buffer->stride % 4 == 0 && buffer->stride <= limits->vertexStride &&
                buffer->stepMode <= mrhi_stepInstance;
    }
    for (uint32_t i = 0; rules && i < def->vertexAttributeCount; ++i)
    {
        const mrhiVertexAttribute* attribute = &def->vertexAttributes[i];
        uint32_t bytes = mrhiGetVertexLayout(attribute->format).bytes;
        uint64_t end = (uint64_t)attribute->offset + bytes;
        rules = attribute->buffer < def->vertexBufferCount && bytes > 0 &&
                attribute->offset % (bytes < 4 ? bytes : 4) == 0 &&
                attribute->location < limits->vertexAttributes &&
                end <= (def->vertexBuffers[attribute->buffer].stride != 0
                            ? def->vertexBuffers[attribute->buffer].stride
                            : limits->vertexStride);
        for (uint32_t j = 0; rules && j < i; ++j)
        {
            rules = def->vertexAttributes[j].location != attribute->location;
        }
    }
    return rules;
}

// Whether an accepted graphics def keeps the rules of WebGPU that the
// def and the device decide alone.
static bool KeepsGraphicsRules(const Device* device, const mrhiGraphicsPipelineDef* def)
{
    bool strip =
        def->topology == mrhi_topologyLineStrip || def->topology == mrhi_topologyTriangleStrip;
    bool triangles =
        def->topology == mrhi_topologyTriangleList || def->topology == mrhi_topologyTriangleStrip;
    bool biased =
        def->depthBias != 0 || def->depthBiasSlopeScale != 0.0f || def->depthBiasClamp != 0.0f;
    bool depth = mrhiFormatHasDepth(def->depthStencilFormat);
    bool attached = def->depthStencilFormat != mrhi_formatNone;
    for (uint32_t i = 0; i < def->colorTargetCount && i < MRHI_MAX_COLOR_TARGETS; ++i)
    {
        attached = attached || def->colorTargets[i].format != mrhi_formatNone;
    }
    uint32_t samples = def->sampleCount;
    return def->vertexEntry != nullptr && KeepsVertexRules(device, def) &&
           KeepsConstants(def->constants, def->constantCount) &&
           def->topology <= mrhi_topologyTriangleStrip &&
           (strip ? def->stripIndexFormat <= mrhi_indexUint32
                  : def->stripIndexFormat == mrhi_indexNone) &&
           def->frontFace <= mrhi_frontClockwise && def->cullMode <= mrhi_cullBack &&
           (!def->unclippedDepth || device->features.unclippedDepth) &&
           (def->viewCount <= 1 ||
            (device->features.multiview && def->viewCount <= device->limits.multiviewViews)) &&
           isfinite(def->depthBiasSlopeScale) && isfinite(def->depthBiasClamp) &&
           (triangles || !biased) &&
           (depth || (!def->depthWrite && def->depthCompare == mrhi_compareAlways && !biased)) &&
           def->colorTargetCount <= MRHI_MAX_COLOR_TARGETS &&
           (def->fragmentEntry != nullptr || def->colorTargetCount == 0) && samples >= 1 &&
           samples <= 16 && (samples & (samples - 1)) == 0 &&
           (!def->alphaToCoverage || (samples > 1 && def->colorTargetCount > 0)) && attached;
}

// Whether a diagnostic code is one a member of mrhiLimits refuses with.
static bool IsLimitCode(mrhiDiagnosticCode code)
{
    for (size_t field = 0; field < sizeof(mrhiLimits); ++field)
    {
        if (mrhiLimitDiagnostic(field) == code)
        {
            return true;
        }
    }
    return false;
}

// The device's diagnostics, taken: how many name a limit, and how many
// name anything else.
typedef struct Named
{
    uint32_t limits;
    uint32_t others;
} Named;

static Named Diagnostics(mrhiDevice* device)
{
    Named named = {0};
    mrhiDiagnostic record;
    while (mrhiNextDeviceDiagnostic(device, &record) == mrhi_success)
    {
        Expect(record.code != 0);
        if (IsLimitCode(record.code))
        {
            ++named.limits;
        }
        else
        {
            ++named.others;
        }
    }
    return named;
}

// Checks what a pipeline creation answered: a status it may give,
// misuse counted for invalid input or a fixed bound passed alone, such
// a def or a bad one named once in the diagnostics, and each device or
// adapter limit passed named by its own code. Capacity is a device
// limit passed, so it names one; unsupported may be a missing feature,
// which names none.
static void CheckAnswer(mrhiDevice* device, mrhiResult status, uint64_t misuse)
{
    Expect(status == mrhi_success || status == mrhi_errorInvalid || status == mrhi_errorLimit ||
           status == mrhi_errorFormat || status == mrhi_errorUnsupported ||
           status == mrhi_errorCapacity || status == mrhi_errorStale);
    bool misused = status == mrhi_errorInvalid || status == mrhi_errorLimit;
    Expect(mrhiGetDeviceMisuse(device) - misuse == (misused ? 1u : 0u));
    Named named = Diagnostics(device);
    Expect(named.others == (misused || status == mrhi_errorFormat ? 1u : 0u));
    bool refused = status != mrhi_success && status != mrhi_errorStale;
    Expect(refused || named.limits == 0);
    Expect(status != mrhi_errorCapacity || named.limits > 0);
}

// Checks that a made pipeline is answered ready once.
static void CheckReady(mrhiDevice* device, mrhiRequestId request)
{
    mrhiDeviceNotification record = {0};
    Expect(request.index1 != 0 && mrhiNextDeviceNotification(device, &record) == mrhi_success &&
           record.kind == mrhi_devicePipelineReady && record.request.index1 == request.index1 &&
           record.request.generation == request.generation && record.outcome == mrhi_success);
    Expect(mrhiNextDeviceNotification(device, &record) == mrhi_empty);
}

static void RunGraphics(Input* in, const Device* device, mrhiShaderId shader)
{
    mrhiGraphicsPipelineDef def = mrhiDefaultGraphicsPipelineDef();
    def.shader = shader;
    def.vertexEntry = "vs";
    def.vertexEntryLength = 2;
    def.fragmentEntry = "fs";
    def.fragmentEntryLength = 2;
    def.colorTargets[0].format = mrhi_formatRgba8Unorm;
    def.colorTargetCount = 1;
    while (in->at < in->size)
    {
        ChangeGraphics(in, device, &def);
    }
    mrhiGraphicsPipelineId pipeline;
    mrhiRequestId request;
    // A refusal is the same the second time.
    mrhiResult first = mrhi_success;
    for (int i = 0; i < 2; ++i)
    {
        uint64_t misuse = mrhiGetDeviceMisuse(device->device);
        mrhiResult status = mrhiCreateGraphicsPipeline(device->device, &def, &pipeline, &request);
        CheckAnswer(device->device, status, misuse);
        Expect(i == 0 || status == first);
        first = status;
        if (status == mrhi_success)
        {
            break;
        }
        Expect(pipeline.index1 == 0 && pipeline.generation == 0 && request.index1 == 0 &&
               request.generation == 0);
    }
    if (first == mrhi_success)
    {
        Expect(KeepsGraphicsRules(device, &def));
        CheckReady(device->device, request);
        Expect(mrhiDestroyGraphicsPipeline(device->device, pipeline) == mrhi_success &&
               mrhiDestroyGraphicsPipeline(device->device, pipeline) == mrhi_errorStale);
    }
}

static void RunCompute(Input* in, const Device* device, mrhiShaderId shader)
{
    mrhiComputePipelineDef def = mrhiDefaultComputePipelineDef();
    def.shader = shader;
    def.entry = "cs";
    def.entryLength = 2;
    while (in->at < in->size)
    {
        ChangeCompute(in, device, &def);
    }
    mrhiComputePipelineId pipeline;
    mrhiRequestId request;
    mrhiResult first = mrhi_success;
    for (int i = 0; i < 2; ++i)
    {
        uint64_t misuse = mrhiGetDeviceMisuse(device->device);
        mrhiResult status = mrhiCreateComputePipeline(device->device, &def, &pipeline, &request);
        CheckAnswer(device->device, status, misuse);
        Expect(i == 0 || status == first);
        first = status;
        if (status == mrhi_success)
        {
            break;
        }
        Expect(pipeline.index1 == 0 && pipeline.generation == 0 && request.index1 == 0 &&
               request.generation == 0);
    }
    if (first == mrhi_success)
    {
        Expect(def.entry != nullptr && KeepsConstants(def.constants, def.constantCount));
        CheckReady(device->device, request);
        Expect(mrhiDestroyComputePipeline(device->device, pipeline) == mrhi_success &&
               mrhiDestroyComputePipeline(device->device, pipeline) == mrhi_errorStale);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

// The first byte picks the device (bit 0), a compute pipeline over a
// graphics one (bit 1) and a container built from the bytes that follow
// over the conformance container (bit 2); the rest changes the def.
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (s_devices[0].device == nullptr)
    {
        Open(&s_devices[0], true);
        Open(&s_devices[1], false);
    }
    Input in = {.data = data, .size = size};
    uint8_t mode = Byte(&in);
    const Device* device = &s_devices[mode & 1];
    mrhiShaderId shader = device->conformance;
    mrhiShaderId built = {0};
    if ((mode & 4) != 0)
    {
        Interface(&in);
        built = MakeShader(device->device, s_container, s_size);
        shader = built;
    }
    Diagnostics(device->device);
    if (shader.index1 != 0 && (mode & 2) != 0)
    {
        RunCompute(&in, device, shader);
    }
    else if (shader.index1 != 0)
    {
        RunGraphics(&in, device, shader);
    }
    if (built.index1 != 0)
    {
        Expect(mrhiDestroyShader(device->device, built) == mrhi_success);
    }
    FreeOwned();
    return 0;
}
