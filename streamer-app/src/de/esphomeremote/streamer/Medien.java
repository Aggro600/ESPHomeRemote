package de.esphomeremote.streamer;

import android.content.ComponentName;
import android.graphics.Bitmap;
import android.media.MediaMetadata;
import android.media.session.MediaController;
import android.media.session.MediaSessionManager;
import android.media.session.PlaybackState;
import android.os.Handler;
import android.os.Looper;
import android.service.notification.NotificationListenerService;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

// Medien-Sitzungen aller Apps: Zustand, Position, Titel, Cover-URL, Dauer. Rein ereignisgesteuert - Android ruft
// uns bei neuen/geschlossenen Sitzungen und bei jeder Zustands-/Metadaten-Aenderung auf. Die Erlaubnis dafuer gibt
// es nur als Benachrichtigungs-Zuhoerer (Benachrichtigungen selbst werden nicht gelesen).
public class Medien extends NotificationListenerService {
    // Wie SmartTubeMeta.java: fuer diese Apps die genauen Felder (TITEL/ARTIST/COVER), Reihenfolge = Vorrang
    private static final String[] ZIEL = {"org.smarttube.stable", "com.netflix.ninja"};

    private MediaSessionManager msm;
    private ComponentName cn;
    private final Handler h = new Handler(Looper.getMainLooper());
    private final List<MediaController> liste = new ArrayList<>();
    private final List<MediaController.Callback> rueckrufe = new ArrayList<>();

    private final MediaSessionManager.OnActiveSessionsChangedListener wechsel = this::neu;
    private final Runnable melden = this::melden;

    @Override
    public void onListenerConnected() {
        Bilder.init(this);
        try {
            msm = getSystemService(MediaSessionManager.class);
            cn = new ComponentName(this, Medien.class);
            msm.addOnActiveSessionsChangedListener(wechsel, cn, h);
            neu(msm.getActiveSessions(cn));
            Log.i(Zustand.TAG, "Medien: verbunden");
        } catch (Exception e) {
            Log.w(Zustand.TAG, "Medien: " + e);
        }
    }

    @Override
    public void onListenerDisconnected() {
        if (msm != null) msm.removeOnActiveSessionsChangedListener(wechsel);
        abmelden();
    }

    private void abmelden() {
        for (int i = 0; i < liste.size(); i++) liste.get(i).unregisterCallback(rueckrufe.get(i));
        liste.clear();
        rueckrufe.clear();
    }

    private void neu(List<MediaController> sitzungen) {
        abmelden();
        if (sitzungen != null) {
            for (MediaController c : sitzungen) {
                MediaController.Callback cb = new MediaController.Callback() {
                    @Override public void onPlaybackStateChanged(PlaybackState s) { spaeter(); }
                    @Override public void onMetadataChanged(MediaMetadata m) { spaeter(); }
                    @Override public void onSessionDestroyed() { spaeter(); }
                };
                c.registerCallback(cb, h);
                liste.add(c);
                rueckrufe.add(cb);
            }
        }
        spaeter();
    }

    // Manche Apps melden mehrmals hintereinander (Zustand, dann Metadaten) - kurz sammeln
    private void spaeter() {
        h.removeCallbacks(melden);
        h.postDelayed(melden, 100);
    }

    private static boolean ziel(String p) {
        for (String z : ZIEL) if (z.equals(p)) return true;
        return false;
    }

    private void melden() {
        StringBuilder sitz = new StringBuilder(), sig = new StringBuilder(), dur = new StringBuilder(), art = new StringBuilder();
        MediaController bester = null;
        int besterRang = 99;
        for (MediaController c : liste) {
            final String pkg = c.getPackageName();
            final PlaybackState ps = c.getPlaybackState();
            final MediaMetadata md = c.getMetadata();
            sitz.append("package=").append(pkg).append('\n');
            sig.append(pkg).append(';');
            if (ps != null) {
                // PlaybackState.toString() = Format von dumpsys: "PlaybackState {state=PLAYING(3), position=..., ...}"
                sitz.append("state=").append(ps.toString()).append('\n');
                // Ohne laufende Position: Startpunkt der Wiedergabe (bleibt beim normalen Abspielen gleich),
                // auf 3 s gerundet - aendert sich nur bei Spulen/Spruengen
                final long ab = ps.getState() == PlaybackState.STATE_PLAYING
                        ? ps.getPosition() - (long) (ps.getPlaybackSpeed() * ps.getLastPositionUpdateTime())
                        : ps.getPosition();
                sig.append(ps.getState()).append(';').append(ps.getPlaybackSpeed()).append(';').append(Math.round(ab / 3000.0)).append(';');
            } else {
                sitz.append("state=null\n");
            }
            if (md != null) {
                sitz.append("  metadata: size=").append(md.size()).append(", description=").append(md.getDescription()).append('\n');
                sig.append(md.getDescription()).append(';');
                final long d = md.getLong(MediaMetadata.METADATA_KEY_DURATION);
                if (d > 0) dur.append("DUR:").append(pkg).append('=').append(d).append('\n');
                // Cover jeder App (2026-10-11): URL aus den Metadaten, sonst das mitgelieferte Bild ueber Bilder
                final String a = cover(pkg, md);
                if (a != null) art.append("ART:").append(pkg).append('=').append(a).append('\n');
            } else {
                sitz.append("  metadata: null\n");
            }
            for (int r = 0; r < ZIEL.length; r++)
                if (ZIEL[r].equals(pkg) && md != null && r < besterRang) { bester = c; besterRang = r; }
        }
        StringBuilder meta = new StringBuilder();
        if (bester != null) {
            final MediaMetadata md = bester.getMetadata();
            final PlaybackState ps = bester.getPlaybackState();
            String cover = md.getString(MediaMetadata.METADATA_KEY_ART_URI);
            if (cover == null) cover = md.getString(MediaMetadata.METADATA_KEY_ALBUM_ART_URI);
            meta.append("TITEL=").append(leer(md.getString(MediaMetadata.METADATA_KEY_TITLE))).append('\n');
            meta.append("ARTIST=").append(leer(md.getString(MediaMetadata.METADATA_KEY_ARTIST))).append('\n');
            meta.append("COVER=").append(leer(cover)).append('\n');
            meta.append("STATE=").append(ps == null ? -1 : ps.getState()).append('\n');
        } else {
            meta.append("TITEL=\nARTIST=\nCOVER=\nSTATE=\n");
        }
        meta.append(dur);
        meta.append(art);
        Zustand.get(this).setMedien(meta.toString(), sitz.toString(), sig.toString());
    }

    private static String cover(String pkg, MediaMetadata md) {
        for (String k : new String[]{MediaMetadata.METADATA_KEY_ART_URI, MediaMetadata.METADATA_KEY_ALBUM_ART_URI, MediaMetadata.METADATA_KEY_DISPLAY_ICON_URI}) {
            final String u = md.getString(k);
            if (u != null && (u.startsWith("http://") || u.startsWith("https://"))) {
                Bilder.setze(pkg, null);
                return u.replace('\n', ' ');
            }
        }
        Bitmap b = null;
        for (String k : new String[]{MediaMetadata.METADATA_KEY_ART, MediaMetadata.METADATA_KEY_ALBUM_ART, MediaMetadata.METADATA_KEY_DISPLAY_ICON}) {
            try { b = md.getBitmap(k); } catch (Exception ignored) { b = null; }
            if (b != null) break;
        }
        return Bilder.setze(pkg, b);
    }

    private static String leer(String s) {
        return s == null ? "" : s.replace('\n', ' ');
    }
}
