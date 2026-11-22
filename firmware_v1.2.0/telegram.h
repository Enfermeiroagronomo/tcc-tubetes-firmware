// =====================================================================
//  telegram.h — /status pelo celular + alertas automaticos (opcional)
//  Use um bot NOVO (BotFather) so para esta placa: o mesmo token em duas
//  placas da erro 409. Token digitado no painel local; pareamento por codigo.
//  Roda SO na tarefa de rede (nucleo 0).
// =====================================================================
#pragma once

String tgToken, tgChat;
volatile bool tgReconfig = true;
volatile int  tgEstado = 0;          // 0 sem token | 1 aguardando pareamento | 2 ok | 3 erro
volatile uint32_t codigoPareamento = 0;
char tgErro[110] = "";
long tgOffset = 0;
WiFiClientSecure tlsTg;

void telegramCarrega() {
  Preferences p; p.begin("tg", true);
  tgToken = p.getString("token", ""); tgChat = p.getString("chat", "");
  p.end();
  tgToken.trim();
  if (!codigoPareamento) codigoPareamento = 100000 + (esp_random() % 900000);
}

int tgChamada(const char* metodo, const String& corpo, String* resposta = nullptr) {
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  String url = String("https://api.telegram.org/bot") + tgToken + "/" + metodo;
  if (!http.begin(tlsTg, url)) { strlcpy(tgErro, "nao consegui iniciar HTTPS", sizeof(tgErro)); return -1; }
  http.addHeader("Content-Type", "application/json");
  int code = http.POST(corpo);
  if (code > 0 && resposta) *resposta = http.getString();
  if (code <= 0)          snprintf(tgErro, sizeof(tgErro), "sem conexao com o Telegram (%s)", http.errorToString(code).c_str());
  else if (code == 401)   strlcpy(tgErro, "token INVALIDO (401): cole o token do BotFather", sizeof(tgErro));
  else if (code == 404)   strlcpy(tgErro, "token incompleto ou com texto a mais (404)", sizeof(tgErro));
  else if (code == 409)   strlcpy(tgErro, "bot usado em outra placa (409): crie um bot so para esta", sizeof(tgErro));
  else if (code != 200)   snprintf(tgErro, sizeof(tgErro), "Telegram respondeu HTTP %d", code);
  else tgErro[0] = 0;
  for (char* c = tgErro; *c; c++) if (*c == '"' || *c == '\\') *c = '\'';
  http.end();
  return code;
}

bool tgEnvia(const String& chat, const String& texto) {
  JsonDocument d;
  d["chat_id"] = chat;
  d["text"] = texto;
  JsonObject rm = d["reply_markup"].to<JsonObject>();
  JsonArray kb = rm["keyboard"].to<JsonArray>();
  JsonArray l1 = kb.add<JsonArray>(); l1.add("/status"); l1.add("/ajuda");
  rm["resize_keyboard"] = true;
  rm["is_persistent"] = true;
  String corpo; serializeJson(d, corpo);
  return tgChamada("sendMessage", corpo) == 200;
}

String textoStatus() {
  String s = "🌱 TCC Tubetes\n";
  char b[120];
  for (int i = 0; i < N_CANAIS; i++) {
    if (solo[i].est == EST_DESATIVADO) continue;
    if (solo[i].est == EST_OK)
      snprintf(b, sizeof(b), "T%d %s: %.0f%% (%.0f mV)\n", i + 1, cfg.c[i].nome, solo[i].pct, solo[i].mv);
    else
      snprintf(b, sizeof(b), "⚠️ T%d %s: %s\n", i + 1, cfg.c[i].nome, nomeEstado(solo[i].est));
    s += b;
  }
  if (!isnan(arT)) { snprintf(b, sizeof(b), "🌡️ Ar: %.1f °C | UR %.0f%% | DPV %.2f kPa\n", arT, arU, calcDPV(arT, arU)); s += b; }
  else s += "🌡️ Ar: sensor sem leitura\n";
  if (!isnan(ar2T)) { snprintf(b, sizeof(b), "🌡️ SHT40: %.1f °C | UR %.0f%% | DPV %.2f kPa\n", ar2T, ar2U, calcDPV(ar2T, ar2U)); s += b; }
  snprintf(b, sizeof(b), "💾 %lu linhas gravadas | Google: %s%s\n", (unsigned long)linhasGravadas,
           gEstado == 1 ? "ok" : gEstado == 2 ? "com erro" : "nao configurado",
           gPendLinhas ? " (fila pendente)" : "");
  s += b;
  snprintf(b, sizeof(b), "📶 %d dBm | ligada ha %lu h | painel: http://%s", (int)WiFi.RSSI(), millis() / 3600000UL, WiFi.localIP().toString().c_str());
  s += b;
  return s;
}

const char* AJUDA_TG =
  "Comandos:\n"
  "/status - umidade dos 4 tubetes, ar e DPV\n"
  "/ajuda - esta lista\n"
  "Alertas automaticos: sensor com problema, tubete abaixo do limite, resumo as 08:00.";

void trataMensagem(const String& chatId, const String& texto) {
  String t = texto; t.trim();
  int at = t.indexOf('@'); int sp = t.indexOf(' ');
  String cmd = t, arg = "";
  if (sp > 0) { cmd = t.substring(0, sp); arg = t.substring(sp + 1); arg.trim(); }
  if (at > 0 && (sp < 0 || at < sp)) cmd = cmd.substring(0, at);
  cmd.toLowerCase();

  if (!tgChat.length()) {
    if ((cmd == "/parear" || cmd == "/start") && arg.toInt() == (long)codigoPareamento) {
      tgChat = chatId;
      Preferences p; p.begin("tg", false); p.putString("chat", tgChat); p.end();
      tgEstado = 2;
      tgEnvia(chatId, "✅ Pareado com a placa TCC Tubetes.\n" + String(AJUDA_TG));
    } else tgEnvia(chatId, "Envie /parear <codigo> (o codigo aparece no painel local da placa).");
    return;
  }
  if (chatId != tgChat) { tgEnvia(chatId, "Este bot ja esta pareado com outra conta."); return; }
  if (cmd == "/status") tgEnvia(chatId, textoStatus());
  else tgEnvia(chatId, AJUDA_TG);
}

bool tgLeMensagens() {
  String corpo = String("{\"offset\":") + tgOffset + ",\"timeout\":0,\"allowed_updates\":[\"message\"]}";
  String resp;
  if (tgChamada("getUpdates", corpo, &resp) != 200) return false;
  JsonDocument filtro;
  filtro["result"][0]["update_id"] = true;
  filtro["result"][0]["message"]["chat"]["id"] = true;
  filtro["result"][0]["message"]["text"] = true;
  JsonDocument doc;
  if (deserializeJson(doc, resp, DeserializationOption::Filter(filtro))) return true;
  for (JsonObject u : doc["result"].as<JsonArray>()) {
    long id = u["update_id"] | 0L;
    if (id >= tgOffset) tgOffset = id + 1;
    const char* txt = u["message"]["text"] | "";
    long long chat = u["message"]["chat"]["id"] | 0LL;
    if (!chat || !txt[0]) continue;
    char cid[24]; snprintf(cid, sizeof(cid), "%lld", chat);
    trataMensagem(String(cid), String(txt));
  }
  return true;
}

void telegramTarefa() {
  static unsigned long ultLeitura = 0, ultFalhaEnvio = 0, ultTeste = 0;
  static bool tlsPronto = false;
  if (!tlsPronto) {
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 3, 5)
    tlsTg.useBuiltinCACertBundle();
#else
    tlsTg.setInsecure();
#endif
    tlsPronto = true;
  }
  if (tgReconfig) {
    tgReconfig = false;
    telegramCarrega();
    tgOffset = 0;
    if (tgToken.length() > 20) tgEstado = tgChat.length() ? 2 : 1;
    else tgEstado = 0;
  }
  if (tgEstado == 0 || (tgEstado == 1 && !tgChat.length())) {
    Alerta lixo;                                                   // sem destino: descarta alertas
    if (tgEstado == 0) { while (xQueueReceive(filaAlerta, &lixo, 0) == pdTRUE) {} return; }
  }
  // envia alertas pendentes
  if (tgChat.length() && millis() - ultFalhaEnvio > 15000UL) {
    Alerta a;
    while (xQueuePeek(filaAlerta, &a, 0) == pdTRUE) {
      if (tgEnvia(tgChat, a.txt)) { xQueueReceive(filaAlerta, &a, 0); tgEstado = 2; }
      else { tgEstado = 3; ultFalhaEnvio = millis(); break; }
    }
  }
  // le comandos a cada 3 s (a cada 30 s se estiver em erro)
  unsigned long per = (tgEstado == 3) ? 30000UL : 3000UL;
  if (millis() - ultLeitura > per) {
    ultLeitura = millis();
    if (tgLeMensagens()) tgEstado = tgChat.length() ? 2 : 1;
    else tgEstado = 3;
  }
  (void)ultTeste;
}
