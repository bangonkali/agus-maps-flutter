#include "include/agus_maps_flutter/agus_maps_flutter_plugin_c_api.h"

#include <flutter/plugin_registrar_windows.h>

#include "agus_maps_flutter_plugin.h"

void AgusMapsFlutterPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
  agus_maps_flutter::AgusMapsFlutterPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrarWindows>(registrar));
}
