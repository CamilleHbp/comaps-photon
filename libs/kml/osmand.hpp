#pragma once
#include "kml/types.hpp"
#include <string_view>
#include <vector>

namespace kml::osmand
{
inline constexpr char kGroup[] = "osmand:group";
inline constexpr char kIdentity[] = "osmand:identity";
inline constexpr char kColor[] = "osmand:color";
inline constexpr char kIcon[] = "osmand:icon";
struct Import
{
  std::vector<FileData> groups;
  size_t skipped = 0;
  bool recognized = false;
};
// Dedicated favorites parser; generic GPX tracks retain their existing import behavior.
Import Parse(std::string_view xml, std::string const & ungroupedName);
BookmarkIcon MapIcon(std::string const & icon);
}
