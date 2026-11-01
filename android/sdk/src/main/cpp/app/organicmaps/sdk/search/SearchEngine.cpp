#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/UserMarkHelper.hpp"
#include "app/organicmaps/sdk/platform/AndroidPlatform.hpp"
#include "app/organicmaps/sdk/util/Distance.hpp"

#include "map/bookmarks_search_params.hpp"
#include "map/everywhere_search_params.hpp"
#include "map/photon_api.hpp"
#include "map/photon_search_state.hpp"
#include "map/place_page_info.hpp"
#include "map/viewport_search_params.hpp"

#include "search/mode.hpp"
#include "search/result.hpp"

#include "platform/network_policy.hpp"

#include "geometry/distance_on_sphere.hpp"

#include "base/assert.hpp"
#include "base/logging.hpp"

#include "defines.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <vector>

using namespace std;
using namespace std::placeholders;
using search::Result;
using search::Results;

namespace
{
FeatureID const kEmptyFeatureId;

// This cache is needed only for showing a specific result on the map after click on the list item.
// Don't use it with another intentions!
Results g_results;

// Timestamp of last search query. Results with older stamps are ignored.
jlong g_queryTimestamp;
// Implements 'SearchListener' java interface.
jobject g_javaListener;
jmethodID g_updateResultsId;
jmethodID g_endResultsId;
jmethodID g_photonStatusId;
jmethodID g_handheldUpdateId;
jmethodID g_handheldEndId;
jfieldID g_onlineField;
jfieldID g_handleField;
// Cached classes and methods to return results.
jclass g_resultClass;
jmethodID g_resultConstructor;
jmethodID g_suggestConstructor;
jclass g_descriptionClass;
jmethodID g_descriptionConstructor;
jclass g_popularityClass;
jmethodID g_popularityConstructor;

// Implements 'MapSearchListener' java interface.
jmethodID g_mapResultsMethod;
jclass g_mapResultClass;
jmethodID g_mapResultCtor;

jmethodID g_updateBookmarksResultsId;
jmethodID g_endBookmarksResultsId;

// These sessions belong only to the phone/tablet UI. Android Auto and the other
// native search callers continue to use the offline path below.
struct HandheldSession : photon::SearchState<Result>
{
  jlong m_timestamp = 0;
  bool m_showOnMap = false;
  bool m_hasPosition = false;
  double m_lat = 0, m_lon = 0;
  std::string m_url;
};
std::shared_ptr<HandheldSession> g_handheld;

// A row keeps its handle even if a later callback changes the order of results.
// Keep published rows until the next query so cancellation before selection is safe.
std::map<jlong, Result> g_handheldRows;
jlong g_nextHandle = 0;
jlong g_handheldOwnerTimestamp = 0;

// Accessed only on the platform's single network worker. The cooldown survives
// closing the search screen, so reopening it cannot bypass a server's limit.
using PhotonClock = std::chrono::steady_clock;
photon::RequestThrottle g_photonThrottle;
std::map<std::string, photon::Response> g_photonCache;
std::atomic<uint64_t> g_cacheEpoch{0};
uint64_t g_workerCacheEpoch = 0;

void CancelHandheld()
{
  if (g_handheld)
  {
    g_handheld->m_active = false;
    // Another search surface may take over the shared offline engine. Finish the
    // phone's progress indicator without sending its online rows to that surface.
    auto * env = jni::GetEnv();
    if (!g_handheld->m_onlineDone)
      env->CallVoidMethod(g_javaListener, g_photonStatusId, static_cast<jint>(photon::Status::Unavailable),
                          g_handheld->m_timestamp);
    env->CallVoidMethod(g_javaListener, g_handheldEndId, g_handheld->m_timestamp);
  }
  g_handheld.reset();
}

bool PopularityHasHigherPriority(bool hasPosition, double distanceInMeters)
{
  return !hasPosition || distanceInMeters > search::Result::kPopularityHighPriorityMinDistance;
}

jobject ToJavaResult(Result const & result, search::ProductInfo const & productInfo, bool hasPosition, double lat,
                     double lon)
{
  JNIEnv * env = jni::GetEnv();

  jni::TScopedLocalIntArrayRef ranges(env, env->NewIntArray(static_cast<jsize>(result.GetHighlightRangesCount() * 2)));
  jint * rawArr = env->GetIntArrayElements(ranges, nullptr);
  for (size_t i = 0; i < result.GetHighlightRangesCount(); i++)
  {
    auto const & range = result.GetHighlightRange(i);
    rawArr[2 * i] = range.first;
    rawArr[2 * i + 1] = range.second;
  }
  env->ReleaseIntArrayElements(ranges.get(), rawArr, 0);

  jni::TScopedLocalIntArrayRef descRanges(
      env, env->NewIntArray(static_cast<jsize>(result.GetDescHighlightRangesCount() * 2)));
  jint * rawArr2 = env->GetIntArrayElements(descRanges, nullptr);
  for (size_t i = 0; i < result.GetDescHighlightRangesCount(); i++)
  {
    auto const & range = result.GetDescHighlightRange(i);
    rawArr2[2 * i] = range.first;
    rawArr2[2 * i + 1] = range.second;
  }
  env->ReleaseIntArrayElements(descRanges.get(), rawArr2, 0);

  ms::LatLon ll = ms::LatLon::Zero();
  if (result.HasPoint())
    ll = mercator::ToLatLon(result.GetFeatureCenter());

  if (result.IsSuggest())
  {
    jni::TScopedLocalRef name(env, jni::ToJavaString(env, result.GetString()));
    jni::TScopedLocalRef suggest(env, jni::ToJavaString(env, result.GetSuggestionString()));
    return env->NewObject(g_resultClass, g_suggestConstructor, name.get(), suggest.get(), ll.m_lat, ll.m_lon,
                          ranges.get(), descRanges.get());
  }

  platform::Distance distance;
  double distanceInMeters = 0.0;
  if (result.HasPoint() && hasPosition)
  {
    distanceInMeters = ms::DistanceOnEarth(lat, lon, ll.m_lat, ll.m_lon);
    distance = platform::Distance::CreateFormatted(distanceInMeters);
  }

  bool const popularityHasHigherPriority = PopularityHasHigherPriority(hasPosition, distanceInMeters);
  bool const isFeature = result.GetResultType() == Result::Type::Feature;
  jni::TScopedLocalRef featureId(
      env, usermark_helper::CreateFeatureId(env, isFeature ? result.GetFeatureID() : kEmptyFeatureId));

  jni::TScopedLocalRef featureType(env, jni::ToJavaString(env, result.GetLocalizedFeatureType()));
  jni::TScopedLocalRef address(env, jni::ToJavaString(env, result.GetAddress()));
  jni::TScopedLocalRef dist(env, ToJavaDistance(env, distance));
  jni::TScopedLocalRef description(env, jni::ToJavaString(env, result.GetFeatureDescription()));

  jni::TScopedLocalRef desc(
      env,
      env->NewObject(g_descriptionClass, g_descriptionConstructor, featureId.get(), featureType.get(), address.get(),
                     dist.get(), description.get(), static_cast<jint>(result.IsOpenNow()), result.GetMinutesUntilOpen(),
                     result.GetMinutesUntilClosed(), static_cast<jboolean>(popularityHasHigherPriority)));

  jni::TScopedLocalRef name(env, jni::ToJavaString(env, result.GetString()));
  jni::TScopedLocalRef popularity(env, env->NewObject(g_popularityClass, g_popularityConstructor,
                                                      /// @todo Restore when popularity will be available
                                                      0 /*static_cast<jint>(result.GetRankingInfo().m_popularity)*/));

  return env->NewObject(g_resultClass, g_resultConstructor, name.get(), desc.get(), ll.m_lat, ll.m_lon, ranges.get(),
                        descRanges.get(), popularity.get());
}

jobjectArray BuildSearchResults(vector<search::ProductInfo> const & productInfo, bool hasPosition, double lat,
                                double lon)
{
  JNIEnv * env = jni::GetEnv();

  auto const count = static_cast<jsize>(g_results.GetCount());
  jobjectArray const jResults = env->NewObjectArray(count, g_resultClass, nullptr);
  for (jsize i = 0; i < count; i++)
  {
    jni::TScopedLocalRef jRes(env, ToJavaResult(g_results[i], productInfo[i], hasPosition, lat, lon));
    env->SetObjectArrayElement(jResults, i, jRes.get());
  }
  return jResults;
}

void OnResults(Results results, vector<search::ProductInfo> const & productInfo, jlong timestamp, bool isMapAndTable,
               bool hasPosition, double lat, double lon)
{
  // Ignore results from obsolete searches.
  if (g_queryTimestamp > timestamp)
    return;

  JNIEnv * env = jni::GetEnv();

  if (!results.IsEndMarker() || results.IsEndedNormal())
  {
    g_results = std::move(results);
    jni::TScopedLocalObjectArrayRef jResults(env, BuildSearchResults(productInfo, hasPosition, lat, lon));
    env->CallVoidMethod(g_javaListener, g_updateResultsId, jResults.get(), timestamp);
  }

  if (results.IsEndMarker())
  {
    env->CallVoidMethod(g_javaListener, g_endResultsId, timestamp);
    if (isMapAndTable && results.IsEndedNormal())
      g_framework->NativeFramework()->GetSearchAPI().PokeSearchInViewport();
  }
}

void PublishHandheld(std::shared_ptr<HandheldSession> const & session)
{
  if (!session->m_active || session != g_handheld)
    return;
  JNIEnv * env = jni::GetEnv();
  Results combined;
  for (auto const & result : session->Snapshot())
    combined.AddResultNoChecks(Result(result));

  auto const count = static_cast<jsize>(combined.GetCount());
  jni::TScopedLocalObjectArrayRef rows(env, env->NewObjectArray(count, g_resultClass, nullptr));
  for (jsize i = 0; i < count; ++i)
  {
    auto const & result = combined[i];
    jni::TScopedLocalRef row(env, ToJavaResult(result, {}, session->m_hasPosition, session->m_lat, session->m_lon));
    auto const handle = ++g_nextHandle;
    g_handheldRows.emplace(handle, result);
    env->SetLongField(row.get(), g_handleField, handle);
    env->SetBooleanField(row.get(), g_onlineField, result.IsExternal());
    env->SetObjectArrayElement(rows.get(), i, row.get());
  }
  env->CallVoidMethod(g_javaListener, g_photonStatusId, static_cast<jint>(session->m_status), session->m_timestamp);
  env->CallVoidMethod(g_javaListener, g_handheldUpdateId, rows.get(), session->m_timestamp);
  if (session->m_showOnMap)
    g_framework->NativeFramework()->FillSearchResultsMarks(true, combined);
  if (session->Complete())
    env->CallVoidMethod(g_javaListener, g_handheldEndId, session->m_timestamp);
}

void ExecutePhoton(std::shared_ptr<HandheldSession> const & session)
{
  if (!session->m_active)
    return;
  auto const epoch = g_cacheEpoch.load();
  if (epoch != g_workerCacheEpoch)
  {
    g_photonCache.clear();
    g_workerCacheEpoch = epoch;
  }
  auto const finish = [session](photon::Response response)
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [session, response = std::move(response)]() mutable
    {
      if (!session->m_active)
        return;
      std::vector<Result> results;
      for (auto & place : response.m_results)
      {
        Result result(mercator::FromLatLon(place.m_position), place.m_name);
        result.SetType(Result::Type::LatLon);
        result.SetAddress(std::move(place.m_address));
        result.SetExternalSource("Photon");
        results.push_back(std::move(result));
      }
      if (session->UpdateOnline(std::move(results), response.m_status))
        PublishHandheld(session);
    });
  };
  if (auto const it = g_photonCache.find(session->m_url); it != g_photonCache.end())
  {
    finish(it->second);
    return;
  }
  auto const now = PhotonClock::now();
  if (g_photonThrottle.IsThrottled(now))
  {
    photon::Response response;
    response.m_status = photon::Status::Throttled;
    finish(std::move(response));
    return;
  }
  if (auto const delay = g_photonThrottle.Delay(now); delay > PhotonClock::duration::zero())
  {
    GetPlatform().RunDelayedTask(Platform::Thread::Network, delay, [session] { ExecutePhoton(session); });
    return;
  }
  // Recheck policy after the debounce: connectivity or roaming may have changed.
  if (!platform::GetCurrentNetworkPolicy().CanUse())
  {
    finish({});
    return;
  }
  g_photonThrottle.Started(now);
  auto response = photon::Search(session->m_url);
  if (response.m_status == photon::Status::Throttled)
    g_photonThrottle.Throttle(PhotonClock::now(), response.m_retryAfter);
  if (session->m_active && (response.m_status == photon::Status::Ready || response.m_status == photon::Status::Empty))
  {
    if (g_photonCache.size() >= 32)
      g_photonCache.erase(g_photonCache.begin());
    g_photonCache.insert_or_assign(session->m_url, response);
  }
  finish(std::move(response));
}

jobjectArray BuildJavaMapResults(vector<storage::DownloaderSearchResult> const & results)
{
  JNIEnv * env = jni::GetEnv();

  auto const count = static_cast<jsize>(results.size());
  jobjectArray const res = env->NewObjectArray(count, g_mapResultClass, nullptr);
  for (jsize i = 0; i < count; i++)
  {
    jni::TScopedLocalRef country(env, jni::ToJavaString(env, results[i].m_countryId));
    jni::TScopedLocalRef matched(env, jni::ToJavaString(env, results[i].m_matchedName));
    jni::TScopedLocalRef item(env, env->NewObject(g_mapResultClass, g_mapResultCtor, country.get(), matched.get()));
    env->SetObjectArrayElement(res, i, item.get());
  }

  return res;
}

void OnMapSearchResults(storage::DownloaderSearchResults const & results, long long timestamp)
{
  // Ignore results from obsolete searches.
  if (g_queryTimestamp > timestamp)
    return;

  JNIEnv * env = jni::GetEnv();
  jni::TScopedLocalObjectArrayRef jResults(env, BuildJavaMapResults(results.m_results));
  env->CallVoidMethod(g_javaListener, g_mapResultsMethod, jResults.get(), static_cast<jlong>(timestamp),
                      results.m_endMarker);
}

void OnBookmarksSearchResults(search::BookmarksSearchParams::Results results,
                              search::BookmarksSearchParams::Status status, long long timestamp)
{
  // Ignore results from obsolete searches.
  if (g_queryTimestamp > timestamp)
    return;

  JNIEnv * env = jni::GetEnv();

  g_framework->NativeFramework()->GetBookmarkManager().FilterInvalidBookmarks(results);
  jni::ScopedLocalRef<jlongArray> jResults(env, env->NewLongArray(static_cast<jsize>(results.size())));
  vector<jlong> const tmp(results.cbegin(), results.cend());
  env->SetLongArrayRegion(jResults.get(), 0, static_cast<jsize>(tmp.size()), tmp.data());

  auto const method = (status == search::BookmarksSearchParams::Status::InProgress) ? g_updateBookmarksResultsId
                                                                                    : g_endBookmarksResultsId;

  env->CallVoidMethod(g_javaListener, method, jResults.get(), static_cast<jlong>(timestamp));
}

}  // namespace

extern "C"
{
JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeInit(JNIEnv * env, jobject thiz)
{
  g_javaListener = env->NewGlobalRef(thiz);
  // public void onResultsUpdate(@NonNull SearchResult[] results, long timestamp)
  g_updateResultsId =
      jni::GetMethodID(env, g_javaListener, "onResultsUpdate", "([Lapp/organicmaps/sdk/search/SearchResult;J)V");
  // public void onResultsEnd(long timestamp)
  g_endResultsId = jni::GetMethodID(env, g_javaListener, "onResultsEnd", "(J)V");
  g_photonStatusId = jni::GetMethodID(env, g_javaListener, "onPhotonStatus", "(IJ)V");
  g_handheldUpdateId = jni::GetMethodID(env, g_javaListener, "onHandheldResultsUpdate",
                                        "([Lapp/organicmaps/sdk/search/SearchResult;J)V");
  g_handheldEndId = jni::GetMethodID(env, g_javaListener, "onHandheldResultsEnd", "(J)V");
  g_resultClass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/search/SearchResult");
  g_onlineField = env->GetFieldID(g_resultClass, "isOnline", "Z");
  g_handleField = env->GetFieldID(g_resultClass, "nativeHandle", "J");
  g_resultConstructor =
      jni::GetConstructorID(env, g_resultClass,
                            "(Ljava/lang/String;Lapp/organicmaps/sdk/search/SearchResult$Description;DD[I[I"
                            "Lapp/organicmaps/sdk/search/Popularity;)V");
  g_suggestConstructor = jni::GetConstructorID(env, g_resultClass, "(Ljava/lang/String;Ljava/lang/String;DD[I[I)V");
  g_descriptionClass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/search/SearchResult$Description");
  /*
      Description(FeatureId featureId, String featureType, String region, Distance distance,
                  String description, int openNow, int minutesUntilOpen, int minutesUntilClosed,
                  boolean hasPopularityHigherPriority)
  */
  g_descriptionConstructor =
      jni::GetConstructorID(env, g_descriptionClass,
                            "(Lapp/organicmaps/sdk/bookmarks/data/FeatureId;"
                            "Ljava/lang/String;Ljava/lang/String;Lapp/organicmaps/sdk/util/Distance;"
                            "Ljava/lang/String;IIIZ)V");

  g_popularityClass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/search/Popularity");
  g_popularityConstructor = jni::GetConstructorID(env, g_popularityClass, "(I)V");

  g_mapResultsMethod = jni::GetMethodID(env, g_javaListener, "onMapSearchResults",
                                        "([Lapp/organicmaps/sdk/search/MapSearchListener$Result;JZ)V");
  g_mapResultClass = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/search/MapSearchListener$Result");
  g_mapResultCtor = jni::GetConstructorID(env, g_mapResultClass, "(Ljava/lang/String;Ljava/lang/String;)V");

  g_updateBookmarksResultsId = jni::GetMethodID(env, g_javaListener, "onBookmarkSearchResultsUpdate", "([JJ)V");
  g_endBookmarksResultsId = jni::GetMethodID(env, g_javaListener, "onBookmarkSearchResultsEnd", "([JJ)V");
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeRunHandheldSearch(
    JNIEnv * env, jclass, jstring query, jstring language, jlong timestamp, jboolean showOnMap, jboolean hasPosition,
    jdouble lat, jdouble lon, jstring endpoint)
{
  CancelHandheld();
  g_handheldRows.clear();
  g_handheldOwnerTimestamp = timestamp;
  auto session = std::make_shared<HandheldSession>();
  session->m_timestamp = timestamp;
  session->m_showOnMap = showOnMap;
  session->m_hasPosition = hasPosition;
  session->m_lat = lat;
  session->m_lon = lon;
  auto const q = jni::ToNativeString(env, query);
  auto const lang = jni::ToNativeString(env, language);
  auto & framework = *g_framework->NativeFramework();
  session->m_url =
      photon::MakeUrl(jni::ToNativeString(env, endpoint), q, lang, mercator::ToLatLon(framework.GetViewportCenter()));
  g_handheld = session;
  g_queryTimestamp = timestamp;
  if (!platform::GetCurrentNetworkPolicy().CanUse())
  {
    session->m_onlineDone = true;
    session->m_status = photon::Status::Unavailable;
  }
  PublishHandheld(session);
  search::EverywhereSearchParams params{q,
                                        lang,
                                        {},
                                        false,
                                        [session](Results results, std::vector<search::ProductInfo> const &)
  {
    if (!session->m_active || results.IsEndedCancelled())
      return;
    if (session->UpdateOffline({results.begin(), results.end()}, results.IsEndMarker()))
      PublishHandheld(session);
  }};
  if (!framework.GetSearchAPI().SearchEverywhere(std::move(params)))
  {
    session->m_offlineDone = true;
    PublishHandheld(session);
  }
  if (!session->m_onlineDone)
    GetPlatform().RunDelayedTask(Platform::Thread::Network, std::chrono::milliseconds(600),
                                 [session] { ExecutePhoton(session); });
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeShowHandheldResult(JNIEnv *, jclass,
                                                                                             jlong handle)
{
  auto const it = g_handheldRows.find(handle);
  if (it != g_handheldRows.end())
    g_framework->NativeFramework()->ShowSearchResult(it->second);
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeShowHandheldResults(JNIEnv * env, jclass,
                                                                                              jlongArray handles)
{
  // Use exactly the snapshot the user saw, not whichever callback finished last.
  auto const count = env->GetArrayLength(handles);
  std::vector<jlong> ids(count);
  env->GetLongArrayRegion(handles, 0, count, ids.data());
  Results results;
  for (auto const id : ids)
    if (auto const it = g_handheldRows.find(id); it != g_handheldRows.end())
      results.AddResultNoChecks(Result(it->second));
  CancelHandheld();
  auto & framework = *g_framework->NativeFramework();
  framework.GetSearchAPI().CancelAllSearches();
  // Close a previously selected place before presenting the new set of pins.
  framework.DeactivateMapSelection();
  LOG(LDEBUG, ("Showing handheld search results:", results.GetCount()));
  framework.FillSearchResultsMarks(true, results);
  framework.UpdateViewport(results);
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeEndHandheldSession(JNIEnv *, jclass,
                                                                                             jlong timestamp)
{
  // A previous activity can be destroyed after its replacement has started searching.
  if (timestamp == 0 || timestamp != g_handheldOwnerTimestamp)
    return;
  CancelHandheld();
  g_handheldRows.clear();
  g_handheldOwnerTimestamp = 0;
  ++g_cacheEpoch;
}

JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeRunSearch(
    JNIEnv * env, jclass clazz, jbyteArray bytes, jboolean isCategory, jstring lang, jlong timestamp,
    jboolean hasPosition, jdouble lat, jdouble lon)
{
  CancelHandheld();
  search::EverywhereSearchParams params{jni::ToNativeString(env, bytes),
                                        jni::ToNativeString(env, lang),
                                        {},  // default timeout
                                        static_cast<bool>(isCategory),
                                        bind(&OnResults, _1, _2, timestamp, false, hasPosition, lat, lon)};
  bool const searchStarted = g_framework->NativeFramework()->GetSearchAPI().SearchEverywhere(std::move(params));
  if (searchStarted)
    g_queryTimestamp = timestamp;
  return searchStarted;
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeRunInteractiveSearch(
    JNIEnv * env, jclass clazz, jbyteArray bytes, jboolean isCategory, jstring lang, jlong timestamp,
    jboolean isMapAndTable, jboolean hasPosition, jdouble lat, jdouble lon)
{
  CancelHandheld();
  search::ViewportSearchParams vparams{
      jni::ToNativeString(env, bytes),
      jni::ToNativeString(env, lang),
      {},  // Default timeout
      static_cast<bool>(isCategory),
      {},  // Empty m_onStarted callback
      {},  // Empty m_onCompleted callback
  };

  // TODO (@alexzatsepin): set up vparams.m_onCompleted here and use
  // HotelsClassifier for hotel queries detection.
  // Don't move vparams here, because it's used below.
  g_framework->NativeFramework()->GetSearchAPI().SearchInViewport(vparams);

  if (isMapAndTable)
  {
    search::EverywhereSearchParams eparams{std::move(vparams.m_query),
                                           std::move(vparams.m_inputLocale),
                                           {},  // default timeout
                                           static_cast<bool>(isCategory),
                                           bind(&OnResults, _1, _2, timestamp, isMapAndTable, hasPosition, lat, lon)};

    if (g_framework->NativeFramework()->GetSearchAPI().SearchEverywhere(std::move(eparams)))
      g_queryTimestamp = timestamp;
  }
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeRunSearchMaps(JNIEnv * env, jclass clazz,
                                                                                        jbyteArray bytes, jstring lang,
                                                                                        jlong timestamp)
{
  CancelHandheld();
  storage::DownloaderSearchParams params{jni::ToNativeString(env, bytes), jni::ToNativeString(env, lang),
                                         bind(&OnMapSearchResults, _1, timestamp)};

  if (g_framework->NativeFramework()->GetSearchAPI().SearchInDownloader(std::move(params)))
    g_queryTimestamp = timestamp;
}

JNIEXPORT jboolean JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeRunSearchInBookmarks(
    JNIEnv * env, jclass clazz, jbyteArray query, jlong catId, jlong timestamp)
{
  CancelHandheld();
  search::BookmarksSearchParams params{jni::ToNativeString(env, query), static_cast<kml::MarkGroupId>(catId),
                                       bind(&OnBookmarksSearchResults, _1, _2, timestamp)};

  bool const searchStarted = g_framework->NativeFramework()->GetSearchAPI().SearchInBookmarks(std::move(params));
  if (searchStarted)
    g_queryTimestamp = timestamp;
  return searchStarted;
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeShowResult(JNIEnv * env, jclass clazz,
                                                                                     jint index)
{
  g_framework->NativeFramework()->ShowSearchResult(g_results[index]);
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeCancelInteractiveSearch(JNIEnv * env,
                                                                                                  jclass clazz)
{
  g_framework->NativeFramework()->GetSearchAPI().CancelSearch(search::Mode::Viewport);
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeCancelEverywhereSearch(JNIEnv * env,
                                                                                                 jclass clazz)
{
  g_framework->NativeFramework()->GetSearchAPI().CancelSearch(search::Mode::Everywhere);
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeCancelAllSearches(JNIEnv * env, jclass clazz)
{
  CancelHandheld();
  g_framework->NativeFramework()->GetSearchAPI().CancelAllSearches();
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_search_SearchEngine_nativeUpdateViewportWithLastResults(JNIEnv * env,
                                                                                                        jclass clazz)
{
  g_framework->NativeFramework()->UpdateViewport(g_results);
}
}  // extern "C"
