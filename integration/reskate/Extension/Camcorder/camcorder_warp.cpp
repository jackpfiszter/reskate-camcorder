#include "camcorder_warp.h"
#include "camcorder_runtime.h"
#include "camcorder_shader.h"
#include "Engine/Core/Log/logging.h"
#include "Extension/UI/Overlay/overlay.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <future>
#include <string>
#include <utility>

namespace dingosdk::overlay {
namespace {
using Microsoft::WRL::ComPtr;
using camcorder::EffectPass;
using camcorder::Slot;

// The picture's textures, after the copy: two for the picture as it goes (each pass reads one and
// writes the other), two spares (blocks drawn in several passes), and the picture a moment ago.
enum Target : int { picture_a, picture_b, spare_a, spare_b, history, targets };
// Our own descriptor heap: the copy's view, an empty 3D view, then a ring the targets' and colour
// tables' views are made in. A view is made anew in the ring's next slot rather than over an old
// one a frame still on the GPU may read; the ring comes round again only after hundreds of new views
// (each a resize or a new colour table), long after those frames are done.
constexpr UINT copy_view = 0, no_table_view = 1, first_ring_view = 2, views = 256;
constexpr int tables = 4;
constexpr DXGI_FORMAT picture_format = DXGI_FORMAT_R16G16B16A16_FLOAT;

// The shader's constant buffer (camcorder_shader.h, Constants).
struct Constants {
    float rect[4]{}, screen[4]{}, clock[4]{}, job[4]{};
    float v[camcorder::effect_values]{};
};
static_assert(sizeof(Constants) == 48 * 4, "48 root constants");

struct Table {
    std::uint64_t hash{};
    ComPtr<ID3D12Resource> texture, upload;
    std::uint64_t used{}; // the chain number that last used it
    UINT view{};          // its view's slot
};

struct Warp {
    ID3D12Device *device{};
    D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpu{};
    ComPtr<ID3D12Resource> texture; // the copy
    UINT64 width{};
    UINT height{};
    DXGI_FORMAT format{DXGI_FORMAT_UNKNOWN};
    bool requested{}, copy_wanted{}, drawn{}, copied{};
    DXGI_FORMAT output{DXGI_FORMAT_UNKNOWN};
    int transfer{};
    // The picture shader (compiled once a game, away from the render thread) and its pipelines.
    std::future<std::pair<ComPtr<ID3DBlob>, ComPtr<ID3DBlob>>> compiling;
    ComPtr<ID3DBlob> vertex_code, pixel_code; // kept across swap chains
    bool compile_started{}, compile_failed{}, pass_failure_said{};
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> to_picture, to_screen; // into our textures, and onto the back buffer
    bool pass_tried{}, pass_ready{};
    // Our descriptor heaps, textures and colour tables.
    ComPtr<ID3D12DescriptorHeap> views, outputs;
    UINT view_step{}, output_step{};
    std::array<ComPtr<ID3D12Resource>, targets> target{};
    std::array<D3D12_RESOURCE_STATES, targets> state{};
    std::array<UINT, targets> target_view{}; // their views' slots
    UINT next_view{first_ring_view};
    UINT64 target_width{};
    UINT target_height{};
    bool history_valid{};
    std::array<Table, tables> table{};
    std::vector<std::pair<UINT64, ComPtr<ID3D12Resource>>> retired; // freed once the GPU is past them
    // This frame's chain, from frame building.
    PictureChain chain;
    bool chain_wanted{};
    std::uint64_t chains{}; // chains drawn
    UINT64 history_at{};    // the frame before the one the history was kept in (its last_signal then)
    ComPtr<ID3D12Fence> fence;
    UINT64 last_use{}, last_signal{}; // the newest frame that used our textures; the newest submitted
    bool working{}, failure_said{};
    std::string why_not; // why the screen has no picture chain this frame, for the notice
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
// A texture the GPU may still be drawing with, let go of once it is done.
void retire(Warp &w, ComPtr<ID3D12Resource> &resource) {
    if (resource) w.retired.emplace_back(w.last_signal + 1, std::move(resource));
    resource.Reset();
}
void free_retired(Warp &w) {
    const UINT64 done = w.fence ? w.fence->GetCompletedValue() : UINT64_MAX;
    std::erase_if(w.retired, [&](const auto &r) { return r.first <= done; });
}

ComPtr<ID3DBlob> compile(const char *entry, const char *target) {
    ComPtr<ID3DBlob> code, errors;
    if (FAILED(D3DCompile(camcorder_picture_shader, sizeof(camcorder_picture_shader) - 1, "camcorder_picture", nullptr, nullptr,
                          entry, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors))) {
        logging::printf(logging::Level::warning, logging::Channel::graphics, "Camcorder: the picture shader (%s) did not compile: %s",
                        entry, errors ? static_cast<const char *>(errors->GetBufferPointer()) : "no reason given");
        return {};
    }
    return code;
}

D3D12_CPU_DESCRIPTOR_HANDLE view_cpu(const Warp &w, UINT slot) {
    auto h = w.views->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * w.view_step;
    return h;
}
D3D12_GPU_DESCRIPTOR_HANDLE view_gpu(const Warp &w, UINT slot) {
    auto h = w.views->GetGPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<UINT64>(slot) * w.view_step;
    return h;
}
D3D12_CPU_DESCRIPTOR_HANDLE output_cpu(const Warp &w, UINT slot) {
    auto h = w.outputs->GetCPUDescriptorHandleForHeapStart();
    h.ptr += static_cast<SIZE_T>(slot) * w.output_step;
    return h;
}
UINT new_view(Warp &w) {
    const UINT slot = w.next_view;
    w.next_view = slot + 1 < views ? slot + 1 : first_ring_view;
    return slot;
}
// A view of the copy in our heap (the overlay's own slot has the other, for the ImGui mesh).
void view_copy(Warp &w) {
    if (!w.views || !w.texture) return;
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = w.format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    view.Shader4ComponentMapping = D3D12_ENCODE_SHADER_4_COMPONENT_MAPPING(
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_0, D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_1,
        D3D12_SHADER_COMPONENT_MAPPING_FROM_MEMORY_COMPONENT_2, D3D12_SHADER_COMPONENT_MAPPING_FORCE_VALUE_1);
    view.Texture2D.MipLevels = 1;
    w.device->CreateShaderResourceView(w.texture.Get(), &view, view_cpu(w, copy_view));
}

// The root signature (the numbers as 48 root constants; three textures, each its own table: what
// a pass reads, the other picture it may need, and a colour table; a smooth and a nearest sampler)
// and the two pipelines.
bool build_pass(Warp &w) {
    D3D12_DESCRIPTOR_RANGE ranges[3]{};
    D3D12_ROOT_PARAMETER parameters[4]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants.Num32BitValues = sizeof(Constants) / 4;
    parameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    for (UINT i = 0; i < 3; ++i) {
        ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[i].NumDescriptors = 1;
        ranges[i].BaseShaderRegister = i;
        ranges[i].OffsetInDescriptorsFromTableStart = 0;
        parameters[i + 1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        parameters[i + 1].DescriptorTable.NumDescriptorRanges = 1;
        parameters[i + 1].DescriptorTable.pDescriptorRanges = &ranges[i];
        parameters[i + 1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_STATIC_SAMPLER_DESC samplers[2]{};
    for (UINT i = 0; i < 2; ++i) {
        samplers[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_LINEAR : D3D12_FILTER_MIN_MAG_MIP_POINT;
        samplers[i].AddressU = samplers[i].AddressV = samplers[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        samplers[i].ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        samplers[i].MaxLOD = D3D12_FLOAT32_MAX;
        samplers[i].ShaderRegister = i;
        samplers[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    }
    D3D12_ROOT_SIGNATURE_DESC description{};
    description.NumParameters = 4;
    description.pParameters = parameters;
    description.NumStaticSamplers = 2;
    description.pStaticSamplers = samplers;
    ComPtr<ID3DBlob> serialized, errors;
    HRESULT hr = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors);
    if (SUCCEEDED(hr)) hr = w.device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(), IID_PPV_ARGS(&w.root));
    if (FAILED(hr)) {
        logging::printf(logging::Level::warning, logging::Channel::graphics, "Camcorder: the picture shader's root signature failed (0x%08lx).",
                        static_cast<unsigned long>(hr));
        return false;
    }
    const auto pipeline = [&](DXGI_FORMAT format, ComPtr<ID3D12PipelineState> &out) {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};
        state.pRootSignature = w.root.Get();
        state.VS = {w.vertex_code->GetBufferPointer(), w.vertex_code->GetBufferSize()};
        state.PS = {w.pixel_code->GetBufferPointer(), w.pixel_code->GetBufferSize()};
        state.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        state.SampleMask = UINT_MAX;
        state.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        state.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        state.RasterizerState.DepthClipEnable = TRUE;
        state.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        state.NumRenderTargets = 1;
        state.RTVFormats[0] = format;
        state.SampleDesc.Count = 1;
        const HRESULT made = w.device->CreateGraphicsPipelineState(&state, IID_PPV_ARGS(&out));
        if (FAILED(made))
            logging::printf(logging::Level::warning, logging::Channel::graphics, "Camcorder: the picture shader's pipeline (format %d) failed (0x%08lx).",
                            static_cast<int>(format), static_cast<unsigned long>(made));
        return SUCCEEDED(made);
    };
    if (!pipeline(picture_format, w.to_picture) || !pipeline(w.output, w.to_screen)) return false;
    // Our own heaps: the shader's views of our textures, and their render target views (and the
    // back buffer's, made each frame). Made once: a new swap chain keeps them.
    if (w.views) return true;
    D3D12_DESCRIPTOR_HEAP_DESC heap{};
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heap.NumDescriptors = views;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = w.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&w.views));
    heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    heap.NumDescriptors = targets + 1;
    heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    if (SUCCEEDED(hr)) hr = w.device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&w.outputs));
    if (FAILED(hr)) {
        logging::printf(logging::Level::warning, logging::Channel::graphics, "Camcorder: the picture shader's descriptor heaps failed (0x%08lx).",
                        static_cast<unsigned long>(hr));
        return false;
    }
    w.view_step = w.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    w.output_step = w.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    // The empty 3D view, for passes with no colour table (and the copy's, until there is a copy).
    for (const UINT i : {copy_view, no_table_view}) {
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        if (i == no_table_view) {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            view.Texture3D.MipLevels = 1;
        } else {
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Texture2D.MipLevels = 1;
        }
        w.device->CreateShaderResourceView(nullptr, &view, view_cpu(w, i));
    }
    view_copy(w);
    return true;
}

// The picture's textures at the back buffer's size (made again when it changes).
bool make_targets(Warp &w, UINT64 width, UINT height) {
    if (w.target_width == width && w.target_height == height && w.target[picture_a]) return true;
    for (auto &t : w.target) retire(w, t);
    w.history_valid = false;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = picture_format;
    desc.SampleDesc = {1, 0};
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    for (int i = 0; i < targets; ++i) {
        const HRESULT hr = w.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                             nullptr, IID_PPV_ARGS(&w.target[static_cast<std::size_t>(i)]));
        if (FAILED(hr)) {
            logging::printf(logging::Level::warning, logging::Channel::graphics, "Camcorder: no texture for the picture's effects (0x%08lx).",
                            static_cast<unsigned long>(hr));
            for (auto &t : w.target) t.Reset();
            w.target_width = 0;
            return false;
        }
        w.state[static_cast<std::size_t>(i)] = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};
        view.Format = picture_format;
        view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        view.Texture2D.MipLevels = 1;
        w.target_view[static_cast<std::size_t>(i)] = new_view(w);
        w.device->CreateShaderResourceView(w.target[static_cast<std::size_t>(i)].Get(), &view, view_cpu(w, w.target_view[static_cast<std::size_t>(i)]));
        w.device->CreateRenderTargetView(w.target[static_cast<std::size_t>(i)].Get(), nullptr, output_cpu(w, static_cast<UINT>(i)));
    }
    w.target_width = width;
    w.target_height = height;
    return true;
}

// A .cube's table as a 3D texture in one of the table slots (the least lately used gives way).
int table_slot(Warp &w, ID3D12GraphicsCommandList *commands, const camcorder::Lut3d &lut) {
    for (int i = 0; i < tables; ++i)
        if (w.table[static_cast<std::size_t>(i)].texture && w.table[static_cast<std::size_t>(i)].hash == lut.hash) {
            w.table[static_cast<std::size_t>(i)].used = w.chains;
            return i;
        }
    int slot = 0;
    for (int i = 1; i < tables; ++i)
        if (w.table[static_cast<std::size_t>(i)].used < w.table[static_cast<std::size_t>(slot)].used) slot = i;
    auto &t = w.table[static_cast<std::size_t>(slot)];
    retire(w, t.texture);
    retire(w, t.upload);
    t.hash = 0;
    const auto n = static_cast<UINT>(lut.size);
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
    desc.Width = n;
    desc.Height = n;
    desc.DepthOrArraySize = static_cast<UINT16>(n);
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    desc.SampleDesc = {1, 0};
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    if (FAILED(w.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                 IID_PPV_ARGS(&t.texture))))
        return -1;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT rows{};
    UINT64 row_size{}, total{};
    w.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &row_size, &total);
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc = {1, 0};
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    void *mapped = nullptr;
    if (FAILED(w.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                 IID_PPV_ARGS(&t.upload))) ||
        FAILED(t.upload->Map(0, nullptr, &mapped))) {
        t.texture.Reset();
        t.upload.Reset();
        return -1;
    }
    auto *base = static_cast<std::uint8_t *>(mapped) + footprint.Offset;
    const std::size_t pitch = footprint.Footprint.RowPitch, slice = pitch * footprint.Footprint.Height;
    for (UINT z = 0; z < n; ++z)
        for (UINT y = 0; y < n; ++y) {
            auto *row = reinterpret_cast<float *>(base + z * slice + y * pitch);
            for (UINT x = 0; x < n; ++x) {
                const std::size_t at = ((static_cast<std::size_t>(z) * n + y) * n + x) * 3;
                row[x * 4 + 0] = lut.rgb[at];
                row[x * 4 + 1] = lut.rgb[at + 1];
                row[x * 4 + 2] = lut.rgb[at + 2];
                row[x * 4 + 3] = 1;
            }
        }
    t.upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION to{}, from{};
    to.pResource = t.texture.Get();
    to.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = t.upload.Get();
    from.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    from.PlacedFootprint = footprint;
    commands->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
    transition(commands, t.texture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    D3D12_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format;
    view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
    view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    view.Texture3D.MipLevels = 1;
    t.view = new_view(w);
    w.device->CreateShaderResourceView(t.texture.Get(), &view, view_cpu(w, t.view));
    t.hash = lut.hash;
    t.used = w.chains;
    return slot;
}

// The chain: the lens from the copy, each effect, then out onto the back buffer.
void draw_chain(Warp &w, ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer) {
    const auto back = back_buffer->GetDesc();
    if (!make_targets(w, back.Width, back.Height)) return;
    ++w.chains;
    // The picture a moment ago is only that if the chain kept it the frame before.
    if (w.history_at + 1 != w.last_signal) w.history_valid = false;
    const auto &c = w.chain;
    // The overlay lays out in its own units (the window's); the passes draw in the back buffer's
    // pixels, which can be more or fewer (display scaling, an upscaler, a render size of its own).
    const float sx = c.screen[0] > 0 ? static_cast<float>(back.Width) / c.screen[0] : 1;
    const float sy = c.screen[1] > 0 ? static_cast<float>(back.Height) / c.screen[1] : 1;
    Constants k;
    k.rect[0] = c.rect[0] * sx;
    k.rect[1] = c.rect[1] * sy;
    k.rect[2] = c.rect[2] * sx;
    k.rect[3] = c.rect[3] * sy;
    k.screen[0] = static_cast<float>(back.Width);
    k.screen[1] = static_cast<float>(back.Height);
    k.screen[2] = 1 / k.screen[0];
    k.screen[3] = 1 / k.screen[1];
    k.clock[0] = c.frame;
    k.clock[1] = c.seconds;
    k.clock[2] = k.rect[2] / 720;
    k.clock[3] = static_cast<float>(w.transfer);
    k.job[3] = 200; // the white an HDR game's picture is taken to have, as the recording takes it
    if (!(k.rect[2] >= 1) || !(k.rect[3] >= 1)) return;

    // The back buffer's render target view, in our heap's last slot.
    const auto screen_view = output_cpu(w, targets);
    w.device->CreateRenderTargetView(back_buffer, nullptr, screen_view);

    ID3D12DescriptorHeap *heaps[]{w.views.Get()};
    commands->SetDescriptorHeaps(1, heaps);
    commands->SetGraphicsRootSignature(w.root.Get());
    const D3D12_VIEWPORT viewport{0, 0, k.screen[0], k.screen[1], 0, 1};
    commands->RSSetViewports(1, &viewport);
    const D3D12_RECT scissor{static_cast<LONG>(std::floor(k.rect[0])), static_cast<LONG>(std::floor(k.rect[1])),
                             static_cast<LONG>(std::ceil(k.rect[0] + k.rect[2])), static_cast<LONG>(std::ceil(k.rect[1] + k.rect[3]))};
    commands->RSSetScissorRects(1, &scissor);
    commands->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    const auto use = [&](int t, D3D12_RESOURCE_STATES to) {
        auto &from = w.state[static_cast<std::size_t>(t)];
        if (from != to) {
            transition(commands, w.target[static_cast<std::size_t>(t)].Get(), from, to);
            from = to;
        }
    };
    int current = picture_a;
    ID3D12PipelineState *bound = nullptr;
    // One pass: `program` with `values`, reading `read` (a target, or -1 the copy) and `other`, into
    // `write` (a target, or -1 the back buffer).
    const auto draw = [&](int program, int read, int other, int write, float mix, bool mix_with_other,
                          const std::array<float, camcorder::effect_values> &values, int table) {
        if (read >= 0) use(read, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (other >= 0) use(other, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        if (write >= 0) use(write, D3D12_RESOURCE_STATE_RENDER_TARGET);
        auto *pipeline = write >= 0 ? w.to_picture.Get() : w.to_screen.Get();
        if (pipeline != bound) {
            commands->SetPipelineState(pipeline);
            bound = pipeline;
        }
        k.job[0] = static_cast<float>(program);
        k.job[1] = mix;
        k.job[2] = mix_with_other ? 1.0f : 0.0f;
        std::copy(values.begin(), values.end(), k.v);
        commands->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &k, 0);
        const auto view_of = [&](int t) { return t >= 0 ? w.target_view[static_cast<std::size_t>(t)] : copy_view; };
        commands->SetGraphicsRootDescriptorTable(1, view_gpu(w, view_of(read)));
        commands->SetGraphicsRootDescriptorTable(2, view_gpu(w, view_of(other >= 0 ? other : read)));
        commands->SetGraphicsRootDescriptorTable(3, view_gpu(w, table >= 0 ? w.table[static_cast<std::size_t>(table)].view : no_table_view));
        const auto target = write >= 0 ? output_cpu(w, static_cast<UINT>(write)) : screen_view;
        commands->OMSetRenderTargets(1, &target, FALSE, nullptr);
        commands->DrawInstanced(3, 1, 0, 0);
    };

    const bool direct = !c.look && c.passes.empty();
    if (direct) transition(commands, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    // The lens.
    std::array<float, camcorder::effect_values> values{};
    std::copy(std::begin(c.lens), std::end(c.lens), values.begin());
    values[4] = c.projection;
    values[5] = c.look ? 1.0f : 0.0f;
    draw(0, -1, -1, direct ? -1 : current, 1, false, values, -1);
    if (!direct) {
        const auto next = [&]() -> int { return current == picture_a ? picture_b : picture_a; };
        for (const auto &pass : c.passes) {
            const auto slot = [&](Slot s) {
                switch (s) {
                case Slot::scratch_a: return static_cast<int>(spare_a);
                case Slot::scratch_b: return static_cast<int>(spare_b);
                case Slot::history: return w.history_valid ? static_cast<int>(history) : current;
                case Slot::next: return next();
                case Slot::current: break;
                }
                return current;
            };
            int table = -1;
            if (pass.lut) {
                table = table_slot(w, commands, *pass.lut);
                if (table < 0) continue;
            }
            const int write = slot(pass.write);
            draw(pass.program, slot(pass.read), slot(pass.other), write, pass.mix, pass.mix_with_other, pass.values, table);
            if (pass.write == Slot::next) current = write;
            if (pass.keep) {
                use(write, D3D12_RESOURCE_STATE_COPY_SOURCE);
                use(history, D3D12_RESOURCE_STATE_COPY_DEST);
                commands->CopyResource(w.target[history].Get(), w.target[static_cast<std::size_t>(write)].Get());
                w.history_valid = true;
                w.history_at = w.last_signal;
            }
        }
        // Out onto the screen.
        transition(commands, back_buffer, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        values = {};
        values[0] = c.look ? 1.0f : 0.0f;
        draw(2, current, -1, -1, 1, false, values, -1);
    }
    transition(commands, back_buffer, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    w.drawn = true;
}

// A texture like the back buffer, in COPY_DEST, with its view in the overlay's heap slot. The view
// reads alpha as 1: what the game leaves in its back buffer's alpha means nothing.
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
    view_copy(w);
    return true;
}
} // namespace

void camcorder_warp_heap(ID3D12Device *device, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE gpu,
                         DXGI_FORMAT format, int transfer) {
    auto &w = warp();
    w.device = device;
    w.cpu = cpu;
    w.gpu = gpu;
    if (format != w.output) { // a new swap chain format needs new pipelines
        w.to_picture.Reset();
        w.to_screen.Reset();
        w.root.Reset();
        w.pass_tried = w.pass_ready = false;
    }
    w.output = format;
    w.transfer = transfer;
}

bool camcorder_picture_pass_ready() {
    auto &w = warp();
    if (w.pass_tried || !w.device || w.output == DXGI_FORMAT_UNKNOWN) return w.pass_ready;
    // The shader is compiled once a game, on a thread of its own (a big shader takes the compiler a
    // moment); a new swap chain needs only new pipelines.
    if (!w.vertex_code && !w.compile_failed) {
        if (!w.compile_started) {
            w.compile_started = true;
            w.compiling = std::async(std::launch::async, [] { return std::pair{compile("picture_vs", "vs_5_0"), compile("picture_ps", "ps_5_0")}; });
            return false;
        }
        if (w.compiling.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
        auto [vertex, pixel] = w.compiling.get();
        if (vertex && pixel) {
            w.vertex_code = std::move(vertex);
            w.pixel_code = std::move(pixel);
        } else {
            w.compile_failed = true;
        }
    }
    w.pass_tried = true;
    w.pass_ready = !w.compile_failed && build_pass(w);
    logging::write(w.pass_ready ? logging::Level::info : logging::Level::warning, logging::Channel::graphics,
                   w.pass_ready ? "Camcorder: picture shader ready (the look shows on screen)."
                                : "Camcorder: no picture shader; the fisheye is drawn as a mesh and the look is left to clips.");
    if (!w.pass_ready && !std::exchange(w.pass_failure_said, true))
        notify(NoticeLevel::warning, "The camcorder's look can't be shown on screen here.",
               "Clips and stills still get it; the camera light needs it. logs\\ReSkate.log says why.");
    return w.pass_ready;
}

bool camcorder_picture_pass_failed() {
    const auto &w = warp();
    return w.compile_failed || (w.pass_tried && !w.pass_ready);
}

int camcorder_warp_transfer() { return warp().transfer; }

void camcorder_picture_chain(PictureChain chain) {
    auto &w = warp();
    if (!w.pass_ready || !w.texture || chain.screen[0] <= 0 || chain.screen[1] <= 0) return;
    w.chain = std::move(chain);
    w.chain_wanted = true;
}

std::optional<ImTextureID> camcorder_warp_request(bool copy) {
    auto &w = warp();
    if (!w.device || !w.gpu.ptr) {
        w.why_not = "the overlay gave the camcorder no place for its picture";
        return std::nullopt;
    }
    w.requested = true;
    // A held picture (a film camera's cadence) is held only once there is one; the copy is the back
    // buffer's own size, whatever size the overlay lays out in.
    w.copy_wanted = copy || !w.texture;
    if (!w.texture) {
        if (w.why_not.empty()) w.why_not = "no copy of the game's picture yet";
        return std::nullopt;
    }
    w.why_not.clear();
    w.drawn = true;
    return static_cast<ImTextureID>(w.gpu.ptr);
}

std::string camcorder_warp_why_not() { return warp().why_not; }

void camcorder_warp_copy(ID3D12GraphicsCommandList *commands, ID3D12Resource *back_buffer) {
    auto &w = warp();
    const bool requested = std::exchange(w.requested, false), copy = std::exchange(w.copy_wanted, false);
    const bool chain = std::exchange(w.chain_wanted, false);
    if (!requested || !commands || !back_buffer || !w.device) return;
    free_retired(w);
    const auto back = back_buffer->GetDesc();
    if (back.SampleDesc.Count != 1) {
        w.why_not = "the game's picture is multisampled";
        camcorder_runtime::note_warp_failed();
        return;
    }
    if (copy) {
        bool fresh = false;
        if (!w.texture || w.width != back.Width || w.height != back.Height || w.format != back.Format) {
            // The old texture (and its view) may still be in use by frames on the GPU.
            if (!idle(w)) return;
            w.texture.Reset();
            if (!make_texture(w, back)) {
                w.why_not = "no texture could be made for the copy";
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
    // The copy is the back buffer's size; one of another size (it changed this frame) waits a frame.
    if (chain && w.pass_ready && w.texture && w.width == back.Width && w.height == back.Height) draw_chain(w, commands, back_buffer);
}

void camcorder_warp_submitted(ID3D12Fence *fence, UINT64 signal) {
    auto &w = warp();
    w.last_signal = signal;
    if (w.copied || w.drawn) {
        w.fence = fence;
        w.last_use = signal;
        if (w.drawn && !std::exchange(w.working, true))
            logging::write(logging::Level::info, logging::Channel::graphics, "Camcorder: picture drawing.");
    }
    w.copied = w.drawn = false;
}

void camcorder_warp_release() {
    auto &w = warp();
    for (int waited = 0; waited < 500 && !idle(w); ++waited) Sleep(1);
    w.texture.Reset();
    for (auto &t : w.target) t.Reset();
    for (auto &t : w.table) t = {};
    w.retired.clear();
    w.target_width = 0;
    w.target_height = 0;
    w.history_valid = false;
    w.views.Reset();
    w.outputs.Reset();
    w.next_view = first_ring_view;
    w.fence.Reset();
    w.to_picture.Reset();
    w.to_screen.Reset();
    w.root.Reset();
    w.pass_tried = w.pass_ready = false;
    w.width = 0;
    w.height = 0;
    w.format = DXGI_FORMAT_UNKNOWN;
    w.requested = w.copy_wanted = w.drawn = w.copied = w.chain_wanted = false;
    w.device = nullptr;
    w.cpu = {};
    w.gpu = {};
}
} // namespace dingosdk::overlay
