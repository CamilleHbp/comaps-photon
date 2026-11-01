package app.organicmaps.sdk.search;

import androidx.annotation.Keep;
import androidx.annotation.NonNull;

/**
 * Native search will return results via this interface.
 */
public interface SearchListener
{
  // Other search surfaces can ignore online status and remain offline-only.
  @Keep
  default void onPhotonStatus(int status, long timestamp)
  {}
  /**
   * @param results   Search results.
   * @param timestamp Timestamp of search request.
   */
  // Called by JNI.
  @Keep
  @SuppressWarnings("unused")
  default void onResultsUpdate(@NonNull SearchResult[] results, long timestamp)
  {}

  /**
   * @param timestamp Timestamp of search request.
   */
  // Called by JNI.
  @Keep
  @SuppressWarnings("unused")
  default void onResultsEnd(long timestamp)
  {}
}
