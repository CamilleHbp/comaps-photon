package app.organicmaps.bookmarks;
import android.app.Application;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import androidx.annotation.NonNull;
import androidx.lifecycle.AndroidViewModel;
import androidx.lifecycle.MutableLiveData;
import app.organicmaps.R;
import app.organicmaps.sdk.bookmarks.data.OsmAndArchive;
import app.organicmaps.sdk.bookmarks.data.OsmAndImport;
import app.organicmaps.sdk.bookmarks.data.BookmarkManager;
import app.organicmaps.sdk.util.concurrency.ThreadPool;
import org.json.JSONArray;
import org.json.JSONObject;
import java.io.BufferedInputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

public class OsmAndImportModel extends AndroidViewModel
{
  public final MutableLiveData<Integer> changes = new MutableLiveData<>(0);
  public JSONArray groups = new JSONArray();
  public boolean[] selected = new boolean[0];
  public boolean busy, finished, started;
  public int skipped, added, duplicates;
  public final List<String> failures = new ArrayList<>();
  public String error;
  private long token;
  private boolean cleared, committing;
  private int next;
  private final Handler main = new Handler(Looper.getMainLooper());
  public OsmAndImportModel(@NonNull Application app) { super(app); }
  private void changed() { changes.setValue(changes.getValue() + 1); }
  public void prepare(List<Uri> uris)
  {
    if (busy) return;
    discard();
    started = busy = true;
    finished = false;
    error = null;
    groups = new JSONArray();
    failures.clear();
    added = duplicates = skipped = 0;
    changed();
    ThreadPool.getStorage().execute(() -> {
      long prepared;
      try
      {
        List<String> documents = new ArrayList<>();
        int bytes = 0;
        if (uris.size() > 64) throw new IllegalArgumentException();
        for (Uri uri : uris)
        {
          try (InputStream raw = getApplication().getContentResolver().openInputStream(uri))
          {
            if (raw == null) throw new IllegalArgumentException();
            BufferedInputStream input = new BufferedInputStream(raw);
            input.mark(4);
            boolean zip = input.read() == 'P' && input.read() == 'K';
            input.reset();
            for (String document : OsmAndArchive.read(input, zip))
            {
              bytes += document.getBytes(java.nio.charset.StandardCharsets.UTF_8).length;
              if (bytes > OsmAndArchive.MAX_BYTES) throw new IllegalArgumentException();
              documents.add(document);
            }
          }
        }
        prepared = OsmAndImport.nativePrepare(documents.toArray(new String[0]),
            getApplication().getString(R.string.osmand_import_ungrouped));
      }
      catch (Exception ignored) { prepared = -1; }
      final long ready = prepared;
      main.post(() -> showPreview(ready));
    });
  }
  private void showPreview(long ready)
  {
    if (cleared) { if (ready > 0) OsmAndImport.nativeDiscard(ready); return; }
    if (BookmarkManager.INSTANCE.isAsyncBookmarksLoadingInProgress())
    {
      main.postDelayed(() -> showPreview(ready), 100);
      return;
    }
    busy = false;
    token = ready;
    if (ready <= 0)
      error = getApplication().getString(ready == -2 ? R.string.osmand_import_not_osmand : R.string.osmand_import_error);
    else
    {
      try
      {
        JSONObject preview = new JSONObject(OsmAndImport.nativePreview(token));
        groups = preview.getJSONArray("groups");
        skipped = preview.getInt("skipped");
        selected = new boolean[groups.length()];
        java.util.Arrays.fill(selected, true);
        if (groups.length() == 0) error = getApplication().getString(R.string.osmand_import_empty);
      }
      catch (Exception e) { error = getApplication().getString(R.string.osmand_import_error); }
    }
    changed();
  }

  public void commit()
  {
    if (busy || token <= 0) return;
    busy = committing = true;
    next = added = duplicates = 0;
    failures.clear();
    changed();
    commitNext();
  }
  private void commitNext()
  {
    while (next < selected.length && !selected[next]) ++next;
    if (next == selected.length)
    {
      busy = committing = false;
      finished = true;
      BookmarkManager.INSTANCE.onBookmarksChanged();
      if (cleared) discard(); else changed();
      return;
    }
    final int index = next++;
    OsmAndImport.nativeCommit(token, index, (success, count, duplicateCount) -> {
      added += count;
      duplicates += duplicateCount;
      if (!success) failures.add(groups.optJSONObject(index).optString("name"));
      commitNext();
    });
  }
  private void discard() { if (token > 0) OsmAndImport.nativeDiscard(token); token = 0; }
  @Override protected void onCleared() { cleared = true; if (!committing) discard(); }
}
