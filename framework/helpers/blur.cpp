#include "../headers/blur.h"
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <cmath>

struct d3d11_state_backup
{
    ID3D11RasterizerState* rs = nullptr;
    ID3D11BlendState* blend_state = nullptr;
    FLOAT blend_factor[4] = { 0 };
    UINT sample_mask = 0;
    ID3D11DepthStencilState* depth_stencil_state = nullptr;
    UINT stencil_ref = 0;
    ID3D11ShaderResourceView* ps_shader_resource = nullptr;
    ID3D11SamplerState* ps_sampler = nullptr;
    ID3D11PixelShader* ps = nullptr;
    ID3D11VertexShader* vs = nullptr;
    UINT ps_instances_count = 0, vs_instances_count = 0;
    ID3D11ClassInstance* ps_instances[256] = { nullptr }, * vs_instances[256] = { nullptr };
    D3D11_PRIMITIVE_TOPOLOGY primitive_topology = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
    ID3D11Buffer* index_buffer = nullptr, * vertex_buffer = nullptr;
    UINT index_buffer_offset = 0, vertex_buffer_stride = 0, vertex_buffer_offset = 0;
    DXGI_FORMAT index_buffer_format = DXGI_FORMAT_UNKNOWN;
    ID3D11InputLayout* input_layout = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    D3D11_VIEWPORT viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = { {0} };
    UINT viewports_count = 0;

    ~d3d11_state_backup()
    {
        if (rs) rs->Release();
        if (blend_state) blend_state->Release();
        if (depth_stencil_state) depth_stencil_state->Release();
        if (ps_shader_resource) ps_shader_resource->Release();
        if (ps_sampler) ps_sampler->Release();
        if (ps) ps->Release();
        if (vs) vs->Release();
        for (UINT i = 0; i < ps_instances_count; i++) if (ps_instances[i]) ps_instances[i]->Release();
        for (UINT i = 0; i < vs_instances_count; i++) if (vs_instances[i]) vs_instances[i]->Release();
        if (index_buffer) index_buffer->Release();
        if (vertex_buffer) vertex_buffer->Release();
        if (input_layout) input_layout->Release();
        if (rtv) rtv->Release();
        if (dsv) dsv->Release();
    }
};

struct blur_callback_data
{
    ID3D11Device* device;
    ID3D11DeviceContext* ctx;
    d3d11_state_backup* state;
    int pass_count = 0;
};

static ID3D11VertexShader* g_vertex_shader = nullptr;
static ID3D11PixelShader* g_pixel_shader_blur = nullptr;
static ID3D11Buffer* g_vertex_buffer = nullptr;
static ID3D11Buffer* g_constant_buffer = nullptr;
static ID3D11SamplerState* g_sampler_state = nullptr;
static ID3D11InputLayout* g_input_layout = nullptr;
static ID3D11Texture2D* g_blur_texture1 = nullptr;
static ID3D11Texture2D* g_blur_texture2 = nullptr;
static ID3D11RenderTargetView* g_blur_rtv1 = nullptr;
static ID3D11RenderTargetView* g_blur_rtv2 = nullptr;
static ID3D11ShaderResourceView* g_blur_srv1 = nullptr;
static ID3D11ShaderResourceView* g_blur_srv2 = nullptr;
static int g_backbuffer_width = 0;
static int g_backbuffer_height = 0;
static DXGI_FORMAT g_backbuffer_format = DXGI_FORMAT_UNKNOWN;

static const float g_gaussian_weights[15] =
{
    0.001f, 0.005f, 0.016f, 0.038f, 0.071f,
    0.109f, 0.135f, 0.147f,
    0.135f, 0.109f, 0.071f, 0.038f, 0.016f, 0.005f, 0.001f
};

static void save_state(ID3D11DeviceContext* ctx, d3d11_state_backup* backup)
{
    ctx->IAGetPrimitiveTopology(&backup->primitive_topology);
    ctx->IAGetVertexBuffers(0, 1, &backup->vertex_buffer, &backup->vertex_buffer_stride, &backup->vertex_buffer_offset);
    ctx->IAGetIndexBuffer(&backup->index_buffer, &backup->index_buffer_format, &backup->index_buffer_offset);
    ctx->IAGetInputLayout(&backup->input_layout);

    backup->vs_instances_count = 256;
    ctx->VSGetShader(&backup->vs, backup->vs_instances, &backup->vs_instances_count);

    backup->ps_instances_count = 256;
    ctx->PSGetShader(&backup->ps, backup->ps_instances, &backup->ps_instances_count);
    ctx->PSGetShaderResources(0, 1, &backup->ps_shader_resource);
    ctx->PSGetSamplers(0, 1, &backup->ps_sampler);

    ctx->OMGetBlendState(&backup->blend_state, backup->blend_factor, &backup->sample_mask);
    ctx->OMGetDepthStencilState(&backup->depth_stencil_state, &backup->stencil_ref);
    ctx->RSGetState(&backup->rs);

    backup->viewports_count = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    ctx->RSGetViewports(&backup->viewports_count, backup->viewports);

    ctx->OMGetRenderTargets(1, &backup->rtv, &backup->dsv);
}

static void restore_state(ID3D11DeviceContext* ctx, d3d11_state_backup* backup)
{
    ctx->IASetPrimitiveTopology(backup->primitive_topology);
    ctx->IASetVertexBuffers(0, 1, &backup->vertex_buffer, &backup->vertex_buffer_stride, &backup->vertex_buffer_offset);
    ctx->IASetIndexBuffer(backup->index_buffer, backup->index_buffer_format, backup->index_buffer_offset);
    ctx->IASetInputLayout(backup->input_layout);

    ctx->VSSetShader(backup->vs, backup->vs_instances, backup->vs_instances_count);
    ctx->PSSetShader(backup->ps, backup->ps_instances, backup->ps_instances_count);
    ctx->PSSetShaderResources(0, 1, &backup->ps_shader_resource);
    ctx->PSSetSamplers(0, 1, &backup->ps_sampler);

    ctx->OMSetBlendState(backup->blend_state, backup->blend_factor, backup->sample_mask);
    ctx->OMSetDepthStencilState(backup->depth_stencil_state, backup->stencil_ref);
    ctx->RSSetState(backup->rs);
    ctx->RSSetViewports(backup->viewports_count, backup->viewports);
    ctx->OMSetRenderTargets(1, &backup->rtv, backup->dsv);
}

static bool ensure_resources(ID3D11Device* device)
{
    if (g_vertex_shader && g_pixel_shader_blur && g_constant_buffer && g_input_layout)
        return true;

    if (g_input_layout)
    {
        g_input_layout->Release();
        g_input_layout = nullptr;
    }

    if (g_pixel_shader_blur)
    {
        g_pixel_shader_blur->Release();
        g_pixel_shader_blur = nullptr;
    }

    if (g_constant_buffer)
    {
        g_constant_buffer->Release();
        g_constant_buffer = nullptr;
    }

    const char* vs_code =
        "struct VS_INPUT { float2 pos : POSITION; float2 uv : TEXCOORD; };"
        "struct PS_INPUT { float4 pos : SV_POSITION; float2 uv : TEXCOORD; };"
        "PS_INPUT main(VS_INPUT input) {"
        "    PS_INPUT output;"
        "    output.pos = float4(input.pos, 0.0, 1.0);"
        "    output.uv = input.uv;"
        "    return output;"
        "}";

    ID3DBlob* vs_blob = nullptr;
    HRESULT hr = D3DCompile(vs_code, strlen(vs_code), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vs_blob, nullptr);
    if (FAILED(hr))
        return false;

    hr = device->CreateVertexShader(vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), nullptr, &g_vertex_shader);
    if (FAILED(hr))
    {
        vs_blob->Release();
        return false;
    }

    D3D11_INPUT_ELEMENT_DESC layout_desc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    hr = device->CreateInputLayout(layout_desc, 2, vs_blob->GetBufferPointer(), vs_blob->GetBufferSize(), &g_input_layout);
    vs_blob->Release();
    if (FAILED(hr))
        return false;

    float vertices[] =
    {
        -1.0f,  1.0f, 0.0f, 0.0f,
         1.0f,  1.0f, 1.0f, 0.0f,
        -1.0f, -1.0f, 0.0f, 1.0f,
         1.0f, -1.0f, 1.0f, 1.0f
    };

    D3D11_BUFFER_DESC bd = {};
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.ByteWidth = sizeof(vertices);
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.StructureByteStride = sizeof(float) * 4;

    D3D11_SUBRESOURCE_DATA sd = {};
    sd.pSysMem = vertices;
    hr = device->CreateBuffer(&bd, &sd, &g_vertex_buffer);
    if (FAILED(hr))
        return false;

    static const char* ps_code_blur = R"(
    Texture2D tex : register(t0);
    SamplerState samp : register(s0);
    cbuffer BlurParams : register(b0) { float2 texelSize; float2 direction; };
    static const float weights[31] = { 0.018216, 0.019671, 0.021166, 0.022692, 0.024237, 0.025785, 0.027318, 0.028816, 0.030259, 0.031626, 0.032899, 0.034059, 0.035089, 0.035973, 0.036696, 0.037245, 0.037609, 0.037778, 0.037746, 0.037509, 0.037067, 0.036420, 0.035573, 0.034529, 0.033295, 0.031880, 0.030293, 0.028545, 0.026649, 0.024617, 0.022462 };
    float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_Target { float4 color = float4(0.0, 0.0, 0.0, 0.0);
    float2 scaledDirection = direction * 3.0; 
    [unroll] for (int i = -15; i <= 15; ++i) { float w = weights[i + 15];
    float2 off = scaledDirection * (float(i) * texelSize); color += tex.Sample(samp, uv + off) * w; } color.a = 1.0; return color; }
)";

    ID3DBlob* ps_blob_blur = nullptr;
    ID3DBlob* error_blob = nullptr;
    hr = D3DCompile(ps_code_blur, strlen(ps_code_blur), nullptr, nullptr, nullptr, "main", "ps_5_0",
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0, &ps_blob_blur, &error_blob);

    if (FAILED(hr))
    {
        if (error_blob) error_blob->Release();
        return false;
    }

    hr = device->CreatePixelShader(ps_blob_blur->GetBufferPointer(), ps_blob_blur->GetBufferSize(), nullptr, &g_pixel_shader_blur);
    ps_blob_blur->Release();
    if (FAILED(hr))
        return false;

    bd = {};
    bd.ByteWidth = sizeof(float) * 4;
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    hr = device->CreateBuffer(&bd, nullptr, &g_constant_buffer);
    if (FAILED(hr))
        return false;

    D3D11_SAMPLER_DESC samp_desc = {};
    samp_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    samp_desc.MinLOD = 0;
    samp_desc.MaxLOD = D3D11_FLOAT32_MAX;
    hr = device->CreateSamplerState(&samp_desc, &g_sampler_state);
    if (FAILED(hr))
        return false;

    return true;
}

static bool ensure_textures(ID3D11Device* device, int width, int height, DXGI_FORMAT format)
{
    if (width == g_backbuffer_width && height == g_backbuffer_height && format == g_backbuffer_format)
        return true;

    if (g_blur_texture1)
    {
        g_blur_texture1->Release();
        g_blur_texture1 = nullptr;
    }

    if (g_blur_texture2)
    {
        g_blur_texture2->Release();
        g_blur_texture2 = nullptr;
    }

    if (g_blur_rtv1)
    {
        g_blur_rtv1->Release();
        g_blur_rtv1 = nullptr;
    }

    if (g_blur_rtv2)
    {
        g_blur_rtv2->Release();
        g_blur_rtv2 = nullptr;
    }

    if (g_blur_srv1)
    {
        g_blur_srv1->Release();
        g_blur_srv1 = nullptr;
    }

    if (g_blur_srv2)
    {
        g_blur_srv2->Release();
        g_blur_srv2 = nullptr;
    }

    g_backbuffer_width = width;
    g_backbuffer_height = height;
    g_backbuffer_format = format;

    D3D11_TEXTURE2D_DESC tex_desc = {};
    tex_desc.Width = width;
    tex_desc.Height = height;
    tex_desc.MipLevels = 1;
    tex_desc.ArraySize = 1;
    tex_desc.Format = format;
    tex_desc.SampleDesc.Count = 1;
    tex_desc.Usage = D3D11_USAGE_DEFAULT;
    tex_desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = device->CreateTexture2D(&tex_desc, nullptr, &g_blur_texture1);
    if (FAILED(hr))
        return false;

    hr = device->CreateTexture2D(&tex_desc, nullptr, &g_blur_texture2);
    if (FAILED(hr))
        return false;

    hr = device->CreateRenderTargetView(g_blur_texture1, nullptr, &g_blur_rtv1);
    if (FAILED(hr))
        return false;

    hr = device->CreateRenderTargetView(g_blur_texture2, nullptr, &g_blur_rtv2);
    if (FAILED(hr))
        return false;

    hr = device->CreateShaderResourceView(g_blur_texture1, nullptr, &g_blur_srv1);
    if (FAILED(hr))
        return false;

    hr = device->CreateShaderResourceView(g_blur_texture2, nullptr, &g_blur_srv2);
    if (FAILED(hr))
        return false;

    return true;
}

static void begin_blur(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    blur_callback_data* data = (blur_callback_data*)cmd->UserCallbackData;
    ID3D11Device* device = data->device;
    ID3D11DeviceContext* ctx = data->ctx;

    data->state = new d3d11_state_backup();
    save_state(ctx, data->state);
    data->pass_count = 0;

    if (!ensure_resources(device))
        return;

    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->OMGetRenderTargets(1, &rtv, &dsv);

    if (rtv)
    {
        ID3D11Resource* backbuffer_resource = nullptr;
        rtv->GetResource(&backbuffer_resource);

        if (backbuffer_resource)
        {
            ID3D11Texture2D* backbuffer_texture = nullptr;
            HRESULT hr = backbuffer_resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&backbuffer_texture);

            if (SUCCEEDED(hr) && backbuffer_texture)
            {
                D3D11_TEXTURE2D_DESC desc;
                backbuffer_texture->GetDesc(&desc);

                if (ensure_textures(device, desc.Width, desc.Height, desc.Format))
                    ctx->CopyResource(g_blur_texture1, backbuffer_resource);

                backbuffer_texture->Release();
            }
            backbuffer_resource->Release();
        }
        rtv->Release();
    }

    if (dsv) dsv->Release();

    D3D11_VIEWPORT vp = {};
    vp.Width = (float)g_backbuffer_width;
    vp.Height = (float)g_backbuffer_height;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    ctx->RSSetViewports(1, &vp);

    ctx->VSSetShader(g_vertex_shader, nullptr, 0);
    ctx->IASetInputLayout(g_input_layout);

    UINT stride = sizeof(float) * 4;
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &g_vertex_buffer, &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    ctx->PSSetSamplers(0, 1, &g_sampler_state);
}

static void blur_pass(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    blur_callback_data* data = (blur_callback_data*)cmd->UserCallbackData;
    ID3D11DeviceContext* ctx = data->ctx;

    data->pass_count++;

    if (!g_pixel_shader_blur || !g_blur_srv1 || !g_blur_rtv2)
        return;

    ctx->PSSetShader(g_pixel_shader_blur, nullptr, 0);

    float direction_x = (data->pass_count % 2 == 1) ? 1.0f : 0.0f;
    float direction_y = (data->pass_count % 2 == 1) ? 0.0f : 1.0f;

    float constants[4] = { 1.0f / static_cast<float>(g_backbuffer_width), 1.0f / static_cast<float>(g_backbuffer_height), direction_x, direction_y };
    ctx->UpdateSubresource(g_constant_buffer, 0, nullptr, constants, 0, 0);
    ctx->PSSetConstantBuffers(0, 1, &g_constant_buffer);

    ctx->PSSetShaderResources(0, 1, &g_blur_srv1);
    ctx->OMSetRenderTargets(1, &g_blur_rtv2, nullptr);

    ctx->Draw(4, 0);

    ctx->CopyResource(g_blur_texture1, g_blur_texture2);
}

static void end_blur(const ImDrawList* parent_list, const ImDrawCmd* cmd)
{
    blur_callback_data* data = (blur_callback_data*)cmd->UserCallbackData;
    ID3D11DeviceContext* ctx = data->ctx;

    if (data->state)
    {
        restore_state(ctx, data->state);
        delete data->state;
        data->state = nullptr;
    }

    delete data;
}

void draw_background_blur(ImDrawList* draw_list, ID3D11Device* device, ID3D11DeviceContext* ctx, float rounding)
{
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    if (window->Hidden || window->SkipItems || ImGui::GetStyle().Alpha < 0.01f)
        return;

    blur_callback_data* data = new blur_callback_data{ device, ctx, nullptr, 0 };

    draw_list->AddCallback(begin_blur, data);

    for (int i = 0; i < 2; i++)
        draw_list->AddCallback(blur_pass, data);

    draw_list->AddCallback(end_blur, data);

    if (g_blur_srv1)
    {
        const float alpha = ImGui::GetStyle().Alpha;
        const ImU32 color = IM_COL32(255, 255, 255, static_cast<int>(255 * alpha));

        ImVec2 window_pos = ImGui::GetWindowPos();
        ImVec2 window_size = ImGui::GetWindowSize();
        ImVec2 window_end = ImVec2(window_pos.x + window_size.x, window_pos.y + window_size.y);

        draw_list->AddImageRounded((ImTextureID)g_blur_srv1, window_pos, window_end, { ImGui::GetCurrentWindow()->Pos.x / ImGui::GetIO().DisplaySize.x, ImGui::GetCurrentWindow()->Pos.y / ImGui::GetIO().DisplaySize.y }, ImGui::GetCurrentWindow()->Pos / ImGui::GetIO().DisplaySize + ImGui::GetCurrentWindow()->Size / ImGui::GetIO().DisplaySize, color, rounding, ImDrawFlags_RoundCornersAll);
    }
}

void release_blur_resources()
{
    if (g_vertex_shader)
    {
        g_vertex_shader->Release();
        g_vertex_shader = nullptr;
    }

    if (g_pixel_shader_blur)
    {
        g_pixel_shader_blur->Release();
        g_pixel_shader_blur = nullptr;
    }

    if (g_vertex_buffer)
    {
        g_vertex_buffer->Release();
        g_vertex_buffer = nullptr;
    }

    if (g_constant_buffer)
    {
        g_constant_buffer->Release();
        g_constant_buffer = nullptr;
    }

    if (g_sampler_state)
    {
        g_sampler_state->Release();
        g_sampler_state = nullptr;
    }

    if (g_input_layout)
    {
        g_input_layout->Release();
        g_input_layout = nullptr;
    }

    if (g_blur_texture1)
    {
        g_blur_texture1->Release();
        g_blur_texture1 = nullptr;
    }

    if (g_blur_texture2)
    {
        g_blur_texture2->Release();
        g_blur_texture2 = nullptr;
    }

    if (g_blur_rtv1)
    {
        g_blur_rtv1->Release();
        g_blur_rtv1 = nullptr;
    }

    if (g_blur_rtv2)
    {
        g_blur_rtv2->Release();
        g_blur_rtv2 = nullptr;
    }

    if (g_blur_srv1)
    {
        g_blur_srv1->Release();
        g_blur_srv1 = nullptr;
    }

    if (g_blur_srv2)
    {
        g_blur_srv2->Release();
        g_blur_srv2 = nullptr;
    }

    g_backbuffer_width = 0;
    g_backbuffer_height = 0;
    g_backbuffer_format = DXGI_FORMAT_UNKNOWN;
}