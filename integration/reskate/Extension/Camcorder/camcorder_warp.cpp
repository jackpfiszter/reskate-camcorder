#include "camcorder_warp.h"
#include "camcorder_runtime.h"
#include "Engine/Core/Log/logging.h"
#include <backends/imgui_impl_dx12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <climits>
#include <utility>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;

struct Warp {
    ID3D12Device *device{};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    ComPtr<ID3D12Resource> texture;
    UINT64 width{};
    UINT height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    bool requested{}, copy_wanted{}, drawn{}, copied{};
    DXGI_FORMAT output{DXGI_FORMAT_UNKNOWN};
    int transfer{};
    // The picture pass (built on first ask).
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    bool pass_tried{}, pass_ready{};
    PicturePass pass{}; // this frame's numbers, for the draw callback
    ComPtr<ID3D12Fence> fence;
    UINT64 last_use{}; // the newest frame that drew from (or copied into) the texture
    bool working{}, failure_said{};
};
Warp &warp() {
    static auto *value = new Warp;
    return *value;
}
void transition(ID3D12GraphicsCommandList *commands, ID3D12Resource *resource, D3D12_RESOURCE_STATES from,
                D3D12_RESOURCE_STATES to) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = from;
    barrier.Transition.StateAfter = to;
    commands->ResourceBarrier(1, &barrier);
}
bool idle(const Warp &w) { return !w.fence || w.fence->GetCompletedValue() >= w.last_use; }

// The picture pass's shader (checked with Microsoft's dxc in development; compiled here as SM 5.0).
constexpr char picture_shader[] = R"hlsl(
// The camcorder's picture: the game's frame (a copy, t0) bent through the fisheye and given the
// camera's look, drawn over the 4:3 picture. camcorder_warp.cpp fills the constants.
cbuffer Pass : register(b0) {
    float4 rect;    // the picture on screen: left, top, width, height (pixels)
    float4 screen;  // the copy: width, height, 1 / width, 1 / height
    float4 lens;    // warped (0 / 1), half the picture's width in radians, tan(half the render's height), render aspect
    float4 tone;    // look on (0 / 1), black crush, highlight gain, saturation
    float4 colour;  // red gain, blue gain, sharpen, grain
    float4 tape;    // chroma width, softness (both in pixels of a 720-wide picture), screen pixels per such pixel, frame
    float4 output;  // transfer (0 SDR, 1 scRGB, 2 HDR10), white (nits), highlight bleed (0 / 1), unused
};
Texture2D picture : register(t0);
SamplerState linear_clamp : register(s0);

float4 vs(uint id : SV_VertexID) : SV_Position {
    // One triangle over the whole screen; the scissor keeps it to the picture.
    float2 uv = float2((id << 1) & 2, id & 2);
    return float4(uv * float2(2, -2) + float2(-1, 1), 0, 1);
}

// A screen pixel as a point of the picture: x right in [-1, 1], y up in [-0.75, 0.75].
float2 picture_point(float2 pixel) {
    return float2((pixel.x - rect.x) / rect.z * 2 - 1, (0.5 - (pixel.y - rect.y) / rect.w) * 1.5);
}
// Where that point is in the game's frame: through the equidistant fisheye (vx1000.h,
// fisheye_source and warp_uv), or straight across for a flat lens.
float2 source_uv(float2 pixel) {
    if (lens.x < 0.5) return pixel * screen.zw;
    float2 p = picture_point(pixel);
    float rho = length(p);
    float2 t = rho > 1e-6 ? p * (tan(min(rho * lens.y, 1.4835298)) / rho) : float2(0, 0);
    return float2(0.5 + 0.5 * t.x / (lens.z * lens.w), 0.5 - 0.5 * t.y / lens.z);
}

float3 srgb_encode(float3 l) {
    l = saturate(l);
    return lerp(12.92 * l, 1.055 * pow(l, 1 / 2.4) - 0.055, step(0.0031308, l));
}
float3 srgb_decode(float3 e) {
    e = saturate(e);
    return lerp(e / 12.92, pow((e + 0.055) / 1.055, 2.4), step(0.04045, e));
}
static const float pq_m1 = 0.1593017578125, pq_m2 = 78.84375, pq_c1 = 0.8359375, pq_c2 = 18.8515625, pq_c3 = 18.6875;
// The look works on picture values as an SDR camera's (sRGB, white 1); HDR is brought into that and back.
float3 into_look(float3 c) {
    if (output.x < 0.5) return c;
    float3 lin;
    if (output.x < 1.5) {
        lin = c * (80 / output.y); // scRGB: 1 is 80 nits
    } else {
        float3 p = pow(saturate(c), 1 / pq_m2);
        float3 nits = 10000 * pow(max(p - pq_c1, 0) / (pq_c2 - pq_c3 * p), 1 / pq_m1);
        float3 wide = nits / output.y;
        lin = float3(dot(wide, float3(1.6605, -0.5876, -0.0728)), dot(wide, float3(-0.1246, 1.1329, -0.0083)),
                     dot(wide, float3(-0.0182, -0.1006, 1.1187)));
    }
    return srgb_encode(lin);
}
float3 out_of_look(float3 e) {
    if (output.x < 0.5) return e;
    float3 lin = srgb_decode(e);
    if (output.x < 1.5) return lin * (output.y / 80);
    float3 wide = float3(dot(lin, float3(0.6274, 0.3293, 0.0433)), dot(lin, float3(0.0691, 0.9195, 0.0114)),
                         dot(lin, float3(0.0164, 0.0880, 0.8956)));
    float3 y = pow(saturate(wide * (output.y / 10000)), pq_m1);
    return pow((pq_c1 + pq_c2 * y) / (1 + pq_c3 * y), pq_m2);
}

float luma(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
float3 sample_at(float2 pixel, float along) {
    return into_look(picture.SampleLevel(linear_clamp, source_uv(pixel + float2(along, 0)), 0).rgb);
}
// Noise for one 720-wide pixel and frame, about -1 to 1 (two draws added, as in look.cpp).
float grain_noise(float2 cell, float frame) {
    uint h = (uint)cell.x * 0x85EBCA6Bu ^ (uint)cell.y * 0xC2B2AE35u ^ (uint)frame * 0x9E3779B9u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    return ((float)(h & 255u) + (float)((h >> 8) & 255u) - 255.0) / 255.0;
}

float4 ps(float4 position : SV_Position) : SV_Target {
    float2 pixel = position.xy;
    if (tone.x < 0.5) return float4(picture.SampleLevel(linear_clamp, source_uv(pixel), 0).rgb, 1);
    float step_px = tape.z; // screen pixels in one pixel of a 720-wide picture
    float3 centre = sample_at(pixel, 0);
    // The tape: brightness softened over `softness`, colour averaged over `chroma width`, along the line.
    float chroma_half = tape.x * 0.5 * step_px, soft = tape.y * step_px;
    float reach = max(chroma_half, soft);
    float3 chroma = 0;
    float chroma_count = 0, bright = 0, bright_count = 0;
    [unroll] for (int i = -4; i <= 4; ++i) {
        float along = reach * i / 4.0;
        float3 c = i == 0 ? centre : sample_at(pixel, along);
        float y = luma(c);
        if (abs(along) <= chroma_half + 0.01) { chroma += c - y; chroma_count += 1; }
        if (abs(along) <= soft + 0.01) { bright += y; bright_count += 1; }
    }
    float y = bright / bright_count;
    // The camera's edge enhancement, on brightness, a 720-wide pixel either side.
    float detail = 2 * luma(centre) - luma(sample_at(pixel, -step_px)) - luma(sample_at(pixel, step_px));
    y += detail * colour.z * 0.5;
    float3 c = y + chroma / chroma_count;
    // White balance, dense blacks, early white, saturation, and blown highlights going white.
    c *= float3(colour.x, 1, colour.y);
    c = max(c - tone.y, 0) / (1 - tone.y) * tone.z;
    float l = luma(c);
    c = l + (c - l) * tone.w;
    float top = max(c.r, max(c.g, c.b));
    if (output.z > 0.5 && top > 236.0 / 255) c += (1 - c) * saturate((top - 236.0 / 255) / (19.0 / 255));
    // Grain, the same on every channel.
    float2 cell = floor((pixel - rect.xy) / max(step_px, 1e-3));
    c += grain_noise(cell, tape.w) * colour.w * (56.0 / 255);
    return float4(out_of_look(saturate(c)), 1);
}
)hlsl";

ComPtr<ID3DBlob> compile(const char *entry, const char *target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(picture_shader, sizeof(picture_shader) - 1, "camcorder_picture", nullptr, nullptr, entry, target,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        logging::printf(logging::Level::warning, logging::Channel::graphics, "Camcorder: the picture shader (%s) did not compile: %s",
                        entry, errors ? static_cast<const char *>(errors->GetBufferPointer()) : "no reason given");
        return {};
    }
    return code;
}

// The root signature (the numbers as root constants, the copy as one SRV, a linear clamped sampler)
// and the pipeline, writing straight to the back buffer.
bool build_pass(Warp &w) {
    const auto vertex = compile("vs", "vs_5_0"), pixel = compile("ps", "ps_5_0");
    if (!vertex || !pixel) return false;
    D3D12_DESCRIPTOR_RANGE range{};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    range.NumDescriptors = 1;
    range.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
    D3D12_ROOT_PARAMETER parameters[2]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = sizeof(PicturePass) / 4;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[1].DescriptorTable.NumDescriptorRanges = 1;
    parameters[1].DescriptorTable.pDescriptorRanges = &range;
    parameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 2;
    description.pParameters = parameters;
    description.NumStaticSamplers = 1;
    description.pStaticSamplers = &sampler;
    ComPtr<ID3DBlob> serialized, errors;
    if (FAILED(D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors)) ||
        FAILED(w.device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&w.root))))
        return false;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
    state.pRootSignature = w.root.Get();
    state.VS = {vertex->GetBufferPointer(), vertex->GetBufferSize()};
    state.PS = {pixel->GetBufferPointer(), pixel->GetBufferSize()};
    state.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    state.SampleMask = UINT_MAX;
    state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    state.RasterizerState.DepthClipEnable = TRUE;
    state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    state.NumRenderTargets = 1;
    state.RTVFormats[0] = w.output;
    state.SampleDesc.Count = 1;
    return SUCCEEDED(w.device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&w.pipeline)));
}

// The draw callback: the picture pass, in the middle of the overlay's draws (ImGui's own state is
// put back by the ImDrawCallback_ResetRenderState that follows it).
void draw_pass(const ImDrawList *, const ImDrawCmd *) {
    auto &w = warp();
    const auto *render = static_cast<ImGui_ImplDX12_RenderState *>(ImGui::GetPlatformIO().Renderer_RenderState);
    if (!render || !render->CommandList || !w.pipeline) return;
    auto *commands = render->CommandList;
    commands->SetGraphicsRootSignature(w.root.Get());
    commands->SetPipelineState(w.pipeline.Get());
    commands->SetGraphicsRoot32BitConstants(0, sizeof(PicturePass) / 4, &w.pass, 0);
    commands->SetGraphicsRootDescriptorTable(1, w.gpu);
    const D3D12_VIEWPORT viewport{0, 0, w.pass.screen[0], w.pass.screen[1], 0, 1};
    commands->RSSetViewports(1, &viewport);
    const D3D12_RECT scissor{static_cast<LONG>(w.pass.rect[0]), static_cast<LONG>(w.pass.rect[1]),
                             static_cast<LONG>(w.pass.rect[0] + w.pass.rect[2] + 0.5f),
                             static_cast<LONG>(w.pass.rect[1] + w.pass.rect[3] + 0.5f)};
    commands->RSSetScissorRects(1, &scissor);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    commands->DrawInstanced(3, 1, 0, 0);
}
// A texture like the back buffer, in COPY_DEST, with its view in the heap slot. The view reads
// alpha as 1: what the game leaves in its back buffer's alpha means nothing.
bool make_texture(Warp &w, const D3D12_RESOURCE_DESC &back) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = back.Width;
    desc.Height = back.Height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = back.Format;
    desc.SampleDesc = {1, 0};
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> made;
    if (FAILED(w.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&made))))
        return false;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    view.Texture2D.MipLevels = 1;
    w.device->CreateShaderResourceView(made.Get(), &view, w.cpu);
    w.texture = std::move(made);
    w.width = desc.Width;
    w.height = desc.Height;
    w.format = desc.Format;
    return true;
}
} // namespace

void camcorder_warp_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                         DXGI_FORMAT format, int transfer) {
    auto &w = warp();
    w.device = device;
    w.cpu = cpu;
    w.gpu = gpu;
    if (format != w.output) { // a new swap chain format needs a new pipeline
        w.pipeline.Reset();
        w.root.Reset();
        w.pass_tried = w.pass_ready = false;
    }
    w.output = format;
    w.transfer = transfer;
}

bool camcorder_picture_pass_ready() {
    auto &w = warp();
    if (!w.pass_tried && w.device && w.output != DXGI_FORMAT_UNKNOWN) {
        w.pass_tried = true;
        w.pass_ready = build_pass(w);
        logging::write(w.pass_ready ? logging::Level::info : logging::Level::warning, logging::Channel::graphics,
                       w.pass_ready ? "Camcorder: picture pass ready (the look shows on screen)."
                                    : "Camcorder: no picture pass; the fisheye is drawn as a mesh and the look is left to clips.");
    }
    return w.pass_ready;
}

int camcorder_warp_transfer() { return warp().transfer; }

void camcorder_picture_pass(ImDrawList *list, const PicturePass &pass) {
    auto &w = warp();
    if (!w.pass_ready) return;
    w.pass = pass;
    w.drawn = true;
    list->AddCallback(draw_pass, nullptr);
    list->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
}

std::optional<ImTextureID> camcorder_warp_request(int width, int height, bool copy) {
    auto &w = warp();
    if (!w.device || !w.gpu.ptr) return std::nullopt;
    w.requested = true;
    // A held picture (a film camera's cadence) is only held while there is one of this size.
    const bool have = w.texture && w.width == static_cast<UINT64>(width) && w.height == static_cast<UINT>(height);
    w.copy_wanted = copy || !have;
    if (!w.texture || w.width != static_cast<UINT64>(width) || w.height != static_cast<UINT>(height)) return std::nullopt;
    w.drawn = true;
    return static_cast<ImTextureID>(w.gpu.ptr);
}

void camcorder_warp_copy(ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer) {
    auto &w = warp();
    if (!std::exchange(w.requested, false) || !std::exchange(w.copy_wanted, false) || !commands || !back_buffer || !w.device)
        return;
    const auto back = back_buffer->GetDesc();
    if (back.SampleDesc.Count != 1) {
        camcorder_runtime::note_warp_failed();
        return;
    }
    bool fresh = false;
    if (!w.texture || w.width != back.Width || w.height != back.Height || w.format != back.Format) {
        // The old texture (and its view) may still be in use by frames on the GPU.
        if (!idle(w)) return;
        w.texture.Reset();
        if (!make_texture(w, back)) {
            if (!std::exchange(w.failure_said, true))
                logging::write(logging::Level::warning, logging::Channel::graphics,
                               "Camcorder: no texture for the fisheye warp; the picture stays flat.");
            camcorder_runtime::note_warp_failed();
            return;
        }
        fresh = true;
    }
    if (!fresh) transition(commands, w.texture.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST);
    transition(commands, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_SOURCE);
    commands->CopyResource(w.texture.Get(), back_buffer);
    transition(commands, back_buffer, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_PRESENT);
    transition(commands, w.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    w.copied = true;
}

void camcorder_warp_submitted(ID3D12Fence *fence, UINT64 signal) {
    auto &w = warp();
    if (w.copied || w.drawn) {
        w.fence = fence;
        w.last_use = signal;
        if (w.drawn && !std::exchange(w.working, true))
            logging::write(logging::Level::info, logging::Channel::graphics, "Camcorder: fisheye warp drawing.");
    }
    w.copied = w.drawn = false;
}

void camcorder_warp_release() {
    auto &w = warp();
    for (int waited = 0; waited < 500 && !idle(w); ++waited) Sleep(1);
    w.texture.Reset();
    w.fence.Reset();
    w.pipeline.Reset();
    w.root.Reset();
    w.pass_tried = w.pass_ready = false;
    w.width = 0;
    w.height = 0;
    w.format = DXGI_FORMAT_UNKNOWN;
    w.requested = w.copy_wanted = w.drawn = w.copied = false;
    w.device = nullptr;
    w.cpu = {};
    w.gpu = {};
}
} // namespace dingosdk::overlay
