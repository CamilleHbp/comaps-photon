package app.organicmaps.sdk.routing;

import static org.junit.Assert.*;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.Test;

public class ValhallaTransportTest
{
  private static class Connection extends HttpURLConnection
  {
    final CountDownLatch outputClosed = new CountDownLatch(1);
    final ByteArrayOutputStream sent = new ByteArrayOutputStream() {
      @Override public void close() { outputClosed.countDown(); }
    };
    final CountDownLatch disconnected = new CountDownLatch(1);
    InputStream input = new ByteArrayInputStream("{\"name\":\"Arrivée\"}".getBytes(StandardCharsets.UTF_8));
    int code = 200;
    int length = -1;
    boolean timeout;
    Connection() throws Exception { super(new URL("https://routing.example/route")); }
    @Override public void disconnect() { disconnected.countDown(); }
    @Override public boolean usingProxy() { return false; }
    @Override public void connect() {}
    @Override public ByteArrayOutputStream getOutputStream() { return sent; }
    @Override public InputStream getInputStream() { return input; }
    @Override public int getContentLength() { return length; }
    @Override public int getResponseCode() throws SocketTimeoutException
    { if (timeout) throw new SocketTimeoutException(); return code; }
  }

  private static int finished(ValhallaTransport.Request request)
  {
    long deadline = System.nanoTime() + TimeUnit.SECONDS.toNanos(3);
    int status;
    do { status = request.await(25); } while (status == 0 && System.nanoTime() < deadline);
    assertNotEquals("Request did not complete", 0, status);
    return status;
  }

  @Test public void postIsUncachedAndDoesNotRedirect() throws Exception
  {
    Connection connection = new Connection();
    var request = ValhallaTransport.start("https://routing.example/route", "{\"locations\":[]}", uri -> connection);
    assertEquals(1, finished(request));
    assertEquals("{\"name\":\"Arrivée\"}", request.getBody());
    assertEquals("POST", connection.getRequestMethod());
    assertEquals("comaps-photon", connection.getRequestProperty("X-Client-Id"));
    assertEquals("", connection.getRequestProperty("Cookie"));
    assertFalse(connection.getUseCaches());
    assertFalse(connection.getInstanceFollowRedirects());
    assertEquals("{\"locations\":[]}", connection.sent.toString("UTF-8"));
  }

  @Test public void cancellationInterruptsTheDownloadAndDiscardsLateData() throws Exception
  {
    Connection connection = new Connection();
    CountDownLatch reading = new CountDownLatch(1);
    connection.input = new InputStream() {
      @Override public int read() {
        reading.countDown();
        try { connection.disconnected.await(3, TimeUnit.SECONDS); }
        catch (InterruptedException error) { Thread.currentThread().interrupt(); }
        return -1;
      }
    };
    var request = ValhallaTransport.start("https://routing.example/route", "{}", uri -> connection);
    assertTrue(reading.await(2, TimeUnit.SECONDS));
    request.cancel();
    assertEquals(2, request.await(1));
    assertEquals("", request.getBody());
    assertTrue(connection.disconnected.await(2, TimeUnit.SECONDS));
    assertEquals(2, request.await(1));
  }

  @Test public void rateLimitsTimeoutsRedirectsAndOversizedBodiesFailClosed() throws Exception
  {
    for (int code : new int[]{429, 503, 302, 401})
    {
      Connection connection = new Connection(); connection.code = code;
      assertEquals(code == 429 || code == 503 ? 4 : 5,
          finished(ValhallaTransport.start("https://routing.example/route", "{}", uri -> connection)));
    }
    Connection timeout = new Connection(); timeout.timeout = true;
    assertEquals(3, finished(ValhallaTransport.start("https://routing.example/route", "{}", uri -> timeout)));
    Connection oversized = new Connection(); oversized.length = 16000001;
    assertEquals(5, finished(ValhallaTransport.start("https://routing.example/route", "{}", uri -> oversized)));
  }

  @Test public void cancellationWhileConnectingDoesNotSendCoordinates() throws Exception
  {
    CountDownLatch connecting = new CountDownLatch(1);
    CountDownLatch connectionReady = new CountDownLatch(1);
    Connection connection = new Connection() {
      @Override public ByteArrayOutputStream getOutputStream() {
        connecting.countDown();
        try { connectionReady.await(2, TimeUnit.SECONDS); }
        catch (InterruptedException ignored) { /* Simulate a connection returning after cancellation. */ }
        return sent;
      }
    };
    var request = ValhallaTransport.start("https://routing.example/route", "{\"coordinates\":[]}", uri -> connection);
    assertTrue(connecting.await(2, TimeUnit.SECONDS));
    request.cancel();
    connectionReady.countDown();
    assertEquals(2, request.await(1));
    assertTrue(connection.outputClosed.await(2, TimeUnit.SECONDS));
    assertTrue(connection.disconnected.await(2, TimeUnit.SECONDS));
    assertEquals(0, connection.sent.size());
  }

  @Test public void invalidEndpointsNeverOpenAConnection()
  {
    AtomicInteger calls = new AtomicInteger();
    for (String endpoint : new String[]{"http://example.org", "https://user:password@example.org", "https://example.org?key=secret"})
    {
      assertEquals(5, finished(ValhallaTransport.start(endpoint, "{}", uri -> { calls.incrementAndGet(); return null; })));
      assertThrows(IllegalArgumentException.class, () -> OnlineRouting.normalizeEndpoint(endpoint));
    }
    assertEquals(0, calls.get());
    assertEquals("https://example.org/api/route", OnlineRouting.normalizeEndpoint("https://example.org/api/isochrone"));
  }
}
