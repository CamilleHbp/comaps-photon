#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"
#include "kml/osmand.hpp"
#include "kml/color_parser.hpp"
#include "kml/serdes_common.hpp"
#include "platform/platform.hpp"
#include "cppjansson/cppjansson.hpp"
#include <atomic>
#include <map>
#include <mutex>

namespace
{
std::mutex g_mutex;
std::map<int64_t, kml::osmand::Import> g_sessions;
std::atomic<int64_t> g_next{1};
jstring Json(JNIEnv * env, json_t * root)
{
  auto * text = json_dumps(root, JSON_COMPACT | JSON_ENSURE_ASCII);
  auto result = jni::ToJavaString(env, text ? text : "{}");
  free(text);
  json_decref(root);
  return result;
}
}
extern "C"
{
JNIEXPORT jlong JNICALL Java_app_organicmaps_sdk_bookmarks_data_OsmAndImport_nativePrepareUtf8(
    JNIEnv * env, jclass, jobjectArray documents, jbyteArray unnamed)
{
  try
  {
    kml::osmand::Import session;
    std::map<std::string, size_t> groups;
    size_t totalPlaces = 0;
    for (jsize i = 0; i < env->GetArrayLength(documents); ++i)
    {
      auto text = static_cast<jbyteArray>(env->GetObjectArrayElement(documents, i));
      auto parsed = kml::osmand::Parse(jni::ToNativeString(env, text), jni::ToNativeString(env, unnamed));
      env->DeleteLocalRef(text);
      if (!parsed.recognized) return -2;
      session.skipped += parsed.skipped;
      for (auto & group : parsed.groups)
      {
        totalPlaces += group.m_bookmarksData.size();
        if (totalPlaces > 100000) return -1;
        auto key = group.m_categoryData.m_properties.at(kml::osmand::kGroup);
        if (!groups.count(key))
        {
          groups[key] = session.groups.size();
          session.groups.push_back(std::move(group));
        }
        else
        {
          auto & target = session.groups[groups[key]].m_bookmarksData;
          for (auto & point : group.m_bookmarksData) target.push_back(std::move(point));
        }
      }
    }
    auto id = g_next++;
    std::lock_guard lock(g_mutex);
    g_sessions.emplace(id, std::move(session));
    return id;
  }
  catch (...) { return -1; }
}
JNIEXPORT jstring JNICALL Java_app_organicmaps_sdk_bookmarks_data_OsmAndImport_nativePreview(
    JNIEnv * env, jclass, jlong token)
{
  auto identities = frm()->GetBookmarkManager().GetOsmAndIdentities();
  std::lock_guard lock(g_mutex);
  auto it = g_sessions.find(token);
  if (it == g_sessions.end()) return Json(env, json_object());
  auto root = json_object();
  json_object_set_new(root, "skipped", json_integer(it->second.skipped));
  auto groups = json_array();
  json_object_set_new(root, "groups", groups);
  for (auto const & group : it->second.groups)
  {
    size_t duplicates = 0;
    for (auto const & point : group.m_bookmarksData)
      if (!identities.insert(point.m_properties.at(kml::osmand::kIdentity)).second) ++duplicates;
    auto const & props = group.m_categoryData.m_properties;
    auto row = json_pack("{s:s,s:i,s:i,s:s,s:i}",
        "name", kml::GetDefaultStr(group.m_categoryData.m_name).c_str(),
        "count", static_cast<int>(group.m_bookmarksData.size()), "duplicates", static_cast<int>(duplicates),
        "color", props.at(kml::osmand::kColor).c_str(),
        "icon", static_cast<int>(kml::osmand::MapIcon(props.at(kml::osmand::kIcon))));
    if (auto rgba = kml::ParseHexColor(props.at(kml::osmand::kColor)))
      json_object_set_new(row, "argb", json_integer(static_cast<int32_t>((*rgba >> 8) | (*rgba << 24))));
    json_array_append_new(groups, row);
  }
  return Json(env, root);
}
JNIEXPORT void JNICALL Java_app_organicmaps_sdk_bookmarks_data_OsmAndImport_nativeDiscard(
    JNIEnv *, jclass, jlong token)
{
  std::lock_guard lock(g_mutex);
  g_sessions.erase(token);
}
JNIEXPORT jintArray JNICALL Java_app_organicmaps_sdk_bookmarks_data_OsmAndImport_nativeStyle(
    JNIEnv * env, jclass, jlong category)
{
  jint values[2] = {0, 0};
  auto & manager = frm()->GetBookmarkManager();
  if (manager.HasBmCategory(category))
  {
    auto const & props = manager.GetCategoryData(category).m_properties;
    if (auto it = props.find(kml::osmand::kColor); it != props.end())
      if (auto rgba = kml::ParseHexColor(it->second)) values[0] = (*rgba >> 8) | (*rgba << 24);
    if (auto it = props.find(kml::osmand::kIcon); it != props.end())
      values[1] = static_cast<int>(kml::osmand::MapIcon(it->second));
  }
  auto result = env->NewIntArray(2);
  env->SetIntArrayRegion(result, 0, 2, values);
  return result;
}
JNIEXPORT void JNICALL Java_app_organicmaps_sdk_bookmarks_data_OsmAndImport_nativeCommit(
    JNIEnv * env, jclass, jlong token, jint index, jobject callback)
{
  auto ref = jni::make_global_ref(callback);
  // Drain pending bookmark writes before saving on the GUI thread. This prevents an older
  // autosave snapshot from overwriting the imported additions after the success callback.
  GetPlatform().RunTask(Platform::Thread::File, [ref, token, index] {
    GetPlatform().RunTask(Platform::Thread::Gui, [ref, token, index] {
      BookmarkManager::OsmAndImportResult result;
      try
      {
        kml::FileData data;
        {
          std::lock_guard lock(g_mutex);
          auto it = g_sessions.find(token);
          if (it != g_sessions.end() && index >= 0 && static_cast<size_t>(index) < it->second.groups.size())
            data = it->second.groups[index];
        }
        result = frm()->GetBookmarkManager().ImportOsmAndGroup(std::move(data));
      }
      catch (...) {}
      auto * env = jni::GetEnv();
      auto method = jni::GetMethodID(env, *ref, "onResult", "(ZII)V");
      env->CallVoidMethod(*ref, method, static_cast<jboolean>(result.success),
                         static_cast<jint>(result.added), static_cast<jint>(result.duplicates));
      jni::HandleJavaException(env);
    });
  });
}
}
