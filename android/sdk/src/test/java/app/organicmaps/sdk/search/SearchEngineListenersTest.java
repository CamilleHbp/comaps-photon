package app.organicmaps.sdk.search;

import static org.junit.Assert.assertEquals;

import android.os.Looper;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;
import org.robolectric.Shadows;
import org.robolectric.annotation.Config;

@RunWith(RobolectricTestRunner.class)
@Config(manifest = Config.NONE, sdk = 28)
public class SearchEngineListenersTest
{
  private static class Listener implements SearchListener
  {
    int updates, ends, statuses;
    public void onResultsUpdate(SearchResult[] results, long timestamp)
    {
      ++updates;
    }
    public void onResultsEnd(long timestamp)
    {
      ++ends;
    }
    public void onPhotonStatus(int status, long timestamp)
    {
      ++statuses;
    }
  }

  @Test
  public void handheldSnapshotsDoNotReachCarSearch()
  {
    // Car search owns an offline index cache, so even an empty online update must stay separate.
    Listener car = new Listener();
    Listener phone = new Listener();
    SearchEngine engine = SearchEngine.INSTANCE;
    engine.addListener(car);
    engine.addHandheldListener(phone);
    try
    {
      engine.onHandheldResultsUpdate(new SearchResult[0], 7);
      engine.onPhotonStatus(SearchEngine.PHOTON_EMPTY, 7);
      engine.onHandheldResultsEnd(7);
      Shadows.shadowOf(Looper.getMainLooper()).idle();
      assertEquals(0, car.updates + car.ends + car.statuses);
      assertEquals(1, phone.updates);
      assertEquals(1, phone.ends);
      assertEquals(1, phone.statuses);

      engine.onResultsUpdate(new SearchResult[0], 8);
      engine.onResultsEnd(8);
      Shadows.shadowOf(Looper.getMainLooper()).idle();
      assertEquals(1, car.updates);
      assertEquals(1, car.ends);
      assertEquals(1, phone.updates);
      assertEquals(1, phone.ends);
    }
    finally
    {
      engine.removeListener(car);
      engine.removeHandheldListener(phone);
    }
  }
}
