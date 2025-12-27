// Windows GUI thread implementation using std::thread and message queue
// This provides the same functionality as agus_gui_thread.cpp but for Windows

#ifdef _WIN32

#include "agus_gui_thread_win.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace agus {

AgusGuiThreadWin::AgusGuiThreadWin()
{
    OutputDebugStringA("[AgusGuiThreadWin] Starting GUI thread\n");
    m_thread = std::thread(&AgusGuiThreadWin::ThreadFunc, this);
}

AgusGuiThreadWin::~AgusGuiThreadWin()
{
    OutputDebugStringA("[AgusGuiThreadWin] Stopping GUI thread\n");
    m_running = false;
    m_cv.notify_all();
    
    if (m_thread.joinable()) {
        m_thread.join();
    }
    
    OutputDebugStringA("[AgusGuiThreadWin] GUI thread stopped\n");
}

void AgusGuiThreadWin::ThreadFunc()
{
    OutputDebugStringA("[AgusGuiThreadWin] Thread started\n");
    
    while (m_running) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return !m_taskQueue.empty() || !m_running; });
            
            if (!m_running && m_taskQueue.empty()) {
                break;
            }
            
            if (!m_taskQueue.empty()) {
                task = std::move(m_taskQueue.front());
                m_taskQueue.pop();
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception & e) {
                std::string msg = "[AgusGuiThreadWin] Task exception: " + std::string(e.what()) + "\n";
                OutputDebugStringA(msg.c_str());
            }
        }
    }
    
    OutputDebugStringA("[AgusGuiThreadWin] Thread exiting\n");
}

base::TaskLoop::PushResult AgusGuiThreadWin::Push(Task && task)
{
    if (!m_running) {
        OutputDebugStringA("[AgusGuiThreadWin] Push failed - not running\n");
        return {false, false};
    }
    
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_taskQueue.push(std::move(task));
    }
    m_cv.notify_one();
    
    return {true, false};
}

base::TaskLoop::PushResult AgusGuiThreadWin::Push(Task const & task)
{
    return Push(Task(task));
}

} // namespace agus

// Factory function to create the GUI thread (called from platform init)
std::unique_ptr<base::TaskLoop> CreateAgusGuiThreadWin()
{
    return std::make_unique<agus::AgusGuiThreadWin>();
}

#endif // _WIN32
