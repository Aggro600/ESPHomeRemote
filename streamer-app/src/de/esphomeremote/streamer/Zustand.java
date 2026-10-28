package de.esphomeremote.streamer;

import android.content.Context;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import android.provider.Settings;
import android.util.Log;

import org.json.JSONObject;

import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;

// Gemeinsamer Stand beider Dienste (Medien, Bedienung) und das Senden an Home Assistant.
//
// Der Text hat dasselbe Format wie frueher die ADB-Abfrage (dumpsys media_session / activity / input_method +
// SmartTubeMeta), damit die Sensoren in packages/esphomeremote_smarttube.yaml unveraendert weiterlaufen.
// Gesendet wird nur, wenn sich etwas Sichtbares aendert (Sitzung, Titel, Zustand, Sprung der Position, App,
// Tastatur) - 80 ms gesammelt - plus alle 5 min ein Lebenszeichen (HA-Neustart).
// Ziel-URL: settings put global esphomeremote_webhook http://<ha>:8123/api/webhook/<id>
final class Zustand {
    static final String TAG = "ERStreamer";
    // SAMMELN_MS 300 -> 80 (2026-10-26): Tastatur-Popup schneller; Bursts sind nach 80 ms meist durch
    private static final long SAMMELN_MS = 80, HERZ_MS = 5 * 60 * 1000, WIEDERHOLEN_MS = 10_000;
    private static Zustand inst;

    static synchronized Zustand get(Context c) {
        if (inst == null) inst = new Zustand(c.getApplicationContext());
        return inst;
    }

    private final Context ctx;
    private final Handler h;

    // Teile (von den Diensten gesetzt)
    private String meta = "";       // TITEL=/ARTIST=/COVER=/STATE= und DUR:-Zeilen (wie SmartTubeMeta)
    private String sitzungen = "";  // package=/state=PlaybackState/metadata:-Bloecke (wie dumpsys media_session)
    private String sitzSig = "";    // dasselbe ohne laufende Position (nur Spruenge zaehlen)
    private String top = "";
    private boolean tastatur;
    private int eingabeArt = 1;

    private String gesendet;        // Signatur des zuletzt erfolgreich gesendeten Stands
    // Fuer die Start-Seite (2026-10-26): wann zuletzt erfolgreich gemeldet, letzter Fehler (null = keiner)
    static volatile long letzteOkMs = 0;
    static volatile String letzterFehler = null;

    private Zustand(Context c) {
        ctx = c;
        HandlerThread t = new HandlerThread("er_senden");
        t.start();
        h = new Handler(t.getLooper());
        h.postDelayed(herz, HERZ_MS);
    }

    private final Runnable senden = this::senden;
    private final Runnable herz = new Runnable() {
        @Override public void run() {
            synchronized (Zustand.this) { gesendet = null; }
            senden();
            h.postDelayed(this, HERZ_MS);
        }
    };

    synchronized void setMedien(String meta, String sitzungen, String sitzSig) {
        this.meta = meta;
        this.sitzungen = sitzungen;
        this.sitzSig = sitzSig;
        planen();
    }

    synchronized void setBedienung(String top, boolean tastatur, int eingabeArt) {
        if (top != null) this.top = top;
        this.tastatur = tastatur;
        this.eingabeArt = eingabeArt;
        planen();
    }

    // Start-Seite "Jetzt melden": Stand sofort neu schicken
    void erneutSenden() {
        synchronized (this) { gesendet = null; }
        planen();
    }

    private void planen() {
        h.removeCallbacks(senden);
        h.postDelayed(senden, SAMMELN_MS);
    }

    private synchronized String signatur() {
        return meta + "|" + sitzSig + "|" + top + "|" + tastatur + "|" + (tastatur ? eingabeArt : 0);
    }

    private synchronized String text() {
        StringBuilder b = new StringBuilder(2048);
        b.append(meta);
        b.append("---SESS\n").append(sitzungen);
        b.append("---UP\n").append(String.format(java.util.Locale.ROOT, "%.2f", SystemClock.elapsedRealtime() / 1000.0)).append('\n');
        b.append("---TOP\n");
        if (!top.isEmpty()) b.append("topResumedActivity=ActivityRecord{0 u0 ").append(top).append("/.App t0}\n");
        b.append("---IME\n").append("mInputShown=").append(tastatur).append('\n');
        if (tastatur) b.append("  editorInfo:  inputType=").append(eingabeArt).append('\n');
        return b.toString();
    }

    private void senden() {
        final String sig = signatur();
        synchronized (this) {
            if (sig.equals(gesendet)) return;
        }
        final String url = Settings.Global.getString(ctx.getContentResolver(), "esphomeremote_webhook");
        if (url == null || url.isEmpty()) {
            Log.w(TAG, "Keine Ziel-URL (settings put global esphomeremote_webhook ...)");
            letzterFehler = "Noch nicht mit Home Assistant verbunden";
            return;
        }
        HttpURLConnection c = null;
        try {
            byte[] daten = new JSONObject().put("text", text()).toString().getBytes(StandardCharsets.UTF_8);
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(4000);
            c.setReadTimeout(4000);
            c.setDoOutput(true);
            c.setRequestMethod("POST");
            c.setRequestProperty("Content-Type", "application/json");
            c.setFixedLengthStreamingMode(daten.length);
            try (OutputStream o = c.getOutputStream()) { o.write(daten); }
            final int code = c.getResponseCode();
            if (code >= 200 && code < 300) {
                synchronized (this) { gesendet = sig; }
                letzteOkMs = System.currentTimeMillis();
                letzterFehler = null;
            } else {
                Log.w(TAG, "HA antwortet " + code);
                letzterFehler = "Home Assistant antwortet " + code;
                h.postDelayed(senden, WIEDERHOLEN_MS);
            }
        } catch (Exception e) {
            Log.w(TAG, "Senden fehlgeschlagen: " + e);
            letzterFehler = "Home Assistant nicht erreichbar";
            h.removeCallbacks(senden);
            h.postDelayed(senden, WIEDERHOLEN_MS);
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
