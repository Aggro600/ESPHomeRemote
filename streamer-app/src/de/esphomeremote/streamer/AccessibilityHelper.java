package de.esphomeremote.streamer;

import android.accessibilityservice.AccessibilityService;
import android.accessibilityservice.AccessibilityServiceInfo;
import android.database.ContentObserver;
import android.provider.Settings;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

import java.util.List;

// Vorderste App und Bildschirmtastatur. Android meldet Fensterwechsel; danach einmal die Fensterliste ansehen:
// ein Fenster vom Typ "Eingabemethode" = Tastatur offen, das aktive App-Fenster = vorderste App. Bei offener
// Tastatur die Art des Eingabefelds (Zahl/Text) fuer den passenden Ziffernblock der Fernbedienung.
public class AccessibilityHelper extends AccessibilityService {
    private final Handler h = new Handler(Looper.getMainLooper());
    private final Runnable pruefen = this::pruefen;

    // Cursor im Eingabefeld (2026-10-26): die Fernbedienung schickt per ADB
    //   am broadcast -a de.esphomeremote.streamer.CURSOR -p de.esphomeremote.streamer --ei d -1|1   (Anfang/Ende: --ei d -999|999)
    // Pfeiltasten waeren am Fernseher Navigation in dessen Bildschirmtastatur - hier wird die Auswahl direkt gesetzt.
    static final String CURSOR = "de.esphomeremote.streamer.CURSOR";
    private final BroadcastReceiver cursor = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { cursorBewegen(i.getIntExtra("d", 0)); }
    };
    // Kamera als Overlay (2026-11-01, Kamera.java): am broadcast -a de.esphomeremote.streamer.CAMERA ...
    private final BroadcastReceiver kamera = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { Camera.befehl(AccessibilityHelper.this, i); }
    };

    // Popup-Menue aus Home Assistant (1.16, Menue.java)
    private final BroadcastReceiver menue = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { Menue.befehl(AccessibilityHelper.this, i); }
    };

    @Override
    protected void onServiceConnected() {
        IntentFilter f = new IntentFilter(CURSOR);
        IntentFilter fk = new IntentFilter(Camera.AKTION);
        IntentFilter fm = new IntentFilter(Menue.AKTION);
        if (Build.VERSION.SDK_INT >= 33) {
            registerReceiver(cursor, f, Context.RECEIVER_EXPORTED);
            registerReceiver(kamera, fk, Context.RECEIVER_EXPORTED);
            registerReceiver(menue, fm, Context.RECEIVER_EXPORTED);
        } else {
            registerReceiver(cursor, f);
            registerReceiver(kamera, fk);
            registerReceiver(menue, fm);
        }
        modusBeob = new ContentObserver(h) {
            @Override public void onChange(boolean selbst) { modusAnwenden(); }
        };
        getContentResolver().registerContentObserver(Settings.Global.getUriFor("esphomeremote_modus"), false, modusBeob);
        modusAnwenden();
        if (!State.nurKamera(this)) pruefen();
        tasten = new Tasten(this, h);   // Tasten umbelegen (1.16)
        cecTasten = new CecTasten(this, h, tasten);   // Tasten der Fernseher-Fernbedienung (per HDMI-CEC)
    }

    private Tasten tasten;
    private CecTasten cecTasten;

    // Tasten fuer Home Assistant (2026-11-05, Tasten.java): true = verbraucht, Android bekommt sie nicht
    @Override
    protected boolean onKeyEvent(KeyEvent e) {
        try {
            return tasten != null && tasten.ereignis(e);
        } catch (Exception ex) {
            return false;
        }
    }

    // Nur Kamera: Android stellt gar keine Fenster-Ereignisse mehr zu (eventTypes 0) - das Overlay braucht nur den
    // verbundenen Dienst. Zurueck auf voll: die Ereignisse aus der XML-Konfiguration wieder abonnieren.
    private ContentObserver modusBeob;
    private int ereignisseVoll = -1;

    private void modusAnwenden() {
        try {
            AccessibilityServiceInfo info = getServiceInfo();
            if (info == null) return;
            if (ereignisseVoll == -1) ereignisseVoll = info.eventTypes;
            final int soll = State.nurKamera(this) ? 0 : ereignisseVoll;
            if (info.eventTypes != soll) {
                info.eventTypes = soll;
                setServiceInfo(info);
            }
            if (soll != 0) pruefen();
        } catch (Exception ignored) {
        }
    }

    @Override
    public void onDestroy() {
        try { if (modusBeob != null) getContentResolver().unregisterContentObserver(modusBeob); } catch (Exception ignored) { }
        try { unregisterReceiver(cursor); } catch (Exception ignored) { }
        try { unregisterReceiver(kamera); } catch (Exception ignored) { }
        try { unregisterReceiver(menue); } catch (Exception ignored) { }
        Menue.zu();
        WebMenue.zu();
        Camera.aus();
        if (tasten != null) tasten.aus();
        if (cecTasten != null) cecTasten.aus();
        super.onDestroy();
    }

    private void cursorBewegen(int d) {
        try {
            AccessibilityNodeInfo f = findFocus(AccessibilityNodeInfo.FOCUS_INPUT);
            if (f == null) return;
            CharSequence t = f.getText();
            int len = (t == null || f.isShowingHintText()) ? 0 : t.length();
            int pos = f.getTextSelectionEnd();
            if (pos < 0 || pos > len) pos = len;
            int neu = Math.max(0, Math.min(len, pos + d));
            Bundle a = new Bundle();
            a.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_START_INT, neu);
            a.putInt(AccessibilityNodeInfo.ACTION_ARGUMENT_SELECTION_END_INT, neu);
            f.performAction(AccessibilityNodeInfo.ACTION_SET_SELECTION, a);
        } catch (Exception ignored) {
        }
    }

    @Override
    public void onAccessibilityEvent(AccessibilityEvent e) {
        if (ereignisseVoll != -1 && getServiceInfo() != null && getServiceInfo().eventTypes == 0) return;   // Nur Kamera
        // Fensterwechsel kommen oft in Bursts - einmal danach pruefen
        h.removeCallbacks(pruefen);
        h.postDelayed(pruefen, 60);   // 150 ms bis 2026-10-26
    }

    @Override
    public void onInterrupt() {
    }

    private void pruefen() {
        boolean tastatur = false;
        String top = null;
        int art = 1;
        try {
            List<AccessibilityWindowInfo> fenster = getWindows();
            for (AccessibilityWindowInfo w : fenster) {
                final int t = w.getType();
                if (t == AccessibilityWindowInfo.TYPE_INPUT_METHOD) {
                    tastatur = true;
                } else if (t == AccessibilityWindowInfo.TYPE_APPLICATION && top == null && (w.isActive() || w.isFocused())) {
                    AccessibilityNodeInfo r = w.getRoot();
                    if (r != null && r.getPackageName() != null) top = r.getPackageName().toString();
                }
            }
            if (tastatur) {
                AccessibilityNodeInfo f = findFocus(AccessibilityNodeInfo.FOCUS_INPUT);
                if (f != null && f.getInputType() != 0) art = f.getInputType();
            }
        } catch (Exception ignored) {
            return;
        }
        State.get(this).setBedienung(top, tastatur, art);
    }
}
