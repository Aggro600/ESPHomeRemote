#!/bin/sh
# Baut die Streamer-App ohne Gradle (2026-10-07). Braucht: JDK 17 (apk add openjdk17-jdk gcompat libstdc++),
# Android build-tools 35 und platform-35 (android.jar). Pfad zu den Werkzeugen: $SDK (Standard s. unten).
#   sh bauen.sh   -> build/esphomeremote-streamer.apk
set -e
cd "$(dirname "$0")"
SDK=${SDK:-/tmp/claude-0/-root/eec25759-c78f-4b03-ab81-253c83138bed/scratchpad/android}
BT=$SDK/android-15            # build-tools 35.0.1
JAR=$SDK/android-35/android.jar
VERSION=${VERSION:-1}
rm -rf build && mkdir -p build/classes build/dex build/res
javac -source 11 -target 11 -encoding UTF-8 -nowarn -classpath "$JAR" -d build/classes $(find src -name '*.java') 2>&1 | grep -v "^warning\|bootstrap\|^Note:\|^1 warning" || true
[ -n "$(find build/classes -name '*.class')" ] || { echo "javac fehlgeschlagen"; exit 1; }
java -cp "$BT/lib/d8.jar" com.android.tools.r8.D8 --release --min-api 29 --lib "$JAR" --output build/dex $(find build/classes -name '*.class')
"$BT/aapt2" compile --dir res -o build/res/res.zip
"$BT/aapt2" link -I "$JAR" --manifest AndroidManifest.xml --min-sdk-version 29 --target-sdk-version 34 \
  --version-code "$VERSION" --version-name "1.$VERSION" -o build/ohne_dex.apk build/res/res.zip
python3 -c "import zipfile,sys; z=zipfile.ZipFile('build/ohne_dex.apk','a'); z.write('build/dex/classes.dex','classes.dex'); z.close()"
"$BT/zipalign" -f -p 4 build/ohne_dex.apk build/ausgerichtet.apk
[ -f schluessel.jks ] || keytool -genkeypair -keystore schluessel.jks -storepass esphomeremote -keypass esphomeremote \
  -alias app -keyalg RSA -keysize 2048 -validity 20000 -dname "CN=ESPHomeRemote Streamer" >/dev/null 2>&1
java -jar "$BT/lib/apksigner.jar" sign --ks schluessel.jks --ks-pass pass:esphomeremote --key-pass pass:esphomeremote \
  --out build/esphomeremote-streamer.apk build/ausgerichtet.apk
java -jar "$BT/lib/apksigner.jar" verify build/esphomeremote-streamer.apk && ls -la build/esphomeremote-streamer.apk
