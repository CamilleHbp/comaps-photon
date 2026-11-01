package app.organicmaps.bookmarks;
import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import androidx.fragment.app.Fragment;
import app.organicmaps.R;
import app.organicmaps.base.BaseToolbarActivity;
import java.util.ArrayList;
import java.util.List;

public class OsmAndImportActivity extends BaseToolbarActivity
{
  public static void start(Activity activity) { activity.startActivity(new Intent(activity, OsmAndImportActivity.class)); }
  public static void start(Activity activity, List<Uri> uris)
  {
    Intent intent = new Intent(activity, OsmAndImportActivity.class);
    intent.putParcelableArrayListExtra("documents", new ArrayList<>(uris));
    activity.startActivity(intent);
  }
  /** Inspect shares off the UI thread; keep generic GPX/KML/KMZ on their existing path. */
  public static void routeShared(app.organicmaps.MwmActivity activity, List<Uri> uris)
  {
    var resolver = activity.getContentResolver();
    app.organicmaps.sdk.util.concurrency.ThreadPool.getStorage().execute(() -> {
      List<Uri> osmand = new ArrayList<>();
      List<Uri> ordinary = new ArrayList<>();
      for (Uri uri : uris)
      {
        if (uri == null) continue;
        boolean recognized = false;
        try (java.io.InputStream raw = resolver.openInputStream(uri))
        {
          if (raw != null)
          {
            var input = new java.io.BufferedInputStream(raw);
            input.mark(4);
            boolean zip = input.read() == 'P' && input.read() == 'K';
            input.reset();
            var docs = app.organicmaps.sdk.bookmarks.data.OsmAndArchive.read(input, zip);
            long token = app.organicmaps.sdk.bookmarks.data.OsmAndImport.nativePrepare(
                docs.toArray(new String[0]), activity.getString(R.string.osmand_import_ungrouped));
            recognized = token > 0;
            if (token > 0) app.organicmaps.sdk.bookmarks.data.OsmAndImport.nativeDiscard(token);
          }
        }
        catch (Exception ignored) {}
        (recognized ? osmand : ordinary).add(uri);
      }
      if (!ordinary.isEmpty())
        app.organicmaps.sdk.bookmarks.data.BookmarkManager.INSTANCE.importBookmarksFiles(resolver, ordinary,
            new java.io.File(app.organicmaps.sdk.util.StorageUtils.getTempPath(activity.getApplicationContext())));
      if (!osmand.isEmpty()) activity.runOnUiThread(() -> {
        if (!activity.isFinishing() && !activity.isDestroyed()) start(activity, osmand);
      });
    });
  }
  @Override protected int getToolbarTitle() { return R.string.osmand_import_title; }
  @Override protected Class<? extends Fragment> getFragmentClass() { return OsmAndImportFragment.class; }
}
