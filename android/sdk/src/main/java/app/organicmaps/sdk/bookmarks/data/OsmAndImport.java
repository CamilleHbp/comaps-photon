package app.organicmaps.sdk.bookmarks.data;
import androidx.annotation.Keep;

/** Prepare on a worker; preview and commit on the GUI thread. Discard releases preview data. */
public final class OsmAndImport
{
  private OsmAndImport() {}
  public static long nativePrepare(String[] documents, String ungroupedName)
  {
    byte[][] utf8 = new byte[documents.length][];
    for (int i = 0; i < documents.length; ++i)
      utf8[i] = documents[i].getBytes(java.nio.charset.StandardCharsets.UTF_8);
    return nativePrepareUtf8(utf8, ungroupedName.getBytes(java.nio.charset.StandardCharsets.UTF_8));
  }
  private static native long nativePrepareUtf8(byte[][] documents, byte[] ungroupedName);
  public static native String nativePreview(long token);
  public static native void nativeDiscard(long token);
  public static native int[] nativeStyle(long categoryId);
  public static native void nativeCommit(long token, int groupIndex, Callback callback);
  @Keep public interface Callback
  {
    @Keep void onResult(boolean success, int added, int duplicates);
  }
}
