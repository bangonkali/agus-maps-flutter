# Windows Implementation Plan (MVP)

## Quick Start: Build & Run

### Prerequisites

- **Visual Studio 2022** with "Desktop development with C++" workload
- **CMake 3.22+** (included with VS or install separately)
- **Ninja** build system (recommended): `winget install Ninja-build.Ninja`
- **Flutter SDK 3.24+** with Windows desktop enabled
- **Git** for Windows
- ~10GB disk space for CoMaps build artifacts

### Enable Flutter Windows Desktop

```powershell
flutter config --enable-windows-desktop
flutter doctor
```

### Debug Mode (Full debugging, slower)

Debug mode enables hot reload, step-through debugging, and verbose logging.

```powershell
# 1. Bootstrap CoMaps dependencies (first time only)
.\scripts\bootstrap_windows.ps1

# 2. Copy CoMaps data files (if not already done)
# Run from Git Bash or WSL:
./scripts/copy_comaps_data.sh

# 3. Run in debug mode
cd example
flutter run -d windows --debug
```

**Debug mode characteristics:**
- Flutter: Hot reload enabled, Dart DevTools available
- Native: Debug symbols included, assertions enabled
- Performance: Slower due to debug overhead
- Logs: View via `OutputDebugString` in Visual Studio debugger or DebugView

### Release Mode (High performance)

```powershell
# Build and run in release mode
cd example
flutter run -d windows --release

# Or build standalone executable
flutter build windows --release
```

**Release mode characteristics:**
- Flutter: AOT compiled, tree-shaken, minified
- Native: Optimized (`/O2`), no debug symbols
- Performance: Full speed
- App location: `build/windows/x64/runner/Release/`

---

## Goal

Get the Windows example app to:

1. Bundle Gibraltar map file (`Gibraltar.mwm`) as an asset
2. Extract map data to `%LOCALAPPDATA%\agus_maps_flutter\`
3. Initialize CoMaps with filesystem paths (no APK/ZIP extraction needed)
4. Render maps using **ANGLE** (OpenGL ES 3.0 → DirectX 11)
5. Display via Flutter's `Texture` widget with zero-copy texture sharing

---

## Architecture Overview

### ANGLE-Based Rendering (Zero-Copy via D3D11)

Windows uses ANGLE to translate OpenGL ES 3.0 calls to DirectX 11. This enables
zero-copy texture sharing since both Flutter (Skia) and CoMaps use DirectX 11
under the hood.

```
┌─────────────────────────────────────────────────────────────┐
│ Flutter Dart Layer                                          │
│   AgusMap widget → Texture(textureId)                       │
│   AgusMapController → FFI calls                             │
├─────────────────────────────────────────────────────────────┤
│ Flutter Windows Engine (Skia/Impeller)                      │
│   TextureRegistrar → samples D3D11 texture                  │
├─────────────────────────────────────────────────────────────┤
│ AgusMapsFlutterPlugin (C++)                                 │
│   FlutterDesktopTextureRegistrar integration                │
│   MethodChannel for asset extraction                        │
├─────────────────────────────────────────────────────────────┤
│ AgusAngleContextFactory                                     │
│   D3D11 shared texture (D3D11_RESOURCE_MISC_SHARED)         │
│   ANGLE EGLSurface from D3D11 texture                       │
│   DrawContext + UploadContext (shared EGL contexts)         │
├─────────────────────────────────────────────────────────────┤
│ CoMaps Core (via src/CMakeLists.txt)                        │
│   Framework → DrapeEngine                                   │
│   OpenGL ES 3.0 calls → ANGLE → DirectX 11                  │
│   map, drape, drape_frontend, platform, etc.                │
└─────────────────────────────────────────────────────────────┘
```

### Zero-Copy Texture Flow

```
ID3D11Texture2D (D3D11_RESOURCE_MISC_SHARED)
      │
      ├──→ ANGLE: eglCreatePbufferFromClientBuffer(EGL_D3D_TEXTURE_ANGLE)
      │         │
      │         ▼
      │    EGLSurface → OpenGL ES FBO
      │         │
      │         ▼
      │    CoMaps DrapeEngine renders
      │
      └──→ Flutter: TextureRegistrar samples D3D11 texture
           (same GPU memory, zero CPU copy)
```

### Key Differences from iOS/macOS (Metal)

| Aspect | iOS/macOS | Windows |
|--------|-----------|---------|
| Graphics API | Metal | DirectX 11 (via ANGLE) |
| CoMaps Backend | `dp::metal::*` | `dp::OGLContext` (OpenGL ES 3.0) |
| Shared Memory | IOSurface | D3D11 Shared Handle |
| Flutter Texture | `FlutterTexture` protocol | `FlutterDesktopTextureRegistrar` |
| Shaders | Pre-compiled `.metallib` | GLSL ES (runtime compiled by ANGLE) |

---

## File Structure

```
windows/
├── CMakeLists.txt                              # Plugin build configuration
├── agus_maps_flutter_plugin.h                  # Plugin class declaration
├── agus_maps_flutter_plugin.cpp                # Plugin implementation
├── agus_maps_flutter_plugin_c_api.cpp          # C API registration
└── include/
    └── agus_maps_flutter/
        └── agus_maps_flutter_plugin_c_api.h    # Public C API header

src/
├── agus_maps_flutter_win.cpp      # Windows FFI implementation
├── agus_platform_win.cpp          # Windows platform stubs
├── agus_angle_context_factory.hpp # ANGLE context factory header
├── agus_angle_context_factory.cpp # ANGLE D3D11 texture integration
└── agus_gui_thread_win.cpp        # Windows GUI thread dispatcher

scripts/
├── bootstrap_windows.ps1          # Full Windows setup script
├── fetch_comaps.ps1               # Clone CoMaps repository
├── apply_comaps_patches.ps1       # Apply patches to CoMaps
└── regenerate_patches.ps1         # Generate patches from modifications
```

---

## ANGLE Library Distribution

ANGLE libraries are extracted from Flutter's engine cache during bootstrap:

```powershell
# Extracted to: build/angle/
libEGL.dll           # EGL implementation
libGLESv2.dll        # OpenGL ES 2.0/3.0 implementation  
d3dcompiler_47.dll   # DirectX shader compiler (if present)
```

### ANGLE Version Sync

Flutter updates ANGLE periodically. If you encounter rendering issues:

1. Check Flutter's ANGLE version:
   ```powershell
   # Find Flutter's ANGLE DLLs
   dir "$env:FLUTTER_ROOT\bin\cache\artifacts\engine\windows-x64\*.dll"
   ```

2. Re-run bootstrap to extract fresh ANGLE libraries:
   ```powershell
   .\scripts\bootstrap_windows.ps1
   ```

3. Verify versions match between `build/angle/` and Flutter's cache

---

## Build Configuration

### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `PLATFORM_DESKTOP` | ON (Windows) | Enable desktop platform code |
| `PLATFORM_WIN` | ON (Windows) | Enable Windows-specific code |
| `USE_PREBUILT_COMAPS` | OFF | Use pre-built CoMaps libraries |
| `CMAKE_BUILD_TYPE` | Release | Debug or Release |

### Compile Definitions

Windows builds include these definitions:
- `PLATFORM_DESKTOP=1` — Desktop platform (not mobile)
- `PLATFORM_WIN=1` — Windows-specific code paths
- `NOMINMAX` — Prevent Windows.h min/max macros
- `WIN32_LEAN_AND_MEAN` — Reduce Windows.h bloat
- `_CRT_SECURE_NO_WARNINGS` — Suppress MSVC deprecation warnings

---

## Debugging

### Visual Studio Debugger

1. Open `example/windows/agus_maps_flutter_example.sln` in VS 2022
2. Set breakpoints in `src/*.cpp` or `windows/*.cpp`
3. Press F5 to debug

### DebugView (without VS)

1. Download [DebugView](https://docs.microsoft.com/sysinternals/downloads/debugview)
2. Run DebugView as Administrator
3. Enable "Capture Win32" and "Capture Global Win32"
4. Run the Flutter app — logs appear in DebugView

### Log Prefixes

| Prefix | Source |
|--------|--------|
| `[AgusMapsFlutterWin]` | Windows FFI implementation |
| `[AgusMapsFlutterPlugin]` | Flutter plugin |
| `[AgusAngleContextFactory]` | ANGLE/D3D11 context |
| `[AgusGuiThreadWin]` | GUI thread dispatcher |
| `[CoMaps]` | CoMaps core library |

---

## Troubleshooting

### "ANGLE libraries not found"

Run bootstrap script:
```powershell
.\scripts\bootstrap_windows.ps1
```

Or manually copy from Flutter:
```powershell
$flutterEngine = "$env:FLUTTER_ROOT\bin\cache\artifacts\engine\windows-x64"
Copy-Item "$flutterEngine\libEGL.dll" "build\angle\"
Copy-Item "$flutterEngine\libGLESv2.dll" "build\angle\"
```

### "CoMaps source not found"

Fetch CoMaps:
```powershell
.\scripts\fetch_comaps.ps1
.\scripts\apply_comaps_patches.ps1
```

### Build errors in CoMaps code

CoMaps may need Windows-specific patches. Follow the patch workflow:

1. Fix the error in `thirdparty/comaps/`
2. Generate patch: `.\scripts\regenerate_patches.ps1`
3. Commit the new patch file
4. Rebuild

### "eglCreateContext failed"

- Ensure graphics drivers are up to date
- Check if D3D11 is supported: `dxdiag`
- Try software rendering: Set `ANGLE_DEFAULT_PLATFORM=d3d_warp`

### Flutter can't find the plugin

Ensure the plugin is properly registered:
```powershell
cd example
flutter clean
flutter pub get
flutter run -d windows
```

---

## Known Limitations

1. **x64 only** — ARM64 Windows not yet supported
2. **No GPU texture registration yet** — Using placeholder texture ID
3. **Texture copy fallback** — If D3D11 shared textures fail, falls back to pixel buffer (slower)
4. **CI/CD not integrated** — Windows builds are local-only for now

---

## Development Workflow

### Making Changes to Native Code

1. Edit files in `src/` or `windows/`
2. Rebuild: `flutter run -d windows`
3. For faster iteration, use VS debugger with hot-restart

### Adding Windows Patches to CoMaps

1. Modify file in `thirdparty/comaps/`
2. Test the build: `flutter build windows`
3. Generate patch: `.\scripts\regenerate_patches.ps1`
4. Review new patch in `patches/comaps/`
5. Commit the patch file

### Testing Texture Sharing

```dart
// In Dart code, check if texture is working:
final textureId = await platform.createSurface(width, height, density);
print('Texture ID: $textureId'); // Should be >= 0
```

---

## References

- [Flutter Windows Desktop](https://docs.flutter.dev/platform-integration/windows/building)
- [ANGLE Project](https://chromium.googlesource.com/angle/angle)
- [Flutter Texture Widget](https://api.flutter.dev/flutter/widgets/Texture-class.html)
- [D3D11 Texture Sharing](https://docs.microsoft.com/windows/win32/direct3d11/d3d11-usage)
