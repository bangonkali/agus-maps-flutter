#pragma once

// ANGLE-based OpenGL ES context factory for Windows
// Uses D3D11 shared textures for zero-copy rendering with Flutter

#ifdef _WIN32

#include "drape/gl_includes.hpp"
#include "drape/oglcontext.hpp"
#include "drape/graphics_context_factory.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi.h>
#include <wrl/client.h>
#include <atomic>
#include <condition_variable>
#include <mutex>

using Microsoft::WRL::ComPtr;

namespace agus
{
    /// OpenGL ES context wrapper for ANGLE on Windows
    class AgusAngleContext : public dp::OGLContext
    {
    public:
        AgusAngleContext(EGLDisplay display, EGLSurface surface, EGLConfig config, AgusAngleContext * contextToShareWith, ID3D11Device * d3dDevice);
        ~AgusAngleContext();

        void MakeCurrent() override;
        void DoneCurrent() override;
        void Present() override;
        void SetFramebuffer(ref_ptr<dp::BaseFramebuffer> framebuffer) override;
        void SetRenderingEnabled(bool enabled) override;
        void SetPresentAvailable(bool available) override;
        bool Validate() override;

        void SetSurface(EGLSurface surface);
        void ResetSurface();
        void ClearCurrent();

    private:
        EGLContext m_nativeContext;
        EGLSurface m_surface;
        EGLDisplay m_display;
        std::atomic<bool> m_presentAvailable;
        Microsoft::WRL::ComPtr<ID3D11Device> m_d3dDevice;
    };

    /// Graphics context factory using ANGLE (OpenGL ES -> DirectX 11)
    /// Manages D3D11 shared textures for zero-copy Flutter integration
    class AgusAngleContextFactory : public dp::GraphicsContextFactory
    {
    public:
        /// Create factory with specified render target size
        AgusAngleContextFactory(int width, int height);
        ~AgusAngleContextFactory();

        /// Optionally pin the D3D device to a specific DXGI adapter (e.g. Flutter's).
        /// NOTE: Must be set before the factory is constructed/initialized.
        static void SetPreferredDxgiAdapter(IDXGIAdapter * adapter);

        /// Check if factory was initialized successfully
        bool IsValid() const;

        // dp::GraphicsContextFactory interface
        dp::GraphicsContext * GetDrawContext() override;
        dp::GraphicsContext * GetResourcesUploadContext() override;
        bool IsDrawContextCreated() const override;
        bool IsUploadContextCreated() const override;
        void WaitForInitialization(dp::GraphicsContext * context) override;
        void SetPresentAvailable(bool available) override;

        /// Update render target size (recreates surfaces)
        void Resize(int width, int height);

        /// Get the D3D11 shared texture handle for Flutter integration
        HANDLE GetSharedTextureHandle() const { return m_sharedHandle; }

        /// Get D3D11 device (for external texture creation if needed)
        ID3D11Device* GetD3D11Device() const { return m_d3dDevice.Get(); }

        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }

    private:
        bool InitializeD3D11();
        bool InitializeANGLE();
        bool CreateSharedTexture(int width, int height);
        bool CreatePbufferSurface();

        // D3D11 resources
        ComPtr<ID3D11Device> m_d3dDevice;
        ComPtr<ID3D11DeviceContext> m_d3dContext;
        ComPtr<ID3D11Texture2D> m_sharedTexture;
        HANDLE m_sharedHandle = nullptr;

        // Preferred adapter (if provided by host/Flutter). If null, uses default adapter.
        ComPtr<IDXGIAdapter> m_dxgiAdapter;

        // EGL/ANGLE resources
        EGLDisplay m_eglDisplay = EGL_NO_DISPLAY;
        EGLConfig m_eglConfig = nullptr;
        EGLSurface m_pbufferSurface = EGL_NO_SURFACE;

        // Contexts
        AgusAngleContext * m_drawContext = nullptr;
        AgusAngleContext * m_uploadContext = nullptr;

        // Dimensions
        int m_width = 0;
        int m_height = 0;

        // State
        bool m_isValid = false;
        bool m_isInitialized = false;
        size_t m_initializationCounter = 0;
        std::condition_variable m_initializationCondition;
        std::mutex m_initializationMutex;
    };

} // namespace agus

#endif // _WIN32
