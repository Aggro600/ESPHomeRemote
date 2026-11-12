// Stile des Konfigurators (2026-11-12, aus configurator.js herausgeloest - reine CSS-Bloecke je Bereich, keine Platzhalter).
// Eingebunden ueber import in configurator.js; die Datei wird ohne Cache-Kopfzeilen ausgeliefert (jeder Aufruf prueft neu).

export const MENU_CSS = `
.irname { display:flex; align-items:center; gap:6px; font-size:14px; }
.irbtns { flex-wrap:wrap; } .irbtns > * { flex:0 0 auto; }
.weg { flex-wrap:wrap; font-size:13px; } .weg > * { flex:0 0 auto; } .weg select { flex:1 1 180px; }
.weg { display:flex; align-items:center; gap:6px 10px; margin-top:6px; padding:6px 8px; border-radius:10px; background:color-mix(in srgb, var(--acc) 8%, transparent); }
.weg.eigen { background:transparent; border:1px dashed var(--div); }
.weg .wl { color:var(--secondary-text-color); } .weg .vgchk { font-weight:600; }
.weg .vg { display:inline-flex; align-items:center; gap:4px; flex:1 1 auto; } .weg .vg ha-icon { --mdc-icon-size:16px; opacity:.7; }
.irgeraet { flex-wrap:wrap; } .irgeraet select { flex:1 1 160px; } .irgeraet > ha-icon { flex:0 0 auto; opacity:.7; }
.irname .btn { margin-left:auto; }
.weg .docks { display:flex; flex-wrap:wrap; gap:4px 10px; align-items:center; flex:1 1 100%; } .weg .docks small { opacity:.7; }
.btplatz { border:1px solid var(--div); border-radius:10px; padding:8px; margin:6px 0; display:flex; flex-direction:column; gap:6px; }
.btplatz .inl > b.nr { flex:0 0 26px; height:26px; border-radius:50%; background:var(--acc-s,rgba(59,130,246,.15)); color:var(--acc,#3b82f6); display:inline-flex; align-items:center; justify-content:center; }
.btplatz .chip { font-size:12px; padding:2px 8px; border-radius:999px; border:1px solid var(--div); white-space:nowrap; }
.btplatz .chip.ok { color:var(--success-color,#2e7d32); border-color:currentColor; font-weight:600; } .btplatz .chip.aus { opacity:.6; } .btplatz .chip.warn { color:var(--warning-color,#b26a00); border-color:currentColor; font-weight:600; }
.btplatz .btzeile { flex-wrap:wrap; gap:8px 12px; } .btplatz .btzeile .akt { flex:1 1 120px; color:var(--sec); }
.btplatz .aktionen { display:inline-flex; gap:4px; margin-left:auto; }
/* 2026-11-10 Handy: Auswahl "Eingabe"/"Am Dock" schrumpfte auf den Pfeil - Feld nimmt den Rest der Zeile */
.btplatz .btzeile .lbl { flex:1 1 160px; flex-wrap:nowrap; min-width:0; } .btplatz .btzeile .lbl select { flex:1 1 auto; min-width:0; width:auto; }
.msg.block { display:block; }
@media (max-width:700px) { .btplatz .btzeile .lbl { flex-basis:100%; } .btplatz .btzeile .lbl > select { flex:1 1 auto; } }
.lbl { display:inline-flex; gap:6px; align-items:center; font-size:13px; color:var(--sec); } .lbl select { width:auto; }
.btopt { display:flex; flex-wrap:wrap; gap:8px 16px; align-items:center; margin:4px 0; }
details.btprox { margin-top:12px; } details.btprox summary { cursor:pointer; font-weight:600; padding:6px 0; }
.btplatz .inl > b { flex:0 0 18px; } .btplatz .inl > small { flex:0 0 auto; white-space:nowrap; opacity:.8; } .btplatz small.ok { color:var(--success-color,#2e7d32); opacity:1; font-weight:600; }
.weg .chk { display:inline-flex; gap:4px; align-items:center; white-space:nowrap; cursor:pointer; }
.irrow { display:flex; align-items:center; gap:4px; } .irrow .ent { flex:1; } .ent:disabled { opacity:.45; cursor:default; }
.dlg.neu { max-width:640px; }
.neuschritte { display:flex; gap:6px; flex-wrap:wrap; margin:0 0 10px; } .neuschritte span { font-size:12px; padding:3px 8px; border-radius:12px; background:rgba(127,127,127,.15); }
.neuschritte span.on { background:var(--primary-color); color:#fff; } .neuschritte span.ok { color:var(--success-color, #22c55e); }
.neubody { overflow:auto; display:flex; flex-direction:column; gap:10px; }
.neulog pre { background:#0e1116; color:#cbd5e1; font-size:11px; padding:8px; border-radius:8px; max-height:220px; overflow:auto; white-space:pre-wrap; margin:6px 0 0; }
.neust .ok { color:#22c55e; } .neust .err { color:#ef4444; }
.spin { display:inline-block; width:12px; height:12px; border:2px solid var(--primary-color); border-right-color:transparent; border-radius:50%; animation:sp 0.8s linear infinite; vertical-align:-2px; }
@keyframes sp { to { transform:rotate(360deg); } }
.neuweg { border:1px solid var(--div); border-radius:10px; padding:8px 12px; display:flex; flex-direction:column; gap:8px; } .neuweg h3 { margin:0; font-size:15px; display:flex; gap:6px; align-items:center; }
.hmain { display:flex; gap:20px; flex-wrap:wrap; align-items:flex-start; padding:12px 0; }
.hprev { background:#0e1116; border-radius:18px; padding:10px; box-shadow:0 6px 20px rgba(0,0,0,.35); }
.hprev.inaktiv .hgrid { opacity:.45; }
.hprev .hint { color:#8b94a3; max-width:300px; font-size:12px; }
.hgrid { position:relative; width:240px; height:320px; zoom:1.25; touch-action:none; }
.ha { position:absolute; box-sizing:border-box; border-radius:10px; background:#1a1f27; border:2px solid #2a3240; color:#f1f3f5; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:2px; font-size:11px; cursor:grab; user-select:none; touch-action:none; overflow:hidden; text-align:center; }
.ha b { font-weight:500; } .ha i { font-style:normal; color:#8b94a3; font-size:10px; } .ha ha-icon { --mdc-icon-size:16px; color:#3b82f6; }
.ha.fix { cursor:pointer; flex-direction:row; border-style:dashed; } .ha.sel { border-color:#3b82f6; }
.ha.a-dock { background:#232a35; } .ha.a-medien { background:#141a22; }
.ha .rs { position:absolute; right:0; bottom:0; width:14px; height:14px; cursor:nwse-resize; background:linear-gradient(135deg, transparent 50%, #3b82f6 50%); border-bottom-right-radius:8px; }
.hlist { flex:1 1 260px; max-width:420px; } .hlist h3 { margin:0 0 8px; }
.hl { padding:8px 10px; border-radius:10px; border:1px solid var(--div); margin-bottom:6px; } .hl.sel { border-color:var(--primary-color); }
/* 2026-11-01: Felder blieben in der Karte nicht drin (Antippen oeffnet, Text, Schrift-Auswahl) */
.hl .hziel { display:flex; flex-wrap:wrap; gap:6px 12px; } .hl .inl2 { flex-wrap:wrap; min-width:0; max-width:100%; }
.hl .inl2 select { min-width:0; max-width:100%; flex:1 1 140px; } .hl .inl2 input { max-width:100%; }
.inl2 > .seg { flex-wrap:wrap; max-width:100%; } .inl2 { flex-wrap:wrap; max-width:100%; }
.hl small { display:block; color:var(--secondary-text-color); margin:2px 0 0 34px; }
.pgsep { display:flex; align-items:center; }
.seg { display:inline-flex; border:1px solid var(--div); border-radius:18px; overflow:hidden; }
.seg button { border:0; background:transparent; color:var(--primary-text-color); padding:6px 14px; cursor:pointer; font:inherit; font-size:14px; }
.seg button.on { background:var(--acc); color:#fff; }
.mbar { display:flex; flex-wrap:wrap; gap:10px 20px; align-items:center; padding:12px 16px 0; }
.inl2 { display:inline-flex; gap:8px; align-items:center; } .inl2 select { width:auto; }
.mmain { display:grid; grid-template-columns:220px 340px 1fr; gap:16px; padding:16px; align-items:start; }
/* Seitenbaum: eigene Scrollleiste und am Bildschirm kleben (2026-10-30) - beim Ziehen scrollte sonst das ganze Fenster */
@media (min-width: 871px) { .mmain .mpages { position:sticky; top:8px; max-height:calc(100vh - 24px); overflow-y:auto; overscroll-behavior:contain; } }
.mpages h3 { display:flex; align-items:center; font-size:15px; font-weight:500; margin:0 0 8px; }
.pg { display:flex; align-items:center; gap:6px; width:100%; text-align:left; border:1px solid var(--div); background:var(--card); color:inherit; border-radius:10px; padding:8px 10px; margin-bottom:6px; cursor:pointer; font:inherit; }
.pg.on { border-color:var(--acc); box-shadow:0 0 0 2px rgba(3,169,244,.25); }
.pg span { display:flex; flex-direction:column; flex:1; min-width:0; } .pg small { color:var(--sec); font-size:12px; }
.pg ha-icon { --mdc-icon-size:16px; color:var(--sec); }
.pgsep { font-size:12px; color:var(--sec); margin:12px 0 6px; }
.pgr { display:flex; align-items:center; gap:2px; margin-bottom:6px; } .pgr .pg { margin:0; flex:1; min-width:0; }
.pgr[draggable="true"] { cursor:grab; } .pgr.drag { opacity:.4; } .pgr.drop .pg { outline:2px dashed var(--acc); outline-offset:2px; }
.pgr .plus { opacity:.45; } .pgr:hover .plus { opacity:1; }
.pgr select.hsel { max-width:110px; font-size:12px; padding:2px 4px; margin-left:4px; }
.hzus { display:flex; flex-direction:column; gap:6px; margin-bottom:6px; }
.hzu { display:flex; flex-direction:column; gap:3px; font-size:13px; } .hzu > span { display:flex; align-items:center; gap:6px; font-weight:600; }
.hzu > span ha-icon { --mdc-icon-size:18px; color:var(--acc); } .hzu select { width:100%; }
.mmain.hstdmain { grid-template-columns:250px minmax(0,1fr); } .hstdw .mbar { padding-top:0; } .hstdw .hmain { padding-left:0; padding-right:0; }
@media (max-width: 760px) { .mmain.hstdmain { grid-template-columns:1fr; } }
.mpfad { display:flex; flex-wrap:wrap; align-items:center; gap:2px; font-size:13px; margin:0 0 8px; color:var(--sec); }
.mpfad ha-icon { --mdc-icon-size:16px; } .mpfad b { color:var(--primary-text-color); } .mpfad small { margin-left:6px; }
.mpfad .lnk { background:none; border:0; color:var(--acc); cursor:pointer; font:inherit; padding:2px 4px; border-radius:6px; } .mpfad .lnk:hover { background:var(--acc-s); }
.mpopt > summary { cursor:pointer; font-size:13px; color:var(--sec); margin:4px 0; }
.pgrid .pi.t-page { cursor:pointer; }
.hakts .hakt { display:flex; flex-wrap:wrap; align-items:center; gap:8px; margin-top:8px; } .hakts .hakt b { min-width:110px; } .hakts .hakt select { width:auto; flex:1 1 200px; }
.pgrid .pi.t-widget { padding:0; overflow:hidden; } .wbox { display:flex; width:100%; height:100%; align-items:stretch; }
.wbox .wt { flex:1; min-width:0; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:1px; font-size:10px; }
.wbox.kombi .wt + .wt { border-left:1px solid #2e3643; }
.wbox .wt:not(.g) { flex-direction:row; gap:5px; } .wbox .wt b { font-size:12px; } .wbox .wt.g b { font-size:20px; } .wbox .wuhr.g b { font-size:28px; }
.wbox .wt i { color:#8b94a3; font-style:normal; font-size:9px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; max-width:100%; }
.wbox .wt ha-icon { --mdc-icon-size:16px; } .wbox .wt.g ha-icon { --mdc-icon-size:26px; }
.wbox .wmed > span { display:flex; flex-direction:column; min-width:0; }
.tog { padding:4px; flex:0 0 auto; } .tog0 { width:26px; flex:0 0 auto; }
.srt { display:flex; flex-direction:column; } .srt .icon.sm { padding:1px; } .srt ha-icon { --mdc-icon-size:16px; }
.pg.aus { opacity:.55; } .pg .pic { --mdc-icon-size:18px; color:var(--sec); }
.popedit { display:flex; flex-direction:column; gap:6px; border:1px solid var(--acc); border-radius:10px; padding:10px; }
.popedit .ph { display:flex; align-items:center; justify-content:space-between; font-size:13px; color:var(--sec); margin-top:4px; }
.trig2 { display:flex; flex-direction:column; gap:6px; padding:8px; border:1px solid var(--div); border-radius:10px; margin-bottom:6px; }
.trig2 .tz { display:flex; align-items:center; gap:6px; flex-wrap:wrap; } .trig2 .entity.sm { flex:1; padding:6px 10px; min-width:0; text-align:left; }
.trig2 .entity small { font-size:11px; display:block; white-space:normal; } .trig2 .entity small i { color:var(--acc); font-style:normal; }
.trig2 select { width:auto; max-width:160px; } .trig2 input { flex:1 1 120px; min-width:100px; } .trig2 .eq { color:var(--sec); }
.trig2 .tsatz { color:var(--sec); font-size:12px; }
.trig { display:flex; align-items:center; gap:4px; } .trig .entity.sm { flex:1; padding:4px 8px; min-width:0; } .trig .entity small { font-size:11px; }
.trig input { width:96px; flex:0 0 auto; padding:6px; font-size:13px; } .trig .attr { width:70px; } .trig .eq { color:var(--sec); }
.lose { display:flex; flex-direction:column; gap:6px; border:1px dashed var(--warning-color,#f57c00); border-radius:8px; padding:8px 10px; font-size:13px; }
.lose > ha-icon { color:var(--warning-color,#f57c00); } .lose .inl select { width:auto; flex:1; } .lose .inl { flex-wrap:wrap; }
.phd { display:flex; flex-direction:column; gap:8px; margin-bottom:12px; }
.phd .chk { display:flex; flex-direction:column; gap:2px; } .phd .sw.small { margin-top:0; }
.phd .btns { flex-wrap:wrap; } .phd .btns > * { flex:0 0 auto; }
.mono { font-family:monospace; font-size:13px; max-width:130px; }
code { font-size:12px; background:rgba(127,127,127,.15); padding:1px 4px; border-radius:4px; }
.phone { width:300px; margin:0 auto; background:#0e1116; border-radius:18px; padding:8px 0 12px; box-shadow:0 6px 20px rgba(0,0,0,.35); color:#f1f3f5; font-size:15px; }
.phead { display:flex; align-items:center; gap:10px; margin:0 8px 0; height:50px; background:#0e1116; }
.phead b { font-size:20px; font-weight:500; } .pback { background:#1a1f27; border-radius:20px; width:56px; height:36px; display:flex; align-items:center; justify-content:center; color:#8b94a3; }
.pgrid { position:relative; width:240px; zoom:1.25; }
.pgrid .pi.t-camera { overflow:hidden; } .pgrid .kambild { position:absolute; inset:0; width:100%; height:100%; object-fit:cover; }
.pgrid .kamname { position:absolute; left:4px; bottom:3px; font-size:9px; background:rgba(0,0,0,.55); padding:0 4px; border-radius:4px; font-style:normal; }
.pgrid .pscr { position:absolute; left:-4px; right:-4px; height:0; border-top:2px dashed #f59e0b; z-index:3; pointer-events:none; }
.pgrid .pscr span { position:absolute; right:4px; top:-15px; font-size:9px; line-height:12px; padding:0 5px; border-radius:6px; background:#f59e0b; color:#111; font-weight:600; }
.pgrid .pdunkel { position:absolute; left:0; right:0; background:repeating-linear-gradient(135deg, rgba(0,0,0,.28) 0 6px, rgba(0,0,0,.12) 6px 12px); z-index:2; pointer-events:none; }
.phone .pinfo { display:flex; align-items:center; justify-content:center; gap:6px; font-size:12px; color:#8b94a3; margin-top:10px; } .phone .pinfo ha-icon { --mdc-icon-size:16px; }
.gl span { position:absolute; border:1px dashed rgba(255,255,255,.06); border-radius:6px; }
.pi { position:absolute; background:#1a1f27; border:2px solid #1a1f27; border-radius:10px; display:flex; align-items:center; gap:4px; padding:0 8px; overflow:hidden; cursor:pointer; user-select:none; touch-action:none; box-sizing:border-box; }
.pi.tile { flex-direction:column; justify-content:center; padding:2px; text-align:center; }
.pi b { font-weight:400; font-size:13px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .pi.tile b { font-size:11px; white-space:normal; line-height:1.15; }
.pi i { font-style:normal; color:#8b94a3; font-size:11px; white-space:nowrap; } .pi.is-on i { color:#22c55e; } .pi.is-on.tile b { color:#22c55e; }
.pi ha-icon { --mdc-icon-size:16px; color:var(--ic, inherit); } .pi.t-text { background:transparent; border-color:transparent; align-items:flex-end; padding:0 2px 2px; }
.pi b, .pi .tx { color:var(--fg, inherit); text-align:var(--al, inherit); } .pi.row b { flex:1 1 auto; }
.pi b { font-size:var(--fs, 13px); } .pi.tile b { font-size:var(--fs, 11px); }
.swatches { display:flex; flex-wrap:wrap; gap:6px; margin-top:4px; }
.swatch { width:28px; height:28px; border-radius:8px; border:2px solid var(--div); background:#1a1f27; color:#8b94a3; cursor:pointer; padding:0; display:flex; align-items:center; justify-content:center; font:inherit; }
.swatch.on { border-color:var(--primary-color); box-shadow:0 0 0 2px rgba(3,169,244,.35); }
.swatch.eigen { position:relative; overflow:hidden; } .swatch.eigen input { position:absolute; inset:0; opacity:0; cursor:pointer; } .swatch.eigen ha-icon { --mdc-icon-size:16px; }
.seg.sm button { padding:4px 10px; font-size:13px; }
.pi .tx { color:var(--fg, #8b94a3); font-size:var(--fs, 11px); text-align:var(--al, left); } .pi .tx.wrap { white-space:normal; align-self:flex-start; line-height:1.25; }
.pi .row1 { display:flex; width:100%; justify-content:space-between; position:absolute; top:4px; left:0; padding:0 8px; box-sizing:border-box; }
.pi .sl { position:absolute; bottom:8px; left:5%; width:90%; height:6px; background:#4b5563; border-radius:3px; } .pi .sl span { display:block; height:100%; background:#3b82f6; border-radius:3px; }
.pi b.rz1 { width:30%; flex:none; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; } .pi .sl.sl1 { position:static; flex:1; margin:0 10px; width:auto; } .pi .rz1w { width:36px; flex:none; text-align:right; }
.pi .cb { position:absolute; bottom:3px; left:0; width:100%; display:flex; justify-content:space-between; padding:0 6px; box-sizing:border-box; }
.pi .cb em { font-style:normal; font-size:10px; background:#232a35; border-radius:6px; width:31%; text-align:center; padding:4px 0; }
.pi.t-camera { background:#000; flex-direction:column; justify-content:center; }
.pi.t-mtitel { background:#16202e; border-color:#16202e; }
.pi .mti { display:flex; flex-direction:column; justify-content:space-between; width:100%; height:100%; min-width:0; padding:1px 2px; box-sizing:border-box; }
.pi .mti b { font-size:13px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .pi .mti small { color:#8b94a3; font-size:10px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .pi .mti small.an { color:#22c55e; }
.pi .mtk { display:flex; flex-direction:column; align-items:center; justify-content:center; gap:1px; width:100%; height:100%; color:var(--fg, #f1f3f5); }
.pi .mtk.neben { flex-direction:row; justify-content:flex-start; gap:8px; padding-left:4px; }
.pi .mtk ha-icon { --mdc-icon-size:20px; color:var(--ic, inherit); } .pi .mtk small { font-size:9px; color:#8b94a3; } .pi .mtk b { font-size:12px; }
.pi .mtk.an, .pi .mtk.an ha-icon, .pi .mtk.an small, .pi .mtk.an b { color:#22c55e; } .pi .mtk.rot, .pi .mtk.rot ha-icon, .pi .mtk.rot small { color:#f87171; }
.pi.msel { outline:2px dashed #f59e0b; outline-offset:1px; } .mli.msel { background:color-mix(in srgb, #f59e0b 14%, transparent); }
.pi.gesperrt { opacity:.4; } .pi.gesperrt.weg { opacity:.25; border-style:dashed; }
.pi .zz { display:flex; flex-direction:column; min-width:0; } .pi .zz b, .pi .zz i { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.pi .gw { display:flex; flex:1; align-items:center; justify-content:space-between; min-width:0; } .pi .gw.hoch { flex-direction:column; align-items:flex-start; justify-content:space-between; height:100%; }
.pi .gw small { color:#8b94a3; font-size:10px; } .pi .gw b { font-size:18px; } .pi .gw.hoch b { font-size:22px; }
.pi i.sub { position:absolute; bottom:2px; left:0; right:0; text-align:center; font-size:9px; color:#8b94a3; }
.pi em.badge { position:absolute; top:-4px; right:-4px; background:#dc2626; color:#fff; font-style:normal; font-size:9px; border-radius:7px; padding:0 4px; line-height:14px; }
.pi .mbar { display:block; height:3px; background:#2e3643; border-radius:2px; } .pi .mbar span { display:block; height:100%; background:#3b82f6; border-radius:2px; }
.pi .mseg { display:flex; gap:3px; width:100%; height:100%; } .pi .mseg span { flex:1; display:flex; align-items:center; justify-content:center; background:#232a35; border-radius:7px; font-size:12px; }
.pi .mseg span.an { background:#14532d; color:#22c55e; }
.pi b.lauf { text-overflow:clip; }
.mgbar { display:flex; flex-wrap:wrap; gap:6px; align-items:center; padding:6px 8px; margin:6px 0; border-radius:10px; background:color-mix(in srgb, #f59e0b 12%, transparent); }
.teile { display:grid; grid-template-columns:repeat(auto-fit, minmax(220px, 1fr)); gap:12px; max-height:55vh; overflow:auto; }
.teile h3 { margin:6px 0; font-size:14px; } .teile i.neu { color:#22c55e; font-style:normal; } .teile i.ers { color:#f59e0b; font-style:normal; }
.dlg.wide { max-width:860px; width:calc(100vw - 32px); }
.farbe { display:inline-flex; align-items:center; gap:4px; font-size:12px; } .farbe input[type=color] { width:28px; height:24px; padding:0; border:none; background:none; }
.zst { display:flex; flex-wrap:wrap; gap:6px; align-items:center; margin:4px 0; } .zst > input:first-child { width:110px; }
.phone.dspl { width:auto; display:inline-flex; flex-direction:column; align-items:center; padding:12px; }
.dscr { position:relative; width:240px; height:320px; zoom:1.25; background:#0b0f14; overflow:hidden; color:#f1f3f5; display:flex; flex-direction:column; }
.dkopf { flex:0 0 40px; display:flex; align-items:center; gap:4px; padding-right:5px; border-bottom:3px solid var(--akz, #3b82f6); }
.dkopf .dx { align-self:stretch; width:54px; background:var(--akz, #3b82f6); display:flex; align-items:center; justify-content:center; font-size:17px; }
.dkopf b { flex:1; min-width:0; font-size:17px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; padding-left:4px; }
.dkopf .dm { flex:0 0 30px; height:30px; border-radius:8px; background:#1f2933; display:flex; align-items:center; justify-content:center; font-size:10px; font-weight:700; --mdc-icon-size:18px; }
.dkopf .dm.an { background:var(--akz, #3b82f6); }
.dkbd { flex:1; display:flex; flex-direction:column; gap:6px; padding:5px 4px 3px; }
.dkbd > div { flex:1; display:flex; gap:4px; }
.dkbd span { flex:1; min-width:0; background:#1f2933; border-radius:7px; display:flex; align-items:center; justify-content:center; font-size:12px; }
.dkbd.zahlen { gap:5px; } .dkbd.zahlen > div { gap:5px; } .dkbd.zahlen span { font-size:18px; border-radius:9px; }
.wmaus { display:flex; align-items:center; justify-content:center; width:100%; height:100%; border-radius:inherit; color:#d0d4da; --mdc-icon-size:18px; }
.wmaus.touchpad { background:#1a1f27; color:#3a4250; box-shadow:inset 0 0 0 1px #2a3240; } .wmaus.scroll { background:#232a35; color:#8b94a3; } .wmaus.mausknopf { background:#232a35; }
.eingopt { display:flex; flex-wrap:wrap; gap:6px 14px; margin:6px 0; }
.dtp { flex:1; display:flex; flex-direction:column; gap:6px; padding:4px 6px 6px; }
.dtp > div { display:flex; gap:4px; } .dtp > div:first-child { flex:1; } .dtp > div:last-child { flex:0 0 46px; gap:7px; }
.dtp .dfeld { flex:1; border:1px solid #2a3038; } .dtp .dleiste { flex:0 0 28px; } .dtp .dk { flex:1; }
.dtp span { background:#181c20; border-radius:10px; display:flex; align-items:center; justify-content:center; color:#525962; --mdc-icon-size:18px; }
.dtp .dk { background:#1f2933; color:#d0d4da; }
.haweg { display:flex; gap:12px; align-items:flex-start; padding:12px 14px; margin:0 0 12px; border-radius:14px; background:color-mix(in srgb, #f59e0b 14%, transparent); border:1px solid color-mix(in srgb, #f59e0b 40%, transparent); }
.haweg > ha-icon { color:#f59e0b; flex:0 0 auto; --mdc-icon-size:26px; } .haweg > div { display:flex; flex-direction:column; gap:4px; } .haweg ol { margin:4px 0; padding-left:20px; }
.haweg.klein { margin:8px 0 0; padding:8px 10px; } .haweg.klein > ha-icon { --mdc-icon-size:20px; }
.neuversion { display:flex; gap:10px; align-items:flex-start; padding:10px 12px; margin:8px 0; border-radius:12px; background:color-mix(in srgb, #22c55e 14%, transparent); }
.neuversion ha-icon { color:#22c55e; flex:0 0 auto; }
.feldok { color:var(--ok, #22c55e); flex:0 0 auto; }
.appbox { margin-top:10px; padding:10px 12px; border-radius:12px; border:1px solid var(--div); }
.appbox ol { margin:6px 0; padding-left:20px; } .appbox ol li { margin:3px 0; }
.appst { list-style:none; padding:0; margin:8px 0; } .appst li { display:flex; gap:6px; align-items:center; margin:3px 0; }
.appst li.gut ha-icon { color:#22c55e; } .appst li.schlecht { color:#f87171; } .appst ha-icon { --mdc-icon-size:18px; }
.vorlagen { display:grid; grid-template-columns:repeat(auto-fill, minmax(210px, 1fr)); gap:8px; margin:8px 0 12px; }
.vorl { display:flex; gap:10px; align-items:center; text-align:left; padding:10px; border-radius:12px; border:1px solid var(--div); background:transparent; color:inherit; cursor:pointer; font:inherit; }
.vorl.on { border-color:var(--acc); background:color-mix(in srgb, var(--acc) 12%, transparent); }
.vorl span { display:flex; flex-direction:column; } .vorl small { color:var(--secondary-text-color); }
.pi.sel { border-color:#3b82f6; background:#2a3340; z-index:2; }
.pgrid.free .pi { cursor:move; }
.rs { position:absolute; right:0; bottom:0; width:14px; height:14px; background:#3b82f6; border-radius:4px 0 8px 0; cursor:nwse-resize; }
.mlist { margin-top:12px; }
.mli { display:flex; align-items:center; gap:4px; border:1px solid var(--div); border-radius:8px; margin-bottom:4px; background:var(--card); }
.mli.sel { border-color:var(--acc); } .mli.drag { opacity:.4; }
.mli .hd { cursor:grab; color:var(--sec); padding:10px 4px 10px 8px; touch-action:none; }
.mli.over-top { box-shadow:0 -3px 0 var(--acc); } .mli.over-bot { box-shadow:0 3px 0 var(--acc); }
.mli .ml { flex:1; display:flex; align-items:center; gap:8px; background:none; border:0; color:inherit; text-align:left; padding:6px; cursor:pointer; font:inherit; min-width:0; }
.mli .ml span { display:flex; flex-direction:column; min-width:0; } .mli small { color:var(--sec); font-size:12px; } .mli b { font-weight:500; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.form { display:flex; flex-direction:column; gap:10px; padding-top:12px; }
.form label { display:flex; flex-direction:column; gap:4px; font-size:13px; color:var(--sec); }
.form label.sw { flex-direction:row; }
.iconpick { display:flex; align-items:center; gap:8px; border:1px solid var(--div); background:transparent; color:var(--primary-text-color); border-radius:8px; padding:8px; cursor:pointer; font:inherit; }
.list.icons { display:grid; grid-template-columns:repeat(auto-fill,minmax(84px,1fr)); grid-auto-rows:76px; align-content:start; gap:4px; }
.ico { display:flex; flex-direction:column; align-items:center; gap:2px; border:1px solid var(--div); background:transparent; color:inherit; border-radius:8px; padding:6px 2px; cursor:pointer; }
.ico small { font-size:10px; color:var(--sec); overflow:hidden; text-overflow:ellipsis; max-width:100%; white-space:nowrap; }
@media (max-width: 1100px) { .mmain { grid-template-columns:200px 1fr; } .mmain .editor { grid-column:1 / -1; } }
@media (max-width: 870px) {
  .mmain { grid-template-columns:1fr; padding:12px; }
  .mpages { max-height:45vh; overflow-y:auto; border:1px solid var(--div); border-radius:10px; padding:8px; }
  .mmain.editing .editor { position:fixed; inset:0; z-index:10; border-radius:0; border:0; overflow-y:auto; padding-top:0; display:block; }
  .mmain:not(.editing) .editor { display:none; }
}
`;

export const THEME_CSS = `
:host { --r:14px; --r-s:10px; --acc-s:color-mix(in srgb, var(--acc) 14%, transparent); --acc-m:color-mix(in srgb, var(--acc) 30%, transparent);
  --flaeche:color-mix(in srgb, var(--primary-text-color) 4%, var(--card)); --rand:color-mix(in srgb, var(--primary-text-color) 11%, transparent);
  --schatten:0 1px 2px rgba(0,0,0,.05), 0 4px 18px rgba(0,0,0,.06); --ok:var(--success-color,#16a34a); --fehler:var(--error-color,#dc2626);
  --div:var(--rand); -webkit-font-smoothing:antialiased; }
button, .btn, .chip, .key, .pg, .ent, .entity, .opt, .mli { transition:background-color .15s, border-color .15s, box-shadow .15s, color .15s, transform .1s; }
button:focus-visible, select:focus-visible, input:focus-visible, textarea:focus-visible { outline:2px solid var(--acc); outline-offset:1px; }
select, input, textarea { border-radius:var(--r-s); background:var(--card); border-color:var(--rand); }
select:hover, input:hover, textarea:hover { border-color:color-mix(in srgb, var(--primary-text-color) 25%, transparent); }
select:focus, input:focus, textarea:focus { outline:0; border-color:var(--acc); box-shadow:0 0 0 3px var(--acc-s); }
::-webkit-scrollbar { width:10px; height:10px; } ::-webkit-scrollbar-thumb { background:var(--rand); border-radius:5px; border:2px solid transparent; background-clip:padding-box; }

/* Knoepfe */
.btn { border-radius:999px; border-color:var(--rand); font-weight:500; padding:7px 14px; }
.btn:not(:disabled):hover { background:var(--flaeche); border-color:color-mix(in srgb, var(--primary-text-color) 22%, transparent); }
.btn.primary:not(:disabled):hover { background:color-mix(in srgb, var(--acc) 88%, #000); }
.btn:not(:disabled):active, .chip:active { transform:scale(.97); }
.btn.small { padding:5px 11px; }
.icon:hover { background:var(--flaeche); }
.chip { border-color:var(--rand); font-weight:500; }
.chip:hover { background:var(--flaeche); }
.chip.on, .seg button.on { box-shadow:0 2px 8px var(--acc-m); }

/* Umschalter */
.seg { background:var(--flaeche); border:1px solid var(--rand); border-radius:12px; padding:3px; gap:2px; }
.seg button { border-radius:9px; padding:6px 14px; color:var(--sec); font-weight:500; }
.seg button:hover { color:var(--primary-text-color); }
.seg button.on { background:var(--card); color:var(--acc); box-shadow:0 1px 3px rgba(0,0,0,.12); }
.seg.sm { padding:2px; } .seg.sm button { padding:3px 10px; }

/* Schalter statt Kaestchen */
.sw input[type=checkbox] { appearance:none; -webkit-appearance:none; position:relative; width:38px; height:22px; flex:0 0 38px; margin:0; padding:0;
  border-radius:11px; background:color-mix(in srgb, var(--primary-text-color) 22%, transparent); border:0; cursor:pointer; transition:background-color .2s; }
.sw input[type=checkbox]::after { content:""; position:absolute; top:3px; left:3px; width:16px; height:16px; border-radius:50%; background:#fff;
  box-shadow:0 1px 2px rgba(0,0,0,.3); transition:transform .2s; }
.sw input[type=checkbox]:checked { background:var(--acc); } .sw input[type=checkbox]:checked::after { transform:translateX(16px); }
.sw input[type=checkbox]:disabled { opacity:.4; cursor:default; } .sw input[type=checkbox]:focus { box-shadow:0 0 0 3px var(--acc-s); }
.sw.small { margin-top:0; }

/* Meldungen und Status */
.msg { border-radius:12px; padding:10px 14px; background:var(--acc-s); border:1px solid var(--acc-m); }
.msg.err { background:color-mix(in srgb, var(--fehler) 10%, transparent); border-color:color-mix(in srgb, var(--fehler) 30%, transparent); }
.xfer { border-radius:12px; border-color:var(--rand); box-shadow:var(--schatten); }
.xfer.ok { border-color:color-mix(in srgb, var(--ok) 45%, transparent); } .xfer.err { border-color:color-mix(in srgb, var(--fehler) 45%, transparent); }

/* Aktivitaet: Name, Bluetooth-Geraet, Leiste */
.aktset { padding:10px 16px 0; gap:10px 16px; }
.aktset > :is(.inl2, .sw) { background:var(--card); border:1px solid var(--rand); border-radius:12px; padding:4px 6px 4px 12px; min-height:42px; }
.aktset input, .aktset select { border:0; background:var(--flaeche); padding:6px 8px; }

/* Fernbedienung */
.main { gap:20px; padding:16px 16px 40px; }
.remote-wrap { top:76px; }
.remote { position:relative; background:linear-gradient(160deg,#3a3f47 0%,#24272d 40%,#18191d 100%); border-radius:42px 42px 56px 56px; padding:20px 16px 30px; gap:9px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.12), inset 0 -2px 6px rgba(0,0,0,.5), 0 2px 4px rgba(0,0,0,.2), 0 18px 40px rgba(0,0,0,.28); }
.row.r2 { grid-template-columns:1fr 1fr; }
.row.aktev { margin-top:6px; padding-top:10px; border-top:1px dashed rgba(255,255,255,.12); }
.row.aktev .key { height:52px; background:transparent; border:1px dashed rgba(255,255,255,.18); }
.screen { border:0; cursor:pointer; font:inherit; padding:8px 10px; min-height:118px; color:#e8eaed; gap:4px; border-radius:14px;
  background:radial-gradient(120% 90% at 30% 0%, #1d2635 0%, #0b0d10 70%); box-shadow:inset 0 0 0 2px #0a0b0d, inset 0 0 0 3px #3a3f48, inset 0 8px 20px rgba(0,0,0,.6); }
.screen:hover { box-shadow:inset 0 0 0 2px #0a0b0d, inset 0 0 0 3px var(--acc), inset 0 8px 20px rgba(0,0,0,.6); }
.screen .sbar { display:flex; align-items:center; gap:4px; width:100%; font-size:10px; color:#8b94a3; } .screen .sbar i { font-style:normal; margin-right:auto; }
.screen .sbar ha-icon { --mdc-icon-size:12px; }
.screen b { font-size:15px; font-weight:600; margin:auto 0 0; max-width:100%; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.screen small { display:inline-flex; align-items:center; gap:3px; font-size:11px; color:#8b94a3; opacity:1; margin-bottom:auto; } .screen small ha-icon { --mdc-icon-size:12px; }
.screen:hover small { color:var(--acc); }
.key { background:linear-gradient(180deg,#464c56,#383d46); border:2px solid transparent; border-radius:15px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.1), 0 2px 0 #15171b, 0 3px 6px rgba(0,0,0,.25); }
.key:hover { background:linear-gradient(180deg,#505762,#414752); }
.key:active { transform:translateY(1px); box-shadow:inset 0 1px 0 rgba(255,255,255,.08), 0 1px 0 #15171b; }
.key.sel { border-color:var(--acc); box-shadow:0 0 0 4px var(--acc-m), inset 0 1px 0 rgba(255,255,255,.1); }
.key.ok { background:radial-gradient(circle at 50% 35%, #555c67, #3a3f48); }
.row.power .key { background:linear-gradient(180deg,#7a2e2e,#5a2222); } .row.power .key:hover { background:linear-gradient(180deg,#8a3434,#682828); }
.key ha-icon { --mdc-icon-size:22px; }
.key .kl { color:#b4bbc6; }
.key .dots { bottom:4px; }
.hint { line-height:1.7; margin-top:14px; }
.clip { border-radius:12px; }

/* Bearbeitungs-Flaeche */
.editor { border:1px solid var(--rand); border-radius:18px; box-shadow:var(--schatten); padding:4px 20px 20px; }
.edhead { top:60px; padding:14px 0 12px; border-bottom-color:var(--rand); }
.edhead h2 { font-weight:600; }
.gest h3 { font-weight:600; font-size:14px; letter-spacing:.01em; margin:20px 0 10px; }
.gest h3 > ha-icon { color:var(--acc); background:var(--acc-s); border-radius:8px; padding:4px; --mdc-icon-size:18px; }
.step { border-color:var(--rand); border-radius:12px; background:var(--card); box-shadow:0 1px 2px rgba(0,0,0,.04); }
.step:hover { border-color:color-mix(in srgb, var(--primary-text-color) 20%, transparent); }
.sh { background:var(--flaeche); padding:5px 5px 5px 12px; border-bottom:1px solid var(--rand); }
.sb { padding:12px; gap:10px; }
.none { font-style:italic; padding:10px 12px; border:1px dashed var(--rand); border-radius:12px; text-align:center; }
.haltmodus { border-color:var(--rand); border-radius:12px; background:var(--flaeche); padding:12px; }
.empty { padding:80px 16px; } .empty ha-icon { --mdc-icon-size:56px; opacity:.35; color:var(--acc); } .empty p { margin:12px auto 0; max-width:420px; line-height:1.5; }
.entity, .opt, .iconpick { border-color:var(--rand); border-radius:12px; }
.entity:hover, .opt:hover, .iconpick:hover { border-color:var(--acc); background:var(--acc-s); }
.ent:hover { background:var(--flaeche); }
details summary { user-select:none; } details summary:hover { color:var(--primary-text-color); }
.weg { border-radius:12px; }

/* Dialoge */
.modal { background:rgba(10,12,16,.45); backdrop-filter:blur(3px); -webkit-backdrop-filter:blur(3px); animation:einblenden .15s ease-out; }
.dlg { border-radius:20px; padding:10px 20px 20px; box-shadow:0 24px 60px rgba(0,0,0,.35); animation:hoch .18s ease-out; }
.dh h2 { font-weight:600; }
.dfoot { padding-top:16px; border-top:1px solid var(--rand); margin-top:8px; }
@keyframes einblenden { from { opacity:0; } }
@keyframes hoch { from { opacity:0; transform:translateY(12px) scale(.98); } }

/* Menue- und Startseiten-Editor */
.mbar { padding:14px 16px 0; }
.pg { border-color:var(--rand); border-radius:12px; }
.pg:hover { background:var(--flaeche); }
.pg.on { border-color:var(--acc); background:var(--acc-s); box-shadow:none; }
.mli { border-color:var(--rand); border-radius:12px; } .mli:hover { border-color:color-mix(in srgb, var(--primary-text-color) 20%, transparent); }
.mli.sel { border-color:var(--acc); background:var(--acc-s); }
.phone, .hprev { border-radius:28px; box-shadow:inset 0 0 0 2px #23272e, 0 18px 40px rgba(0,0,0,.28); }
.hl { border-color:var(--rand); border-radius:12px; } .hl.sel { background:var(--acc-s); }
.swatch { border-radius:10px; }
.ico { border-color:var(--rand); border-radius:10px; } .ico:hover { border-color:var(--acc); background:var(--acc-s); }

/* Dock, Firmware, Einstellungen: Karten */
.dock, .fw, .einst { margin:0 auto; padding:16px 16px 48px; }
.dock .karte, .fw .karte, .einst .karte, .dock .karte.hw { border-color:var(--rand); border-radius:18px; padding:16px 18px; box-shadow:var(--schatten); }
.dock .dkopf h2, .fw h3 { font-weight:600; }
.dock .zeile input, .dock .zeile select, .hw select { border-color:var(--rand); border-radius:var(--r-s); background:var(--card); }
.dock table { border-collapse:separate; border-spacing:0; } .dock td { border-top-color:var(--rand); padding:7px 8px; }
.dock tr:hover td { background:var(--flaeche); }
.badge, .dock .badge { border-radius:999px; padding:2px 8px; font-weight:600; background:var(--flaeche); border:1px solid var(--rand); }
.badge.ok, .badge.warn, .badge.blau, .badge.rot, .dock .badge.eigen { border-color:transparent; }
.balken, .xbar { height:8px; border-radius:4px; background:var(--flaeche); }
.joblog, .neulog pre { border-radius:12px; }
.einst .egrid { gap:16px; }
.einst h3 { font-weight:600; font-size:15px; margin:0 0 10px; }
.einst h3 > ha-icon { color:var(--acc); background:var(--acc-s); border-radius:8px; padding:4px; --mdc-icon-size:18px; }
.einst .ez { border-top-color:var(--rand); min-height:42px; padding:4px 0; }
.einst .ez.anders > span { font-weight:500; }

@media (max-width: 870px) {
  .main { padding:12px 12px 32px; }
  .editor { border-radius:0; box-shadow:none; }
  .main.editing .editor, .mmain.editing .editor { padding:0 16px 24px; }
  .dlg { border-radius:0; }
  .aktset > :is(.inl2, .sw) { flex:1 1 auto; }
}

/* Speichern-Leiste: erscheint unten mittig, sobald etwas ungespeichert ist */
.savebar { position:fixed; left:50%; bottom:20px; transform:translateX(-50%); z-index:15; display:flex; align-items:center; gap:8px;
  max-width:calc(100% - 24px); padding:8px 8px 8px 16px; border-radius:999px; background:var(--card); border:1px solid var(--rand);
  box-shadow:0 10px 36px rgba(0,0,0,.25), 0 0 0 4px var(--acc-s); animation:hochmitte .2s ease-out; }
.savebar > ha-icon { color:var(--acc); flex:0 0 auto; }
.savebar .sbt { font-size:14px; font-weight:500; margin-right:6px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; min-width:0; }
.savebar .btn { flex:0 0 auto; }
.savebar-platz { height:84px; }
@keyframes hochmitte { from { opacity:0; transform:translate(-50%, 16px); } }

/* Kopf je Abschnitt (Dock, Firmware) */
.dkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin:26px 0 8px; }
.dock > .dkopf:first-of-type, .fw > .dkopf:first-child { margin-top:4px; }
.dkopf h2, .dkopf h3 { margin:0; display:flex; align-items:center; gap:8px; font-weight:600; }
.dkopf h2 { font-size:20px; } .dkopf h3 { font-size:16px; }
.dkopf h2 > ha-icon { color:var(--acc); }
.dkopf + .hint { text-align:left; max-width:none; margin:0 0 8px; }
.fw > .hint, .dock > .hint { text-align:left; max-width:none; }
.dockleiste { display:flex; flex-wrap:wrap; gap:8px; align-items:center; margin:4px 0 8px; padding:10px 12px; border-radius:14px; background:var(--flaeche); border:1px solid var(--rand); }
.dockleiste .dl { display:inline-flex; align-items:center; gap:6px; font-size:13px; font-weight:500; padding:4px 10px 4px 8px; border-radius:999px; background:var(--card); border:1px solid var(--rand); }
.dockleiste .dl ha-icon { --mdc-icon-size:16px; color:var(--sec); }
.dockleiste .dl i { font-style:normal; font-weight:400; font-size:12px; color:var(--sec); }
.dockleiste .dl.on i { color:var(--ok); } .dockleiste .dl.on ha-icon { color:var(--ok); }
.dockleiste .dl b { font-size:11px; padding:1px 6px; border-radius:999px; background:#d97706; color:#fff; }
.dockleiste .btn { margin-left:auto; }
.dockleiste + .dkopf { margin-top:18px; }
.main.editing:has(~ .savebar) .editor { padding-bottom:96px; }

@media (max-width: 870px) {
  .savebar { left:12px; right:12px; bottom:12px; transform:none; border-radius:18px; animation:none; }
  .savebar .sbt { flex:1; font-size:13px; }
  .savebar .btn { padding:7px 10px; }
  .dockleiste .btn { margin-left:0; }
}
@media (max-width: 520px) { .savebar .sbt { display:none; } .savebar { justify-content:flex-end; } }
@media (prefers-reduced-motion: reduce) { *, *::after { transition:none !important; animation:none !important; } }
`;

export const CSS = `
.addart .opts .opt { width:100%; text-align:left; cursor:pointer; border:1px solid var(--div); border-radius:10px; background:var(--card); color:var(--primary-text-color); font:inherit; }
.addart .opts .opt:hover { border-color:var(--acc, var(--primary-color)); }
option, optgroup { background:var(--card-background-color, var(--primary-background-color)); color:var(--primary-text-color); }
:host { display:block; min-height:100vh; background:var(--primary-background-color); color:var(--primary-text-color);
  font-family:var(--paper-font-body1_-_font-family, Roboto, system-ui, sans-serif), "ESPHomeRemote Symbole"; --acc:var(--primary-color,#03a9f4);
  --card:var(--card-background-color,#fff); --div:var(--divider-color,rgba(127,127,127,.25)); --sec:var(--secondary-text-color,#888); }
* { box-sizing:border-box; }
ha-icon { --mdc-icon-size:20px; }
.grow { flex:1; }
.icon { background:none; border:0; color:inherit; padding:8px; border-radius:50%; cursor:pointer; display:inline-flex; }
.icon:hover { background:rgba(127,127,127,.15); }
.btn { display:inline-flex; align-items:center; gap:6px; border:1px solid var(--div); background:var(--card); color:var(--primary-text-color);
  padding:7px 12px; border-radius:18px; font-size:14px; cursor:pointer; white-space:nowrap; }
.btn.primary { background:var(--acc); color:#fff; border-color:transparent; }
.btn.ghost { background:transparent; }
.btn.small { padding:4px 10px; font-size:13px; }
.btn:disabled { opacity:.45; cursor:default; }
.sw { display:inline-flex; align-items:center; gap:8px; cursor:pointer; }
.sw input { width:18px; height:18px; accent-color:var(--acc); }
.sw.small { font-size:13px; color:var(--sec); margin-top:8px; }
.st { display:inline-flex; align-items:center; gap:6px; font-size:13px; color:var(--sec); }
.st.ok { color:var(--success-color,#43a047); }
.xfer { border:1px solid var(--div); border-radius:10px; padding:8px 12px; background:var(--card); display:flex; flex-direction:column; gap:6px; }
.xfer.ok { border-color:var(--success-color,#43a047); } .xfer.err { border-color:var(--error-color,#db4437); }
.xsteps { display:flex; align-items:center; gap:4px; flex-wrap:wrap; font-size:12px; color:var(--sec); }
.xs { display:inline-flex; align-items:center; gap:3px; } .xs ha-icon { --mdc-icon-size:16px; }
.xs.ok { color:var(--success-color,#43a047); } .xs.cur { color:var(--acc); font-weight:500; } .xs.err { color:var(--error-color,#db4437); font-weight:500; }
.xl { flex:0 1 18px; height:1px; background:var(--div); }
.xbar { height:6px; border-radius:3px; background:rgba(127,127,127,.2); overflow:hidden; }
.xbar span { display:block; height:100%; background:var(--acc); transition:width .4s; }
.xfer.ok .xbar span { background:var(--success-color,#43a047); } .xfer.err .xbar span { background:var(--error-color,#db4437); }
.xbar.busy span { animation:xpulse 1.2s ease-in-out infinite; }
@keyframes xpulse { 0%,100% { opacity:.45; } 50% { opacity:1; } }
.xtxt { font-size:13px; display:flex; align-items:center; gap:8px; flex-wrap:wrap; } .st.warn { color:var(--warning-color,#f57c00); }
.msg { margin:10px 16px 0; padding:10px 12px; border-radius:8px; background:rgba(3,169,244,.12); display:flex; align-items:center; gap:8px; }
.msg.err { background:rgba(219,68,55,.15); color:var(--error-color,#db4437); }
.msg .icon { margin-left:auto; padding:2px; }
.main { display:grid; grid-template-columns:minmax(300px,380px) 1fr; gap:16px; padding:16px; align-items:start; }
.remote-wrap { position:sticky; top:72px; }
.remote { background:linear-gradient(#2b2f36,#1d2026); border-radius:36px 36px 48px 48px; padding:18px 14px 26px; box-shadow:0 6px 24px rgba(0,0,0,.35);
  display:flex; flex-direction:column; gap:8px; max-width:360px; margin:0 auto; }
.rmodell { position:relative; width:min(100%, 330px, calc((100vh - 170px) * 60 / 206)); margin:0 auto; }
.rmodell svg { display:block; width:100%; height:auto; filter:drop-shadow(0 6px 14px rgba(0,0,0,.35)); }
.rk { cursor:pointer; } .rk .rks { fill:#fff; } .rk.frei .rks { opacity:.38; }
.rk:hover .rkf { filter:brightness(1.6); } .rk.sel .rkf { stroke:var(--acc,#03a9f4); stroke-width:.7; paint-order:stroke; }
.rscreen { all:unset; position:absolute; left:14%; top:9.417%; width:72%; height:27.961%; overflow:hidden; cursor:pointer; border-radius:1px; }
.rscreen:hover { outline:2px solid var(--acc,#03a9f4); outline-offset:2px; }
.rsinhalt { position:absolute; left:0; top:0; width:240px; height:320px; transform:scale(var(--rs)); transform-origin:0 0; pointer-events:none;
  background:#000; color:#f1f3f5; font-size:15px; }
.rsbar { display:flex; align-items:center; gap:4px; height:30px; padding:0 8px; font-size:14px; color:#cfd3d8; } .rsbar ha-icon { --mdc-icon-size:16px; }
.rsbar i { font-style:normal; font-weight:600; }
.rshome { position:absolute; top:36px; left:0; width:240px; height:240px; overflow:hidden; }
.rshome .phone { width:240px; padding:0; background:none; box-shadow:none; border-radius:0; }
.rshome .pgrid { zoom:1; }   /* im Editor 1,25-fach vergroessert - im Display 1:1 (240 px) */
.rsleiste { position:absolute; left:0; right:0; bottom:0; height:40px; display:flex; gap:var(--lgap,2px); box-sizing:border-box; background:var(--lg); border-top:var(--ll) solid var(--la); }
.rsleiste.oben { bottom:auto; border-top:0; border-bottom:var(--ll) solid var(--la); }
.rsleiste span, .lsvor span { flex:1; min-width:0; display:flex; flex-direction:var(--lsym,row); gap:3px; align-items:center; justify-content:var(--ljust,center); padding:0 4px; background:var(--lf); color:var(--lt); border-radius:var(--lr,0); }
.rsleiste span ha-icon, .lsvor span ha-icon { --mdc-icon-size:16px; flex:none; }
.rsleiste span.an { background:var(--la); color:var(--lat); }
.rsstift2 { position:absolute; right:6px; top:4px; } .rsbar[hidden] { display:none; }
.lsvor { display:flex; gap:var(--lgap,2px); height:40px; width:240px; max-width:100%; background:var(--lg); border-top:var(--ll) solid var(--la); box-sizing:border-box; margin:4px 0 8px; }
.lsvor span { font-size:var(--lfs); overflow:hidden; white-space:nowrap; text-overflow:ellipsis; } .lsvor small { font-size:var(--lfs); overflow:hidden; text-overflow:ellipsis; }
.lsvor span.an { background:var(--la); color:var(--lat); }
.akzfeld { width:22px; height:22px; border-radius:50%; border:2px solid transparent; padding:0; cursor:pointer; } .akzfeld.on { border-color:var(--primary-text-color); }
.hdisp .klickbar { cursor:pointer; } .hdisp .klickbar:hover { outline:1px dashed rgba(255,255,255,.55); outline-offset:-1px; }
.hdisp .klickbar.fok { outline:2px solid var(--blau, #3b82f6); outline-offset:-2px; }
.rsleiste span[data-ai], .lsvor span[data-ai] { cursor:grab; touch-action:none; } .rsleiste span.zieht, .lsvor span.zieht { opacity:.5; }
.rsleiste span.ziel, .lsvor span.ziel { box-shadow:inset 0 0 0 2px #fff; }
.lsfarben { display:flex; flex-wrap:wrap; gap:6px 12px; margin-bottom:8px; }
.hleisten { flex-wrap:wrap; gap:6px 14px; align-items:center; } .hleisten small { color:var(--sec,#8b94a3); }
.rsleiste small { font-size:var(--lfs); max-width:100%; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.rshome .phead, .rshome .pinfo, .rshome .gl, .rshome .pscr, .rshome .pdunkel, .rshome .rs { display:none; }
.rsstd { display:flex; flex-direction:column; align-items:center; justify-content:center; gap:6px; height:250px; text-align:center; }
.rsstd b { font-size:22px; } .rsstd small { color:#8a919b; font-size:13px; }
.rsstift { display:flex; align-items:center; gap:3px; padding:2px 8px; border-radius:10px;
  background:rgba(255,255,255,.12); font-size:12px; color:#cfd3d8; } .rsstift ha-icon { --mdc-icon-size:13px; }
.aktev { margin-top:14px; max-width:330px; margin-left:auto; margin-right:auto; display:flex; flex-direction:column; gap:6px; }
.aktev > small { color:var(--sec,#8b94a3); font-size:12px; line-height:1.3; }
.key .evl { font-weight:500; font-size:13px; }
.row { display:grid; gap:8px; }
.row.r3 { grid-template-columns:repeat(3,minmax(0,1fr)); } .row.r4 { grid-template-columns:repeat(4,minmax(0,1fr)); }
.row.power { grid-template-columns:1fr 64px; align-items:stretch; margin-bottom:6px; }
.screen { border-radius:14px; background:#0b0d10; border:2px solid #3a3f48; color:#6b7380; display:flex; flex-direction:column; align-items:center; justify-content:center; min-height:110px; font-size:13px; gap:2px; }
.screen small { font-size:11px; opacity:.7; }
.key { position:relative; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:2px; min-height:58px; padding:6px 3px 10px;
  background:#3a3f48; color:#e8eaed; border:2px solid transparent; border-radius:14px; cursor:pointer; font:inherit; }
.key:hover { background:#454b55; }
.key.sel { border-color:var(--acc); box-shadow:0 0 0 3px rgba(3,169,244,.35); }
.key.ok { border-radius:50%; aspect-ratio:1; min-height:0; justify-self:center; align-self:center; }
.key .okt { font-weight:700; font-size:16px; }
.key .kl { font-size:10px; line-height:1.15; color:#aab1bb; max-width:100%; overflow:hidden; padding:0 2px; text-align:center;
  display:-webkit-box; -webkit-line-clamp:2; -webkit-box-orient:vertical; word-break:break-word; min-height:2.3em; }
.key { height:72px; min-height:0; }
.haltmodus { display:flex; flex-direction:column; gap:6px; margin-top:12px; padding:10px; border:1px solid var(--div); border-radius:10px; }
.haltmodus b { display:flex; align-items:center; gap:6px; font-weight:500; } .haltmodus .inl2 select { width:auto; }
.gest.aus { opacity:.45; }
.aktset { display:flex; flex-wrap:wrap; align-items:center; gap:8px 14px; padding:8px 16px 0; font-size:13px; color:var(--sec); }
.aktset input { width:140px; } .aktset select { width:auto; }
/* Aktivitaets-Einstellungen eingeklappt (2026-11-10): eine Zeile, die Remote bleibt oben sichtbar */
.aktzu { display:flex; align-items:center; gap:10px; width:calc(100% - 48px); margin:10px 24px 0; box-sizing:border-box; padding:6px 12px; border:1px dashed var(--div);
  border-radius:12px; background:none; color:inherit; font:inherit; text-align:left; cursor:pointer; }
.aktzu:hover { border-style:solid; background:var(--card); }
.aktzu > ha-icon { --mdc-icon-size:18px; color:var(--sec); flex:none; }
.aktzu .azt { display:flex; flex-direction:column; min-width:0; } .aktzu .azt b { font-weight:500; font-size:13px; }
.aktzu .azt small { color:var(--sec); font-size:12px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.aktzu .azk { display:flex; align-items:center; gap:2px; color:var(--primary-color); font-size:13px; flex:none; }
.key.ok .kl { display:none; }
.key .dots { position:absolute; bottom:3px; display:flex; gap:3px; }
.key .lock { position:absolute; top:3px; left:5px; --mdc-icon-size:11px; opacity:.45; }
.key .mstar { position:absolute; top:3px; right:5px; --mdc-icon-size:12px; color:#ffca28; }
.cdot { width:22px; height:10px; border-radius:5px; }
.row.power .key { background:#5a2a2a; }
.dot { display:inline-block; width:6px; height:6px; border-radius:50%; vertical-align:middle; }
.modstart { display:flex; flex-wrap:wrap; align-items:center; gap:6px; margin:6px 0 2px; } .modstart b { display:flex; align-items:center; gap:6px; margin-right:4px; }
.dot.short { background:#4fc3f7; } .dot.double { background:#ffb74d; } .dot.long { background:#ba68c8; }
.key .offl { --mdc-icon-size:11px; color:#90a4ae; }
.ohneha { margin-top:14px; border:1px dashed var(--divider-color,#555); border-radius:12px; padding:4px 10px 8px; }
.ohneha .ohkopf { all:unset; display:flex; align-items:center; gap:8px; width:100%; padding:8px 0; cursor:pointer; }
.ohneha .ohkopf[disabled] { cursor:default; }
.ohneha .note.warn { color:var(--warning-color,#ffa000); display:block; }
.gest.off h3 { opacity:.9; }
.hint { font-size:12px; color:var(--sec); text-align:center; line-height:1.6; max-width:360px; margin:10px auto 0; }
.hint ha-icon.sm { --mdc-icon-size:13px; }
.editor { background:var(--card); border-radius:12px; border:1px solid var(--div); padding:4px 16px 16px; min-height:300px; }
.empty { text-align:center; color:var(--sec); padding:60px 10px; } .empty ha-icon { --mdc-icon-size:48px; opacity:.5; }
.edhead { display:flex; align-items:center; gap:8px; padding:10px 0; border-bottom:1px solid var(--div); position:sticky; top:56px; background:var(--card); z-index:2; }
.edhead h2 { margin:0; font-size:18px; font-weight:500; } .edhead small { color:var(--sec); }
.edhead .back { display:none; }
.gest h3 { display:flex; align-items:center; gap:8px; font-size:15px; font-weight:500; margin:16px 0 8px; }
.none { color:var(--sec); font-size:13px; padding:4px 2px; }
.note { display:block; color:var(--sec); font-size:12px; margin-top:4px; }
.step { border:1px solid var(--div); border-radius:10px; margin-bottom:8px; overflow:hidden; }
.step .sh { min-width:0; } .step .sh select { min-width:0; flex:1 1 60px; }   /* schmale Fenster: Knoepfe rechts nicht abschneiden */
.step .sh .icon { flex:none; } .step .sh .grow { flex:0 1 auto; }
.inl2 { max-width:100%; min-width:0; flex-wrap:wrap; } .inl2 select { max-width:100%; min-width:0; }   /* schmale Editoren: Auswahl nicht abschneiden */
.sh { display:flex; align-items:center; gap:6px; padding:4px 4px 4px 10px; background:rgba(127,127,127,.08); }
.sh select { border:0; background:transparent; font-weight:500; }
.sb { padding:10px; display:flex; flex-direction:column; gap:8px; }
select, input, textarea { font:inherit; font-size:15px; color:var(--primary-text-color); background:var(--card); border:1px solid var(--div); border-radius:8px; padding:8px; width:100%; }
.sh select { width:auto; }
.inl { display:flex; gap:8px; align-items:center; } .inl > * { flex:1; } .inl span { flex:0; }
details summary { cursor:pointer; color:var(--sec); font-size:13px; margin-bottom:6px; }
details textarea, details input { margin-top:4px; font-family:monospace; font-size:13px; }
.entity, .ent { display:flex; align-items:center; gap:10px; width:100%; text-align:left; background:transparent; border:1px solid var(--div); border-radius:8px; padding:8px 10px; color:inherit; cursor:pointer; font:inherit; }
.entity span, .ent span { display:flex; flex-direction:column; min-width:0; }
.entity small, .ent small { color:var(--sec); font-size:12px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.ent { border:0; border-bottom:1px solid var(--div); border-radius:0; }
.ent:hover { background:rgba(127,127,127,.1); }
.modal { position:fixed; inset:0; background:rgba(0,0,0,.5); z-index:20; display:flex; align-items:center; justify-content:center; padding:16px; }
.dlg { background:var(--card); border-radius:14px; width:100%; max-width:520px; max-height:90vh; display:flex; flex-direction:column; padding:8px 16px 16px; }
.dlg.pick { height:80vh; }
.dh { display:flex; align-items:center; } .dh h2 { margin:8px 0; font-size:18px; font-weight:500; flex:1; }
.chips { display:flex; gap:6px; overflow-x:auto; padding:8px 0; flex-shrink:0; }
.chip { border:1px solid var(--div); background:transparent; color:inherit; border-radius:16px; padding:5px 12px; white-space:nowrap; cursor:pointer; font-size:13px; }
.chip.on { background:var(--acc); color:#fff; border-color:transparent; }
.list { overflow-y:auto; flex:1; }
.opts { overflow-y:auto; display:flex; flex-direction:column; gap:6px; }
.opt { display:flex; gap:10px; align-items:center; border:1px solid var(--div); border-radius:8px; padding:8px 10px; cursor:pointer; }
.opt select { width:auto; margin-left:auto; }
.opt input { width:18px; height:18px; flex:0 0 auto; accent-color:var(--acc); }
.opt span { display:flex; flex-direction:column; } .opt small { color:var(--sec); }
.dfoot { display:flex; justify-content:flex-end; gap:8px; padding-top:12px; }
.clip { display:flex; align-items:center; gap:10px; max-width:360px; margin:0 auto 10px; padding:8px 6px 8px 12px; border-radius:10px;
  border:1px dashed var(--acc); background:rgba(3,169,244,.08); }
.clip span { display:flex; flex-direction:column; flex:1; min-width:0; } .clip small { color:var(--sec); overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.swaps { display:flex; flex-wrap:wrap; align-items:center; gap:6px; padding:10px 0 0; font-size:13px; color:var(--sec); }
.icon.sm { padding:5px; } .icon.sm ha-icon { --mdc-icon-size:18px; } .icon:disabled { opacity:.35; cursor:default; }
.msg .btn.small { margin-left:auto; } .msg .btn.small + .icon { margin-left:0; }
@media (max-width: 870px) {
  .wide { display:none; }
  .main { grid-template-columns:1fr; padding:12px; }
  .remote-wrap { position:static; }
  .main.editing .editor { position:fixed; inset:0; z-index:10; border-radius:0; border:0; overflow-y:auto; padding-top:0; }
  .main.editing .edhead { top:0; }
  .edhead .back { display:inline-flex; }
  .main:not(.editing) .editor { display:none; }
  .dlg { max-height:100%; height:100%; border-radius:0; max-width:none; }
  .modal { padding:0; }
}
`;

export const DOCK_CSS = `
.dock { padding:12px 16px 40px; max-width:1100px; }
.dock .dkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:8px; }
.dock .dkopf h2 { margin:0; font-size:20px; }
.dock .karte { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:12px; margin:10px 0; }
.dock .zeile { display:flex; gap:10px; align-items:center; flex-wrap:wrap; }
.dock .zeile label { display:flex; flex-direction:column; gap:2px; font-size:12px; color:var(--secondary-text-color); }
.dock .zeile input, .dock .zeile select { font-size:14px; padding:6px 8px; border-radius:8px; border:1px solid var(--div); background:var(--primary-background-color); color:var(--primary-text-color); }
.dock .ents { font-size:12px; color:var(--secondary-text-color); margin-top:6px; }
.dock .ents code { font-size:12px; }
.dock table { width:100%; border-collapse:collapse; margin-top:8px; font-size:13px; }
.dock td { border-top:1px solid var(--div); padding:4px 6px; vertical-align:middle; }
.dock td.nm { white-space:nowrap; }
.dock td.nm small { display:block; color:var(--secondary-text-color); }
.dock td.cd { font-family:monospace; font-size:12px; color:var(--secondary-text-color); }
.dock td.cd input, .dock td.cd select { font-size:12px; padding:3px 4px; width:7em; }
.dock td.ak { white-space:nowrap; text-align:right; }
.dock .badge { font-size:11px; padding:1px 6px; border-radius:8px; background:var(--div); }
.dock .badge.eigen { background:var(--acc); color:#fff; }
.dock .schalter td { padding:6px; }
.dock .an { color:#22c55e; font-weight:600; }
.dock .fehlerk h3 { display:flex; align-items:center; gap:8px; }
.dock .fampel { display:flex; align-items:center; gap:8px; border-radius:10px; padding:8px 12px; margin:4px 0 8px; background:var(--flaeche, rgba(127,127,127,.12)); }
.dock .fehlerk.gruen .fampel { background:rgba(34,197,94,.15); color:#15803d; } .dock .fehlerk.gelb .fampel { background:rgba(245,158,11,.14); color:#b45309; }
.dock .fehlerk .ez { display:flex; justify-content:space-between; gap:12px; align-items:center; border-top:1px solid var(--div); padding:6px 0; }
.dock .fehlerk .ez small { display:block; } .dock .fehlerk a.btn { text-decoration:none; }
@media (max-width: 700px) { .dock td.cd { display:none; } }
`;

export const FW_CSS = `
.fw { padding:12px 16px 40px; max-width:1100px; }
.fw .karte, .dock .karte.hw { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:12px; margin:10px 0; }
.hw { display:flex; flex-direction:column; gap:4px; }
.hw .kopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
.hw .kopf b { font-size:15px; } .hw .kopf small { color:var(--secondary-text-color); }
.hw .zeile { display:flex; gap:10px; align-items:flex-end; flex-wrap:wrap; }
.hw .zeile label { display:flex; flex-direction:column; gap:2px; font-size:12px; color:var(--secondary-text-color); }
.hw select { font-size:14px; padding:6px 8px; border-radius:8px; border:1px solid var(--div); background:var(--primary-background-color); color:var(--primary-text-color); max-width:100%; }
.hw .ents { font-size:12px; color:var(--secondary-text-color); }
.badge { font-size:11px; padding:1px 6px; border-radius:8px; background:var(--div); white-space:nowrap; }
.icon.rot:hover { color:var(--fehler, #dc2626); }
.badge.ok { background:#16a34a; color:#fff; } .badge.warn { background:#d97706; color:#fff; } .badge.blau { background:#2563eb; color:#fff; } .badge.rot { background:#b91c1c; color:#fff; }
.joblog { font-family:monospace; font-size:11px; white-space:pre-wrap; max-height:240px; overflow:auto; background:var(--primary-background-color);
  border:1px solid var(--div); border-radius:8px; padding:6px 8px; margin-top:6px; }
.balken { height:6px; border-radius:3px; background:var(--div); overflow:hidden; margin-top:6px; }
.balken > i { display:block; height:100%; background:var(--acc); transition:width .4s; }
.testerg { border-collapse:collapse; font-size:13px; margin:6px 0; }
.testerg td { padding:2px 10px 2px 0; } .testerg tr.diag td { color:var(--secondary-text-color); font-size:12px; }
.testerg .badge { font-size:12px; white-space:normal; }
.hwgesamt { display:flex; align-items:center; gap:8px; padding:10px 12px; border-radius:12px; margin:6px 0 10px; background:var(--div); }
.hwgesamt.ok { background:rgba(22,163,74,.15); } .hwgesamt.rot { background:rgba(185,28,28,.15); } .hwgesamt.warn { background:rgba(217,119,6,.13); }
.hwliste { display:flex; flex-direction:column; }
.hwzeile { display:grid; grid-template-columns:auto 1fr; gap:2px 10px; align-items:baseline; padding:8px 0; border-top:1px solid var(--div); }
.hwzeile:first-child { border-top:0; } .hwzeile .badge { grid-row:span 2; align-self:start; min-width:76px; text-align:center; font-size:12px; }
.hwzeile small { color:var(--sec); line-height:1.4; grid-column:2; }
.hwknoepfe { grid-column:2; display:flex; flex-wrap:wrap; gap:6px; margin-top:4px; }
.hwtasten { grid-column:1 / -1; display:grid; grid-template-columns:repeat(auto-fill, minmax(70px, 1fr)); gap:5px; margin-top:6px; }
.hwt { display:flex; flex-direction:column; align-items:center; padding:5px 2px; border-radius:8px; border:1px solid var(--div); font-size:12px; }
.hwt small { font-size:10px; color:var(--sec); } .hwt.ok { background:#16a34a; color:#fff; border-color:transparent; } .hwt.ok small { color:#dcfce7; }
.hwt.warn { background:#d97706; color:#fff; } .hwt.rot { background:#b91c1c; color:#fff; }
.hwdiag { margin:8px 0; } .hwdiag summary { cursor:pointer; color:var(--sec); font-size:13px; }
ol.schritte { margin:6px 0 0 18px; padding:0; font-size:13px; color:var(--secondary-text-color); }
.fw h3 { margin:18px 0 4px; }
.akku .ladelimit { margin-bottom:12px; } .akku .ladelimit h3 { display:flex; align-items:center; gap:8px; margin:0 0 8px; font-size:15px; } .akku .ladelimit .lbl select { width:auto; max-width:100%; }
.fwvorher { margin-top:6px; align-items:center; gap:8px; } .fwvorher > ha-icon { --mdc-icon-size:18px; color:var(--sec); flex:none; }
.fw .msg.warn, .hw .msg.warn { margin:6px 0 0; background:rgba(217,119,6,.13); border:1px solid rgba(217,119,6,.4); font-size:13px; }
.hw .zeile input { font-size:14px; padding:6px 8px; border-radius:8px; border:1px solid var(--div); background:var(--primary-background-color); color:var(--primary-text-color); }
`;

export const EINST_CSS = `
.appwiz { max-width:520px; } .dpadwiz { max-width:600px; }
.dp_tab { display:grid; gap:6px; margin-top:6px; } .dp_tab label { display:grid; grid-template-columns:24px 120px 1fr; align-items:center; gap:8px; }
.dp_tab label span { display:flex; flex-direction:column; } .dp_tab label small { color:var(--sec); font-size:11px; } .dp_tab input { min-width:0 !important; width:100%; }
.dp_so { margin:8px 0; padding:10px 12px; border-radius:10px; border:1px solid var(--rand); background:var(--flaeche); font-size:13px; } .dp_so ol { margin:6px 0 0; padding-left:20px; } .appwiz .aw_schritte { display:flex; flex-direction:column; gap:12px; padding-left:20px; margin:8px 0; }
.appwiz .aw_schritte li > b { display:inline-block; min-width:60px; } .appwiz .aw_schritte select, .appwiz .aw_schritte input { width:auto; min-width:220px; }
.appwiz .aw_live { display:flex; gap:10px; align-items:center; margin-top:8px; padding:12px; border-radius:12px; border:1px solid var(--rand); background:var(--flaeche); }
.appwiz .aw_live span { display:flex; flex-direction:column; } .appwiz .aw_live small { color:var(--sec); }
.appwiz .aw_live.ok { border-color:#22c55e; } .appwiz .aw_live.ok ha-icon { color:#22c55e; } .appwiz .aw_live.aus ha-icon { color:var(--sec); }
.fkt .fkams { display:flex; flex-direction:column; gap:6px; margin-top:8px; }
.fkt .fkam { display:flex; align-items:center; gap:8px; flex-wrap:wrap; padding:6px 8px; border:1px solid var(--rand); border-radius:10px; }
.fkt .fkam label span { display:flex; flex-direction:column; } .fkt .fkam small { color:var(--sec); font-size:11px; }
.fkt .fkam input { flex:1 1 140px; min-width:120px; } .fkt .fkam.an { border-color:var(--acc-m); } .fkt .fkam input[type=checkbox] { flex:none; min-width:0; }
.fkt .fneu { display:flex; gap:6px; align-items:center; flex-wrap:wrap; margin-bottom:6px; } .fkt .fneu input, .fkt .fneu select { flex:1 1 130px; min-width:110px; width:auto; }
.fkt .ez .zt { display:flex; flex-direction:column; } .fkt .ez .zt small { color:var(--sec); font-size:12px; }
.fkt .ez { flex-wrap:wrap; } .fkt .ez .zt { flex:1 1 220px; min-width:0; } .fkt .ez > select, .fkt .ez > input { flex:1 1 180px; min-width:150px; }
.anz .aansicht { display:grid; grid-template-columns:minmax(220px, 340px) 1fr; gap:16px 24px; align-items:start; }
@media (max-width: 700px) { .anz .aansicht { grid-template-columns:1fr; } }
.anz .ateil { display:flex; align-items:center; gap:6px; padding:4px 0; border-bottom:1px solid var(--rand); } .anz .ateil.aus { opacity:.6; }
.anz .aopt { display:flex; flex-direction:column; gap:10px; align-items:flex-start; } .anz .aopt select { width:auto; }
.rueber { padding:16px 16px 40px; display:flex; flex-direction:column; gap:16px; } .rueber .einst { padding:0; }
.rueber .ogrid { display:grid; grid-template-columns:repeat(auto-fit, minmax(360px, 1fr)); gap:16px; align-items:start; }
.rueber .ogrid > .einst { display:flex; flex-direction:column; gap:16px; } .rueber .ez ha-icon { --mdc-icon-size:18px; color:var(--sec); }
.rueber .ez .zi { display:flex; align-items:center; gap:10px; min-width:0; } .rueber .ez .zt { display:flex; flex-direction:column; } .rueber .ez .zt small { font-size:12px; color:var(--sec); }
.rfw { padding:16px 16px 40px; }
.reiter { padding:12px 16px 0; } .reiter .seg button { display:inline-flex; align-items:center; gap:6px; }
.reiter .seg ha-icon { --mdc-icon-size:16px; }
.imdock { padding:0 16px 40px; } .imdock .einst { padding:0; }
.einst details.funkwege { margin:-4px 0 14px; border:1px solid var(--div); border-radius:12px; padding:8px 12px; background:var(--card); }
.einst details.funkwege summary { cursor:pointer; display:flex; align-items:center; gap:6px; font-weight:600; }
.einst details.funkwege td { vertical-align:top; font-size:13px; line-height:1.4; } .einst details.funkwege td small { color:var(--sec); }
.verkn .tchips { display:flex; flex-wrap:wrap; gap:6px; margin:6px 0 8px; }
.verkn .tchip { display:inline-flex; align-items:center; gap:6px; padding:4px 6px 4px 12px; border-radius:999px; border:1px solid var(--acc); background:var(--acc-s); cursor:pointer; }
.verkn .tchip span { display:flex; flex-direction:column; line-height:1.2; } .verkn .tchip small { color:var(--sec); font-size:11px; }
.verkn .tchip ha-icon { --mdc-icon-size:16px; color:var(--sec); } .verkn .tchip:hover ha-icon { color:var(--error-color,#e5484d); }
.einst details.funkwege .wtabw { overflow-x:auto; margin:8px 0 4px; }
.einst details.funkwege .wtab { width:100%; min-width:560px; border-collapse:collapse; table-layout:fixed; }
.einst details.funkwege .wtab th, .einst details.funkwege .wtab td { border:1px solid var(--div); padding:6px 8px; text-align:left; }
.einst details.funkwege .wtab tr > *:first-child { width:130px; } .einst details.funkwege .wtab tr > *:last-child { width:24%; }
.einst .emodi { display:grid; grid-template-columns:repeat(auto-fill, minmax(220px, 1fr)); gap:8px; margin:4px 0 12px; }
.einst .emodus { all:unset; cursor:pointer; border:2px solid var(--div); background:var(--card); border-radius:12px; padding:10px 12px; display:flex; flex-direction:column; gap:4px; }
.einst .emodus b { display:flex; align-items:center; gap:6px; font-size:15px; } .einst .emodus small { color:var(--sec); line-height:1.35; }
.einst .emodus.on { border-color:var(--acc); } .einst .emodus.on b::after { content:"aktiv"; margin-left:auto; font-size:11px; color:var(--acc); }
.einst { padding:8px 16px 40px; max-width:1100px; }
.verkn .karte { display:flex; flex-direction:column; gap:6px; } .einst.verkn select { max-width:100%; width:100%; }
.verkn .vtext { margin:0; color:var(--sec); font-size:13px; line-height:1.4; } .verkn .ez small.vtext { display:block; font-size:12px; }
.verkn h3 input { font:inherit; }
.akku .akhero { display:flex; flex-wrap:wrap; gap:18px; align-items:center; padding:16px 18px; border-radius:18px; background:var(--card); border:1px solid var(--div); margin-bottom:12px; }
.akku .akring { position:relative; width:120px; height:120px; flex:0 0 auto; } .akku .akring svg { width:100%; height:100%; transform:rotate(-90deg); }
.akku .akring > div { position:absolute; inset:0; display:flex; flex-direction:column; align-items:center; justify-content:center; }
.akku .akring b { font-size:34px; font-weight:700; } .akku .akring b small { font-size:16px; } .akku .akring ha-icon { color:#f59e0b; --mdc-icon-size:20px; }
.akku .akinfo { flex:1 1 220px; display:flex; flex-direction:column; gap:4px; } .akku .akinfo h2 { margin:0; font-size:20px; }
.akku .akzu { font-size:17px; font-weight:600; } .akku .akzu.lad { color:#f59e0b; } .akku .akinfo small { color:var(--sec); }
.akku .akzahlen ha-icon { color:var(--acc); --mdc-icon-size:20px; }
.akku .akgross { margin-bottom:12px; } .akku .akdia svg { width:100%; height:auto; display:block; }
.akku .akdia .aklad { fill:color-mix(in srgb, #22c55e 16%, transparent); } .akku .akdia .akr { stroke:var(--div); stroke-width:1; vector-effect:non-scaling-stroke; }
.akku .akdia .akt { fill:var(--sec); font-size:11px; } .akku .akdia .kl { display:flex; justify-content:space-between; font-size:11px; color:var(--sec); margin-top:4px; }
.akku .akdia .akli { display:inline-block; width:10px; height:10px; border-radius:3px; background:color-mix(in srgb, #22c55e 40%, transparent); margin-right:4px; vertical-align:-1px; }
.akku .akzahlen { display:grid; grid-template-columns:repeat(auto-fill, minmax(150px, 1fr)); gap:10px; margin:6px 0 12px; }
.akku .akzahlen > div { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:10px 12px; display:flex; flex-direction:column; gap:2px; }
.akku .akzahlen b { font-size:22px; } .akku .akzahlen small { color:var(--sec); }
.akku .akbalken { display:flex; height:16px; border-radius:8px; overflow:hidden; background:var(--div); }
.akku .aklegende { display:flex; flex-wrap:wrap; gap:6px 14px; margin-top:8px; font-size:13px; color:var(--sec); }
.akku .aklegende i { display:inline-block; width:10px; height:10px; border-radius:3px; margin-right:5px; vertical-align:middle; }
.akku .ez b { min-width:5.5em; text-align:right; white-space:nowrap; } .akku .ez small { color:var(--sec); white-space:nowrap; text-align:right; }
.akku .ez > span { display:flex; align-items:center; gap:6px; } .akku .ez > span ha-icon { --mdc-icon-size:18px; flex:none; }
.akku .karte + .egrid { margin-top:12px; }
.einst.jz .egrid { grid-template-columns:repeat(auto-fill, minmax(min(100%, 440px), 1fr)); }
.jz .ez:not(.quer) { flex-direction:column; align-items:stretch; gap:4px; padding:6px 0; } .jz .ez:not(.quer) select { max-width:100%; width:100%; }
.einst .egrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(320px, 1fr)); gap:12px; }
.einst .karte { border:1px solid var(--div); background:var(--card); border-radius:12px; padding:10px 12px; }
.einst h3 { display:flex; align-items:center; gap:6px; margin:2px 0 8px; font-size:15px; }
.einst .ez { display:flex; align-items:center; gap:8px; min-height:34px; border-top:1px solid var(--div); padding:3px 0; }
.einst .ez:first-of-type { border-top:0; }
.einst .ez > span { flex:1; } .einst .ez.offen > span::after { content:" ⏳"; }
.einst .ez.anders > span { color:var(--acc); }
.einst select, .einst input[type=number] { max-width:52%; }
.einst input[type=number] { width:5.5em; }
.einst .estat { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin:6px 0 10px; }
.einst .ehilfe { margin:-2px 0 6px; font-size:12px; line-height:1.4; color:var(--sec); }
.ehilfe[data-hk]:not(.auf), .eintro[data-hk]:not(.auf) { display:none; }
.ehilfe[data-hk], .eintro[data-hk] { background:var(--secondary-background-color, rgba(127,127,127,.08)); border-radius:6px; padding:6px 8px; }
.ehbtn { background:none; border:0; padding:0 0 0 4px; margin:0; cursor:pointer; color:var(--sec); vertical-align:-2px; line-height:1; }
.ehbtn ha-icon { --mdc-icon-size:16px; }
.ehbtn.auf, .ehbtn:hover { color:var(--acc); }
h3 .ehbtn ha-icon { --mdc-icon-size:18px; }
.einst .ehilfe.tvwahl { color:var(--primary-text-color); }
.einst .ehilfe.tvwahl ha-icon { --mdc-icon-size:16px; color:var(--sec); margin-right:4px; vertical-align:-3px; }
.einst .eintro { margin:0 0 8px; font-size:12.5px; line-height:1.5; color:var(--sec); }
.einst .eintro .wtabw { overflow-x:auto; margin:6px 0; }
.einst .eintro .wtab { width:100%; border-collapse:collapse; font-size:11.5px; table-layout:fixed; }
.einst .eintro .wtab th, .einst .eintro .wtab td { border:1px solid var(--div); padding:3px 5px; text-align:left; vertical-align:top; }
.einst .eintro .wtab th { color:var(--fg); font-weight:600; }
.einst .eintro .wtab td:first-child { font-weight:600; width:22%; }
details.dscan { margin-top:14px; } details.dscan > summary { cursor:pointer; list-style:none; display:flex; align-items:center; gap:10px; }
details.dscan > summary::-webkit-details-marker { display:none; } details.dscan > summary h2 { margin:0; font-size:17px; display:flex; align-items:center; gap:8px; }
details.dscan > summary small { color:var(--sec); } details.dscan > summary::after { content:"›"; margin-left:auto; font-size:20px; color:var(--sec); transition:transform .15s; }
details.dscan[open] > summary::after { transform:rotate(90deg); }
.einst details.efein { margin-top:6px; border-top:1px solid var(--div); padding-top:4px; }
.einst details.efein > summary { cursor:pointer; display:flex; align-items:center; gap:6px; color:var(--sec); font-size:13px; padding:4px 0; list-style:none; }
.einst details.efein > summary::-webkit-details-marker { display:none; }
.einst details.efein > summary::after { content:"›"; margin-left:auto; transition:transform .15s; }
.einst details.efein[open] > summary::after { transform:rotate(90deg); }
.einst details.efein > summary ha-icon { --mdc-icon-size:16px; }
`;

export const LICHT_CSS = `
.licht { margin-bottom:16px; }
.licht .lkopf { display:flex; align-items:center; gap:10px; flex-wrap:wrap; }
.licht .lkopf h3 { margin:0; flex:1 1 auto; }
.licht .lsensor { display:flex; gap:8px; align-items:center; flex-wrap:wrap; margin:10px 0 4px; font-size:13px; color:var(--sec); }
.licht .lsensor select { width:auto; flex:1 1 220px; max-width:420px; }
.licht .lux { font-size:22px; font-weight:600; color:var(--primary-text-color); }
.licht svg { width:100%; height:auto; display:block; margin:6px 0; touch-action:none; user-select:none; }
.licht svg .gitter { stroke:var(--rand); stroke-width:1; }
.licht svg text { fill:var(--sec); font-size:11px; }
.licht svg .jetzt { stroke:var(--primary-text-color); stroke-dasharray:4 4; opacity:.5; }
.licht svg path { fill:none; stroke-width:2.5; }
.licht svg path.aus { stroke-dasharray:3 5; opacity:.35; }
.licht svg path.neben { opacity:.35; }
.licht svg .kp { cursor:grab; stroke:var(--card); stroke-width:2; }
.licht svg .kp:hover, .licht svg .kp.zieh { r:9; }
.licht .lziele { display:flex; flex-direction:column; gap:6px; }
.licht .lz { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:6px 10px; border-radius:12px; border:1px solid var(--rand); cursor:pointer; }
.licht .lz.sel { border-color:var(--acc); background:var(--acc-s); }
.licht .lz .farbe { width:14px; height:14px; border-radius:50%; flex:0 0 auto; }
.licht .lz b { font-weight:600; min-width:150px; }
.licht .lz .jetztw { color:var(--sec); font-size:13px; min-width:70px; }
.licht .lz .pkte { display:flex; gap:4px; flex-wrap:wrap; flex:1 1 auto; }
.licht .lz .pk { display:inline-flex; align-items:center; gap:2px; font-size:12px; padding:2px 4px 2px 8px; border-radius:999px; background:var(--flaeche); border:1px solid var(--rand); }
.licht .lz .pk button { border:0; background:none; color:var(--sec); cursor:pointer; padding:0 2px; font-size:14px; line-height:1; }
.licht .lfuss { display:flex; gap:10px 16px; align-items:center; flex-wrap:wrap; margin-top:10px; }
.ledtab { width:100%; border-collapse:collapse; font-size:14px; }
.ledkarte .ez > span:first-child { display:flex; flex-direction:column; } .ledkarte .ez .vtext { color:var(--sec); font-size:12px; }
.ledtab td { padding:8px 6px; border-top:1px solid var(--rand); vertical-align:middle; }
.ledtab tr:first-child td { border-top:0; }
.ledtab td.n b { display:block; } .ledtab td.n small { color:var(--sec); }
.ledtab input[type=range] { width:140px; accent-color:var(--acc); vertical-align:middle; padding:0; border:0; }
.ledtab td.ls select { width:auto; max-width:220px; } .ledtab td.ls small { display:block; color:var(--sec); font-size:12px; }
.ledtab .wert { display:inline-block; min-width:3.2em; text-align:right; font-variant-numeric:tabular-nums; }
.einst .ez .auto { font-size:11px; font-weight:600; color:var(--ok); margin-left:6px; }
`;

export const GESTEN_CSS = `
.gesten .lernbox { padding:12px 14px; border-radius:12px; background:var(--acc-s); border:1px solid var(--acc-m); margin:8px 0; display:flex; flex-direction:column; gap:8px; }
.gesten .lernbox .punkte { display:flex; gap:10px; } .gesten .lernbox .punkte i { width:14px; height:14px; border-radius:50%; background:var(--rand); display:inline-block; }
.gesten .lernbox .punkte i.ok { background:#22c55e; }
.gesten .lernbox .zeile { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }
.gesten { display:grid; grid-template-columns:minmax(260px,340px) 1fr; gap:16px; padding:12px 16px 40px; align-items:start; }
.gesten .gliste { display:flex; flex-direction:column; gap:8px; }
.gesten .gk { display:flex; align-items:center; gap:10px; padding:10px 12px; border:1px solid var(--rand); border-radius:12px; background:var(--card); cursor:pointer; }
.gesten .gk.sel { border-color:var(--acc); background:var(--acc-s); }
.gesten .gk > ha-icon { color:var(--acc); }
.gesten .gk span { flex:1; display:flex; flex-direction:column; min-width:0; } .gesten .gk small { color:var(--sec); font-size:12px; }
.gesten .gk.aus { opacity:.6; }
.gesten .gkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; }
.gesten .vorlagen { display:flex; gap:8px; flex-wrap:wrap; margin:8px 0; }
.gesten .vl { position:relative; border:1px solid var(--rand); border-radius:10px; background:var(--flaeche); padding:4px; }
.gesten .vl svg { width:120px; height:56px; display:block; }
.gesten .vl button { position:absolute; top:2px; right:2px; padding:2px; }
.gesten .log { font-size:13px; display:flex; flex-direction:column; gap:4px; max-height:160px; overflow:auto; }
.gesten .log div { display:flex; gap:8px; } .gesten .log small { color:var(--sec); }
.gesten .makro { display:flex; gap:6px; align-items:center; margin-bottom:6px; }
.gesten .makro input { flex:1; min-width:0; } .gesten .makro select { width:auto; }
.gesten .makro input.n { flex:0 0 110px; }
@media (max-width: 870px) { .gesten { grid-template-columns:1fr; padding:12px; } }
.anz .azeile { display:flex; gap:10px; flex-wrap:wrap; align-items:center; margin:6px 0; }
.anz .azeile label { display:flex; flex-direction:column; gap:3px; font-size:12px; color:var(--sec); flex:1 1 200px; }
.anz .kal { display:flex; flex-wrap:wrap; gap:4px 14px; margin:6px 0; }
.anz .kal label { display:inline-flex; gap:6px; align-items:center; font-size:13px; cursor:pointer; } .anz .kal input { width:auto; flex:0 0 auto; }
.anz .vorschau { display:flex; gap:16px; flex-wrap:wrap; align-items:flex-start; }
.anz .mini { width:200px; border-radius:20px; background:#0e1116; color:#f1f3f5; padding:12px 12px 10px; box-shadow:inset 0 0 0 2px #23272e; font-size:12px; }
.anz .mini .u { font-size:34px; text-align:center; font-weight:600; letter-spacing:1px; }
.anz .mini .d { text-align:center; color:#8b94a3; }
.anz .mini .w { display:flex; gap:8px; align-items:center; margin-top:8px; } .anz .mini .w ha-icon { --mdc-icon-size:30px; color:#facc15; }
.anz .mini .k { background:#1a1f27; border-radius:8px; padding:6px 8px; margin-top:8px; display:flex; flex-direction:column; gap:4px; }
.anz .mini .k b { color:#3b82f6; font-weight:500; }
`;

export const SHELL_CSS = `
:host { overflow-anchor:none; }   /* 2026-11-01: Neuzeichnen beim Ziehen liess HA die Seite Stueck fuer Stueck nach unten rollen */
:host { --side:256px; --kopf:64px; --side-bg:color-mix(in srgb, var(--card) 55%, var(--primary-background-color)); --blau:#3b82f6; --gelb:#f59e0b; }
button, input, select, textarea { font-family:inherit; }
.laden { min-height:60vh; display:flex; align-items:center; justify-content:center; gap:16px; padding:24px; }
.laden .logo, .brand .logo { width:40px; height:40px; border-radius:12px; display:inline-flex; align-items:center; justify-content:center; color:#fff; flex:0 0 auto;
  background:linear-gradient(135deg, var(--acc), color-mix(in srgb, var(--acc) 45%, #8b5cf6)); box-shadow:0 6px 18px var(--acc-m); }
.huelle { container-type:inline-size; container-name:huelle; min-height:100vh; }
.app { display:grid; grid-template-columns:var(--side) minmax(0,1fr); min-height:100vh; }

/* Seitenleiste */
.side { position:sticky; top:0; height:100vh; overflow-y:auto; overscroll-behavior:contain; display:flex; flex-direction:column; gap:2px;
  padding:16px 12px 14px; background:var(--side-bg); border-right:1px solid var(--rand); scrollbar-width:thin; z-index:8; }
.brand { display:flex; align-items:center; gap:11px; padding:2px 6px 16px; }
.brand .bt { display:flex; flex-direction:column; line-height:1.2; min-width:0; } .brand b { font-size:15px; } .brand small { font-size:10.5px; color:var(--sec); letter-spacing:.08em; text-transform:uppercase; }
.navzu { display:none; margin-left:auto; }
.suche { display:flex; align-items:center; gap:8px; border:1px solid var(--rand); background:var(--card); color:var(--sec); border-radius:12px; padding:8px 10px; cursor:pointer; font-size:13px; }
.suche:hover { border-color:var(--acc); color:var(--primary-text-color); }
.suche > span { flex:1; text-align:left; white-space:nowrap; } .suche ha-icon { --mdc-icon-size:18px; }
kbd { font-family:inherit; font-size:10.5px; line-height:1.4; padding:1px 6px; border-radius:6px; border:1px solid var(--rand); border-bottom-width:2px; background:var(--flaeche); color:var(--sec); white-space:nowrap; }
.suche.nav { margin:0 0 10px; }
.nsec { font-size:10.5px; font-weight:600; letter-spacing:.1em; text-transform:uppercase; color:var(--sec); padding:18px 10px 6px; }
.ni { display:flex; align-items:center; gap:12px; width:100%; border:0; background:none; color:var(--primary-text-color); padding:9px 10px; border-radius:11px; cursor:pointer; font-size:14px; text-align:left; position:relative; }
.ni > ha-icon { color:var(--sec); --mdc-icon-size:20px; flex:0 0 auto; }
.ni:hover { background:color-mix(in srgb, var(--primary-text-color) 6%, transparent); }
.ni.on { background:var(--acc-s); color:var(--acc); font-weight:600; } .ni.on > ha-icon { color:var(--acc); }
.ni > span { flex:1; min-width:0; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.zahl { font-style:normal; font-size:11px; font-weight:700; min-width:20px; height:20px; padding:0 6px; border-radius:10px; display:inline-flex; align-items:center; justify-content:center; background:var(--gelb); color:#1a1a1a; }
.punkt { width:8px; height:8px; border-radius:50%; background:var(--acc); flex:0 0 auto; display:inline-block; } .punkt.warn { background:var(--gelb); }
.rgruppe { border-radius:16px; margin:1px 0; transition:background-color .2s; } .rgruppe.auf { background:color-mix(in srgb, var(--primary-text-color) 4.5%, transparent); padding-bottom:6px; }
.rk { display:flex; align-items:center; gap:11px; width:100%; border:0; background:none; color:inherit; padding:8px; border-radius:14px; cursor:pointer; text-align:left; }
.rk:hover { background:color-mix(in srgb, var(--primary-text-color) 6%, transparent); } .rgruppe.auf .rk:hover { background:none; }
.ava { position:relative; width:40px; height:40px; flex:0 0 40px; display:grid; place-items:center; }
.ava svg { position:absolute; inset:0; width:100%; height:100%; transform:rotate(-90deg); }
.ava b { font-size:14px; } .ava svg.mini { position:relative; z-index:1; transform:none; inset:auto; width:12px !important; height:22px !important; grid-area:1 / 1; } .rkopf h2 svg.mini { vertical-align:-5px; margin-right:8px; } .ava::after { content:""; position:absolute; right:-1px; bottom:-1px; width:12px; height:12px; border-radius:50%; border:2.5px solid var(--side-bg); background:#6b7280; }
.ava.z-wlan::after { background:var(--ok); } .ava.z-ble::after { background:var(--blau); } .ava.z-dock::after { background:#a855f7; }   /* z-: "dock" allein traf .dock der Dock-Seite (Innenabstand) */
svg .rg { fill:none; stroke:color-mix(in srgb, var(--primary-text-color) 10%, transparent); } svg .rv { fill:none; stroke-linecap:round; transition:stroke-dasharray .6s ease; }
.rkt { display:flex; flex-direction:column; min-width:0; flex:1; line-height:1.3; } .rkt b { font-size:14px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rkt small { font-size:11.5px; color:var(--sec); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.rk.on .rkt b { color:var(--acc); }
.subnav { display:flex; flex-direction:column; gap:1px; padding:0 6px 0 10px; }
.subnav .ni { padding:7px 10px 7px 12px; font-size:13.5px; gap:11px; } .subnav .ni > ha-icon { --mdc-icon-size:18px; }
.nsprache { margin-top:auto; display:flex; align-items:center; gap:6px; padding:16px 10px 0; color:var(--sec); --mdc-icon-size:16px; }
.nsprache select { flex:1; min-width:0; font-size:12px; padding:4px 6px; border-radius:8px; border:1px solid var(--div); background:transparent; color:inherit; }
.nsprache + .nfuss { margin-top:0; }
.nfuss { margin-top:auto; padding:18px 10px 0; font-size:10.5px; color:var(--sec); display:flex; flex-direction:column; gap:2px; opacity:.8; }

/* Kopf */
.haupt { min-width:0; display:flex; flex-direction:column; }
.seitenkopf { position:sticky; top:0; z-index:7; height:var(--kopf); display:flex; align-items:center; gap:12px; padding:0 24px;
  background:color-mix(in srgb, var(--primary-background-color) 80%, transparent); backdrop-filter:saturate(1.6) blur(16px); -webkit-backdrop-filter:saturate(1.6) blur(16px); border-bottom:1px solid var(--rand); }
.ktitel { display:flex; align-items:center; gap:12px; min-width:0; border:0; background:none; color:inherit; padding:0; text-align:left; cursor:default; }
.kico { width:38px; height:38px; flex:0 0 38px; border-radius:11px; background:var(--acc-s); color:var(--acc); display:inline-flex; align-items:center; justify-content:center; }
.kt { display:flex; flex-direction:column; min-width:0; line-height:1.25; } .kt b { font-size:18px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.kt small { font-size:12px; color:var(--sec); display:flex; align-items:center; gap:2px; } .kt small ha-icon { --mdc-icon-size:14px; display:none; }
.seitenkopf .suche { min-width:230px; }
.hamenu { color:var(--sec); }
.syncw { position:relative; }
.pill { display:inline-flex; align-items:center; gap:8px; border:1px solid var(--rand); background:var(--card); color:var(--primary-text-color); border-radius:999px; padding:5px 13px 5px 5px; font-size:13px; cursor:pointer; white-space:nowrap; }
.pill:hover, .pill.auf { border-color:color-mix(in srgb, var(--primary-text-color) 28%, transparent); }
.mlliste { display:flex; flex-direction:column; gap:4px; margin:4px 0; } .mlz { display:flex; align-items:center; gap:8px; } .mlz > span { flex:1; display:flex; flex-direction:column; min-width:0; } .mlz small { opacity:.6; font-size:11px; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }
.hlive { display:flex; flex-direction:column; align-items:center; gap:6px; margin:6px auto 12px; width:310px; flex:none; text-align:center; }
.hdisp { zoom:1.25; position:relative; width:240px; height:320px; background:#000; color:#f1f3f5; border-radius:4px; overflow:hidden; box-shadow:0 0 0 5px #2b2f36, 0 6px 18px rgba(0,0,0,.35); }
.hdisp .rsbar { position:absolute; top:0; left:0; right:0; } .hdisp .rsbar[hidden] { display:none; }
.hfen { position:absolute; left:0; width:240px; overflow-y:auto; overflow-x:hidden; scrollbar-width:none; } .hfen::-webkit-scrollbar { display:none; }
.hfen .pgrid { zoom:1; margin-top:-4px; } .hfen .pscr, .hfen .pdunkel { display:none; }   /* Raster wie auf der Remote: Zeile 1 bei 2 px (Editor-Rand 6) */
.hlb { position:absolute; border-radius:10px; overflow:hidden; } .hlb.kn { background:#1a1f27; display:flex; align-items:center; justify-content:center; font-size:15px; } .hlb.kn b { font-weight:400; }
.hlb.med .mw { inset:0; }
.hseinst { display:flex; flex-direction:column; gap:10px; } .hseinst h3 { display:flex; align-items:center; gap:6px; margin:0; font-size:15px; }
.rshome .pgrid { margin-top:-4px; }
.klblock { border:1px solid var(--div); border-radius:10px; padding:8px; margin:8px 0; } .klfeld { display:flex; flex-direction:column; gap:6px; margin-top:6px; }
.fwliste { display:flex; flex-direction:column; gap:4px; margin-top:6px; }
.fwz { display:flex; flex-wrap:wrap; align-items:center; gap:8px; padding:6px 10px; border-radius:10px; background:var(--flaeche); cursor:pointer; border:1px solid transparent; }
.fwz:hover { border-color:var(--divider-color); } .fwz.gewaehlt { border-color:var(--primary-color); }
.fwz .fwsym { --mdc-icon-size:20px; opacity:.8; } .fwz.laeuft .fwsym { color:var(--primary-color); } .fwz.fertig .fwsym { color:var(--success-color, #4caf50); }
.fwz.err .fwsym { color:var(--error-color, #db4437); } .fwz.fertig { opacity:.85; }
.fwz .fwt { flex:1; min-width:160px; display:flex; flex-direction:column; } .fwz .fwt small { opacity:.7; }
.fwz .fwart { display:inline-flex; align-items:center; gap:4px; font-size:12px; padding:2px 8px; border-radius:999px; background:var(--card-background-color); --mdc-icon-size:15px; white-space:nowrap; }
.fwz .balken { flex-basis:100%; }
.fwtrenn { font-size:12px; opacity:.6; margin:6px 2px 0; }
.fwlogkopf { margin:10px 2px 4px; font-size:13px; }
.fwbadge { display:inline-flex; align-items:center; gap:4px; margin-top:6px; font:inherit; font-size:12px; padding:3px 10px; border-radius:999px; border:1px solid var(--divider-color); background:var(--flaeche); color:inherit; cursor:pointer; --mdc-icon-size:15px; }
.fwbadge.laeuft { border-color:var(--primary-color); color:var(--primary-color); }
.fwschlange { margin-top:8px; display:flex; flex-direction:column; gap:4px; } .fwsz { display:flex; align-items:center; gap:8px; padding:4px 8px; border-radius:8px; background:var(--flaeche); } .fwsz span { flex:1; } .fwsz small { opacity:.6; }
.mw { position:absolute; inset:2px; display:flex; gap:8px; overflow:hidden; text-align:left; border-radius:8px; }
.pi .mw b, .mw b { font-size:15px; font-weight:400; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; display:block; } .pi .mw small, .mw small { font-size:11px; opacity:.7; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; display:block; min-height:13px; }
.mw .mk { font-style:normal; width:26px; height:26px; border-radius:50%; background:#232a35; display:inline-flex; align-items:center; justify-content:center; flex:none; } .mw .mk.h { background:#3b82f6; width:30px; height:30px; } .mw .mk ha-icon { --mdc-icon-size:16px; }
.mw .mkr { display:flex; justify-content:space-evenly; align-items:center; margin-top:auto; padding-bottom:4px; }
.mw .mbar { display:block; height:3px; background:#4b5563; border-radius:2px; margin:4px 0; } .mw .mbar span { display:block; height:100%; background:#ef4444; border-radius:2px; }
.mw.gross .mcov { width:auto; height:100%; aspect-ratio:1; border-radius:8px; background:#0e1116 center/cover; display:flex; align-items:center; justify-content:center; flex:none; }
.mw.gross .mwr, .mw.schmal .mwr { flex:1; min-width:0; display:flex; flex-direction:column; } .mw.schmal { align-items:center; }
.mw.karte { inset:0; background:#0e1116 center/cover; flex-direction:column; justify-content:flex-end; } .mw.karte .mn { position:absolute; left:50%; top:35%; transform:translate(-50%,-50%); --mdc-icon-size:28px; opacity:.4; }
.mw.karte .mwu { display:flex; flex-direction:column; padding:28px 8px 2px; background:linear-gradient(transparent, rgba(0,0,0,.8) 45%); color:#f1f3f5; } .mw.karte .mkr { margin-top:6px; }
.mw.karte .mbar { margin:0; border-radius:0; height:3px; } .mw.karte .mbar span { border-radius:0; }
.wlicht { position:absolute; inset:2px 4px; display:flex; flex-direction:column; gap:0; overflow:hidden; text-align:left; }
.wlicht .lkopf { display:flex; align-items:center; gap:8px; height:34px; flex:none; }
.wlicht .lpw { width:28px; height:28px; border-radius:50%; background:#2e3643; display:inline-flex; align-items:center; justify-content:center; flex:none; } .wlicht .lpw ha-icon { --mdc-icon-size:17px; }
.wlicht .lnm { display:flex; flex-direction:column; min-width:0; flex:1; line-height:1.15; } .wlicht .lnm b { white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .wlicht .lnm small { font-size:10px; opacity:.6; }
.wlicht .lpct { font-size:13px; } .wlicht .lzi { --mdc-icon-size:16px; opacity:.6; }
.wlicht .lchips { display:flex; gap:4px; height:30px; align-items:center; overflow:hidden; flex:none; } .wlicht .lchips i { font-style:normal; font-size:10px; padding:4px 9px; border-radius:11px; background:#2e3643; white-space:nowrap; opacity:.6; } .wlicht .lchips i.an { opacity:1; } .wlicht .lchips i.on { background:#3b82f6; opacity:1; }
.wlicht .lreg { display:flex; align-items:center; gap:8px; height:28px; flex:none; } .wlicht .lreg ha-icon { --mdc-icon-size:15px; opacity:.6; }
.wlicht .lreg > span { position:relative; flex:1; height:8px; border-radius:4px; background:#4b5563; margin-right:6px; } .wlicht .lreg b { position:absolute; inset:0 auto 0 0; border-radius:4px; background:#ffd58a; }
.wlicht .lreg u { position:absolute; top:50%; width:12px; height:12px; margin:-6px 0 0 -6px; border-radius:50%; background:#f1f3f5; border:2px solid #111; }
.wlicht .lreg.temp > span { background:linear-gradient(90deg,#ff9f45,#dde8ff); } .wlicht .lreg.farbe > span { background:linear-gradient(90deg,red,#ff0,lime,cyan,blue,#f0f,red); } .wlicht .lreg.temp b, .wlicht .lreg.farbe b { display:none; }
.wlicht .lpal { display:flex; justify-content:space-evenly; align-items:center; height:28px; flex:none; } .wlicht .lpal i { width:16px; height:16px; border-radius:50%; border:2px solid #111; }
.wlicht .lsz { display:flex; flex-wrap:wrap; gap:4px; } .wlicht .lsz i { font-style:normal; font-size:10px; padding:5px 9px; border-radius:7px; background:#2e3643; }
.linkbtn { background:none; border:0; padding:0; color:var(--acc,#03a9f4); cursor:pointer; font:inherit; text-decoration:underline; }
[data-od].drag, [data-odk].drag { opacity:.4; } [data-odk].drop, [data-od].drop { outline:2px dashed var(--acc,#03a9f4); outline-offset:2px; border-radius:12px; }
.wlanw { position:relative; display:inline-flex; } .pill .pfeil { --mdc-icon-size:16px; color:var(--sec); margin-left:-4px; }
.wlanmenu { position:absolute; top:calc(100% + 6px); right:0; z-index:20; min-width:190px; display:flex; flex-direction:column; padding:6px; border-radius:12px; background:var(--card); border:1px solid var(--rand); box-shadow:0 8px 24px rgba(0,0,0,.3); }
.wlanmenu small { color:var(--sec); padding:4px 8px; } .wlanmenu hr { border:0; border-top:1px solid var(--rand); margin:4px 0; width:100%; }
.wlanmenu button { text-align:left; background:none; border:0; color:inherit; padding:8px; border-radius:8px; cursor:pointer; font:inherit; } .wlanmenu button:hover { background:var(--flaeche); }
.karte.hw .kopf .wlanw { margin-left:10px; } .karte.hw .kopf .wlanmenu { left:0; right:auto; }
.pill .pd { width:26px; height:26px; border-radius:50%; display:inline-flex; align-items:center; justify-content:center; background:var(--flaeche); color:var(--sec); } .pill .pd ha-icon { --mdc-icon-size:16px; }
.pill.ok .pd { background:color-mix(in srgb, var(--ok) 18%, transparent); color:var(--ok); }
.pill.warn .pd, .pill.wait .pd { background:color-mix(in srgb, var(--gelb) 20%, transparent); color:var(--gelb); }
.pill.err .pd { background:color-mix(in srgb, var(--fehler) 18%, transparent); color:var(--fehler); }
.pill.busy .pd { background:var(--acc-s); color:var(--acc); animation:puls 1.4s ease-in-out infinite; }
@keyframes puls { 50% { box-shadow:0 0 0 5px var(--acc-s); } }
.syncpop { position:absolute; right:0; top:calc(100% + 10px); width:min(440px, calc(100vw - 24px)); z-index:30; background:var(--card); border:1px solid var(--rand);
  border-radius:18px; padding:16px; box-shadow:0 22px 60px rgba(0,0,0,.32); display:flex; flex-direction:column; gap:12px; animation:hoch .14s ease-out; }
.groesse { display:flex; flex-direction:column; gap:6px; font-size:12.5px; color:var(--sec); }
.groesse > span { display:flex; justify-content:space-between; gap:8px; } .groesse b { color:var(--primary-text-color); font-weight:600; }
.groesse.warn .xbar span { background:var(--gelb); } .groesse.err .xbar span { background:var(--fehler); } .groesse.warn b { color:var(--gelb); } .groesse.err b { color:var(--fehler); }
.zahlen span.voll { border-color:var(--gelb); color:var(--gelb); } .zahlen span.voll b { color:var(--gelb); }
.syncpop .spt { font-size:14px; } .syncpop .xfer { box-shadow:none; } .syncpop .note { margin:0; line-height:1.5; }

/* Inhalt */
.inhalt { min-width:0; flex:1; padding-bottom:28px; }
/* Leiste "Was gerade laeuft" (2026-10-23) */
.aktivl { display:flex; flex-direction:column; gap:6px; padding:10px 24px 0; }
.aktivl .al { display:flex; align-items:center; gap:10px; padding:7px 12px; border-radius:12px; background:var(--card); border:1px solid var(--rand); font-size:13px; min-width:0; }
.aktivl .al > ha-icon { --mdc-icon-size:18px; color:var(--sec); flex:none; }
.aktivl .alt { display:flex; flex-direction:column; min-width:0; flex:1; } .aktivl .alt b { font-weight:500; }
.aktivl .alt small { color:var(--sec); font-size:12px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.aktivl .alb { flex:0 1 180px; height:6px; border-radius:3px; background:var(--flaeche); overflow:hidden; } .aktivl .alb i { display:block; height:100%; background:var(--acc); transition:width .4s; }
.aktivl .alb.busy i { width:30% !important; animation:xpulse 1.2s ease-in-out infinite; }
.aktivl .alp { font-size:12px; color:var(--sec); min-width:34px; text-align:right; font-variant-numeric:tabular-nums; }
.aktivl .al.busy > ha-icon { color:var(--acc); animation:puls 1.4s ease-in-out infinite; }
.aktivl .al.ok { border-color:color-mix(in srgb, var(--ok) 40%, transparent); } .aktivl .al.ok > ha-icon { color:var(--ok); }
.aktivl .al.wait > ha-icon, .aktivl .al.warn > ha-icon { color:var(--gelb); }
.aktivl .al.err { border-color:color-mix(in srgb, var(--fehler) 45%, transparent); } .aktivl .al.err > ha-icon { color:var(--fehler); }
@media (max-width:700px) { .aktivl { padding:10px 12px 0; } .aktivl .alb { flex-basis:70px; } .aktivl .alt small { white-space:normal; } }
/* 2026-11-10 Handy: ruhende Zeilen ("wartet", "aktiv", "unterwegs") einzeilig - zwei Remotes fraßen sonst ein Drittel des Bildschirms.
   Die Erklärung steht weiter im title; Fehler und Laufendes behalten ihren Text. */
@media (max-width:700px) { .aktivl .al:is(.wait, .ok) .alt small { display:none; } .aktivl .al:is(.wait, .ok) { padding:5px 10px; } }
.sbw { position:sticky; bottom:18px; z-index:15; display:flex; justify-content:center; pointer-events:none; padding:0 12px; }
.sbw .savebar { position:relative; left:auto; bottom:auto; transform:none; pointer-events:auto; animation:hoch .2s ease-out; } .sbw .savebar.ruhig { animation:none; }
.savebar-platz { display:none; }
.lade { display:flex; align-items:center; gap:10px; color:var(--sec); padding:40px; justify-content:center; }
.blink { animation:blink 2.2s ease-out; border-radius:10px; }
@keyframes blink { 0%, 30% { box-shadow:0 0 0 3px var(--acc), 0 0 0 9px var(--acc-s); background:var(--acc-s); } 100% { box-shadow:0 0 0 0 transparent; } }
.main, .mmain { padding:20px 24px 28px; } .mbar { padding:18px 24px 0; } .gesten { padding:20px 24px 40px; }
.einst, .dock, .fw { max-width:1240px; padding:20px 24px 40px; }
@container huelle (min-width: 761px) { .main { grid-template-columns:minmax(300px,380px) minmax(0,1fr); gap:24px; } }
.remote-wrap { top:calc(var(--kopf) + 16px); }
.edhead { top:var(--kopf); flex-wrap:wrap; row-gap:6px; } .edhead > div:nth-child(2) { flex:1 1 200px; min-width:0; }
.main .editor { min-height:520px; }

/* Menue-Editor */
@container huelle (min-width: 1150px) { .mmain { grid-template-columns:250px 360px minmax(0,1fr); } }
@container huelle (min-width: 761px) and (max-width: 1149px) { .mmain { grid-template-columns:240px minmax(0,1fr); } .mmain .editor { grid-column:1 / -1; } }
@container huelle (max-width: 760px) { .mmain, .main { grid-template-columns:minmax(0,1fr); } }
.phd .inl:has(> .inl2) { flex-direction:column; align-items:stretch; gap:8px; }
.phd .inl2 { display:flex; flex-direction:column; align-items:stretch; gap:3px; font-size:12px; color:var(--sec); }
.phd .inl2 select { width:100%; }
.mprev { min-width:0; }
.note.warnz { color:var(--gelb); }
.karte.entw { gap:6px; } .karte.entw .sw span b { font-weight:600; }
.mgrp { border:1px solid var(--rand, var(--div)); border-radius:14px; margin:10px 0; padding:0 12px; background:var(--card); }
.mgrp > summary { list-style:none; cursor:pointer; display:flex; align-items:center; gap:8px; padding:10px 0; font-size:15px; user-select:none; }
.mgrp > summary::-webkit-details-marker { display:none; }
.mgrp > summary > ha-icon { color:var(--acc); background:var(--acc-s); border-radius:8px; padding:4px; --mdc-icon-size:18px; }
.mgrp > summary > b { font-weight:500; }
.mgrp > summary > small { margin-left:auto; color:var(--sec); font-size:12px; text-align:right; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; max-width:55%; }
.mgrp > summary::after { content:"›"; color:var(--sec); font-size:20px; line-height:1; transition:transform .15s; margin-left:4px; }
.mgrp[open] > summary::after { transform:rotate(90deg); }
.mgrp[open] > summary { border-bottom:1px solid var(--div); margin-bottom:10px; }
.mgrpi { padding:0 0 12px; display:flex; flex-direction:column; gap:8px; }
.mgrpi > label, .mgrpi > .inl { margin:0; }
.grpbar { display:flex; align-items:center; gap:6px; flex-wrap:wrap; justify-content:flex-end; }
.form.mgrpform { display:block; padding:0; }
.gest .inl:has(.seg) { flex-wrap:wrap; } .gest .inl:has(.seg) > label { flex:1 1 auto; }   /* Ausrichtung/Groesse liefen rechts aus dem Editor */
/* Einstellungen: Hinweis links statt zentriert */
.einst > .hint { text-align:left; max-width:none; margin:0 0 4px; }

/* Aktivitaeten-Leiste */
.aktleiste { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:18px 24px 0; }
.akts { display:flex; gap:6px; flex-wrap:wrap; align-items:center; }
.aktk { display:flex; align-items:center; gap:9px; border:1px solid var(--rand); background:var(--card); color:inherit; border-radius:14px; padding:5px 14px 5px 5px; cursor:pointer; text-align:left; line-height:1.2; }
.aktk:hover { border-color:color-mix(in srgb, var(--primary-text-color) 26%, transparent); }
.ai { width:32px; height:32px; border-radius:10px; display:inline-flex; align-items:center; justify-content:center; background:var(--flaeche); color:var(--sec); flex:0 0 auto; } .ai ha-icon { --mdc-icon-size:18px; }
.aktk > span:last-child { display:flex; flex-direction:column; } .aktk b { font-size:14px; font-weight:600; } .aktk small { font-size:11px; color:var(--sec); white-space:nowrap; max-width:130px; overflow:hidden; text-overflow:ellipsis; }
.aktk.on { border-color:var(--acc); background:var(--acc-s); box-shadow:0 4px 14px var(--acc-s); } .aktk.on .ai { background:var(--acc); color:#fff; }
.aktk.nodock { opacity:.62; } .aktk.nodock b { font-style:italic; }
.aktk.ohne { border-style:dotted; }
.aktk.plus { padding:0; width:44px; height:44px; justify-content:center; border-style:dashed; color:var(--sec); background:transparent; }
.akts.modi .aktk { border-style:dashed; } .akts.modi .aktk.on { border-style:solid; } .akts.modi .ai { width:28px; height:28px; }
.trenner { width:1px; align-self:stretch; min-height:32px; background:var(--rand); margin:0 2px; }
.seg.ansicht button { display:inline-flex; align-items:center; gap:6px; } .seg.ansicht ha-icon { --mdc-icon-size:17px; }
.aktset { padding:12px 24px 0; } .modhint { text-align:left; max-width:none; margin:8px 24px 0; }

/* Steckbrief (keine Taste gewaehlt) */
.steckbrief { padding:20px 4px 8px; display:flex; flex-direction:column; gap:16px; }
.sbkopf { display:flex; gap:14px; align-items:center; } .sbkopf h2 { margin:0; font-size:22px; } .sbkopf small { color:var(--sec); }
.ai.gross { width:52px; height:52px; border-radius:15px; background:var(--acc-s); color:var(--acc); } .ai.gross ha-icon { --mdc-icon-size:28px; }
.sbzahlen { display:grid; grid-template-columns:repeat(4, minmax(0,1fr)); gap:8px; }
.sbzahlen > div { background:var(--flaeche); border:1px solid var(--rand); border-radius:14px; padding:12px 14px; display:flex; flex-direction:column; gap:2px; }
.sbzahlen b { font-size:26px; font-weight:700; line-height:1.1; } .sbzahlen b small { font-size:14px; color:var(--sec); font-weight:500; }
.sbzahlen span { font-size:12px; color:var(--sec); display:flex; align-items:center; gap:5px; }
.c-short { color:#4fc3f7; } .c-double { color:#ffb74d; } .c-long { color:#ba68c8; } i.dot { display:inline-block; }
.sbev { display:flex; align-items:center; gap:8px; flex-wrap:wrap; font-size:13px; color:var(--sec); padding:10px 12px; border-radius:12px; border:1px dashed var(--rand); }
.sbev span { flex:1 1 200px; } .sbev > ha-icon { --mdc-icon-size:18px; }
.steckbrief h3 { margin:8px 0 0; font-size:11px; font-weight:600; text-transform:uppercase; letter-spacing:.09em; color:var(--sec); }
.chips.frei { flex-wrap:wrap; overflow:visible; padding:0; } .chips.frei .chip { display:inline-flex; align-items:center; gap:6px; } .chips.frei ha-icon { --mdc-icon-size:16px; color:var(--sec); }
i.cd { width:14px; height:8px; border-radius:4px; display:inline-block; }
.tipps { margin:0; padding-left:18px; color:var(--sec); font-size:13px; line-height:2; } .tipps a { color:var(--acc); }
.steckbrief .msg { margin:0; }

/* Tabelle aller Aktivitaeten */
.matrixw { padding:18px 24px 40px; }
table.matrix { border-collapse:separate; border-spacing:0; width:100%; table-layout:fixed; font-size:12.5px; background:var(--card); border:1px solid var(--rand); border-radius:16px; }
.matrix th, .matrix td { border-bottom:1px solid var(--rand); padding:0; text-align:left; vertical-align:top; }
.matrix tr:last-child > * { border-bottom:0; }
.matrix thead th { position:sticky; top:var(--kopf); z-index:2; background:var(--card); padding:10px 8px; font-size:13px; font-weight:600; white-space:nowrap; }
.matrix thead th:first-child { border-top-left-radius:16px; } .matrix thead th:last-child { border-top-right-radius:16px; }
.matrix thead th .ai { width:26px; height:26px; margin-right:8px; vertical-align:middle; } .matrix thead th.mod { color:var(--sec); }
.matrix thead th:first-child { width:200px; }
.matrix thead th { overflow:hidden; text-overflow:ellipsis; }
.matrix tbody th { padding:10px 12px; white-space:nowrap; font-weight:500; overflow:hidden; text-overflow:ellipsis; }
.matrix tbody th > * { vertical-align:middle; } .matrix tbody th > span { margin-left:8px; }
.matrix tbody th ha-icon { --mdc-icon-size:18px; color:var(--sec); } .matrix .okt { font-style:normal; font-weight:700; font-size:11px; color:var(--sec); width:18px; text-align:center; }
.matrix td + td, .matrix th + td { border-left:1px solid var(--rand); }
.matrix td button { display:flex; flex-direction:column; gap:3px; width:100%; min-height:42px; border:0; background:none; color:inherit; text-align:left; padding:8px 10px; cursor:pointer; font-size:12.5px; }
.matrix td button:hover { background:var(--acc-s); } .matrix td button.on { background:var(--acc-s); box-shadow:inset 0 0 0 2px var(--acc); }
.mz { display:flex; gap:7px; align-items:baseline; min-width:0; max-width:100%; } .mz > span { overflow:hidden; text-overflow:ellipsis; white-space:nowrap; min-width:0; } .mz i { font-style:normal; color:var(--sec); }
.mz .dot { flex:0 0 auto; transform:translateY(-1px); }
.matrix .frei { color:var(--sec); opacity:.45; } .matrix tbody tr:hover th { color:var(--acc); }
.matrixw .hint { text-align:left; max-width:none; }

/* Uebersicht */
.ueber, .werk { max-width:1280px; margin:0 auto; padding:30px 28px 40px; display:flex; flex-direction:column; gap:30px; }
.hero { display:flex; align-items:flex-end; gap:10px; flex-wrap:wrap; }
.hero h1 { margin:0; font-size:30px; font-weight:700; letter-spacing:-.01em; }
.hero p { margin:6px 0 0; color:var(--sec); font-size:14px; } .warnt { color:var(--gelb); } .okt2 { color:var(--ok); }
h2.abschn { font-size:11.5px; letter-spacing:.1em; text-transform:uppercase; color:var(--sec); margin:0 0 12px; font-weight:600; display:flex; align-items:center; gap:8px; }
h2.abschn i { font-style:normal; font-size:11px; padding:1px 7px; border-radius:999px; background:var(--flaeche); border:1px solid var(--rand); letter-spacing:0; }
.hinweise { display:flex; flex-direction:column; gap:8px; }
.hw2 { display:flex; align-items:center; gap:12px; padding:11px 12px 11px 12px; border-radius:16px; background:var(--card); border:1px solid var(--rand); flex-wrap:wrap; }
.hi { width:36px; height:36px; border-radius:11px; display:inline-flex; align-items:center; justify-content:center; flex:0 0 auto; background:var(--acc-s); color:var(--acc); }
.hw2.warn .hi { background:color-mix(in srgb, var(--gelb) 18%, transparent); color:var(--gelb); }
.hw2.err .hi { background:color-mix(in srgb, var(--fehler) 16%, transparent); color:var(--fehler); } .hw2.err { border-color:color-mix(in srgb, var(--fehler) 40%, transparent); }
.hw2.busy .hi { animation:puls 1.4s ease-in-out infinite; }
.ht { flex:1 1 240px; min-width:0; display:flex; flex-direction:column; gap:1px; } .ht b { font-size:14px; font-weight:600; } .ht small { font-size:12.5px; color:var(--sec); }
.rgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 400px), 1fr)); gap:18px; }
.rkarte { position:relative; overflow:hidden; background:var(--card); border:1px solid var(--rand); border-radius:24px; padding:22px; display:flex; flex-direction:column; gap:16px; box-shadow:var(--schatten); --glow:#6b7280; }
.rkarte.z-wlan { --glow:var(--ok); } .rkarte.z-ble { --glow:var(--blau); } .rkarte.z-dock { --glow:#a855f7; }
.rkarte::before { content:""; position:absolute; right:-90px; top:-110px; width:300px; height:300px; border-radius:50%; background:radial-gradient(circle, var(--glow) 0%, transparent 68%); opacity:.13; pointer-events:none; }
.rkarte > * { position:relative; }
.rkarte header { display:flex; gap:18px; align-items:center; }
.ringw { position:relative; width:88px; height:88px; flex:0 0 auto; } .ringw svg { width:100%; height:100%; transform:rotate(-90deg); }
.ringw .rw { position:absolute; inset:0; display:flex; flex-direction:column; align-items:center; justify-content:center; line-height:1; }
.ringw .rw b { font-size:24px; font-weight:700; } .ringw .rw b small { font-size:12px; color:var(--sec); font-weight:500; margin-left:1px; }
.ringw .rw ha-icon { --mdc-icon-size:16px; color:var(--gelb); margin-top:2px; }
.rkopf { display:flex; flex-direction:column; gap:5px; min-width:0; } .rkopf h2 { margin:0; font-size:21px; font-weight:700; } .rkopf small { color:var(--sec); font-size:12.5px; line-height:1.4; }
.zst { display:inline-flex; align-items:center; gap:7px; font-size:13px; font-weight:600; color:var(--sec); }
.zst i { width:8px; height:8px; border-radius:50%; background:currentColor; box-shadow:0 0 0 4px color-mix(in srgb, currentColor 18%, transparent); }
.zst.z-wlan { color:var(--ok); } .zst.z-ble { color:var(--blau); } .zst.z-dock { color:#a855f7; } .zst.z-wlan i, .zst.z-ble i, .zst.z-dock i { animation:atmen 2.4s ease-in-out infinite; }
@keyframes atmen { 50% { box-shadow:0 0 0 7px color-mix(in srgb, currentColor 6%, transparent); } }
.fakten { display:grid; grid-template-columns:repeat(2, minmax(0,1fr)); gap:8px; }
.fakt { display:flex; gap:10px; align-items:center; padding:10px 12px; border-radius:14px; background:var(--flaeche); min-width:0; }
.fakt > ha-icon { --mdc-icon-size:18px; color:var(--sec); flex:0 0 auto; }
.fakt span { display:flex; flex-direction:column; min-width:0; } .fakt small { font-size:10.5px; color:var(--sec); text-transform:uppercase; letter-spacing:.07em; }
.fakt b { font-size:13.5px; font-weight:600; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.fakt.ok b { color:var(--ok); } .fakt.warn b { color:var(--gelb); } .fakt.err b { color:var(--fehler); } .fakt.busy b { color:var(--acc); }
.zahlen span { display:inline-flex; align-items:center; gap:4px; } .zahlen span ha-icon { --mdc-icon-size:14px; opacity:.75; }
.erste .ek { display:flex; align-items:center; } .erste .eliste { display:grid; grid-template-columns:repeat(auto-fill, minmax(320px, 1fr)); gap:8px; }
.erste .es { display:flex; align-items:center; gap:10px; padding:10px 12px; border:1px solid var(--rand); border-radius:12px; background:var(--card); }
.erste .es.ok { opacity:.6; } .erste .es.ok .ei { color:#43a047; } .erste .ei { color:var(--primary-color); flex:none; display:flex; }
.erste .et { display:flex; flex-direction:column; flex:1; min-width:0; } .erste .et b { font-weight:500; font-size:13.5px; } .erste .et small { color:var(--sec); font-size:12px; }
.hwkopf { display:flex; align-items:center; gap:8px; flex-wrap:wrap; width:100%; border:none; background:none; padding:0; font:inherit; color:inherit; cursor:pointer; text-align:left; }
.hwkopf h2 { margin:0; } .hwkopf > ha-icon { color:var(--secondary-text-color); }
.hwz { font-size:12px; padding:2px 8px; border-radius:999px; background:var(--secondary-background-color); color:var(--secondary-text-color); }
.hwz.Fehler { background:color-mix(in srgb, var(--error-color) 15%, transparent); color:var(--error-color); }
.hwz.Warnung { background:color-mix(in srgb, var(--warning-color) 18%, transparent); color:var(--warning-color); }
.hwkopf + .hinweise { margin-top:10px; }
.kurve .k50 { stroke:currentColor; stroke-opacity:.25; stroke-dasharray:3 4; }
.kurve .kz rect { fill:transparent; } .kurve .kz rect:hover { fill:currentColor; fill-opacity:.08; }
.zahlen { display:flex; gap:6px; flex-wrap:wrap; } .zahlen span { font-size:12px; padding:4px 10px; border-radius:999px; border:1px solid var(--rand); color:var(--sec); } .zahlen b { color:var(--primary-text-color); }
.rkarte footer { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
.gesp { color:var(--sec); font-size:11.5px; margin-top:-6px; }
.dgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 260px), 1fr)); gap:12px; }
.dkarte { background:var(--card); border:1px solid var(--rand); border-radius:18px; padding:14px; display:flex; flex-direction:column; gap:10px; }
.dk1 { display:flex; align-items:center; gap:11px; } .dk1 > span:nth-child(2) { display:flex; flex-direction:column; flex:1; min-width:0; } .dk1 b { font-size:14px; }
.dk1 small { color:var(--sec); font-size:12px; display:flex; align-items:center; gap:6px; } .zp { width:7px; height:7px; border-radius:50%; background:#6b7280; display:inline-block; } .dkarte.on .zp { background:var(--ok); }
.dkarte:not(.on) { opacity:.75; }
.dkarte footer { display:flex; gap:6px; flex-wrap:wrap; } .ki.dkm { background:color-mix(in srgb, var(--primary-text-color) 6%, transparent); }
.ava svg.minidock { position:relative; z-index:1; transform:none; inset:auto; grid-area:1 / 1; } .rgruppe.aus .rk { opacity:.6; } .rgruppe.aus.auf .rk { opacity:1; }
.dzgrid.eins { grid-template-columns:minmax(0, 640px); }
.dk2 { display:flex; gap:12px; flex-wrap:wrap; font-size:12px; color:var(--sec); } .dk2 span { display:inline-flex; align-items:center; gap:4px; } .dk2 ha-icon { --mdc-icon-size:15px; }
.ki { width:42px; height:42px; border-radius:12px; display:inline-flex; align-items:center; justify-content:center; background:var(--acc-s); color:var(--acc); flex:0 0 auto; }
.kgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 250px), 1fr)); gap:12px; }
.kachel { display:flex; gap:13px; align-items:center; padding:14px; border-radius:18px; border:1px solid var(--rand); background:var(--card); cursor:pointer; text-align:left; color:inherit; }
.kachel:hover { border-color:var(--acc); transform:translateY(-1px); box-shadow:0 8px 24px rgba(0,0,0,.12); }
.kachel > span:nth-child(2) { display:flex; flex-direction:column; gap:3px; flex:1; min-width:0; } .kachel b { font-size:14.5px; } .kachel small { font-size:12.5px; color:var(--sec); line-height:1.4; }
.kachel .kpf { color:var(--sec); --mdc-icon-size:18px; } .kachel:hover .kpf { color:var(--acc); }
.kachel.rot .ki { background:color-mix(in srgb, var(--fehler) 14%, transparent); color:var(--fehler); } .kachel.rot:hover { border-color:var(--fehler); }

/* Firmware-Seite: Titel steht im Kopf, nur die Knoepfe bleiben */
.seite-fw .fw > .dkopf:first-child h2 { display:none; }
/* Aenderungsliste ueber der Speicherleiste */
.sbw { flex-direction:column; align-items:center; gap:8px; }
.diffpop { pointer-events:auto; width:min(560px, 100%); max-height:min(52vh, 460px); display:flex; flex-direction:column; gap:8px; padding:14px 10px 10px 14px; background:var(--card);
  border:1px solid var(--rand); border-radius:18px; box-shadow:0 18px 50px rgba(0,0,0,.32); animation:hoch .15s ease-out; }
.diffpop > b { font-size:13px; padding-right:4px; }
.diffl { overflow-y:auto; display:flex; flex-direction:column; gap:2px; padding-right:4px; }
.dz { display:flex; align-items:center; gap:10px; border:0; background:none; color:inherit; text-align:left; padding:6px 8px; border-radius:10px; cursor:pointer; }
.dz:hover:not(:disabled) { background:var(--flaeche); } .dz:disabled { cursor:default; }
.dz > span:nth-child(2) { display:flex; flex-direction:column; flex:1; min-width:0; } .dz b { font-size:13px; font-weight:500; } .dz small { font-size:11.5px; color:var(--sec); }
.dzi { width:28px; height:28px; border-radius:8px; display:inline-flex; align-items:center; justify-content:center; background:var(--acc-s); color:var(--acc); flex:0 0 auto; } .dzi ha-icon { --mdc-icon-size:16px; }
.dzi.neu { background:color-mix(in srgb, var(--ok) 16%, transparent); color:var(--ok); } .dzi.weg { background:color-mix(in srgb, var(--fehler) 14%, transparent); color:var(--fehler); }
.dzi.warn { background:color-mix(in srgb, var(--gelb) 18%, transparent); color:var(--gelb); }
.diffk.on { background:var(--acc-s); border-color:var(--acc); }
.dz .kpf { color:var(--sec); --mdc-icon-size:18px; }

/* Akku-Verlauf in der Remote-Karte */
.kurve { margin:-4px 0 -2px; } .kurve svg { width:100%; height:54px; display:block; }
.kurve .kl { display:flex; justify-content:space-between; font-size:10.5px; color:var(--sec); margin-top:2px; } .kurve .kl span:nth-child(2) { color:inherit; opacity:.9; }

/* Werkstatt */
.werk .fw.eingebettet { padding:0; max-width:none; margin:0; }
.werk .fw.eingebettet > .dkopf:first-child { margin-top:0; }
.kuerzel { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 300px), 1fr)); gap:8px; }
.kuerzel > div { display:flex; flex-direction:column; gap:6px; padding:12px 14px; border-radius:14px; background:var(--card); border:1px solid var(--rand); } .kuerzel small { color:var(--sec); }

/* Bluetooth und Infrarot als Seite */
.alsseite { padding:20px 24px 40px; }
.alsseite .modal { position:static; inset:auto; background:none; backdrop-filter:none; -webkit-backdrop-filter:none; padding:0; display:block; animation:none; z-index:auto; }
.alsseite .dlg { max-width:900px; margin:0 auto; max-height:none; height:auto; border-radius:22px; border:1px solid var(--rand); box-shadow:var(--schatten); animation:none; padding:8px 24px 24px; }
.alsseite .dlg .opts { overflow:visible; }
.alsseite .dh .icon[data-a="modalx"], .alsseite .dfoot { display:none; }
.alsseite .dh h2 { font-size:20px; }
.alsseite h3 { margin:18px 0 6px; font-size:15px; }

/* Toast */
.toast { position:fixed; right:22px; bottom:22px; z-index:45; width:min(460px, calc(100vw - 32px)); display:flex; align-items:flex-start; gap:10px; padding:13px 10px 13px 14px; border-radius:16px;
  background:var(--card); border:1px solid var(--rand); box-shadow:0 18px 48px rgba(0,0,0,.3); font-size:13.5px; line-height:1.5; overflow:hidden; }
.toast.neu { animation:toastein .26s cubic-bezier(.2,.9,.3,1.2); }
@keyframes toastein { from { opacity:0; transform:translateY(16px) scale(.97); } }
.toast > ha-icon { color:var(--acc); flex:0 0 auto; margin-top:1px; }
.toast.err { border-color:color-mix(in srgb, var(--fehler) 50%, transparent); } .toast.err > ha-icon { color:var(--fehler); }
.toast > span { flex:1; min-width:0; } .toast .icon { padding:4px; margin:-3px 0 0; } .toast .btn { flex:0 0 auto; }
.toast .zeit { position:absolute; left:0; bottom:0; height:3px; width:100%; background:var(--acc); opacity:.5; transform-origin:left; animation-name:zeit; animation-timing-function:linear; animation-fill-mode:forwards; }
@keyframes zeit { to { transform:scaleX(0); } }
.huelle:has(.savebar) ~ .toast { bottom:92px; }

/* Schnellsuche */
.palbg { position:fixed; inset:0; z-index:50; background:rgba(8,10,14,.5); backdrop-filter:blur(5px); -webkit-backdrop-filter:blur(5px); display:flex; justify-content:center; align-items:flex-start; padding:11vh 16px 16px; animation:einblenden .12s; }
.pal { width:min(660px, 100%); max-height:72vh; display:flex; flex-direction:column; background:var(--card); border:1px solid var(--rand); border-radius:20px; box-shadow:0 30px 90px rgba(0,0,0,.5); overflow:hidden; animation:hoch .16s ease-out; }
.palin { display:flex; align-items:center; gap:10px; padding:12px 16px; border-bottom:1px solid var(--rand); } .palin > ha-icon { color:var(--acc); }
.palin input { border:0; background:none; font-size:17px; padding:6px 0; box-shadow:none !important; outline:0; }
.palin input::-webkit-search-cancel-button { display:none; }
.palerg { overflow-y:auto; padding:6px 8px 8px; scroll-padding:30px; }
.palg { font-size:10.5px; text-transform:uppercase; letter-spacing:.1em; color:var(--sec); padding:12px 10px 5px; font-weight:600; }
.pe { display:flex; align-items:center; gap:12px; width:100%; border:0; background:none; color:inherit; text-align:left; padding:7px 10px; border-radius:12px; cursor:pointer; }
.pico { width:34px; height:34px; border-radius:10px; background:var(--flaeche); display:inline-flex; align-items:center; justify-content:center; color:var(--sec); flex:0 0 auto; } .pico ha-icon { --mdc-icon-size:18px; }
.pe > span:nth-child(2) { display:flex; flex-direction:column; min-width:0; flex:1; }
.pe b { font-weight:500; font-size:14px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; } .pe small { font-size:12px; color:var(--sec); white-space:nowrap; overflow:hidden; text-overflow:ellipsis; }
.pe:hover { background:var(--flaeche); } .pe.sel { background:var(--acc-s); } .pe.sel .pico { background:var(--acc); color:#fff; }
.pe mark { background:none; color:var(--acc); font-weight:700; }
.palfuss { display:flex; gap:14px; align-items:center; padding:9px 16px; border-top:1px solid var(--rand); font-size:11.5px; color:var(--sec); }

/* Leiste unten (Handy) */
.unten { display:none; position:fixed; left:0; right:0; bottom:0; z-index:12; padding:6px 6px calc(6px + env(safe-area-inset-bottom)); justify-content:space-around;
  background:color-mix(in srgb, var(--card) 90%, transparent); backdrop-filter:saturate(1.6) blur(16px); -webkit-backdrop-filter:saturate(1.6) blur(16px); border-top:1px solid var(--rand); }
.unten button { flex:1; display:flex; flex-direction:column; align-items:center; gap:3px; border:0; background:none; color:var(--sec); font-size:10.5px; cursor:pointer; padding:2px 0; }
.unten .ui { display:inline-flex; padding:4px 18px; border-radius:999px; transition:background-color .2s; }
.unten button.on { color:var(--acc); font-weight:600; } .unten button.on .ui { background:var(--acc-s); }
.navgrund { display:none; }

/* Mittlere Breite: Leiste nur mit Symbolen */
@container huelle (min-width: 761px) and (max-width: 1100px) {
  .app { grid-template-columns:78px minmax(0,1fr); }
  .side { padding:14px 9px; align-items:center; }
  .brand { padding:2px 0 14px; } .brand .bt, .suche.nav > span, .suche.nav kbd, .ni > span, .rkt, .nfuss, .nsprache { display:none; }
  .suche.nav { width:46px; height:40px; justify-content:center; padding:0; }
  .nsec { padding:0; margin:12px 0 8px; width:36px; height:1px; background:var(--rand); font-size:0; }
  .ni { width:48px; height:44px; justify-content:center; padding:0; }
  .ni .zahl, .ni .punkt { position:absolute; top:3px; right:3px; } .ni .zahl { min-width:17px; height:17px; font-size:10px; padding:0 4px; }
  .rgruppe { width:60px; } .rgruppe.auf { padding-bottom:4px; }
  .rk { justify-content:center; padding:7px 0; position:relative; } .rk > .punkt { position:absolute; top:6px; right:6px; }
  .subnav { padding:0; align-items:center; } .subnav .ni { width:44px; height:38px; padding:0; }
}
@container huelle (min-width: 761px) and (max-width: 1100px) { .main { grid-template-columns:minmax(280px,340px) minmax(0,1fr); gap:18px; } }
/* Schmal: Schublade + Leiste unten */
@container huelle (max-width: 760px) {
  .app { grid-template-columns:minmax(0,1fr); }
  .side { position:fixed; left:0; top:0; bottom:0; height:auto; width:min(310px, 86vw); z-index:40; transform:translateX(-104%); transition:transform .24s cubic-bezier(.2,.8,.2,1); box-shadow:0 0 60px rgba(0,0,0,.4); background:var(--card); --side-bg:var(--card); }
  .app.navauf .side { transform:none; }
  .app.navauf .navgrund { display:block; position:fixed; inset:0; z-index:39; background:rgba(0,0,0,.45); animation:einblenden .2s; }
  .navzu { display:inline-flex; }
  .unten { display:flex; }
  .seitenkopf { padding:0 8px 0 6px; gap:6px; height:58px; --kopf:58px; }
  .seitenkopf .suche { min-width:0; padding:8px; border:0; background:none; } .seitenkopf .suche > span, .seitenkopf .suche kbd { display:none; } .seitenkopf .suche ha-icon { --mdc-icon-size:22px; }
  .ktitel { cursor:pointer; } .kico { width:34px; height:34px; flex-basis:34px; } .kt b { font-size:16px; } .kt small ha-icon { display:inline-flex; }
  .pill { padding:4px; } .pill .ptx { display:none; }
  .inhalt { padding-bottom:84px; }
  .sbw { bottom:80px; }
  .huelle:has(.main.editing) .unten, .huelle:has(.mmain.editing) .unten { display:none; }
  .huelle:has(.main.editing) .sbw, .huelle:has(.mmain.editing) .sbw { bottom:12px; }
  .main, .mmain { padding:14px 12px 20px; } .mbar, .aktleiste { padding:14px 12px 0; } .aktset { padding:10px 12px 0; } .modhint { margin:8px 12px 0; }
  .gesten { padding:14px 12px 30px; } .einst, .dock, .fw, .alsseite, .matrixw { padding:14px 12px 30px; }
  .ueber, .werk { padding:20px 14px 30px; gap:24px; } .hero h1 { font-size:24px; }
  .akts { flex-wrap:wrap; max-width:100%; } .aktleiste { flex-wrap:wrap; } .trenner { display:none; }
  .rkarte { padding:18px; border-radius:20px; } .fakt b { white-space:normal; display:-webkit-box; -webkit-line-clamp:2; -webkit-box-orient:vertical; } .ringw { width:72px; height:72px; } .ringw .rw b { font-size:20px; }
  .sbzahlen { grid-template-columns:repeat(2, minmax(0,1fr)); }
  .alsseite .dlg { padding:4px 14px 18px; border-radius:18px; }
  .matrix thead th:first-child { width:130px; } .matrixw { overflow-x:auto; }
  .matrix thead th { position:static; }
}
@media (max-width: 760px) {
  .toast { left:12px; right:12px; width:auto; bottom:84px; }
  .huelle:has(.savebar) ~ .toast { bottom:150px; }
  .palbg { padding:0; } .pal { max-height:100%; height:100%; border-radius:0; width:100%; border:0; }
  .main.editing .editor, .mmain.editing .editor { z-index:20; }
  .sbw { z-index:25; }
}
/* Handy (2026-11-07): nichts breiter als der Bildschirm - Auswahllisten so breit wie ihr laengster Eintrag, lange
   Entitaets-Namen ohne Umbruch, Knopfzeilen ohne Umbruch schoben die Seite seitlich auf (Uebersicht, Einstellungen ...) */
@container huelle (max-width: 760px) {
  select { max-width:100%; min-width:0; }
  .lbl, .lsensor, .opt, .aopt, .aopt > *, .inl { max-width:100%; min-width:0; }
  .lbl, .opt, .inl { flex-wrap:wrap; }
  .inl > .btn, .btn.small { white-space:normal; max-width:100%; }
  .ht, .ht b, .ht small, .note, .hint, .hw2, small { overflow-wrap:anywhere; min-width:0; }
}
`;

export const PRUEF_CSS = `
.pruef .psuche { display:flex; align-items:center; gap:8px; background:var(--card); border:1px solid var(--rand); border-radius:12px; padding:6px 12px; max-width:560px; }
.pruef .psuche input { flex:1; border:0; background:transparent; color:inherit; font:inherit; font-size:14px; outline:none; padding:4px 0; }
.pruef .pents { display:flex; flex-direction:column; gap:6px; }
.pruef .pent { background:var(--card); border:1px solid var(--rand); border-radius:12px; }
.pruef .pent summary { display:flex; align-items:center; gap:10px; padding:8px 12px; cursor:pointer; list-style:none; }
.pruef .pent summary::-webkit-details-marker { display:none; }
.pruef .pent summary span { flex:1; display:flex; flex-direction:column; min-width:0; } .pruef .pent summary small { color:var(--sec); font-size:12px; overflow:hidden; text-overflow:ellipsis; }
.pruef .pz { font-style:normal; font-size:12px; color:var(--sec); }
.pruef .prefs { display:flex; flex-direction:column; padding:0 8px 8px; }
.pruef .pref { display:flex; align-items:center; gap:10px; text-align:left; background:none; border:0; border-top:1px solid var(--div); color:inherit; font:inherit; font-size:13px; padding:6px 4px; cursor:pointer; }
.pruef .pref:hover { background:var(--flaeche); } .pruef .pref span:nth-child(2) { flex:1; }
.pruef .pdev { font-size:11px; font-weight:600; padding:1px 7px; border-radius:999px; background:var(--acc-s); color:var(--acc); white-space:nowrap; }
.vgl .vwahl { display:flex; flex-wrap:wrap; align-items:center; gap:10px; }
.vgl .vwahl label { display:flex; align-items:center; gap:6px; font-weight:600; }
.vgl .vliste { background:var(--card); border:1px solid var(--rand); border-radius:16px; padding:4px 12px; }
.vgl .vz { display:flex; align-items:center; gap:10px; flex-wrap:wrap; padding:8px 0; border-top:1px solid var(--div); }
.vgl .vz:first-child { border-top:0; } .vgl .vz > span:nth-of-type(1) { flex:1 1 180px; min-width:0; }
.vgl .vz.sub { padding:5px 0 5px 30px; font-size:13px; } .vgl .vz.gleich { color:var(--sec); }
.vgl .vst { font-size:12px; color:var(--sec); } .vgl .vst.ok { color:var(--ok); } .vgl .vst.warn { color:var(--gelb); font-weight:600; }
.vgl .vk { display:flex; gap:6px; } .vgl .vk ha-icon { --mdc-icon-size:16px; }
.dock .dzgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(min(100%, 320px), 1fr)); gap:12px; margin:8px 0 18px; }
.dock .dzk { border:1px solid var(--div); background:var(--card); border-radius:14px; padding:12px; display:flex; flex-direction:column; gap:10px; }
.dock .dzk.off { opacity:.75; }
.dock .dz1 { display:flex; align-items:center; gap:10px; } .dock .dz1 > span:nth-child(2) { display:flex; flex-direction:column; } .dock .dz1 small { color:var(--sec); font-size:12px; }
.dock .dz1 .zp { display:inline-block; width:8px; height:8px; border-radius:50%; background:#6b7280; margin-right:6px; } .dock .dzk.on .zp { background:var(--ok); } .dock .dzk.off .zp { background:var(--fehler); }
.dock .dfakten { display:grid; grid-template-columns:repeat(auto-fill, minmax(92px, 1fr)); gap:6px; }
.dock .df { display:flex; flex-direction:column; gap:1px; background:var(--flaeche); border-radius:10px; padding:6px 8px; } .dock .df ha-icon { --mdc-icon-size:16px; color:var(--sec); }
.dock .df small { font-size:11px; color:var(--sec); } .dock .df b { font-size:14px; } .dock .df.warn b { color:var(--gelb); }
.dock .dz24 { display:flex; flex-wrap:wrap; align-items:center; gap:6px 12px; font-size:13px; } .dock .dz24 small { color:var(--sec); margin-left:auto; } .dock .dz24 .warn { color:var(--gelb); font-weight:600; }
.dock .dvk { width:100%; height:40px; display:block; } .dock .dvk path { fill:none; stroke:var(--acc); stroke-width:2; } .dock .dvk .gr { stroke:var(--fehler); stroke-dasharray:4 4; stroke-width:1; opacity:.6; }
.dock .dkspr { display:flex; flex-wrap:wrap; gap:6px; margin:8px 0 4px; position:sticky; top:0; z-index:2; padding:6px 0; background:var(--bg, var(--primary-background-color)); }
.dock .dkspr .chip { display:inline-flex; align-items:center; gap:4px; } .dock .dkspr ha-icon { --mdc-icon-size:16px; }
.dock .ampel { display:flex; gap:10px; align-items:flex-start; padding:8px 10px; border-radius:12px; background:var(--flaeche); }
.dock .ampel > span:last-child { display:flex; flex-direction:column; gap:2px; } .dock .ampel small { color:var(--sec); font-size:12px; }
.dock .ampel .lichter { display:flex; flex-direction:column; gap:3px; padding:4px 5px; border-radius:8px; background:#111827; flex:0 0 auto; }
.dock .ampel .lichter i { width:10px; height:10px; border-radius:50%; background:#374151; display:block; }
.dock .ampel.rot .r { background:#ef4444; box-shadow:0 0 6px #ef4444; } .dock .ampel.gelb .y { background:#f59e0b; box-shadow:0 0 6px #f59e0b; } .dock .ampel.gruen .g { background:#22c55e; box-shadow:0 0 6px #22c55e; }
.dock .dzkurve .kl { display:flex; justify-content:space-between; font-size:11px; color:var(--sec); }
`;

export const SYNC_CSS = `
.sync .syremotes { display:flex; flex-wrap:wrap; gap:6px 14px; margin:2px 0 8px; }
.sync label.sw { display:flex; align-items:flex-start; gap:8px; cursor:pointer; padding:4px 0; }
.sync label.sw input { margin-top:3px; flex:none; }
.sync label.sw span { display:flex; flex-direction:column; gap:1px; }
.sync label.sw span small { color:var(--sec); font-size:12px; line-height:1.35; }
.sync .syzeile { border-top:1px solid var(--div); } .sync .syzeile b { display:flex; align-items:center; gap:6px; } .sync .syzeile ha-icon { --mdc-icon-size:18px; }
.sync .syhaupt b { font-size:15px; }
.sync .sygr { border-top:1px solid var(--div); padding:4px 0; }
.sync .sygr summary { display:flex; align-items:center; justify-content:flex-start; gap:8px; cursor:pointer; list-style:none; min-height:32px; text-align:left; }
.sync .sygr summary b { flex:1; } .sync .sygr summary small { white-space:nowrap; } .sync .sygr summary input { margin:0; flex:none; width:auto; min-width:0; }
.sync .sygr summary::-webkit-details-marker { display:none; }
.sync .sygr summary::after { content:"›"; margin-left:auto; transition:transform .15s; color:var(--sec); }
.sync .sygr[open] summary::after { transform:rotate(90deg); }
.sync .sygr summary small { color:var(--sec); } .sync .sygr summary ha-icon { --mdc-icon-size:18px; }
.sync .syfelder { display:grid; grid-template-columns:repeat(auto-fill, minmax(220px, 1fr)); gap:0 12px; padding:2px 0 6px 26px; }
.sync .syfelder label.sw { padding:2px 0; font-size:13px; }
.sync .sydiff { color:var(--acc); font-style:normal; font-weight:700; }
.sync .sykopf { font-size:12px; font-weight:600; color:var(--sec); margin:8px 0 2px; text-transform:uppercase; letter-spacing:.04em; }
.sync .ez { flex-wrap:wrap; } .sync .ez > small { flex:1 1 100%; color:var(--sec); }
.sync .ez.syaus { opacity:.55; }
`;

export const IRC_CSS = `
.irc { max-width:1100px; }
.irc .irkopf { display:flex; gap:16px; align-items:flex-end; justify-content:space-between; flex-wrap:wrap; margin-bottom:18px; }
.irc .irkopf h2 { margin:0 0 4px; font-size:24px; display:flex; align-items:center; gap:10px; }
.irc .irkopf h2 ha-icon { --mdc-icon-size:28px; color:var(--acc); }
.irc .irkopf small { color:var(--sec); max-width:560px; display:block; line-height:1.4; }
.irc .irzahlen { display:flex; gap:10px; flex-wrap:wrap; }
.irc .irzahl { border:1px solid var(--rand); border-radius:14px; padding:8px 14px; min-width:96px; background:var(--card); }
.irc .irzahl b { display:block; font-size:20px; } .irc .irzahl small { color:var(--sec); font-size:12px; }
.irc .irsek { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin:26px 0 10px; }
.irc .irsek h3 { margin:0; font-size:17px; display:flex; align-items:center; gap:8px; }
.irc .irsek .inl2 { flex:1 1 180px; flex-wrap:nowrap; } .irc .irsek .inl2 select { flex:1 1 auto; }   /* Handy: Remote-Name nicht abschneiden */
@media (max-width:700px) { .irc .irsek .inl2 { flex-basis:100%; } }
.irc .irsek h3 ha-icon { color:var(--acc); background:var(--acc-s); border-radius:8px; padding:4px; --mdc-icon-size:18px; }
.irc .qgrid { display:grid; grid-template-columns:repeat(auto-fill, minmax(300px, 1fr)); gap:14px; }
.irc .qkarte { border:1px solid var(--rand); border-radius:18px; background:var(--card); box-shadow:var(--schatten); overflow:hidden; display:flex; flex-direction:column; }
.irc .qkopf { padding:14px 16px 12px; display:flex; gap:12px; align-items:center; background:linear-gradient(135deg, var(--acc-s), transparent 70%); }
.irc .qkopf ha-icon { --mdc-icon-size:30px; color:var(--acc); }
.irc .qkopf b { font-size:16px; display:block; } .irc .qkopf small { color:var(--sec); }
.irc .qrumpf { padding:4px 16px 14px; display:flex; flex-direction:column; gap:8px; flex:1; }
.irc .qwerte { display:flex; gap:16px; flex-wrap:wrap; font-size:13px; } .irc .qwerte span { color:var(--sec); } .irc .qwerte b { color:var(--primary-text-color); font-weight:600; }
.irc .qneu { display:flex; align-items:center; gap:6px; font-size:13px; color:#b45309; background:rgba(245,158,11,.14); border-radius:10px; padding:6px 10px; }
.irc .qok { display:flex; align-items:center; gap:6px; font-size:13px; color:#15803d; } .irc .qok ha-icon, .irc .qneu ha-icon { --mdc-icon-size:18px; }
.irc .qfuss { display:flex; gap:6px; flex-wrap:wrap; align-items:center; margin-top:auto; }
.irc .qfuss .sw { margin-left:auto; font-size:13px; }
.irc .geraet { border:1px solid var(--rand); border-radius:14px; background:var(--card); margin-bottom:10px; overflow:hidden; }
.irc .gkopf { display:flex; align-items:center; gap:8px; padding:8px 10px 8px 12px; }
.irc .gkopf input.gname { flex:1; min-width:120px; font-size:15px; font-weight:600; border:1px solid transparent; background:transparent; color:inherit; border-radius:8px; padding:4px 6px; }
.irc .gkopf input.gname:hover, .irc .gkopf input.gname:focus { border-color:var(--div); background:var(--primary-background-color); }
.irc .gkopf .anz { color:var(--sec); font-size:12px; white-space:nowrap; }
.irc .codes { border-top:1px solid var(--div); }
.irc .code { display:flex; align-items:center; gap:8px; padding:5px 10px 5px 14px; border-top:1px solid var(--div); }
.irc .code:first-child { border-top:0; }
.irc .code input { flex:1; min-width:90px; border:1px solid transparent; background:transparent; color:inherit; border-radius:6px; padding:3px 6px; font-size:14px; }
.irc .code input:hover, .irc .code input:focus { border-color:var(--div); background:var(--primary-background-color); }
.irc .code small { color:var(--sec); font-size:12px; white-space:nowrap; overflow:hidden; text-overflow:ellipsis; max-width:240px; }
.irc .code select { font-size:12px; max-width:130px; }
.irc .leer { border:2px dashed var(--div); border-radius:14px; padding:22px; text-align:center; color:var(--sec); }
.irc .leer ha-icon { --mdc-icon-size:36px; display:block; margin:0 auto 6px; opacity:.6; }
.irc .irsuche { display:flex; gap:8px; flex-wrap:wrap; align-items:center; margin-bottom:8px; }
.irc .irsuche input[type=search] { flex:1; min-width:220px; font-size:15px; padding:9px 12px; border-radius:12px; border:1px solid var(--div); background:var(--primary-background-color); color:inherit; }
.irc .treffer { display:grid; grid-template-columns:repeat(auto-fill, minmax(260px, 1fr)); gap:8px; }
.irc .treffer .ent { margin:0; }
.qbadge { font-size:11px; border-radius:999px; padding:1px 7px; background:var(--acc-s); color:var(--acc); font-weight:600; white-space:nowrap; }
.qbadge.flipper { background:rgba(249,115,22,.15); color:#ea580c; } .qbadge.harmony { background:rgba(14,165,233,.15); color:#0284c7; }
.qbadge.eigen { background:rgba(34,197,94,.16); color:#16a34a; }
.irc .devkarte { border:1px solid var(--rand); border-radius:16px; background:var(--card); padding:12px 14px; margin-top:6px; }
.irc .devkarte .dkk { display:flex; align-items:center; gap:8px; flex-wrap:wrap; margin-bottom:8px; }
.irc .devkarte .dkk b { font-size:16px; }
.irc .devkarte .liste { max-height:420px; overflow:auto; border-top:1px solid var(--div); }
.irfilter { display:flex; gap:6px; flex-wrap:wrap; margin:0 0 8px; }
.irfilter .chip.on { background:var(--acc); color:#fff; border-color:var(--acc); }
@media (max-width: 600px) { .irc .code small { display:none; } }
`;
