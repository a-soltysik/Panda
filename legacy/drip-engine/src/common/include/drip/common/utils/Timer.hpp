#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace drip::common::utils
{

class Timer
{
public:
    static auto oneShot(std::chrono::milliseconds latency, std::function<void()> callback) -> std::unique_ptr<Timer>;
    static auto periodic(std::chrono::milliseconds latency, std::function<void()> callback) -> std::unique_ptr<Timer>;

    Timer(const Timer&) = delete;
    Timer(Timer&&) = delete;
    auto operator=(const Timer&) = delete;
    auto operator=(Timer&&) = delete;
    ~Timer();

    void stop();

private:
    Timer(std::chrono::milliseconds latency, std::function<void()> callback, bool isPeriodic);

    std::chrono::milliseconds _latency;
    bool _isPeriodic;
    std::atomic<bool> _isRunning = true;

    std::condition_variable _timerCondition;
    std::mutex _timerMutex;
    std::jthread _callbackThread;
};

}
