#include "routing/routes_builder/routes_builder.hpp"
#include "routing/routing_options.hpp"
#include "platform/platform.hpp"
#include "geometry/mercator.hpp"
#include "cppjansson/cppjansson.hpp"
#include <gflags/gflags.h>
#include <chrono>
#include <filesystem>
#include <iostream>

DEFINE_string(cases, "", "JSON manifest with id and points [[lat,lon], ...].");
DEFINE_string(output, "", "Output JSON file.");
DEFINE_string(maps, "", "Directory containing the downloaded MWM files.");
DEFINE_string(resources, "", "CoMaps data resource directory.");
DEFINE_bool(avoid_tolls, false, "Reject reported toll roads.");
DEFINE_bool(no_leaps, false, "Use Joints instead of long-distance leaps for comparison.");

int main(int argc, char ** argv)
{
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_cases.empty() || FLAGS_output.empty() || FLAGS_maps.empty() || FLAGS_resources.empty()) return 2;
  if (std::filesystem::exists(FLAGS_output))
  {
    std::cerr << "Output already exists; choose a new capture file.\n";
    return 2;
  }
  if (!std::filesystem::is_directory(FLAGS_maps)) return 2;
  bool hasVersionDirectory = false;
  for (auto const & entry : std::filesystem::directory_iterator(FLAGS_maps))
  {
    if (!entry.is_directory()) continue;
    auto const name = entry.path().filename().string();
    if (name.empty() || name.find_first_not_of("0123456789") != std::string::npos) continue;
    for (auto const & file : std::filesystem::directory_iterator(entry.path()))
      if (file.path().extension() == ".mwm") hasVersionDirectory = true;
  }
  if (!hasVersionDirectory)
  {
    std::cerr << "--maps must contain numeric version directories with MWM files (for example maps/261004/).\n";
    return 2;
  }
  json_error_t error;
  base::JSONPtr cases(json_load_file(FLAGS_cases.c_str(), JSON_REJECT_DUPLICATES, &error));
  if (!cases || !json_is_array(cases.get())) return 2;
  GetPlatform().SetWritableDirForTests(FLAGS_maps);
  auto const settingsDirectory = std::filesystem::path(FLAGS_output).parent_path() / "comaps-settings";
  std::filesystem::create_directories(settingsDirectory);
  GetPlatform().SetSettingsDir(settingsDirectory.string());
  GetPlatform().SetResourceDir(FLAGS_resources);
  routing::RoutingOptionSetter preferences(FLAGS_avoid_tolls ? routing::RoutingOptions::Toll : 0, routing::VehicleType::Car);
  routing::routes_builder::RoutesBuilder builder(1);
  base::JSONPtr output(json_array());
  size_t index; json_t * item;
  json_array_foreach(cases.get(), index, item)
  {
    auto * id = json_object_get(item, "id");
    auto * input = json_object_get(item, "points");
    if (!json_is_string(id) || !json_is_array(input) || json_array_size(input) < 2) return 2;
    std::vector<m2::PointD> points;
    size_t j; json_t * point;
    json_array_foreach(input, j, point)
    {
      if (!json_is_array(point) || json_array_size(point) != 2 || !json_is_number(json_array_get(point,0)) ||
          !json_is_number(json_array_get(point,1))) return 2;
      double const lat=json_number_value(json_array_get(point,0)), lon=json_number_value(json_array_get(point,1));
      if (!(lat >= -85 && lat <= 85 && lon >= -180 && lon <= 180)) return 2;
      points.push_back(mercator::FromLatLon(lat,lon));
    }
    routing::routes_builder::RoutesBuilder::Params params(routing::VehicleType::Car, std::move(points));
    params.m_disableLeaps = FLAGS_no_leaps;
    params.m_timeoutSeconds = 120;
    auto const start=std::chrono::steady_clock::now();
    auto result=builder.ProcessTask(params);
    auto const elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
    auto * record=json_pack("{s:s,s:s,s:b,s:b,s:f}", "id",json_string_value(id), "code",routing::ToString(result.m_code).c_str(),
                           "avoid_tolls",FLAGS_avoid_tolls,"no_leaps",FLAGS_no_leaps,"latency_seconds",elapsed);
    if (result.m_code == routing::RouterResultCode::NoError && !result.m_routes.empty())
    {
      auto const & route=result.m_routes.front();
      json_object_set_new(record,"seconds",json_real(route.m_eta));
      json_object_set_new(record,"meters",json_real(route.m_distance));
      json_object_set_new(record,"has_toll",json_boolean(route.m_hasToll));
      json_object_set_new(record,"accepted",json_boolean(!FLAGS_avoid_tolls || !route.m_hasToll));
      auto * geometry=json_array();
      for (auto const & p : route.GetWaypoints()) json_array_append_new(geometry,json_pack("[f,f]",p.m_lon,p.m_lat));
      json_object_set_new(record,"coordinates",geometry);
    }
    else json_object_set_new(record,"accepted",json_false());
    json_array_append_new(output.get(),record);
    if (json_dump_file(output.get(), FLAGS_output.c_str(), JSON_INDENT(2)) != 0) return 3;
  }
  return 0;
}
