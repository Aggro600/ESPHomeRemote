🇬🇧 English · 🇩🇪 [Deutsch](README.de.md)

<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="logo-dunkel.png">
    <img src="logo.png" alt="Z906 Bridge" width="420">
  </picture>
</p>

# Logitech Z906 on Wi-Fi (ESPHome)

An ESP32 sits between the console and the amplifier of a Logitech Z906 and brings the speaker system into
Home Assistant – **without** replacing the console. The console works exactly as before; the ESP32 passes every
byte through in both directions and reads along.

**In Home Assistant:** main volume, subwoofer, centre and rear level (number, %), input 1–6 and effect
(3D / 2.1 / 4.1 / off) as selects, power state, a "read status" button.

**What it takes care of**
- Passes everything through from the very first moment (own task), so the console starts regardless of when the
  IR command or the power button comes.
- **Start-up protection:** turning the volume knob while the console is powering on used to make the console abort
  its start. Level steps during the wake-up sequence are held back, and the bridge sends nothing of its own for
  4 s after power-on (the console reads the status itself then).
- Commands from Home Assistant are only sent when the line is idle and no reply is pending (the amplifier can't
  handle overlapping requests).
- Input changes like the console does them: mute, switch input, restore the input's effect, unmute.
- Interference filter for the garbage the amplifier sends while it boots.

**Setup**
1. Wire the ESP32 as described at the top of [`logi-z906.yaml`](logi-z906.yaml) (DE-15 between console and
   amplifier, own USB power supply for the ESP32).
2. Copy `logi-z906.yaml` into your ESPHome folder, add `api_key_z906`, `ota_pw`, `wifi_ssid` and `wifi_password`
   to your `secrets.yaml` and install it. The component comes straight from GitHub (`external_components`).
3. Add the device in Home Assistant (ESPHome integration).

Protocol reference: [nomis/logitech-z906](https://github.com/nomis/logitech-z906/blob/main/protocol.rst).
Idea and wiring: [Jupsi/logi_z906_wifi](https://github.com/Jupsi/logi_z906_wifi).

> Known issue on one unit: switching to an input with the 3D effect made the amplifier reset – also with older
> firmware of the bridge. If you see the same, please test once with the console plugged directly into the
> amplifier (without the bridge) and report the result.
