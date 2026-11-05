package de.esphomeremote.streamer;

import android.accessibilityservice.AccessibilityService;
import android.content.Context;
import android.graphics.Color;
import android.graphics.PixelFormat;
import android.graphics.drawable.GradientDrawable;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.KeyEvent;
import android.view.WindowManager;
import android.webkit.JavascriptInterface;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.widget.FrameLayout;

// Popup-Menue aus dem Konfigurator (2026-11-05, App 1.17): dasselbe Menue wie auf der Remote (Seiten mit Kacheln),
// von Home Assistant als Seite geliefert (custom_components/esphomeremote/streamer_menue.py). Die Menue-Taste oeffnet
// es; solange es offen ist, gehen Pfeile/OK/Zurueck an die Seite (taste("up"...)). Die Seite schliesst es mit App.zu().
//   settings put global esphomeremote_menue_url <Adresse>        (leer = aus, dann das einfache Menue aus Menue.java)
//   settings put global esphomeremote_menue_groesse 70           (Prozent des Bildschirms, 100 = ganzer Bildschirm)
//   settings put global esphomeremote_menue_position mitte|links|rechts|oben|unten|oben_links|oben_rechts|unten_links|unten_rechts
// Rundruf de.esphomeremote.streamer.MENU --es web auf [--ei groesse N] [--es position P] [--es seite ID] (1.19): nur dieses Mal
final class WebMenue {
    static final String EINST_URL = "esphomeremote_menue_url";
    private static FrameLayout rahmen;
    private static WebView web;
    private static final Handler h = new Handler(Looper.getMainLooper());

    static boolean offen() { return rahmen != null; }

    static String url(Context c) {
        String u = Settings.Global.getString(c.getContentResolver(), EINST_URL);
        return u == null || u.trim().isEmpty() || "null".equals(u.trim()) ? "" : u.trim();
    }

    static void zeigen(AccessibilityService s) { zeigen(s, 0, null, null); }

    static void zeigen(AccessibilityService s, int groesseNur, String posNur, String seite) {
        String u0 = url(s);
        if (u0.isEmpty()) return;
        if (seite != null && seite.matches("[A-Za-z0-9_\\-]{1,40}")) u0 += (u0.contains("?") ? "&" : "?") + "seite=" + seite;
        final String u = u0;
        try {
            zu();
            int groesse = 70;
            try { groesse = Integer.parseInt(Settings.Global.getString(s.getContentResolver(), "esphomeremote_menue_groesse")); }
            catch (Exception ignored) { }
            if (groesseNur > 0) groesse = groesseNur;
            groesse = Math.max(30, Math.min(100, groesse));
            String pos = posNur != null && !posNur.isEmpty() ? posNur : Settings.Global.getString(s.getContentResolver(), "esphomeremote_menue_position");
            if (pos == null) pos = "mitte";
            DisplayMetrics dm = s.getResources().getDisplayMetrics();
            final float dp = dm.density;
            final boolean voll = groesse >= 100;
            final int w = voll ? WindowManager.LayoutParams.MATCH_PARENT : dm.widthPixels * groesse / 100;
            final int hh = voll ? WindowManager.LayoutParams.MATCH_PARENT : dm.heightPixels * groesse / 100;
            WindowManager.LayoutParams p = new WindowManager.LayoutParams(w, hh,
                    WindowManager.LayoutParams.TYPE_ACCESSIBILITY_OVERLAY,
                    WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE | WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE
                            | WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN | WindowManager.LayoutParams.FLAG_HARDWARE_ACCELERATED,
                    PixelFormat.TRANSLUCENT);
            int gx = pos.contains("links") ? Gravity.START : pos.contains("rechts") ? Gravity.END : Gravity.CENTER_HORIZONTAL;
            int gy = pos.contains("oben") ? Gravity.TOP : pos.contains("unten") ? Gravity.BOTTOM : Gravity.CENTER_VERTICAL;
            p.gravity = gx | gy;
            if (!voll && gx != Gravity.CENTER_HORIZONTAL) p.x = Math.round(24 * dp);
            if (!voll && gy != Gravity.CENTER_VERTICAL) p.y = Math.round(24 * dp);
            rahmen = new FrameLayout(s);
            GradientDrawable bg = new GradientDrawable();
            bg.setColor(0xF0121214);
            bg.setCornerRadius(voll ? 0 : 18 * dp);
            if (!voll) bg.setStroke(Math.max(1, Math.round(1.2f * dp)), 0x80FFFFFF);
            rahmen.setBackground(bg);
            rahmen.setClipToOutline(true);
            web = new WebView(s);
            web.setBackgroundColor(Color.TRANSPARENT);
            WebSettings ws = web.getSettings();
            ws.setJavaScriptEnabled(true);
            ws.setDomStorageEnabled(true);
            ws.setMixedContentMode(WebSettings.MIXED_CONTENT_ALWAYS_ALLOW);
            ws.setUseWideViewPort(false);
            ws.setLoadWithOverviewMode(false);
            web.setWebChromeClient(new WebChromeClient());
            web.setFocusable(false);
            web.addJavascriptInterface(new Object() {
                @JavascriptInterface public void zu() { h.post(WebMenue::zu); }
            }, "App");
            rahmen.addView(web, new FrameLayout.LayoutParams(FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
            ((WindowManager) s.getSystemService(Context.WINDOW_SERVICE)).addView(rahmen, p);
            web.loadUrl(u);
            Log.i(State.TAG, "Menue offen (" + groesse + " %, " + pos + ")");
        } catch (Exception e) {
            Log.w(State.TAG, "Menue nicht anzeigbar: " + e);
            rahmen = null;
            web = null;
        }
    }

    // Taste an die Seite; true = verbraucht. Zurueck auf der ersten Seite schliesst (die Seite ruft App.zu()).
    static boolean taste(KeyEvent e) {
        if (web == null) return false;
        final String t;
        switch (e.getKeyCode()) {
            case KeyEvent.KEYCODE_DPAD_UP: t = "up"; break;
            case KeyEvent.KEYCODE_DPAD_DOWN: t = "down"; break;
            case KeyEvent.KEYCODE_DPAD_LEFT: t = "left"; break;
            case KeyEvent.KEYCODE_DPAD_RIGHT: t = "right"; break;
            case KeyEvent.KEYCODE_DPAD_CENTER:
            case KeyEvent.KEYCODE_ENTER:
            case KeyEvent.KEYCODE_NUMPAD_ENTER: t = "ok"; break;
            case KeyEvent.KEYCODE_BACK:
            case KeyEvent.KEYCODE_ESCAPE: t = "back"; break;
            default: return false;
        }
        if (e.getAction() == KeyEvent.ACTION_DOWN && (e.getRepeatCount() == 0 || !"ok".equals(t))) {
            final WebView w = web;
            h.post(() -> { if (w != null) w.evaluateJavascript("window.taste && taste('" + t + "')", null); });
        }
        return true;
    }

    static void zu() {
        final FrameLayout r = rahmen;
        final WebView w = web;
        rahmen = null;
        web = null;
        if (r == null) return;
        try { ((WindowManager) r.getContext().getSystemService(Context.WINDOW_SERVICE)).removeView(r); } catch (Exception ignored) { }
        try { if (w != null) w.destroy(); } catch (Exception ignored) { }
        Log.i(State.TAG, "Menue zu");
    }
}
