#pragma once

#include "routing/valhalla_router.hpp"
#include "routing/routing_options.hpp"

#include <memory>

namespace routing
{
class CarRouter final : public IRouter
{
public:
  using OptionsGetter = std::function<RoutingOptions()>;
  CarRouter(CarRoutingConfig config, std::unique_ptr<IRouter> offline, OnlineTransport transport,
            std::shared_ptr<CarRoutingState> state, OptionsGetter options);
  std::string GetName() const override { return "car-coordinator"; }
  void ClearState() override { m_offline->ClearState(); }
  void StartNewJourney() override;
  // The actual producing backend determines missing-map checks after calculation.
  bool UsesLocalMaps() const override { return false; }
  void SetGuides(GuidesTracks && guides) override { m_offline->SetGuides(std::move(guides)); }
  RouterResultCode CalculateRoute(Checkpoints const &, m2::PointD const &, bool,
                                  RouterDelegate const &, Route &) override;
  bool FindClosestProjectionToRoad(m2::PointD const &, m2::PointD const &, double, EdgeProj &) override;

private:
  CarRoutingConfig m_config;
  std::unique_ptr<IRouter> m_offline;
  ValhallaRouter m_online;
  std::shared_ptr<CarRoutingState> m_state;
  OptionsGetter m_options;
  std::atomic<bool> m_offlinePinned{false}; // Also read by GUI-thread road projection.
  CarFallbackReason m_pinnedReason = CarFallbackReason::None;
};
}
