#include "utils/timer.h"

namespace httpserver {

Timer::Timer(std::function<void()> callback, TimePoint expiration, double interval)
    : callback_(std::move(callback)),
      expiration_(expiration),
      interval_(interval),
      repeat_(interval > 0.0) {}

void Timer::run() const {
    if (callback_) {
        callback_();
    }
}

void Timer::restart(TimePoint now) {
    if (repeat_) {
        expiration_ = now + std::chrono::microseconds(
            static_cast<int64_t>(interval_ * 1000000));
    }
}

Timer::TimePoint Timer::expiration() const { return expiration_; }
bool Timer::repeat() const { return repeat_; }

}  // namespace httpserver
