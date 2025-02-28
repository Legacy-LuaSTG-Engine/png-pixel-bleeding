// Dear ImGui: standalone example application for DirectX 11

// Learn about Dear ImGui:
// - FAQ                  https://dearimgui.com/faq
// - Getting Started      https://dearimgui.com/getting-started
// - Documentation        https://dearimgui.com/docs (same as your local docs/ folder).
// - Introduction, links and more at the top of imgui.cpp

#include <stdexcept>
#include <string>

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
#include <dxgi1_6.h>
#include <d3d11_4.h>
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

        return initGuiFont();
    }

    bool initGuiFont() {
        ImGuiIO& io = ImGui::GetIO();
        io.Fonts->Clear();

        m_font_glyph_ranges_builder.AddRanges(io.Fonts->GetGlyphRangesDefault());

        m_font_glyph_ranges_builder.AddText("文件");
        m_font_glyph_ranges_builder.AddText("打开");
        m_font_glyph_ranges_builder.AddText("关闭");
        m_font_glyph_ranges_builder.AddText("保存");
        m_font_glyph_ranges_builder.AddText("另存为");

        m_font_glyph_ranges_builder.AddText("帮助");
        m_font_glyph_ranges_builder.AddText("演示（Dear ImGui）");

        m_font_glyph_ranges_builder.AddText("打开的文件：");
        auto const open_file_path = winrt::to_string(m_open_file_path);
        m_font_glyph_ranges_builder.AddText(open_file_path.c_str());

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
                m_open_file_path.assign(path);
                m_font_glyph_cache_dirty = true;
                CoTaskMemFree(path);
            }
        }
    }

    void closeFileCommand() {
        m_opened = false;
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

    void layoutImageView() {
        ImGuiWindowFlags flags{};
        ImGuiWindowFlags_Modal
        if (ImGui::Begin("Image View", nullptr, flags)) {

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
    bool m_opened{false};
    std::wstring m_open_file_path;
    bool m_font_glyph_cache_dirty{false};

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
