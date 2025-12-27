// Windows-specific GUI thread implementation for agus-maps-flutter
// Provides a task queue with a dedicated background thread

#pragma once
#ifdef _WIN32

#include "base/task_loop.hpp"

#include <windows.h>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <condition_variable>

namespace agus {

/// Windows implementation of base::TaskLoop for GUI thread operations
/// Uses a dedicated thread with a simple task queue
class AgusGuiThreadWin : public base::TaskLoop
{
public:
    AgusGuiThreadWin();
    ~AgusGuiThreadWin() override;

    PushResult Push(Task && task) override;
    PushResult Push(Task const & task) override;

private:
    void ThreadFunc();
    
    std::thread m_thread;
    std::queue<Task> m_taskQueue;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::atomic<bool> m_running{true};
};

} // namespace agus

// Factory function to create the GUI thread
std::unique_ptr<base::TaskLoop> CreateAgusGuiThreadWin();

#endif // _WIN32

