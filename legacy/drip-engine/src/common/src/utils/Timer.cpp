#include "drip/common/utils/Timer.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>

namespace drip::common::utils
{

auto Timer::oneShot(std::chrono::milliseconds latency, std::function<void()> callback) -> std::unique_ptr<Timer>
{
    return std::unique_ptr<Timer>(new Timer {latency, std::move(callback), false});
}

auto Timer::periodic(std::chrono::milliseconds latency, std::function<void()> callback) -> std::unique_ptr<Timer>
{
    return std::unique_ptr<Timer>(new Timer {latency, std::move(callback), true});
}

Timer::Timer(std::chrono::milliseconds latency, std::function<void()> callback, bool isPeriodic)
    : _latency(latency),
      _isPeriodic(isPeriodic),
      _callbackThread([this, callback = std::move(callback)] {
          while (_isRunning.load())
          {
              auto lock = std::unique_lock(_timerMutex);
              _timerCondition.wait_for(lock, _latency, [this] {
                  return !_isRunning.load();
              });
              if (_isRunning)
              {
                  callback();
              }
              if (!_isPeriodic)
              {
                  break;
              }
          }
      })
{
}

Timer::~Timer()
{
    stop();
}

void Timer::stop()
{
    _isRunning.store(false);
    _timerCondition.notify_one();
}
}
