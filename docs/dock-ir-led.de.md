🇬🇧 [English](dock-ir-led.md) · 🇩🇪 Deutsch

# Dock Rev6: externe IR-LED und zusätzliche IR-LEDs

Das Dock (Platine **OpenRemote Dock Rev6**) sendet Infrarot über vier 940-nm-LEDs (D1–D4) am Platinenrand.
Zwei Lötpads mit der Beschriftung **J4 / „IR LED“** direkt neben D1 sind für eine IR-LED am Kabel gedacht – zum
Beispiel in einen geschlossenen Schrank. Für alles auf dieser Seite muss die Firmware nicht geändert werden.

## So funktioniert der IR-Ausgang

```
 +5V ──R5 39Ω──┬── D1 ──┐
               │        │
              J4/1    J4/2 ─── IR_K ─── Q1 (MOSFET, FS8205A) ─── GND
                                          │
 +5V ──R6 39Ω──── D2 ───── IR_K           GPIO0 (IR_PWM)
 +5V ──R7 39Ω──── D3 ───── IR_K
 +5V ──R8 39Ω──── D4 ───── IR_K
```

- Jede LED hat ihren **eigenen 39-Ω-Widerstand** an +5 V. Das ergibt etwa **95 mA** pro LED, solange sie leuchtet.
- Alle Minus-Anschlüsse (Kathoden) laufen in **IR_K** zusammen, das der MOSFET Q1 auf Masse schaltet.
- Die Firmware pulst nur: 38-kHz-Träger mit **33 %** Einschaltzeit, und nur während ein Befehl gesendet wird. Im
  Mittel zieht eine LED beim Senden etwa 30 mA, sonst nichts.
- Das ganze Dock ist mit einer rückstellenden **1-A-Sicherung** (F1) abgesichert.

## J4: die Pads „IR LED“

J4 ist **kein eigener Ausgang**. Es liegt **parallel zu D1** und teilt sich D1s Widerstand R5:

- **J4 Pin 1 = Plus der LED (Anode)**, verbunden mit R5 und der Anode von D1
- **J4 Pin 2 = Minus der LED (Kathode)**, verbunden mit IR_K

Vor dem Löten die Polung mit dem Multimeter (Durchgangsprüfer) prüfen: Das **Plus**-Pad piept gegen das Bein von
**R5**, das *nicht* an +5 V geht; das **Minus**-Pad piept gegen das entsprechende Bein von D2, D3 und D4.

### Variante A – externe LED statt D1 (empfohlen)

**D1** ablöten und eine IR-LED am Kabel an J4 anschließen. **Nur die LED** – der Widerstand (R5) sitzt schon auf
der Platine. Die externe LED bekommt die vollen ~95 mA. Ein paar Meter Kabel sind kein Problem, am besten verdrillt.

### Variante B – externe LED zusätzlich zu D1

Geht, aber D1 und die externe LED teilen sich dann den Strom von R5 – ungleich, weil keine zwei LEDs gleich sind.
Beide werden schwächer. Besser Variante A oder C.

### Was an J4 passt

Nur eine **IR-LED** (940 nm, z. B. 5-mm-LED, gepulst für mindestens 100 mA ausgelegt). Eine sichtbare LED als
Sendeanzeige würde auch leuchten. Sonst funktioniert dort nichts: An den Pads liegen nur kurze 38-kHz-Pulse beim
Senden an, keine Stromversorgung.

## Variante C – mehr IR-LEDs (mehr Leistung)

Mehr Leistung gibt es nicht über J4, sondern mit **zusätzlichen LEDs, jede mit eigenem Widerstand**:

```
 +5V ── 39 Ω (0,5 W) ── LED + … LED − ── J4 Pin 2 (IR_K)
```

- **Jede zusätzliche LED braucht ihren eigenen 39-Ω-Widerstand.** Rechnerisch reichen 0,25 W, 0,5 W gibt Reserve.
- **Nie** eine LED ohne Widerstand zwischen +5 V und IR_K hängen – sie brennt sofort durch und kann die Sicherung auslösen.
- Manche fertigen IR-LEDs am Kabel haben einen Widerstand eingebaut. Die kommen an J4 (statt D1) oder direkt an
  +5 V/IR_K ohne zusätzlichen Widerstand – Produktbeschreibung prüfen.

### Wie viele?

| | Spitze beim Senden | |
|---|---|---|
| ESP32-C3 mit WLAN + Bluetooth | bis ~250 mA | |
| 4 eingebaute LEDs | ~380 mA | |
| **+ 2–3 zusätzliche LEDs** | +190–285 mA | **mit jedem ordentlichen USB-Netzteil** |
| **+ 4 zusätzliche LEDs (8 insgesamt)** | ~+380 mA | **2-A-Netzteil** verwenden |

Die 1-A-Sicherung reagiert auf Erwärmung, also auf den mittleren Strom, und der liegt weit unter den Spitzen
(33 % Einschaltzeit, nur beim Senden). Der 100-µF-Kondensator neben den IR-LEDs glättet die Pulse. Der MOSFET Q1
schafft mehrere Ampere und ist nicht die Grenze.

## Tipp

Mehrere LEDs in dieselbe Richtung bringen kaum mehr Reichweite. Am meisten helfen sie, wenn sie in **verschiedene
Richtungen** zeigen oder am Kabel **direkt vor einem Gerät** liegen – etwa im Schrank, unter dem Fernseher, neben der Soundbar.

---

Platinendaten: [OpenRemote-Hardware, Dock Rev6](https://github.com/LORDSn1per/OpenRemote-Hardware) (Schaltplan und
Layout, GPL-3.0). IR-Sender-Einstellungen: `remote_transmitter` in [`esphomeremote-dock-board.yaml`](../esphomeremote-dock-board.yaml).
