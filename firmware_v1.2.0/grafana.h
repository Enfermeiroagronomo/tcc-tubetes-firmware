// =====================================================================
//  grafana.h — envia as leituras a cada 60 s para o Grafana Cloud
//  (painel em tempo real para compartilhar com colaboradores).
//
//  Protocolo: Influx line protocol -> Grafana Cloud (vira metrica Prometheus)
//    POST https://<host>/api/v1/push/influx/write
//    Authorization: Bearer <ID da instancia>:<token metrics:write>
//  Metricas: tubetes_solo_mv{tubete,trat} tubetes_solo_pct{tubete,trat}
//            tubetes_ar_temp / _ur / _dpv   tubetes_sis_rssi / _falhas / _fila / _uptime_h
//  Credenciais digitadas no painel local, guardadas na placa (nunca no codigo).
//  Roda SO na tarefa de rede (nucleo 0). Licao do Urban Jungle: String(v,0)
//  gera espaco e o Grafana responde 400 -> usar lroundf e trim.
// =====================================================================
#pragma once

#define GF_INTERVALO_MS 60000UL

String gfUrl, gfUser, gfToken;
volatile bool gfReconfig = true;
volatile int  gfEstado = 0;             // 0 nao configurado | 1 ok | 2 erro
volatile int  gfUltimoHttp = 0;
volatile uint32_t gfEnvios = 0, gfFalhas = 0;
volatile unsigned long gfUltOkMs = 0;
char gfErro[140] = "";

void grafanaCarrega() {
  Preferences p; p.begin("grafana", true);
  gfUrl = p.getString("url", ""); gfUser = p.getString("usr", ""); gfToken = p.getString("tok", "");
  p.end();
  gfUrl.trim(); gfUser.trim(); gfToken.trim();
  if (gfUrl.length()) {                 // aceita colar so o host ou qualquer URL do stack
    if (!gfUrl.startsWith("http")) gfUrl = "https://" + gfUrl;
    int i = gfUrl.indexOf("/api/");
    if (i > 0) gfUrl = gfUrl.substring(0, i);
    while (gfUrl.endsWith("/")) gfUrl.remove(gfUrl.length() - 1);
    gfUrl += "/api/v1/push/influx/write";
  }
  gfEstado = (gfUrl.length() && gfUser.length() && gfToken.length() > 20) ? 1 : 0;
}

// escapa espacos, virgulas e '=' em valores de tag (line protocol)
static String gfTag(const char* s) {
  String o;
  for (const char* c = s; *c; c++) { if (*c == ' ' || *c == ',' || *c == '=') o += '\\'; o += *c; }
  if (!o.length()) o = "sem_nome";
  return o;
}
static void gfCampo(String& c, const char* k, float v, int casas) {
  if (isnan(v)) return;
  if (c.length()) c += ',';
  c += k; c += '=';
  if (casas == 0) c += String((long)lroundf(v));
  else { String t = String(v, casas); t.trim(); c += t; }
}

String grafanaCorpo() {
  String b; b.reserve(700);
  for (int i = 0; i < N_CANAIS; i++) {
    if (solo[i].est == EST_DESATIVADO) continue;
    String c;
    gfCampo(c, "mv", solo[i].est == EST_OK ? solo[i].mv : solo[i].mvInst, 0);
    if (solo[i].est == EST_OK) gfCampo(c, "pct", solo[i].pct, 1);
    gfCampo(c, "falha", solo[i].est == EST_OK ? 0 : 1, 0);
    if (!c.length()) continue;
    b += "tubetes_solo,placa="; b += HOSTNAME;
    b += ",tubete=T"; b += (i + 1);
    b += ",trat="; b += gfTag(cfg.c[i].nome);
    b += ' '; b += c; b += '\n';
  }
  String a;
  gfCampo(a, "temp", arT, 1);
  gfCampo(a, "ur", arU, 1);
  gfCampo(a, "dpv", calcDPV(arT, arU), 3);
  gfCampo(a, "temp2", ar2T, 1);
  gfCampo(a, "ur2", ar2U, 1);
  if (a.length()) { b += "tubetes_ar,placa="; b += HOSTNAME; b += ' '; b += a; b += '\n'; }
  String s;
  gfCampo(s, "rssi", (float)WiFi.RSSI(), 0);
  gfCampo(s, "falhas", (float)mascaraFalhas(), 0);
  gfCampo(s, "fila", (float)gPendLinhas, 0);
  gfCampo(s, "uptime_h", millis() / 3600000.0f, 2);
  b += "tubetes_sis,placa="; b += HOSTNAME; b += ' '; b += s;
  return b;
}

bool grafanaEnvia() {
  WiFiClientSecure cli; prepTls(cli);
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(8000);
  if (!http.begin(cli, gfUrl)) { gfUltimoHttp = -1; strlcpy(gfErro, "URL invalida", sizeof(gfErro)); return false; }
  http.addHeader("Authorization", "Bearer " + gfUser + ":" + gfToken);
  http.addHeader("Content-Type", "text/plain");
  int code = http.POST(grafanaCorpo());
  if (code >= 300 || code < 0) {
    String e = code < 0 ? http.errorToString(code) : http.getString();
    e.replace("\"", "'"); e.replace("\n", " ");
    snprintf(gfErro, sizeof(gfErro), "HTTP %d %s", code, e.substring(0, 110).c_str());
    Serial.printf("[grafana] %s\n", gfErro);
  } else gfErro[0] = 0;
  http.end();
  gfUltimoHttp = code;
  return code >= 200 && code < 300;
}

void grafanaTarefa() {
  static unsigned long ult = 0;
  if (gfReconfig) { gfReconfig = false; grafanaCarrega(); ult = 0; }
  if (gfEstado == 0) return;
  if (ult != 0 && millis() - ult < GF_INTERVALO_MS) return;
  if (millis() < 20000UL) return;                     // espera os sensores estabilizarem
  ult = millis();
  if (grafanaEnvia()) { gfEstado = 1; gfEnvios = gfEnvios + 1; gfUltOkMs = millis(); }
  else { gfEstado = 2; gfFalhas = gfFalhas + 1; }
}
