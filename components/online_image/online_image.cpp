#include "online_image.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>
#include <strings.h>

#include <JPEGDEC.h>
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *const TAG = "online_image";

namespace esphome::online_image {

// Groesste Datei, die angenommen wird (Cover 12-21 KB, Kamerabilder etwas mehr)
static constexpr size_t MAX_BYTES = 512 * 1024;
// Gesamtzeit fuer einen Abruf; die http_request-Zeitgrenze gilt pro Warten auf Daten
static constexpr uint32_t MAX_GESAMT_MS = 30000;

// ---- Hintergrund-Task (einer fuer alle Bilder) --------------------------------------------------

static QueueHandle_t g_queue = nullptr;

static uint8_t *psram_alloc(size_t n) {
  auto *p = static_cast<uint8_t *>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (p == nullptr)
    p = static_cast<uint8_t *>(heap_caps_malloc(n, MALLOC_CAP_8BIT));
  return p;
}

static esp_err_t http_event(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_HEADER && evt->user_data != nullptr) {
    auto *a = static_cast<Auftrag *>(evt->user_data);
    if (strcasecmp(evt->header_key, "etag") == 0) {
      a->etag = evt->header_value;
    } else if (strcasecmp(evt->header_key, "last-modified") == 0) {
      a->last_modified = evt->header_value;
    }
  }
  return ESP_OK;
}

struct Ziel {
  uint16_t *pixel;
  int w, h;
};

static int jpeg_zeichnen(JPEGDRAW *d) {
  auto *z = static_cast<Ziel *>(d->pUser);
  if (d->x >= z->w)
    return 1;
  const int breite = std::min(d->iWidth, z->w - d->x);
  for (int r = 0; r < d->iHeight; r++) {
    const int y = d->y + r;
    if (y >= z->h)
      break;
    memcpy(z->pixel + (size_t) y * z->w + d->x, d->pPixels + (size_t) r * d->iWidth, (size_t) breite * 2);
  }
  return 1;
}

// Laedt a.url und dekodiert das JPEG nach a.pixel. Laeuft im Task, fasst nur den Auftrag an.
static void laden(Auftrag &a) {
  const uint32_t t0 = millis();
  esp_http_client_config_t cfg = {};
  cfg.url = a.url.c_str();
  cfg.method = HTTP_METHOD_GET;
  cfg.timeout_ms = (int) a.timeout_ms;
  cfg.buffer_size = 2048;
  cfg.event_handler = http_event;
  cfg.user_data = &a;
  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c == nullptr) {
    a.fehler = "HTTP-Client nicht angelegt";
    return;
  }
  for (auto &h : a.headers)
    esp_http_client_set_header(c, h.first.c_str(), h.second.c_str());

  uint8_t *roh = nullptr;
  size_t n = 0;
  if (esp_http_client_open(c, 0) != ESP_OK) {
    a.fehler = "Verbindung fehlgeschlagen";
  } else {
    const int64_t laenge = esp_http_client_fetch_headers(c);
    a.http_code = esp_http_client_get_status_code(c);
    if (a.http_code == HTTP_CODE_NOT_MODIFIED) {
      // nichts zu tun
    } else if (a.http_code != HTTP_CODE_OK) {
      a.fehler = "HTTP-Fehler";
    } else if (laenge > (int64_t) MAX_BYTES) {
      a.fehler = "Datei zu gross";
    } else {
      size_t cap = laenge > 0 ? (size_t) laenge : 32 * 1024;
      roh = psram_alloc(cap);
      while (roh != nullptr) {
        if (n == cap) {
          if (laenge > 0 || cap >= MAX_BYTES) {
            if (laenge <= 0)
              a.fehler = "Datei zu gross";
            break;
          }
          const size_t neu = std::min(cap * 2, MAX_BYTES);
          auto *p = psram_alloc(neu);
          if (p != nullptr)
            memcpy(p, roh, n);
          heap_caps_free(roh);
          roh = p;
          cap = neu;
          continue;
        }
        const int r = esp_http_client_read(c, reinterpret_cast<char *>(roh + n), (int) (cap - n));
        if (r > 0) {
          n += r;
        } else if (r == 0) {
          if (!esp_http_client_is_complete_data_received(c))
            a.fehler = "Verbindung vorzeitig beendet";
          break;
        } else {
          a.fehler = "Zeitueberschreitung beim Lesen";
          break;
        }
        if (millis() - t0 > MAX_GESAMT_MS) {
          a.fehler = "Download dauert zu lange";
          break;
        }
      }
      if (roh == nullptr)
        a.fehler = "kein Speicher fuer den Download";
      else if (a.fehler == nullptr && laenge > 0 && n != (size_t) laenge)
        a.fehler = "Datei unvollstaendig";
    }
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  a.bytes = n;

  if (a.fehler == nullptr && a.http_code == HTTP_CODE_OK) {
    auto *j = new JPEGDEC();
    if (!j->openRAM(roh, (int) n, jpeg_zeichnen)) {
      a.fehler = "kein gueltiges JPEG";
    } else if (j->getJPEGType() != JPEG_MODE_BASELINE) {
      a.fehler = "nur Baseline-JPEG wird unterstuetzt";
      j->close();
    } else {
      a.w = j->getWidth();
      a.h = j->getHeight();
      a.pixel = psram_alloc((size_t) a.w * a.h * 2);
      if (a.pixel == nullptr) {
        a.fehler = "kein Speicher fuer das Bild";
      } else {
        memset(a.pixel, 0, (size_t) a.w * a.h * 2);
        Ziel z{reinterpret_cast<uint16_t *>(a.pixel), a.w, a.h};
        j->setUserPointer(&z);
        j->setPixelType(a.big_endian ? RGB565_BIG_ENDIAN : RGB565_LITTLE_ENDIAN);
        if (!j->decode(0, 0, 0))
          a.fehler = "Fehler beim Dekodieren";
      }
      j->close();
    }
    delete j;
  }
  if (roh != nullptr)
    heap_caps_free(roh);
  a.ms = millis() - t0;
}

static void task_fn(void *) {
  OnlineImage *img;
  for (;;) {
    if (xQueueReceive(g_queue, &img, portMAX_DELAY) == pdTRUE) {
      laden(img->auftrag());
      img->fertig_();
    }
  }
}

static bool task_starten() {
  if (g_queue != nullptr)
    return true;
  g_queue = xQueueCreate(8, sizeof(OnlineImage *));
  if (g_queue == nullptr)
    return false;
  // Prioritaet 1 = wie die Hauptschleife, ohne Kernbindung: der Task bekommt Rechenzeit, ohne sie zu verdraengen
  if (xTaskCreatePinnedToCore(task_fn, "online_img", 6144, nullptr, 1, nullptr, tskNO_AFFINITY) != pdPASS) {
    vQueueDelete(g_queue);
    g_queue = nullptr;
    return false;
  }
  return true;
}

// ---- Komponente (Hauptschleife) -----------------------------------------------------------------

OnlineImage::OnlineImage(const std::string &url, int width, int height, runtime_image::ImageFormat format,
                         image::ImageType type, image::Transparency transparency, image::Image *placeholder,
                         uint32_t buffer_size, bool is_big_endian)
    : RuntimeImage(format, type, transparency, placeholder, is_big_endian, width, height) {
  this->set_url(url);
}

bool OnlineImage::validate_url_(const std::string &url) {
  if (url.empty()) {
    ESP_LOGE(TAG, "URL is empty");
    return false;
  }
  if (url.length() > 2048) {
    ESP_LOGE(TAG, "URL is too long");
    return false;
  }
  if (!url.starts_with("http://") && !url.starts_with("https://")) {
    ESP_LOGE(TAG, "URL must start with http:// or https://");
    return false;
  }
  return true;
}

void OnlineImage::freigeben_(Auftrag &a) {
  if (a.pixel != nullptr)
    heap_caps_free(a.pixel);
  a.pixel = nullptr;
}

void OnlineImage::update() {
  if (this->state_.load() != FREI) {
    // Laeuft noch: danach mit der dann aktuellen URL erneut laden statt den Wunsch zu verlieren
    this->nochmal_ = true;
    return;
  }
  if (!this->validate_url_(this->url_)) {
    ESP_LOGE(TAG, "Invalid URL: %s", this->url_.c_str());
    this->download_error_callback_.call();
    return;
  }
  if (this->get_format() != runtime_image::JPEG || this->get_type() != image::IMAGE_TYPE_RGB565 ||
      this->transparency_ != image::TRANSPARENCY_OPAQUE || this->fixed_width_ != 0 || this->fixed_height_ != 0) {
    ESP_LOGE(TAG, "Diese Fassung kann nur JPEG -> RGB565 ohne Transparenz und ohne resize");
    this->download_error_callback_.call();
    return;
  }
  if (!task_starten()) {
    ESP_LOGE(TAG, "Download-Task konnte nicht gestartet werden");
    this->download_error_callback_.call();
    return;
  }

  Auftrag &a = this->auftrag_;
  a = Auftrag();
  a.url = this->url_;
  a.timeout_ms = this->parent_->get_timeout();
  a.big_endian = this->is_big_endian_;
  if (!this->etag_.empty())
    a.headers.emplace_back("If-None-Match", this->etag_);
  if (!this->last_modified_.empty())
    a.headers.emplace_back("If-Modified-Since", this->last_modified_);
  a.headers.emplace_back("Accept", "image/jpeg,*/*;q=0.8");
  for (auto &h : this->request_headers_)
    a.headers.emplace_back(h.first, h.second.value());

  ESP_LOGD(TAG, "Updating image from %s", this->url_.c_str());
  this->verwerfen_ = false;
  this->state_.store(WARTET);
  OnlineImage *self = this;
  if (xQueueSend(g_queue, &self, 0) != pdTRUE) {
    this->state_.store(FREI);
    ESP_LOGE(TAG, "Download-Warteschlange voll");
    this->download_error_callback_.call();
    return;
  }
  this->enable_loop();
}

void OnlineImage::loop() {
  const uint8_t s = this->state_.load();
  if (s == WARTET)
    return;
  if (s == FREI) {
    this->disable_loop();
    return;
  }

  // FERTIG: Ergebnis uebernehmen, Auftrag freigeben - erst danach die Trigger (die duerfen update() rufen)
  Auftrag &a = this->auftrag_;
  enum { NICHTS, FEHLER, GECACHT, NEU } ergebnis = NICHTS;
  if (this->verwerfen_) {
    ESP_LOGD(TAG, "Download verworfen (release)");
  } else if (a.fehler != nullptr) {
    ESP_LOGE(TAG, "Bild nicht geladen: %s (HTTP %d, %zu Byte, %" PRIu32 " ms)", a.fehler, a.http_code, a.bytes,
             a.ms);
    ergebnis = FEHLER;
  } else if (a.http_code == HTTP_CODE_NOT_MODIFIED) {
    ESP_LOGI(TAG, "Server returned HTTP 304 (Not Modified). Download skipped.");
    ergebnis = GECACHT;
  } else if (this->resize_buffer_(a.w, a.h) == 0) {
    ESP_LOGE(TAG, "Kein Speicher fuer %dx%d", a.w, a.h);
    ergebnis = FEHLER;
  } else {
    memcpy(this->buffer_, a.pixel, (size_t) a.w * a.h * 2);
    this->width_ = this->buffer_width_;
    this->height_ = this->buffer_height_;
    this->data_start_ = this->buffer_;
    if (a.url == this->url_) {
      this->etag_ = a.etag;
      this->last_modified_ = a.last_modified;
    }
    ESP_LOGD(TAG, "Image fully downloaded, %zu bytes, %dx%d in %" PRIu32 " ms (Hintergrund)", a.bytes, a.w, a.h,
             a.ms);
    ergebnis = NEU;
  }
  freigeben_(a);
  this->verwerfen_ = false;
  this->state_.store(FREI);

  if (ergebnis == FEHLER)
    this->download_error_callback_.call();
  else if (ergebnis == GECACHT)
    this->download_finished_callback_.call(true);
  else if (ergebnis == NEU)
    this->download_finished_callback_.call(false);

  if (this->nochmal_ && this->state_.load() == FREI) {
    this->nochmal_ = false;
    this->update();
  }
  if (this->state_.load() == FREI)
    this->disable_loop();
}

void OnlineImage::release() {
  this->etag_ = "";
  this->last_modified_ = "";
  if (this->state_.load() != FREI)
    this->verwerfen_ = true;
  this->nochmal_ = false;
  RuntimeImage::release();
}

}  // namespace esphome::online_image
