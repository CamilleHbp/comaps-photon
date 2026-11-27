#include "routing/valhalla_api.hpp"

#include <jansson.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>

namespace routing::valhalla
{
namespace
{
using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
bool Valid(Point const & p)
{
  return std::isfinite(p.lat) && std::isfinite(p.lon) && std::abs(p.lat) <= 85 && std::abs(p.lon) <= 180;
}
bool Number(json_t * object, char const * key, double & out)
{
  auto * n = json_object_get(object, key);
  if (!json_is_number(n)) return false;
  out = json_number_value(n);
  return std::isfinite(out) && out >= 0;
}
bool Integer(json_t * object, char const * key, size_t & out)
{
  auto * n = json_object_get(object, key);
  if (!json_is_integer(n) || json_integer_value(n) < 0 || json_integer_value(n) > 500000) return false;
  out = static_cast<size_t>(json_integer_value(n));
  return true;
}
std::string String(json_t * value)
{
  return json_is_string(value) ? std::string(json_string_value(value), json_string_length(value)) : std::string{};
}
std::string Names(json_t * array, bool signs = false)
{
  std::string result;
  size_t index; json_t * value;
  json_array_foreach(array, index, value)
  {
    auto const text = String(signs ? json_object_get(value, "text") : value);
    if (!text.empty()) { if (!result.empty()) result += "; "; result += text; }
  }
  return result;
}
// Informational codes: 208 ignored hard exclusions, 401 relaxed second search pass.
// Returned restrictions are still verified from the summary and maneuvers.
bool Warnings(json_t * object)
{
  auto * value = json_object_get(object, "warnings");
  if (!value) return false;
  if (!json_is_array(value)) return true;
  size_t i; json_t * warning;
  json_array_foreach(value, i, warning)
  {
    auto const code = json_integer_value(json_object_get(warning, "code"));
    if (code != 208 && code != 401) return true;
  }
  return false;
}
Error Summary(json_t * summary, Options const & options, double & seconds, double & meters)
{
  if (!Number(summary, "time", seconds) || !Number(summary, "length", meters)) return Error::InvalidResponse;
  meters *= 1000;
  if (!std::isfinite(meters)) return Error::InvalidResponse;
  for (auto const & entry : {std::pair{options.avoidTolls, "has_toll"},
                            std::pair{options.avoidMotorways, "has_highway"},
                            std::pair{options.avoidFerries, "has_ferry"}})
  {
    if (!entry.first) continue;
    auto * value = json_object_get(summary, entry.second);
    if (!json_is_boolean(value)) return Error::RestrictionUnknown;
    if (json_is_true(value)) return std::string(entry.second) == "has_toll" ? Error::Toll : Error::Restriction;
  }
  return Error::None;
}
}  // namespace

std::string MakeRequest(std::vector<Point> const & points, Options const & options)
{
  if (points.size() < 2 || points.size() > 50 || !std::all_of(points.begin(), points.end(), Valid)) return {};
  if (std::adjacent_find(points.begin(), points.end()) != points.end()) return {};
  if (options.heading && (!std::isfinite(*options.heading) || *options.heading < 0 || *options.heading >= 360)) return {};
  Json root(json_object(), json_decref);
  auto * locations = json_array();
  for (auto const & p : points)
    json_array_append_new(locations, json_pack("{s:f,s:f,s:s}", "lat", p.lat, "lon", p.lon, "type", "break"));
  if (options.heading) json_object_set_new(json_array_get(locations, 0), "heading", json_real(*options.heading));
  json_object_set_new(root.get(), "locations", locations);
  json_object_set_new(root.get(), "costing", json_string("auto"));
  auto * costing = json_pack("{s:f,s:f,s:f}", "use_tolls", options.avoidTolls ? 0.0 : 0.5,
                            "use_highways", options.avoidMotorways ? 0.0 : 0.5, "use_ferry", options.avoidFerries ? 0.0 : 0.5);
  if (options.avoidTolls) json_object_set_new(costing, "use_tolls", json_integer(0));
  if (options.avoidMotorways) json_object_set_new(costing, "use_highways", json_integer(0));
  if (options.avoidFerries) json_object_set_new(costing, "use_ferry", json_integer(0));
  if (options.hardExclusions)
  {
    if (options.avoidTolls) json_object_set_new(costing, "exclude_tolls", json_true());
    if (options.avoidMotorways) json_object_set_new(costing, "exclude_highways", json_true());
    if (options.avoidFerries) json_object_set_new(costing, "exclude_ferries", json_true());
  }
  json_object_set_new(root.get(), "costing_options", json_pack("{s:o}", "auto", costing));
  json_object_set_new(root.get(), "units", json_string("kilometers"));
  auto const lang = options.language.substr(0, options.language.find_first_of("_-"));
  json_object_set_new(root.get(), "language", json_string(lang == "fr" ? "fr-FR" : "en-US"));
  json_object_set_new(root.get(), "shape_format", json_string("polyline6"));
  std::unique_ptr<char, decltype(&free)> dumped(json_dumps(root.get(), JSON_COMPACT), free);
  return dumped ? dumped.get() : "";
}

bool DecodePolyline6(std::string const & encoded, std::vector<Point> & points)
{
  points.clear();
  if (encoded.size() > 8000000) return false;
  size_t pos = 0;
  auto read = [&](int64_t & total)
  {
    uint64_t value = 0; unsigned shift = 0; unsigned byte;
    do
    {
      if (pos >= encoded.size() || shift > 30) return false;
      auto const character = static_cast<unsigned char>(encoded[pos++]);
      if (character < 63 || character > 126) return false;
      byte = character - 63;
      value |= uint64_t(byte & 31) << shift;
      shift += 5;
    } while (byte >= 32);
    total += value & 1 ? -static_cast<int64_t>((value >> 1) + 1) : static_cast<int64_t>(value >> 1);
    return true;
  };
  int64_t lat = 0, lon = 0;
  while (pos < encoded.size())
  {
    if (!read(lat) || !read(lon)) return false;
    Point const point{lat / 1e6, lon / 1e6};
    if (!Valid(point) || points.size() == 500000) return false;
    points.push_back(point);
  }
  return points.size() >= 2;
}

Response ParseResponse(std::string const & body, Options const & options)
{
  Response result;
  auto fail = [&](Error error) { result.error = error; result.legs.clear(); return result; };
  if (body.size() > 16000000) return result;
  json_error_t error;
  Json root(json_loadb(body.data(), body.size(), JSON_REJECT_DUPLICATES, &error), json_decref);
  auto * trip = json_object_get(root.get(), "trip");
  auto * status = json_object_get(trip, "status");
  auto * legs = json_object_get(trip, "legs");
  if (!json_is_integer(status) || json_integer_value(status) != 0 || String(json_object_get(trip, "units")) != "kilometers" ||
      !json_is_array(legs) || json_array_size(legs) == 0 || json_array_size(legs) > 49) return result;
  if (Warnings(root.get()) || Warnings(trip)) return fail(Error::ServerWarning);
  auto e = Summary(json_object_get(trip, "summary"), options, result.seconds, result.meters);
  if (e != Error::None) return fail(e);
  size_t index; json_t * item;
  double seconds = 0, meters = 0;
  size_t pointCount = 0;
  json_array_foreach(legs, index, item)
  {
    Leg leg;
    if (Warnings(item)) return fail(Error::ServerWarning);
    e = Summary(json_object_get(item, "summary"), options, leg.seconds, leg.meters);
    if (e != Error::None) return fail(e);
    if (!DecodePolyline6(String(json_object_get(item, "shape")), leg.shape)) return fail(Error::InvalidResponse);
    pointCount += leg.shape.size();
    if (pointCount > 500000 || (!result.legs.empty() && result.legs.back().shape.back() != leg.shape.front())) return fail(Error::InvalidResponse);
    auto * maneuvers = json_object_get(item, "maneuvers");
    if (!json_is_array(maneuvers) || json_array_size(maneuvers) < 2) return fail(Error::InvalidResponse);
    size_t j; json_t * maneuver;
    size_t previousEnd = 0;
    double maneuverTime = 0;
    json_array_foreach(maneuvers, j, maneuver)
    {
      Maneuver m;
      size_t type;
      if (!Integer(maneuver, "type", type) || type > 100 || !Integer(maneuver, "begin_shape_index", m.begin) ||
          !Integer(maneuver, "end_shape_index", m.end) || m.begin > m.end || m.end >= leg.shape.size() ||
          m.begin != previousEnd || !Number(maneuver, "time", m.seconds)) return fail(Error::InvalidResponse);
      m.type = static_cast<unsigned>(type);
      size_t exit = 0;
      if (json_object_get(maneuver, "roundabout_exit_count") && !Integer(maneuver, "roundabout_exit_count", exit)) return fail(Error::InvalidResponse);
      m.exit = static_cast<unsigned>(exit);
      m.name = Names(json_object_get(maneuver, "street_names"));
      auto * sign = json_object_get(maneuver, "sign");
      m.exitNumber = Names(json_object_get(sign, "exit_number_elements"), true);
      m.destination = Names(json_object_get(sign, "exit_toward_elements"), true);
      m.destinationRef = Names(json_object_get(sign, "exit_branch_elements"), true);
      m.toll = json_is_true(json_object_get(maneuver, "toll"));
      m.highway = json_is_true(json_object_get(maneuver, "highway"));
      if (options.avoidTolls && m.toll) return fail(Error::Toll);
      if (options.avoidMotorways && m.highway) return fail(Error::Restriction);
      previousEnd = m.end; maneuverTime += m.seconds;
      if (!std::isfinite(maneuverTime)) return fail(Error::InvalidResponse);
      leg.maneuvers.push_back(std::move(m));
    }
    auto const & departure = leg.maneuvers.front();
    auto const & arrival = leg.maneuvers.back();
    if (departure.type < 1 || departure.type > 3 || arrival.type < 4 || arrival.type > 6 ||
        arrival.begin != leg.shape.size() - 1 || arrival.end != arrival.begin || arrival.seconds != 0 ||
        std::abs(maneuverTime - leg.seconds) > std::max(1.0, leg.seconds * 0.001)) return fail(Error::InvalidResponse);
    seconds += leg.seconds; meters += leg.meters;
    if (!std::isfinite(seconds) || !std::isfinite(meters)) return fail(Error::InvalidResponse);
    result.legs.push_back(std::move(leg));
  }
  if (std::abs(seconds - result.seconds) > std::max(1.0, seconds * 0.001) ||
      std::abs(meters - result.meters) > std::max(10.0, meters * 0.001)) return fail(Error::InvalidResponse);
  result.error = Error::None;
  return result;
}
}  // namespace routing::valhalla
