#pragma once

// Zeitlich begrenzter HTTP-Endpunkt zum Hoch-/Runterladen der Konfigdateien
// (runtime.json / menu.json / learned.json) auf die SD-Karte.
//
// Design-Entscheidung: KEIN dauerhaft laufender Webserver (Stromkosten), sondern
// ein "Konfig-Modus" - per HA-Button/Schalter fuer ein paar Minuten an, dann
// automatisch wieder aus. Laeuft auf der Stations-IP im Heimnetz (bzw. auf der
// Fallback-AP-IP, wenn kein WLAN da ist).
//
// Endpunkte (Query-Parameter ?path=/menu.json, Pfad muss mit / beginnen, kein ".."):
//   GET    /sd/api/info                 -> {mounted,size,free,name}
//   GET    /sd/api/list?path=/          -> [{name,dir,size}, ...]
//   GET    /sd/api/file?path=/menu.json -> Dateiinhalt (text/plain)
//   POST   /sd/api/file?path=/menu.json -> Body (application/json o. text/plain
//                                          o. octet-stream, NICHT form-urlencoded)
//                                          schreibt die Datei
//   DELETE /sd/api/file?path=/x.json    -> loescht die Datei
//
// Sicherheit: waehrend des Fensters ist der Endpunkt im LAN offen. Fenster kurz
// halten. (Ein optionaler Key liesse sich hier spaeter ergaenzen.)

#ifdef USE_ESP32
#include "esphome/core/defines.h"
#if defined(USE_NETWORK) && !defined(USE_ZEPHYR)

#include "sd_card.h"
#include "esphome/components/web_server_base/web_server_base.h"
#include <string>

namespace esphome {
namespace sd_card {

class SdHttp : public AsyncWebHandler {
 public:
  explicit SdHttp(SdCard *sd) : sd_(sd) {}

  bool canHandle(AsyncWebServerRequest *request) const override;
  void handleRequest(AsyncWebServerRequest *request) override;
  void handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) override;
  bool isRequestHandlerTrivial() const override { return false; }

 protected:
  static bool safe_path_(const std::string &p);
  std::string req_path_(AsyncWebServerRequest *request) const;

  SdCard *sd_;
  // Upload-Zustand (httpd ist single-threaded -> immer nur eine Anfrage)
  std::string up_path_;
  bool up_started_{false};
  bool up_ok_{false};
  size_t up_written_{0};
};

}  // namespace sd_card
}  // namespace esphome

#endif  // USE_NETWORK
#endif  // USE_ESP32
