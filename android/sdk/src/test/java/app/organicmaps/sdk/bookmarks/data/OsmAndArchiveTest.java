package app.organicmaps.sdk.bookmarks.data;
import static org.junit.Assert.*;
import org.junit.Test;
import java.io.*;
import java.util.zip.*;

public class OsmAndArchiveTest
{
  private byte[] zip(String name, byte[] content) throws IOException
  {
    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
    try (ZipOutputStream zip = new ZipOutputStream(bytes))
    {
      zip.putNextEntry(new ZipEntry(name));
      zip.write(content);
      zip.closeEntry();
    }
    return bytes.toByteArray();
  }
  @Test public void readsPlainAndCompressedFavorites() throws Exception
  {
    byte[] gpx = "<gpx creator=\"OsmAnd\"/>".getBytes(java.nio.charset.StandardCharsets.UTF_8);
    assertEquals(1, OsmAndArchive.read(new ByteArrayInputStream(gpx), false).size());
    assertEquals(new String(gpx), OsmAndArchive.read(new ByteArrayInputStream(zip("favorites.gpx", gpx)), true).get(0));
  }
  @Test public void rejectsTraversalEmptyCorruptAndOversizedArchives() throws Exception
  {
    for (String path : new String[]{"../favorites.gpx", "/favorites.gpx", "folder/../favorites.gpx", "C:/favorites.gpx"})
      assertThrows(IOException.class, () -> OsmAndArchive.read(new ByteArrayInputStream(zip(path, new byte[0])), true));
    assertThrows(IOException.class, () -> OsmAndArchive.read(new ByteArrayInputStream(new byte[0]), true));
    assertThrows(IOException.class, () -> OsmAndArchive.read(new ByteArrayInputStream(zip("favorites.gpx", new byte[OsmAndArchive.MAX_BYTES + 1])), true));
    byte[] bad = zip("favorites.gpx", new byte[100]);
    bad[40] ^= 127;
    assertThrows(IOException.class, () -> OsmAndArchive.read(new ByteArrayInputStream(bad), true));
  }
}
