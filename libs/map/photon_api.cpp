#include "map/photon_api.hpp"

#include "coding/url.hpp"
#include "cppjansson/cppjansson.hpp"

#include <algorithm>
#include <charconv>
#include <ctime>
#include <iomanip>
#include <locale>
#include <sstream>

namespace photon
{
namespace
{
std::string String(json_t * object, char const * key)
{
  auto const * value = json_string_value(json_object_get(object, key));
  return value ? value : "";
}

void Append(std::string & text, std::string const & part, std::string const & separator = ", ")
{
  if (part.empty())
    return;
  if (!text.empty())
    text += separator;
  text += part;
}
}  // namespace

bool IsValidEndpoint(std::string const & endpoint)
{
  auto const parsed = url::Url::FromString(endpoint);
  return endpoint.starts_with("https://") && parsed.GetScheme() == "https" && !parsed.GetHost().empty() &&
         endpoint.find_first_of("?#@ \t\r\n") == std::string::npos;
}

std::string MakeUrl(std::string const & endpoint, std::string const & query, std::string const & language,
                    ms::LatLon const & bias)
{
  if (!IsValidEndpoint(endpoint))
    return {};
  // Photon accepts one language, not an Android locale such as fr_FR or fr-FR.
  auto const lang = language.substr(0, language.find_first_of("_-"));
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out.precision(8);
  out << url::Join(endpoint, "api/") << "?q=" << url::UrlEncode(query) << "&limit=10";
  if (!lang.empty())
    out << "&lang=" << url::UrlEncode(lang);
  out << "&lat=" << bias.m_lat << "&lon=" << bias.m_lon;
  return out.str();
}

Response ParseResponse(std::string const & body)
{
  Response response;
  json_error_t error;
  base::JSONPtr root(json_loads(body.c_str(), 0, &error));
  if (!root || String(root.get(), "type") != "FeatureCollection")
    return response;
  auto * features = json_object_get(root.get(), "features");
  if (!json_is_array(features))
    return response;

  size_t index;
  json_t * feature;
  json_array_foreach(features, index, feature)
  {
    if (response.m_results.size() == 10)
      break;
    auto * geometry = json_object_get(feature, "geometry");
    auto * properties = json_object_get(feature, "properties");
    auto * coordinates = json_object_get(geometry, "coordinates");
    if (String(feature, "type") != "Feature" || String(geometry, "type") != "Point" || !json_is_object(properties) ||
        !json_is_array(coordinates) || json_array_size(coordinates) < 2)
      continue;
    auto * x = json_array_get(coordinates, 0);
    auto * y = json_array_get(coordinates, 1);
    if (!json_is_number(x) || !json_is_number(y))
      continue;
    double const lon = json_number_value(x), lat = json_number_value(y);
    if (!(lon >= -180 && lon <= 180 && lat >= -90 && lat <= 90))
      continue;

    auto street = String(properties, "housenumber");
    Append(street, String(properties, "street"), " ");
    auto address = street;
    auto city = String(properties, "postcode");
    Append(city, String(properties, "city"), " ");
    Append(address, city);
    Append(address, String(properties, "state"));
    Append(address, String(properties, "country"));
    auto name = String(properties, "name");
    if (name.empty())
      name = street.empty() ? address : street;
    if (name.empty())
      name = std::to_string(lat) + ", " + std::to_string(lon);

    response.m_results.push_back({{lat, lon}, std::move(name), std::move(address)});
  }
  response.m_status = response.m_results.empty() ? Status::Empty : Status::Ready;
  // An array of broken features is not a successful empty search.
  if (json_array_size(features) && response.m_results.empty())
    response.m_status = Status::Unavailable;
  return response;
}

Response DecodeResponse(int httpCode, std::string const & body, std::string const & retryAfter)
{
  if (httpCode == 429 || httpCode == 503)
  {
    Response response;
    response.m_status = Status::Throttled;
    if (!retryAfter.empty())
    {
      int seconds = 0;
      auto const parsed = std::from_chars(retryAfter.data(), retryAfter.data() + retryAfter.size(), seconds);
      if (parsed.ec == std::errc{} && parsed.ptr == retryAfter.data() + retryAfter.size() && seconds > 0)
        response.m_retryAfter = std::chrono::seconds(seconds);
      else
      {
        // Retry-After can be an HTTP date instead of a number of seconds.
        std::tm date{};
        std::istringstream input(retryAfter);
        input.imbue(std::locale::classic());
        input >> std::get_time(&date, "%a, %d %b %Y %H:%M:%S GMT");
        using namespace std::chrono;
        year_month_day const day{year{date.tm_year + 1900}, month{static_cast<unsigned>(date.tm_mon + 1)},
                                 std::chrono::day{static_cast<unsigned>(date.tm_mday)}};
        if (!input.fail() && day.ok())
        {
          auto const until =
              sys_days(day) + hours(date.tm_hour) + minutes(date.tm_min) + std::chrono::seconds(date.tm_sec);
          response.m_retryAfter =
              std::max(std::chrono::seconds(1), ceil<std::chrono::seconds>(until - system_clock::now()));
        }
      }
    }
    return response;
  }
  if (httpCode != 200)
    return {};
  return ParseResponse(body);
}
}  // namespace photon
