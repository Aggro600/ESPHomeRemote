package de.esphomeremote.streamer;

import android.accessibilityservice.AccessibilityService;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityWindowInfo;

import java.util.List;

// Vorderste App und Bildschirmtastatur. Android meldet Fensterwechsel; danach einmal die Fensterliste ansehen:
// ein Fenster vom Typ "Eingabemethode" = Tastatur offen, das aktive App-Fenster = vorderste App. Bei offener
// Tastatur die Art des Eingabefelds (Zahl/Text) fuer den passenden Ziffernblock der Fernbedienung.
public class Bedienung extends AccessibilityService {
    private final Handler h = new Handler(Looper.getMainLooper());
    private final Runnable pruefen = this::pruefen;

    // Cursor im Eingabefeld (2026-10-26): die Fernbedienung schickt per ADB
    //   am broadcast -a de.esphomeremote.streamer.CURSOR -p de.esphomeremote.streamer --ei d -1|1   (Anfang/Ende: --ei d -999|999)
    // Pfeiltasten waeren am Fernseher Navigation in dessen Bildschirmtastatur - hier wird die Auswahl direkt gesetzt.
    static final String CURSOR = "de.esphomeremote.streamer.CURSOR";
    private final BroadcastReceiver cursor = new BroadcastReceiver() {
        @Override public void onReceive(Context c, Intent i) { cursorBewegen(i.getIntExtra("d", 0)); }
    };

    @Override
    protected void onServiceConnected() {
        IntentFilter f = new IntentFilter(CURSOR);
        if (Build.VERSION.SDK_INT >= 33) registerReceiver(cursor, f, Context.RECEIVER_EXPORTED);
        else registerReceiver(cursor, f);
        pruefen();
    }

    @Override
    public void onDestroy() {
        try { unregisterReceiver(cursor); } catch (Exception ignored) { }
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
        Zustand.get(this).setBedienung(top, tastatur, art);
    }
}
