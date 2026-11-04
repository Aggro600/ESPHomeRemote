# ESPHomeRemote Streamer app

`esphomeremote-streamer.apk` is built from the source in [`streamer-app/`](../../../streamer-app) of this repository
(`AndroidManifest.xml`, `src/`, `res/`, `build.sh`). The workflow `.github/workflows/streamer-app.yml` builds it from
that source on every change, so the build can be checked; the version is in `version.json`.

What it does on the TV/streamer, and why it asks for the accessibility service and the notification listener, is
described in [`streamer-app/README.md`](../../../streamer-app/README.md).
