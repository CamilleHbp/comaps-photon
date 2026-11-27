package app.organicmaps.search;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class SearchRowsTest
{
  @Test
  public void offlineResultsAreVisibleBeforeOnlineArrives()
  {
    SearchRows rows = new SearchRows(true, new boolean[] {false, false});
    assertEquals(4, rows.size());
    assertEquals(SearchRows.ONLINE_HEADER, rows.resultIndex(0));
    assertEquals(SearchRows.OFFLINE_HEADER, rows.resultIndex(1));
    assertEquals(0, rows.resultIndex(2));
    assertEquals(1, rows.resultIndex(3));
  }

  @Test
  public void groupHeadersDoNotChangeResultIdentity()
  {
    // Interleave providers to ensure grouping keeps their original result indexes.
    SearchRows rows = new SearchRows(true, new boolean[] {false, true, false, true});
    int[] expected = {SearchRows.ONLINE_HEADER, 1, 3, SearchRows.OFFLINE_HEADER, 0, 2};
    assertEquals(expected.length, rows.size());
    for (int i = 0; i < expected.length; ++i)
      assertEquals(expected[i], rows.resultIndex(i));
  }

  @Test
  public void categoriesAndOfflineOnlySearchHaveNoHeaders()
  {
    SearchRows rows = new SearchRows(false, new boolean[] {false, false});
    assertEquals(2, rows.size());
    assertEquals(0, rows.resultIndex(0));
    assertEquals(1, rows.resultIndex(1));
    assertEquals(0, new SearchRows(false, new boolean[0]).size());
  }

  @Test
  public void emptyOnlineSearchStillHasRoomForStatus()
  {
    SearchRows rows = new SearchRows(true, new boolean[0]);
    assertEquals(1, rows.size());
    assertEquals(SearchRows.ONLINE_HEADER, rows.resultIndex(0));
  }
}
