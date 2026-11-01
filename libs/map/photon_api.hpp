#pragma once

#include "geometry/latlon.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace photon
{
// Values are shared with Android's SearchEngine.PHOTON_* constants.
enum class Status
{
  Ready = 0,
  Loading,
  Empty,
  Unavailable,
  Throttled
};

// Keep the transport independent of downloaded-map IDs and the search engine.
struct Place
{
  ms::LatLon m_position;
  std::string m_name;
  std::string m_address;
};

struct Response
{
  Status m_status = Status::Unavailable;
  std::vector<Place> m_results;
  std::chrono::seconds m_retryAfter{60};
};

bool IsValidEndpoint(std::string const & endpoint);
std::string MakeUrl(std::string const & endpoint, std::string const & query, std::string const & language,
                    ms::LatLon const & bias);
Response ParseResponse(std::string const & body);
Response DecodeResponse(int httpCode, std::string const & body, std::string const & retryAfter = {});
// Blocking; call only on a worker. HTTP failures never become successful empty searches.
Response Search(std::string const & url);
}  // namespace photon
