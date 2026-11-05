package de.esphomeremote.streamer;

import android.accessibilityservice.AccessibilityService;
import android.content.Context;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

// Popup-Menue auf dem Fernseher (2026-11-05, App 1.16). Bedienung mit der Fernbedienung: hoch/runter waehlen, OK
// bestaetigt, Zurueck schliesst; nach 10 s ohne Taste geht es von selbst zu. Solange es offen ist, gehoeren diese
// Tasten dem Menue (AccessibilityHelper.onKeyEvent). Die Wahl geht an Home Assistant: {"menue": {id, wahl, text}} ->
// Ereignis esphomeremote_streamer_menue - was dann passiert, steht in HA.
//   Oeffnen per Taste:  settings put global esphomeremote_menue_taste 235
//                       settings put global esphomeremote_menue "Kamera anzeigen|Soundbar ein/aus|Haustuer oeffnen"
//   Oeffnen aus HA:     am broadcast -a de.esphomeremote.streamer.MENU -p de.esphomeremote.streamer
//                         --es id tuer --es titel "Haustuer wirklich oeffnen?" --es punkte "Nein|Ja"
final class Menue {
    private static final String TAG = "ESPHomeRemoteMenue";
    static final String AKTION = "de.esphomeremote.streamer.MENU";
    static final String EINST_TASTE = "esphomeremote_menue_taste";
    static final String EINST_PUNKTE = "esphomeremote_menue";
    private static final long ZU_NACH_MS = 10000;

    private static LinearLayout panel;
    private static final List<TextView> zeilen = new ArrayList<>();
    private static final List<String> texte = new ArrayList<>();
    private static int wahl;
    private static String id = "";
    private static Tasten melder;
    private static final Handler h = new Handler(Looper.getMainLooper());
    private static final Runnable zuR = Menue::zu;

    static void setMelder(Tasten t) { melder = t; }

    static boolean offen() { return panel != null; }

    static void befehl(AccessibilityService s, Intent i) {
        // Menue aus dem Konfigurator von aussen (1.18, Dienst esphomeremote.streamer_menue): --es web auf|zu|umschalten
        String web = i.getStringExtra("web");
        if (web != null) {
            if ("zu".equals(web) || ("umschalten".equals(web) && WebMenue.offen())) WebMenue.zu();
            else if (!WebMenue.offen() || i.hasExtra("seite")) WebMenue.zeigen(s, i.getIntExtra("groesse", 0), i.getStringExtra("position"), i.getStringExtra("seite"));
            return;
        }
        String p = i.getStringExtra("punkte");
        if (p == null || p.isEmpty()) { zu(); return; }
        String t = i.getStringExtra("titel");
        String mid = i.getStringExtra("id");
        zeigen(s, t == null ? "" : t, p.split("\\|"), mid == null ? "" : mid, i.getIntExtra("start", 0));
    }

    static void zeigen(AccessibilityService s, String titel, String[] punkte, String menueId) {
        zeigen(s, titel, punkte, menueId, 0);
    }

    // start = vorausgewaehlte Zeile (--ei start N, z. B. die laufende Kamera)
    static void zeigen(AccessibilityService s, String titel, String[] punkte, String menueId, int start) {
        try {
            zu();
            id = menueId;
            wahl = 0;
            final float dp = s.getResources().getDisplayMetrics().density;
            panel = new LinearLayout(s);
            panel.setOrientation(LinearLayout.VERTICAL);
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(0xE61C1C1E);
            bg.setCornerRadius(18 * dp);
            bg.setStroke(Math.max(1, Math.round(1.2f * dp)), 0x80FFFFFF);
            panel.setBackground(bg);
            final int pad = Math.round(14 * dp);
            panel.setPadding(pad, pad, pad, pad);
            if (!titel.isEmpty()) {
                TextView tt = new TextView(s);
                tt.setText(titel);
                tt.setTextColor(0xB3FFFFFF);
                tt.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
                tt.setPadding(Math.round(10 * dp), 0, Math.round(10 * dp), Math.round(8 * dp));
                panel.addView(tt);
            }
            zeilen.clear();
            texte.clear();
            for (String p : punkte) {
                if (p.trim().isEmpty()) continue;
                TextView z = new TextView(s);
                z.setText(p.trim());
                z.setTextColor(Color.WHITE);
                z.setTextSize(TypedValue.COMPLEX_UNIT_SP, 20);
                z.setTypeface(Typeface.DEFAULT_BOLD);
                z.setPadding(Math.round(16 * dp), Math.round(10 * dp), Math.round(16 * dp), Math.round(10 * dp));
                LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT,
                        LinearLayout.LayoutParams.WRAP_CONTENT);
                lp.topMargin = Math.round(4 * dp);
                panel.addView(z, lp);
                zeilen.add(z);
                texte.add(p.trim());
            }
            wahl = Math.max(0, Math.min(zeilen.size() - 1, start));
            markieren(dp);
            WindowManager.LayoutParams lp = new WindowManager.LayoutParams(Math.round(340 * dp),
                    WindowManager.LayoutParams.WRAP_CONTENT, WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN,
                    android.graphics.PixelFormat.TRANSLUCENT);
            lp.gravity = Gravity.CENTER;
            ((WindowManager) s.getSystemService(Context.WINDOW_SERVICE)).addView(panel, lp);
            h.removeCallbacks(zuR);
            h.postDelayed(zuR, ZU_NACH_MS);
        } catch (Exception e) {
            Log.w(TAG, "Menue nicht anzeigbar: " + e);
            panel = null;
        }
    }

    private static void markieren(float dp) {
        for (int i = 0; i < zeilen.size(); i++) {
            TextView z = zeilen.get(i);
            if (i == wahl) {
                GradientDrawable m = new GradientDrawable();
                m.setColor(0xFF03A9F4);
                m.setCornerRadius(12 * dp);
                z.setBackground(m);
            } else {
                z.setBackground(null);
            }
        }
    }

    // true = Taste gehoert dem Menue
    static boolean taste(KeyEvent e) {
        if (panel == null) return false;
        final int c = e.getKeyCode();
        final boolean nav = c == KeyEvent.KEYCODE_DPAD_UP || c == KeyEvent.KEYCODE_DPAD_DOWN
                || c == KeyEvent.KEYCODE_DPAD_CENTER || c == KeyEvent.KEYCODE_ENTER || c == KeyEvent.KEYCODE_BACK
                || c == KeyEvent.KEYCODE_DPAD_LEFT || c == KeyEvent.KEYCODE_DPAD_RIGHT;
        if (!nav) return false;
        if (e.getAction() != KeyEvent.ACTION_DOWN) return true;
        h.removeCallbacks(zuR);
        h.postDelayed(zuR, ZU_NACH_MS);
        final float dp = panel.getResources().getDisplayMetrics().density;
        switch (c) {
            case KeyEvent.KEYCODE_DPAD_UP:   // oben angekommen: weiter nach ganz unten (rundum)
                if (!zeilen.isEmpty()) wahl = (wahl - 1 + zeilen.size()) % zeilen.size();
                markieren(dp);
                break;
            case KeyEvent.KEYCODE_DPAD_DOWN: // unten angekommen: weiter nach ganz oben
                if (!zeilen.isEmpty()) wahl = (wahl + 1) % zeilen.size();
                markieren(dp);
                break;
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_ENTER:
                if (e.getRepeatCount() > 0) break;
                waehlen();
                break;
            case KeyEvent.KEYCODE_BACK:
                zu();
                break;
            default:
                break;
        }
        return true;
    }

    private static void waehlen() {
        final int w = wahl;
        final String t = w < texte.size() ? texte.get(w) : "";
        final String mid = id;
        zu();
        if (melder == null) return;
        try {
            melder.meldenJson(new JSONObject().put("menue", new JSONObject().put("id", mid).put("wahl", w).put("text", t)));
        } catch (Exception ignored) {
        }
    }

    static void zu() {
        h.removeCallbacks(zuR);
        final View p = panel;
        panel = null;
        zeilen.clear();
        if (p == null) return;
        try {
            ((WindowManager) p.getContext().getSystemService(Context.WINDOW_SERVICE)).removeView(p);
        } catch (Exception ignored) {
        }
    }
}
