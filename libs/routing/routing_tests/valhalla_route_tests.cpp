#include "testing/testing.hpp"
#include "routing/valhalla_route.hpp"
#include "routing/route.hpp"
#include "platform/location.hpp"
#include "geometry/mercator.hpp"
#include "cppjansson/cppjansson.hpp"
#include <cstdlib>
#include <memory>

namespace
{
routing::valhalla::Response Example()
{
  using namespace routing::valhalla;
  Response r; r.error = Error::None; r.seconds = 180;
  Leg leg; leg.shape = {{48.85,2.35},{48.851,2.35},{48.851,2.351},{48.852,2.351}}; leg.seconds = 180;
  Maneuver start; start.type = 1; start.end = 1; start.seconds = 60;
  Maneuver right; right.type = 10; right.begin = 1; right.end = 3; right.seconds = 120; right.name = "Rue du Test";
  Maneuver end; end.type = 4; end.begin = end.end = 3;
  leg.maneuvers = {start,right,end}; r.legs.push_back(leg); return r;
}
UNIT_TEST(Valhalla_RouteConversionAndMatching)
{
  routing::Route route("test", 123);
  TEST(routing::valhalla::ConvertRoute(Example(), route), ());
  TEST(route.IsRouteId(123), ());
  TEST_EQUAL(route.GetRouterId(), "valhalla-online", ());
  TEST_EQUAL(route.GetRouteSegments().size(), 3, ());
  TEST_EQUAL(route.GetTotalTimeSec(), 180, ());
  TEST_EQUAL(route.GetRouteSegments()[0].GetTurn().m_turn, routing::turns::CarDirection::TurnRight, ());
  TEST(route.GetRouteSegments().back().GetTurn().IsTurnReachedYourDestination(), ());
  double distance = 0, time = 0;
  for (auto const & segment : route.GetRouteSegments())
  {
    TEST(segment.IsExternalRoad() && !segment.HasNativeFeature() && !segment.IsArtificialConnector(), ());
    TEST_GREATER_OR_EQUAL(segment.GetDistFromBeginningMeters(), distance, ());
    TEST_GREATER_OR_EQUAL(segment.GetTimeFromBeginningSec(), time, ());
    distance = segment.GetDistFromBeginningMeters(); time = segment.GetTimeFromBeginningSec();
  }
  location::GpsInfo gps; gps.m_latitude=48.8505; gps.m_longitude=2.35; gps.m_horizontalAccuracy=5;
  route.MoveIterator(gps);
  location::RouteMatchingInfo match;
  TEST(route.MatchLocationToRoute(gps, match), ());
}
UNIT_TEST(Valhalla_SharedFixtureAndTurnIndexes)
{
  json_error_t error;
  base::JSONPtr fixture(json_load_file(VALHALLA_FIXTURE_PATH, 0, &error));
  TEST(fixture != nullptr, ());
  std::unique_ptr<char, decltype(&free)> text(json_dumps(json_object_get(fixture.get(), "response"), 0), free);
  routing::valhalla::Options options; options.avoidTolls = true;
  auto response=routing::valhalla::ParseResponse(text.get(), options);
  routing::Route route("test", 1);
  TEST(routing::valhalla::ConvertRoute(response, route), ());
  TEST_EQUAL(route.GetRouteSegments()[0].GetTurn().m_index, 1, ());
  TEST_EQUAL(route.GetRouteSegments().back().GetTurn().m_index, 3, ());
  TEST_EQUAL(route.GetRouteSegments()[1].GetRoadNameInfo().m_name, "Rue de l’Arrivée", ());
}
UNIT_TEST(Valhalla_UnsupportedManeuverDoesNotReplaceRoute)
{
  auto response=Example(); response.legs[0].maneuvers[1].type=100;
  routing::Route route("existing", 7);
  TEST(!routing::valhalla::ConvertRoute(response, route), ());
  TEST_EQUAL(route.GetRouterId(), "existing", ());
}
UNIT_TEST(Valhalla_InvalidCoordinatesAndTimesDoNotReplaceRoute)
{
  routing::Route route("existing", 7);
  auto response = Example(); response.legs[0].shape[1].lat = 91;
  TEST(!routing::valhalla::ConvertRoute(response, route), ());
  response = Example(); response.legs[0].seconds = std::numeric_limits<double>::infinity();
  TEST(!routing::valhalla::ConvertRoute(response, route), ());
  response = Example(); response.legs[0].maneuvers[0].seconds = -1;
  TEST(!routing::valhalla::ConvertRoute(response, route), ());
  TEST_EQUAL(route.GetRouterId(), "existing", ());
}
UNIT_TEST(Valhalla_LegBoundariesAndNativeSegments)
{
  auto response=Example(); auto leg=response.legs[0];
  for (auto & p : leg.shape) { p.lat += 0.002; p.lon += 0.001; }
  leg.shape.front() = response.legs[0].shape.back();
  response.legs.push_back(leg); response.seconds=360;
  routing::Route route("test", 1);
  TEST(routing::valhalla::ConvertRoute(response, route), ());
  TEST_EQUAL(route.GetSubrouteCount(), 2, ());
  TEST_EQUAL(route.GetSubrouteAttrs(1).GetBeginSegmentIdx(), 3, ());
  TEST(!route.GetRouteSegments()[2].GetTurn().IsTurnReachedYourDestination(), ());
  TEST_EQUAL(route.GetTotalTimeSec(), 360, ());
  routing::RouteSegment native(routing::Segment(0,0,0,true), {}, {}, {});
  TEST(native.HasNativeFeature() && !native.IsArtificialConnector(), ());
  routing::RouteSegment connector(routing::Segment(), {}, {}, {});
  TEST(connector.IsArtificialConnector() && !connector.IsExternalRoad(), ());
}
}
