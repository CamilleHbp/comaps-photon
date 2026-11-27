#pragma once

#include "routing/car_routing.hpp"
#include "routing/router.hpp"
#include "routing/valhalla_api.hpp"

namespace routing
{
enum class OnlineResult { Ok, Cancelled, Timeout, Busy, Unavailable };
struct OnlineResponse
{
  OnlineResult result = OnlineResult::Unavailable;
  std::string body;
};
using OnlineTransport = std::function<OnlineResponse(std::string const &, std::string const &,
                                                    RouterDelegate const &)>;

class ValhallaRouter final : public IRouter
{
public:
  ValhallaRouter(CarRoutingConfig config, OnlineTransport transport);
  std::string GetName() const override { return "valhalla-online"; }
  void SetGuides(GuidesTracks &&) override {}
  bool UsesLocalMaps() const override { return false; }
  void SetOptionsForRequest(RoutingOptions const & options) override;
  RouterResultCode CalculateRoute(Checkpoints const &, m2::PointD const &, bool,
                                  RouterDelegate const &, Route &) override;
  bool FindClosestProjectionToRoad(m2::PointD const &, m2::PointD const &, double, EdgeProj &) override
  { return false; }
  CarFallbackReason GetFailureReason() const { return m_failure; }

private:
  CarRoutingConfig m_config;
  OnlineTransport m_transport;
  valhalla::Options m_options;
  CarFallbackReason m_failure = CarFallbackReason::None;
};
}
