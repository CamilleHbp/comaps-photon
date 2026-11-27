#include "routing/car_router.hpp"
#include "routing/route.hpp"
#include "routing/router_delegate.hpp"

namespace routing
{
CarRouter::CarRouter(CarRoutingConfig config, std::unique_ptr<IRouter> offline, OnlineTransport transport,
                     std::shared_ptr<CarRoutingState> state, OptionsGetter options)
  : m_config(config), m_offline(std::move(offline)), m_online(config, std::move(transport)),
    m_state(std::move(state)), m_options(std::move(options))
{}

void CarRouter::StartNewJourney()
{
  m_offlinePinned = false;
  m_pinnedReason = CarFallbackReason::None;
  m_state->failure = CarRoutingFailure::None;
  m_state->reason = CarFallbackReason::None;
}

RouterResultCode CarRouter::CalculateRoute(Checkpoints const & checkpoints, m2::PointD const & direction,
                                          bool adjust, RouterDelegate const & delegate, Route & route)
{
  if (delegate.IsCancelled()) return RouterResultCode::Cancelled;
  auto const options = m_options();
  auto reason = m_offlinePinned ? m_pinnedReason : CarFallbackReason::None;
  bool const unsupported = options.Has(RoutingOptions::Dirty) || options.Has(RoutingOptions::Paved) ||
                           options.Has(RoutingOptions::Steps);
  if (m_config.online && !m_offlinePinned)
  {
    if (unsupported)
      reason = CarFallbackReason::UnsupportedOptions;
    else
    {
      m_online.SetOptionsForRequest(options);
      auto const code = m_online.CalculateRoute(checkpoints, direction, false, delegate, route);
      if (code == RouterResultCode::Cancelled || delegate.IsCancelled()) return RouterResultCode::Cancelled;
      if (code == RouterResultCode::NoError)
      {
        m_state->failure = CarRoutingFailure::None;
        m_state->reason = CarFallbackReason::None;
        return code;
      }
      reason = m_online.GetFailureReason();
    }
  }
  if (delegate.IsCancelled()) return RouterResultCode::Cancelled;
  Route candidate(m_offline->GetName(), route.GetRouteId());
  m_offline->SetOptionsForRequest(options);
  auto code = m_offline->CalculateRoute(checkpoints, direction, adjust && (!m_config.online || m_offlinePinned), delegate, candidate);
  if (code == RouterResultCode::Cancelled || delegate.IsCancelled()) return RouterResultCode::Cancelled;
  if (code == RouterResultCode::NoError && (!candidate.IsValid() || candidate.GetRouteSegments().empty()))
    code = RouterResultCode::InternalError;
  // Like upstream CoMaps, unavoidable tolls are reported as route warnings rather than failures.
  m_state->reason = reason;
  if (code == RouterResultCode::NoError)
  {
    candidate.SetCarRouteInfo({true, CarBackend::CoMaps, reason});
    route = std::move(candidate);
    m_state->failure = CarRoutingFailure::None;
    if (m_config.online) { m_offlinePinned = true; m_pinnedReason = reason; }
  }
  else
  {
    for (auto const & country : candidate.GetAbsentCountries()) route.AddAbsentCountry(country);
    route.SetCarRouteInfo({true, CarBackend::CoMaps, reason});
    m_state->failure = code == RouterResultCode::NeedMoreMaps ? CarRoutingFailure::MissingMaps :
        options.Has(RoutingOptions::Toll) && code == RouterResultCode::RouteNotFound ?
          CarRoutingFailure::NoTollFreeRoute :
        reason != CarFallbackReason::None && reason != CarFallbackReason::UnsupportedOptions ?
          CarRoutingFailure::Service : CarRoutingFailure::RouteNotFound;
  }
  return code;
}

bool CarRouter::FindClosestProjectionToRoad(m2::PointD const & point, m2::PointD const & direction,
                                           double radius, EdgeProj & projection)
{
  if (m_config.online && !m_offlinePinned) return false;
  return m_offline->FindClosestProjectionToRoad(point, direction, radius, projection);
}
}
