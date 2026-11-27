package app.organicmaps.sdk.routing;

import android.content.Context;
import androidx.annotation.Keep;
import app.organicmaps.sdk.R;
import java.net.URI;

@Keep
public final class OnlineRouting
{
  private OnlineRouting() {}
  public static native boolean nativeIsEnabled();
  public static native String nativeGetEndpoint();
  public static native boolean nativeUsesHardExclusions();
  public static native boolean nativeConfigure(boolean enabled, String endpoint, boolean hardExclusions);
  public static native boolean nativeCanNavigate();
  /** Actual backend, fallback reason, final failure. Values mirror car_routing.hpp. */
  public static native int[] nativeGetStatus();

  public static String normalizeEndpoint(String text)
  {
    try
    {
      URI uri = new URI(text);
      if (!"https".equals(uri.getScheme()) || uri.getHost() == null || uri.getUserInfo() != null ||
          uri.getRawQuery() != null || uri.getRawFragment() != null || text.length() > 2048)
        throw new IllegalArgumentException();
      String path = uri.getRawPath().replaceAll("/+$", "").replaceFirst("/(route|status|isochrone)$", "");
      return "https://" + uri.getRawAuthority() + path + "/route";
    }
    catch (Exception error) { throw new IllegalArgumentException("Invalid routing endpoint"); }
  }

  public static String statusText(Context context)
  {
    int[] state = nativeGetStatus();
    if (state[2] != 0)
    {
      int[] errors = {R.string.online_route_service_failed, R.string.online_route_service_failed,
          R.string.online_route_missing_maps, R.string.online_route_no_toll_free, R.string.online_route_not_found};
      String error = context.getString(errors[Math.min(state[2], errors.length - 1)]);
      return RoutingController.get().isNavigating() ? context.getString(R.string.online_route_rebuild_failed) + "\n" + error : error;
    }
    if (state[0] == 1) return context.getString(R.string.online_route_preview);
    if (state[1] == 0) return context.getString(R.string.online_route_comaps);
    int[] reasons = {R.string.online_route_reason_service, R.string.online_route_reason_service,
        R.string.online_route_reason_timeout, R.string.online_route_reason_busy, R.string.online_route_reason_invalid,
        R.string.online_route_reason_toll, R.string.online_route_reason_unknown, R.string.online_route_reason_warning,
        R.string.online_route_reason_options};
    return context.getString(R.string.online_route_fallback,
        context.getString(reasons[Math.min(state[1], reasons.length - 1)]));
  }
}
