// Dear ImGui: standalone example application for DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include <stdexcept>
#include <string>
#include <ranges>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NOSERVICE
#define NOMCX
#define NOIME
#include <windows.h>
#include <shobjidl_core.h>
#include <winrt/base.h>
#include <wincodec.h>
#include <dxgi1_6.h>
#include <d3d11_4.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <wil/resource.h>
#include <wil/com.h>
#include <wil/result_macros.h>

// Data
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool g_SwapChainOccluded = false;
static UINT g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class BooleanMap2D {
public:
    [[nodiscard]] bool get(uint32_t const x, uint32_t const y) const {
        return m_pixels.at(y * m_width + x);
    }

    void set(uint32_t const x, uint32_t const y) {
        m_pixels.at(y * m_width + x) = true;
    }

    void resize(uint32_t const width, uint32_t const height) {
        m_width = width;
        m_height = height;
        m_pixels.resize(width * height);
    }

private:
    std::vector<bool> m_pixels;
    uint32_t m_width{};
    uint32_t m_height{};
};

class Image2D {
public:
    [[nodiscard]] uint32_t width() const noexcept {
        return m_width;
    }

    [[nodiscard]] uint32_t pitch() const noexcept {
        return static_cast<uint32_t>(m_width * sizeof(DirectX::PackedVector::XMCOLOR));
    }

    [[nodiscard]] uint32_t height() const noexcept {
        return m_height;
    }

    [[nodiscard]] uint32_t size() const noexcept {
        return static_cast<uint32_t>(m_pixels.size() * sizeof(DirectX::PackedVector::XMCOLOR));
    }

    template <typename T>
    [[nodiscard]] T* buffer() noexcept {
        return reinterpret_cast<T*>(m_pixels.data());
    }

    void clear() {
        m_width = 0;
        m_height = 0;
        m_pixels.clear();
        m_pixels.shrink_to_fit();
    }

    void resize(uint32_t const width, uint32_t const height) {
        m_width = width;
        m_height = height;
        m_pixels.resize(width * height);
    }

    void fill(DirectX::PackedVector::XMCOLOR const color = {}) {
        std::ranges::fill(m_pixels, color);
    }

    [[nodiscard]] DirectX::PackedVector::XMCOLOR const& pixel(uint32_t const x, uint32_t const y) const {
        return m_pixels.at(y * m_width + x);
    }

    [[nodiscard]] DirectX::PackedVector::XMCOLOR& pixel(uint32_t const x, uint32_t const y) {
        return m_pixels.at(y * m_width + x);
    }

    [[nodiscard]] bool findNotTransparentNeighbors(
        BooleanMap2D const& processed, uint32_t const x, uint32_t const y,
        uint32_t& count, DirectX::PackedVector::XMCOLOR results[8]
    ) const noexcept {
        count = 0;
        for (int32_t yy = -1; yy <= 1; ++yy) {
            for (int32_t xx = -1; xx <= 1; ++xx) {
                if (xx == 0 && yy == 0) {
                    continue; // exclude center (self)
                }
                if (xx == -1 && x == 0) {
                    continue; // out of bounds
                }
                if (xx == 1 && x == width() - 1) {
                    continue; // out of bounds
                }
                if (yy == -1 && y == 0) {
                    continue; // out of bounds
                }
                if (yy == 1 && y == height() - 1) {
                    continue; // out of bounds
                }
                auto const px = pixel(x + xx, y + yy);
                if (!processed.get(x + xx, y + yy) && px.a == 0) {
                    continue; // ignore transparent pixel or not processed pixel
                }
                results[count] = px;
                ++count;
            }
        }
        return count > 0;
    }

    void doPixelBleeding() {
        BooleanMap2D processed;
        processed.resize(width(), height());
        size_t miss_count{};
        uint32_t count{};
        DirectX::PackedVector::XMCOLOR results[8]{};
        do {
            miss_count = 0;
            Image2D cache = *this;
            for (uint32_t y = 0; y < height(); ++y) {
                for (uint32_t x = 0; x < width(); ++x) {
                    if (processed.get(x, y)) {
                        continue;
                    }
                    auto& color = pixel(x, y);
                    if (color.a > 0) {
                        processed.set(x, y);
                        continue;
                    }
                    if (!cache.findNotTransparentNeighbors(processed, x, y, count, results)) {
                        ++miss_count;
                        continue;
                    }
                    color = results[0];
                    color.a = 0;
                    processed.set(x, y);
                }
            }
        }
        while (miss_count > 0);
    }

private:
    std::vector<DirectX::PackedVector::XMCOLOR> m_pixels;
    uint32_t m_width{};
    uint32_t m_height{};
};

class Application {
public:
    bool initGui(HWND const hwnd) {
        m_win32_window = hwnd;

        if (!IMGUI_CHECKVERSION()) {
            return false;
        }
        if (!ImGui::CreateContext()) {
            return false;
        }
        m_gui_initialized = true;

        ImGuiIO& io = ImGui::GetIO();
        (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad; // Enable Gamepad Controls

        if (!ImGui_ImplWin32_Init(hwnd)) {
            return false;
        }
        m_gui_backend_win32_initialized = true;

        if (!ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext)) {
            return false;
        }
        m_gui_backend_d3d11_initialized = true;

        D3D11_SAMPLER_DESC sampler_state_info{};
        sampler_state_info.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        sampler_state_info.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_state_info.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_state_info.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sampler_state_info.MipLODBias = 0;
        sampler_state_info.MaxAnisotropy = 1;
        sampler_state_info.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sampler_state_info.MinLOD = -D3D11_FLOAT32_MAX;
        sampler_state_info.MaxLOD = D3D11_FLOAT32_MAX;
        THROW_IF_FAILED(g_pd3dDevice->CreateSamplerState(&sampler_state_info, m_sampler_state_point.put()));

        D3D11_RENDER_TARGET_BLEND_DESC blend_info{};
        blend_info.BlendEnable = TRUE;
        blend_info.SrcBlend = D3D11_BLEND_ONE;
        blend_info.DestBlend = D3D11_BLEND_ZERO;
        blend_info.BlendOp = D3D11_BLEND_OP_ADD;
        blend_info.SrcBlendAlpha = D3D11_BLEND_ONE;
        blend_info.DestBlendAlpha = D3D11_BLEND_ONE;
        blend_info.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        blend_info.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        D3D11_BLEND_DESC blend_state_info{};
        for (auto& render_target : blend_state_info.RenderTarget) {
            render_target = blend_info;
        }
        THROW_IF_FAILED(g_pd3dDevice->CreateBlendState(&blend_state_info, m_blend_state_one.put()));

        return initGuiFont();
    }

    bool initGuiFont() {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        m_font_glyph_ranges_builder.Clear();
        m_font_glyph_ranges_builder.AddRanges(io.Fonts->GetGlyphRangesDefault());

        m_font_glyph_ranges_builder.AddText("文件");
        m_font_glyph_ranges_builder.AddText("打开");
        m_font_glyph_ranges_builder.AddText("关闭");
        m_font_glyph_ranges_builder.AddText("保存");
        m_font_glyph_ranges_builder.AddText("另存为");

        m_font_glyph_ranges_builder.AddText("帮助");
        m_font_glyph_ranges_builder.AddText("演示（Dear ImGui）");

        m_font_glyph_ranges_builder.AddText("工作区");
        m_font_glyph_ranges_builder.AddText("打开的文件：");
        m_font_glyph_ranges_builder.AddText(m_open_file_path.c_str());
        m_font_glyph_ranges_builder.AddText("图像尺寸：");
        m_font_glyph_ranges_builder.AddText("预览透明度通道");
        m_font_glyph_ranges_builder.AddText("临近采样缩放");
        m_font_glyph_ranges_builder.AddText("预览缩放");
        m_font_glyph_ranges_builder.AddText("处理透明像素");

        m_font_glyph_ranges.clear();
        m_font_glyph_ranges_builder.BuildRanges(&m_font_glyph_ranges);

        auto const scaling = ImGui_ImplWin32_GetDpiScaleForHwnd(m_win32_window);
        auto const font_size = 16.0f * scaling;
        if (!io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\msyh.ttc)", font_size, nullptr,
                                          m_font_glyph_ranges.Data)) {
            if (!io.Fonts->AddFontFromFileTTF(R"(C:\Windows\Fonts\msyh.ttf)", font_size, nullptr,
                                              m_font_glyph_ranges.Data)) {
                return false;
            }
        }

        ImGuiStyle style;
        ImGui::StyleColorsDark(&style);
        style.ScaleAllSizes(scaling);
        ImGui::GetStyle() = style;

        return true;
    }

    void reloadGuiFont() {
        ImGui_ImplDX11_Shutdown();
        initGuiFont();
        ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);
    }

    void destroyGui() {
        m_sampler_state_point.reset();
        m_blend_state_one.reset();
        closeFileCommand();
        if (m_gui_backend_d3d11_initialized) {
            ImGui_ImplDX11_Shutdown();
            m_gui_backend_d3d11_initialized = false;
        }
        if (m_gui_backend_win32_initialized) {
            ImGui_ImplWin32_Shutdown();
            m_gui_backend_win32_initialized = false;
        }
        if (m_gui_initialized) {
            ImGui::DestroyContext();
            m_gui_initialized = false;
        }
    }

    void openFileCommand() {
        auto const file_open_dialog = winrt::create_instance<IFileOpenDialog>(CLSID_FileOpenDialog);
        FILEOPENDIALOGOPTIONS options{};
        THROW_IF_FAILED(file_open_dialog->GetOptions(&options));
        options |= FOS_FORCEFILESYSTEM;
        THROW_IF_FAILED(file_open_dialog->SetOptions(options));
        constexpr COMDLG_FILTERSPEC file_types[]{
            COMDLG_FILTERSPEC{
                .pszName{L"PNG 文件"},
                .pszSpec{L"*.png"},
            }
        };
        THROW_IF_FAILED(file_open_dialog->SetFileTypes(
            std::size(file_types), file_types
        ));
        THROW_IF_FAILED(file_open_dialog->SetFileTypeIndex(1));
        THROW_IF_FAILED(file_open_dialog->SetDefaultExtension(file_types[0].pszSpec));
        if (SUCCEEDED(THROW_IF_FAILED(file_open_dialog->Show(nullptr)))) {
            wil::com_ptr<IShellItem> item;
            THROW_IF_FAILED(file_open_dialog->GetResult(item.put()));
            PWSTR path{};
            if (SUCCEEDED(THROW_IF_FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)))) {
                m_opened = true;
                m_open_file_path.assign(winrt::to_string(path));
                m_font_glyph_cache_dirty = true;
                loadImage();
                CoTaskMemFree(path);
            }
        }
    }

    void closeFileCommand() {
        m_opened = false;
        m_open_file_path.clear();
        unloadImage();
    }

    void createTextureResources(uint32_t const width, uint32_t const height) {
        m_image.resize(width, height);
        m_image.fill();

        D3D11_TEXTURE2D_DESC texture_info{};
        texture_info.Width = width;
        texture_info.Height = height;
        texture_info.MipLevels = 1;
        texture_info.ArraySize = 1;
        texture_info.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texture_info.SampleDesc.Count = 1;
        texture_info.Usage = D3D11_USAGE_DYNAMIC;
        texture_info.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        texture_info.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        THROW_IF_FAILED(g_pd3dDevice->CreateTexture2D(
            &texture_info, nullptr, m_opened_texture.put()
        ));

        D3D11_SHADER_RESOURCE_VIEW_DESC srv_info{};
        srv_info.Format = texture_info.Format;
        srv_info.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srv_info.Texture2D.MostDetailedMip = 0;
        srv_info.Texture2D.MipLevels = texture_info.MipLevels;
        THROW_IF_FAILED(g_pd3dDevice->CreateShaderResourceView(
            m_opened_texture.get(), &srv_info, m_opened_srv.put()
        ));
    }

    void uploadTextureData() {
        D3D11_MAPPED_SUBRESOURCE mapped{};
        THROW_IF_FAILED(g_pd3dDeviceContext->Map(
            m_opened_texture.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped
        ));
        auto source = m_image.buffer<uint8_t>();
        auto pointer = static_cast<uint8_t*>(mapped.pData);
        for (uint32_t y = 0; y < m_image.height(); ++y) {
            std::memcpy(pointer, source, m_image.pitch());
            source += m_image.pitch();
            pointer += mapped.RowPitch;
        }
        g_pd3dDeviceContext->Unmap(m_opened_texture.get(), 0);
    }

    void loadImage() {
        if (!m_wic_factory) {
            m_wic_factory = wil::CoCreateInstance<IWICImagingFactory>(CLSID_WICImagingFactory);
        }

        // create decoder

        wil::com_ptr<IWICBitmapDecoder> decoder;
        auto const file_path = winrt::to_hstring(m_open_file_path);
        THROW_IF_FAILED(m_wic_factory->CreateDecoderFromFilename(
            file_path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.put()
        ));

        // get first frame

        wil::com_ptr<IWICBitmapFrameDecode> decoder_frame;
        THROW_IF_FAILED(decoder->GetFrame(0, decoder_frame.put()));

        UINT color_context_count{};
        THROW_IF_FAILED(decoder_frame->GetColorContexts(
            0, nullptr, &color_context_count
        ));
        std::vector<IWICColorContext*> color_contexts(color_context_count);
        THROW_IF_FAILED(decoder_frame->GetColorContexts(
            color_context_count, color_contexts.data(), &color_context_count
        ));
        [[maybe_unused]] auto const auto_release_color_contexts = wil::scope_exit([&]() -> void {
            for (auto const color_context : color_contexts) {
                if (color_context) {
                    color_context->Release();
                }
            }
            color_contexts.clear();
        });
        for (auto const color_context : color_contexts) {
            WICColorContextType type{};
            THROW_IF_FAILED(color_context->GetType(&type));
            if (type == WICColorContextExifColorSpace) {
                UINT color_space{};
                THROW_IF_FAILED(color_context->GetExifColorSpace(&color_space));
            }
        }

        // convert

        auto const target_pixel_format{GUID_WICPixelFormat32bppBGRA};
        WICPixelFormatGUID pixel_format{};
        THROW_IF_FAILED(decoder_frame->GetPixelFormat(&pixel_format));

        auto decodeFrame = [&](IWICBitmapSource* bitmap) -> void {
            UINT width{};
            UINT height{};
            THROW_IF_FAILED(bitmap->GetSize(&width, &height));

            createTextureResources(width, height);

            THROW_IF_FAILED(bitmap->CopyPixels(
                nullptr, m_image.pitch(), m_image.size(), m_image.buffer<BYTE>()
            ));

            D3D11_MAPPED_SUBRESOURCE mapped{};
            THROW_IF_FAILED(g_pd3dDeviceContext->Map(
                m_opened_texture.get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped
            ));
            THROW_IF_FAILED(bitmap->CopyPixels(
                nullptr, mapped.RowPitch, mapped.RowPitch * height, static_cast<BYTE*>(mapped.pData)
            ));
            g_pd3dDeviceContext->Unmap(m_opened_texture.get(), 0);
        };

        if (pixel_format != target_pixel_format) {
            wil::com_ptr<IWICFormatConverter> format_converter;
            THROW_IF_FAILED(m_wic_factory->CreateFormatConverter(format_converter.put()));
            BOOL can_convert{FALSE};
            THROW_IF_FAILED(format_converter->CanConvert(pixel_format, target_pixel_format, &can_convert));
            if (can_convert) {
                THROW_IF_FAILED(format_converter->Initialize(
                    decoder_frame.get(), target_pixel_format,
                    WICBitmapDitherTypeNone, nullptr, 0.0f, WICBitmapPaletteTypeCustom
                ));
                decodeFrame(format_converter.get());
            }
        }
        else {
            decodeFrame(decoder_frame.get());
        }
    }

    void unloadImage() {
        m_image.clear();
        m_opened_texture.reset();
        m_opened_srv.reset();
    }

    void layoutMainMenu() {
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("文件")) {
                if (ImGui::MenuItem("打开")) {
                    openFileCommand();
                }
                if (ImGui::MenuItem("关闭", nullptr, false, m_opened)) {
                    closeFileCommand();
                }
                if (ImGui::MenuItem("保存", nullptr, false, m_opened)) {
                }
                if (ImGui::MenuItem("另存为", nullptr, false, m_opened)) {
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("帮助")) {
                ImGui::MenuItem("演示（Dear ImGui）", nullptr, &m_show_demo_window);
                ImGui::EndMenu();
            }
            ImGui::EndMainMenuBar();
        }
    }

    static void applyRenderState(ImDrawList const* const parent_list, ImDrawCmd const* const cmd) {
        auto const self = static_cast<Application*>(cmd->UserCallbackData);
        if (self->m_preview_point_scale) {
            ID3D11SamplerState* const sampler_state[1]{self->m_sampler_state_point.get()};
            g_pd3dDeviceContext->PSSetSamplers(0, 1, sampler_state);
        }
        if (!self->m_preview_alpha) {
            constexpr float blend_factor[4]{};
            g_pd3dDeviceContext->OMSetBlendState(
                self->m_blend_state_one.get(), blend_factor,D3D11_DEFAULT_SAMPLE_MASK);
        }
    }

    void layoutImageView() {
        if (ImGui::Begin("工作区")) {
            ImGui::Text("打开的文件：%s", m_open_file_path.c_str());
            if (m_opened_texture) {
                D3D11_TEXTURE2D_DESC texture_info{};
                m_opened_texture->GetDesc(&texture_info);
                ImGui::Text("图像尺寸：%u x %u", texture_info.Width, texture_info.Height);
                if (ImGui::Button("处理透明像素")) {
                    m_image.doPixelBleeding();
                    uploadTextureData();
                }
                ImGui::SameLine();
                ImGui::Checkbox("预览透明度通道", &m_preview_alpha);
                ImGui::SameLine();
                ImGui::Checkbox("临近采样缩放", &m_preview_point_scale);
                ImGui::SameLine();
                ImGui::SliderFloat("预览缩放", &m_preview_scale, 0.01f, 10.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
                ImGui::GetWindowDrawList()->AddCallback(&applyRenderState, this);
                ImGui::Image(
                    reinterpret_cast<ImTextureID>(m_opened_srv.get()),
                    ImVec2(texture_info.Width * m_preview_scale, texture_info.Height * m_preview_scale)
                );
                ImGui::GetWindowDrawList()->AddCallback(ImDrawCallback_ResetRenderState, nullptr);
            }
        }
        ImGui::End();
    }

    bool layoutGui() {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        layoutMainMenu();
        layoutImageView();
        if (m_show_demo_window) {
            ImGui::ShowDemoWindow(&m_show_demo_window);
        }
        ImGui::EndFrame();
        ImGui::Render();
        return true;
    }

    bool frame() {
        if (!m_gui_initialized) {
            return false;
        }

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            ::Sleep(10);
            return true;
        }
        g_SwapChainOccluded = false;

        if (g_ResizeWidth != 0 && g_ResizeHeight != 0) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        if (m_font_glyph_cache_dirty) {
            m_font_glyph_cache_dirty = false;
            reloadGuiFont();
        }

        if (!layoutGui()) {
            return false;
        }

        auto const clear_color = ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->
            ClearRenderTargetView(g_mainRenderTargetView, reinterpret_cast<float const*>(&clear_color));
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0); // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);

        return true;
    }

    static void run() {
        MSG msg{};
        for (;;) {
            auto const result = GetMessageW(&msg, nullptr, 0, 0);
            if (result == -1) {
                throw std::runtime_error("GetMessageW failed");
            }
            if (result == FALSE) {
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

private:
    HWND m_win32_window{};
    bool m_gui_initialized{false};
    bool m_gui_backend_win32_initialized{false};
    bool m_gui_backend_d3d11_initialized{false};
    ImVector<ImWchar> m_font_glyph_ranges;
    ImFontGlyphRangesBuilder m_font_glyph_ranges_builder;
    bool m_show_demo_window{false};

    wil::com_ptr<IWICImagingFactory> m_wic_factory;

    wil::com_ptr<ID3D11SamplerState> m_sampler_state_point;
    wil::com_ptr<ID3D11BlendState> m_blend_state_one;

    bool m_opened{false};
    std::string m_open_file_path;
    bool m_font_glyph_cache_dirty{false};
    Image2D m_image;
    wil::com_ptr<ID3D11Texture2D> m_opened_texture;
    wil::com_ptr<ID3D11ShaderResourceView> m_opened_srv;

    float m_preview_scale{1.0f};
    bool m_preview_point_scale{false};
    bool m_preview_alpha{false};

public:
    static Application& getInstance() {
        static Application instance;
        return instance;
    }
};

// Main code
int main(int, char**) {
    winrt::init_apartment();

    // Create application window
    //ImGui_ImplWin32_EnableDpiAwareness();
    WNDCLASSEXW wc = {
        sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        L"ImGui Example", nullptr
    };
    ::RegisterClassExW(&wc);
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Dear ImGui DirectX11 Example", WS_OVERLAPPEDWINDOW, 100, 100, 1280,
                                800, nullptr, nullptr, wc.hInstance, nullptr);

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    Application::getInstance().initGui(hwnd);

    // Main loop
    Application::run();

    // Cleanup
    Application::getInstance().destroyGui();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// Helper functions

bool CreateDeviceD3D(HWND hWnd) {
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0,};
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags,
                                                featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain,
                                                &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags,
                                            featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice,
                                            &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) {
        g_pSwapChain->Release();
        g_pSwapChain = nullptr;
    }
    if (g_pd3dDeviceContext) {
        g_pd3dDeviceContext->Release();
        g_pd3dDeviceContext = nullptr;
    }
    if (g_pd3dDevice) {
        g_pd3dDevice->Release();
        g_pd3dDevice = nullptr;
    }
}

void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget() {
    if (g_mainRenderTargetView) {
        g_mainRenderTargetView->Release();
        g_mainRenderTargetView = nullptr;
    }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    case WM_PAINT:
        Application::getInstance().frame();
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
