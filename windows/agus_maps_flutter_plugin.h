#ifndef FLUTTER_PLUGIN_AGUS_MAPS_FLUTTER_PLUGIN_H_
#define FLUTTER_PLUGIN_AGUS_MAPS_FLUTTER_PLUGIN_H_

#include <flutter/method_channel.h>
#include <flutter/plugin_registrar_windows.h>
#include <flutter/texture_registrar.h>
#include <flutter_texture_registrar.h>

#include <memory>
#include <string>

namespace agus_maps_flutter {

class AgusMapsFlutterPlugin : public flutter::Plugin {
 public:
  static void RegisterWithRegistrar(flutter::PluginRegistrarWindows *registrar);

  AgusMapsFlutterPlugin(flutter::PluginRegistrarWindows *registrar,
                        flutter::TextureRegistrar *texture_registrar);

  virtual ~AgusMapsFlutterPlugin();

  // Disallow copy and assign.
  AgusMapsFlutterPlugin(const AgusMapsFlutterPlugin&) = delete;
  AgusMapsFlutterPlugin& operator=(const AgusMapsFlutterPlugin&) = delete;

  // Notify Flutter that a new frame is ready (public for callback access)
  void NotifyFrameReady();

 private:
  // Called when a method is called on this plugin's channel from Dart.
  void HandleMethodCall(
      const flutter::MethodCall<flutter::EncodableValue> &method_call,
      std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

  // Extract a map asset to the filesystem
  std::string ExtractMap(const std::string& asset_path);

  // Extract CoMaps data files
  std::string ExtractDataFiles();

  // Get application data path
  std::string GetAppDataPath();

  // Create the map rendering surface
  void CreateSurface(int width, int height, float density);

  // Get Flutter assets path
  std::string GetAssetsPath();

  flutter::PluginRegistrarWindows* registrar_;
  flutter::TextureRegistrar* texture_registrar_;
  std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> channel_;
  
  // Texture for rendering
  int64_t texture_id_ = -1;
  std::unique_ptr<flutter::TextureVariant> texture_;
  FlutterDesktopGpuSurfaceDescriptor gpu_surface_descriptor_ = {};
  void* shared_handle_ = nullptr;
  
  // Surface dimensions
  int surface_width_ = 0;
  int surface_height_ = 0;
  
  // Paths
  std::string app_data_path_;
  std::string assets_path_;
};

}  // namespace agus_maps_flutter

#endif  // FLUTTER_PLUGIN_AGUS_MAPS_FLUTTER_PLUGIN_H_
