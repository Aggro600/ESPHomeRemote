# Contributing

Thanks for helping. This page tells you how the project is put together, how to run it without owning a remote,
and what a good pull request looks like. German is fine in issues and discussions; code comments are German, user-facing
texts are German with English in `frontend/i18n/en.json` (see below).

## Where things live

| Part | Path | What it is |
|---|---|---|
| Integration | `custom_components/esphomeremote/` | Home Assistant integration: configurator (sidebar panel), firmware builder, dock bridge, entities |
| Configurator UI | `custom_components/esphomeremote/frontend/configurator.js` | One web component, plain JavaScript, no build step |
| Remote firmware | `esphomeremote-firmware.yaml` + `esphomeremote-*.h` | ESPHome configuration and C++ parts for the remote (ESP32-S3) |
| Dock firmware | `esphomeremote-dock-*.yaml` | ESPHome configuration for the dock (ESP32-C3) and for any spare ESP32 |
| ESPHome components | `components/` | Custom ESPHome components (menu, key configuration, dock radio) |
| Streamer app | `streamer-app/` | Android TV app (menu on the TV, cover art, keyboard popup) |
| Tests | `tests/` | pytest for the integration, run on every push |
| Docs | `docs/` | Hardware notes (IR LEDs, charging contacts), development notes under `docs/development/` |

This repository is **generated**: the maintainer's working copy lives in a private Home Assistant instance and
`tools/publish.py` writes this public version (it strips names, addresses and keys). Pull requests against this
repository are welcome anyway – they are merged back by hand. Please keep them focused.

## Run the integration without hardware

You need Python 3.13 and nothing else. The tests start a Home Assistant without network, Supervisor or ESPHome add-on:

```sh
pip install pytest-homeassistant-custom-component
pip install $(python3 tests/ha_requirements.py)
pytest tests/ -c tests/pytest.ini
```

To click through the configurator, run a throwaway Home Assistant in a virtualenv:

```sh
pip install homeassistant
mkdir ha && cp -r custom_components ha/
hass -c ha --skip-pip-packages pymicro-vad,pyspeex-noise
```

Open http://localhost:8123, finish onboarding, add the integration (Settings → Devices & services → Add → ESPHomeRemote)
and type any remote name. The configurator appears in the sidebar as **Remote** with a default key layout and menu.
Firmware building needs the ESPHome add-on and is therefore unavailable here – everything else works.

## Firmware

Compile without flashing (needs `pip install esphome`):

```sh
python3 .github/ci/prepare.py . .          # makes ci-remote.yaml / ci-dock.yaml from the templates
esphome compile ci-remote.yaml
esphome compile ci-dock.yaml
```

This is exactly what CI does. A change to the firmware must compile for both.

## Texts and languages

- Code and comments are German. User-facing strings in the configurator are German in the code and translated at
  runtime through `frontend/i18n/en.json` (German text → English). If you add or change a visible string, add the
  English line there. Whole sentences are matched first, then known fragments; patterns with numbers go into `__regeln`.
- Home Assistant texts (config flow, repairs, services) live in `strings.json` with `translations/en.json` and
  `translations/de.json`. The test suite checks that the three have the same keys.

## Pull requests

- One topic per PR. Say what the user sees differently, not only what the code does.
- Run `pytest tests/` – it takes a few seconds.
- No personal data: no IP addresses, MAC addresses, Wi-Fi names, people's names in code, comments or screenshots.
- Firmware changes: say which hardware you tested on (remote Rev6, dock Rev6, spare ESP32) – or that you could not.

## Releases

Releases are cut by the maintainer with `tools/release.py`: version `YYYY.M.D.n`, bilingual CHANGELOG entry, three
test compiles, tag `vYYYY.M.D.n`, branch `stabil` (what HACS and the devices follow) and the GitHub release with
the Streamer app. You do not need to touch versions in a PR.

## Reporting bugs

Please attach the diagnostics file: Settings → Devices & services → ESPHomeRemote → ⋮ → **Download diagnostics**.
It contains versions, firmware state and the last build errors, with device names, addresses and your key layout
removed.
