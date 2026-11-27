package app.organicmaps.search;

import java.util.ArrayList;
import java.util.List;

/** Maps section rows to result indexes; headers must never reach native selection. */
final class SearchRows
{
  static final int ONLINE_HEADER = -1;
  static final int OFFLINE_HEADER = -2;
  private final List<Integer> mRows = new ArrayList<>();

  SearchRows(boolean grouped, boolean[] online)
  {
    if (grouped)
    {
      mRows.add(ONLINE_HEADER);
      for (int i = 0; i < online.length; ++i)
        if (online[i])
          mRows.add(i);
      if (mRows.size() - 1 < online.length)
        mRows.add(OFFLINE_HEADER);
    }
    for (int i = 0; i < online.length; ++i)
      if (!grouped || !online[i])
        mRows.add(i);
  }

  int size()
  {
    return mRows.size();
  }
  int resultIndex(int row)
  {
    return mRows.get(row);
  }
}
