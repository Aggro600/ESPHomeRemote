#pragma once
// Eigene Fassung von ESPHomes online_image (Basis 2026.9.1, Stand 2026-10-10).
//
// Warum: das Original laedt und dekodiert in der Hauptschleife. get() wartet auf die Antwort, jedes read()
// wartet auf Daten (blockierend bis zur http_request-Zeitgrenze), danach dekodiert JPEGDEC Pixel fuer Pixel ueber
// draw_pixel(). Ueber den Bluetooth-Tunnel dauert ein Cover mehrere Sekunden - so lange reagierte die Remote auf
// keine Taste und das Display stand.
//
// Jetzt: Download UND Dekodierung laufen in einem eigenen FreeRTOS-Task (einer fuer alle Bilder, Auftraege
// nacheinander - nie zwei Downloads gleichzeitig). Die Hauptschleife uebernimmt nur das fertige RGB565-Bild
// (memcpy, wenige ms) und ruft dann on_download_finished / on_error. YAML (set_url, release, Trigger) unveraendert.
// Bis das neue Bild da ist, bleibt das alte stehen. Unterstuetzt: JPEG -> RGB565 ohne Transparenz, ohne resize.

#include <atomic>
#include <string>
#include <utility>
#include <vector>

#include "esphome/components/http_request/http_request.h"
#include "esphome/components/runtime_image/runtime_image.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/defines.h"
#include "esphome/core/helpers.h"

namespace esphome::online_image {

using t_http_codes = enum {
  HTTP_CODE_OK = 200,
  HTTP_CODE_NOT_MODIFIED = 304,
  HTTP_CODE_NOT_FOUND = 404,
};

// Ein Auftrag fuer den Hintergrund-Task. Die Hauptschleife fuellt die Anfrage, der Task das Ergebnis; danach
// gehoert er wieder der Hauptschleife (state_ zeigt, wer gerade dran ist).
struct Auftrag {
  // Anfrage
  std::string url;
  std::vector<std::pair<std::string, std::string>> headers;
  uint32_t timeout_ms{4500};
  bool big_endian{false};
  // Ergebnis
  int http_code{0};
  std::string etag, last_modified;
  uint8_t *pixel{nullptr};   // RGB565, w*h*2 Byte, PSRAM
  int w{0}, h{0};
  size_t bytes{0};
  uint32_t ms{0};
  const char *fehler{nullptr};
};

class OnlineImage final : public PollingComponent,
                          public runtime_image::RuntimeImage,
                          public Parented<esphome::http_request::HttpRequestComponent> {
 public:
  OnlineImage(const std::string &url, int width, int height, runtime_image::ImageFormat format, image::ImageType type,
              image::Transparency transparency, image::Image *placeholder, uint32_t buffer_size,
              bool is_big_endian = false);

  void update() override;
  void loop() override;

  /** Set the URL to download the image from. */
  void set_url(const std::string &url) {
    if (this->validate_url_(url)) {
      this->url_ = url;
    }
    this->etag_ = "";
    this->last_modified_ = "";
  }

  /** Add the request header */
  template<typename V> void add_request_header(const std::string &header, V value) {
    this->request_headers_.push_back(std::pair<std::string, TemplatableValue<std::string>>(header, value));
  }

  /** Bild freigeben; ein laufender Download wird verworfen. */
  void release();

  template<typename F> void add_on_finished_callback(F &&callback) {
    this->download_finished_callback_.add(std::forward<F>(callback));
  }
  template<typename F> void add_on_error_callback(F &&callback) {
    this->download_error_callback_.add(std::forward<F>(callback));
  }

  // Zustand des Auftrags: Hauptschleife -> WARTET -> Task -> FERTIG -> Hauptschleife
  enum Zustand : uint8_t { FREI = 0, WARTET, FERTIG };
  Auftrag &auftrag() { return this->auftrag_; }
  void fertig_() { this->state_.store(FERTIG); }

 protected:
  bool validate_url_(const std::string &url);
  void uebernehmen_();
  static void freigeben_(Auftrag &a);

  CallbackManager<void(bool)> download_finished_callback_{};
  CallbackManager<void()> download_error_callback_{};

  std::string url_;
  std::vector<std::pair<std::string, TemplatableValue<std::string>>> request_headers_;
  std::string etag_;
  std::string last_modified_;

  Auftrag auftrag_;
  std::atomic<uint8_t> state_{FREI};
  bool nochmal_{false};    // update() kam, waehrend ein Download lief -> danach erneut laden (neueste URL)
  bool verwerfen_{false};  // release() waehrend eines Downloads -> Ergebnis wegwerfen
};

template<typename... Ts> class OnlineImageSetUrlAction final : public Action<Ts...> {
 public:
  OnlineImageSetUrlAction(OnlineImage *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, url)
  TEMPLATABLE_VALUE(bool, update)
  void play(const Ts &...x) override {
    this->parent_->set_url(this->url_.value(x...));
    if (this->update_.value(x...)) {
      this->parent_->update();
    }
  }

 protected:
  OnlineImage *parent_;
};

template<typename... Ts> class OnlineImageReleaseAction final : public Action<Ts...> {
 public:
  OnlineImageReleaseAction(OnlineImage *parent) : parent_(parent) {}
  void play(const Ts &...x) override { this->parent_->release(); }

 protected:
  OnlineImage *parent_;
};

}  // namespace esphome::online_image
