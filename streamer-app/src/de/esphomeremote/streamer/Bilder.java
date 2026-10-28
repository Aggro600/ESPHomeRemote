package de.esphomeremote.streamer;

import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.drawable.Drawable;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

// Cover-Bilder der Medien-Sitzungen (2026-10-11). Viele Apps (YouTube, YouTube Music, Spotify, Plex ...) liefern das
// Bild nicht als URL, sondern als Bitmap in den Metadaten. Wir rechnen es als JPEG herunter und stellen es im
// Heimnetz bereit: http://<streamer>:8790/art/<paket>.jpg - Home Assistant holt es ab (smarttube_cover_resize.py)
// und rechnet es fuer die Fernbedienungen um. Nur Lesen, nur diese Bilder, nichts sonst.
// Seit Version 4: /apps (Liste "paket<TAB>name" der TV-Apps) und /icon/<paket>.png (Banner, sonst Symbol der App) -
// daraus baut tools/make_app_covers.py fuer jede installierte App ein eigenes Cover der SD-Karte.
final class Bilder {
    static final int PORT = 8790;
    private static final int MAX_PX = 480;

    private static final class Eintrag {
        Bitmap quelle;
        int generation;
        byte[] jpeg;
        String hash;
    }

    private static final Map<String, Eintrag> bilder = Collections.synchronizedMap(new HashMap<>());
    private static Thread server;
    private static Context ctx;

    static void init(Context c) {
        ctx = c.getApplicationContext();
        starten();
    }

    // Bild einer App setzen (null = keins). Gibt die URL zurueck, unter der es abrufbar ist, oder null.
    static String setze(String pkg, Bitmap b) {
        if (b == null) {
            bilder.remove(pkg);
            return null;
        }
        Eintrag e = bilder.get(pkg);
        if (e == null || e.quelle != b || e.generation != b.getGenerationId()) {
            try {
                Bitmap k = b;
                final int gross = Math.max(b.getWidth(), b.getHeight());
                if (gross > MAX_PX) {
                    final float f = MAX_PX / (float) gross;
                    k = Bitmap.createScaledBitmap(b, Math.max(1, Math.round(b.getWidth() * f)), Math.max(1, Math.round(b.getHeight() * f)), true);
                }
                ByteArrayOutputStream o = new ByteArrayOutputStream(64 * 1024);
                k.compress(Bitmap.CompressFormat.JPEG, 85, o);
                e = new Eintrag();
                e.quelle = b;
                e.generation = b.getGenerationId();
                e.jpeg = o.toByteArray();
                e.hash = Integer.toHexString(java.util.Arrays.hashCode(e.jpeg));
                bilder.put(pkg, e);
            } catch (Exception ex) {
                Log.w(Zustand.TAG, "Bild " + pkg + ": " + ex);
                return null;
            }
        }
        starten();
        final String ip = eigeneIp();
        return ip == null ? null : "http://" + ip + ":" + PORT + "/art/" + pkg + ".jpg?v=" + e.hash;
    }

    private static String eigeneIp() {
        try {
            for (NetworkInterface n : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!n.isUp() || n.isLoopback()) continue;
                for (InetAddress a : Collections.list(n.getInetAddresses()))
                    if (a instanceof Inet4Address && a.isSiteLocalAddress()) return a.getHostAddress();
            }
        } catch (Exception ignored) {
        }
        return null;
    }

    private static synchronized void starten() {
        if (server != null && server.isAlive()) return;
        server = new Thread(() -> {
            try (ServerSocket ss = new ServerSocket(PORT)) {
                Log.i(Zustand.TAG, "Bilder: Port " + PORT);
                while (true) {
                    try (Socket s = ss.accept()) {
                        s.setSoTimeout(4000);
                        antworten(s);
                    } catch (Exception ex) {
                        Log.w(Zustand.TAG, "Bilder: " + ex);
                    }
                }
            } catch (Exception ex) {
                Log.w(Zustand.TAG, "Bilder-Server: " + ex);
            }
        }, "er_bilder");
        server.setDaemon(true);
        server.start();
    }

    private static void antworten(Socket s) throws Exception {
        InputStream in = s.getInputStream();
        StringBuilder zeile = new StringBuilder();
        int c;
        while ((c = in.read()) != -1 && c != '\n' && zeile.length() < 512) zeile.append((char) c);
        // Restliche Kopfzeilen bis zur Leerzeile lesen - sonst setzt das Schliessen mit ungelesenen Daten die
        // Verbindung zurueck (Python/urllib: "Connection reset by peer").
        int leer = 0, n = 0;
        while (leer < 2 && n < 8192 && (c = in.read()) != -1) {
            n++;
            if (c == '\n') leer++; else if (c != '\r') leer = 0;
        }
        // "GET /art/<paket>.jpg?v=... HTTP/1.1"
        String pfad = zeile.toString().split(" ").length > 1 ? zeile.toString().split(" ")[1] : "";
        final int q = pfad.indexOf('?');
        if (q >= 0) pfad = pfad.substring(0, q);
        OutputStream o = s.getOutputStream();
        if (pfad.equals("/apps")) {
            senden(o, "text/plain; charset=utf-8", apps().getBytes(StandardCharsets.UTF_8));
            return;
        }
        if (pfad.startsWith("/icon/") && pfad.endsWith(".png")) {
            senden(o, "image/png", grafik(pfad.substring(6, pfad.length() - 4)));
            return;
        }
        Eintrag e = null;
        if (pfad.startsWith("/art/") && pfad.endsWith(".jpg")) e = bilder.get(pfad.substring(5, pfad.length() - 4));
        if (e == null) {
            o.write("HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".getBytes(StandardCharsets.US_ASCII));
        } else {
            o.write(("HTTP/1.0 200 OK\r\nContent-Type: image/jpeg\r\nContent-Length: " + e.jpeg.length
                    + "\r\nCache-Control: no-cache\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            o.write(e.jpeg);
        }
        o.flush();
        s.shutdownOutput();
    }

    private static void senden(OutputStream o, String typ, byte[] daten) throws Exception {
        if (daten == null) {
            o.write("HTTP/1.0 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n".getBytes(StandardCharsets.US_ASCII));
        } else {
            o.write(("HTTP/1.0 200 OK\r\nContent-Type: " + typ + "\r\nContent-Length: " + daten.length
                    + "\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));
            o.write(daten);
        }
        o.flush();
    }

    // TV-Apps (Leanback-Starter, sonst normaler Starter): "paket<TAB>Name" je Zeile
    private static String apps() {
        if (ctx == null) return "";
        PackageManager pm = ctx.getPackageManager();
        Map<String, String> m = new java.util.TreeMap<>();
        for (String kat : new String[] {Intent.CATEGORY_LEANBACK_LAUNCHER, Intent.CATEGORY_LAUNCHER}) {
            Intent i = new Intent(Intent.ACTION_MAIN).addCategory(kat);
            for (ResolveInfo r : pm.queryIntentActivities(i, 0)) {
                final String pkg = r.activityInfo.packageName;
                if (!m.containsKey(pkg)) m.put(pkg, String.valueOf(r.activityInfo.applicationInfo.loadLabel(pm)).replace('\t', ' ').replace('\n', ' '));
            }
        }
        StringBuilder b = new StringBuilder();
        for (Map.Entry<String, String> e : m.entrySet()) b.append(e.getKey()).append('\t').append(e.getValue()).append('\n');
        return b.toString();
    }

    // Banner der App (Android-TV-Kachel 320x180), sonst ihr Symbol - als PNG
    private static byte[] grafik(String pkg) {
        if (ctx == null) return null;
        try {
            PackageManager pm = ctx.getPackageManager();
            Drawable d = null;
            Intent i = pm.getLeanbackLaunchIntentForPackage(pkg);
            if (i != null && i.getComponent() != null) d = pm.getActivityBanner(i.getComponent());
            if (d == null) d = pm.getApplicationBanner(pkg);
            if (d == null) d = pm.getApplicationIcon(pkg);
            int w = d.getIntrinsicWidth(), h = d.getIntrinsicHeight();
            if (w <= 0 || h <= 0) { w = 320; h = 180; }
            final float f = Math.min(1f, 640f / Math.max(w, h));
            w = Math.max(1, Math.round(w * f));
            h = Math.max(1, Math.round(h * f));
            Bitmap b = Bitmap.createBitmap(w, h, Bitmap.Config.ARGB_8888);
            d.setBounds(0, 0, w, h);
            d.draw(new Canvas(b));
            ByteArrayOutputStream o = new ByteArrayOutputStream();
            b.compress(Bitmap.CompressFormat.PNG, 100, o);
            return o.toByteArray();
        } catch (Exception ex) {
            Log.w(Zustand.TAG, "Grafik " + pkg + ": " + ex);
            return null;
        }
    }
}
