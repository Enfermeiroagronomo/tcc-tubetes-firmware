/*
  ============================================================
   TCC TUBETES — ESP32 + 4 sensores capacitivos de umidade do substrato
                 (1 tubete por tratamento) + DHT22 (ar)
   Firmware v1.0  —  Cristiano Alves dos Santos / ESALQ-USP
  ============================================================
  O QUE FAZ
   - Mede a umidade de 4 tubetes (T1..T4) a cada 2 s, com media aparada de
     64 amostras e filtro exponencial; guarda SEMPRE o valor bruto em mV
     (a calibracao pode ser refeita depois, no Colab, sem perder nada).
   - Mede temperatura e UR do ar (DHT22) e calcula o DPV (Tetens).
   - A cada N minutos (padrao 5, alinhado no relogio: :00, :05, :10...) grava
     uma linha de dados:
       1) na memoria da placa (arquivo /dados.csv, baixa pelo painel)
       2) no Google Sheets (historico permanente + painel para o celular)
          -> se a internet cair, as linhas ficam na fila e sao enviadas depois
   - Painel local no celular/PC: http://tubetes.local  (ou pelo IP)
   - Telegram (opcional): /status e alertas (sensor com problema, tubete secando)

  LIGACOES (placa ESP32 DevKit 38 pinos)
   Sensor solo T1: AOUT -> P34 | VCC -> 3V3 | GND -> GND
   Sensor solo T2: AOUT -> P35 | VCC -> 3V3 | GND -> GND
   Sensor solo T3: AOUT -> P32 | VCC -> 3V3 | GND -> GND
   Sensor solo T4: AOUT -> P33 | VCC -> 3V3 | GND -> GND
   DHT22 (ar):     out  -> P4  | +   -> 3V3 | -   -> GND
   SHT40 (ar, I2C 0x44): SDA -> P21 | SCL -> P22 | VCC -> 3V3 | GND -> GND  (v1.2: ocupa o "sensor 2")
   Use SO pinos ADC1 (32-39) para os sensores: ADC2 nao funciona com Wi-Fi.
   Alimente os sensores em 3V3: com 5V a saida pode passar de 3,3 V (limite do GPIO).
   Recomendado: resistor de 100 kOhm de cada AOUT para GND (sensor solto le ~0 e e detectado).

  PRIMEIRO USO
   1. No celular, conecte na rede "Tubetes-Setup" (senha: tubetes123)
   2. Se a pagina nao abrir sozinha, acesse 192.168.4.1 -> Configure WiFi -> sua rede 2,4 GHz
   3. Abra http://tubetes.local (ou o IP mostrado no Monitor Serial / roteador)
   Apagar Wi-Fi salvo: segure o botao IO0 (BOOT) por 5 s com a placa ligada.

  IDE: Placa "ESP32 Dev Module" | Partition Scheme: "Minimal SPIFFS (1.9MB APP with OTA)"
       (obrigatorio: o firmware tem ~1,37 MB e nao cabe na particao padrao)
  Bibliotecas: WiFiManager (tzapu), ArduinoJson (Benoit Blanchon),
               DHT sensor library + Adafruit Unified Sensor + Adafruit SHT4x (Adafruit)
  OTA (gravar pelo Wi-Fi): porta de rede "tubetes at <IP>", senha 1234
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFiManager.h>
#include <esp_task_wdt.h>
#include <esp_arduino_version.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <time.h>

#define FW_VERSAO "1.2.0"

// ---------------- IDENTIDADE / REDE ----------------
const char* HOSTNAME  = "tubetes";          // http://tubetes.local
const char* AP_NOME   = "Tubetes-Setup";
const char* AP_SENHA  = "tubetes123";
const char* OTA_SENHA = "1234";

// ---------------- PINOS ----------------
#define N_CANAIS 4
const uint8_t PIN_SOLO[N_CANAIS] = {34, 35, 32, 33};   // T1, T2, T3, T4 (todos ADC1)
#define PIN_DHT_A 4
#define PIN_SDA   21   // SHT40 (I2C, endereco 0x44)
#define PIN_SCL   22
#define PIN_LED   2
#define PIN_BOOT  0

// ---------------- CONFIGURACAO (salva na memoria NVS) ----------------
struct Canal {
  char    nome[24];   // ex.: "0% biochar"
  int16_t seco;       // mV no ponto SECO  (substrato seco ao ar, no tubete)
  int16_t molhado;    // mV no ponto MOLHADO (saturado e drenado 30 min = capacidade do recipiente)
  int8_t  alerta;     // avisa no Telegram se ficar abaixo deste % (0 = sem aviso)
  uint8_t ativo;      // 0 = canal desligado (sem sensor)
};
struct Config {
  uint32_t versao;
  Canal    c[N_CANAIS];
  uint16_t intervaloMin;   // registro a cada N min (1..60)
  char     pin[12];        // PIN do painel para alterar configuracoes
};
#define CFG_VERSAO 0x54554201UL
Config cfg;

void cfgPadrao() {
  memset(&cfg, 0, sizeof(cfg));
  cfg.versao = CFG_VERSAO;
  for (int i = 0; i < N_CANAIS; i++) {
    snprintf(cfg.c[i].nome, sizeof(cfg.c[i].nome), "Tratamento %d", i + 1);
    cfg.c[i].seco = 2400;       // valores tipicos em 3V3 - CALIBRAR
    cfg.c[i].molhado = 1000;
    cfg.c[i].alerta = 0;
    cfg.c[i].ativo = 1;
  }
  cfg.intervaloMin = 5;
  strcpy(cfg.pin, "1234");
}
void carregaCfg() {
  Preferences p; p.begin("tub", true);
  bool ok = false;
  if (p.getBytesLength("cfg") == sizeof(cfg)) { p.getBytes("cfg", &cfg, sizeof(cfg)); ok = (cfg.versao == CFG_VERSAO); }
  p.end();
  if (!ok) cfgPadrao();
  if (cfg.intervaloMin < 1 || cfg.intervaloMin > 60) cfg.intervaloMin = 5;
  for (int i = 0; i < N_CANAIS; i++) cfg.c[i].nome[sizeof(cfg.c[i].nome) - 1] = 0;
  cfg.pin[sizeof(cfg.pin) - 1] = 0;
}
void salvaCfg() { Preferences p; p.begin("tub", false); p.putBytes("cfg", &cfg, sizeof(cfg)); p.end(); }

// ---------------- ESTADO GERAL ----------------
WebServer server(80);
WiFiManager wm;
bool redeIniciada = false;
unsigned long ultimaTentativaWifi = 0;
volatile unsigned long netBeat = 0;

bool horaValida() { return time(nullptr) > 1700000000; }
uint32_t agoraEpoch() { return (uint32_t)time(nullptr); }

// ---------------- ALERTAS (fila entre os nucleos) ----------------
struct Alerta { char txt[240]; };
QueueHandle_t filaAlerta = nullptr;
void alerta(const char* fmt, ...) {
  Alerta a; va_list ap; va_start(ap, fmt); vsnprintf(a.txt, sizeof(a.txt), fmt, ap); va_end(ap);
  Serial.printf("[alerta] %s\n", a.txt);
  if (filaAlerta) xQueueSend(filaAlerta, &a, 0);
}

#include "sensores.h"
#include "armazenamento.h"
#include "google.h"
#include "grafana.h"
#include "telegram.h"
#include "pagina.h"

// ============================================================
//  ALERTAS AUTOMATICOS (nucleo 1)
// ============================================================
void verificaAlertas() {
  static bool avisadoFalha[N_CANAIS] = {false};
  static bool avisadoSeco[N_CANAIS] = {false};
  static bool avisadoAr = false;
  if (millis() < 60000UL) return;                         // espera estabilizar
  for (int i = 0; i < N_CANAIS; i++) {
    Solo& s = solo[i];
    if (s.est == EST_DESATIVADO) { avisadoFalha[i] = avisadoSeco[i] = false; continue; }
    bool falha = s.est != EST_OK && s.est != EST_INICIANDO;
    if (falha && !avisadoFalha[i] && millis() - s.desdeMs > 120000UL) {
      alerta("⚠️ T%d (%s): %s (%.0f mV). Verifique o cabo/sensor.", i + 1, cfg.c[i].nome, nomeEstado(s.est), s.mvInst);
      avisadoFalha[i] = true;
    } else if (!falha && avisadoFalha[i] && s.est == EST_OK) {
      alerta("✅ T%d (%s): sensor voltou ao normal (%.0f%%).", i + 1, cfg.c[i].nome, s.pct);
      avisadoFalha[i] = false;
    }
    int lim = cfg.c[i].alerta;
    if (lim > 0 && s.est == EST_OK && !isnan(s.pct)) {
      if (!avisadoSeco[i] && s.pct < lim) {
        alerta("🏜️ T%d (%s) secando: %.0f%% (limite %d%%).", i + 1, cfg.c[i].nome, s.pct, lim);
        avisadoSeco[i] = true;
      } else if (avisadoSeco[i] && s.pct > lim + 5) {
        alerta("💧 T%d (%s) reidratado: %.0f%%.", i + 1, cfg.c[i].nome, s.pct);
        avisadoSeco[i] = false;
      }
    }
  }
  // Wi-Fi: avisa quando volta depois de ficar fora mais de 5 min
  static unsigned long wifiCaiuMs = 0;
  if (WiFi.status() != WL_CONNECTED) { if (!wifiCaiuMs) wifiCaiuMs = millis(); }
  else if (wifiCaiuMs) {
    unsigned long fora = (millis() - wifiCaiuMs) / 60000UL;
    if (fora >= 5) alerta("📶 Wi-Fi voltou apos %lu min fora. Os dados desse periodo estavam na fila e estao sendo enviados.", fora);
    wifiCaiuMs = 0;
  }
  // Google / Grafana com erro ha mais de 30 min
  static unsigned long gErroDesde = 0, gfErroDesde = 0; static bool gAvisado = false, gfAvisado = false;
  if (gEstado == 2) { if (!gErroDesde) gErroDesde = millis(); if (!gAvisado && millis() - gErroDesde > 1800000UL) { alerta("☁️ Google Sheets com erro ha 30 min: %s. Os dados continuam na fila da placa.", gErro); gAvisado = true; } }
  else { if (gAvisado) alerta("☁️ Google Sheets voltou a receber os dados."); gErroDesde = 0; gAvisado = false; }
  if (gfEstado == 2) { if (!gfErroDesde) gfErroDesde = millis(); if (!gfAvisado && millis() - gfErroDesde > 1800000UL) { alerta("📊 Grafana com erro ha 30 min: %s", gfErro); gfAvisado = true; } }
  else { if (gfAvisado) alerta("📊 Grafana voltou a receber os dados."); gfErroDesde = 0; gfAvisado = false; }

  bool arFalha = isnan(arT) && isnan(ar2T);
  if (arFalha && !avisadoAr && millis() > 180000UL) { alerta("⚠️ Sensor do ar (DHT22) sem leitura. Verifique o fio no P4."); avisadoAr = true; }
  else if (!arFalha && avisadoAr) { alerta("✅ Sensor do ar voltou: %.1f °C, %.0f%%.", arT, arU); avisadoAr = false; }
}

// Resumo diario as 08:00
void resumoDiario() {
  static int diaEnviado = -1;
  if (!horaValida()) return;
  time_t t = time(nullptr); struct tm lt; localtime_r(&t, &lt);
  if (lt.tm_hour == 8 && lt.tm_mday != diaEnviado) {
    diaEnviado = lt.tm_mday;
    char b[240]; int n = snprintf(b, sizeof(b), "☀️ Resumo 08:00\n");
    for (int i = 0; i < N_CANAIS && n < (int)sizeof(b) - 40; i++) {
      if (solo[i].est == EST_DESATIVADO) continue;
      if (solo[i].est == EST_OK) n += snprintf(b + n, sizeof(b) - n, "T%d: %.0f%%\n", i + 1, solo[i].pct);
      else n += snprintf(b + n, sizeof(b) - n, "T%d: %s\n", i + 1, nomeEstado(solo[i].est));
    }
    if (!isnan(arT)) snprintf(b + n, sizeof(b) - n, "Ar: %.1f °C | %.0f%% | DPV %.2f kPa", arT, arU, calcDPV(arT, arU));
    alerta("%s", b);
  }
}

// ============================================================
//  TAREFA DE REDE (nucleo 0): Telegram + Google
//  Internet lenta ou fora do ar nunca atrasa as medicoes.
// ============================================================
void tarefaRede(void*) {
  for (;;) {
    netBeat = millis();
    if (WiFi.status() != WL_CONNECTED) { vTaskDelay(pdMS_TO_TICKS(1000)); continue; }
    telegramTarefa();
    googleTarefa();
    grafanaTarefa();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

const char* motivoReset() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "ligada na energia";
    case ESP_RST_SW:       return "reinicio pelo sistema/atualizacao";
    case ESP_RST_PANIC:    return "erro interno";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "watchdog: travou e se recuperou";
    case ESP_RST_BROWNOUT: return "QUEDA DE TENSAO na alimentacao";
    case ESP_RST_EXT:      return "botao EN";
    default:               return "outro";
  }
}

// ============================================================
//  API HTTP (painel local)
// ============================================================
bool pinOk() {
  if (server.arg("pin") != String(cfg.pin)) { server.send(403, "text/plain", "PIN incorreto"); return false; }
  return true;
}

void rotaStatus() {
  JsonDocument d;
  d["fw"] = FW_VERSAO;
  JsonArray ca = d["canais"].to<JsonArray>();
  for (int i = 0; i < N_CANAIS; i++) {
    JsonObject o = ca.add<JsonObject>();
    o["nome"] = cfg.c[i].nome;
    o["pino"] = PIN_SOLO[i];
    o["ativo"] = cfg.c[i].ativo;
    o["mv"] = lroundf(solo[i].est == EST_OK ? solo[i].mv : solo[i].mvInst);
    o["ruido"] = lroundf(solo[i].ruido);
    if (solo[i].est == EST_OK && !isnan(solo[i].pct)) o["pct"] = roundf(solo[i].pct * 10) / 10.0f;
    else o["pct"] = nullptr;
    o["est"] = solo[i].est;
    o["estTxt"] = nomeEstado(solo[i].est);
    o["seco"] = cfg.c[i].seco;
    o["molh"] = cfg.c[i].molhado;
    o["alerta"] = cfg.c[i].alerta;
  }
  JsonObject ar = d["ar"].to<JsonObject>();
  if (!isnan(arT)) { ar["t"] = roundf(arT * 10) / 10.0f; ar["u"] = roundf(arU * 10) / 10.0f; ar["dpv"] = roundf(calcDPV(arT, arU) * 100) / 100.0f; }
  if (ar2OkMs) {
    if (!isnan(ar2T)) { ar["t2"] = roundf(ar2T * 10) / 10.0f; ar["u2"] = roundf(ar2U * 10) / 10.0f; }
    else ar["t2"] = nullptr;
  }
  JsonObject r = d["rede"].to<JsonObject>();
  r["ip"] = WiFi.localIP().toString();
  r["rssi"] = WiFi.RSSI();
  r["up"] = millis() / 1000;
  r["hora"] = horaValida() ? agoraEpoch() : 0;
  r["int"] = cfg.intervaloMin;
  r["fsUsado"] = fsOk ? LittleFS.usedBytes() : 0;
  r["fsTotal"] = fsOk ? LittleFS.totalBytes() : 0;
  r["linhas"] = linhasGravadas;
  JsonObject g = d["google"].to<JsonObject>();
  g["est"] = gEstado; g["envios"] = gEnvios; g["falhas"] = gFalhas; g["pend"] = gPendLinhas;
  g["seg"] = gUltOkMs ? (long)((millis() - gUltOkMs) / 1000) : -1L;
  g["erro"] = gErro;
  g["url"] = gUrl.length() ? "configurado" : "";
  JsonObject gf = d["grafana"].to<JsonObject>();
  gf["est"] = gfEstado; gf["http"] = gfUltimoHttp; gf["envios"] = gfEnvios; gf["falhas"] = gfFalhas;
  gf["seg"] = gfUltOkMs ? (long)((millis() - gfUltOkMs) / 1000) : -1L;
  gf["erro"] = gfErro;
  JsonObject t = d["tg"].to<JsonObject>();
  t["est"] = tgEstado; t["codigo"] = tgChat.length() ? 0 : codigoPareamento; t["erro"] = tgErro;
  String out; serializeJson(d, out);
  server.send(200, "application/json", out);
}

void rotaHist() {
  // formato compacto: {"t":[epoch...],"p":[[T1..],[T2..],...],"mv":[[...]...],"ta":[...],"ua":[...]}
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "application/json", "");
  String b; b.reserve(2200);
  auto flush = [&b]() { if (b.length() > 1800) { server.sendContent(b); b = ""; } };
  b = "{\"t\":[";
  for (int k = 0; k < histQtd; k++) { if (k) b += ','; b += histEm(k).t; flush(); }
  b += "],\"p\":[";
  for (int i = 0; i < N_CANAIS; i++) {
    if (i) b += ','; b += '[';
    for (int k = 0; k < histQtd; k++) { if (k) b += ','; int8_t v = histEm(k).pct[i]; if (v < 0) b += "null"; else b += v; flush(); }
    b += ']';
  }
  b += "],\"mv\":[";
  for (int i = 0; i < N_CANAIS; i++) {
    if (i) b += ','; b += '[';
    for (int k = 0; k < histQtd; k++) { if (k) b += ','; b += histEm(k).mv[i]; flush(); }
    b += ']';
  }
  b += "],\"ta\":[";
  for (int k = 0; k < histQtd; k++) { if (k) b += ','; int16_t v = histEm(k).t10; if (v == INT16_MIN) b += "null"; else b += String(v / 10.0f, 1); flush(); }
  b += "],\"ua\":[";
  for (int k = 0; k < histQtd; k++) { if (k) b += ','; int16_t v = histEm(k).u10; if (v == INT16_MIN) b += "null"; else b += String(v / 10.0f, 1); flush(); }
  b += "]}";
  server.sendContent(b);
  server.sendContent("");
}

void rotaCSV() {
  server.sendHeader("Content-Disposition", "attachment; filename=tubetes_dados.csv");
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/csv", "");
  enviaCSV([](const char* p, size_t n) { server.sendContent(p, n); });
  server.sendContent("");
}

void rotaCalib() {
  if (!pinOk()) return;
  int c = server.arg("c").toInt();
  if (c < 0 || c >= N_CANAIS) { server.send(400, "text/plain", "canal invalido"); return; }
  String ponto = server.arg("ponto");
  int v = server.hasArg("mv") && server.arg("mv").length() ? server.arg("mv").toInt()
                                                          : (int)lroundf(solo[c].mv);
  if (solo[c].est != EST_OK && !(server.hasArg("mv") && server.arg("mv").length())) {
    server.send(409, "text/plain", "Sensor sem leitura valida agora: nao da para calibrar"); return;
  }
  if (v < 200 || v > 3200) { server.send(400, "text/plain", "valor fora da faixa (200-3200 mV)"); return; }
  if (ponto == "seco") cfg.c[c].seco = v;
  else if (ponto == "molhado") cfg.c[c].molhado = v;
  else { server.send(400, "text/plain", "ponto invalido"); return; }
  salvaCfg();
  String msg = "T" + String(c + 1) + ": " + ponto + " = " + String(v) + " mV salvo";
  if (abs(cfg.c[c].seco - cfg.c[c].molhado) < 300) msg += " (ATENCAO: seco e molhado muito proximos)";
  server.send(200, "text/plain", msg);
}

void rotaCfg() {
  if (!pinOk()) return;
  for (int i = 0; i < N_CANAIS; i++) {
    String k = String(i);
    if (server.hasArg("n" + k)) {
      String n = server.arg("n" + k); n.trim(); n.replace(",", " "); n.replace("\"", "'");
      if (n.length()) strlcpy(cfg.c[i].nome, n.c_str(), sizeof(cfg.c[i].nome));
    }
    if (server.hasArg("a" + k)) cfg.c[i].alerta = constrain(server.arg("a" + k).toInt(), 0, 95);
    if (server.hasArg("on" + k)) cfg.c[i].ativo = server.arg("on" + k).toInt() ? 1 : 0;
  }
  if (server.hasArg("int")) cfg.intervaloMin = constrain(server.arg("int").toInt(), 1, 60);
  if (server.hasArg("novopin")) {
    String np = server.arg("novopin"); np.trim();
    if (np.length() >= 4 && np.length() < sizeof(cfg.pin)) strlcpy(cfg.pin, np.c_str(), sizeof(cfg.pin));
  }
  salvaCfg();
  server.send(200, "text/plain", "Configuracao salva");
}

void rotaGoogle() {
  if (!pinOk()) return;
  String url = server.arg("url"); url.trim();
  String key = server.arg("key"); key.trim();
  if (url.length() && !url.startsWith("https://script.google.com/")) { server.send(400, "text/plain", "A URL deve comecar com https://script.google.com/"); return; }
  Preferences p; p.begin("google", false);
  if (url.length()) p.putString("url", url);
  if (key.length()) p.putString("key", key);
  if (server.arg("apagar") == "1") p.clear();
  p.end();
  gReconfig = true;
  server.send(200, "text/plain", "Google salvo. Testando em alguns segundos...");
}

void rotaGrafana() {
  if (!pinOk()) return;
  Preferences p; p.begin("grafana", false);
  if (server.arg("apagar") == "1") p.clear();
  else {
    String u = server.arg("url"), us = server.arg("usr"), tk = server.arg("tok");
    u.trim(); us.trim(); tk.trim();
    if (u.length()) p.putString("url", u);
    if (us.length()) p.putString("usr", us);
    if (tk.length() > 20) p.putString("tok", tk);
  }
  p.end();
  gfReconfig = true;
  server.send(200, "text/plain", "Grafana salvo. Primeiro envio em ate 1 min.");
}

void rotaTelegram() {
  if (!pinOk()) return;
  Preferences p; p.begin("tg", false);
  String tok = server.arg("token"); tok.trim();
  if (server.arg("apagar") == "1") p.clear();
  else if (server.arg("desparear") == "1") p.remove("chat");
  else if (tok.length() > 20) { p.putString("token", tok); p.remove("chat"); }
  else { p.end(); server.send(400, "text/plain", "Token invalido"); return; }
  p.end();
  tgReconfig = true;
  server.send(200, "text/plain", "Telegram salvo");
}

void rotaReiniciar() {
  if (!pinOk()) return;
  server.send(200, "text/plain", "Reiniciando...");
  delay(300);
  ESP.restart();
}

void configRotas() {
  server.on("/", HTTP_GET, [] { server.send_P(200, "text/html; charset=utf-8", PAGINA); });
  server.on("/api/status", HTTP_GET, rotaStatus);
  server.on("/api/hist", HTTP_GET, rotaHist);
  server.on("/dados.csv", HTTP_GET, rotaCSV);
  server.on("/api/calib", HTTP_POST, rotaCalib);
  server.on("/api/cfg", HTTP_POST, rotaCfg);
  server.on("/api/google", HTTP_POST, rotaGoogle);
  server.on("/api/tg", HTTP_POST, rotaTelegram);
  server.on("/api/grafana", HTTP_POST, rotaGrafana);
  server.on("/api/reiniciar", HTTP_POST, rotaReiniciar);
  server.onNotFound([] { server.send(404, "text/plain", "nao encontrado"); });
}

// ============================================================
//  REDE
// ============================================================
void guardaWifi() {
  String ss = WiFi.SSID(), pw = WiFi.psk();
  if (!ss.length()) return;
  Preferences p; p.begin("wifi", false);
  if (p.getString("ssid", "") != ss || p.getString("pass", "") != pw) { p.putString("ssid", ss); p.putString("pass", pw); }
  p.end();
}

bool conectaWifiGuardado() {
  Preferences p; p.begin("wifi", true);
  String ss = p.getString("ssid", ""), pw = p.getString("pass", "");
  p.end();
  if (!ss.length()) return false;
  Serial.printf("Conectando em \"%s\"...\n", ss.c_str());
  WiFi.begin(ss.c_str(), pw.c_str());
  for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) { delay(500); esp_task_wdt_reset(); }
  return WiFi.status() == WL_CONNECTED;
}

void dnsReserva() {   // se o DNS do roteador falhar: Google e Cloudflare
  esp_netif_t* nif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (!nif) return;
  esp_netif_dns_info_t d = {};
  d.ip.type = ESP_IPADDR_TYPE_V4;
  d.ip.u_addr.ip4.addr = ipaddr_addr("8.8.8.8");
  esp_netif_set_dns_info(nif, ESP_NETIF_DNS_BACKUP, &d);
  d.ip.u_addr.ip4.addr = ipaddr_addr("1.1.1.1");
  esp_netif_set_dns_info(nif, ESP_NETIF_DNS_FALLBACK, &d);
}

void iniciaRede() {
  Serial.print("Wi-Fi conectado. IP: "); Serial.println(WiFi.localIP());
  dnsReserva();
  configTzTime("<-03>3", "pool.ntp.org", "time.google.com");   // horario de Brasilia
  if (MDNS.begin(HOSTNAME)) { MDNS.addService("http", "tcp", 80); Serial.println("Painel: http://tubetes.local"); }
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_SENHA);
  ArduinoOTA.begin();
  configRotas();
  server.begin();
  redeIniciada = true;
  guardaWifi();
}

void checaBotaoBoot() {
  static unsigned long apertou = 0;
  if (digitalRead(PIN_BOOT) == LOW) {
    if (!apertou) apertou = millis();
    if (millis() - apertou > 5000) {
      Serial.println("Apagando Wi-Fi salvo e reiniciando...");
      wm.resetSettings();
      { Preferences p; p.begin("wifi", false); p.clear(); p.end(); }
      delay(500);
      ESP.restart();
    }
  } else apertou = 0;
}

void atualizaLed() {
  // pisca rapido = sensor com problema | pisca lento = sem Wi-Fi | apagado = tudo ok
  bool falha = false;
  for (int i = 0; i < N_CANAIS; i++) if (solo[i].est != EST_OK && solo[i].est != EST_DESATIVADO && solo[i].est != EST_INICIANDO) falha = true;
  bool e;
  if (millis() - ledRegistroMs < 200) e = true;                 // piscada curta = linha gravada
  else if (falha) e = (millis() / 150) % 2;
  else if (WiFi.status() != WL_CONNECTED) e = (millis() / 1000) % 2;
  else e = false;
  digitalWrite(PIN_LED, e);
}

// ============================================================
//  SETUP / LOOP
// ============================================================
void setup() {
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_BOOT, INPUT_PULLUP);
  Serial.begin(115200);
  delay(300);
  Serial.printf("\n=== TCC TUBETES v%s ===\n", FW_VERSAO);

  carregaCfg();
  sensoresInicia();
  fsInicia();
  googleCarrega();
  telegramCarrega();

#if ESP_ARDUINO_VERSION_MAJOR >= 3
  esp_task_wdt_config_t wdt = { .timeout_ms = 30000, .idle_core_mask = 0, .trigger_panic = true };
  if (esp_task_wdt_reconfigure(&wdt) != ESP_OK) esp_task_wdt_init(&wdt);
#else
  esp_task_wdt_init(30, true);
#endif
  esp_task_wdt_add(NULL);

  WiFi.mode(WIFI_STA);
  WiFi.setHostname(HOSTNAME);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  wm.setConfigPortalBlocking(false);            // mede mesmo sem rede
  bool temCopia;
  { Preferences p; p.begin("wifi", true); temCopia = p.getString("ssid", "").length() > 0; p.end(); }
  wm.setConfigPortalTimeout(temCopia ? 300 : 0);
  wm.setConnectTimeout(30);
  wm.setCleanConnect(true);
  wm.setConnectRetries(3);
  wm.setTitle("TCC Tubetes");
  if (conectaWifiGuardado()) iniciaRede();
  else if (wm.autoConnect(AP_NOME, AP_SENHA)) iniciaRede();
  else Serial.printf("Sem Wi-Fi. Conecte o celular na rede \"%s\" (senha %s) e abra 192.168.4.1\n", AP_NOME, AP_SENHA);

  filaAlerta = xQueueCreate(10, sizeof(Alerta));
  xTaskCreatePinnedToCore(tarefaRede, "rede", 16384, nullptr, 1, nullptr, 0);   // nucleo 0

  alerta("🔄 TCC Tubetes iniciada (%s). Painel: http://%s", motivoReset(), WiFi.localIP().toString().c_str());
}

void loop() {
  esp_task_wdt_reset();
  wm.process();
  if (redeIniciada) { server.handleClient(); ArduinoOTA.handle(); }
  else if (WiFi.status() == WL_CONNECTED && !wm.getConfigPortalActive()) iniciaRede();

  if (WiFi.status() != WL_CONNECTED && !wm.getConfigPortalActive() && millis() - ultimaTentativaWifi > 30000) {
    ultimaTentativaWifi = millis();
    Preferences pw; pw.begin("wifi", true);
    String ss = pw.getString("ssid", ""), ps = pw.getString("pass", "");
    pw.end();
    if (ss.length()) WiFi.begin(ss.c_str(), ps.c_str()); else WiFi.begin();
  }

  checaBotaoBoot();

  // vigia: tarefa de internet travada ha 10 min -> reinicia
  if (netBeat && millis() - netBeat > 600000UL) { Serial.println("Tarefa de rede travada -> reiniciando"); delay(200); ESP.restart(); }

  static unsigned long ultSolo = 0, ultSeg = 0, ultPrint = 0;
  if (millis() - ultSolo >= 2000) { ultSolo = millis(); leSolo(); }
  climaLoop();
  if (millis() - ultSeg >= 1000) {
    ultSeg = millis();
    registraSeForHora();
    verificaAlertas();
    resumoDiario();
  }
  if (millis() - ultPrint >= 5000) {
    ultPrint = millis();
    Serial.printf("T1 %4.0fmV %5.1f%% | T2 %4.0fmV %5.1f%% | T3 %4.0fmV %5.1f%% | T4 %4.0fmV %5.1f%% | DHT %.1fC %.0f%% | SHT40 %.2fC %.1f%% DPV %.3fkPa | %s\n",
      solo[0].mvInst, solo[0].pct, solo[1].mvInst, solo[1].pct, solo[2].mvInst, solo[2].pct, solo[3].mvInst, solo[3].pct,
      arT, arU, ar2T, ar2U, calcDPV(ar2T, ar2U), WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "sem Wi-Fi");
  }
  atualizaLed();
  delay(2);
}
