// Windows-specific FFI implementation for agus-maps-flutter
// Uses ANGLE for OpenGL ES -> DirectX 11 rendering
// This file is compiled only on Windows (controlled by CMakeLists.txt)

#ifdef _WIN32

#include "agus_maps_flutter.h"
#include "agus_angle_context_factory.hpp"

#include <windows.h>
#include <chrono>
#include <atomic>
#include <memory>
#include <string>
#include <mutex>
#include <cstdio> // for fflush
#include <algorithm> // for std::replace
#include <cstdlib> // for getenv
#include <cstring> // for strcmp
#include <exception>
#include <csignal>

#include "agus_env_utils.hpp"

#include "base/logging.hpp"
#include "map/framework.hpp"
#include "platform/local_country_file.hpp"
#include "drape/graphics_context_factory.hpp"
#include "drape_frontend/visual_params.hpp"
#include "drape_frontend/user_event_stream.hpp"
#include "drape_frontend/active_frame_callback.hpp"
#include "geometry/mercator.hpp"
#include "indexer/mwm_set.hpp"

// External platform init functions
extern "C" void AgusPlatform_InitPaths(const char* resourcePath, const char* writablePath);

// Globals
static std::unique_ptr<Framework> g_framework;
static drape_ptr<dp::ThreadSafeFactory> g_factory;
static agus::AgusAngleContextFactory* g_angleFactory = nullptr; // Raw pointer - owned by g_factory
static Microsoft::WRL::ComPtr<IDXGIAdapter> g_dxgiAdapter;
static std::string g_resourcePath;
static std::string g_writablePath;
bool g_platformInitialized = false;  // Shared with agus_platform_win.cpp
static bool g_drapeEngineCreated = false;
static bool g_loggingInitialized = false;

FFI_PLUGIN_EXPORT void comaps_set_dxgi_adapter(void* adapter)
{
    g_dxgiAdapter.Reset();
    if (!adapter)
    {
        if (agus::IsAgusVerboseEnabled())
        {
            OutputDebugStringA("[AgusMapsFlutterWin] comaps_set_dxgi_adapter(null)\n");
            std::fprintf(stderr, "[AgusMapsFlutterWin] comaps_set_dxgi_adapter(null)\n");
            std::fflush(stderr);
        }
        return;
    }

    IDXGIAdapter* dxgiAdapter = reinterpret_cast<IDXGIAdapter*>(adapter);
    g_dxgiAdapter = dxgiAdapter; // ComPtr AddRef

    // Ensure subsequent factory construction uses this adapter.
    agus::AgusAngleContextFactory::SetPreferredDxgiAdapter(g_dxgiAdapter.Get());

    if (agus::IsAgusVerboseEnabled())
    {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(dxgiAdapter->GetDesc(&desc)))
        {
            char name[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            std::fprintf(stderr,
                         "[AgusMapsFlutterWin] DXGI adapter set: %s (VendorId=0x%04X DeviceId=0x%04X)\n",
                         name, desc.VendorId, desc.DeviceId);
            std::fflush(stderr);
        }
    }
}

static LONG WINAPI AgusUnhandledExceptionFilter(EXCEPTION_POINTERS * ep)
{
    if (ep == nullptr || ep->ExceptionRecord == nullptr)
    {
        std::fprintf(stderr, "[AgusMapsFlutterWin] UnhandledExceptionFilter: (null)\n");
        std::fflush(stderr);
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto const * rec = ep->ExceptionRecord;
    std::fprintf(stderr,
                 "\n[AgusMapsFlutterWin] UNHANDLED EXCEPTION\n"
                 "  code=0x%08lX flags=0x%08lX addr=%p params=%lu\n",
                 rec->ExceptionCode, rec->ExceptionFlags, rec->ExceptionAddress,
                 static_cast<unsigned long>(rec->NumberParameters));
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void AgusSignalHandler(int sig)
{
    std::fprintf(stderr, "\n[AgusMapsFlutterWin] SIGNAL %d received\n", sig);
    std::fflush(stderr);
}

static void AgusTerminateHandler()
{
    std::fprintf(stderr, "\n[AgusMapsFlutterWin] std::terminate() called\n");
    try
    {
        auto eptr = std::current_exception();
        if (eptr)
            std::rethrow_exception(eptr);
        std::fprintf(stderr, "[AgusMapsFlutterWin] terminate: no active exception\n");
    }
    catch (std::exception const & e)
    {
        std::fprintf(stderr, "[AgusMapsFlutterWin] terminate: %s\n", e.what());
    }
    catch (...)
    {
        std::fprintf(stderr, "[AgusMapsFlutterWin] terminate: unknown exception\n");
    }
    std::fflush(stderr);

    // Don't try to recover; proceed with normal terminate behavior.
    std::abort();
}

static void InstallCrashHandlersOnce()
{
    static bool installed = false;
    if (installed)
        return;
    installed = true;

    // Avoid Windows error UI popups that can look like "silent exit" under flutter run.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    ::SetUnhandledExceptionFilter(&AgusUnhandledExceptionFilter);
    std::set_terminate(&AgusTerminateHandler);

    std::signal(SIGABRT, &AgusSignalHandler);
    std::signal(SIGSEGV, &AgusSignalHandler);
    std::signal(SIGILL, &AgusSignalHandler);

    std::atexit([]() {
        std::fprintf(stderr, "\n[AgusMapsFlutterWin] atexit() reached (process exiting)\n");
        std::fflush(stderr);
    });

    std::fprintf(stderr, "[AgusMapsFlutterWin] Crash/exit handlers installed\n");
    std::fflush(stderr);
}

// Ensure logging is configured to not abort on LERROR (DEBUG builds default to aborting on LERROR)
static void ensureLoggingConfigured() {
    if (!g_loggingInitialized) {
        InstallCrashHandlersOnce();

        // On Windows, do not abort on log levels. We want the process to stay
        // alive and report errors rather than exiting immediately.
        base::g_LogAbortLevel = base::NUM_LOG_LEVELS;

        // Optional verbose/profiling mode via env vars (Windows only).
        // Set AGUS_VERBOSE_LOG=1 or AGUS_PROFILE=1 to enable detailed logs.
        const char* verboseEnv = std::getenv("AGUS_VERBOSE_LOG");
        const char* profileEnv = std::getenv("AGUS_PROFILE");
        if (agus::IsEnvEnabled(verboseEnv) || agus::IsEnvEnabled(profileEnv)) {
            base::g_LogLevel = base::LDEBUG;
            OutputDebugStringA("[AgusMapsFlutterWin] Verbose logging enabled via AGUS_VERBOSE_LOG/AGUS_PROFILE\n");
            fprintf(stderr, "[AgusMapsFlutterWin] Verbose logging enabled via AGUS_VERBOSE_LOG/AGUS_PROFILE\n");
            fflush(stderr);

            if (agus::IsEnvEnabled(profileEnv)) {
                fprintf(stderr, "[AgusMapsFlutterWin] AGUS_PROFILE=1 enabled\n");
                fflush(stderr);
            }
        }

        g_loggingInitialized = true;
    }
}

// Surface dimensions
static int g_surfaceWidth = 0;
static int g_surfaceHeight = 0;
static float g_density = 1.0f;

// Frame notification timing for 60fps rate limiting
static std::chrono::steady_clock::time_point g_lastFrameNotification;
static constexpr auto kMinFrameInterval = std::chrono::milliseconds(16); // ~60fps
static std::atomic<bool> g_frameNotificationPending{false};

// Frame ready callback (set by Flutter plugin)
static void (*g_frameReadyCallback)() = nullptr;

/// Internal function to notify Flutter about a new frame
static void notifyFlutterFrameReady() {
    try {
        // Rate limiting: Enforce 60fps max
        auto now = std::chrono::steady_clock::now();
        auto elapsed = now - g_lastFrameNotification;
        if (elapsed < kMinFrameInterval) {
            return;
        }
        
        // Throttle: if a notification is already pending, skip this one
        bool expected = false;
        if (!g_frameNotificationPending.compare_exchange_strong(expected, true)) {
            return;
        }
        
        g_lastFrameNotification = now;
        
        // Call the registered callback
        if (g_frameReadyCallback) {
            g_frameReadyCallback();
        }
        
        g_frameNotificationPending.store(false);
    } catch (const std::exception& e) {
        OutputDebugStringA("[AgusMapsFlutterWin] notifyFlutterFrameReady exception: ");
        OutputDebugStringA(e.what());
        OutputDebugStringA("\n");
        fprintf(stderr, "[AgusMapsFlutterWin] notifyFlutterFrameReady exception: %s\n", e.what());
        fflush(stderr);
    } catch (...) {
        OutputDebugStringA("[AgusMapsFlutterWin] notifyFlutterFrameReady unknown exception\n");
        fprintf(stderr, "[AgusMapsFlutterWin] notifyFlutterFrameReady unknown exception\n");
        fflush(stderr);
    }
}

static void createDrapeEngineIfNeeded(int width, int height, float density) {
    if (g_drapeEngineCreated || !g_framework) {
        return;
    }
    
    if (width <= 0 || height <= 0) {
        OutputDebugStringA("[AgusMapsFlutterWin] createDrapeEngine: Invalid dimensions\n");
        return;
    }
    
    if (!g_factory) {
        OutputDebugStringA("[AgusMapsFlutterWin] createDrapeEngine: Factory not valid\n");
        return;
    }
    
    // Register active frame callback BEFORE creating DrapeEngine
    df::SetActiveFrameCallback([]() {
        try {
            notifyFlutterFrameReady();
        } catch (...) {
            OutputDebugStringA("[AgusMapsFlutterWin] Exception in SetActiveFrameCallback lambda\n");
        }
    });
    OutputDebugStringA("[AgusMapsFlutterWin] Active frame callback registered\n");
    fprintf(stderr, "[AgusMapsFlutterWin] Active frame callback registered\n");
    fflush(stderr);
    
    Framework::DrapeCreationParams p;
    p.m_apiVersion = dp::ApiVersion::OpenGLES3;
    p.m_surfaceWidth = width;
    p.m_surfaceHeight = height;
    p.m_visualScale = density;
    
    std::string msg = "[AgusMapsFlutterWin] Creating DrapeEngine: " + 
        std::to_string(width) + "x" + std::to_string(height) + 
        ", scale=" + std::to_string(density) + "\n";
    OutputDebugStringA(msg.c_str());
    fprintf(stderr, "%s", msg.c_str());
    fflush(stderr);
    
    g_framework->CreateDrapeEngine(make_ref(g_factory), std::move(p));
    g_drapeEngineCreated = true;
    
    OutputDebugStringA("[AgusMapsFlutterWin] DrapeEngine created successfully\n");
    fprintf(stderr, "[AgusMapsFlutterWin] DrapeEngine created successfully\n");
    fflush(stderr);
}

// ============================================================================
// FFI Exports
// ============================================================================

// Initialize with separate resource and writable paths
FFI_PLUGIN_EXPORT void comaps_init_paths(const char* resourcePath, const char* writablePath) {
    ensureLoggingConfigured();
    
    std::string msg = "[AgusMapsFlutterWin] comaps_init_paths: resource=" + 
        std::string(resourcePath) + ", writable=" + std::string(writablePath) + "\n";
    OutputDebugStringA(msg.c_str());
    
    // Store paths for later use
    g_resourcePath = resourcePath;
    g_writablePath = writablePath;
    
    // Initialize platform
    AgusPlatform_InitPaths(resourcePath, writablePath);
    g_platformInitialized = true;
    
    OutputDebugStringA("[AgusMapsFlutterWin] Platform initialized, Framework deferred to render thread\n");
}

// Legacy init function (forwards to comaps_init_paths)
FFI_PLUGIN_EXPORT void comaps_init(const char* apkPath, const char* storagePath) {
    comaps_init_paths(apkPath, storagePath);
}

FFI_PLUGIN_EXPORT void comaps_load_map_path(const char* path) {
    std::string msg = "[AgusMapsFlutterWin] comaps_load_map_path: " + std::string(path) + "\n";
    OutputDebugStringA(msg.c_str());
    
    if (g_framework) {
        g_framework->RegisterAllMaps();
        OutputDebugStringA("[AgusMapsFlutterWin] Maps registered\n");
    } else {
        OutputDebugStringA("[AgusMapsFlutterWin] Framework not yet initialized, maps will be loaded later\n");
    }
}

FFI_PLUGIN_EXPORT void comaps_set_view(double lat, double lon, int zoom) {
    std::string msg = "[AgusMapsFlutterWin] comaps_set_view: lat=" + std::to_string(lat) + 
        ", lon=" + std::to_string(lon) + ", zoom=" + std::to_string(zoom) + "\n";
    OutputDebugStringA(msg.c_str());
    
    if (g_framework) {
        g_framework->SetViewportCenter(mercator::FromLatLon(lat, lon), zoom, false);
    }
}

FFI_PLUGIN_EXPORT void comaps_touch(int type, int id1, float x1, float y1, int id2, float x2, float y2) {
    if (!g_framework) return;
    
    df::TouchEvent event;
    
    switch (type) {
        case 1: event.SetTouchType(df::TouchEvent::TOUCH_DOWN); break;
        case 2: event.SetTouchType(df::TouchEvent::TOUCH_MOVE); break;
        case 3: event.SetTouchType(df::TouchEvent::TOUCH_UP); break;
        case 4: event.SetTouchType(df::TouchEvent::TOUCH_CANCEL); break;
        default: return;
    }
    
    // Set first touch
    df::Touch t1;
    t1.m_id = id1;
    t1.m_location = m2::PointF(x1, y1);
    event.SetFirstTouch(t1);
    event.SetFirstMaskedPointer(0);
    
    // Set second touch if valid (for multitouch)
    if (id2 >= 0) {
        df::Touch t2;
        t2.m_id = id2;
        t2.m_location = m2::PointF(x2, y2);
        event.SetSecondTouch(t2);
        event.SetSecondMaskedPointer(1);
    }
    
    g_framework->TouchEvent(event);
}

FFI_PLUGIN_EXPORT int comaps_register_single_map(const char* fullPath) {
    if (!g_framework) {
        OutputDebugStringA("[AgusMapsFlutterWin] comaps_register_single_map: Framework not initialized\n");
        fprintf(stderr, "[AgusMapsFlutterWin] comaps_register_single_map: Framework not initialized\n");
        fflush(stderr);
        return -1;
    }
    
    try {
        std::string pathStr(fullPath);
        
        // Normalize path separators for Windows (replace forward slashes with backslashes)
        std::replace(pathStr.begin(), pathStr.end(), '/', '\\');
        
        std::string msg = "[AgusMapsFlutterWin] Registering map: " + pathStr + "\n";
        OutputDebugStringA(msg.c_str());
        fprintf(stderr, "%s", msg.c_str());
        fflush(stderr);
        
        // Use MakeTemporary which correctly handles full paths to MWM files
        platform::LocalCountryFile file = platform::LocalCountryFile::MakeTemporary(pathStr);
        file.SyncWithDisk();
        
        auto result = g_framework->RegisterMap(file);
        int resultCode = static_cast<int>(result.second);
        
        if (result.second == MwmSet::RegResult::Success) {
            msg = "[AgusMapsFlutterWin] Successfully registered: " + pathStr + "\n";
            OutputDebugStringA(msg.c_str());
            fprintf(stderr, "%s", msg.c_str());
            fflush(stderr);
            return 0;  // Success
        } else {
            msg = "[AgusMapsFlutterWin] Failed to register " + pathStr + ", result=" + std::to_string(resultCode) + "\n";
            OutputDebugStringA(msg.c_str());
            fprintf(stderr, "%s", msg.c_str());
            fflush(stderr);
            return resultCode;
        }
    } catch (const std::exception & e) {
        std::string msg = "[AgusMapsFlutterWin] Exception registering map: " + std::string(e.what()) + "\n";
        OutputDebugStringA(msg.c_str());
        fprintf(stderr, "%s", msg.c_str());
        fflush(stderr);
        return -2;
    }
}

// ============================================================================
// Windows-specific surface management (called from Flutter plugin)
// ============================================================================

/// Set the frame ready callback (called from Flutter plugin)
FFI_PLUGIN_EXPORT void comaps_set_frame_callback(void (*callback)()) {
    g_frameReadyCallback = callback;
    OutputDebugStringA("[AgusMapsFlutterWin] Frame callback set\n");
}

/// Create rendering surface with specified dimensions
FFI_PLUGIN_EXPORT int comaps_create_surface(int width, int height, float density) {
    // Use fprintf(stderr) which Flutter captures
    fprintf(stderr, "[AgusMapsFlutterWin] >>> comaps_create_surface ENTRY\n");
    fflush(stderr);
    
    OutputDebugStringA("[AgusMapsFlutterWin] >>> comaps_create_surface ENTRY\n");
    fflush(stdout);
    
    ensureLoggingConfigured();
    
    fprintf(stderr, "[AgusMapsFlutterWin] comaps_create_surface: %dx%d, density=%.2f\n", width, height, density);
    fflush(stderr);
    
    if (!g_platformInitialized) {
        OutputDebugStringA("[AgusMapsFlutterWin] Platform not initialized!\n");
        return -1;
    }
    
    OutputDebugStringA("[AgusMapsFlutterWin] Platform initialized check passed\n");
    fflush(stdout);
    
    g_surfaceWidth = width;
    g_surfaceHeight = height;
    g_density = density;
    
    // Create Framework if not already created
    if (!g_framework) {
        OutputDebugStringA("[AgusMapsFlutterWin] Creating Framework...\n");
        fflush(stdout);
        
        try {
            FrameworkParams params;
            params.m_enableDiffs = false;
            params.m_numSearchAPIThreads = 1;
            
            OutputDebugStringA("[AgusMapsFlutterWin] Framework params set, constructing...\n");
            fflush(stdout);
            
            g_framework = std::make_unique<Framework>(params, false /* loadMaps */);
            OutputDebugStringA("[AgusMapsFlutterWin] Framework constructed\n");
            fflush(stdout);
        } catch (const std::exception& e) {
            std::string err = "[AgusMapsFlutterWin] Framework exception: " + std::string(e.what()) + "\n";
            OutputDebugStringA(err.c_str());
            return -3;
        } catch (...) {
            OutputDebugStringA("[AgusMapsFlutterWin] Framework unknown exception\n");
            return -4;
        }
        
        OutputDebugStringA("[AgusMapsFlutterWin] Framework created, registering maps...\n");
        fflush(stdout);
        
        // Register maps
        try {
            g_framework->RegisterAllMaps();
            OutputDebugStringA("[AgusMapsFlutterWin] Maps registered\n");
            fflush(stdout);
        } catch (const std::exception& e) {
            std::string err = "[AgusMapsFlutterWin] RegisterAllMaps exception: " + std::string(e.what()) + "\n";
            OutputDebugStringA(err.c_str());
            return -5;
        }
    }
    
    OutputDebugStringA("[AgusMapsFlutterWin] Creating ANGLE context factory...\n");
    fflush(stdout);
    
    // Create ANGLE context factory
    agus::AgusAngleContextFactory* angleFactory = nullptr;
    try {
        angleFactory = new agus::AgusAngleContextFactory(width, height);
        OutputDebugStringA("[AgusMapsFlutterWin] ANGLE factory constructed\n");
        fflush(stdout);
    } catch (const std::exception& e) {
        std::string err = "[AgusMapsFlutterWin] ANGLE factory exception: " + std::string(e.what()) + "\n";
        OutputDebugStringA(err.c_str());
        return -6;
    } catch (...) {
        OutputDebugStringA("[AgusMapsFlutterWin] ANGLE factory unknown exception\n");
        return -7;
    }
    
    if (!angleFactory->IsValid()) {
        OutputDebugStringA("[AgusMapsFlutterWin] ANGLE factory not valid\n");
        delete angleFactory;
        return -2;
    }
    
    OutputDebugStringA("[AgusMapsFlutterWin] ANGLE factory valid, creating ThreadSafeFactory...\n");
    fflush(stdout);
    
    // Store raw pointer for accessing shared handle, ownership transfers to ThreadSafeFactory
    g_angleFactory = angleFactory;
    
    try {
        g_factory = make_unique_dp<dp::ThreadSafeFactory>(angleFactory);
        OutputDebugStringA("[AgusMapsFlutterWin] ThreadSafeFactory created\n");
        fflush(stdout);
    } catch (const std::exception& e) {
        std::string err = "[AgusMapsFlutterWin] ThreadSafeFactory exception: " + std::string(e.what()) + "\n";
        OutputDebugStringA(err.c_str());
        return -8;
    }
    
    OutputDebugStringA("[AgusMapsFlutterWin] Creating DrapeEngine...\n");
    fflush(stdout);
    
    // Create DrapeEngine
    try {
        createDrapeEngineIfNeeded(width, height, density);
        OutputDebugStringA("[AgusMapsFlutterWin] DrapeEngine creation complete\n");
        fflush(stdout);
    } catch (const std::exception& e) {
        std::string err = "[AgusMapsFlutterWin] DrapeEngine exception: " + std::string(e.what()) + "\n";
        OutputDebugStringA(err.c_str());
        return -9;
    } catch (...) {
        OutputDebugStringA("[AgusMapsFlutterWin] DrapeEngine unknown exception\n");
        return -10;
    }
    
    OutputDebugStringA("[AgusMapsFlutterWin] Surface created successfully\n");
    fflush(stdout);
    return 0;
}

/// Get the D3D11 shared texture handle for Flutter integration
FFI_PLUGIN_EXPORT void* comaps_get_shared_handle() {
    bool const verbose = agus::IsAgusVerboseEnabled();
    if (verbose)
    {
        OutputDebugStringA("[AgusMapsFlutterWin] comaps_get_shared_handle() called\n");
        fprintf(stderr, "[AgusMapsFlutterWin] comaps_get_shared_handle() called\n");
        fflush(stderr);
    }
    
    if (g_angleFactory) {
        if (verbose)
        {
            OutputDebugStringA("[AgusMapsFlutterWin] g_angleFactory exists, getting handle...\n");
            fprintf(stderr, "[AgusMapsFlutterWin] g_angleFactory exists, getting handle...\n");
            fflush(stderr);
        }
        
        void* handle = g_angleFactory->GetSharedTextureHandle();
        
        if (verbose)
        {
            char msg[256];
            snprintf(msg, sizeof(msg), "[AgusMapsFlutterWin] Shared handle: %p\n", handle);
            OutputDebugStringA(msg);
            fprintf(stderr, "%s", msg);
            fflush(stderr);
        }
        
        return handle;
    }
    
    if (verbose)
    {
        OutputDebugStringA("[AgusMapsFlutterWin] g_angleFactory is null!\n");
        fprintf(stderr, "[AgusMapsFlutterWin] g_angleFactory is null!\n");
        fflush(stderr);
    }
    return nullptr;
}

/// Update surface size (e.g., on window resize)
FFI_PLUGIN_EXPORT void comaps_resize_surface(int width, int height) {
    if (width == g_surfaceWidth && height == g_surfaceHeight) {
        return;
    }
    
    std::string msg = "[AgusMapsFlutterWin] comaps_resize_surface: " + 
        std::to_string(width) + "x" + std::to_string(height) + "\n";
    OutputDebugStringA(msg.c_str());
    
    g_surfaceWidth = width;
    g_surfaceHeight = height;
    
    if (g_angleFactory) {
        g_angleFactory->Resize(width, height);
    }
    
    if (g_framework && g_drapeEngineCreated) {
        g_framework->OnSize(width, height);
    }
}

/// Destroy rendering surface
FFI_PLUGIN_EXPORT void comaps_destroy_surface() {
    OutputDebugStringA("[AgusMapsFlutterWin] comaps_destroy_surface\n");
    
    if (g_framework && g_drapeEngineCreated) {
        g_framework->SetRenderingDisabled(true);
    }
    
    g_factory.reset(); // This also destroys g_angleFactory
    g_angleFactory = nullptr;
    g_drapeEngineCreated = false;
}

// Debug function to list all registered MWMs and their bounds
FFI_PLUGIN_EXPORT void comaps_debug_list_mwms() {
    OutputDebugStringA("[AgusMapsFlutterWin] === DEBUG: Listing all registered MWMs ===\n");
    fprintf(stderr, "[AgusMapsFlutterWin] === DEBUG: Listing all registered MWMs ===\n");
    fflush(stderr);
    
    if (!g_framework) {
        OutputDebugStringA("[AgusMapsFlutterWin] comaps_debug_list_mwms: Framework not initialized\n");
        fprintf(stderr, "[AgusMapsFlutterWin] comaps_debug_list_mwms: Framework not initialized\n");
        fflush(stderr);
        return;
    }
    
    auto const & dataSource = g_framework->GetDataSource();
    std::vector<std::shared_ptr<MwmInfo>> mwms;
    dataSource.GetMwmsInfo(mwms);
    
    char msg[256];
    snprintf(msg, sizeof(msg), "[AgusMapsFlutterWin] Total registered MWMs: %zu\n", mwms.size());
    OutputDebugStringA(msg);
    fprintf(stderr, "%s", msg);
    fflush(stderr);
    
    for (auto const & info : mwms) {
        auto const & bounds = info->m_bordersRect;
        const char* typeStr = "UNKNOWN";
        switch (info->GetType()) {
            case MwmInfo::COUNTRY: typeStr = "COUNTRY"; break;
            case MwmInfo::COASTS: typeStr = "COASTS"; break;
            case MwmInfo::WORLD: typeStr = "WORLD"; break;
        }
        
        char detail[512];
        snprintf(detail, sizeof(detail), 
            "  MWM: %s [%s] version=%lld scales=[%d-%d] bounds=[%.4f,%.4f - %.4f,%.4f] status=%d\n",
            info->GetCountryName().c_str(),
            typeStr,
            static_cast<long long>(info->GetVersion()),
            info->m_minScale,
            info->m_maxScale,
            bounds.minX(), bounds.minY(),
            bounds.maxX(), bounds.maxY(),
            static_cast<int>(info->GetStatus()));
        OutputDebugStringA(detail);
        fprintf(stderr, "%s", detail);
    }
    fflush(stderr);
}

// Debug function to check which MWMs cover a specific point
FFI_PLUGIN_EXPORT void comaps_debug_check_point(double lat, double lon) {
    char msg[256];
    snprintf(msg, sizeof(msg), "[AgusMapsFlutterWin] === DEBUG: Checking point coverage lat=%.6f, lon=%.6f ===\n", lat, lon);
    OutputDebugStringA(msg);
    fprintf(stderr, "%s", msg);
    fflush(stderr);
    
    if (!g_framework) {
        OutputDebugStringA("[AgusMapsFlutterWin] comaps_debug_check_point: Framework not initialized\n");
        fprintf(stderr, "[AgusMapsFlutterWin] comaps_debug_check_point: Framework not initialized\n");
        fflush(stderr);
        return;
    }
    
    // Convert to Mercator coordinates (what the engine uses internally)
    m2::PointD const pt = mercator::FromLatLon(lat, lon);
    snprintf(msg, sizeof(msg), "[AgusMapsFlutterWin] Mercator coords: x=%.6f, y=%.6f\n", pt.x, pt.y);
    OutputDebugStringA(msg);
    fprintf(stderr, "%s", msg);
    fflush(stderr);
    
    auto const & dataSource = g_framework->GetDataSource();
    std::vector<std::shared_ptr<MwmInfo>> mwms;
    dataSource.GetMwmsInfo(mwms);
    
    int coveringCount = 0;
    for (auto const & info : mwms) {
        if (info->m_bordersRect.IsPointInside(pt)) {
            coveringCount++;
            char detail[256];
            snprintf(detail, sizeof(detail), "  COVERS: %s [scales %d-%d]\n",
                info->GetCountryName().c_str(),
                info->m_minScale, info->m_maxScale);
            OutputDebugStringA(detail);
            fprintf(stderr, "%s", detail);
        }
    }
    
    if (coveringCount == 0) {
        OutputDebugStringA("[AgusMapsFlutterWin]   NO MWM covers this point!\n");
        fprintf(stderr, "[AgusMapsFlutterWin]   NO MWM covers this point!\n");
    } else {
        snprintf(msg, sizeof(msg), "[AgusMapsFlutterWin] Point covered by %d MWMs\n", coveringCount);
        OutputDebugStringA(msg);
        fprintf(stderr, "%s", msg);
    }
    
    OutputDebugStringA("[AgusMapsFlutterWin] === END point check ===\n");
    fprintf(stderr, "[AgusMapsFlutterWin] === END point check ===\n");
    fflush(stderr);
}

#endif // _WIN32
