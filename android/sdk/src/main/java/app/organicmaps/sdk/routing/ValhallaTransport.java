package app.organicmaps.sdk.routing;

import androidx.annotation.Keep;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URI;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Future;
import java.util.concurrent.ThreadPoolExecutor;
import java.util.concurrent.TimeUnit;

/** Bounded, cancellable transport. No cookies, credentials, redirects or response caching. */
@Keep
public final class ValhallaTransport
{
  private static ThreadPoolExecutor pool(String name)
  {
    return new ThreadPoolExecutor(2, 2, 30, TimeUnit.SECONDS, new ArrayBlockingQueue<>(2), runnable -> {
      Thread thread = new Thread(runnable, name);
      thread.setDaemon(true);
      return thread;
    }, new ThreadPoolExecutor.AbortPolicy());
  }
  private static final ThreadPoolExecutor NETWORK = pool("Valhalla HTTP");
  private static final ThreadPoolExecutor DISCONNECT = pool("Valhalla cancel");

  interface ConnectionFactory { HttpURLConnection open(URI uri) throws Exception; }

  public static Request start(String endpoint, String body)
  {
    return start(endpoint, body, uri -> (HttpURLConnection) uri.toURL().openConnection());
  }

  static Request start(String endpoint, String body, ConnectionFactory factory)
  {
    Request request = new Request();
    try { request.task = NETWORK.submit(() -> request.run(endpoint, body, factory)); }
    catch (RuntimeException error) { request.finish(5, ""); }
    return request;
  }

  @Keep
  public static final class Request
  {
    // JNI status contract: pending, success, cancelled, timeout, busy, service error.
    private int status;
    private String body = "";
    private volatile boolean cancelled;
    private volatile HttpURLConnection connection;
    private volatile Future<?> task;

    private synchronized void finish(int code, String value)
    {
      if (status != 0) return;
      body = value;
      status = code;
      notifyAll();
    }

    public synchronized int await(int millis)
    {
      if (status == 0)
      {
        try { wait(Math.max(1, Math.min(50, millis))); }
        catch (InterruptedException error) { Thread.currentThread().interrupt(); }
      }
      return status;
    }

    public synchronized String getBody() { return body; }

    public void cancel()
    {
      cancelled = true;
      finish(2, "");
      Future<?> current = task;
      if (current != null) current.cancel(true);
      HttpURLConnection active = connection;
      if (active != null)
      {
        // Some platform implementations synchronize disconnect with a read.
        // Never block the routing worker waiting for that lock.
        try { DISCONNECT.execute(active::disconnect); }
        catch (RuntimeException ignored) { /* Socket timeouts still bound the worker. */ }
      }
      NETWORK.purge();
    }

    private void run(String endpoint, String payload, ConnectionFactory factory)
    {
      HttpURLConnection active = null;
      try
      {
        URI uri = new URI(endpoint);
        if (!"https".equals(uri.getScheme()) || uri.getHost() == null || uri.getUserInfo() != null ||
            uri.getRawQuery() != null || uri.getRawFragment() != null || payload.length() > 256000)
          throw new IllegalArgumentException("Invalid routing request");
        if (cancelled) return;
        long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(15);
        active = factory.open(uri);
        connection = active;
        if (cancelled) return;
        active.setInstanceFollowRedirects(false);
        active.setUseCaches(false);
        active.setConnectTimeout(5000);
        active.setReadTimeout(1000);
        active.setRequestMethod("POST");
        active.setRequestProperty("Content-Type", "application/json");
        active.setRequestProperty("Accept", "application/json");
        active.setRequestProperty("X-Client-Id", "comaps-photon");
        active.setRequestProperty("Cache-Control", "no-store");
        active.setRequestProperty("Cookie", "");
        active.setDoOutput(true);
        byte[] bytes = payload.getBytes(StandardCharsets.UTF_8);
        active.setFixedLengthStreamingMode(bytes.length);
        try (var output = active.getOutputStream())
        {
          if (cancelled) return; // Connecting may have completed after consent was revoked.
          output.write(bytes);
        }
        if (cancelled) return;
        int code = active.getResponseCode();
        if (code == 429 || code == 503) { finish(4, ""); return; }
        if (code != 200) { finish(5, ""); return; }
        if (active.getContentLength() > 16000000) { finish(5, ""); return; }
        try (InputStream input = active.getInputStream(); ByteArrayOutputStream output = new ByteArrayOutputStream())
        {
          byte[] buffer = new byte[16384];
          while (!cancelled)
          {
            if (System.nanoTime() >= deadline) { finish(3, ""); return; }
            int size = input.read(buffer);
            if (size < 0) break;
            if (output.size() + size > 16000000) { finish(5, ""); return; }
            output.write(buffer, 0, size);
          }
          if (!cancelled) finish(1, output.toString(StandardCharsets.UTF_8.name()));
        }
      }
      catch (java.net.SocketTimeoutException error) { finish(3, ""); }
      catch (Exception error) { finish(5, ""); }
      finally
      {
        if (active != null) active.disconnect();
        connection = null;
      }
    }
  }
}
