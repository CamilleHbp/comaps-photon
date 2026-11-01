#include "map/photon_api.hpp"
#include "platform/http_client.hpp"

#include <algorithm>
#include <cctype>

namespace photon
{
Response Search(std::string const & url)
{
  if (url.empty())
    return {};
  platform::HttpClient request(url);
  // A slow server should leave the offline results usable without a long wait.
  request.SetTimeout(5.0);
  request.SetRawHeader("Accept", "application/json");
  request.SetRawHeader("User-Agent", "CoMaps-Photon/1.0");
  request.LoadHeaders(true);
  if (!request.RunHttpRequest())
    return {};
  std::string retryAfter;
  for (auto const & [key, value] : request.GetHeaders())
  {
    auto normalized = key;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (normalized == "retry-after")
      retryAfter = value;
  }
  return DecodeResponse(request.ErrorCode(), request.ServerResponse(), retryAfter);
}
}  // namespace photon
