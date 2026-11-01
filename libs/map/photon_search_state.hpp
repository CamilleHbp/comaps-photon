#pragma once

#include "map/photon_api.hpp"

#include <atomic>
#include <chrono>
#include <vector>

namespace photon
{
// Results are combined on the UI thread. Workers only read the cancellation flag.
// Keeping this independent of JNI lets tests exercise out-of-order callbacks.
template <typename Result>
struct SearchState
{
  std::atomic<bool> m_active{true};
  bool m_offlineDone = false, m_onlineDone = false;
  Status m_status = Status::Loading;
  std::vector<Result> m_offline, m_online;

  bool UpdateOffline(std::vector<Result> results, bool done)
  {
    if (!m_active)
      return false;
    m_offline = std::move(results);
    m_offlineDone = done;
    return true;
  }

  bool UpdateOnline(std::vector<Result> results, Status status)
  {
    if (!m_active)
      return false;
    m_online = std::move(results);
    m_status = status;
    m_onlineDone = true;
    return true;
  }

  bool Complete() const { return m_offlineDone && m_onlineDone; }

  std::vector<Result> Snapshot() const
  {
    auto results = m_online;
    results.insert(results.end(), m_offline.begin(), m_offline.end());
    return results;
  }
};

// One instance survives search-screen sessions. Calls belong to one network worker.
struct RequestThrottle
{
  using Clock = std::chrono::steady_clock;
  Clock::time_point m_nextRequest{}, m_cooldown{};

  bool IsThrottled(Clock::time_point now) const { return now < m_cooldown; }
  Clock::duration Delay(Clock::time_point now) const
  {
    return now < m_nextRequest ? m_nextRequest - now : Clock::duration::zero();
  }
  void Started(Clock::time_point now) { m_nextRequest = now + std::chrono::seconds(1); }
  void Throttle(Clock::time_point now, std::chrono::seconds delay) { m_cooldown = now + delay; }
};
}  // namespace photon
