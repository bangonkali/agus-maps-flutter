// Windows-specific platform implementation for agus-maps-flutter
// This file provides a complete Platform implementation for Windows
// including GetPlatform(), GetReader(), and other required methods.

#ifdef _WIN32

// Must include windows.h early to avoid conflicts
#define NOGDI  // Prevent ERROR macro from being defined
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

// CRT debug includes for capturing assertion failures
#include <crtdbg.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <cstdlib>
#include <cstring>

#include "platform/platform.hpp"
#include "platform/settings.hpp"
#include "platform/constants.hpp"
#include "platform/measurement_utils.hpp"
#include "coding/file_reader.hpp"
#include "base/file_name_utils.hpp"
#include "base/logging.hpp"
#include "base/task_loop.hpp"
#include "base/assert.hpp"

#include <string>
#include <filesystem>
#include <boost/regex.hpp>

// Include our custom GUI thread
#include "agus_gui_thread_win.hpp"

// Forward declaration
std::unique_ptr<base::TaskLoop> CreateAgusGuiThreadWin();

// ============================================================================
// CRT Debug Handlers - Redirect assertion failures to stderr/log instead of dialog
// ============================================================================
#ifdef _DEBUG

// Custom invalid parameter handler - logs the error instead of showing a dialog
static void AgusInvalidParameterHandler(
    const wchar_t* expression,
    const wchar_t* function,
    const wchar_t* file,
    unsigned int line,
    uintptr_t pReserved)
{
    // Convert wide strings to narrow for logging
    char expr[512] = {0};
    char func[256] = {0};
    char filename[512] = {0};
    
    if (expression) wcstombs(expr, expression, sizeof(expr) - 1);
    if (function) wcstombs(func, function, sizeof(func) - 1);
    if (file) wcstombs(filename, file, sizeof(filename) - 1);
    
    fprintf(stderr, "\n[AGUS CRT ERROR] Invalid parameter detected!\n");
    fprintf(stderr, "  Expression: %s\n", expr[0] ? expr : "(null)");
    fprintf(stderr, "  Function: %s\n", func[0] ? func : "(null)");
    fprintf(stderr, "  File: %s\n", filename[0] ? filename : "(null)");
    fprintf(stderr, "  Line: %u\n", line);
    fflush(stderr);
    
    // Also log to debug output
    char debugMsg[2048];
    snprintf(debugMsg, sizeof(debugMsg),
        "[AGUS CRT ERROR] Invalid parameter: expr='%s', func='%s', file='%s', line=%u\n",
        expr, func, filename, line);
    OutputDebugStringA(debugMsg);
    
    // Trigger a breakpoint if a debugger is attached, otherwise abort
    if (IsDebuggerPresent()) {
        __debugbreak();
    }
}

// Custom CRT report hook - intercepts assertion messages
static int AgusCrtReportHook(int reportType, char* message, int* returnValue)
{
    const char* typeStr = "UNKNOWN";
    switch (reportType) {
        case _CRT_WARN: typeStr = "WARNING"; break;
        case _CRT_ERROR: typeStr = "ERROR"; break;
        case _CRT_ASSERT: typeStr = "ASSERT"; break;
    }
    
    fprintf(stderr, "\n[AGUS CRT %s] %s\n", typeStr, message ? message : "(null)");
    fflush(stderr);
    
    // Also log to debug output
    char debugMsg[4096];
    snprintf(debugMsg, sizeof(debugMsg), "[AGUS CRT %s] %s\n", typeStr, message ? message : "(null)");
    OutputDebugStringA(debugMsg);
    
    // Return TRUE to skip the normal CRT behavior (dialog box)
    // Return FALSE to allow normal CRT behavior after our logging
    *returnValue = 0;  // Don't abort immediately
    
    // For assertions, trigger breakpoint if debugger attached
    if (reportType == _CRT_ASSERT && IsDebuggerPresent()) {
        __debugbreak();
    }
    
    return TRUE;  // We handled it, skip dialog
}

// Initialize CRT debug handlers - call this as early as possible
static void InitCrtDebugHandlers()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    
    // Set our custom invalid parameter handler
    _set_invalid_parameter_handler(AgusInvalidParameterHandler);
    
    // Set report hook to intercept all CRT messages
    _CrtSetReportHook(AgusCrtReportHook);
    
    // Redirect assertions to stderr instead of showing a dialog
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    
    // Redirect errors to stderr instead of showing a dialog
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    
    // Warnings go to stderr too
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
    
    // Disable abort dialog
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    
    fprintf(stderr, "[AGUS] CRT debug handlers initialized - assertions will be logged\n");
    fflush(stderr);
}

// ============================================================================
// CoMaps Assert Handler - Logs assertion failures without showing dialog boxes
// ============================================================================
// Custom CoMaps assert handler that logs to stderr but does NOT crash
// Returns false to skip ASSERT_CRASH() which would call assert(false) and show a dialog
static bool AgusCoMapsAssertHandler(base::SrcPoint const & srcPoint, std::string const & msg)
{
    // Log to stderr (same format as default handler)
    fprintf(stderr, "[AGUS ASSERT] %s:%d\n%s\n", 
            srcPoint.FileName(), srcPoint.Line(), msg.c_str());
    fflush(stderr);
    
    // Also log to Windows debug output
    char debugMsg[4096];
    snprintf(debugMsg, sizeof(debugMsg), "[AGUS ASSERT] %s:%d - %s\n",
             srcPoint.FileName(), srcPoint.Line(), msg.c_str());
    OutputDebugStringA(debugMsg);
    
    // Trigger breakpoint if debugger attached (for debugging)
    if (IsDebuggerPresent()) {
        __debugbreak();
    }
    
    // Return FALSE to skip ASSERT_CRASH() which would show dialog box
    // This allows the app to continue running (may cause issues but avoids blocking)
    return false;
}

// Initialize CoMaps assert handler
static void InitCoMapsAssertHandler()
{
    static bool initialized = false;
    if (initialized) return;
    initialized = true;
    
    base::SetAssertFunction(AgusCoMapsAssertHandler);
    fprintf(stderr, "[AGUS] CoMaps assert handler installed - assertions will be logged without dialogs\n");
    fflush(stderr);
}

// Use a static initializer to set up handlers as early as possible
namespace {
    struct CrtDebugInitializer {
        CrtDebugInitializer() { 
            InitCrtDebugHandlers();
            InitCoMapsAssertHandler();
        }
    };
    static CrtDebugInitializer g_crtDebugInitializer;
}

#else
// Release build - no-op
static void InitCrtDebugHandlers() {}
static void InitCoMapsAssertHandler() {}
#endif

// ============================================================================
// Custom Platform class for Agus Flutter
// Uses "Construct on First Use" idiom to avoid Static Initialization Order Fiasco.
// The Platform base class constructor logs at LINFO level, and in DEBUG builds
// the default g_LogAbortLevel might not be initialized yet, causing an abort.
// ============================================================================
class AgusPlatform : public Platform
{
public:
  AgusPlatform() = default;

  void InitPaths(std::string const & resourcePath, std::string const & writablePath)
  {
    m_resourcesDir = resourcePath;
    m_writableDir = writablePath;
    m_settingsDir = writablePath;
    
    // Normalize paths: ensure trailing backslash
    auto ensureTrailingSlash = [](std::string & path) {
      if (!path.empty() && path.back() != '\\' && path.back() != '/') 
        path += '\\';
    };
    
    ensureTrailingSlash(m_resourcesDir);
    ensureTrailingSlash(m_writableDir);
    ensureTrailingSlash(m_settingsDir);
    
    // Create tmp directory
    m_tmpDir = m_writableDir + "tmp\\";
    std::filesystem::create_directories(m_tmpDir);
    
    // Initialize the GUI thread
    SetGuiThread(CreateAgusGuiThreadWin());
  }
};

// ============================================================================
// GetPlatform() - Use "Construct on First Use" idiom to avoid SIOF
// ============================================================================
// Global platform pointer - initialized lazily to avoid Static Initialization Order Fiasco
static AgusPlatform* g_platform = nullptr;

static AgusPlatform& GetAgusPlatform()
{
    // Configure logging before first Platform construction
    // This ensures g_LogAbortLevel is properly set before Platform constructor runs
    static bool logConfigured = false;
    if (!logConfigured) {
    base::g_LogAbortLevel = base::NUM_LOG_LEVELS;
        logConfigured = true;
    }
    
    if (!g_platform) {
        g_platform = new AgusPlatform();
    }
    return *g_platform;
}

Platform & GetPlatform()
{
  return GetAgusPlatform();
}

// ============================================================================
// Platform method implementations not in platform_win.cpp
// ============================================================================

std::string Platform::Version() const
{
  return "1.0.0";
}

int32_t Platform::IntVersion() const
{
  return 100;
}

// static
Platform::EError Platform::MkDir(std::string const & dirName)
{
  if (std::filesystem::create_directories(dirName))
    return Platform::ERR_OK;
  if (std::filesystem::exists(dirName))
    return Platform::ERR_FILE_ALREADY_EXISTS;
  return Platform::ERR_UNKNOWN;
}

void Platform::GetFilesByRegExp(std::string const & directory, boost::regex const & regexp, FilesList & outFiles)
{
  try {
    for (auto const & entry : std::filesystem::directory_iterator(directory)) {
      std::string name = entry.path().filename().string();
      if (boost::regex_search(name.begin(), name.end(), regexp))
        outFiles.push_back(std::move(name));
    }
  } catch (std::filesystem::filesystem_error const &) {
    // Directory doesn't exist or can't be read
  }
}

void Platform::GetAllFiles(std::string const & directory, FilesList & outFiles)
{
  try {
    for (auto const & entry : std::filesystem::directory_iterator(directory)) {
      outFiles.push_back(entry.path().filename().string());
    }
  } catch (std::filesystem::filesystem_error const &) {
    // Directory doesn't exist or can't be read
  }
}

std::unique_ptr<ModelReader> Platform::GetReader(std::string const & file, std::string searchScope) const
{
  return std::make_unique<FileReader>(ReadPathForFile(file, std::move(searchScope)), 
                                      READER_CHUNK_LOG_SIZE, READER_CHUNK_LOG_COUNT);
}

bool Platform::GetFileSizeByName(std::string const & fileName, uint64_t & size) const
{
  try {
    return GetFileSizeByFullPath(ReadPathForFile(fileName), size);
  } catch (RootException const &) {
    return false;
  }
}

int Platform::PreCachingDepth() const
{
  return 3;
}

int Platform::VideoMemoryLimit() const
{
  return 20 * 1024 * 1024;  // 20 MB
}

void Platform::SetupMeasurementSystem() const
{
  // Check if units are already set in settings
  auto units = measurement_utils::Units::Metric;
  if (settings::Get(settings::kMeasurementUnits, units))
    return;
  
  // Default to metric on Windows (could check registry for locale settings)
  units = measurement_utils::Units::Metric;
  settings::Set(settings::kMeasurementUnits, units);
}

// ============================================================================
// HTTP Thread stubs (required by downloader)
// ============================================================================
class HttpThread;

namespace downloader {
  class IHttpThreadCallback;
  
  void DeleteNativeHttpThread(::HttpThread* thread)
  {
    // No-op: HTTP not supported in headless mode
  }

  ::HttpThread * CreateNativeHttpThread(
      std::string const & url, IHttpThreadCallback & callback, int64_t begRange,
      int64_t endRange, int64_t expectedSize, std::string const & postBody)
  {
    // Return nullptr - no HTTP support in headless mode
    OutputDebugStringA("[AgusPlatformWin] CreateNativeHttpThread called - returning nullptr\n");
    return nullptr;
  }
}

// ============================================================================
// Custom log handler that outputs to Windows debug output
// ============================================================================
#include "agus_env_utils.hpp"

static void AgusLogMessageWin(base::LogLevel level, base::SrcPoint const & src, std::string const & msg)
{
  char const * levelStr = "???";
  switch (level)
  {
  case base::LDEBUG: levelStr = "DEBUG"; break;
  case base::LINFO: levelStr = "INFO"; break;
  case base::LWARNING: levelStr = "WARN"; break;
  case base::LERROR: levelStr = "ERROR"; break;
  case base::LCRITICAL: levelStr = "CRITICAL"; break;
  default: break;
  }

  // Mirror logs to both OutputDebugString (VS debugger) and stderr (flutter run console).
  std::string out = "[CoMaps][" + std::string(levelStr) + "] " + DebugPrint(src) + msg + "\n";
  OutputDebugStringA(out.c_str());
  std::fprintf(stderr, "%s", out.c_str());
  std::fflush(stderr);

  // IMPORTANT: Do not abort by default on Windows.
  // Aborting here makes the app close immediately and hides the root cause if
  // no debugger is attached. If needed for development, opt-in via env var.
  if (level >= base::LCRITICAL && agus::IsEnvEnabled(::getenv("AGUS_ABORT_ON_CRITICAL")))
  {
    OutputDebugStringA("[CoMaps] AGUS_ABORT_ON_CRITICAL=1, aborting\n");
    std::fprintf(stderr, "[CoMaps] AGUS_ABORT_ON_CRITICAL=1, aborting\n");
    std::fflush(stderr);
    abort();
  }
}

// ============================================================================
// Initialization functions (called from Dart FFI)
// ============================================================================

// Defined in agus_maps_flutter_win.cpp, shared initialization state
extern bool g_platformInitialized;

extern "C" void AgusPlatform_InitPaths(const char* resourcePath, const char* writablePath)
{
    // Set log abort level FIRST to avoid assertion failures in DEBUG builds
    // In DEBUG, the default is LERROR which causes any LOG(LERROR) to abort
  // Use NUM_LOG_LEVELS to effectively disable abort-on-log for all levels.
  base::g_LogAbortLevel = base::NUM_LOG_LEVELS;
    base::SetLogMessageFn(&AgusLogMessageWin);
    
    if (g_platformInitialized) {
        OutputDebugStringA("[AgusPlatformWin] Already initialized, skipping\n");
        return;
    }
    
    std::string resourceDir(resourcePath);
    std::string writableDir(writablePath);
    
    // Normalize paths: convert forward slashes to backslashes
    for (auto & c : resourceDir) { if (c == '/') c = '\\'; }
    for (auto & c : writableDir) { if (c == '/') c = '\\'; }
    
    // Initialize the platform (use GetAgusPlatform() to access lazy-initialized instance)
    static_cast<AgusPlatform&>(GetAgusPlatform()).InitPaths(resourceDir, writableDir);
    
    g_platformInitialized = true;
    
    OutputDebugStringA("[AgusPlatformWin] Platform initialized\n");
    OutputDebugStringA(("[AgusPlatformWin] Resources: " + resourceDir + "\n").c_str());
    OutputDebugStringA(("[AgusPlatformWin] Writable: " + writableDir + "\n").c_str());
}

extern "C" void AgusPlatform_Init(const char* apkPath, const char* storagePath)
{
    // On Windows, just forward to InitPaths
    AgusPlatform_InitPaths(apkPath, storagePath);
}

#endif // _WIN32
