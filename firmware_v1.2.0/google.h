// =====================================================================
//  google.h — envia as linhas da fila (/pend.csv) para o Google Sheets
//             via Apps Script (Web App). Roda SO na tarefa de rede (nucleo 0).
//
//  POST JSON {k, placa, fw, nomes[4], cab, linhas[]}  ->  {"ok":true,"n":..}
//  O Apps Script ignora linhas com epoch ja gravado (reenvio nao duplica).
//  URL e chave digitadas no painel local, guardadas na placa (nunca no codigo).
// =====================================================================
#pragma once

String gUrl, gKey;
extern String tgToken, tgChat;           // definidos em telegram.h (vigia de placa offline no Apps Script)
volatile bool gReconfig = true;
volatile int  gEstado = 0;               // 0 nao configurado | 1 ok | 2 erro
volatile uint32_t gEnvios = 0, gFalhas = 0, gPendLinhas = 0;
volatile unsigned long gUltOkMs = 0;
char gErro[140] = "";
size_t gOffset = 0;                      // bytes de /pend.csv ja confirmados

void googleCarrega() {
  Preferences p; p.begin("google", true);
  gUrl = p.getString("url", ""); gKey = p.getString("key", "");
  p.end();
  gUrl.trim(); gKey.trim();
  googleAtivo = gUrl.startsWith("https://script.google.com/") && gKey.length() >= 4;
  gEstado = googleAtivo ? 1 : 0;
}

static void prepTls(WiFiClientSecure& c) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 5)
  c.useBuiltinCACertBundle();           // valida o certificado do Google
#else
  c.setInsecure();
#endif
  c.setTimeout(12);
}

// POST com redirecionamento manual: o Apps Script responde 302 -> googleusercontent (GET)
int googlePost(const String& corpo, String& resp) {
  WiFiClientSecure cli; prepTls(cli);
  HTTPClient http;
  http.setConnectTimeout(10000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  const char* hk[] = {"Location"};
  if (!http.begin(cli, gUrl)) return -100;
  http.collectHeaders(hk, 1);
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(corpo);
  if (code == 301 || code == 302 || code == 303 || code == 307) {
    String loc = http.header("Location");
    http.end();
    if (!loc.length()) return -101;
    HTTPClient h2;
    h2.setConnectTimeout(10000);
    h2.setTimeout(15000);
    if (!h2.begin(cli, loc)) return -102;
    code = h2.GET();
    resp = code > 0 ? h2.getString() : h2.errorToString(code);
    h2.end();
    return code;
  }
  resp = code > 0 ? http.getString() : http.errorToString(code);
  http.end();
  return code;
}

// le ate 'maxL' linhas da fila a partir de gOffset
int leLote(String* linhas, int maxL, size_t& fimOffset) {
  int n = 0;
  xSemaphoreTake(fsMux, portMAX_DELAY);
  size_t tam = tamArq(ARQ_PEND);
  if (tam == 0 || gOffset >= tam) {
    if (tam) LittleFS.remove(ARQ_PEND);
    gOffset = 0; gPendLinhas = 0;
    xSemaphoreGive(fsMux);
    return 0;
  }
  File f = LittleFS.open(ARQ_PEND, "r");
  if (f) {
    f.seek(gOffset);
    while (n < maxL && f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length()) linhas[n++] = l; }
    fimOffset = f.position();
    gPendLinhas = (tam - gOffset) / 75 + 1;           // estimativa (~75 bytes/linha)
    f.close();
  }
  xSemaphoreGive(fsMux);
  return n;
}

void googleTarefa() {
  static unsigned long proxTentativa = 0, espera = 20000;
  if (gReconfig) { gReconfig = false; googleCarrega(); proxTentativa = 0; espera = 20000; gErro[0] = 0; }
  if (!googleAtivo || !fsOk) return;
  if (millis() < proxTentativa) return;
  proxTentativa = millis() + 15000;                    // checa a fila a cada 15 s

  const int MAXL = 36;
  String linhas[MAXL];
  size_t fim = gOffset;
  int n = leLote(linhas, MAXL, fim);
  if (n == 0) return;

  JsonDocument d;
  d["k"] = gKey;
  d["placa"] = HOSTNAME;
  d["fw"] = FW_VERSAO;
  JsonArray nm = d["nomes"].to<JsonArray>();
  for (int i = 0; i < N_CANAIS; i++) nm.add(cfg.c[i].nome);
  d["cab"] = CAB_CSV;
  if (tgToken.length() > 20 && tgChat.length()) { d["tg_token"] = tgToken; d["tg_chat"] = tgChat; }
  JsonArray ls = d["linhas"].to<JsonArray>();
  for (int i = 0; i < n; i++) ls.add(linhas[i]);
  String corpo; serializeJson(d, corpo);
  d.clear();

  String resp;
  int code = googlePost(corpo, resp);
  bool ok = false;
  if (code == 200) {
    JsonDocument r;
    if (!deserializeJson(r, resp) && (r["ok"] | false)) ok = true;
    else {
      String e = r["erro"] | "";
      if (!e.length()) e = "resposta inesperada: " + resp.substring(0, 80);
      e.replace("\"", "'"); e.replace("\n", " ");
      strlcpy(gErro, e.c_str(), sizeof(gErro));
    }
  } else {
    String e = "HTTP " + String(code) + " " + resp.substring(0, 90);
    e.replace("\"", "'"); e.replace("\n", " ");
    strlcpy(gErro, e.c_str(), sizeof(gErro));
  }
  if (ok) {
    gOffset = fim;
    gEnvios = gEnvios + n;
    gUltOkMs = millis();
    gEstado = 1;
    gErro[0] = 0;
    espera = 20000;
    proxTentativa = 0;                                 // se ainda tem fila, continua ja
  } else {
    gFalhas = gFalhas + 1;
    gEstado = 2;
    Serial.printf("[google] falhou: %s\n", gErro);
    proxTentativa = millis() + espera;
    espera = min(espera * 2, 600000UL);                // 20 s, 40 s ... ate 10 min
  }
}
