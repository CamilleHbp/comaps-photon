#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace routing::valhalla
{
struct Point
{
  double lat = 0, lon = 0;
  bool operator==(Point const &) const = default;
};

struct Options
{
  bool avoidTolls = false;
  bool avoidMotorways = false;
  bool avoidFerries = false;
  bool hardExclusions = false;
  std::optional<double> heading;
  std::string language = "en";
};

enum class Error { None, InvalidResponse, Toll, Restriction, RestrictionUnknown, ServerWarning };

struct Maneuver
{
  unsigned type = 0;
  size_t begin = 0, end = 0;
  double seconds = 0;
  unsigned exit = 0;
  std::string name, exitNumber, destination, destinationRef;
  bool toll = false, highway = false;
};

struct Leg
{
  std::vector<Point> shape;
  std::vector<Maneuver> maneuvers;
  double seconds = 0, meters = 0;
};

struct Response
{
  Error error = Error::InvalidResponse;
  std::vector<Leg> legs;
  double seconds = 0, meters = 0;
};

// Empty request / false indicates invalid input. No network or settings access.
std::string MakeRequest(std::vector<Point> const & points, Options const & options);
bool DecodePolyline6(std::string const & encoded, std::vector<Point> & points);
Response ParseResponse(std::string const & json, Options const & options);
}  // namespace routing::valhalla
