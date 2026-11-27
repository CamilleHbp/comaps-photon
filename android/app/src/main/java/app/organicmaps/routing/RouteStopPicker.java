package app.organicmaps.routing;

import android.app.Activity;
import android.net.Uri;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.widget.ScrollView;
import android.widget.TextView;
import androidx.appcompat.app.AlertDialog;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.routing.RoutingController;
import app.organicmaps.sdk.util.Config;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONObject;

final class RouteStopPicker
{
  private static final String[] TAGS = {"amenity.fuel", "tourism.hostel", "amenity.restaurant", "amenity.cafe",
                                         "amenity.charging_station", "amenity.toilets"};
  private static final double METRES = 111195;
  private record Place(String name, double lat, double lon, int leg, double along, double offset) {}

  private final Activity activity;
  private final double[] path;
  private final String[] labels;

  private RouteStopPicker(Activity activity, double[] path)
  {
    this.activity = activity;
    this.path = path;
    labels = activity.getResources().getStringArray(R.array.route_stop_choices);
  }

  static void show(Activity activity, Runnable pickLocation)
  {
    double[] path = Framework.nativeGetStopSearchPath();
    if (!Config.isPhotonEnabled() || path == null || path.length < 6)
    {
      pickLocation.run();
      return;
    }
    RouteStopPicker picker = new RouteStopPicker(activity, path);
    picker.showChoices(pickLocation);
  }

  private int maxKm = 1;
  private String queryText = "";

  private void showChoices(Runnable pickLocation)
  {
    LinearLayout controls = new LinearLayout(activity);
    controls.setOrientation(LinearLayout.VERTICAL);
    int padding = (int)(20 * activity.getResources().getDisplayMetrics().density);
    controls.setPadding(padding, 0, padding, padding);
    EditText query = new EditText(activity);
    query.setSingleLine();
    query.setHint(R.string.route_stop_query);
    controls.addView(query);
    Spinner category = new Spinner(activity);
    category.setAdapter(new ArrayAdapter<>(activity, android.R.layout.simple_spinner_dropdown_item, labels));
    controls.addView(category);
    TextView distance = new TextView(activity);
    distance.setText(activity.getString(R.string.route_stop_radius, maxKm));
    controls.addView(distance);
    SeekBar slider = new SeekBar(activity);
    slider.setMax(99);
    slider.setContentDescription(distance.getText());
    slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
      public void onProgressChanged(SeekBar bar, int value, boolean user)
      {
        maxKm = value + 1;
        distance.setText(activity.getString(R.string.route_stop_radius, maxKm));
        bar.setContentDescription(distance.getText());
      }
      public void onStartTrackingTouch(SeekBar bar) {}
      public void onStopTrackingTouch(SeekBar bar) {}
    });
    controls.addView(slider);
    ScrollView scroll = new ScrollView(activity);
    scroll.addView(controls);
    new MaterialAlertDialogBuilder(activity).setTitle(R.string.placepage_add_stop)
        .setView(scroll).setNeutralButton(R.string.route_stop_location, (d,i) -> pickLocation.run())
        .setPositiveButton(R.string.search, (d,i) -> {
          queryText = query.getText().toString().trim();
          int type = category.getSelectedItemPosition();
          if (queryText.isEmpty() && type == 0) pickLocation.run();
          else search(type);
        }).setNegativeButton(android.R.string.cancel, null).show();
  }

  private static double deltaLon(double delta) { return (delta + 540) % 360 - 180; }

  private double segmentLength(int i)
  {
    double scale = Math.cos(Math.toRadians((path[i] + path[i-3]) / 2));
    return METRES * Math.hypot(path[i] - path[i-3], deltaLon(path[i+1] - path[i-2]) * scale);
  }

  private void search(int category)
  {
    AlertDialog progress = new MaterialAlertDialogBuilder(activity).setTitle(labels[category])
        .setMessage(R.string.route_stop_searching).setNegativeButton(android.R.string.cancel, null).show();
    AtomicBoolean cancelled = new AtomicBoolean();
    progress.setOnDismissListener(d -> cancelled.set(true));
    String endpoint = Config.getPhotonEndpoint();
    new Thread(() -> {
      List<Place> places = new ArrayList<>();
      boolean success = false;
      try
      {
        double[] along = new double[path.length / 3];
        for (int i = 1; i < along.length; ++i)
          along[i] = along[i - 1] + segmentLength(i * 3);
        // Cover long trips with several bounded searches, including the final leg.
        double span = Math.max(Math.max(10000, maxKm * 2000), along[along.length - 1] / 8);
        Map<String, Place> matches = new HashMap<>();
        int first = 0;
        for (int last = 1; last < along.length; ++last)
        {
          if (cancelled.get()) return;
          if (along[last] - along[first] < span && last != along.length - 1) continue;
          searchBounds(endpoint, category, first * 3, last * 3, along, matches);
          first = last;
        }
        places.addAll(matches.values());
        places.sort(Comparator.comparingDouble(Place::along));
        success = true;
      }
      catch (Exception error) { android.util.Log.w("RouteStops", "Place search failed", error); }
      boolean complete = success;
      activity.runOnUiThread(() -> {
        if (activity.isFinishing() || activity.isDestroyed() || !progress.isShowing()) return;
        progress.dismiss();
        if (!current()) return;
        MaterialAlertDialogBuilder dialog = new MaterialAlertDialogBuilder(activity).setTitle(labels[category]);
        if (!complete || places.isEmpty())
          dialog.setMessage(complete ? R.string.route_stop_empty : R.string.route_stop_error)
              .setPositiveButton(android.R.string.ok, null);
        else
        {
          String[] items = new String[places.size()];
          for (int i = 0; i < items.length; ++i)
            items[i] = activity.getString(R.string.route_stop_result, places.get(i).name,
                                          places.get(i).offset / 1000, places.get(i).along / 1000);
          dialog.setItems(items, (d,i) -> {
            Place place = places.get(i);
            if (current()) RoutingController.get().addStopOnLeg(place.name, place.lat, place.lon, place.leg);
          });
        }
        dialog.show();
      });
    }, "RouteStopSearch").start();
  }

  private void searchBounds(String endpoint, int category, int first, int last, double[] along,
                            Map<String, Place> places) throws Exception
  {
    double base = path[first+1], west = base, east = base;
    double south = path[first], north = south;
    for (int i = first; i <= last; i += 3)
    {
      double lon = base + deltaLon(path[i+1] - base);
      west = Math.min(west, lon); east = Math.max(east, lon);
      south = Math.min(south, path[i]); north = Math.max(north, path[i]);
    }
    south = Math.max(-90, south - maxKm * 1000 / METRES);
    north = Math.min(90, north + maxKm * 1000 / METRES);
    double pad = maxKm * 1000 / (METRES * Math.cos(Math.toRadians(Math.max(Math.abs(south), Math.abs(north)))));
    west -= pad; east += pad;
    if (east - west >= 360)
    {
      west = -180;
      east = 180;
    }
    query(endpoint, category, Math.max(-180,west), south, Math.min(180,east), north, along, places);
    if (west < -180) query(endpoint, category, west+360, south, 180, north, along, places);
    if (east > 180) query(endpoint, category, -180, south, east-360, north, along, places);
  }

  private boolean current()
  {
    return RoutingController.get().isPlanning() && RoutingController.get().isBuilt()
        && Arrays.equals(path, Framework.nativeGetStopSearchPath());
  }

  private void query(String endpoint, int category, double west, double south, double east, double north,
                     double[] along, Map<String, Place> places) throws Exception
  {
    Uri.Builder builder = Uri.parse(endpoint.replaceAll("/+$", "") + "/api/").buildUpon()
        .appendQueryParameter("bbox", west + "," + south + "," + east + "," + north)
        .appendQueryParameter("limit", "100")
        .appendQueryParameter("lang", Locale.getDefault().getLanguage());
    if (!queryText.isEmpty()) builder.appendQueryParameter("q", queryText);
    if (category != 0) builder.appendQueryParameter("include", "osm." + TAGS[category-1]);
    Uri url = builder.build();
    HttpURLConnection connection = (HttpURLConnection) new URL(url.toString()).openConnection();
    connection.setConnectTimeout(10000);
    connection.setReadTimeout(15000);
    try (BufferedReader reader = new BufferedReader(new InputStreamReader(connection.getInputStream(), StandardCharsets.UTF_8)))
    {
      StringBuilder json = new StringBuilder();
      char[] buffer = new char[8192];
      int n;
      while ((n = reader.read(buffer)) != -1)
      {
        if (json.length() + n > 2000000) throw new java.io.IOException("Response too large");
        json.append(buffer, 0, n);
      }
      JSONArray features = new JSONObject(json.toString()).getJSONArray("features");
      for (int j = 0; j < features.length(); j++)
      {
        JSONObject feature = features.getJSONObject(j), props = feature.getJSONObject("properties");
        if (category != 0 && !TAGS[category-1].equals(props.optString("osm_key") + "." + props.optString("osm_value"))) continue;
        JSONArray coord = feature.getJSONObject("geometry").getJSONArray("coordinates");
        double lat = coord.getDouble(1), lon = coord.getDouble(0);
        String id = props.has("osm_id") ? props.optString("osm_type") + ":" + props.getString("osm_id")
                                        : lat + "," + lon + "," + props.optString("name");
        if (places.containsKey(id)) continue;
        double scale = Math.cos(Math.toRadians(lat)), best = maxKm * 1000;
        Place nearest = null;
        for (int i = 3; i < path.length; i += 3)
        {
          double dx = deltaLon(path[i+1] - path[i-2]) * scale * METRES, dy = (path[i] - path[i-3]) * METRES;
          double x = deltaLon(lon - path[i-2]) * scale * METRES, y = (lat - path[i-3]) * METRES;
          double squared = dx*dx + dy*dy;
          double fraction = squared == 0 ? 0 : Math.max(0, Math.min(1, (x*dx + y*dy) / squared));
          double offset = Math.hypot(x - fraction*dx, y - fraction*dy);
          if (offset <= maxKm * 1000 && (nearest == null || offset < best))
          {
            best = offset;
            nearest = new Place(props.optString("name", labels[category]), lat, lon, (int)path[i+2], along[i/3 - 1] + fraction * (along[i/3] - along[i/3 - 1]), offset);
          }
        }
        if (nearest != null) places.put(id, nearest);
      }
    }
    finally { connection.disconnect(); }
  }
}
