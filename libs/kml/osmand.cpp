#include "kml/osmand.hpp"
#include "kml/color_parser.hpp"
#include "kml/serdes_common.hpp"
#include "coding/sha1.hpp"
#include "geometry/mercator.hpp"
#include "base/string_utils.hpp"
#include "base/timer.hpp"
#include <pugixml.hpp>
#include <cmath>
#include <iomanip>
#include <locale>
#include <map>
#include <sstream>
#include <stdexcept>

namespace kml::osmand
{
namespace
{
std::string_view Local(char const * name)
{
  std::string_view s(name);
  auto p = s.find(':');
  return p == s.npos ? s : s.substr(p + 1);
}
pugi::xml_node Child(pugi::xml_node node, std::string_view name)
{
  for (auto child : node.children())
    if (Local(child.name()) == name) return child;
  return {};
}
std::string Text(pugi::xml_node node, std::string_view name)
{
  return Child(node, name).text().as_string();
}
std::string Raw(pugi::xml_node node)
{
  std::ostringstream out;
  node.print(out, "", pugi::format_raw);
  return out.str();
}
std::string Identity(std::string const & group, std::string const & name, double lat, double lon)
{
  std::ostringstream out;
  out.imbue(std::locale::classic());
  out << group.size() << ':' << group << name.size() << ':' << name << std::fixed << std::setprecision(7)
      << ':' << lat << ':' << lon;
  auto digest = coding::SHA1::CalculateForString(out.str());
  out.str("");
  for (auto b : digest) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(b);
  return out.str();
}
}
BookmarkIcon MapIcon(std::string const & icon)
{
  static std::map<std::string, BookmarkIcon> const icons = {
    {"restaurants", BookmarkIcon::Food}, {"amenity_restaurant", BookmarkIcon::Food},
    {"amenity_cafe", BookmarkIcon::Cafe}, {"amenity_fast_food", BookmarkIcon::FastFood},
    {"amenity_bar", BookmarkIcon::Bar}, {"amenity_pub", BookmarkIcon::Pub},
    {"tourism_hotel", BookmarkIcon::Hotel}, {"tourism_museum", BookmarkIcon::Museum},
    {"gallery", BookmarkIcon::Art}, {"tourism_viewpoint", BookmarkIcon::Viewpoint},
    {"wood", BookmarkIcon::Park}, {"forest", BookmarkIcon::Park}, {"waterfall", BookmarkIcon::Water},
    {"volcano", BookmarkIcon::Mountain}, {"halt", BookmarkIcon::Transport},
    {"amenity_ferry_terminal", BookmarkIcon::Transport}, {"dojo", BookmarkIcon::Sport},
    {"leisure_sports_centre", BookmarkIcon::Sport}, {"leisure_beach_resort", BookmarkIcon::Swim},
    {"attraction", BookmarkIcon::Sights}, {"historic_castle", BookmarkIcon::Sights},
    {"historic_archaeological_site", BookmarkIcon::Sights}};
  auto it = icons.find(icon);
  if (it != icons.end()) return it->second;
  if (icon.starts_with("shop_") || icon == "retail" || icon == "stationery") return BookmarkIcon::Shop;
  return BookmarkIcon::None;
}
Import Parse(std::string_view xml, std::string const & ungroupedName)
{
  if (xml.size() > 32 * 1024 * 1024 || xml.find("<!DOCTYPE") != xml.npos || xml.find("<!ENTITY") != xml.npos)
    throw std::runtime_error("Unsupported or oversized XML");
  pugi::xml_document doc;
  if (!doc.load_buffer(xml.data(), xml.size())) throw std::runtime_error("Malformed GPX");
  auto root = doc.document_element();
  if (Local(root.name()) != "gpx") throw std::runtime_error("Not a GPX file");
  Import result;
  result.recognized = std::string(root.attribute("creator").value()).find("OsmAnd") != std::string::npos;
  for (auto attr : root.attributes())
    if (std::string_view(attr.name()).starts_with("xmlns") && std::string_view(attr.value()).find("osmand.net") != std::string_view::npos)
      result.recognized = true;
  if (!result.recognized) return result;
  std::map<std::string, pugi::xml_node> defaults;
  for (auto g : Child(Child(root, "extensions"), "points_groups").children())
    if (Local(g.name()) == "group") defaults[g.attribute("name").value()] = g;
  std::map<std::string, size_t> groups;
  size_t count = 0;
  for (auto point : root.children())
  {
    if (Local(point.name()) != "wpt") continue;
    if (++count > 100000) throw std::runtime_error("Too many places");
    double lat, lon;
    std::string_view latText(point.attribute("lat").value()), lonText(point.attribute("lon").value());
    if (latText.empty() || lonText.empty() ||
        latText.find_first_not_of("0123456789+-.eE") != latText.npos ||
        lonText.find_first_not_of("0123456789+-.eE") != lonText.npos ||
        !strings::to_double(point.attribute("lat").value(), lat) ||
        !strings::to_double(point.attribute("lon").value(), lon) || !std::isfinite(lat) || !std::isfinite(lon) ||
        lat < -90 || lat > 90 || lon < -180 || lon > 180)
    {
      ++result.skipped;
      continue;
    }
    auto group = Text(point, "type");
    auto def = defaults[group];
    if (!groups.count(group))
    {
      groups[group] = result.groups.size();
      result.groups.emplace_back();
      auto & category = result.groups.back().m_categoryData;
      SetDefaultStr(category.m_name, group.empty() ? ungroupedName : group);
      category.m_properties[kGroup] = "group:" + group;
      category.m_properties[kColor] = def.attribute("color").value();
      category.m_properties[kIcon] = def.attribute("icon").value();
      category.m_properties["osmand:group_xml"] = Raw(def);
      category.m_visible = !def.attribute("hidden").as_bool(false) && def.attribute("visible").as_bool(true);
    }
    BookmarkData bookmark;
    auto name = Text(point, "name");
    SetDefaultStr(bookmark.m_name, name.empty() ? std::to_string(lat) + ", " + std::to_string(lon) : name);
    bookmark.m_customName = bookmark.m_name;
    bookmark.m_point = mercator::FromLatLon(lat, lon);
    auto & props = bookmark.m_properties;
    props[kIdentity] = Identity(group, name, lat, lon);
    props[kGroup] = "group:" + group;
    props["osmand:original_name"] = name;
    props["osmand:latitude"] = point.attribute("lat").value();
    props["osmand:longitude"] = point.attribute("lon").value();
    props["osmand:elevation"] = Text(point, "ele");
    auto time = Text(point, "time");
    props["osmand:time"] = time;
    auto timestamp = base::StringToTimestamp(time);
    if (timestamp != base::INVALID_TIME_STAMP) bookmark.m_timestamp = TimestampClock::from_time_t(timestamp);
    auto extensions = Child(point, "extensions");
    props["osmand:extensions_xml"] = Raw(extensions);
    props["osmand:point_xml"] = Raw(point);
    for (auto attr : root.attributes())
      if (std::string_view(attr.name()).starts_with("xmlns"))
        props["osmand:namespace:" + std::string(attr.name())] = attr.value();
    for (auto e : extensions.children()) props["osmand:field:" + std::string(e.name())] = e.text().as_string();
    auto color = Text(extensions, "color");
    if (color.empty()) color = def.attribute("color").value();
    auto icon = Text(extensions, "icon");
    if (icon.empty()) icon = def.attribute("icon").value();
    props[kColor] = color;
    props[kIcon] = icon;
    bookmark.m_icon = MapIcon(icon);
    if (auto rgba = ParseHexColor(color))
    {
      bookmark.m_color.m_rgba = *rgba;
      bookmark.m_color.m_predefinedColor = MapPredefinedColor(*rgba);
    }
    else bookmark.m_color.m_predefinedColor = PredefinedColor::Red;
    std::string description = Text(point, "desc");
    auto append = [&](std::string const & value) {
      if (!value.empty()) { if (!description.empty()) description += "\n\n"; description += value; }
    };
    auto comment = Text(point, "cmt");
    if (comment != description) append(comment);
    for (auto const & [field, label] : std::map<std::string, std::string>{
        {"address", "Address"}, {"website", "Website"}, {"phone", "Phone"},
        {"amenity_opening_hours", "Opening hours"}, {"osm_tag_cuisine", "Cuisine"},
        {"collapsable_cuisine", "Cuisine"}, {"collapsable_diet", "Diet"}})
    {
      auto value = Text(extensions, field);
      if (!value.empty()) append(label + ": " + value);
    }
    for (auto e : extensions.children())
    {
      std::string field(Local(e.name()));
      if (field.starts_with("osm_tag_diet_")) append(field.substr(8) + ": " + e.text().as_string());
    }
    SetDefaultStr(bookmark.m_description, description);
    std::erase_if(props, [](auto const & entry) { return entry.second.empty(); });
    result.groups[groups[group]].m_bookmarksData.push_back(std::move(bookmark));
  }
  return result;
}
}
