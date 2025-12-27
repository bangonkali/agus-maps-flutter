// ANGLE-based OpenGL ES context factory implementation for Windows
// Provides zero-copy texture sharing via D3D11 shared handles

#ifdef _WIN32

#include "agus_angle_context_factory.hpp"
#include "base/assert.hpp"
#include "base/logging.hpp"

#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>

#include "agus_env_utils.hpp"

// ANGLE-specific EGL extensions
#ifndef EGL_D3D11_DEVICE_ANGLE
#define EGL_D3D11_DEVICE_ANGLE 0x33A1
#endif

#ifndef EGL_D3D_TEXTURE_ANGLE
#define EGL_D3D_TEXTURE_ANGLE 0x33A3
#endif

#ifndef EGL_PLATFORM_ANGLE_ANGLE
#define EGL_PLATFORM_ANGLE_ANGLE 0x3202
#endif

#ifndef EGL_PLATFORM_ANGLE_TYPE_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_ANGLE 0x3203
#endif

#ifndef EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE
#define EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE 0x3208
#endif

// Function pointer types for EGL extensions
typedef EGLDisplay (EGLAPIENTRY *PFNEGLGETPLATFORMDISPLAYEXTPROC)(EGLenum platform, void *native_display, const EGLint *attrib_list);

// EGLAttrib is a pointer-sized integer type (EGL 1.5).
// If the system EGL headers are older than 1.5, define it locally.
#ifndef EGL_VERSION_1_5
typedef intptr_t EGLAttrib;
#endif

typedef EGLDisplay (EGLAPIENTRY *PFNEGLGETPLATFORMDISPLAYPROC)(EGLenum platform, void *native_display, const EGLAttrib *attrib_list);

namespace agus
{

namespace
{
void DebugEglError(char const * where)
{
    EGLint const err = eglGetError();
    if (err == EGL_SUCCESS)
        return;

    char buf[256];
    std::snprintf(buf, sizeof(buf), "[AgusAngleContextFactory] %s: EGL error 0x%04X (%d)\n", where, err, err);
    OutputDebugStringA(buf);
    LOG(LERROR, ("[AgusAngleContextFactory]", where, "EGL error", std::hex, err));
}
}  // namespace

static Microsoft::WRL::ComPtr<IDXGIAdapter> g_preferredDxgiAdapter;

// --- AgusAngleContext ---

AgusAngleContext::AgusAngleContext(EGLDisplay display, EGLSurface surface, EGLConfig config, AgusAngleContext * contextToShareWith, ID3D11Device * d3dDevice)
    : m_nativeContext(EGL_NO_CONTEXT)
    , m_surface(surface)
    , m_display(display)
    , m_presentAvailable(true)
{
    if (d3dDevice)
        m_d3dDevice = d3dDevice;

    EGLint contextAttribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,  // OpenGL ES 3.0
        EGL_NONE
    };

    EGLContext sharedContext = (contextToShareWith == nullptr) ? EGL_NO_CONTEXT : contextToShareWith->m_nativeContext;
    m_nativeContext = eglCreateContext(m_display, config, sharedContext, contextAttribs);

    if (m_nativeContext == EGL_NO_CONTEXT) {
        EGLint error = eglGetError();
        LOG(LERROR, ("eglCreateContext failed with error:", std::hex, error));
    } else {
        LOG(LINFO, ("AgusAngleContext created: context=", m_nativeContext, "surface=", m_surface));
    }
}

AgusAngleContext::~AgusAngleContext()
{
    if (m_nativeContext != EGL_NO_CONTEXT) {
        eglDestroyContext(m_display, m_nativeContext);
    }
}

void AgusAngleContext::MakeCurrent()
{
    if (m_surface != EGL_NO_SURFACE) {
        OutputDebugStringA(("[AgusAngleContext] MakeCurrent called for context: " + 
            std::to_string(reinterpret_cast<uintptr_t>(m_nativeContext)) + "\n").c_str());
        EGLBoolean result = eglMakeCurrent(m_display, m_surface, m_surface, m_nativeContext);
        if (result != EGL_TRUE) {
            EGLint error = eglGetError();
            LOG(LERROR, ("eglMakeCurrent failed with error:", std::hex, error));
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[AgusAngleContext] eglMakeCurrent FAILED: 0x%04X (%d)\n", error, error);
            OutputDebugStringA(buf);
        } else {
            OutputDebugStringA("[AgusAngleContext] eglMakeCurrent SUCCESS\n");
            if (IsAgusVerboseEnabled())
            {
                EGLContext const cur = eglGetCurrentContext();
                EGLSurface const curDraw = eglGetCurrentSurface(EGL_DRAW);
                EGLSurface const curRead = eglGetCurrentSurface(EGL_READ);
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "[AgusAngleContext] currentContext=%p drawSurface=%p readSurface=%p\n",
                              cur, curDraw, curRead);
                OutputDebugStringA(buf);

                GLenum const glErr = glGetError();
                if (glErr != GL_NO_ERROR)
                {
                    std::snprintf(buf, sizeof(buf), "[AgusAngleContext] glGetError after MakeCurrent: 0x%04X (%u)\n",
                                  static_cast<unsigned int>(glErr), static_cast<unsigned int>(glErr));
                    OutputDebugStringA(buf);
                }

                if (m_d3dDevice)
                {
                    HRESULT const removed = m_d3dDevice->GetDeviceRemovedReason();
                    if (removed != S_OK)
                    {
                        std::snprintf(buf, sizeof(buf), "[AgusAngleContext] D3D device removed reason: 0x%08lX\n",
                                      static_cast<unsigned long>(removed));
                        OutputDebugStringA(buf);
                    }
                }
            }
        }
    } else {
        LOG(LWARNING, ("MakeCurrent called but m_surface is EGL_NO_SURFACE"));
        OutputDebugStringA("[AgusAngleContext] WARNING: MakeCurrent called but m_surface is EGL_NO_SURFACE\n");
    }
}

void AgusAngleContext::DoneCurrent()
{
    eglMakeCurrent(m_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

void AgusAngleContext::Present()
{
    if (m_presentAvailable && m_surface != EGL_NO_SURFACE) {
        // Ensure all GL commands are executed before swapping/presenting
        // This is crucial for shared textures to ensure the consumer (Flutter) sees the update
        glFinish();
        
        // For Pbuffer surfaces backed by D3D textures, eglSwapBuffers might not be needed
        // or might cause issues if the context is not double buffered.
        // We rely on glFinish() to ensure the D3D texture is updated.
        /*
        if (!eglSwapBuffers(m_display, m_surface)) {
            EGLint error = eglGetError();
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[AgusAngleContext] eglSwapBuffers failed: 0x%04X\n", error);
            OutputDebugStringA(buf);
            LOG(LERROR, ("eglSwapBuffers failed:", std::hex, error));
        }
        */
    }
}

void AgusAngleContext::SetFramebuffer(ref_ptr<dp::BaseFramebuffer> framebuffer)
{
    if (framebuffer)
        framebuffer->Bind();
    else
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void AgusAngleContext::SetRenderingEnabled(bool enabled)
{
    if (enabled)
        MakeCurrent();
    else
        DoneCurrent();
}

void AgusAngleContext::SetPresentAvailable(bool available)
{
    m_presentAvailable = available;
}

bool AgusAngleContext::Validate()
{
    return m_presentAvailable && eglGetCurrentContext() != EGL_NO_CONTEXT;
}

void AgusAngleContext::SetSurface(EGLSurface surface)
{
    m_surface = surface;
}

void AgusAngleContext::ResetSurface()
{
    m_surface = EGL_NO_SURFACE;
}

void AgusAngleContext::ClearCurrent()
{
    DoneCurrent();
}


// --- AgusAngleContextFactory ---

AgusAngleContextFactory::AgusAngleContextFactory(int width, int height)
    : m_width(width)
    , m_height(height)
{
    OutputDebugStringA("[AgusAngleContextFactory] Initializing ANGLE context factory\n");

    if (g_preferredDxgiAdapter)
        m_dxgiAdapter = g_preferredDxgiAdapter;

    if (!InitializeD3D11()) {
        OutputDebugStringA("[AgusAngleContextFactory] Failed to initialize D3D11\n");
        return;
    }

    if (!InitializeANGLE()) {
        OutputDebugStringA("[AgusAngleContextFactory] Failed to initialize ANGLE\n");
        return;
    }

    if (!CreateSharedTexture(width, height)) {
        OutputDebugStringA("[AgusAngleContextFactory] Failed to create shared texture\n");
        return;
    }

    if (!CreatePbufferSurface()) {
        OutputDebugStringA("[AgusAngleContextFactory] Failed to create Pbuffer surface\n");
        return;
    }

    m_isValid = true;
    OutputDebugStringA("[AgusAngleContextFactory] Initialization complete\n");
}

void AgusAngleContextFactory::SetPreferredDxgiAdapter(IDXGIAdapter * adapter)
{
    g_preferredDxgiAdapter.Reset();
    if (!adapter)
        return;

    g_preferredDxgiAdapter = adapter;

    if (IsAgusVerboseEnabled())
    {
        DXGI_ADAPTER_DESC desc;
        if (SUCCEEDED(adapter->GetDesc(&desc)))
        {
            char name[256] = {};
            WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
            char buf[512];
            std::snprintf(buf, sizeof(buf),
                          "[AgusAngleContextFactory] Preferred DXGI adapter set: %s (VendorId=0x%04X DeviceId=0x%04X)\n",
                          name, desc.VendorId, desc.DeviceId);
            OutputDebugStringA(buf);
        }
    }
}

AgusAngleContextFactory::~AgusAngleContextFactory()
{
    OutputDebugStringA("[AgusAngleContextFactory] Destroying context factory\n");

    if (m_drawContext) {
        delete m_drawContext;
        m_drawContext = nullptr;
    }

    if (m_uploadContext) {
        delete m_uploadContext;
        m_uploadContext = nullptr;
    }

    if (m_pbufferSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglDisplay, m_pbufferSurface);
        m_pbufferSurface = EGL_NO_SURFACE;
    }

    if (m_eglDisplay != EGL_NO_DISPLAY) {
        eglTerminate(m_eglDisplay);
        m_eglDisplay = EGL_NO_DISPLAY;
    }

    // D3D11 resources are released automatically via ComPtr
}

bool AgusAngleContextFactory::InitializeD3D11()
{
    OutputDebugStringA("[AgusAngleContextFactory] Initializing D3D11\n");

    D3D_FEATURE_LEVEL featureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
    };

    UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef DEBUG
    creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL actualFeatureLevel;
    ID3D11Device * deviceRaw = nullptr;
    ID3D11DeviceContext * contextRaw = nullptr;

    // If Flutter provides an adapter, we must create the device on the same adapter,
    // otherwise cross-adapter shared handles can cause device removal/context loss.
    IDXGIAdapter * adapter = m_dxgiAdapter.Get();
    D3D_DRIVER_TYPE driverType = adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE;

    HRESULT hr = D3D11CreateDevice(
        adapter,                    // Preferred adapter (or nullptr)
        driverType,
        nullptr,                    // No software rasterizer
        creationFlags,
        featureLevels,
        ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION,
        &deviceRaw,
        &actualFeatureLevel,
        &contextRaw
    );

    if (FAILED(hr)) {
        OutputDebugStringA("[AgusAngleContextFactory] D3D11CreateDevice failed\n");
        return false;
    }

    m_d3dDevice.Attach(deviceRaw);
    m_d3dContext.Attach(contextRaw);

    // Enable multi-threaded D3D11 access since CoMaps uses separate upload and draw threads
    // Without this, concurrent D3D11 access can cause DXGI_ERROR_DEVICE_REMOVED (0x887A0005)
    Microsoft::WRL::ComPtr<ID3D11Multithread> multithread;
    hr = m_d3dDevice.As(&multithread);
    if (SUCCEEDED(hr) && multithread) {
        multithread->SetMultithreadProtected(TRUE);
        OutputDebugStringA("[AgusAngleContextFactory] D3D11 multi-thread protection enabled\n");
    } else {
        OutputDebugStringA("[AgusAngleContextFactory] WARNING: Could not enable D3D11 multi-thread protection\n");
    }

    if (IsAgusVerboseEnabled())
    {
        HRESULT const removed = m_d3dDevice->GetDeviceRemovedReason();
        if (removed != S_OK)
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "[AgusAngleContextFactory] WARNING: DeviceRemovedReason at init: 0x%08lX\n",
                          static_cast<unsigned long>(removed));
            OutputDebugStringA(buf);
        }
    }

    OutputDebugStringA("[AgusAngleContextFactory] D3D11 initialized successfully\n");
    return true;
}

bool AgusAngleContextFactory::InitializeANGLE()
{
    OutputDebugStringA("[AgusAngleContextFactory] Initializing ANGLE EGL\n");

    auto eglGetPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYPROC>(
        eglGetProcAddress("eglGetPlatformDisplay"));
    auto eglGetPlatformDisplayEXT = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (eglGetPlatformDisplay)
    {
        EGLAttrib displayAttribs[] = {
            EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
            EGL_D3D11_DEVICE_ANGLE, reinterpret_cast<EGLAttrib>(m_d3dDevice.Get()),
            EGL_NONE};
        m_eglDisplay = eglGetPlatformDisplay(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);

        if (IsAgusVerboseEnabled())
        {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "[AgusAngleContextFactory] Using eglGetPlatformDisplay (EGL 1.5). sizeof(void*)=%zu d3dDevice=%p\n",
                          sizeof(void *), m_d3dDevice.Get());
            OutputDebugStringA(buf);
        }
    }
    else if (eglGetPlatformDisplayEXT)
    {
        // EXT version uses EGLint list. Passing pointers is only safe when EGLint is pointer-sized.
        if (sizeof(void *) <= sizeof(EGLint))
        {
            EGLint displayAttribs[] = {
                EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
                EGL_D3D11_DEVICE_ANGLE, static_cast<EGLint>(reinterpret_cast<intptr_t>(m_d3dDevice.Get())),
                EGL_NONE};
            m_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
            if (IsAgusVerboseEnabled())
                OutputDebugStringA("[AgusAngleContextFactory] Using eglGetPlatformDisplayEXT with EGL_D3D11_DEVICE_ANGLE\n");
        }
        else
        {
            EGLint displayAttribs[] = {EGL_PLATFORM_ANGLE_TYPE_ANGLE, EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE, EGL_NONE};
            m_eglDisplay = eglGetPlatformDisplayEXT(EGL_PLATFORM_ANGLE_ANGLE, EGL_DEFAULT_DISPLAY, displayAttribs);
            if (IsAgusVerboseEnabled())
                OutputDebugStringA(
                    "[AgusAngleContextFactory] Using eglGetPlatformDisplayEXT without EGL_D3D11_DEVICE_ANGLE (avoid pointer truncation)\n");
        }
    }
    else
    {
        m_eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (IsAgusVerboseEnabled())
            OutputDebugStringA("[AgusAngleContextFactory] Using eglGetDisplay fallback\n");
    }

    if (m_eglDisplay == EGL_NO_DISPLAY) {
        OutputDebugStringA("[AgusAngleContextFactory] Failed to get EGL display\n");
        DebugEglError("eglGetPlatformDisplay/eglGetPlatformDisplayEXT/eglGetDisplay");
        return false;
    }

    EGLint majorVersion, minorVersion;
    if (!eglInitialize(m_eglDisplay, &majorVersion, &minorVersion)) {
        OutputDebugStringA("[AgusAngleContextFactory] eglInitialize failed\n");
        DebugEglError("eglInitialize");
        return false;
    }

    OutputDebugStringA(("[AgusAngleContextFactory] EGL version: " + 
        std::to_string(majorVersion) + "." + std::to_string(minorVersion) + "\n").c_str());

    // Choose EGL config
    EGLint configAttribs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8,
        EGL_DEPTH_SIZE, 24,
        EGL_STENCIL_SIZE, 8,
        EGL_NONE
    };

    EGLint numConfigs;
    if (!eglChooseConfig(m_eglDisplay, configAttribs, &m_eglConfig, 1, &numConfigs) || numConfigs == 0) {
        OutputDebugStringA("[AgusAngleContextFactory] eglChooseConfig failed\n");
        DebugEglError("eglChooseConfig");
        return false;
    }

    // Bind OpenGL ES API
    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
        OutputDebugStringA("[AgusAngleContextFactory] eglBindAPI failed\n");
        DebugEglError("eglBindAPI");
        return false;
    }

    if (IsAgusVerboseEnabled())
    {
        char const * vendor = eglQueryString(m_eglDisplay, EGL_VENDOR);
        char const * version = eglQueryString(m_eglDisplay, EGL_VERSION);
        char const * extensions = eglQueryString(m_eglDisplay, EGL_EXTENSIONS);
        OutputDebugStringA("[AgusAngleContextFactory] EGL vendor/version:\n");
        OutputDebugStringA(vendor ? vendor : "(null)");
        OutputDebugStringA("\n");
        OutputDebugStringA(version ? version : "(null)");
        OutputDebugStringA("\n");
        if (extensions)
        {
            OutputDebugStringA("[AgusAngleContextFactory] EGL extensions: ");
            OutputDebugStringA(extensions);
            OutputDebugStringA("\n");
        }
    }

    OutputDebugStringA("[AgusAngleContextFactory] ANGLE EGL initialized successfully\n");
    return true;
}

bool AgusAngleContextFactory::CreateSharedTexture(int width, int height)
{
    OutputDebugStringA(("[AgusAngleContextFactory] Creating shared texture " + 
        std::to_string(width) + "x" + std::to_string(height) + "\n").c_str());

    // Release existing texture if any
    m_sharedTexture.Reset();
    if (m_sharedHandle) {
        CloseHandle(m_sharedHandle);
        m_sharedHandle = nullptr;
    }

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;  // Try RGBA to match EGL_TEXTURE_RGBA
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    // Use standard SHARED for broader compatibility with Flutter's texture registrar
    // KEYEDMUTEX requires explicit Acquire/Release sync which Flutter's simple shared handle path might not do
    desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

    HRESULT hr = m_d3dDevice->CreateTexture2D(&desc, nullptr, &m_sharedTexture);
    if (FAILED(hr)) {
        OutputDebugStringA("[AgusAngleContextFactory] CreateTexture2D failed\n");
        return false;
    }

    // Flush to ensure the texture is created on the GPU before we try to open it elsewhere
    if (m_d3dContext) {
        m_d3dContext->Flush();
    }

    // Get shared handle for Flutter
    ComPtr<IDXGIResource> dxgiResource;
    hr = m_sharedTexture.As(&dxgiResource);
    if (FAILED(hr)) {
        OutputDebugStringA("[AgusAngleContextFactory] QueryInterface for IDXGIResource failed\n");
        return false;
    }

    hr = dxgiResource->GetSharedHandle(&m_sharedHandle);
    if (FAILED(hr)) {
        OutputDebugStringA("[AgusAngleContextFactory] GetSharedHandle failed\n");
        return false;
    }

    OutputDebugStringA("[AgusAngleContextFactory] Shared texture created successfully\n");
    return true;
}

bool AgusAngleContextFactory::CreatePbufferSurface()
{
    OutputDebugStringA("[AgusAngleContextFactory] Creating Pbuffer surface\n");

    // Destroy existing surface
    if (m_pbufferSurface != EGL_NO_SURFACE) {
        eglDestroySurface(m_eglDisplay, m_pbufferSurface);
        m_pbufferSurface = EGL_NO_SURFACE;
    }

    // Try to create Pbuffer from D3D11 texture (ANGLE extension)
    // Note: This may not be available in all ANGLE versions
    auto eglCreatePbufferFromClientBuffer = reinterpret_cast<PFNEGLCREATEPBUFFERFROMCLIENTBUFFERPROC>(
        eglGetProcAddress("eglCreatePbufferFromClientBuffer"));

    if (eglCreatePbufferFromClientBuffer && m_sharedTexture) {
        EGLint pbufferAttribs[] = {
            EGL_WIDTH, m_width,
            EGL_HEIGHT, m_height,
            EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
            EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
            EGL_MIPMAP_TEXTURE, EGL_FALSE,
            EGL_NONE
        };

        m_pbufferSurface = eglCreatePbufferFromClientBuffer(
            m_eglDisplay,
            EGL_D3D_TEXTURE_ANGLE,
            reinterpret_cast<EGLClientBuffer>(m_sharedTexture.Get()),
            m_eglConfig,
            pbufferAttribs
        );

        if (m_pbufferSurface != EGL_NO_SURFACE) {
            OutputDebugStringA("[AgusAngleContextFactory] Created Pbuffer from D3D11 texture\n");
            return true;
        }

        EGLint error = eglGetError();
        OutputDebugStringA(("[AgusAngleContextFactory] eglCreatePbufferFromClientBuffer failed: 0x" + 
            std::to_string(error) + "\n").c_str());
    }

    // Fallback: Create standard Pbuffer (will require texture copy)
    OutputDebugStringA("[AgusAngleContextFactory] Falling back to standard Pbuffer\n");

    EGLint pbufferAttribs[] = {
        EGL_WIDTH, m_width,
        EGL_HEIGHT, m_height,
        EGL_TEXTURE_FORMAT, EGL_TEXTURE_RGBA,
        EGL_TEXTURE_TARGET, EGL_TEXTURE_2D,
        EGL_NONE
    };

    m_pbufferSurface = eglCreatePbufferSurface(m_eglDisplay, m_eglConfig, pbufferAttribs);
    if (m_pbufferSurface == EGL_NO_SURFACE) {
        EGLint error = eglGetError();
        OutputDebugStringA(("[AgusAngleContextFactory] eglCreatePbufferSurface failed: 0x" + 
            std::to_string(error) + "\n").c_str());
        return false;
    }

    OutputDebugStringA("[AgusAngleContextFactory] Pbuffer surface created successfully\n");
    return true;
}

bool AgusAngleContextFactory::IsValid() const
{
    return m_isValid;
}

dp::GraphicsContext * AgusAngleContextFactory::GetDrawContext()
{
    if (!m_drawContext && m_eglDisplay != EGL_NO_DISPLAY) {
        // Create upload context first (if not exists) to share with draw context
        GetResourcesUploadContext();
        
        m_drawContext = new AgusAngleContext(m_eglDisplay, m_pbufferSurface, m_eglConfig, m_uploadContext, m_d3dDevice.Get());
        OutputDebugStringA("[AgusAngleContextFactory] Draw context created\n");
    }
    return m_drawContext;
}

dp::GraphicsContext * AgusAngleContextFactory::GetResourcesUploadContext()
{
    if (!m_uploadContext && m_eglDisplay != EGL_NO_DISPLAY) {
        // Upload context uses a simple Pbuffer (doesn't need shared texture)
        EGLint uploadPbufferAttribs[] = {
            EGL_WIDTH, 1,
            EGL_HEIGHT, 1,
            EGL_NONE
        };
        EGLSurface uploadSurface = eglCreatePbufferSurface(m_eglDisplay, m_eglConfig, uploadPbufferAttribs);
        
        m_uploadContext = new AgusAngleContext(m_eglDisplay, uploadSurface, m_eglConfig, nullptr, m_d3dDevice.Get());
        OutputDebugStringA("[AgusAngleContextFactory] Upload context created\n");
    }
    return m_uploadContext;
}

bool AgusAngleContextFactory::IsDrawContextCreated() const
{
    return m_drawContext != nullptr;
}

bool AgusAngleContextFactory::IsUploadContextCreated() const
{
    return m_uploadContext != nullptr;
}

void AgusAngleContextFactory::WaitForInitialization(dp::GraphicsContext * context)
{
    std::unique_lock<std::mutex> lock(m_initializationMutex);
    if (!m_isInitialized) {
        ++m_initializationCounter;
        if (m_initializationCounter >= 2) {
            m_isInitialized = true;
            m_initializationCondition.notify_all();
        } else {
            m_initializationCondition.wait(lock, [this] { return m_isInitialized; });
        }
    }
}

void AgusAngleContextFactory::SetPresentAvailable(bool available)
{
    if (m_drawContext) {
        m_drawContext->SetPresentAvailable(available);
    }
}

void AgusAngleContextFactory::Resize(int width, int height)
{
    if (width == m_width && height == m_height) {
        return;
    }

    OutputDebugStringA(("[AgusAngleContextFactory] Resizing to " + 
        std::to_string(width) + "x" + std::to_string(height) + "\n").c_str());

    m_width = width;
    m_height = height;

    // Recreate shared texture and surface
    if (!CreateSharedTexture(width, height)) {
        OutputDebugStringA("[AgusAngleContextFactory] Resize: Failed to create shared texture\n");
        return;
    }

    if (!CreatePbufferSurface()) {
        OutputDebugStringA("[AgusAngleContextFactory] Resize: Failed to create Pbuffer surface\n");
        return;
    }

    // Update draw context surface
    if (m_drawContext) {
        m_drawContext->SetSurface(m_pbufferSurface);
    }
}

} // namespace agus

#endif // _WIN32
