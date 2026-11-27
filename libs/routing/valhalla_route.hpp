#pragma once

#include "routing/valhalla_api.hpp"

namespace routing
{
class Route;
namespace valhalla
{
// Converts a validated response atomically. Unsupported maneuvers fail closed.
// Android may use this adapter for opt-in previews; navigation stays locked by
// kValhallaNavigationValidated until the quality and navigation gates pass.
bool ConvertRoute(Response const & response, Route & route);
}
}
