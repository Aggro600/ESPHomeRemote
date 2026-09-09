# Host-Test für den ADPCM-Pfad

Der Encoder ist der Teil, der stillschweigend Müll produzieren kann, ohne dass
man es am Protokoll merkt. Der Test kodiert ein Testsignal genau so, wie
`AtvVoice::loop()` es tut (Frame-Header + Nibble-Packing), dekodiert jedes Frame
mit einem unabhängig geschriebenen Referenzdekoder **nur aus dem Frame-Header**
und misst den Fehler. Damit ist gleichzeitig geprüft, dass ein verlorenes Frame
den Rest des Streams nicht mitreißt.

```
g++ -std=c++17 -O2 -o adpcm_test adpcm_test.cpp && ./adpcm_test
# frames=40  SNR=30.1 dB
```

Alles unter ~20 dB heißt: der Encoder folgt dem Eingangssignal nicht.
