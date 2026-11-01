#include "testing/testing.hpp"
#include "kml/osmand.hpp"
#include "kml/serdes.hpp"
#include "kml/serdes_common.hpp"
#include "coding/file_reader.hpp"
#include "coding/writer.hpp"
#include <cstdlib>

namespace
{
std::string const kFavorites = R"(<g:gpx xmlns:g="http://www.topografix.com/GPX/1/1" xmlns:o="https://osmand.net" creator="OsmAnd">
<g:wpt lat="48.1" lon="2.3"><g:name>Café 東京</g:name><g:type>Food</g:type><g:time>2020-01-02T03:04:05Z</g:time><g:ele>52</g:ele><g:desc>Note</g:desc><g:cmt>Comment</g:cmt><g:extensions><o:address>Street 1</o:address><o:website>https://example.org</o:website><o:custom>keep me</o:custom></g:extensions></g:wpt>
<g:wpt lat="49" lon="3"><g:name>Override</g:name><g:type>Food</g:type><g:extensions><o:color>#FF0000</o:color><o:icon>amenity_cafe</o:icon></g:extensions></g:wpt>
<g:wpt lat="0" lon="0"><g:name>Zero</g:name></g:wpt>
<g:wpt lon="2"><g:name>Missing</g:name></g:wpt><g:wpt lat="oops" lon="2"/><g:wpt lat="91" lon="2"/><g:wpt lat="NaN" lon="2"/>
<g:extensions><o:points_groups><g:group name="Food" color="#88E030" icon="restaurants" hidden="true"/></o:points_groups></g:extensions></g:gpx>)";
}
UNIT_TEST(OsmAnd_GroupsDefaultsCoordinatesAndMetadata)
{
  auto parsed = kml::osmand::Parse(kFavorites, "Imported places");
  TEST(parsed.recognized, ());
  TEST_EQUAL(parsed.skipped, 4, ());
  TEST_EQUAL(parsed.groups.size(), 2, ());
  auto const & group = parsed.groups[0];
  TEST_EQUAL(group.m_bookmarksData.size(), 2, ());
  TEST(!group.m_categoryData.m_visible, ());
  auto const & first = group.m_bookmarksData[0];
  TEST_EQUAL(first.m_icon, kml::BookmarkIcon::Food, ());
  TEST_EQUAL(first.m_properties.at(kml::osmand::kColor), "#88E030", ());
  TEST_EQUAL(first.m_properties.at("osmand:elevation"), "52", ());
  TEST(first.m_timestamp != kml::Timestamp{}, ());
  TEST(first.m_properties.at("osmand:extensions_xml").find("keep me") != std::string::npos, ());
  auto description = kml::GetDefaultStr(first.m_description);
  TEST(description.find("Street 1") != std::string::npos, ());
  TEST(description.find("Comment") != std::string::npos, ());
  TEST_EQUAL(group.m_bookmarksData[1].m_icon, kml::BookmarkIcon::Cafe, ());
  TEST_EQUAL(group.m_bookmarksData[1].m_properties.at(kml::osmand::kColor), "#FF0000", ());
  TEST_EQUAL(kml::GetDefaultStr(parsed.groups[1].m_categoryData.m_name), "Imported places", ());
  std::string xml;
  MemWriter<std::string> writer(xml);
  kml::SerializerKml(group).Serialize(writer);
  kml::FileData restored;
  kml::DeserializerKml(restored).Deserialize(MemReader(xml));
  TEST_EQUAL(restored.m_bookmarksData[0].m_properties, first.m_properties, ());
  TEST_EQUAL(restored.m_bookmarksData[0].m_timestamp, first.m_timestamp, ());
}
UNIT_TEST(OsmAnd_RejectsMalformedAndKeepsGenericGPXSeparate)
{
  TEST(!kml::osmand::Parse("<gpx><wpt lat=\"1\" lon=\"2\"/></gpx>", "Other").recognized, ());
  bool failed = false;
  try { kml::osmand::Parse("<gpx>", "Other"); } catch (...) { failed = true; }
  TEST(failed, ());
  failed = false;
  try { kml::osmand::Parse("<!DOCTYPE gpx><gpx/>", "Other"); } catch (...) { failed = true; }
  TEST(failed, ());
}
UNIT_TEST(OsmAnd_PrivateBackup)
{
  // Optional local acceptance fixture. Never store private places in the repository.
  auto path = std::getenv("OSMAND_TEST_GPX");
  if (!path) return;
  std::string xml;
  FileReader(path).ReadAsString(xml);
  auto parsed = kml::osmand::Parse(xml, "Imported places");
  TEST_EQUAL(parsed.groups.size(), 4, ());
  size_t count = 0;
  for (auto const & group : parsed.groups) count += group.m_bookmarksData.size();
  TEST_EQUAL(count, 355, ());
  TEST_EQUAL(parsed.skipped, 0, ());
}
