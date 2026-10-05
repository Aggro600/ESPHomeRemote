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

static const char *const SD_UI_HTML = R"SDUI(<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>SD-Karte</title>
<style>
body{font-family:-apple-system,sans-serif;background:#0e1116;color:#f1f3f5;margin:0;padding:16px}
h1{font-size:18px;margin:0 0 10px}
#path{color:#8b94a3;margin-bottom:8px;word-break:break-all;font-size:13px}
table{width:100%;border-collapse:collapse}
td{padding:7px 4px;border-bottom:1px solid #232a35;font-size:14px}
a{color:#3b82f6;text-decoration:none;cursor:pointer}
button{background:#232a35;color:#f1f3f5;border:1px solid #3a424e;border-radius:6px;padding:5px 10px;cursor:pointer;font-size:13px}
button:hover{background:#2a3340}
#drop{border:2px dashed #3a424e;border-radius:10px;padding:22px;text-align:center;margin:14px 0;color:#8b94a3}
#drop.over{border-color:#3b82f6;color:#f1f3f5}
#msg{white-space:pre-wrap;font-size:12px;color:#8b94a3;margin-top:10px;max-height:160px;overflow:auto}
.size{color:#8b94a3;font-size:12px;white-space:nowrap}
</style>
<h1>SD-Karte</h1>
<div id="path"></div>
<table id="tbl"></table>
<div id="drop">Dateien hierher ziehen oder <label style="color:#3b82f6;cursor:pointer">ausw&auml;hlen<input id="file" type="file" multiple style="display:none"></label></div>
<div id="msg"></div>
<script>
let cur = "/";
const tbl = document.getElementById('tbl'), pathEl = document.getElementById('path'), msg = document.getElementById('msg');
function log(t){ msg.textContent = t + "\n" + msg.textContent; }
function parent(p){
  let s = p.endsWith('/') ? p.slice(0, -1) : p;
  const i = s.lastIndexOf('/');
  return i <= 0 ? '/' : s.slice(0, i);
}
async function list(p){
  cur = p;
  pathEl.textContent = p;
  const r = await fetch('/sd/api/list?path=' + encodeURIComponent(p));
  if (!r.ok) { log('Auflisten fehlgeschlagen: ' + await r.text()); return; }
  const items = await r.json();
  items.sort((a, b) => (b.dir - a.dir) || a.name.localeCompare(b.name));
  tbl.innerHTML = '';
  if (p !== '/') {
    const up = parent(p);
    tbl.innerHTML += '<tr><td colspan=3><a onclick="list(\'' + up + '\')">.. (hoch)</a></td></tr>';
  }
  for (const it of items) {
    const full = (p.endsWith('/') ? p : p + '/') + it.name;
    if (it.dir) {
      tbl.innerHTML += '<tr><td><a onclick="list(\'' + full + '\')">' + it.name + '/</a></td><td></td><td></td></tr>';
    } else {
      tbl.innerHTML += '<tr><td>' + it.name + '</td><td class=size>' + it.size + ' B</td>' +
        '<td><button onclick="del(\'' + full + '\')">l&ouml;schen</button></td></tr>';
    }
  }
}
async function del(path){
  if (!confirm('Wirklich löschen?\n' + path)) return;
  const r = await fetch('/sd/api/delete?path=' + encodeURIComponent(path), {method: 'POST'});
  log((r.ok ? 'OK ' : 'FEHLER ') + path);
  list(cur);
}
async function upload(file){
  const path = (cur.endsWith('/') ? cur : cur + '/') + file.name;
  log('lade hoch: ' + path + ' (' + file.size + ' B) ...');
  // Als rohen Koerper senden, KEIN multipart/form-data - der Geraete-HTTP-Server kann nur
  // application/octet-stream o.ae. verarbeiten (s. Kommentar in sd_http.cpp). fetch() setzt
  // bei einem File/Blob als body automatisch dessen eigenen Content-Type, nie multipart.
  const r = await fetch('/sd/api/file?path=' + encodeURIComponent(path), {method: 'POST', body: file});
  log((r.ok ? 'OK ' : 'FEHLER ') + await r.text());
  list(cur);
}
document.getElementById('file').addEventListener('change', e => {
  for (const f of e.target.files) upload(f);
  e.target.value = '';
});
const drop = document.getElementById('drop');
drop.addEventListener('dragover', e => { e.preventDefault(); drop.classList.add('over'); });
drop.addEventListener('dragleave', () => drop.classList.remove('over'));
drop.addEventListener('drop', e => {
  e.preventDefault();
  drop.classList.remove('over');
  for (const f of e.dataTransfer.files) upload(f);
});
list('/');
</script>
)SDUI";

void SdHttp::handleRequest(AsyncWebServerRequest *request) {
  char urlbuf[AsyncWebServerRequest::URL_BUF_SIZE];
  StringRef uref = request->url_to(urlbuf);
  std::string u(uref.c_str());
  std::string ep = u.substr(strlen(PREFIX));  // "info" | "list" | "file" | "delete"
  http_method m = request->method();

  if (this->sd_ == nullptr) {
    request->send(500, "text/plain", "kein SD");
    return;
  }

  // ---- POST /delete?path=... : loescht wie DELETE /file, aber ueber eine Methode, die der
  // zugrundeliegende HTTP-Server tatsaechlich durchlaesst (s. Kommentar in sd_http.h) ----
  if (ep == "delete" && m == HTTP_POST) {
    std::string path = this->req_path_(request);
    if (!safe_path_(path)) {
      request->send(400, "text/plain", "unsicherer Pfad");
      return;
    }
    if (!this->sd_->is_mounted() && !this->sd_->mount()) {
      request->send(503, "text/plain", "SD nicht gemountet");
      return;
    }
    bool ok = this->sd_->remove_file(path);
    request->send(ok ? 200 : 500, "text/plain", ok ? "geloescht" : "Fehler");
    return;
  }

  // ---- POST /file : Abschluss (Body kam ueber handleBody) ----
  if (ep == "file" && m == HTTP_POST) {
    if (this->up_started_ && this->up_ok_) {
      std::string msg = "OK " + std::to_string(this->up_written_) + " B -> " + this->up_path_;
      request->send(200, "text/plain", msg.c_str());
    } else {
      // Haeufigste Ursache (live nachgewiesen 2026-09-27): der Client hat den Standard-Content-Type
      // von "curl --data-binary" (application/x-www-form-urlencoded) nicht ueberschrieben - dann
      // versucht der zugrundeliegende HTTP-Server, den Koerper selbst als Formular zu zerlegen, statt
      // ihn roh an handleBody() weiterzugeben. Abhilfe: Content-Type explizit auf etwas anderes setzen,
      // z.B. application/octet-stream (macht tools/sd_upload.py automatisch).
      request->send(400, "text/plain",
        "Upload fehlgeschlagen. Haeufigster Grund: Content-Type war application/x-www-form-urlencoded "
        "(curl-Standard bei --data-binary) - explizit auf z.B. application/octet-stream setzen. "
        "Sonst pruefen: Pfad korrekt? SD gemountet?");
    }
    this->up_started_ = false;
    return;
  }

  if (!this->sd_->is_mounted() && !this->sd_->mount()) {
    request->send(503, "text/plain", "SD nicht gemountet");
    return;
  }

  // ---- GET /ui : kleine Weboberflaeche zum Ansehen/Hoch-/Runterladen/Loeschen im Browser
  // (Nutzerwunsch 2026-09-27: nicht nur per curl/Skript, sondern direkt ueber die IP im
  // Browser). Braucht kein gemountetes SD - list() im Browser zeigt dann einfach nichts.
  if (ep == "ui" && m == HTTP_GET) {
    request->send(200, "text/html", SD_UI_HTML);
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
