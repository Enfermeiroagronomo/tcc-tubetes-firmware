# TCC Tubetes — firmware de monitoramento (v1.2.0)

Firmware para ESP32 usado no Trabalho de Conclusão de Curso **"Avaliação de biochar como componente de substrato na aclimatação de mudas clonais de eucalipto (*Corymbia* sp.)"** (Engenharia Agronômica, ESALQ/USP, 2026), de Cristiano Alves dos Santos; orientador: Prof. Dr. Paulo Hercílio Viegas Rodrigues.

## O que faz
- Lê 4 sensores capacitivos de umidade do substrato (HW-390), o DHT22 e o SHT40 (temperatura e umidade relativa do ar) e calcula o DPV (Tetens).
- Grava uma linha a cada 5 min na memória da placa (LittleFS, com fila offline) e envia para Google Sheets (Apps Script), Grafana Cloud (Influx line protocol) e Telegram (/status e alertas).
- Painel local em `http://tubetes.local`.

## Versões
| versão | período em campo | mudança |
|---|---|---|
| 1.1.0 | 29/09 a 07/10/2026 | integração com Google Sheets, Grafana e Telegram |
| 1.2.0 | 07/10/2026 19:15 até o fim da série (15/11/2026) | leitura do SHT40 (referência do ar) |

SHA-256 de `firmware_v1.2.0/TCC_Tubetes.ino`: `e9b0efe6e678bbc1fc384f7d7b14121773b1f5437849d27c3aad0dc97f6a83a4`

## Segurança
Nenhuma credencial está no código: rede Wi-Fi, URL do Apps Script, token do Telegram e chave do Grafana são configurados pelo painel da placa e ficam na memória dela. As senhas padrão do ponto de acesso de configuração (`tubetes123`) e do OTA (`1234`) devem ser trocadas em qualquer novo uso.

## Como citar
SANTOS, Cristiano Alves dos. **TCC Tubetes**: firmware de monitoramento da umidade do substrato e do ar em tubetes com ESP32. Versão 1.2.0. Cerquilho, 2026. Disponível em: https://github.com/Enfermeiroagronomo/tcc-tubetes-firmware.
