#include "routing/valhalla_router.hpp"
#include "routing/checkpoints.hpp"
#include "routing/route.hpp"
#include "routing/router_delegate.hpp"
#include "routing/routing_options.hpp"
#include "routing/valhalla_route.hpp"
#include "geometry/mercator.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace routing
{
std::string NormalizeValhallaEndpoint(std::string endpoint)
{
  if (!endpoint.starts_with("https://") || endpoint.size() > 2048 ||
      endpoint.find_first_of("@?#\\") != std::string::npos ||
      std::any_of(endpoint.begin(), endpoint.end(), [](unsigned char c) { return c <= 32 || c >= 127; }))
    return {};
  auto const slash = endpoint.find('/', 8);
  auto const authority = endpoint.substr(8, slash == std::string::npos ? slash : slash - 8);
  if (authority.empty() || std::any_of(authority.begin(), authority.end(), [](unsigned char c)
      { return !std::isalnum(c) && c != '.' && c != '-' && c != ':' && c != '[' && c != ']'; }))
    return {};
  while (endpoint.ends_with('/')) endpoint.pop_back();
  for (auto const * action : {"/route", "/isochrone", "/status"})
    if (endpoint.ends_with(action)) { endpoint.resize(endpoint.size() - std::char_traits<char>::length(action)); break; }
  return endpoint + "/route";
}

ValhallaRouter::ValhallaRouter(CarRoutingConfig config, OnlineTransport transport)
  : m_config(std::move(config)), m_transport(std::move(transport))
{}

void ValhallaRouter::SetOptionsForRequest(RoutingOptions const & options)
{
  m_options.avoidTolls = options.Has(RoutingOptions::Toll);
  m_options.avoidMotorways = options.Has(RoutingOptions::Motorway);
  m_options.avoidFerries = options.Has(RoutingOptions::Ferry);
  m_options.hardExclusions = m_config.hardExclusions;
}

RouterResultCode ValhallaRouter::CalculateRoute(Checkpoints const & checkpoints, m2::PointD const & direction,
                                               bool, RouterDelegate const & delegate, Route & route)
{
  m_failure = CarFallbackReason::None;
  if (delegate.IsCancelled()) return RouterResultCode::Cancelled;
  auto fail = [&](CarFallbackReason reason) { m_failure = reason; return RouterResultCode::RouteNotFound; };
  auto const endpoint = NormalizeValhallaEndpoint(m_config.endpoint);
  if (!m_config.online || endpoint.empty() || !m_transport) return fail(CarFallbackReason::Service);
  std::vector<valhalla::Point> points;
  for (size_t i = checkpoints.GetPassedIdx(); i < checkpoints.GetPoints().size(); ++i)
  {
    auto const point = mercator::ToLatLon(checkpoints.GetPoint(i));
    points.push_back({point.m_lat, point.m_lon});
  }
  m_options.heading.reset();
  if (!direction.IsAlmostZero())
    m_options.heading = std::fmod(std::atan2(direction.x, direction.y) * 180 / std::acos(-1) + 360, 360);
  auto const request = valhalla::MakeRequest(points, m_options);
  if (request.empty()) return fail(CarFallbackReason::InvalidResponse);
  auto response = m_transport(endpoint, request, delegate);
  if (delegate.IsCancelled() || response.result == OnlineResult::Cancelled) return RouterResultCode::Cancelled;
  switch (response.result)
  {
  case OnlineResult::Timeout: return fail(CarFallbackReason::Timeout);
  case OnlineResult::Busy: return fail(CarFallbackReason::RateLimit);
  case OnlineResult::Unavailable: return fail(CarFallbackReason::Service);
  case OnlineResult::Cancelled: return RouterResultCode::Cancelled;
  case OnlineResult::Ok: break;
  }
  auto const parsed = valhalla::ParseResponse(response.body, m_options);
  switch (parsed.error)
  {
  case valhalla::Error::Toll: return fail(CarFallbackReason::Toll);
  case valhalla::Error::RestrictionUnknown: return fail(CarFallbackReason::UnknownRestriction);
  case valhalla::Error::ServerWarning: return fail(CarFallbackReason::ServerWarning);
  case valhalla::Error::Restriction: case valhalla::Error::InvalidResponse:
    return fail(CarFallbackReason::InvalidResponse);
  case valhalla::Error::None: break;
  }
  if (parsed.legs.size() + 1 != points.size()) return fail(CarFallbackReason::InvalidResponse);
  Route candidate(GetName(), route.GetRouteId());
  if (!valhalla::ConvertRoute(parsed, candidate)) return fail(CarFallbackReason::InvalidResponse);
  // Keep the session's absolute checkpoint indices while sending only unvisited stops.
  std::vector<Route::SubrouteAttrs> subroutes;
  for (size_t i = 0; i < checkpoints.GetPassedIdx(); ++i)
    subroutes.emplace_back(geometry::PointWithAltitude(checkpoints.GetPoint(i), geometry::kInvalidAltitude),
                           geometry::PointWithAltitude(checkpoints.GetPoint(i + 1), geometry::kInvalidAltitude), 0, 0);
  auto const & remaining = candidate.GetSubroutes();
  subroutes.insert(subroutes.end(), remaining.begin(), remaining.end());
  candidate.SetCurrentSubrouteIdx(checkpoints.GetPassedIdx());
  candidate.SetSubroteAttrs(std::move(subroutes));
  candidate.SetCarRouteInfo({true, CarBackend::Valhalla, CarFallbackReason::None});
  if (delegate.IsCancelled()) return RouterResultCode::Cancelled;
  route = std::move(candidate);
  return RouterResultCode::NoError;
}
}
