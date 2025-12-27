# CoMaps Patches Documentation

This document describes all patches applied to the CoMaps codebase for the agus-maps-flutter plugin.

## Patch Regeneration

Patches are generated from modifications in `thirdparty/comaps/` using:
```powershell
./scripts/regenerate_patches.ps1
```

Patches are applied using:
```powershell
./scripts/apply_comaps_patches.ps1
```

## Current Patch Set (45 patches)

| # | Patch File | Target File | Purpose |
|---|------------|-------------|---------|
| 0001 | 3party-freetype-CMakeLists.txt.patch | 3party/freetype/CMakeLists.txt | CMake build fixes |
| 0002 | 3party-icu-CMakeLists.txt.patch | 3party/icu/CMakeLists.txt | ICU build configuration |
| 0003 | 3party-jansson-jansson.patch | 3party/jansson/jansson | Jansson submodule fix |
| 0004 | 3party-jansson-janssonconfig.h.patch | 3party/jansson/jansson_config.h | Jansson config |
| 0005 | 3party-openinghours-rulesevaluation.cpp.patch | 3party/opening_hours/rules_evaluation.cpp | Fix for date handling |
| 0006 | 3party-protobuf-CMakeLists.txt.patch | 3party/protobuf/CMakeLists.txt | Protobuf build config |
| 0007 | 3party-protobuf-protobuf.patch | 3party/protobuf/protobuf | Protobuf submodule fix |
| 0008 | CMakeLists.txt.patch | CMakeLists.txt | Root CMake configuration |
| 0009 | libs-base-logging.cpp.patch | libs/base/logging.cpp | Fix `std::toupper` on non-ASCII chars |
| 0010 | libs-base-stringutils.hpp.patch | libs/base/string_utils.hpp | String utility improvements |
| 0011 | libs-base-threadchecker.cpp.patch | libs/base/thread_checker.cpp | Conditional thread checking (OMIM_DISABLE_THREAD_CHECKER) |
| 0012 | libs-base-threadchecker.hpp.patch | libs/base/thread_checker.hpp | Thread checker header with disable flag |
| 0013 | libs-base-timer.cpp.patch | libs/base/timer.cpp | Fix `std::isdigit` on chars |
| 0014 | libs-drape-dynamictexture.hpp.patch | libs/drape/dynamic_texture.hpp | Texture handling |
| 0015 | libs-drape-framebuffer.hpp.patch | libs/drape/framebuffer.hpp | Framebuffer config |
| 0016 | libs-drape-glfunctions.cpp.patch | libs/drape/gl_functions.cpp | GL function pointer loading |
| 0017 | libs-drape-glincludes.hpp.patch | libs/drape/gl_includes.hpp | GL header includes |
| 0018 | libs-drape-texture.hpp.patch | libs/drape/texture.hpp | Texture handling |
| 0019 | libs-drapefrontend-CMakeLists.txt.patch | libs/drape_frontend/CMakeLists.txt | Add new source files |
| 0020 | libs-drapefrontend-activeframecallback.cpp.patch | libs/drape_frontend/active_frame_callback.cpp | **NEW FILE**: Active frame callback |
| 0021 | libs-drapefrontend-activeframecallback.hpp.patch | libs/drape_frontend/active_frame_callback.hpp | **NEW FILE**: Active frame callback header |
| 0022 | libs-drapefrontend-frontendrenderer.cpp.patch | libs/drape_frontend/frontend_renderer.cpp | Call active frame callback |
| 0023 | libs-indexer-categoriesholder.cpp.patch | libs/indexer/categories_holder.cpp | Fix `std::isdigit` |
| 0024 | libs-indexer-editablemapobject.cpp.patch | libs/indexer/editable_map_object.cpp | Fix `isdigit`, `isalnum` |
| 0025 | libs-indexer-searchstringutils.cpp.patch | libs/indexer/search_string_utils.cpp | Fix `::isdigit` |
| 0026 | libs-indexer-transliterationloader.cpp.patch | libs/indexer/transliteration_loader.cpp | Transliteration path fixes |
| 0027 | libs-map-framework.cpp.patch | libs/map/framework.cpp | Framework initialization |
| 0028 | libs-map-routingmanager.cpp.patch | libs/map/routing_manager.cpp | Routing manager fixes |
| 0029 | libs-platform-CMakeLists.txt.patch | libs/platform/CMakeLists.txt | Platform build config |
| 0030 | libs-platform-guithreadwin.cpp.patch | libs/platform/gui_thread_win.cpp | **NEW FILE**: Windows GUI thread |
| 0031 | libs-platform-httpthreadwin.cpp.patch | libs/platform/http_thread_win.cpp | **NEW FILE**: Windows HTTP thread stub |
| 0032 | libs-platform-localcountryfileutils.cpp.patch | libs/platform/local_country_file_utils.cpp | Fix `isdigit` |
| 0033 | libs-platform-platformandroid.cpp.patch | libs/platform/platform_android.cpp | Android platform fixes |
| 0034 | libs-platform-platformmac.mm.patch | libs/platform/platform_mac.mm | Apple platform fixes |
| 0035 | libs-platform-platformwin.cpp.patch | libs/platform/platform_win.cpp | Windows platform implementation |
| 0036 | libs-routing-lanes-lanesparser.cpp.patch | libs/routing/lanes/lanes_parser.cpp | Fix `std::isspace`, `std::tolower` |
| 0037 | libs-routing-routingquality-api-google-googleapi.cpp.patch | libs/routing/.../google_api.cpp | Google routing API |
| 0038 | libs-routing-routingsession.cpp.patch | libs/routing/routing_session.cpp | Session management |
| 0039 | libs-routing-speedcameramanager.cpp.patch | libs/routing/speed_camera_manager.cpp | Speed camera handling |
| 0040 | libs-search-CMakeLists.txt.patch | libs/search/CMakeLists.txt | Search build config |
| 0041 | libs-search-latlonmatch.cpp.patch | libs/search/latlon_match.cpp | Fix `isdigit` |
| 0042 | libs-search-processor.cpp.patch | libs/search/processor.cpp | Fix `isdigit` |
| 0043 | libs-search-searchquality-samplesgenerationtool-samplesgenerationtool.cpp.patch | libs/search/.../samples_generation_tool.cpp | Fix `isdigit` |
| 0044 | libs-shaders-metalprogrampool.mm.patch | libs/shaders/metal_program_pool.mm | Metal shader pool (Apple) |
| 0045 | libs-transit-transitschedule.cpp.patch | libs/transit/transit_schedule.cpp | Transit schedule handling |

## Patch Categories

### Thread Checker (Patches 0011-0012)

Adds `OMIM_DISABLE_THREAD_CHECKER` compile flag for embedded builds:
- When defined: `CHECK_THREAD_CHECKER` becomes no-op, `CalledOnOriginalThread()` returns `true`
- Prevents assertion failures in plugin contexts where threading model differs from standalone CoMaps

### Character Type Safety (Patches 0009, 0013, 0023-0025, 0032, 0036, 0041-0043)

Fixes Windows debug assertion failures (`c >= -1 && c <= 255`) by casting `char` to `unsigned char`:
```cpp
// Before (assertion failure)
if (isdigit(c)) ...

// After (correct)
if (isdigit(static_cast<unsigned char>(c))) ...
```

### New Platform Files (Patches 0020-0021, 0030-0031)

- `active_frame_callback.cpp/hpp`: Active frame notification for Flutter
- `gui_thread_win.cpp`: Windows GUI thread implementation
- `http_thread_win.cpp`: Windows HTTP thread stub

### OpenGL/Graphics (Patches 0014-0018)

Platform-specific GL function loading and texture handling.

## Platform Impact

| Platform | Affected Patches |
|----------|-----------------|
| Windows | 0011-0012 (thread checker), 0030-0031 (platform), 0035 (platform_win) |
| Android | 0033 (platform_android) |
| iOS/macOS | 0034 (platform_mac), 0044 (metal shaders) |
| All | Character safety, CMake fixes, Drape frontend |

## Troubleshooting

### Thread Checker Assertions

`CHECK(m_threadChecker.CalledOnOriginalThread())` failures indicate cross-thread access.

**Solution**: Ensure `OMIM_DISABLE_THREAD_CHECKER` is defined globally in `src/CMakeLists.txt` before `add_subdirectory` for CoMaps.

### Windows Debug Assertions

`c >= -1 && c <= 255` failures indicate uncasted char in ctype functions.

**Solution**: Add `static_cast<unsigned char>()` around the character argument.

### Map Registration Failures

`result=4` (BadFile) means the MWM file is missing, corrupted, or has path issues.

**Solution**: 
1. Verify the file exists at the specified path
2. Ensure path separators are correct (Windows uses backslashes)
3. Check the MWM file is a valid, complete download
