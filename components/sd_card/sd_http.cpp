#include "sd_http.h"

#ifdef USE_ESP32
#if defined(USE_NETWORK) && !defined(USE_ZEPHYR)

#include "esphome/core/log.h"
#include "esphome/core/application.h"

namespace esphome {
namespace sd_card {

static const char *const TAG = "sd_card.http";
static const char *const PREFIX = "/sd/api/";

void SdCard::http_enable(uint32_t timeout_s) {
  auto *wsb = web_server_base::global_web_server_base;
  if (wsb == nullptr) {
    ESP_LOGW(TAG, "kein web_server_base - Konfig-HTTP nicht moeglich");
    return;
  }
  if (!this->http_registered_) {
    this->http_handler_ = new SdHttp(this);  // NOLINT - lebt bis Reboot
    wsb->add_handler(this->http_handler_);
    this->http_registered_ = true;
  }
  if (!this->http_active_) {
    wsb->init();  // refcount 0->1: Listener starten
    this->http_active_ = true;
    ESP_LOGI(TAG, "Konfig-HTTP AN (Port %u), Endpunkte unter /sd/api/", wsb->get_port());
  }
  // Auto-Aus neu aufziehen
  this->cancel_timeout("sd_http_off");
  if (timeout_s > 0) {
    this->set_timeout("sd_http_off", timeout_s * 1000, [this]() {
      ESP_LOGI(TAG, "Konfig-HTTP: Zeitfenster abgelaufen");
      this->http_disable();
    });
  }
}

void SdCard::http_disable() {
  this->cancel_timeout("sd_http_off");
  if (!this->http_active_)
    return;
  if (web_server_base::global_web_server_base != nullptr)
    web_server_base::global_web_server_base->deinit();  // refcount 1->0: Listener stoppen
  this->http_active_ = false;
  ESP_LOGI(TAG, "Konfig-HTTP AUS");
}

bool SdHttp::safe_path_(const std::string &p) {
  if (p.empty() || p[0] != '/')
    return false;
  if (p.find("..") != std::string::npos)
    return false;
  return true;
}

std::string SdHttp::req_path_(AsyncWebServerRequest *request) const {
  auto *pp = request->getParam("path");
  if (pp == nullptr)
    return "";
  return pp->value();
}

bool SdHttp::canHandle(AsyncWebServerRequest *request) const {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef u = request->url_to(buf);
  if (u.size() < strlen(PREFIX) || strncmp(u.c_str(), PREFIX, strlen(PREFIX)) != 0)
    return false;
  http_method m = request->method();
  return m == HTTP_GET || m == HTTP_POST || m == HTTP_DELETE;
}

void SdHttp::handleBody(AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
  char buf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef u = request->url_to(buf);
  if (std::string(u.c_str()) != std::string(PREFIX) + "file" || request->method() != HTTP_POST)
    return;

  if (index == 0) {
    this->up_started_ = true;
    this->up_ok_ = false;
    this->up_written_ = 0;
    this->up_path_ = this->req_path_(request);
    if (!safe_path_(this->up_path_)) {
      ESP_LOGW(TAG, "Upload: unsicherer Pfad '%s'", this->up_path_.c_str());
      this->up_path_.clear();
      return;
    }
    if (this->sd_ == nullptr || (!this->sd_->is_mounted() && !this->sd_->mount())) {
      ESP_LOGW(TAG, "Upload: SD nicht gemountet");
      this->up_path_.clear();
      return;
    }
    ESP_LOGI(TAG, "Upload -> %s (%u B)", this->up_path_.c_str(), (unsigned) total);
  }

  if (this->up_path_.empty())
    return;

  if (index == 0 && !this->sd_->write_open(this->up_path_)) {
    ESP_LOGW(TAG, "Upload: Datei konnte nicht angelegt werden: %s", this->up_path_.c_str());
    this->up_path_.clear();
    return;
  }
  if (!this->sd_->write_chunk(data, len)) {
    ESP_LOGW(TAG, "Upload: Schreibfehler bei Offset %u", (unsigned) index);
    this->sd_->write_close();
    this->up_path_.clear();
    return;
  }
  this->up_written_ += len;
  if (index + len >= total) {
    this->sd_->write_close();
    this->up_ok_ = true;
  }
}

void SdHttp::handleRequest(AsyncWebServerRequest *request) {
  char urlbuf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef uref = request->url_to(urlbuf);
  std::string u(uref.c_str());
  std::string ep = u.substr(strlen(PREFIX));  // "info" | "list" | "file"
  http_method m = request->method();

  if (this->sd_ == nullptr) {
    request->send(500, "text/plain", "kein SD");
    return;
  }

  // ---- POST /file : Abschluss (Body kam ueber handleBody) ----
  if (ep == "file" && m == HTTP_POST) {
    if (this->up_started_ && this->up_ok_) {
      std::string msg = "OK " + std::to_string(this->up_written_) + " B -> " + this->up_path_;
      request->send(200, "text/plain", msg.c_str());
    } else {
      request->send(400, "text/plain", "Upload fehlgeschlagen (Pfad? SD gemountet? Content-Type nicht form-urlencoded?)");
    }
    this->up_started_ = false;
    return;
  }

  if (!this->sd_->is_mounted() && !this->sd_->mount()) {
    request->send(503, "text/plain", "SD nicht gemountet");
    return;
  }

  // ---- GET /info ----
  if (ep == "info" && m == HTTP_GET) {
    char b[220];
    snprintf(b, sizeof(b),
             "{\"mounted\":%s,\"name\":\"%s\",\"type\":\"%s\",\"size\":%llu,\"free\":%llu}",
             this->sd_->is_mounted() ? "true" : "false", this->sd_->card_name().c_str(),
             this->sd_->card_type().c_str(), (unsigned long long) this->sd_->card_size_bytes(),
             (unsigned long long) this->sd_->free_bytes());
    request->send(200, "application/json", b);
    return;
  }

  // ---- GET /list?path=/ ----
  if (ep == "list" && m == HTTP_GET) {
    std::string path = this->req_path_(request);
    if (path.empty())
      path = "/";
    if (!safe_path_(path)) {
      request->send(400, "text/plain", "unsicherer Pfad");
      return;
    }
    auto es = this->sd_->list_dir(path);
    std::string out = "[";
    for (size_t i = 0; i < es.size(); i++) {
      if (i)
        out += ",";
      out += "{\"name\":\"" + es[i].name + "\",\"dir\":" + (es[i].is_dir ? "true" : "false") +
             ",\"size\":" + std::to_string(es[i].size) + "}";
    }
    out += "]";
    request->send(200, "application/json", out.c_str());
    return;
  }

  // ---- GET/DELETE /file?path=... ----
  if (ep == "file") {
    std::string path = this->req_path_(request);
    if (!safe_path_(path)) {
      request->send(400, "text/plain", "unsicherer Pfad");
      return;
    }
    if (m == HTTP_GET) {
      if (!this->sd_->file_exists(path)) {
        request->send(404, "text/plain", "nicht gefunden");
        return;
      }
      std::string content;
      if (!this->sd_->read_text(path, content, 512 * 1024)) {
        request->send(500, "text/plain", "Lesefehler");
        return;
      }
      request->send(200, "text/plain", content.c_str());
      return;
    }
    if (m == HTTP_DELETE) {
      bool ok = this->sd_->remove_file(path);
      request->send(ok ? 200 : 500, "text/plain", ok ? "geloescht" : "Fehler");
      return;
    }
  }

  request->send(404, "text/plain", "unbekannter Endpunkt");
}

}  // namespace sd_card
}  // namespace esphome

#endif  // USE_NETWORK
#endif  // USE_ESP32
