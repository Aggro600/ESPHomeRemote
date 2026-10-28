package de.esphomeremote.streamer;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.StateListDrawable;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

// Start-Seite (2026-10-26): zeigt, dass die App da ist und ob sie arbeitet - Begruessung, Status, Version,
// Deinstallieren. Alles mit dem Steuerkreuz bedienbar. Eingerichtet wird sie vom Konfigurator in Home Assistant.
public class Start extends Activity {
    private static final int HG = 0xFF0E1626, KARTE = 0xFF16202E, TEXT = 0xFFF1F3F5, GRAU = 0xFF8B94A3,
            GRUEN = 0xFF22C55E, ROT = 0xFFF87171, BLAU = 0xFF3B82F6;
    private final Handler h = new Handler(Looper.getMainLooper());
    private TextView[] zeilen;
    private TextView meldung;
    private Button hilfe;

    private final Runnable auffrischen = new Runnable() {
        @Override public void run() { zeigen(); h.postDelayed(this, 2000); }
    };

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setBackgroundColor(HG);
        root.setPadding(dp(48), dp(32), dp(48), dp(32));

        LinearLayout kopf = new LinearLayout(this);
        kopf.setGravity(Gravity.CENTER_VERTICAL);
        ImageView logo = new ImageView(this);
        logo.setImageResource(getApplicationInfo().icon);
        kopf.addView(logo, new LinearLayout.LayoutParams(dp(64), dp(64)));
        TextView titel = text("ESPHomeRemote Streamer", 30, TEXT);
        titel.setTypeface(Typeface.DEFAULT_BOLD);
        titel.setPadding(dp(16), 0, 0, 0);
        kopf.addView(titel);
        root.addView(kopf);

        TextView hallo = text("Hallo! Diese App sagt Home Assistant, was hier gerade läuft – damit deine Fernbedienungen "
                + "Titel, Cover und die Tastatur zeigen können. Du musst hier nichts tun.", 18, GRAU);
        hallo.setGravity(Gravity.CENTER);
        hallo.setPadding(0, dp(14), 0, dp(18));
        hallo.setMaxWidth(dp(720));
        root.addView(hallo);
        hallo.setAlpha(0f);
        hallo.animate().alpha(1f).setDuration(900).start();

        LinearLayout karte = new LinearLayout(this);
        karte.setOrientation(LinearLayout.VERTICAL);
        GradientDrawable kg = new GradientDrawable();
        kg.setColor(KARTE);
        kg.setCornerRadius(dp(16));
        karte.setBackground(kg);
        karte.setPadding(dp(28), dp(18), dp(28), dp(18));
        zeilen = new TextView[4];
        for (int i = 0; i < zeilen.length; i++) {
            zeilen[i] = text("", 18, TEXT);
            zeilen[i].setPadding(0, dp(4), 0, dp(4));
            karte.addView(zeilen[i]);
        }
        meldung = text("", 15, GRAU);
        meldung.setPadding(0, dp(8), 0, 0);
        karte.addView(meldung);
        root.addView(karte, new LinearLayout.LayoutParams(dp(640), LinearLayout.LayoutParams.WRAP_CONTENT));

        LinearLayout knoepfe = new LinearLayout(this);
        knoepfe.setGravity(Gravity.CENTER);
        knoepfe.setPadding(0, dp(22), 0, 0);
        hilfe = knopf("Bedienungshilfe öffnen", v -> startActivity(new Intent(Settings.ACTION_ACCESSIBILITY_SETTINGS)));
        Button melden = knopf("Jetzt melden", v -> { Zustand.get(this).erneutSenden(); meldung.setText("Wird gesendet …"); });
        Button weg = knopf("Deinstallieren", v -> startActivity(new Intent(Intent.ACTION_DELETE, Uri.parse("package:" + getPackageName()))));
        knoepfe.addView(hilfe);
        knoepfe.addView(melden);
        knoepfe.addView(weg);
        root.addView(knoepfe);

        String ver = "";
        try { ver = getPackageManager().getPackageInfo(getPackageName(), 0).versionName; } catch (Exception ignored) { }
        TextView fuss = text("Version " + ver + "  ·  Einrichten, Aktualisieren und Entfernen geht auch im Konfigurator (Home Assistant → Fernbedienung → Wiedergabe)", 13, GRAU);
        fuss.setGravity(Gravity.CENTER);
        fuss.setPadding(0, dp(22), 0, 0);
        root.addView(fuss);
        setContentView(root);
        melden.requestFocus();
    }

    @Override protected void onResume() { super.onResume(); h.post(auffrischen); }
    @Override protected void onPause() { super.onPause(); h.removeCallbacks(auffrischen); }

    private void zeigen() {
        ContentResolver cr = getContentResolver();
        final String pkg = getPackageName();
        final String acc = Settings.Secure.getString(cr, "enabled_accessibility_services");
        final String nl = Settings.Secure.getString(cr, "enabled_notification_listeners");
        final String url = Settings.Global.getString(cr, "esphomeremote_webhook");
        final boolean a = acc != null && acc.contains(pkg + "/"), n = nl != null && nl.contains(pkg + "/"),
                u = url != null && !url.isEmpty();
        zeile(0, a, "App und Bildschirmtastatur erkennen", "Bedienungshilfe ist aus");
        zeile(1, n, "Titel und Cover lesen", "Benachrichtigungszugriff fehlt");
        zeile(2, u, "Mit Home Assistant verbunden", "Noch nicht mit Home Assistant verbunden");
        final long ok = Zustand.letzteOkMs;
        final String f = Zustand.letzterFehler;
        final boolean laeuft = f == null && ok > 0;
        if (f != null) zeile(3, false, "", f);
        else if (ok > 0) zeile(3, true, "Letzte Meldung vor " + dauer(System.currentTimeMillis() - ok), "");
        else zeile(3, u, "Wartet auf die erste Meldung", "Noch keine Meldung");
        meldung.setText(a && n && u && laeuft ? "Alles in Ordnung – die App arbeitet." :
                "Am einfachsten im Konfigurator auf „Einrichten“ tippen, dann wird alles automatisch eingestellt.");
        meldung.setTextColor(a && n && u && laeuft ? GRUEN : GRAU);
        hilfe.setVisibility(a ? View.GONE : View.VISIBLE);
    }

    private void zeile(int i, boolean gut, String jaText, String neinText) {
        zeilen[i].setText((gut ? "✓  " : "✗  ") + (gut ? jaText : neinText));
        zeilen[i].setTextColor(gut ? TEXT : ROT);
    }

    private static String dauer(long ms) {
        long s = ms / 1000;
        if (s < 60) return s + " s";
        if (s < 3600) return (s / 60) + " min";
        return (s / 3600) + " h";
    }

    private TextView text(String t, int sp, int farbe) {
        TextView v = new TextView(this);
        v.setText(t);
        v.setTextSize(sp);
        v.setTextColor(farbe);
        return v;
    }

    private Button knopf(String t, View.OnClickListener l) {
        Button k = new Button(this);
        k.setText(t);
        k.setAllCaps(false);
        k.setTextSize(16);
        k.setTextColor(TEXT);
        k.setFocusable(true);
        GradientDrawable normal = new GradientDrawable(), fokus = new GradientDrawable();
        normal.setColor(0xFF232A35);
        normal.setCornerRadius(dp(12));
        fokus.setColor(BLAU);
        fokus.setCornerRadius(dp(12));
        StateListDrawable sl = new StateListDrawable();
        sl.addState(new int[]{android.R.attr.state_focused}, fokus);
        sl.addState(new int[]{android.R.attr.state_pressed}, fokus);
        sl.addState(new int[]{}, normal);
        k.setBackground(sl);
        k.setPadding(dp(22), dp(10), dp(22), dp(10));
        k.setOnClickListener(l);
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
        lp.setMargins(dp(8), 0, dp(8), 0);
        k.setLayoutParams(lp);
        return k;
    }

    private int dp(int v) { return Math.round(v * getResources().getDisplayMetrics().density); }
}
