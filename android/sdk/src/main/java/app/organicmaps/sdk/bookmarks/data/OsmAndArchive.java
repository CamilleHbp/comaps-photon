package app.organicmaps.sdk.bookmarks.data;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Bounded, extraction-free reader for GPX and OsmAnd favorites ZIP backups. */
public final class OsmAndArchive
{
  public static final int MAX_BYTES = 32 * 1024 * 1024;
  private OsmAndArchive() {}
  public static List<String> read(InputStream source, boolean zip) throws IOException
  {
    List<String> documents = new ArrayList<>();
    int remaining = MAX_BYTES;
    if (!zip)
    {
      documents.add(new String(readBounded(source, remaining), StandardCharsets.UTF_8));
      return documents;
    }
    try (ZipInputStream input = new ZipInputStream(source))
    {
      ZipEntry entry;
      int entries = 0;
      while ((entry = input.getNextEntry()) != null)
      {
        if (++entries > 256) throw new IOException("Too many ZIP entries");
        String name = entry.getName().replace('\\', '/');
        if (name.startsWith("/") || name.contains(":") || List.of(name.split("/")).contains(".."))
          throw new IOException("Unsafe ZIP path");
        byte[] bytes = readBounded(input, remaining);
        remaining -= bytes.length;
        if (!entry.isDirectory() && name.toLowerCase(Locale.ROOT).endsWith(".gpx"))
          documents.add(new String(bytes, StandardCharsets.UTF_8));
        input.closeEntry();
      }
    }
    if (documents.isEmpty()) throw new IOException("No GPX files in backup");
    return documents;
  }
  private static byte[] readBounded(InputStream input, int limit) throws IOException
  {
    ByteArrayOutputStream output = new ByteArrayOutputStream();
    byte[] buffer = new byte[8192];
    int count;
    while ((count = input.read(buffer)) != -1)
    {
      if (count > limit - output.size()) throw new IOException("Backup exceeds 32 MiB");
      output.write(buffer, 0, count);
    }
    return output.toByteArray();
  }
}
