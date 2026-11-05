package de.esphomeremote.streamer;

import android.content.Context;
import android.database.ContentObserver;
import android.os.Handler;
import android.provider.Settings;
import android.util.Log;
import android.view.InputDevice;
import android.view.KeyEvent;

import org.json.JSONObject;

import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.HashSet;
import java.util.Set;

// Tasten umbelegen (2026-11-05, App 1.16): die Bedienungshilfe sieht jede Taste, bevor Android sie verteilt - auch die
// Tasten der Fernseher-Fernbedienung, die der Fernseher per HDMI-CEC an den Stick weiterreicht.
//   settings put global esphomeremote_tasten "183,186"      -> diese Tastencodes gehoeren Home Assistant: die App
//       meldet "kurz" (losgelassen vor LANG_MS) bzw. "lang" (beim Erreichen von LANG_MS) und gibt sie NICHT weiter
//   settings put global esphomeremote_tasten_lernen 1        -> jede Taste melden (art "lernen", mit Geraetename),
//       weiter normal verteilen - so sieht man, was welche Fernbedienung schickt
// Ziel ist der Webhook der Integration (esphomeremote_webhook), JSON {"taste": {...}} -> Ereignis
// esphomeremote_streamer_taste in Home Assistant. Gilt auch im Modus "Nur Kamera".
final class Tasten {
    private static final String TAG = "ESPHomeRemoteTasten";
    static final String EINST_TASTEN = "esphomeremote_tasten";
    static final String EINST_LERNEN = "esphomeremote_tasten_lernen";
    private static final long LANG_MS = 600;

    private final Context ctx;
    private final Handler h;
    private final ContentObserver beob;
    private volatile Set<Integer> uebernommen = new HashSet<>();
    private volatile boolean lernen;
    private volatile int menueTaste = -1;          // oeffnet das Popup-Menue (Menue.java)
    private volatile String[] menuePunkte = new String[0];
    private int unten = -1;
    private boolean langGemeldet;
    private final Runnable lang = this::langErreicht;

    Tasten(Context c, Handler h) {
        this.ctx = c;
        this.h = h;
        beob = new ContentObserver(h) {
            @Override public void onChange(boolean selbst) { laden(); }
        };
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(EINST_TASTEN), false, beob);
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(EINST_LERNEN), false, beob);
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(Menue.EINST_TASTE), false, beob);
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(Menue.EINST_PUNKTE), false, beob);
        laden();
        Menue.setMelder(this);
    }

    void aus() {
        try { ctx.getContentResolver().unregisterContentObserver(beob); } catch (Exception ignored) { }
        h.removeCallbacks(lang);
    }

    private void laden() {
        Set<Integer> s = new HashSet<>();
        String t = Settings.Global.getString(ctx.getContentResolver(), EINST_TASTEN);
        if (t != null) {
            for (String teil : t.split("[,; ]+")) {
                try { if (!teil.isEmpty()) s.add(Integer.parseInt(teil.trim())); } catch (NumberFormatException ignored) { }
            }
        }
        int mt = -1;
        try { mt = Integer.parseInt(String.valueOf(Settings.Global.getString(ctx.getContentResolver(), Menue.EINST_TASTE)).trim()); }
        catch (NumberFormatException ignored) { }
        menueTaste = mt;
        String mp = Settings.Global.getString(ctx.getContentResolver(), Menue.EINST_PUNKTE);
        menuePunkte = mp == null || mp.isEmpty() ? new String[0] : mp.split("\\|");
        if (mt >= 0) s.add(mt);
        uebernommen = s;
        lernen = "1".equals(Settings.Global.getString(ctx.getContentResolver(), EINST_LERNEN));
        Log.i(TAG, "Tasten fuer Home Assistant: " + s + (lernen ? " (Lernmodus)" : ""));
    }

    // true = die Taste gehoert Home Assistant (Android bekommt sie nicht)
    boolean ereignis(KeyEvent e) {
        final int code = e.getKeyCode();
        if (WebMenue.offen() && code != menueTaste && WebMenue.taste(e)) return true;   // Web-Menue offen (1.17)
        if (Menue.offen() && code != menueTaste && Menue.taste(e)) return true;   // Menue offen: Pfeile/OK/Zurueck dorthin
        if (lernen && e.getAction() == KeyEvent.ACTION_DOWN && e.getRepeatCount() == 0) melden(code, "lernen", e);
        if (!uebernommen.contains(code)) return false;
        if (e.getAction() == KeyEvent.ACTION_DOWN) {
            if (e.getRepeatCount() == 0) {
                unten = code;
                langGemeldet = false;
                h.removeCallbacks(lang);
                h.postDelayed(lang, LANG_MS);
            }
            return true;
        }
        if (e.getAction() == KeyEvent.ACTION_UP) {
            h.removeCallbacks(lang);
            if (unten == code && !langGemeldet) {
                if (code == menueTaste && !WebMenue.url(ctx).isEmpty()) {   // Menue aus dem Konfigurator (1.17)
                    if (WebMenue.offen()) WebMenue.zu();
                    else WebMenue.zeigen((android.accessibilityservice.AccessibilityService) ctx);
                } else if (code == menueTaste && menuePunkte.length > 0) {
                    if (Menue.offen()) Menue.zu();   // dieselbe Taste noch einmal: Menue zu
                    else Menue.zeigen((android.accessibilityservice.AccessibilityService) ctx, "", menuePunkte, "haupt");
                } else {
                    melden(code, "kurz", e);
                }
            }
            unten = -1;
        }
        return true;
    }

    private void langErreicht() {
        if (unten < 0) return;
        langGemeldet = true;
        melden(unten, "lang", null);
    }

    private void melden(int code, String art, KeyEvent e) {
        String quelle = "";
        if (e != null) {
            InputDevice d = InputDevice.getDevice(e.getDeviceId());
            if (d != null) quelle = d.getName();
        }
        final String q = quelle;
        new Thread(() -> senden(code, art, q, false), "taste").start();
    }

    // Beliebige Meldung an Home Assistant (Menue-Wahl)
    void meldenJson(JSONObject j) {
        new Thread(() -> posten(j), "meldung").start();
    }

    // Fuer CecTasten: Tasten der Fernseher-Fernbedienung (CEC-Tastencode statt Android-Tastencode)
    void meldenExtern(int code, String art, String quelle, boolean cec) {
        new Thread(() -> senden(code, art, quelle, cec), "taste").start();
    }

    private void senden(int code, String art, String quelle, boolean cec) {
        try {
            JSONObject t = new JSONObject().put("code", code)
                    .put("name", cec ? String.format("CEC_0x%02X", code) : KeyEvent.keyCodeToString(code))
                    .put("art", art).put("quelle", quelle).put("cec", cec);
            posten(new JSONObject().put("taste", t));
        } catch (Exception ex) {
            Log.w(TAG, "Taste nicht gemeldet: " + ex);
        }
    }

    private void posten(JSONObject j) {
        final String url = Settings.Global.getString(ctx.getContentResolver(), "esphomeremote_webhook");
        if (url == null || url.isEmpty()) {
            Log.w(TAG, "Keine Ziel-URL - nicht gemeldet: " + j);
            return;
        }
        HttpURLConnection c = null;
        try {
            byte[] daten = j.toString().getBytes(StandardCharsets.UTF_8);
            c = (HttpURLConnection) new URL(url).openConnection();
            c.setConnectTimeout(3000);
            c.setReadTimeout(3000);
            c.setDoOutput(true);
            c.setRequestMethod("POST");
            c.setRequestProperty("Content-Type", "application/json");
            c.setFixedLengthStreamingMode(daten.length);
            try (OutputStream o = c.getOutputStream()) { o.write(daten); }
            c.getResponseCode();
        } catch (Exception ex) {
            Log.w(TAG, "Taste nicht gemeldet: " + ex);
        } finally {
            if (c != null) c.disconnect();
        }
    }
}
