package app.organicmaps.sdk.search;

import android.content.Context;
import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.util.Language;
import app.organicmaps.sdk.util.concurrency.UiThread;
import java.nio.charset.StandardCharsets;
import org.chromium.base.ObserverList;

public enum SearchEngine implements SearchListener, MapSearchListener,
                                    BookmarkSearchListener
{
  INSTANCE;

  public static final int PHOTON_READY = 0;
  public static final int PHOTON_LOADING = 1;
  public static final int PHOTON_EMPTY = 2;
  public static final int PHOTON_UNAVAILABLE = 3;
  public static final int PHOTON_THROTTLED = 4;

  @Override
  @androidx.annotation.Keep
  public void onPhotonStatus(int status, long timestamp)
  {
    UiThread.run(() -> {
      for (SearchListener listener : mHandheldListeners)
        listener.onPhotonStatus(status, timestamp);
    });
  }

  // Keep online snapshots off the ordinary listener channel: Android Auto still
  // selects from its own offline result cache and must never receive these rows.
  @androidx.annotation.Keep
  public void onHandheldResultsUpdate(SearchResult[] results, long timestamp)
  {
    UiThread.run(() -> {
      for (SearchListener listener : mHandheldListeners)
        listener.onResultsUpdate(results, timestamp);
    });
  }

  @androidx.annotation.Keep
  public void onHandheldResultsEnd(long timestamp)
  {
    UiThread.run(() -> {
      for (SearchListener listener : mHandheldListeners)
        listener.onResultsEnd(timestamp);
    });
  }

  private final ObserverList<SearchListener> mHandheldListeners = new ObserverList<>();

  public void addHandheldListener(SearchListener listener)
  {
    mHandheldListeners.addObserver(listener);
  }
  public void removeHandheldListener(SearchListener listener)
  {
    mHandheldListeners.removeObserver(listener);
  }

  // Only the handheld search screen opts in; car search keeps its existing API.
  @MainThread
  public void searchHandheld(String query, String language, long timestamp, boolean showOnMap, boolean hasLocation,
                             double lat, double lon, String endpoint)
  {
    nativeRunHandheldSearch(query, language, timestamp, showOnMap, hasLocation, lat, lon, endpoint);
  }

  public void showHandheldResult(long handle)
  {
    mQuery = "";
    nativeShowHandheldResult(handle);
  }

  public void showHandheldResults(long[] handles)
  {
    nativeShowHandheldResults(handles);
  }

  public void endHandheldSession(long timestamp)
  {
    nativeEndHandheldSession(timestamp);
  }

  private static native void nativeRunHandheldSearch(String query, String language, long timestamp, boolean showOnMap,
                                                     boolean hasLocation, double lat, double lon, String endpoint);
  private static native void nativeShowHandheldResult(long handle);
  private static native void nativeShowHandheldResults(long[] handles);
  private static native void nativeEndHandheldSession(long timestamp);

  public static final long ALL_BOOKMARK_CATEGORIES = -1L;

  // Query, which results are shown on the map.
  @Nullable
  private String mQuery;

  @Override
  public void onResultsUpdate(@NonNull final SearchResult[] results, final long timestamp)
  {
    UiThread.run(() -> {
      for (SearchListener listener : mListeners)
        listener.onResultsUpdate(results, timestamp);
    });
  }

  @Override
  public void onResultsEnd(final long timestamp)
  {
    UiThread.run(() -> {
      for (SearchListener listener : mListeners)
        listener.onResultsEnd(timestamp);
    });
  }

  @Override
  public void onMapSearchResults(@NonNull final MapSearchListener.Result[] results, final long timestamp,
                                 final boolean isLast)
  {
    UiThread.run(() -> {
      for (MapSearchListener listener : mMapListeners)
        listener.onMapSearchResults(results, timestamp, isLast);
    });
  }

  @Override
  public void onBookmarkSearchResultsUpdate(@Nullable final long[] bookmarkIds, final long timestamp)
  {
    UiThread.run(() -> {
      for (BookmarkSearchListener listener : mBookmarkListeners)
        listener.onBookmarkSearchResultsUpdate(bookmarkIds, timestamp);
    });
  }

  @Override
  public void onBookmarkSearchResultsEnd(@Nullable final long[] bookmarkIds, final long timestamp)
  {
    UiThread.run(() -> {
      for (BookmarkSearchListener listener : mBookmarkListeners)
        listener.onBookmarkSearchResultsEnd(bookmarkIds, timestamp);
    });
  }

  private final ObserverList<SearchListener> mListeners = new ObserverList<>();

  private final ObserverList<MapSearchListener> mMapListeners = new ObserverList<>();

  private final ObserverList<BookmarkSearchListener> mBookmarkListeners = new ObserverList<>();

  public void addListener(SearchListener listener)
  {
    mListeners.addObserver(listener);
  }

  public void removeListener(SearchListener listener)
  {
    mListeners.removeObserver(listener);
  }

  public void addMapListener(MapSearchListener listener)
  {
    mMapListeners.addObserver(listener);
  }

  public void removeMapListener(MapSearchListener listener)
  {
    mMapListeners.removeObserver(listener);
  }

  public void addBookmarkListener(BookmarkSearchListener listener)
  {
    mBookmarkListeners.addObserver(listener);
  }

  public void removeBookmarkListener(BookmarkSearchListener listener)
  {
    mBookmarkListeners.removeObserver(listener);
  }

  /**
   *
   * @param context
   * @param timestamp Search results are filtered according to it after multiple requests.
   * @return whether search was actually started.
   */
  @MainThread
  public boolean search(@NonNull Context context, @NonNull String query, boolean isCategory, long timestamp,
                        boolean hasLocation, double lat, double lon)
  {
    return nativeRunSearch(query.getBytes(StandardCharsets.UTF_8), isCategory, Language.getKeyboardLocale(context),
                           timestamp, hasLocation, lat, lon);
  }

  @MainThread
  public void searchInteractive(@NonNull String query, boolean isCategory, @NonNull String locale, long timestamp,
                                boolean isMapAndTable, boolean hasLocation, double lat, double lon)
  {
    nativeRunInteractiveSearch(query.getBytes(StandardCharsets.UTF_8), isCategory, locale, timestamp, isMapAndTable,
                               hasLocation, lat, lon);
  }

  @MainThread
  public void searchInteractive(@NonNull String query, boolean isCategory, @NonNull String locale, long timestamp,
                                boolean isMapAndTable)
  {
    searchInteractive(query, isCategory, locale, timestamp, isMapAndTable, false, 0, 0);
  }

  @MainThread
  public void searchInteractive(@NonNull Context context, @NonNull String query, boolean isCategory, long timestamp,
                                boolean isMapAndTable)
  {
    searchInteractive(query, isCategory, Language.getKeyboardLocale(context), timestamp, isMapAndTable, false, 0, 0);
  }

  @MainThread
  public static void searchMaps(@NonNull Context context, @NonNull String query, long timestamp)
  {
    nativeRunSearchMaps(query.getBytes(StandardCharsets.UTF_8), Language.getKeyboardLocale(context), timestamp);
  }

  @MainThread
  public boolean searchInBookmarks(@NonNull String query, long categoryId, long timestamp)
  {
    return nativeRunSearchInBookmarks(query.getBytes(StandardCharsets.UTF_8), categoryId, timestamp);
  }

  @MainThread
  public boolean searchInBookmarks(@NonNull String query, long timestamp)
  {
    return nativeRunSearchInBookmarks(query.getBytes(StandardCharsets.UTF_8), ALL_BOOKMARK_CATEGORIES, timestamp);
  }

  public void setQuery(@Nullable String query)
  {
    mQuery = query;
  }

  @Nullable
  public String getQuery()
  {
    return mQuery;
  }

  @MainThread
  public void cancel()
  {
    cancelApiCall();
    cancelAllSearches();
  }

  @MainThread
  private static void cancelApiCall()
  {
    Framework.nativeClearApiPoints();
  }

  @MainThread
  public void cancelInteractiveSearch()
  {
    mQuery = "";
    nativeCancelInteractiveSearch();
  }

  @MainThread
  private void cancelAllSearches()
  {
    mQuery = "";
    nativeCancelAllSearches();
  }

  @MainThread
  public void showResult(int index)
  {
    mQuery = "";
    nativeShowResult(index);
  }

  @MainThread
  public void updateViewportWithLastResults()
  {
    nativeUpdateViewportWithLastResults();
  }

  public void initialize()
  {
    nativeInit();
  }

  private native void nativeInit();

  /**
   * @param bytes utf-8 formatted bytes of query.
   */
  private static native boolean nativeRunSearch(byte[] bytes, boolean isCategory, String language, long timestamp,
                                                boolean hasLocation, double lat, double lon);

  /**
   * @param bytes utf-8 formatted query bytes
   */
  private static native void nativeRunInteractiveSearch(byte[] bytes, boolean isCategory, String language,
                                                        long timestamp, boolean isMapAndTable, boolean hasLocation,
                                                        double lat, double lon);

  /**
   * @param bytes utf-8 formatted query bytes
   */
  private static native void nativeRunSearchMaps(byte[] bytes, String language, long timestamp);

  private static native boolean nativeRunSearchInBookmarks(byte[] bytes, long categoryId, long timestamp);

  private static native void nativeShowResult(int index);

  private static native void nativeCancelInteractiveSearch();

  private static native void nativeCancelEverywhereSearch();

  private static native void nativeCancelAllSearches();

  private static native void nativeUpdateViewportWithLastResults();
}
