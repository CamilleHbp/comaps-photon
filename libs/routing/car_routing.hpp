#pragma once

#include <atomic>
#include <string>

namespace routing
{
// JNI mirrors these values. They are independent of the travel-mode enum.
enum class CarBackend { CoMaps = 0, Valhalla = 1 };
enum class CarFallbackReason
{
  None = 0, Service, Timeout, RateLimit, InvalidResponse, Toll, UnknownRestriction,
  ServerWarning, UnsupportedOptions
};
enum class CarRoutingFailure { None = 0, Service, MissingMaps, NoTollFreeRoute, RouteNotFound };
inline std::string DebugPrint(CarBackend value) { return std::to_string(static_cast<int>(value)); }
inline std::string DebugPrint(CarFallbackReason value) { return std::to_string(static_cast<int>(value)); }
inline std::string DebugPrint(CarRoutingFailure value) { return std::to_string(static_cast<int>(value)); }

// Valhalla routes are followed by the CoMaps navigation engine; rerouting
// falls back to CoMaps for the rest of the journey once the service fails.
inline constexpr bool kValhallaNavigationValidated = true;
// Public FOSSGIS service, used until the user selects another endpoint.
inline constexpr char kDefaultValhallaEndpoint[] = "https://valhalla1.openstreetmap.de/route";

struct CarRouteInfo
{
  bool managed = false;
  CarBackend backend = CarBackend::CoMaps;
  CarFallbackReason fallback = CarFallbackReason::None;
  bool CanNavigate() const { return backend != CarBackend::Valhalla || kValhallaNavigationValidated; }
  bool HasNativeRoadMetadata() const { return backend == CarBackend::CoMaps; }
};

struct CarRoutingConfig
{
  bool online = true;
  std::string endpoint;
  bool hardExclusions = false;
};

struct CarRoutingState
{
  std::atomic<CarRoutingFailure> failure{CarRoutingFailure::None};
  std::atomic<CarFallbackReason> reason{CarFallbackReason::None};
};

// HTTPS only; no embedded credentials, query parameters, fragments or whitespace.
// Accept service roots and action URLs copied from a service's web interface.
std::string NormalizeValhallaEndpoint(std::string endpoint);
}
