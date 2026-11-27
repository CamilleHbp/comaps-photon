#include "testing/testing.hpp"
#include "routing/car_router.hpp"
#include "routing/checkpoints.hpp"
#include "routing/route.hpp"
#include "routing/router_delegate.hpp"
#include "routing/routing_session.hpp"
#include "routing/routing_tests/tools.hpp"
#include "routing/absent_regions_finder.hpp"
#include "routing/data_source.hpp"
#include "geometry/mercator.hpp"
#include "platform/location.hpp"
#include "cppjansson/cppjansson.hpp"

#include <cstdlib>
#include <memory>
#include <future>

namespace
{
using namespace routing;
std::string FixtureResponse()
{
  json_error_t error;
  base::JSONPtr fixture(json_load_file(VALHALLA_FIXTURE_PATH, 0, &error));
  TEST(fixture != nullptr, ());
  std::unique_ptr<char, decltype(&free)> text(json_dumps(json_object_get(fixture.get(), "response"), 0), free);
  return text.get();
}
Checkpoints Points()
{
  return Checkpoints(mercator::FromLatLon(48.85, 2.35), mercator::FromLatLon(48.852, 2.351));
}
struct Offline final : IRouter
{
  int calls = 0;
  bool toll = false;
  RouterResultCode result = RouterResultCode::NoError;
  RoutingOptions options;
  std::function<void()> onCalculate;
  std::string GetName() const override { return "offline-test"; }
  void SetGuides(GuidesTracks &&) override {}
  void SetOptionsForRequest(RoutingOptions const & value) override { options = value; }
  bool FindClosestProjectionToRoad(m2::PointD const &, m2::PointD const &, double, EdgeProj &) override { return true; }
  RouterResultCode CalculateRoute(Checkpoints const & points, m2::PointD const &, bool,
                                  RouterDelegate const &, Route & route) override
  {
    ++calls;
    if (onCalculate) onCalculate();
    if (result == RouterResultCode::NeedMoreMaps) route.AddAbsentCountry("Missing");
    if (result != RouterResultCode::NoError) return result;
    Route candidate(GetName(), route.GetRouteId());
    auto const & geometry = points.GetPoints();
    candidate.SetGeometry(geometry.begin(), geometry.end());
    std::vector<RouteSegment> segments;
    for (size_t i = 1; i < geometry.size(); ++i)
    {
      segments.emplace_back(Segment(0, 1, static_cast<uint32_t>(i), true), turns::TurnItem{},
                            geometry::PointWithAltitude(geometry[i], 0), RouteSegment::RoadNameInfo{});
      segments.back().SetDistancesAndTime(100.0 * i, 0.1 * i, 60.0 * i);
      if (toll) { RoutingOptions types; types.Add(RoutingOptions::Toll); segments.back().SetRoadTypes(types); }
    }
    candidate.SetRouteSegments(std::move(segments));
    route = std::move(candidate);
    return result;
  }
};

struct Harness
{
  RoutingOptions options;
  std::shared_ptr<CarRoutingState> state = std::make_shared<CarRoutingState>();
  Offline * offline = nullptr;
  int requests = 0;
  OnlineResult response = OnlineResult::Ok;
  std::string body = FixtureResponse();
  std::string sent;
  std::function<void()> onRequest;
  std::unique_ptr<CarRouter> Make(bool enabled = true)
  {
    auto native = std::make_unique<Offline>(); offline = native.get();
    return std::make_unique<CarRouter>(CarRoutingConfig{enabled, "https://routing.example/isochrone", false},
      std::move(native), [this](auto const & url, auto const & payload, auto const &)
      {
        TEST_EQUAL(url, "https://routing.example/route", ());
        ++requests; sent = payload;
        if (onRequest) onRequest();
        return OnlineResponse{response, body};
      }, state, [this] { return options; });
  }
};

UNIT_TEST(CarRouter_DefaultOfflineAndTollPolicy)
{
  Harness h; auto router = h.Make(false); RouterDelegate delegate; Route route("old", 7);
  h.options.Add(RoutingOptions::Toll); h.offline->toll = true;
  // Unavoidable tolls become route warnings, as in upstream CoMaps.
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(h.requests, 0, ());
  TEST_EQUAL(h.state->failure.load(), CarRoutingFailure::None, ());
  TEST(route.IsValid(), ());
  h.offline->result = RouterResultCode::RouteNotFound;
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::RouteNotFound, ());
  TEST_EQUAL(h.state->failure.load(), CarRoutingFailure::NoTollFreeRoute, ());
  h.offline->result = RouterResultCode::NoError; h.offline->toll = false;
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST(route.GetCarRouteInfo().CanNavigate(), ());
}

UNIT_TEST(CarRouter_OnlineRouteNavigation)
{
  Harness h; auto router = h.Make(); RouterDelegate delegate; Route route("old", 17);
  h.options.Add(RoutingOptions::Toll);
  TEST_EQUAL(router->CalculateRoute(Points(), {1, 0}, true, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(h.offline->calls, 0, ());
  TEST_EQUAL(route.GetCarRouteInfo().backend, CarBackend::Valhalla, ());
  TEST(!route.GetCarRouteInfo().HasNativeRoadMetadata(), ());
  TEST(route.GetCarRouteInfo().CanNavigate(), ());
  EdgeProj projection;
  TEST(!router->FindClosestProjectionToRoad({}, {}, 20, projection), ());
  json_error_t error; base::JSONPtr request(json_loads(h.sent.c_str(), 0, &error));
  TEST_EQUAL(json_number_value(json_object_get(json_array_get(json_object_get(request.get(), "locations"), 0), "heading")), 90, ());
  RoutingSession session;
  session.AssignRouteForTesting(std::make_shared<Route>(std::move(route)), RouterResultCode::NoError);
  TEST(session.CanFollowRoute() && session.EnableFollowMode(), ());
}

UNIT_TEST(CarRouter_StickyFallbackAndNewJourney)
{
  Harness h; auto router = h.Make(); RouterDelegate delegate; Route route("old", 7);
  h.response = OnlineResult::Timeout;
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(route.GetCarRouteInfo().fallback, CarFallbackReason::Timeout, ());
  TEST_EQUAL(route.GetCarRouteInfo().backend, CarBackend::CoMaps, ());
  h.response = OnlineResult::Ok;
  router->ClearState(); // Traffic rebuilds must not release the fallback latch.
  TEST_EQUAL(router->CalculateRoute(Points(), {}, true, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(h.requests, 1, ());
  TEST_EQUAL(h.offline->calls, 2, ());
  router->StartNewJourney();
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(h.requests, 2, ());
  TEST_EQUAL(route.GetCarRouteInfo().backend, CarBackend::Valhalla, ());
}

UNIT_TEST(CarRouter_CancellationNeverFallsBackOrAdopts)
{
  Harness h; auto router = h.Make(); RouterDelegate delegate; Route route("old", 7);
  h.onRequest = [&] { delegate.Cancel(); };
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::Cancelled, ());
  TEST_EQUAL(h.offline->calls, 0, ());
  TEST_EQUAL(route.GetRouterId(), "old", ());
  delegate.Reset(); h.onRequest = {}; h.response = OnlineResult::Unavailable;
  h.offline->onCalculate = [&] { delegate.Cancel(); };
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::Cancelled, ());
  TEST(!route.IsValid(), ());
}

UNIT_TEST(CarRouter_OptionsSnapshotAndUnsupportedSurface)
{
  Harness h; auto router = h.Make(); RouterDelegate delegate; Route route("old", 7);
  h.options.Add(RoutingOptions::Toll);
  h.onRequest = [&] { h.options.Remove(RoutingOptions::Toll); };
  h.response = OnlineResult::Busy;
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST(h.offline->options.Has(RoutingOptions::Toll), ());
  TEST_EQUAL(route.GetCarRouteInfo().fallback, CarFallbackReason::RateLimit, ());
  router->StartNewJourney(); h.options.Add(RoutingOptions::Dirty);
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(h.requests, 1, ());
  TEST_EQUAL(route.GetCarRouteInfo().fallback, CarFallbackReason::UnsupportedOptions, ());
}

UNIT_TEST(CarRouter_ReportedTollAndMissingMaps)
{
  Harness h; auto router = h.Make(); RouterDelegate delegate; Route route("old", 7);
  h.options.Add(RoutingOptions::Toll);
  auto at = h.body.find("\"has_toll\": false"); TEST_NOT_EQUAL(at, std::string::npos, ());
  h.body.replace(at, std::string("\"has_toll\": false").size(), "\"has_toll\": true");
  h.offline->result = RouterResultCode::NeedMoreMaps;
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NeedMoreMaps, ());
  TEST_EQUAL(h.state->reason.load(), CarFallbackReason::Toll, ());
  TEST_EQUAL(h.state->failure.load(), CarRoutingFailure::MissingMaps, ());
  TEST(route.GetAbsentCountries().contains("Missing"), ());
  h.offline->result = RouterResultCode::NoError; h.offline->toll = true;
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(route.GetCarRouteInfo().fallback, CarFallbackReason::Toll, ());
  TEST_EQUAL(h.state->failure.load(), CarRoutingFailure::None, ());
}

UNIT_TEST(CarRouter_RemainingCheckpointsAndEndpointValidation)
{
  Harness h; auto router = h.Make(); RouterDelegate delegate; Route route("old", 7);
  Checkpoints checkpoints(std::vector<m2::PointD>{mercator::FromLatLon(48, 2),
      mercator::FromLatLon(48.85, 2.35), mercator::FromLatLon(48.852, 2.351)});
  checkpoints.PassNextPoint();
  TEST_EQUAL(router->CalculateRoute(checkpoints, {}, true, delegate, route), RouterResultCode::NoError, ());
  TEST_EQUAL(route.GetSubrouteCount(), 2, ());
  TEST_EQUAL(route.GetCurrentSubrouteIdx(), 1, ());
  TEST_EQUAL(route.GetSubrouteAttrs(0).GetStart().GetPoint(), checkpoints.GetPoint(0), ());
  json_error_t error; base::JSONPtr request(json_loads(h.sent.c_str(), 0, &error));
  TEST_EQUAL(json_array_size(json_object_get(request.get(), "locations")), 2, ());
  for (auto const * url : {"http://example.org", "https://user:secret@example.org", "https://example.org?secret=x", "https://example.org/#x", "https://"})
    TEST(NormalizeValhallaEndpoint(url).empty(), ());
}

UNIT_TEST(CarRouter_FailedRebuildRetainsRouteWithoutClaimingOnRoute)
{
  Harness h; auto router = h.Make(false); RouterDelegate delegate; Route route("old", 7);
  TEST_EQUAL(router->CalculateRoute(Points(), {}, false, delegate, route), RouterResultCode::NoError, ());
  RoutingSession session;
  session.AssignRouteForTesting(std::make_shared<Route>(std::move(route)), RouterResultCode::NoError);
  TEST(session.EnableFollowMode(), ());
  session.MarkRebuildFailed();
  TEST(session.IsRouteValid() && session.IsFollowing(), ());
  TEST(!session.IsOnRoute(), ());
}

UNIT_CLASS_TEST(AsyncGuiThreadTest, CarRouter_OnlineDoesNotConsultMissingMaps)
{
  Harness h;
  FrozenDataSource data;
  std::atomic<int> mapLookups{0};
  auto finder = std::make_unique<AbsentRegionsFinder>([&](auto const &)
      { ++mapLookups; return std::string("NotDownloaded"); }, [](auto const &) { return false; },
      std::make_shared<NumMwmIds>(), data);
  std::promise<void> ready;
  auto done = ready.get_future();
  {
    AsyncRouter async(nullptr);
    async.SetRouter(h.Make(), std::move(finder));
    async.CalculateRoute(Points(), {}, false, [&](std::shared_ptr<Route> route, RouterResultCode code)
    {
      TEST_EQUAL(code, RouterResultCode::NoError, ());
      TEST_EQUAL(route->GetCarRouteInfo().backend, CarBackend::Valhalla, ());
      ready.set_value();
    }, [&](auto, auto const &) { TEST(false, ("Online route required local maps")); },
    [&](auto) { TEST(false, ("Online route removed")); }, nullptr, 10);
    TEST(done.wait_for(std::chrono::seconds(3)) == std::future_status::ready, ());
  }
  TEST_EQUAL(mapLookups.load(), 0, ());
}

UNIT_CLASS_TEST(AsyncGuiThreadTest, CarRouter_ReplacedEndpointDiscardsStaleResponse)
{
  Harness oldRequest, replacement;
  std::promise<void> started, release, ready;
  auto start = started.get_future();
  auto unblock = release.get_future();
  auto done = ready.get_future();
  std::atomic<int> stale{0}, fallbacks{0};
  oldRequest.onRequest = [&] { started.set_value(); unblock.wait_for(std::chrono::seconds(3)); };
  {
    AsyncRouter async(nullptr);
    auto initial = oldRequest.Make();
    oldRequest.offline->onCalculate = [&] { ++fallbacks; };
    async.SetRouter(std::move(initial), nullptr);
    async.CalculateRoute(Points(), {}, false, [&](auto, auto) { ++stale; }, nullptr,
                         [&](auto) { ++stale; }, nullptr, 10);
    TEST(start.wait_for(std::chrono::seconds(3)) == std::future_status::ready, ());
    async.SetRouter(replacement.Make(false), nullptr); // Same operation as changing the configured endpoint.
    async.CalculateRoute(Points(), {}, false, [&](std::shared_ptr<Route> route, auto code)
    {
      TEST_EQUAL(code, RouterResultCode::NoError, ());
      TEST_EQUAL(route->GetCarRouteInfo().backend, CarBackend::CoMaps, ());
      ready.set_value();
    }, nullptr, nullptr, nullptr, 10);
    release.set_value();
    TEST(done.wait_for(std::chrono::seconds(3)) == std::future_status::ready, ());
  }
  TEST_EQUAL(stale.load(), 0, ());
  TEST_EQUAL(fallbacks.load(), 0, ());
}
}
