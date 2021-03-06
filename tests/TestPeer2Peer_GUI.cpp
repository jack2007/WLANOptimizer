#include "Connection.h"

#include <iostream> // cin
#include <unordered_map>

#include "../thirdparty/imgui.h"
#include "../thirdparty/imgui_impl_dx11.h"

#define HMONITOR_DECLARED
#include <d3d11.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <tchar.h>


static logger::Channel Logger("TestPeer2Peer", wopt::kMinLogLevel);


static std::mutex StatsLock;

static std::unordered_map<std::string, wopt::Statistics> StatsMap;

class GUIStatsReceiver : public wopt::IStatisticsReceiver
{
public:
    void OnStats(const wopt::Statistics& stats) override
    {
        Logger.Info("One-Way Delay Stats (msec): Min=", stats.Min, " Max=",
            stats.Max, " Avg=", stats.Average, " StdDev=", stats.StandardDeviation);

        Logger.Info("One-Way Delay Percentiles: 10%=", stats.Percentiles[1],
            " 20%=", stats.Percentiles[2],
            " 50%=", stats.Percentiles[5],
            " 80%=", stats.Percentiles[8],
            " 90%=", stats.Percentiles[9]);

        Logger.Info("Wire PLR=", stats.WirePLR, "%, Effective PLR=", stats.EffectivePLR, "%");

        std::lock_guard<std::mutex> locker(StatsLock);
        StatsMap[stats.Name] = stats;
    }
};

void DrawStats()
{
    std::lock_guard<std::mutex> locker(StatsLock);
    for (auto& pairs : StatsMap)
    {
        wopt::Statistics& stats = pairs.second;

        std::string title = pairs.first;

        ImGui::Begin(title.c_str());
        ImGui::Text("One-Way Delay Statistics for %s", title.c_str());
        ImGui::BulletText("Minimum OWD (msec) = %f", stats.Min);
        ImGui::BulletText("Average OWD (msec) = %f", stats.Average);
        ImGui::BulletText("Maximum OWD (msec) = %f", stats.Max);
        ImGui::BulletText("Standard deviation (msec) = %f", stats.StandardDeviation);
        ImGui::BulletText("Wire PLR = %f%% (simulated 3%% loss rate)", stats.WirePLR);
        ImGui::BulletText("Effective PLR = %f%% (with CCat FEC)", stats.EffectivePLR);
        ImGui::PlotHistogram(
            "10%-90% percentile latencies",
            stats.Percentiles,
            11,
            0,
            nullptr,
            stats.Min, stats.Max > 100.f ? stats.Max : 100.f, ImVec2(400, 400), 4);
        ImGui::End();
    }
}


//------------------------------------------------------------------------------
// Entrypoint

// Data
static ID3D11Device*            g_pd3dDevice = NULL;
static ID3D11DeviceContext*     g_pd3dDeviceContext = NULL;
static IDXGISwapChain*          g_pSwapChain = NULL;
static ID3D11RenderTargetView*  g_mainRenderTargetView = NULL;

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&pBackBuffer);
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, NULL, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = NULL; }
}

HRESULT CreateDeviceD3D(HWND hWnd)
{
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
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };

    HRESULT result = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);

    if (result != S_OK)
    {
        return result;
    }

    CreateRenderTarget();

    return S_OK;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = NULL; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = NULL; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = NULL; }
}

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != NULL && wParam != SIZE_MINIMIZED)
        {
            ImGui_ImplDX11_InvalidateDeviceObjects();
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
            ImGui_ImplDX11_CreateDeviceObjects();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

int main(int, char**)
{
    GUIStatsReceiver receiver;
    wopt::TestSocket sock(&receiver);

    bool initSuccess = false;

    if (!sock.Initialize(wopt::kServerPort))
    {
        Logger.Error("Socket initialization failed.  Only one copy of the tester can run on each computer");
        initSuccess = false;
    }
    else
    {
        Logger.Info("Listening on port ", wopt::kServerPort, ".  Press ENTER to stop the app.");
        initSuccess = true;
    }

    // Create application window
    WNDCLASSEX wc = { sizeof(WNDCLASSEX), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandle(NULL), NULL, NULL, NULL, NULL, _T("WLANOptimizerDemoClass"), NULL };
    RegisterClassEx(&wc);
    HWND hwnd = CreateWindow(_T("WLANOptimizerDemoClass"), _T("WLANOptimizer Demo"), WS_OVERLAPPEDWINDOW, 100, 100, 1280, 800, NULL, NULL, wc.hInstance, NULL);

    // Initialize Direct3D
    if (CreateDeviceD3D(hwnd) < 0)
    {
        CleanupDeviceD3D();
        UnregisterClass(_T("WLANOptimizerDemoClass"), wc.hInstance);
        return 1;
    }

    // Show the window
    ShowWindow(hwnd, SW_SHOWDEFAULT);
    UpdateWindow(hwnd);

    // Setup ImGui binding
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    //io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;  // Enable Keyboard Controls
    ImGui_ImplDX11_Init(hwnd, g_pd3dDevice, g_pd3dDeviceContext);

    // Setup style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    // Load Fonts
    // - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them. 
    // - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple. 
    // - If the file cannot be loaded, the function will return NULL. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
    // - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
    // - Read 'misc/fonts/README.txt' for more instructions and details.
    // - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
    //io.Fonts->AddFontDefault();
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
    //io.Fonts->AddFontFromFileTTF("../../misc/fonts/ProggyTiny.ttf", 10.0f);
    //ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, NULL, io.Fonts->GetGlyphRangesJapanese());
    //IM_ASSERT(font != NULL);

    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    // Main loop
    MSG msg;
    ZeroMemory(&msg, sizeof(msg));
    while (msg.message != WM_QUIT)
    {
        // You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
        // - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application.
        // - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application.
        // Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
        if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            continue;
        }
        ImGui_ImplDX11_NewFrame();

        // 1. Show a simple window.
        // Tip: if we don't call ImGui::Begin()/ImGui::End() the widgets automatically appears in a window called "Debug".
        {
            static float f = 0.0f;
            static int counter = 0;
            if (initSuccess)
            {
                ImGui::Text("This is a demo for the small WLANOptimizer library.");
                ImGui::Text("The library greatly improves latency on Windows laptops or desktops");
                ImGui::Text("that are using WiFi (Wireless LAN) for networking.");
                ImGui::Text("To set up a demo:");
                ImGui::BulletText("Run this program on two computers on the same LAN.");
                ImGui::BulletText("At least one needs to be a Windows computer on WiFi.");

                ImGui::Text("Render average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            }
            else
            {
                ImGui::BulletText("This program FAILED to open a socket!!");
                ImGui::BulletText("!! You need to use two computers for this demo.");
                ImGui::BulletText("!! You cannot run two instances.");
            }
        }

        DrawStats();

        // Rendering
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, NULL);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_color);
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // Present with vsync
                                     //g_pSwapChain->Present(0, 0); // Present without vsync
    }

    Logger.Info("Shutdown started..");

    ImGui_ImplDX11_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(hwnd);
    UnregisterClass(_T("ImGui Example"), wc.hInstance);

    sock.Shutdown();

    return 0;
}
