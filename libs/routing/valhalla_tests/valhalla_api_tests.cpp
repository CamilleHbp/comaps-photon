#include "routing/valhalla_api.hpp"
#include <jansson.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>

using namespace routing::valhalla;
using Json = std::unique_ptr<json_t, decltype(&json_decref)>;
void Require(bool ok, char const * message) { if (!ok) throw std::runtime_error(message); }
std::string Dump(json_t * value)
{
  std::unique_ptr<char, decltype(&free)> text(json_dumps(value, JSON_COMPACT), free);
  return text.get();
}
int main(int argc, char ** argv)
{
  try
  {
    Require(argc >= 2, "fixture required");
    json_error_t error;
    Json fixture(json_load_file(argv[1], 0, &error), json_decref);
    Require(bool(fixture), "fixture JSON");
    Options options; options.avoidTolls = true; options.language = "fr";
    auto request = MakeRequest({{48.85, 2.35}, {48.852, 2.351}}, options);
    Json actual(json_loads(request.c_str(), 0, &error), json_decref);
    Require(json_equal(actual.get(), json_object_get(fixture.get(), "request")), "shared request parity");
    auto * response = json_object_get(fixture.get(), "response");
    auto parsed = ParseResponse(Dump(response), options);
    Require(parsed.error == Error::None && parsed.seconds == 180 && parsed.meters == 300, "totals");
    Require(parsed.legs[0].shape.size() == 4 && parsed.legs[0].shape.front() == Point{48.85,2.35}, "polyline6");
    Require(parsed.legs[0].maneuvers[1].type == 10, "maneuver");
    auto * summary = json_object_get(json_object_get(response, "trip"), "summary");
    json_object_set_new(summary, "has_toll", json_true());
    Require(ParseResponse(Dump(response), options).error == Error::Toll, "toll rejection");
    json_object_del(summary, "has_toll");
    Require(ParseResponse(Dump(response), options).error == Error::RestrictionUnknown, "unknown toll rejection");
    json_object_set_new(summary, "has_toll", json_false());
    json_object_set_new(response, "warnings", json_pack("[{s:i},{s:i}]", "code", 208, "code", 401));
    Require(ParseResponse(Dump(response), options).error == Error::None, "informational warnings");
    json_object_set_new(summary, "has_toll", json_true());
    Require(ParseResponse(Dump(response), options).error == Error::Toll, "toll despite informational warnings");
    json_object_set_new(summary, "has_toll", json_false());
    json_object_set_new(response, "warnings", json_pack("[{s:i}]", "code", 100));
    Require(ParseResponse(Dump(response), options).error == Error::ServerWarning, "server warning");
    json_object_set_new(response, "warnings", json_string("bad"));
    Require(ParseResponse(Dump(response), options).error == Error::ServerWarning, "malformed warnings");
    json_object_del(response, "warnings");
    json_object_set_new(summary, "length", json_real(1e308));
    Require(ParseResponse(Dump(response), options).error == Error::InvalidResponse, "unit conversion overflow");
    for (auto const * bad : {"", "?", "~~~~~~~?", "\001"})
    { std::vector<Point> points; Require(!DecodePolyline6(bad, points), "malformed polyline"); }
    Require(MakeRequest({{0,0},{0,0}},options).empty(), "equal points");
    Require(MakeRequest({{90,0},{0,0}},options).empty(), "coordinate range");
    if (argc >= 3)
    {
      std::ifstream stream(argv[2]); std::string body{std::istreambuf_iterator<char>(stream), {}};
      options.avoidTolls = argc < 4 || std::string(argv[3]) != "allow-tolls";
      auto live = ParseResponse(body, options);
      Require(live.error == Error::None, "captured live response");
      std::cout << "Live response: " << live.meters << " m, " << live.seconds << " s\n";
    }
    std::cout << "Valhalla API checks passed\n";
    return 0;
  }
  catch (std::exception const & e) { std::cerr << e.what() << '\n'; return 1; }
}
