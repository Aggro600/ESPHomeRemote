package de.esphomeremote.streamer;

import android.content.Context;
import android.database.ContentObserver;
import android.os.Handler;
import android.provider.Settings;
import android.util.Log;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Tasten der Fernseher-Fernbedienung (2026-11-05, App 1.16). Der Fernseher reicht sie per HDMI-CEC weiter; Android
// speist sie an der Bedienungshilfe VORBEI ein (Tasten.java sieht sie nicht). Der CEC-Treiber des Sticks schreibt aber
// jede empfangene Nachricht ins Systemprotokoll:
//   D/hdmi_cec_hw: [CECHAL] print_data_msg: initiator = 0, destination = 4, data[2] = 0x44 (Opcode: ...PRESSED) 0x 2
// Mit READ_LOGS (per ADB: pm grant de.esphomeremote.streamer android.permission.READ_LOGS) liest die App das mit.
//   settings put global esphomeremote_cec_tasten "0x72,0x71"   -> diese CEC-Tasten melden (kurz/lang)
//   esphomeremote_tasten_lernen 1                               -> jede CEC-Taste melden (art "lernen")
// Die Taste wirkt am Stick trotzdem weiter (abfangen geht nicht) - also Tasten ohne Funktion oder den langen Druck nehmen.
final class CecTasten {
    private static final String TAG = "ESPHomeRemoteCec";
    static final String EINST = "esphomeremote_cec_tasten";
    private static final long LANG_MS = 600;
    private static final Pattern GEDRUECKT = Pattern.compile("data\\[\\d+\\] = 0x44 .*\\) 0x\\s*([0-9a-fA-F]+)");
    private static final Pattern LOS = Pattern.compile("data\\[\\d+\\] = 0x45 ");

    private final Context ctx;
    private final Handler h;
    private final Tasten melder;
    private final ContentObserver beob;
    private volatile Set<Integer> tasten = new HashSet<>();
    private volatile boolean lernen;
    private volatile boolean menue;
    private volatile boolean laeuft = true;
    private Thread leser;
    private Process proc;
    private int unten = -1;
    private boolean langGemeldet;
    private final Runnable lang = this::langErreicht;

    CecTasten(Context c, Handler h, Tasten melder) {
        this.ctx = c;
        this.h = h;
        this.melder = melder;
        beob = new ContentObserver(h) {
            @Override public void onChange(boolean selbst) { laden(); }
        };
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(EINST), false, beob);
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(Tasten.EINST_LERNEN), false, beob);
        c.getContentResolver().registerContentObserver(Settings.Global.getUriFor(WebMenue.EINST_URL), false, beob);
        laden();
    }

    void aus() {
        laeuft = false;
        try { ctx.getContentResolver().unregisterContentObserver(beob); } catch (Exception ignored) { }
        stoppen();
    }

    private void laden() {
        Set<Integer> s = new HashSet<>();
        String t = Settings.Global.getString(ctx.getContentResolver(), EINST);
        if (t != null) {
            for (String teil : t.split("[,; ]+")) {
                try { if (!teil.isEmpty()) s.add(Integer.decode(teil.trim())); } catch (NumberFormatException ignored) { }
            }
        }
        tasten = s;
        lernen = "1".equals(Settings.Global.getString(ctx.getContentResolver(), Tasten.EINST_LERNEN));
        menue = !WebMenue.url(ctx).isEmpty();   // Menue eingerichtet: Fernseher-Pfeile/OK/Zurueck dorthin weiterreichen
        if (s.isEmpty() && !lernen && !menue) stoppen();
        else starten();
    }

    private synchronized void starten() {
        if (leser != null && leser.isAlive()) return;
        leser = new Thread(this::lesen, "cec-tasten");
        leser.start();
    }

    private synchronized void stoppen() {
        if (proc != null) proc.destroy();
        proc = null;
    }

    private void lesen() {
        while (laeuft && (!tasten.isEmpty() || lernen || menue)) {
            try {
                // -T 1: nur neue Zeilen; nur der CEC-Treiber
                Process p = Runtime.getRuntime().exec(new String[]{"logcat", "-T", "1", "-v", "brief", "hdmi_cec_hw:D", "*:S"});
                synchronized (this) { proc = p; }
                BufferedReader r = new BufferedReader(new InputStreamReader(p.getInputStream()));
                String z;
                while ((z = r.readLine()) != null) {
                    if (!z.contains("print_data_msg") || !z.contains("initiator = 0,")) continue;
                    Matcher m = GEDRUECKT.matcher(z);
                    if (m.find()) {
                        final int code = Integer.parseInt(m.group(1), 16);
                        h.post(() -> gedrueckt(code));
                    } else if (LOS.matcher(z).find()) {
                        h.post(this::losgelassen);
                    }
                }
                p.destroy();
            } catch (Exception e) {
                Log.w(TAG, "Protokoll nicht lesbar (READ_LOGS erteilt?): " + e);
            }
            if (!laeuft || (tasten.isEmpty() && !lernen && !menue)) break;
            try { Thread.sleep(5000); } catch (InterruptedException ignored) { }
        }
    }

    // Der Fernseher wiederholt "gedrueckt" beim Halten - nur das erste zaehlt
    private void gedrueckt(int code) {
        if (unten == code) return;
        // Web-Menue offen (1.17): Pfeile/OK/Zurueck der Fernseher-Fernbedienung dorthin (der Stick bekommt sie trotzdem)
        if (WebMenue.offen()) {
            final int k = code == 0x01 ? android.view.KeyEvent.KEYCODE_DPAD_UP : code == 0x02 ? android.view.KeyEvent.KEYCODE_DPAD_DOWN
                    : code == 0x03 ? android.view.KeyEvent.KEYCODE_DPAD_LEFT : code == 0x04 ? android.view.KeyEvent.KEYCODE_DPAD_RIGHT
                    : code == 0x00 ? android.view.KeyEvent.KEYCODE_DPAD_CENTER : code == 0x0D ? android.view.KeyEvent.KEYCODE_BACK : -1;
            if (k >= 0) WebMenue.taste(new android.view.KeyEvent(android.view.KeyEvent.ACTION_DOWN, k));
        }
        if (lernen) melder.meldenExtern(code, "lernen", "HDMI-CEC (Fernseher)", true);
        if (!tasten.contains(code)) { unten = code; langGemeldet = true; return; }
        unten = code;
        langGemeldet = false;
        h.removeCallbacks(lang);
        h.postDelayed(lang, LANG_MS);
    }

    private void losgelassen() {
        h.removeCallbacks(lang);
        if (unten >= 0 && !langGemeldet) melder.meldenExtern(unten, "kurz", "HDMI-CEC (Fernseher)", true);
        unten = -1;
    }

    private void langErreicht() {
        if (unten < 0 || langGemeldet) return;
        langGemeldet = true;
        melder.meldenExtern(unten, "lang", "HDMI-CEC (Fernseher)", true);
    }
}
