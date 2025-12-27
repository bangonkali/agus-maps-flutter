#include "agus_maps_flutter_plugin.h"

#include <windows.h>
#include <shlobj.h>
#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/standard_method_codec.h>
#include <flutter/texture_registrar.h>

#include <dxgi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

// FFI functions from our native library
extern "C" {
    __declspec(dllimport) void comaps_init_paths(const char* resourcePath, const char* writablePath);
    __declspec(dllimport) void comaps_set_frame_callback(void (*callback)());
    __declspec(dllimport) void comaps_set_dxgi_adapter(void* adapter);
    __declspec(dllimport) int comaps_create_surface(int width, int height, float density);
    __declspec(dllimport) void* comaps_get_shared_handle();
    __declspec(dllimport) void comaps_resize_surface(int width, int height);
    __declspec(dllimport) void comaps_destroy_surface();
}

namespace agus_maps_flutter {

// Static plugin instance for callbacks
static AgusMapsFlutterPlugin* g_plugin_instance = nullptr;

// Callback function for frame notifications
static void OnFrameReady() {
    if (g_plugin_instance) {
        g_plugin_instance->NotifyFrameReady();
    }
}

void AgusMapsFlutterPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrarWindows *registrar) {
    auto texture_registrar = registrar->texture_registrar();
    auto plugin = std::make_unique<AgusMapsFlutterPlugin>(registrar, texture_registrar);

    auto channel =
        std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
            registrar->messenger(), "agus_maps_flutter",
            &flutter::StandardMethodCodec::GetInstance());

    channel->SetMethodCallHandler(
        [plugin_pointer = plugin.get()](const auto &call, auto result) {
            plugin_pointer->HandleMethodCall(call, std::move(result));
        });

    plugin->channel_ = std::move(channel);
    g_plugin_instance = plugin.get();
    
    registrar->AddPlugin(std::move(plugin));
}

AgusMapsFlutterPlugin::AgusMapsFlutterPlugin(
    flutter::PluginRegistrarWindows *registrar,
    flutter::TextureRegistrar *texture_registrar)
    : registrar_(registrar), texture_registrar_(texture_registrar) {
    
    // Get application data path
    app_data_path_ = GetAppDataPath();
    
    // Get assets path
    assets_path_ = GetAssetsPath();
    
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Initialized\n"));
    OutputDebugStringA(("[AgusMapsFlutterPlugin] App data: " + app_data_path_ + "\n").c_str());
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Assets: " + assets_path_ + "\n").c_str());
    
    // Set frame callback
    comaps_set_frame_callback(&OnFrameReady);
}

AgusMapsFlutterPlugin::~AgusMapsFlutterPlugin() {
    OutputDebugStringA("[AgusMapsFlutterPlugin] Destroying plugin\n");
    
    g_plugin_instance = nullptr;
    
    // Cleanup rendering surface
    comaps_destroy_surface();
    
    // Unregister texture
    if (texture_id_ >= 0 && texture_registrar_) {
        texture_registrar_->UnregisterTexture(texture_id_);
        texture_id_ = -1;
    }
}

void AgusMapsFlutterPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue> &method_call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
    
    const auto& method_name = method_call.method_name();
    
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Method call: " + method_name + "\n").c_str());
    
    if (method_name == "extractDataFiles") {
        try {
            std::string path = ExtractDataFiles();
            result->Success(flutter::EncodableValue(path));
        } catch (const std::exception& e) {
            result->Error("EXTRACT_ERROR", e.what());
        }
    }
    else if (method_name == "extractMap") {
        const auto* arguments = std::get_if<flutter::EncodableMap>(method_call.arguments());
        if (arguments) {
            auto it = arguments->find(flutter::EncodableValue("assetPath"));
            if (it != arguments->end()) {
                const auto* asset_path = std::get_if<std::string>(&it->second);
                if (asset_path) {
                    try {
                        std::string path = ExtractMap(*asset_path);
                        result->Success(flutter::EncodableValue(path));
                        return;
                    } catch (const std::exception& e) {
                        result->Error("EXTRACT_ERROR", e.what());
                        return;
                    }
                }
            }
        }
        result->Error("INVALID_ARGUMENTS", "Missing assetPath argument");
    }
    else if (method_name == "getApkPath") {
        // On Windows, return the assets path
        result->Success(flutter::EncodableValue(assets_path_));
    }
    else if (method_name == "createSurface" || method_name == "createMapSurface") {
        const auto* arguments = std::get_if<flutter::EncodableMap>(method_call.arguments());
        if (arguments) {
            int width = 800;
            int height = 600;
            double density = 1.0;
            
            auto it_w = arguments->find(flutter::EncodableValue("width"));
            if (it_w != arguments->end()) {
                if (const auto* val = std::get_if<int32_t>(&it_w->second)) {
                    width = *val;
                }
            }
            
            auto it_h = arguments->find(flutter::EncodableValue("height"));
            if (it_h != arguments->end()) {
                if (const auto* val = std::get_if<int32_t>(&it_h->second)) {
                    height = *val;
                }
            }
            
            auto it_d = arguments->find(flutter::EncodableValue("density"));
            if (it_d != arguments->end()) {
                if (const auto* val = std::get_if<double>(&it_d->second)) {
                    density = *val;
                }
            }
            
            CreateSurface(width, height, static_cast<float>(density));
            result->Success(flutter::EncodableValue(texture_id_));
        } else {
            result->Error("INVALID_ARGUMENTS", "Missing surface arguments");
        }
    }
    else if (method_name == "resizeSurface") {
        const auto* arguments = std::get_if<flutter::EncodableMap>(method_call.arguments());
        if (arguments) {
            int width = 800;
            int height = 600;
            
            auto it_w = arguments->find(flutter::EncodableValue("width"));
            if (it_w != arguments->end()) {
                if (const auto* val = std::get_if<int32_t>(&it_w->second)) {
                    width = *val;
                }
            }
            
            auto it_h = arguments->find(flutter::EncodableValue("height"));
            if (it_h != arguments->end()) {
                if (const auto* val = std::get_if<int32_t>(&it_h->second)) {
                    height = *val;
                }
            }
            
            comaps_resize_surface(width, height);
            result->Success();
        } else {
            result->Error("INVALID_ARGUMENTS", "Missing resize arguments");
        }
    }
    else if (method_name == "destroySurface") {
        comaps_destroy_surface();
        if (texture_id_ >= 0 && texture_registrar_) {
            texture_registrar_->UnregisterTexture(texture_id_);
            texture_id_ = -1;
        }
        result->Success();
    }
    else {
        result->NotImplemented();
    }
}

std::string AgusMapsFlutterPlugin::GetAppDataPath() {
    wchar_t* path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &path))) {
        std::wstring wpath(path);
        CoTaskMemFree(path);
        
        // Convert wide string to UTF-8
        int size = WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, nullptr, 0, nullptr, nullptr);
        std::string result(size - 1, 0);
        WideCharToMultiByte(CP_UTF8, 0, wpath.c_str(), -1, &result[0], size, nullptr, nullptr);
        
        // Append app-specific directory
        result += "\\agus_maps_flutter";
        
        // Create directory if it doesn't exist
        std::filesystem::create_directories(result);
        
        return result;
    }
    
    // Fallback to temp directory
    return std::filesystem::temp_directory_path().string() + "\\agus_maps_flutter";
}

std::string AgusMapsFlutterPlugin::GetAssetsPath() {
    // Get executable path
    wchar_t exe_path[MAX_PATH];
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    
    std::filesystem::path exe_dir = std::filesystem::path(exe_path).parent_path();
    // Flutter assets are at data/flutter_assets/
    // The asset paths from Dart already include "assets/" prefix (e.g., "assets/maps/World.mwm")
    std::filesystem::path assets_path = exe_dir / "data" / "flutter_assets";
    
    return assets_path.string();
}

std::string AgusMapsFlutterPlugin::ExtractDataFiles() {
    OutputDebugStringA("[AgusMapsFlutterPlugin] ExtractDataFiles called\n");
    
    // Flutter bundles assets at data/flutter_assets/assets/
    // So "assets/comaps_data" in pubspec.yaml becomes "flutter_assets/assets/comaps_data" on disk
    std::filesystem::path source_dir = std::filesystem::path(assets_path_) / "assets" / "comaps_data";
    std::filesystem::path dest_dir = std::filesystem::path(app_data_path_) / "comaps_data";
    
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Source: " + source_dir.string() + "\n").c_str());
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Dest: " + dest_dir.string() + "\n").c_str());
    
    // Create destination directory
    std::filesystem::create_directories(dest_dir);
    
    // Copy files if source exists
    if (std::filesystem::exists(source_dir)) {
        std::filesystem::copy(source_dir, dest_dir, 
            std::filesystem::copy_options::recursive | 
            std::filesystem::copy_options::overwrite_existing);
        OutputDebugStringA("[AgusMapsFlutterPlugin] Data files copied\n");
    } else {
        OutputDebugStringA("[AgusMapsFlutterPlugin] Source directory doesn't exist, skipping copy\n");
        OutputDebugStringA(("[AgusMapsFlutterPlugin] Looking for: " + source_dir.string() + "\n").c_str());
    }
    
    // Initialize CoMaps with paths
    std::string resource_path = dest_dir.string();
    std::string writable_path = app_data_path_;
    
    comaps_init_paths(resource_path.c_str(), writable_path.c_str());
    
    return resource_path;
}

std::string AgusMapsFlutterPlugin::ExtractMap(const std::string& asset_path) {
    OutputDebugStringA(("[AgusMapsFlutterPlugin] ExtractMap: " + asset_path + "\n").c_str());
    
    // Normalize the asset path - replace forward slashes with backslashes for Windows
    std::string normalized_asset_path = asset_path;
    std::replace(normalized_asset_path.begin(), normalized_asset_path.end(), '/', '\\');
    
    std::filesystem::path source_path = std::filesystem::path(assets_path_) / normalized_asset_path;
    std::filesystem::path dest_path = std::filesystem::path(app_data_path_) / normalized_asset_path;
    
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Source path: " + source_path.string() + "\n").c_str());
    OutputDebugStringA(("[AgusMapsFlutterPlugin] Dest path: " + dest_path.string() + "\n").c_str());
    
    // Create destination directory
    std::filesystem::create_directories(dest_path.parent_path());
    
    // Copy file if source exists and destination doesn't (or is older)
    if (std::filesystem::exists(source_path)) {
        if (!std::filesystem::exists(dest_path) || 
            std::filesystem::last_write_time(source_path) > std::filesystem::last_write_time(dest_path)) {
            std::filesystem::copy_file(source_path, dest_path, 
                std::filesystem::copy_options::overwrite_existing);
            OutputDebugStringA(("[AgusMapsFlutterPlugin] Copied map to: " + dest_path.string() + "\n").c_str());
        } else {
            OutputDebugStringA(("[AgusMapsFlutterPlugin] Map already exists and is up to date: " + dest_path.string() + "\n").c_str());
        }
    } else {
        std::string error_msg = "Source map not found: " + source_path.string();
        OutputDebugStringA(("[AgusMapsFlutterPlugin] ERROR: " + error_msg + "\n").c_str());
        throw std::runtime_error(error_msg);
    }
    
    return dest_path.string();
}

void AgusMapsFlutterPlugin::CreateSurface(int width, int height, float density) {
    std::string log_msg = "[AgusMapsFlutterPlugin] CreateSurface: " + 
        std::to_string(width) + "x" + std::to_string(height) + ", density=" + std::to_string(density) + "\n";
    OutputDebugStringA(log_msg.c_str());
    fprintf(stderr, "%s", log_msg.c_str());
    fflush(stderr);
    
    surface_width_ = width;
    surface_height_ = height;

    // Ensure native D3D device is created on the same adapter as Flutter.
    if (registrar_) {
        auto* view = registrar_->GetView();
        if (view) {
            IDXGIAdapter* adapter = view->GetGraphicsAdapter();
            if (adapter) {
                comaps_set_dxgi_adapter(reinterpret_cast<void*>(adapter));
            }
        }
    }
    
    // Create the native rendering surface
    int result = comaps_create_surface(width, height, density);
    if (result != 0) {
        log_msg = "[AgusMapsFlutterPlugin] Failed to create native surface, result=" + std::to_string(result) + "\n";
        OutputDebugStringA(log_msg.c_str());
        fprintf(stderr, "%s", log_msg.c_str());
        fflush(stderr);
        return;
    }
    
    OutputDebugStringA("[AgusMapsFlutterPlugin] Native surface created successfully\n");
    fprintf(stderr, "[AgusMapsFlutterPlugin] Native surface created successfully\n");
    fflush(stderr);
    
    OutputDebugStringA("[AgusMapsFlutterPlugin] About to call comaps_get_shared_handle()...\n");
    fprintf(stderr, "[AgusMapsFlutterPlugin] About to call comaps_get_shared_handle()...\n");
    fflush(stderr);
    
    // Do not fetch and cache the shared handle here; Flutter may request descriptors
    // asynchronously (including after resize). We'll fetch and duplicate per-callback.
    
    // Register GPU surface texture with Flutter
    if (texture_registrar_) {
        OutputDebugStringA("[AgusMapsFlutterPlugin] Creating GPU surface texture...\n");
        fprintf(stderr, "[AgusMapsFlutterPlugin] Creating GPU surface texture...\n");
        fflush(stderr);
        
        // Create a GPU surface texture descriptor callback
        // Using a static to ensure lifetime - callback captures the static
        flutter::GpuSurfaceTexture::ObtainDescriptorCallback obtain_callback = 
            [this](size_t w, size_t h) -> const FlutterDesktopGpuSurfaceDescriptor* {
                // Log callback invocation
                OutputDebugStringA("[AgusMapsFlutterPlugin] ObtainDescriptor callback invoked\n");
                fprintf(stderr, "[AgusMapsFlutterPlugin] ObtainDescriptor callback invoked\n");
                fflush(stderr);

                void* raw_handle = comaps_get_shared_handle();
                if (!raw_handle) {
                    OutputDebugStringA("[AgusMapsFlutterPlugin] ObtainDescriptor: shared handle is null\n");
                    fprintf(stderr, "[AgusMapsFlutterPlugin] ObtainDescriptor: shared handle is null\n");
                    fflush(stderr);
                    return nullptr;
                }

                // Duplicate the handle for Flutter to open, and close it once Flutter has opened it.
                HANDLE duplicated = nullptr;
                BOOL ok = DuplicateHandle(GetCurrentProcess(),
                                          reinterpret_cast<HANDLE>(raw_handle),
                                          GetCurrentProcess(),
                                          &duplicated,
                                          0,
                                          FALSE,
                                          DUPLICATE_SAME_ACCESS);
                if (!ok || !duplicated) {
                    OutputDebugStringA("[AgusMapsFlutterPlugin] DuplicateHandle failed\n");
                    fprintf(stderr, "[AgusMapsFlutterPlugin] DuplicateHandle failed\n");
                    fflush(stderr);
                    return nullptr;
                }
                
                // Store descriptor in member to ensure it outlives the callback
                gpu_surface_descriptor_.struct_size = sizeof(FlutterDesktopGpuSurfaceDescriptor);
                gpu_surface_descriptor_.handle = duplicated;
                gpu_surface_descriptor_.width = static_cast<size_t>(surface_width_);
                gpu_surface_descriptor_.height = static_cast<size_t>(surface_height_);
                gpu_surface_descriptor_.visible_width = static_cast<size_t>(surface_width_);
                gpu_surface_descriptor_.visible_height = static_cast<size_t>(surface_height_);
                gpu_surface_descriptor_.format = kFlutterDesktopPixelFormatBGRA8888;
                gpu_surface_descriptor_.release_context = duplicated;
                gpu_surface_descriptor_.release_callback = [](void* ctx) {
                    if (ctx) {
                        CloseHandle(reinterpret_cast<HANDLE>(ctx));
                    }
                };
                return &gpu_surface_descriptor_;
            };
        
        OutputDebugStringA("[AgusMapsFlutterPlugin] Creating TextureVariant with GpuSurfaceTexture...\n");
        fprintf(stderr, "[AgusMapsFlutterPlugin] Creating TextureVariant with GpuSurfaceTexture...\n");
        fflush(stderr);
        
        // Create TextureVariant containing a GpuSurfaceTexture directly
        texture_ = std::make_unique<flutter::TextureVariant>(
            flutter::GpuSurfaceTexture(
                kFlutterDesktopGpuSurfaceTypeDxgiSharedHandle,
                obtain_callback
            )
        );
        
        OutputDebugStringA("[AgusMapsFlutterPlugin] Registering texture with Flutter...\n");
        fprintf(stderr, "[AgusMapsFlutterPlugin] Registering texture with Flutter...\n");
        fflush(stderr);
        
        // Register with Flutter
        texture_id_ = texture_registrar_->RegisterTexture(texture_.get());
        
        log_msg = "[AgusMapsFlutterPlugin] Texture registered with ID: " + std::to_string(texture_id_) + "\n";
        OutputDebugStringA(log_msg.c_str());
        fprintf(stderr, "%s", log_msg.c_str());
        fflush(stderr);
        
        // Mark initial frame available
        texture_registrar_->MarkTextureFrameAvailable(texture_id_);
        OutputDebugStringA("[AgusMapsFlutterPlugin] Initial frame marked available\n");
        fprintf(stderr, "[AgusMapsFlutterPlugin] Initial frame marked available\n");
        fflush(stderr);
    } else {
        OutputDebugStringA("[AgusMapsFlutterPlugin] ERROR: No texture registrar available!\n");
        fprintf(stderr, "[AgusMapsFlutterPlugin] ERROR: No texture registrar available!\n");
        fflush(stderr);
        texture_id_ = -1;
    }
}

void AgusMapsFlutterPlugin::NotifyFrameReady() {
    if (texture_id_ >= 0 && texture_registrar_) {
        texture_registrar_->MarkTextureFrameAvailable(texture_id_);
    }
}

}  // namespace agus_maps_flutter
