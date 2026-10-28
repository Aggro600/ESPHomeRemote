# Dock: Ladekontakte per ESP schalten (Nachrüstung, 2026-10-11)

Ziel: Die Pogo-Pins (POGO_5V) des Docks über einen freien Pin des ESP32-C3 ein- und ausschalten. Das ersetzt den
Smart Plug (`switch.dockingstation_remote_wz_wohnzimmer`), mit dem die Remote bisher geweckt wurde. Aus → an erzeugt an der
Remote den Weckgrund „Ladekabel“. Das Dock selbst bleibt dabei an, also auch IR, 433 MHz und WLAN.

In ESPHome (`dock-wz.yaml`) gibt es dafür:
- den Schalter **Ladekontakte**
- den Knopf **Remote wecken** (1,5 s aus, dann wieder an)

## Pin

**GPIO2** (Super Mini, linke Reihe, Beschriftung „2“). Auf der Rev6-Platine ist er bewusst frei.

Er ist ein Strapping-Pin. Espressif empfiehlt dort beim Start HIGH, und genau das liefert der Pull-up R1 unten.
Damit gilt: **Beim Einschalten, beim Reset und ohne Firmware sind die Kontakte AN.** Die Remote lädt also immer,
auch wenn das Dock hängt.

## Schaltung (High-Side-Schalter)

```
 +5V (nach F1) ──────────────┬──────────┐
                             │          │ S
                            R3 10k    ┌─┴─┐
                             │   G    │Q1 │ AO3401A (P-Kanal, SOT-23)
                             ├────────┤   │
                             │        └─┬─┘
                             │          │ D
                             │          └────────── POGO_5V (J1 Pin 1, zu den Pogo-Pins)
                             │ D
                           ┌─┴─┐
          GPIO2 ──R2 1k──G │Q2 │ AO3400A (N-Kanal, SOT-23)
            │              └─┬─┘
           R1 10k            │ S
            │               GND
          +3V3
```

- GPIO2 HIGH (auch durch R1) → Q2 leitet → Gate von Q1 liegt auf 0 V (V_GS = −5 V) → **Kontakte an**
- GPIO2 LOW → Q2 sperrt → R3 zieht das Gate von Q1 auf +5 V → **Kontakte aus**

| Teil | Wert | LCSC | Hinweis |
|---|---|---|---|
| Q1 | AO3401A, P-MOSFET −30 V / −4 A | C15127 | R_DS(on) ≈ 50 mΩ bei −4,5 V: bei 1 A Ladestrom 50 mV / 50 mW |
| Q2 | AO3400A, N-MOSFET 30 V / 5,7 A | C20917 | Logic-Level, schaltet sicher mit 3,3 V (2N7002 geht auch) |
| R1 | 10 kΩ nach 3V3 | C25804 | Pull-up: Standard AN, Strapping-Pin HIGH |
| R2 | 1 kΩ | C21190 | Serienwiderstand Gate |
| R3 | 10 kΩ nach +5V | C25804 | hält Q1 aus, wenn Q2 sperrt |

Optional sind 100 nF an POGO_5V nach GND, um die Flanke beim Einschalten etwas weicher zu machen.

## Einbau ohne neue Platine

J1 (HX-XH2.54, 2-polig) führt per Kabel zu den Pogo-Pins. So geht der Umbau am einfachsten:
1. Die Plus-Ader des Pogo-Kabels auftrennen.
2. Q1 auf einem kleinen Lochraster- oder SOT-23-Adapterplättchen dazwischensetzen: Source zu J1, Drain zu den Pogo-Pins.
3. Q2, R1 und R2 auf dasselbe Plättchen setzen.
4. Drei Drähte zur Platine legen: GPIO2, 3V3 und GND.
5. Den 5-V-Anschluss für R3 an der Quelle von Q1 abgreifen (J1 Pin 1).

Für eine spätere Platinenrevision gehört der Schalter zwischen F1 und J1 Pin 1.

## Hinweise

- **Rückspeisung:** Der P-MOSFET hat eine Body-Diode von POGO_5V nach +5V. Ist der Schalter aus, kann Strom nur
  zurückfließen, wenn an den Pogo-Pins mehr als 5 V anliegen. Bei unserer Remote (TP4056-Eingang, keine
  Rückspeisung) kommt das nicht vor.
- **Polyfuse F1** bleibt davor und schützt weiterhin bei Kurzschluss der offenen Kontakte.
- **Fertige Alternative auf einer Platine:** ein Load-Switch-IC mit aktiv-HIGH-Enable, z. B. TPS22918 (bis 5,5 V)
  oder ein USB-Power-Switch mit Strombegrenzung wie der AP22802. Für die Nachrüstung ist die MOSFET-Lösung einfacher.
- **Smart Plug:** Nach dem Umbau kann die Steckdose dauerhaft an bleiben. Wecken geht dann über
  `button.dock_wz_remote_wecken`. Den Lovelace-Button bitte entsprechend umstellen.
