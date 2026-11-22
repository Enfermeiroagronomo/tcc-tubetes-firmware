# TCC Tubetes — passo a passo

Uma ESP32 lê 4 sensores capacitivos (um tubete por tratamento) e um DHT22 (ar). A cada 5 min grava uma linha na memória da placa e no Google Sheets. Você acompanha pelo painel no celular e, se quiser, pelo Telegram.

## 1. Ligações (ESP32 DevKit de 38 pinos)

| Peça | Fio | Pino da ESP32 |
|---|---|---|
| Sensor T1 | amarelo (AOUT) | **P34** |
| Sensor T2 | amarelo (AOUT) | **P35** |
| Sensor T3 | amarelo (AOUT) | **P32** |
| Sensor T4 | amarelo (AOUT) | **P33** |
| Os 4 sensores | vermelho (VCC) | trilho **+** → **3V3** |
| Os 4 sensores | preto (GND) | trilho **−** → **GND** |
| DHT22 (ar) | out | **P4** |
| DHT22 | + / − | 3V3 / GND |
| SHT40 (ar, v1.2) | SDA / SCL | **P21** / **P22** |
| SHT40 | VCC / GND | 3V3 / GND |

- Os pinos 34, 35, 32 e 33 ficam em sequência, no lado esquerdo da placa, logo abaixo de 3V3, EN, VP e VN.
- **Os sensores vão no 3V3, não no 5V.** Com 5 V a saída pode passar de 3,3 V, que é o limite do pino.
- Na sua protoboard, os trilhos + e − são **cortados no meio**, perto da coluna 30. Ponha um jumper ligando as duas metades ou use só uma metade.
- Opcional, mas recomendado: um resistor de 100 kΩ de cada pino (34/35/32/33) para o GND. Assim, um sensor solto lê ~0 e o firmware avisa na hora. Sem o resistor, o pino fica "flutuando" e a detecção depende do ruído.

## 2. Preparar os sensores (antes de ir para o substrato)
1. Passe esmalte incolor, cola quente ou verniz na parte eletrônica **acima da linha branca** e nas **bordas cortadas da placa**. A borda da placa absorve água e a leitura vai derivando com os dias.
2. Enterre sempre até a linha, na vertical, no centro do tubete e na mesma profundidade nos quatro.
3. Não troque um sensor de tubete depois de calibrar: cada sensor tem sua própria curva.

## 3. Gravar o firmware (primeira vez por USB)
1. Arduino IDE → abra `Documents/Arduino/TCC_Tubetes/TCC_Tubetes.ino`
2. Ferramentas → Placa: **ESP32 Dev Module** · Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA)**
3. **Porta: confira que é a placa NOVA.** Anote as portas COM antes de plugar e veja qual aparece depois. Não repita o erro de gravar na placa 1.
4. Upload. Se aparecer "Connecting……" e travar, segure o botão **BOOT** até começar.
5. Depois disso as próximas gravações podem ser pelo Wi-Fi: porta de rede `tubetes at <IP>`, senha **1234**.

## 4. Wi-Fi
No celular, conecte na rede **Tubetes-Setup** (senha `tubetes123`) → 192.168.4.1 → Configure WiFi → sua rede 2,4 GHz.
Painel: **http://tubetes.local**, ou pelo IP (aparece no Monitor Serial e no roteador; vale reservar um IP fixo no roteador).

## 5. Calibração (é aqui que o dado vira ciência)
**A. Teste de dispersão dos sensores (10 min).** Anote o mV dos 4 sensores no ar e depois os 4 no mesmo copo d'água, até a linha. Se um deles ficar muito diferente dos outros (mais de ~150 mV), troque esse sensor antes de começar.

**B. Calibração no substrato de cada tratamento:**
- **MOLHADO:** encha o tubete, sature com água, deixe drenar 30 min (capacidade do recipiente). Espere a leitura estabilizar e aperte "Leitura atual = MOLHADO" no painel. **Pese o tubete.**
- **SECO:** num tubete extra com o mesmo substrato seco ao ar, aperte "Leitura atual = SECO". Se preferir, deixe os tubetes secarem e use o valor mais alto que aparecer.
- O "%" do painel é um **índice relativo entre seco e molhado**, não umidade volumetrica.

**C. Curva gravimétrica (recomendado para o TCC).** Pese cada tubete todo dia, no mesmo horário (balança de 0,1 g), e anote na planilha. No Colab, relacione mV × umidade gravimétrica por tratamento. O firmware guarda sempre o **mV bruto**, então dá para refazer a conversão depois sem perder nada.

## 6. Google Sheets (histórico permanente + painel para o celular)
1. Crie a planilha **TCC_Tubetes_Dados** no Drive → Extensões → Apps Script → cole `google/TCC_Tubetes_AppsScript.gs` → salve.
2. Escolha a função `configurar` → Executar → autorize. O registro mostra a **CHAVE**.
3. Implantar → Nova implantação → App da Web · Executar como: **Eu** · Acesso: **Qualquer pessoa** → copie a URL `/exec`.
4. No painel da placa, card ☁️ Google: cole a URL e a CHAVE → Salvar.
5. Abra a URL `/exec` no celular e adicione à tela inicial. Esse é o seu painel de qualquer lugar.

Se a internet cair, as linhas ficam numa fila na placa (vários dias de folga) e sobem sozinhas quando a conexão voltar, sem duplicar.

## 7. Telegram (opcional)
Crie um bot novo no @BotFather, **só para esta placa** (o mesmo token em duas placas dá erro 409). Cole o token no painel e envie ao bot `/parear <código mostrado no painel>`.
Você recebe: /status, alerta de sensor com problema, alerta de tubete abaixo do limite (defina o % no painel) e um resumo diário às 08:00.

## 7b. Dados públicos de tempo (Open-Meteo) e alerta de chuva
**Sem chave e sem custo.** Coordenadas do experimento: −23,16427 / −47,75754 (célula do modelo a 583 m de altitude).

No Grafana, a linha **"Tempo lá fora e previsão de chuva"** mostra:
- **Lá fora agora:** temperatura, UR, DPV e nuvens no ar livre a 2 m. Comparar com o sensor na bancada mostra o efeito do microclima.
- **Chuva:** chance máxima e milímetros previstos nas próximas 6 h, e a próxima hora com chance ≥ 50% ou ≥ 0,5 mm.
- **Barras de 48 h:** chance de chuva hora a hora. Passe o mouse sobre uma barra para ver os mm.
- **Dentro × lá fora:** gráficos de temperatura e de DPV. O DPV traz as mesmas faixas usadas para o eucalipto.

**Telegram:** o Apps Script consulta a previsão a cada 30 min. Quando há chance ≥ 60% ou ≥ 1 mm nas próximas 3 h, ele avisa uma vez por evento, com a hora de início e a tabela das próximas horas. Os limites ficam nas constantes `CHUVA_PROB`, `CHUVA_MM` e `CHUVA_JANELA_H`. Para ver o que o script está lendo agora, execute `testarChuva`. Para reinstalar o gatilho, execute `instalarAlertaChuva`; a função `configurar` também o reinstala.

**O que esses dados não fazem (declarar no método):**
- É previsão de **modelo numérico de hora em hora**. Ela acerta a hora provável, não o minuto. Chuva convectiva de primavera e verão pode errar em ±1–2 h e em alguns km. Aviso "no minuto" exige radar meteorológico (nowcasting), que não tem API aberta estável para esta região.
- O dado do ar livre **não substitui** os sensores na bancada nem a umidade do substrato. Ele serve de contexto e de comparação.
- NASA POWER (radiação diária por satélite) tem atraso de ~5 dias. Serve para a análise do TCC (índice de claridade Rs/Ra), não para tempo real.
- A API do INMET estava instável nos testes. A estação automática mais próxima pode entrar depois como validação.

## 8. Arquivo de dados (colunas)
`data_hora, epoch, mv1..mv4, pct1..pct4, ar_temp, ar_ur, dpv_kpa, ar2_temp, ar2_ur, falhas, rssi`
`falhas` é uma máscara de bits: 1=T1, 2=T2, 4=T3, 8=T4, 16=DHT sem leitura. Se o valor for 0, está tudo ok.

## 9. Limitações (declarar no método)
- **Um tubete por tratamento = n = 1.** O dado é descritivo: sensor e tratamento ficam confundidos, e não dá para fazer teste estatístico com isso. Para inferência, é preciso ≥ 2 sensores por tratamento ou a pesagem de todas as repetições.
- O sensor mede uma faixa de ~1–2 cm em volta da placa. Em tubete pequeno (50–55 cm³) ele ocupa boa parte do volume e atrapalha as raízes. Em tubetes de ≥ 110 cm³ o problema é bem menor.
- O DHT22 erra e satura acima de ~90% de UR. Para a fase de aclimatação, o sensor certo é o SHT40.
- Não tire conclusões com menos de 7 dias de dados, incluindo as tardes.
