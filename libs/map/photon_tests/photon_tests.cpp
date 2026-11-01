#include "map/photon_api.hpp"
#include "map/photon_search_state.hpp"

#include <iostream>
#include <stdexcept>

namespace
{
void Require(bool condition, char const * message)
{
  if (!condition)
    throw std::runtime_error(message);
}

std::string Feature(std::string const & properties, std::string const & coordinates = "[2.3604077,48.8554814]")
{
  return R"({"type":"FeatureCollection","features":[{"type":"Feature","properties":)" + properties +
         R"(,"geometry":{"type":"Point","coordinates":)" + coordinates + "}}]}";
}

void Parsing()
{
  auto address = photon::ParseResponse(
      Feature(R"({"housenumber":"10","street":"Rue de Rivoli","city":"Paris","postcode":"75004","country":"France"})"));
  Require(address.m_status == photon::Status::Ready && address.m_results.size() == 1, "numbered address");
  auto const & place = address.m_results.front();
  Require(place.m_name == "10 Rue de Rivoli", "unnamed addresses need a readable title");
  Require(place.m_address == "10 Rue de Rivoli, 75004 Paris, France", "address formatting");
  Require(place.m_position.m_lat == 48.8554814 && place.m_position.m_lon == 2.3604077, "GeoJSON longitude comes first");
  Require(photon::ParseResponse(Feature(R"({"name":"École 東京"})")).m_results.front().m_name == "École 東京",
          "Unicode names");
  Require(!photon::ParseResponse(Feature("{}")).m_results.front().m_name.empty(), "missing address and name");
  Require(photon::ParseResponse(Feature("{}", "[0,0]")).m_status == photon::Status::Ready,
          "zero coordinates are valid");
  for (auto const * coordinates : {"[200,20]", "[2,-91]", "[2]", "[null,10]", "[\"2\",10]"})
    Require(photon::ParseResponse(Feature("{}", coordinates)).m_status == photon::Status::Unavailable,
            "invalid coordinates");
  for (auto const * body :
       {"not json", "{}", "{\"features\":[]}", "{\"type\":\"FeatureCollection\",\"features\":null}"})
    Require(photon::ParseResponse(body).m_status == photon::Status::Unavailable, "malformed response");
  Require(photon::ParseResponse(R"({"type":"FeatureCollection","features":[]})").m_status == photon::Status::Empty,
          "successful empty response");
}

void Requests()
{
  auto const url = photon::MakeUrl("https://example.org/photon", "café & boulangerie", "fr-FR", {48.85, 2.35});
  Require(url.find("/photon/api/?q=caf%C3%A9%20%26%20boulangerie&limit=10&lang=fr") != std::string::npos,
          "query encoding and locale");
  Require(url.find("&lat=48.85&lon=2.35") != std::string::npos, "bias coordinates");
  for (auto const * endpoint : {"http://example.org", "example.org", "https://", "https://user:secret@example.org",
                                "https://example.org?q=x", "https://example.org/#x"})
    Require(!photon::IsValidEndpoint(endpoint), "reject unsafe endpoint");
  Require(photon::DecodeResponse(500, "{}").m_status == photon::Status::Unavailable,
          "HTTP error is not an empty search");
  Require(photon::DecodeResponse(429, "", "120").m_retryAfter.count() == 120, "Retry-After seconds");
  Require(photon::DecodeResponse(503, "").m_status == photon::Status::Throttled, "service unavailable cooldown");
  Require(photon::DecodeResponse(429, "", "Wed, 21 Oct 2099 12:00:00 GMT").m_retryAfter.count() > 86400,
          "date-form cooldown");
}

void Coordination()
{
  photon::SearchState<int> session;
  Require(session.UpdateOffline({1, 2}, false), "offline results publish immediately");
  Require(session.Snapshot() == std::vector<int>({1, 2}) && !session.Complete(), "offline does not wait for network");
  auto oldSnapshot = session.Snapshot();
  session.UpdateOnline({9}, photon::Status::Ready);
  Require(session.Snapshot() == std::vector<int>({9, 1, 2}), "online group comes first");
  Require(oldSnapshot == std::vector<int>({1, 2}), "previous snapshot stays stable");
  session.UpdateOffline({1, 2, 3}, true);
  Require(session.Complete(), "both engines finish");
  session.m_active = false;
  Require(!session.UpdateOnline({99}, photon::Status::Ready), "cancelled network response ignored");
  Require(!session.UpdateOffline({99}, true), "cancelled offline response ignored");
  Require(session.Snapshot() == std::vector<int>({9, 1, 2, 3}), "cancel does not lose selectable rows");
  photon::SearchState<int> failed;
  failed.UpdateOnline({}, photon::Status::Unavailable);
  failed.UpdateOffline({3}, true);
  Require(failed.Complete() && failed.Snapshot() == std::vector<int>({3}), "network errors preserve offline results");

  using namespace std::chrono_literals;
  photon::RequestThrottle throttle;
  auto now = photon::RequestThrottle::Clock::time_point{} + 1h;
  Require(throttle.Delay(now) == 0s, "first request allowed");
  throttle.Started(now);
  Require(throttle.Delay(now + 600ms) == 400ms, "typing debounce cannot bypass one request per second");
  Require(throttle.Delay(now + 1s) == 0s, "rate limit expires");
  throttle.Throttle(now, 120s);
  Require(throttle.IsThrottled(now + 119s) && !throttle.IsThrottled(now + 120s), "server cooldown respected");
}
}  // namespace

int main()
{
  try
  {
    Parsing();
    Requests();
    Coordination();
    std::cout << "Photon parsing, HTTP handling, cancellation, ordering and throttling checks passed.\n";
  }
  catch (std::exception const & error)
  {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
