#include "routing/valhalla_route.hpp"

#include "routing/route.hpp"
#include "geometry/mercator.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace routing::valhalla
{
namespace
{
std::optional<turns::CarDirection> Direction(unsigned type)
{
  using D = turns::CarDirection;
  switch (type)
  {
  case 0: case 1: case 2: case 3: case 7: return D::None;
  case 4: case 5: case 6: return D::ReachedYourDestination;
  case 8: case 17: case 22: case 25: case 28: case 29: return D::GoStraight;
  case 9: case 18: case 23: case 37: return D::TurnSlightRight;
  case 10: return D::TurnRight;
  case 11: return D::TurnSharpRight;
  case 12: return D::UTurnRight;
  case 13: return D::UTurnLeft;
  case 14: return D::TurnSharpLeft;
  case 15: return D::TurnLeft;
  case 16: case 19: case 24: case 38: return D::TurnSlightLeft;
  case 20: return D::ExitHighwayToRight;
  case 21: return D::ExitHighwayToLeft;
  case 26: return D::EnterRoundAbout;
  case 27: return D::LeaveRoundAbout;
  default: return {};
  }
}
}  // namespace

bool ConvertRoute(Response const & response, Route & route)
{
  if (response.error != Error::None || response.legs.empty()) return false;
  std::vector<m2::PointD> geometry;
  std::vector<RouteSegment> segments;
  std::vector<Route::SubrouteAttrs> subroutes;
  double totalMeters = 0, totalMerc = 0, totalSeconds = 0;
  for (size_t legIndex = 0; legIndex < response.legs.size(); ++legIndex)
  {
    auto const & leg = response.legs[legIndex];
    if (leg.shape.size() < 2 || leg.maneuvers.size() < 2 || !std::isfinite(leg.seconds) || leg.seconds < 0) return false;
    std::vector<m2::PointD> points;
    for (auto const & p : leg.shape)
    {
      if (!std::isfinite(p.lat) || !std::isfinite(p.lon) || std::abs(p.lat) > 85 || std::abs(p.lon) > 180) return false;
      points.push_back(mercator::FromLatLon(p.lat, p.lon));
    }
    size_t const offset = segments.size();
    size_t const count = points.size() - 1;
    if (geometry.empty()) geometry.push_back(points.front());
    else if (geometry.back() != points.front()) return false;
    std::vector<double> distances(count), times(count, 0);
    std::vector<turns::TurnItem> turnItems(count);
    std::vector<RouteSegment::RoadNameInfo> names(count);
    std::vector<RoutingOptions> roadTypes(count);
    for (size_t i = 0; i < count; ++i) distances[i] = mercator::DistanceOnEarth(points[i], points[i + 1]);
    for (auto const & m : leg.maneuvers)
    {
      auto const direction = Direction(m.type);
      if (!direction || m.begin > m.end || m.end > count || !std::isfinite(m.seconds) || m.seconds < 0) return false;
      if (m.begin > 0 && !(m.type >= 4 && m.type <= 6 && legIndex + 1 < response.legs.size()))
        turnItems[m.begin - 1] = turns::TurnItem(static_cast<uint32_t>(offset + m.begin), *direction, m.exit);
      double distance = 0;
      for (size_t i = m.begin; i < m.end; ++i) distance += distances[i];
      if (m.begin == m.end && m.seconds != 0) return false;
      for (size_t i = m.begin; i < m.end; ++i)
      {
        times[i] += distance > 0 ? m.seconds * (distances[i] / distance) : m.seconds / (m.end - m.begin);
        names[i] = RouteSegment::RoadNameInfo(m.name);
        names[i].m_junction_ref = m.exitNumber;
        names[i].m_destination = m.destination;
        names[i].m_destination_ref = m.destinationRef;
        names[i].m_isLink = m.type >= 17 && m.type <= 21;
        if (m.toll) roadTypes[i].Add(RoutingOptions::Toll);
        if (m.highway) roadTypes[i].Add(RoutingOptions::Motorway);
      }
    }
    double elapsed = 0, maneuverTotal = 0;
    for (auto const & m : leg.maneuvers) maneuverTotal += m.seconds;
    if (!std::isfinite(maneuverTotal)) return false;
    if (leg.seconds > 0 && maneuverTotal == 0) return false;
    for (size_t i = 0; i < count; ++i)
    {
      elapsed += times[i];
      // Preserve the service's leg total despite rounded maneuver durations.
      double const fraction = leg.seconds == 0 ? 0 : elapsed;
      double const seconds = maneuverTotal > 0 ? fraction / maneuverTotal * leg.seconds : 0;
      totalMeters += distances[i]; totalMerc += points[i].Length(points[i + 1]);
      geometry.push_back(points[i + 1]);
      segments.emplace_back(Segment(), turnItems[i], geometry::PointWithAltitude(points[i + 1], geometry::kInvalidAltitude), names[i]);
      segments.back().SetExternalRoad();
      segments.back().SetRoadTypes(roadTypes[i]);
      segments.back().SetDistancesAndTime(totalMeters, totalMerc, totalSeconds + seconds);
    }
    totalSeconds += leg.seconds;
    if (!std::isfinite(totalSeconds)) return false;
    subroutes.emplace_back(geometry::PointWithAltitude(points.front(), geometry::kInvalidAltitude),
                           geometry::PointWithAltitude(points.back(), geometry::kInvalidAltitude), offset, segments.size());
  }
  if (geometry.size() != segments.size() + 1 || totalMeters == 0) return false;
  // SetGeometry resets the followed polyline, including its artificial-edge mask.
  Route converted("valhalla-online", route.GetRouteId());
  converted.SetCarRouteInfo({true, CarBackend::Valhalla, CarFallbackReason::None});
  converted.SetGeometry(geometry.begin(), geometry.end());
  converted.SetRouteSegments(std::move(segments));
  converted.SetSubroteAttrs(std::move(subroutes));
  route = std::move(converted);
  return true;
}
}  // namespace routing::valhalla
