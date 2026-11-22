// =====================================================================
//  sensores.h — 4 sensores capacitivos de substrato (HW-390 v2.0) + DHT22 + SHT40 (I2C)
//
//  Leitura do solo: 64 amostras em mV (analogReadMilliVolts usa a calibracao
//  de fabrica do ADC), media aparada (descarta 25% de cada ponta) e filtro
//  exponencial. O "ruido" (amplitude entre os percentis 10 e 90) serve para
//  detectar mau contato / pino solto.
//
//  Porcentagem = indice relativo entre os pontos SECO e MOLHADO calibrados
//  no proprio substrato de cada tratamento. NAO e umidade volumetrica: para
//  isso, use a curva gravimetrica (peso do tubete x mV) no Colab.
// =====================================================================
#pragma once
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_SHT4x.h>

enum { EST_OK = 0, EST_SEM_SINAL, EST_SATURADO, EST_INSTAVEL, EST_DESATIVADO, EST_INICIANDO };

const char* nomeEstado(uint8_t e) {
  switch (e) {
    case EST_OK:         return "ok";
    case EST_SEM_SINAL:  return "sem sinal (sensor solto?)";
    case EST_SATURADO:   return "sinal acima de 3 V (alimentado em 5V?)";
    case EST_INSTAVEL:   return "leitura instavel (mau contato?)";
    case EST_DESATIVADO: return "desativado";
    default:             return "iniciando";
  }
}

// limites de diagnostico (mV)
#define SOLO_MIN_MV     300    // abaixo: sem sinal
#define SOLO_MAX_MV     3050   // acima: saturado
#define SOLO_RUIDO_MAX  250    // amplitude P10-P90 acima disso: instavel
#define SOLO_ALFA       0.25f  // filtro exponencial (2 s por passo -> ~8 s de constante)

struct Solo {
  float mv = -1;            // filtrado
  float mvInst = 0;         // ultima leitura (sem filtro)
  float ruido = 0;
  float pct = NAN;
  uint8_t est = EST_INICIANDO;
  uint8_t candidato = EST_INICIANDO;
  uint8_t repet = 0;
  unsigned long desdeMs = 0;  // desde quando esta no estado atual
};
Solo solo[N_CANAIS];

DHT dhtA(PIN_DHT_A, DHT22);
// v1.2: o "sensor 2" (ar2T/ar2U) passa a ser um SHT40 no I2C (SDA 21 / SCL 22).
// Assim o CSV, o Google Sheets e o Grafana (temp2/ur2) recebem o SHT40 sem mudar colunas,
// e a serie do DHT22 (ar_temp/ar_ur/dpv_kpa) continua igual.
Adafruit_SHT4x sht4;
bool shtOk = false;
uint8_t shtFalhas = 0;
unsigned long shtTentMs = 0;
float arT = NAN, arU = NAN, ar2T = NAN, ar2U = NAN;
unsigned long arOkMs = 0, ar2OkMs = 0;

float calcDPV(float t, float u) {
  if (isnan(t) || isnan(u)) return NAN;
  float es = 0.6108f * expf(17.27f * t / (t + 237.3f));
  return es * (1.0f - u / 100.0f);
}

float pctDe(int i, float mv) {
  const Canal& c = cfg.c[i];
  float d = (float)c.seco - (float)c.molhado;
  if (fabsf(d) < 50) return NAN;
  float p = 100.0f * ((float)c.seco - mv) / d;
  if (p < 0) p = 0;
  if (p > 100) p = 100;
  return p;
}

void i2cScan() {
  Serial.printf("I2C scan (SDA %d, SCL %d):", PIN_SDA, PIN_SCL);
  int n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) { Serial.printf(" 0x%02X", a); n++; }
  }
  if (!n) Serial.print(" nenhum dispositivo");
  Serial.println();
}

void shtInicia(bool primeira) {
  if (primeira) { Wire.begin(PIN_SDA, PIN_SCL); Wire.setClock(100000); i2cScan(); }
  shtTentMs = millis();
  shtOk = sht4.begin(&Wire);
  if (shtOk) {
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);
    shtFalhas = 0;
    Serial.printf("SHT40 ok (serial 0x%08lX)\n", (unsigned long)sht4.readSerial());
  } else if (primeira) Serial.println("SHT40 NAO encontrado em 0x44");
}

void sensoresInicia() {
  analogReadResolution(12);
  for (int i = 0; i < N_CANAIS; i++) { pinMode(PIN_SOLO[i], INPUT); analogSetPinAttenuation(PIN_SOLO[i], ADC_11db); }
  dhtA.begin();
  shtInicia(true);
}

static int cmpU16(const void* a, const void* b) { return (int)*(const uint16_t*)a - (int)*(const uint16_t*)b; }

void leCanal(int i, float& media, float& ruido) {
  const int N = 64;
  uint16_t v[N];
  for (int k = 0; k < N; k++) { v[k] = analogReadMilliVolts(PIN_SOLO[i]); delayMicroseconds(150); }
  qsort(v, N, sizeof(uint16_t), cmpU16);
  uint32_t s = 0;
  for (int k = N / 4; k < 3 * N / 4; k++) s += v[k];
  media = s / (float)(N / 2);
  ruido = (float)v[N * 9 / 10] - (float)v[N / 10];
}

void leSolo() {
  for (int i = 0; i < N_CANAIS; i++) {
    Solo& s = solo[i];
    if (!cfg.c[i].ativo) {
      if (s.est != EST_DESATIVADO) { s.est = EST_DESATIVADO; s.desdeMs = millis(); }
      s.pct = NAN; continue;
    }
    float m, r;
    leCanal(i, m, r);
    s.mvInst = m;
    s.ruido = r;
    uint8_t e = EST_OK;
    if (m < SOLO_MIN_MV) e = EST_SEM_SINAL;
    else if (m > SOLO_MAX_MV) e = EST_SATURADO;
    else if (r > SOLO_RUIDO_MAX) e = EST_INSTAVEL;
    // so troca de estado depois de 3 leituras iguais seguidas (6 s)
    if (e == s.candidato) { if (s.repet < 255) s.repet++; } else { s.candidato = e; s.repet = 1; }
    if (s.est == EST_DESATIVADO) { s.est = EST_INICIANDO; s.desdeMs = millis(); }
    if (s.repet >= 3 && s.est != e) { s.est = e; s.desdeMs = millis(); if (e == EST_OK) s.mv = -1; }
    if (s.est == EST_OK || (s.est == EST_INICIANDO && e == EST_OK)) {
      s.mv = (s.mv < 0) ? m : s.mv + SOLO_ALFA * (m - s.mv);
      s.pct = pctDe(i, s.mv);
    } else s.pct = NAN;
  }
}

// Le um sensor a cada 2,5 s, alternando DHT22 (A) e SHT40 (B): cada um a cada 5 s
void climaLoop() {
  static unsigned long ult = 0;
  static bool vezB = false;
  if (millis() - ult < 2500) return;
  ult = millis();
  float t = NAN, u = NAN;
  if (vezB) {
    if (!shtOk) { if (millis() - shtTentMs > 30000UL) shtInicia(false); }
    else {
      sensors_event_t eu, et;
      if (sht4.getEvent(&eu, &et)) { t = et.temperature; u = eu.relative_humidity; shtFalhas = 0; }
      else if (++shtFalhas >= 5) { shtOk = false; Serial.println("SHT40 sem resposta: tentando de novo a cada 30 s"); }
    }
  } else {
    t = dhtA.readTemperature();
    u = dhtA.readHumidity();
  }
  bool ok = !isnan(t) && !isnan(u) && u >= 0 && u <= 100.5f && t > -20 && t < 70;
  if (u > 100) u = 100;
  if (vezB) {
    if (ok) { ar2T = t; ar2U = u; ar2OkMs = millis(); }
    else if (ar2OkMs && millis() - ar2OkMs > 60000UL) { ar2T = NAN; ar2U = NAN; }
  } else {
    if (ok) { arT = t; arU = u; arOkMs = millis(); }
    else if (millis() - arOkMs > 60000UL) { arT = NAN; arU = NAN; }
  }
  vezB = !vezB;
}

// bits: 0..3 = T1..T4 com problema | 4 = DHT principal sem leitura
uint8_t mascaraFalhas() {
  uint8_t m = 0;
  for (int i = 0; i < N_CANAIS; i++) if (solo[i].est != EST_OK && solo[i].est != EST_DESATIVADO) m |= (1 << i);
  if (isnan(arT)) m |= 0x10;
  return m;
}
