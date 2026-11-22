// =====================================================================
//  armazenamento.h — registro dos dados
//   - historico de 48 h na RAM (grafico do painel local)
//   - /dados.csv na memoria flash da placa (com rotacao para /dados_ant.csv)
//   - /pend.csv = fila de linhas ainda nao enviadas ao Google
//     (se a internet cair, nada se perde enquanto houver espaco)
// =====================================================================
#pragma once
#include <LittleFS.h>

const char* CAB_CSV = "epoch,mv1,mv2,mv3,mv4,pct1,pct2,pct3,pct4,ar_temp,ar_ur,dpv_kpa,ar2_temp,ar2_ur,falhas,rssi";
#define ARQ_DADOS "/dados.csv"
#define ARQ_ANT   "/dados_ant.csv"
#define ARQ_PEND  "/pend.csv"

SemaphoreHandle_t fsMux = nullptr;
bool fsOk = false;
size_t limDados = 40000, limPend = 50000;
volatile uint32_t linhasGravadas = 0;
volatile unsigned long ledRegistroMs = 0;
volatile bool googleAtivo = false;       // definido em google.h

// ---------------- historico na RAM ----------------
struct Ponto { uint32_t t; uint16_t mv[N_CANAIS]; int8_t pct[N_CANAIS]; int16_t t10; int16_t u10; };
#define HIST_N 576
Ponto hist[HIST_N];
int histIni = 0, histQtd = 0;
const Ponto& histEm(int k) { return hist[(histIni + k) % HIST_N]; }
void histAdd(const Ponto& p) {
  if (histQtd < HIST_N) hist[(histIni + histQtd++) % HIST_N] = p;
  else { hist[histIni] = p; histIni = (histIni + 1) % HIST_N; }
}

void fsInicia() {
  fsMux = xSemaphoreCreateMutex();
  fsOk = LittleFS.begin(true);
  if (fsOk) {
    size_t tot = LittleFS.totalBytes();
    limDados = tot * 0.28;      // dados + dados_ant = 56%
    limPend  = tot * 0.30;      // fila offline
    Serial.printf("Memoria de dados: %u KB (dados ate %u KB x2, fila ate %u KB)\n", tot / 1024, limDados / 1024, limPend / 1024);
  } else Serial.println("ERRO: nao consegui montar a memoria de dados (LittleFS)");
}

static String numTxt(float v, int casas) {
  if (isnan(v)) return "";
  String s = String(v, casas); s.trim();
  return s;
}

String montaLinha(uint32_t ep) {
  String l; l.reserve(120);
  l += ep;
  for (int i = 0; i < N_CANAIS; i++) {
    l += ',';
    if (solo[i].est != EST_DESATIVADO) l += (long)lroundf(solo[i].est == EST_OK ? solo[i].mv : solo[i].mvInst);
  }
  for (int i = 0; i < N_CANAIS; i++) { l += ','; if (solo[i].est == EST_OK) l += numTxt(solo[i].pct, 1); }
  l += ','; l += numTxt(arT, 1);
  l += ','; l += numTxt(arU, 1);
  l += ','; l += numTxt(calcDPV(arT, arU), 3);
  l += ','; l += numTxt(ar2T, 1);
  l += ','; l += numTxt(ar2U, 1);
  l += ','; l += mascaraFalhas();
  l += ','; l += (WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0);
  return l;
}

size_t tamArq(const char* a) { if (!LittleFS.exists(a)) return 0; File f = LittleFS.open(a, "r"); size_t s = f ? f.size() : 0; if (f) f.close(); return s; }

void gravaArquivos(const String& linha) {
  if (!fsOk) return;
  xSemaphoreTake(fsMux, portMAX_DELAY);
  if (tamArq(ARQ_DADOS) > limDados) { LittleFS.remove(ARQ_ANT); LittleFS.rename(ARQ_DADOS, ARQ_ANT); }
  bool novo = !LittleFS.exists(ARQ_DADOS);
  File f = LittleFS.open(ARQ_DADOS, FILE_APPEND);
  if (f) { if (novo) f.println(CAB_CSV); f.println(linha); f.close(); }
  if (googleAtivo) {
    if (tamArq(ARQ_PEND) < limPend) { File g = LittleFS.open(ARQ_PEND, FILE_APPEND); if (g) { g.println(linha); g.close(); } }
    else Serial.println("Fila do Google cheia: linha fica so no /dados.csv");
  }
  xSemaphoreGive(fsMux);
}

// Grava uma linha quando o relogio passa por um multiplo do intervalo (ex.: :00, :05, :10)
void registraSeForHora() {
  static uint32_t ultimoSlot = 0;
  if (!horaValida() || millis() < 30000UL) return;     // precisa de hora certa e sensores estabilizados
  uint32_t ep = agoraEpoch();
  uint32_t passo = (uint32_t)cfg.intervaloMin * 60UL;
  uint32_t slot = ep / passo;
  if (slot == ultimoSlot) return;
  ultimoSlot = slot;
  uint32_t epAlinhado = slot * passo;

  String linha = montaLinha(epAlinhado);
  gravaArquivos(linha);
  linhasGravadas = linhasGravadas + 1;
  ledRegistroMs = millis();

  // historico RAM a cada 5 min (ou no intervalo, se maior)
  static uint32_t ultHist = 0;
  if (epAlinhado - ultHist >= 300 || ultHist == 0) {
    ultHist = epAlinhado;
    Ponto p;
    p.t = epAlinhado;
    for (int i = 0; i < N_CANAIS; i++) {
      p.mv[i] = (uint16_t)lroundf(solo[i].est == EST_OK ? solo[i].mv : 0);
      p.pct[i] = (solo[i].est == EST_OK && !isnan(solo[i].pct)) ? (int8_t)lroundf(solo[i].pct) : -1;
    }
    p.t10 = isnan(arT) ? INT16_MIN : (int16_t)lroundf(arT * 10);
    p.u10 = isnan(arU) ? INT16_MIN : (int16_t)lroundf(arU * 10);
    histAdd(p);
  }
  Serial.printf("[registro] %s\n", linha.c_str());
}

// Envia /dados_ant.csv + /dados.csv (sem repetir o cabecalho)
template <typename F>
void enviaCSV(F envia) {
  if (!fsOk) { envia(CAB_CSV, strlen(CAB_CSV)); return; }
  xSemaphoreTake(fsMux, portMAX_DELAY);
  bool cabEnviado = false;
  const char* arqs[2] = {ARQ_ANT, ARQ_DADOS};
  char buf[1024];
  for (int a = 0; a < 2; a++) {
    if (!LittleFS.exists(arqs[a])) continue;
    File f = LittleFS.open(arqs[a], "r");
    if (!f) continue;
    if (cabEnviado) f.readStringUntil('\n');             // pula cabecalho repetido
    cabEnviado = true;
    while (f.available()) { size_t n = f.read((uint8_t*)buf, sizeof(buf)); if (n) envia(buf, n); esp_task_wdt_reset(); }
    f.close();
  }
  xSemaphoreGive(fsMux);
  if (!cabEnviado) { envia(CAB_CSV, strlen(CAB_CSV)); envia("\n", 1); }
}
