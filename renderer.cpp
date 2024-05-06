// renderer.cpp : Defines the entry point for the application.
//

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <cassert>
#include <chrono>
#include <cstdint>
#include <iostream>

#include <d3d12.h>
#include <D3dx12.h>
#include <dxgi1_6.h>
#include <wrl.h>

using Microsoft::WRL::ComPtr;

#undef min
#undef max

#ifdef _DEBUG
constexpr bool g_isDebug = true;
#else
constexpr bool g_isDebug = false;
#endif

constexpr uint32_t g_width = 800;
constexpr uint32_t g_height = 600;
constexpr uint8_t g_bufferCount = 3;
uint32_t g_clientWidth = g_width;
uint32_t g_clientHeight = g_height;

void parseCommandLineArguments(int argc, wchar_t** argv);
void enableDebugLayer();
void handleInput(GLFWwindow* window);
void update();
void render();
void resize(uint32_t width, uint32_t height);
void setFullscreen(bool fullscreen);

ComPtr<IDXGIAdapter4> getAdapter();
ComPtr<ID3D12Device2> getDevice(ComPtr<IDXGIAdapter4> adapter);
ComPtr<ID3D12CommandQueue> createCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
ComPtr<IDXGISwapChain4> createSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount);
ComPtr<ID3D12DescriptorHeap> createDescriptorHeap(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors);
void updateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap);
ComPtr<ID3D12CommandAllocator> createCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type);
ComPtr<ID3D12GraphicsCommandList> createCommandList(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type);
bool checkTearingSupport();
ComPtr<ID3D12Fence> createFence(ComPtr<ID3D12Device2> device);
HANDLE createEventHandle();
uint64_t signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, uint64_t& fenceValue);
void waitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent, std::chrono::milliseconds duration = std::chrono::milliseconds::max());
void flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, uint64_t& fenceValue, HANDLE fenceEvent);


HWND g_hWnd;
RECT g_windowRect;
bool g_VSync = true;
bool g_tearingSupported = false;
bool g_fullscreen = false;

ComPtr<ID3D12Device2> g_device;
ComPtr<ID3D12CommandQueue> g_commandQueue;
ComPtr<IDXGISwapChain4> g_swapChain;
ComPtr<ID3D12Resource> g_backBuffers[g_bufferCount];
ComPtr<ID3D12GraphicsCommandList> g_commandList;
ComPtr<ID3D12CommandAllocator> g_commandAllocators[g_bufferCount];
ComPtr<ID3D12DescriptorHeap> g_rtvDescriptorHeap;
UINT g_rtvDescriptorSize;
UINT g_currentBackBufferIndex;
bool g_isInitialized = false;

ComPtr<ID3D12Fence> g_fence;
uint64_t g_fenceValue = 0;
uint64_t g_fenceValues[g_bufferCount] = {};
HANDLE g_fenceEvent;

static inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        throw std::runtime_error("HRESULT is not S_OK");
    }
}

#ifdef _DEBUG
int main(int argc, wchar_t** argv)
#else
int CALLBACK wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow)
#endif
{
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(g_width, g_height, "DirectX 12", nullptr, nullptr);
    if (!window) {
        return -1;
    }
    glfwSetWindowSizeCallback(window,
        [](GLFWwindow* window, int width, int height) {
            resize(width, height);
        }
    );

    g_hWnd = glfwGetWin32Window(window);

    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#ifdef _DEBUG
        parseCommandLineArguments(argc, argv);
#else
        int argc;
        wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        parseCommandLineArguments(argc, argv);
        LocalFree(argv);
#endif
    enableDebugLayer();

    g_tearingSupported = checkTearingSupport();
    GetWindowRect(g_hWnd, &g_windowRect);

    auto adapter = getAdapter();
    g_device = getDevice(adapter);
    g_commandQueue = createCommandQueue(g_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    g_swapChain = createSwapChain(g_hWnd, g_commandQueue, g_clientWidth, g_clientHeight, g_bufferCount);
    g_currentBackBufferIndex = g_swapChain->GetCurrentBackBufferIndex();
    g_rtvDescriptorHeap = createDescriptorHeap(g_device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, g_bufferCount);
    g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    updateRenderTargetViews(g_device, g_swapChain, g_rtvDescriptorHeap);

    for (uint32_t i = 0; i < g_bufferCount; ++i) {
        g_commandAllocators[i] = createCommandAllocator(g_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
    }
    g_commandList = createCommandList(g_device, g_commandAllocators[g_currentBackBufferIndex], D3D12_COMMAND_LIST_TYPE_DIRECT);

    g_fence = createFence(g_device);
    g_fenceEvent = createEventHandle();

    g_isInitialized = true;
    resize(g_clientWidth, g_clientHeight);

    while (!glfwWindowShouldClose(window)) {
        handleInput(window);
        update();
        render();
        glfwPollEvents();
    }

    flush(g_commandQueue, g_fence, g_fenceValue, g_fenceEvent);
    CloseHandle(g_fenceEvent);

    glfwTerminate();

    return 0;
}

void parseCommandLineArguments(int argc, wchar_t** argv)
{
    for (int i = 0; i < argc; ++i) {
        if (wcscmp(argv[i], L"-w") == 0) {
            g_clientWidth = std::max(1, _wtoi(argv[++i]));
        } else if (wcscmp(argv[i], L"-h") == 0) {
            g_clientHeight = std::max(1, _wtoi(argv[++i]));
        }
    }
}

void enableDebugLayer()
{
    if constexpr (g_isDebug) {
        ComPtr<ID3D12Debug> debugController = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
            debugController->EnableDebugLayer();

        }
    }
}

void handleInput(GLFWwindow* window)
{
    if (glfwGetKey(window, GLFW_KEY_F11) == GLFW_PRESS) {
        setFullscreen(!g_fullscreen);
    }

    if (glfwGetKey(window, GLFW_KEY_V) == GLFW_PRESS) {
        g_VSync = !g_VSync;
    }

    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
}

void update()
{
    static uint64_t frameCount = 0;
    static double elapsedSeconds = 0.0;
    static std::chrono::high_resolution_clock clock;
    static auto t0 = clock.now();

    ++frameCount;
    auto t1 = clock.now();
    auto deltaTime = t1 - t0;
    t0 = t1;

    elapsedSeconds += deltaTime.count() * 1e-9;
    if (elapsedSeconds > 1.0) {
        char buffer[500];
        auto fps = frameCount / elapsedSeconds;
        sprintf_s(buffer, 500, "FPS: %f\n", fps);
        OutputDebugStringA(buffer);

        frameCount = 0;
        elapsedSeconds = 0.0;
    }
}

void render()
{
    auto commandAllocator = g_commandAllocators[g_currentBackBufferIndex];
    auto backBuffer = g_backBuffers[g_currentBackBufferIndex];

    commandAllocator->Reset();
    g_commandList->Reset(commandAllocator.Get(), nullptr);

    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_commandList->ResourceBarrier(1, &barrier);

        FLOAT clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(g_rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), g_currentBackBufferIndex, g_rtvDescriptorSize);

        g_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    }

    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(backBuffer.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
        g_commandList->ResourceBarrier(1, &barrier);

        ThrowIfFailed(g_commandList->Close());

        ID3D12CommandList* const commandLists[] = { g_commandList.Get() };
        g_commandQueue->ExecuteCommandLists(_countof(commandLists), commandLists);

        UINT syncInterval = g_VSync ? 1 : 0;
        UINT presentFlags = g_tearingSupported && !g_VSync ? DXGI_PRESENT_ALLOW_TEARING : 0;
        ThrowIfFailed(g_swapChain->Present(syncInterval, presentFlags));

        g_fenceValues[g_currentBackBufferIndex] = signal(g_commandQueue, g_fence, g_fenceValue);

        g_currentBackBufferIndex = g_swapChain->GetCurrentBackBufferIndex();
        waitForFenceValue(g_fence, g_fenceValues[g_currentBackBufferIndex], g_fenceEvent);
    }
}

void resize(uint32_t width, uint32_t height)
{
    if (g_clientWidth == width && g_clientHeight == height) {
        return;
    }

    g_clientHeight = std::max(1u, height);
    g_clientWidth = std::max(1u, width);

    flush(g_commandQueue, g_fence, g_fenceValue, g_fenceEvent);

    if (g_swapChain) {
        for (uint32_t i = 0; i < g_bufferCount; ++i) {
            g_backBuffers[i].Reset();
            g_fenceValues[i] = g_fenceValue;
        }
    }

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    ThrowIfFailed(g_swapChain->GetDesc1(&desc));
    ThrowIfFailed(g_swapChain->ResizeBuffers(g_bufferCount, width, height, desc.Format, desc.Flags));

    g_currentBackBufferIndex = g_swapChain->GetCurrentBackBufferIndex();

    updateRenderTargetViews(g_device, g_swapChain, g_rtvDescriptorHeap);
}

void setFullscreen(bool fullscreen)
{
    if (g_fullscreen == fullscreen) {
        return;
    }

    if (fullscreen) {
        GetWindowRect(g_hWnd, &g_windowRect);
        UINT windowStyle = WS_OVERLAPPEDWINDOW & ~(WS_CAPTION | WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_SYSMENU | WS_THICKFRAME);
        SetWindowLongW(g_hWnd, GWL_STYLE, windowStyle);

        HMONITOR hMonitor = MonitorFromWindow(g_hWnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEX monitorInfo = {};
        monitorInfo.cbSize = sizeof(MONITORINFOEX);
        GetMonitorInfo(hMonitor, &monitorInfo);

        SetWindowPos(g_hWnd, HWND_TOP,
            monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.top,
            monitorInfo.rcMonitor.right - monitorInfo.rcMonitor.left,
            monitorInfo.rcMonitor.bottom - monitorInfo.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE
        );

        ShowWindow(g_hWnd, SW_MAXIMIZE);
    } else {
        SetWindowLong(g_hWnd, GWL_STYLE, WS_OVERLAPPEDWINDOW);

        SetWindowPos(g_hWnd, HWND_NOTOPMOST,
            g_windowRect.left,
            g_windowRect.top,
            g_windowRect.right - g_windowRect.left,
            g_windowRect.bottom - g_windowRect.top,
            SWP_FRAMECHANGED | SWP_NOACTIVATE
        );

        ShowWindow(g_hWnd, SW_NORMAL);
    }

    g_fullscreen = fullscreen;
}

ComPtr<IDXGIAdapter4> getAdapter()
{

    uint32_t createFactoryFlags = 0;
    if constexpr (g_isDebug) {
        ComPtr<ID3D12Debug> debugController = nullptr;
        if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
        {
            debugController->EnableDebugLayer();
        }

        createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }

    ComPtr<IDXGIFactory4> factory = nullptr;
    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory)));

    ComPtr<IDXGIAdapter1> adapter1 = nullptr;
    ComPtr<IDXGIAdapter4> adapter4 = nullptr;
    ThrowIfFailed(factory->EnumAdapters1(0, &adapter1));

    UINT adapterIndex = 0;
    bool adapterFound = false;
    SIZE_T maxVideoMemory = 0;
    while (factory->EnumAdapters1(adapterIndex, &adapter1) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        adapter1->GetDesc1(&desc);

        if(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
            ++adapterIndex;
            continue;
        }

        if (SUCCEEDED(D3D12CreateDevice(adapter1.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)) &&
            desc.DedicatedVideoMemory > maxVideoMemory)
        {
            maxVideoMemory = desc.DedicatedVideoMemory;
            adapter1.As(&adapter4);
            adapterFound = true;
        }

        ++adapterIndex;
    }

    if(!adapterFound) {
        std::wcerr << "Failed to find a suitable adapter" << std::endl;
    }

    if constexpr (g_isDebug) {
        DXGI_ADAPTER_DESC1 desc;
        adapter4->GetDesc1(&desc);
        std::wcout << "Adapter: " << desc.Description << std::endl;
    }

    return adapter4;
}

ComPtr<ID3D12Device2> getDevice(ComPtr<IDXGIAdapter4> adapter)
{
    ComPtr<ID3D12Device2> device = nullptr;
    ThrowIfFailed(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));;

    if constexpr (g_isDebug) {
        ComPtr<ID3D12InfoQueue> infoQueue = nullptr;
        if (SUCCEEDED(device.As(&infoQueue))) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);

            D3D12_MESSAGE_SEVERITY severities[] = {
                D3D12_MESSAGE_SEVERITY_INFO
            };

            D3D12_MESSAGE_ID denyIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_MAP_INVALID_NULLRANGE,
                D3D12_MESSAGE_ID_UNMAP_INVALID_NULLRANGE
            };

            D3D12_INFO_QUEUE_FILTER filter = {};
            // filter.DenyList.NumCategories = _countof(categories);
            // filter.DenyList.pCategoryList = categories;
            filter.DenyList.NumSeverities = _countof(severities);
            filter.DenyList.pSeverityList = severities;
            filter.DenyList.NumIDs = _countof(denyIds);
            filter.DenyList.pIDList = denyIds;

            infoQueue->PushStorageFilter(&filter);
        }
    }

    return device;
}   

ComPtr<ID3D12CommandQueue> createCommandQueue(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
    D3D12_COMMAND_QUEUE_DESC desc = {};
    desc.Type = type;
    desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    desc.NodeMask = 0;

    ComPtr<ID3D12CommandQueue> commandQueue = nullptr;
    ThrowIfFailed(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&commandQueue)));

    return commandQueue;
}

ComPtr<IDXGISwapChain4> createSwapChain(HWND hWnd, ComPtr<ID3D12CommandQueue> commandQueue, uint32_t width, uint32_t height, uint32_t bufferCount)
{
    uint32_t createFactoryFlags = 0;
    if constexpr (g_isDebug) {
        createFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }

    ComPtr<IDXGIFactory4> factory = nullptr;
    ThrowIfFailed(CreateDXGIFactory2(createFactoryFlags, IID_PPV_ARGS(&factory)));

    DXGI_SWAP_CHAIN_DESC1 desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Stereo = FALSE;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = bufferCount;
    desc.Scaling = DXGI_SCALING_STRETCH;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    desc.Flags = checkTearingSupport() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    ComPtr<IDXGISwapChain1> swapChain1 = nullptr;
    ComPtr<IDXGISwapChain4> swapChain4 = nullptr;
    ThrowIfFailed(factory->CreateSwapChainForHwnd(commandQueue.Get(), hWnd, &desc, nullptr, nullptr, &swapChain1));
    ThrowIfFailed(factory->MakeWindowAssociation(hWnd, DXGI_MWA_NO_ALT_ENTER));
    ThrowIfFailed(swapChain1.As(&swapChain4));

    return swapChain4;
}

ComPtr<ID3D12DescriptorHeap> createDescriptorHeap(ComPtr<ID3D12Device2> device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t numDescriptors)
{
    D3D12_DESCRIPTOR_HEAP_DESC desc = {};
    desc.NumDescriptors = numDescriptors;
    desc.Type = type;

    ComPtr<ID3D12DescriptorHeap> descriptorHeap = nullptr;
    ThrowIfFailed(device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptorHeap)));

    return descriptorHeap;
}

void updateRenderTargetViews(ComPtr<ID3D12Device2> device, ComPtr<IDXGISwapChain4> swapChain, ComPtr<ID3D12DescriptorHeap> descriptorHeap)
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(descriptorHeap->GetCPUDescriptorHandleForHeapStart());

    for (uint32_t i = 0; i < g_bufferCount; ++i) {
        ComPtr<ID3D12Resource> backBuffer = nullptr;
        ThrowIfFailed(swapChain->GetBuffer(i, IID_PPV_ARGS(&backBuffer)));
        device->CreateRenderTargetView(backBuffer.Get(), nullptr, rtvHandle);
        g_backBuffers[i] = backBuffer;
        rtvHandle.Offset(1, device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV));
    }
}

ComPtr<ID3D12CommandAllocator> createCommandAllocator(ComPtr<ID3D12Device2> device, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12CommandAllocator> commandAllocator = nullptr;
    ThrowIfFailed(device->CreateCommandAllocator(type, IID_PPV_ARGS(&commandAllocator)));

    return commandAllocator;
}

ComPtr<ID3D12GraphicsCommandList> createCommandList(ComPtr<ID3D12Device2> device, ComPtr<ID3D12CommandAllocator> commandAllocator, D3D12_COMMAND_LIST_TYPE type)
{
    ComPtr<ID3D12GraphicsCommandList> commandList = nullptr;
    ThrowIfFailed(device->CreateCommandList(0, type, commandAllocator.Get(), nullptr, IID_PPV_ARGS(&commandList)));
    ThrowIfFailed(commandList->Close());

    return commandList;
}

bool checkTearingSupport()
{
    BOOL allowTearing = FALSE;

    ComPtr<IDXGIFactory4> factory4 = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)))) {
        return false;
    }

    ComPtr<IDXGIFactory5> factory5 = nullptr;
    if (FAILED(factory4.As(&factory5))) {
        return false;
    }

    if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing)))) {
        return false;
    }

    return allowTearing == TRUE;
}

ComPtr<ID3D12Fence> createFence(ComPtr<ID3D12Device2> device)
{
    ComPtr<ID3D12Fence> fence = nullptr;
    ThrowIfFailed(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

    return fence;
}

HANDLE createEventHandle()
{
    HANDLE fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    assert(fenceEvent && "Failed to create fence event");

    return fenceEvent;
}

uint64_t signal(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, uint64_t& fenceValue)
{
    uint64_t fenceValueForSignal = ++fenceValue;
    ThrowIfFailed(commandQueue->Signal(fence.Get(), fenceValueForSignal));

    return fenceValueForSignal;
}

void waitForFenceValue(ComPtr<ID3D12Fence> fence, uint64_t fenceValue, HANDLE fenceEvent, std::chrono::milliseconds duration)
{
    if (fence->GetCompletedValue() < fenceValue) {
        ThrowIfFailed(fence->SetEventOnCompletion(fenceValue, fenceEvent));
        WaitForSingleObject(fenceEvent, static_cast<DWORD>(duration.count()));
    }
}

void flush(ComPtr<ID3D12CommandQueue> commandQueue, ComPtr<ID3D12Fence> fence, uint64_t& fenceValue, HANDLE fenceEvent)
{
    uint64_t fenceValueForSignal = signal(commandQueue, fence, fenceValue);
    waitForFenceValue(fence, fenceValueForSignal, fenceEvent);
}
