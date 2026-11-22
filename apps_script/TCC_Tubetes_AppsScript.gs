/**
 * TCC TUBETES — Apps Script (colar numa planilha Google: Extensões > Apps Script)
 *
 *  doPost : recebe as linhas da ESP32 (protegido por CHAVE) e grava na aba "dados"
 *           - ignora linhas com epoch já gravado (reenvio da fila não duplica)
 *  doGet  : painel para o celular (cartões + gráficos 24 h / 7 d / 30 d / tudo)
 *
 *  Instalação (uma vez):
 *   1. Selecione a função "configurar" e clique em Executar -> autorize.
 *      O registro de execução mostra a CHAVE (copie para o painel da placa).
 *   2. Implantar > Nova implantação > tipo "App da Web"
 *      Executar como: Eu | Quem pode acessar: Qualquer pessoa -> Implantar
 *   3. Copie a URL que termina em /exec e cole no painel da placa (card Google).
 *   4. (Vigia) Execute "instalarVigia" uma vez: a cada 15 min o script confere se a
 *      placa falou nos ultimos 20 min; se nao, avisa no Telegram (queda de energia,
 *      Wi-Fi, travamento). O token/chat do Telegram chegam da propria placa.
 *   5. (Chuva) Execute "instalarAlertaChuva" uma vez: a cada 30 min consulta a previsao
 *      horaria do Open-Meteo para as coordenadas do experimento e avisa no Telegram
 *      quando ha chuva provavel nas proximas 3 h (um aviso por evento, sem repetir).
 *      Execute "testarChuva" para ver no registro o que o script esta lendo agora.
 */

const ABA = 'dados';

function configurar() {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  ss.setSpreadsheetTimeZone('America/Sao_Paulo');
  const p = PropertiesService.getScriptProperties();
  let k = p.getProperty('CHAVE');
  if (!k) { k = Utilities.getUuid().replace(/-/g, '').slice(0, 16); p.setProperty('CHAVE', k); }
  if (!ss.getSheetByName(ABA)) {
    const sh = ss.insertSheet(ABA);
    sh.appendRow(['data_hora', 'epoch', 'mv1', 'mv2', 'mv3', 'mv4', 'pct1', 'pct2', 'pct3', 'pct4',
                  'ar_temp', 'ar_ur', 'dpv_kpa', 'ar2_temp', 'ar2_ur', 'falhas', 'rssi']);
    sh.setFrozenRows(1);
  }
  Logger.log('CHAVE para colar no painel da placa: ' + k);
  instalarAlertaChuva();                 // (re)instala o alerta de chuva a cada 30 min
  return k;
}

function json_(o) {
  return ContentService.createTextOutput(JSON.stringify(o)).setMimeType(ContentService.MimeType.JSON);
}

function doPost(e) {
  const lock = LockService.getScriptLock();
  try { lock.waitLock(20000); } catch (err) { return json_({ ok: false, erro: 'ocupado, tente de novo' }); }
  try {
    const body = JSON.parse(e.postData.contents);
    const p = PropertiesService.getScriptProperties();
    if (!body.k || body.k !== p.getProperty('CHAVE')) return json_({ ok: false, erro: 'chave invalida' });
    const ss = SpreadsheetApp.getActiveSpreadsheet();
    let sh = ss.getSheetByName(ABA);
    const cab = ['data_hora'].concat(String(body.cab || '').split(','));
    if (!sh) { sh = ss.insertSheet(ABA); sh.appendRow(cab); sh.setFrozenRows(1); }
    const ncol = cab.length;
    const ultimo = Number(p.getProperty('ULTIMO_EPOCH') || 0);
    let maxEp = ultimo;
    const linhas = [];
    (body.linhas || []).forEach(function (l) {
      const v = String(l).split(',');
      const ep = Number(v[0]);
      if (!ep || ep <= maxEp) return;               // já gravada ou fora de ordem
      const row = [new Date(ep * 1000)].concat(v.map(function (x) { return x === '' ? '' : Number(x); }));
      while (row.length < ncol) row.push('');
      row.length = ncol;
      linhas.push(row);
      maxEp = ep;
    });
    if (linhas.length) {
      sh.getRange(sh.getLastRow() + 1, 1, linhas.length, ncol).setValues(linhas);
      p.setProperty('ULTIMO_EPOCH', String(maxEp));
    }
    if (body.nomes) p.setProperty('NOMES', JSON.stringify(body.nomes));
    if (body.tg_token && body.tg_chat) { p.setProperty('TG_TOKEN', body.tg_token); p.setProperty('TG_CHAT', String(body.tg_chat)); }
    p.setProperty('ULTIMO_CONTATO', String(Date.now()));
    return json_({ ok: true, n: linhas.length });
  } catch (err) {
    return json_({ ok: false, erro: String(err).slice(0, 110) });
  } finally {
    lock.releaseLock();
  }
}

// ---------- dados para o painel ----------
function dadosPainel(horas) {
  const ss = SpreadsheetApp.getActiveSpreadsheet();
  const sh = ss.getSheetByName(ABA);
  const p = PropertiesService.getScriptProperties();
  const nomes = JSON.parse(p.getProperty('NOMES') || '["T1","T2","T3","T4"]');
  const contato = Number(p.getProperty('ULTIMO_CONTATO') || 0);
  const out = { nomes: nomes, contato: contato, t: [], p: [[], [], [], []], mv: [[], [], [], []], ta: [], ua: [], dpv: [], planilha: ss.getUrl() };
  if (!sh || sh.getLastRow() < 2) return out;
  const n = sh.getLastRow() - 1;
  // ~288 linhas/dia a cada 5 min: lê só o necessário
  const linhasPorHora = 13;
  const qtd = horas > 0 ? Math.min(n, Math.ceil(horas * linhasPorHora) + 20) : n;
  const vals = sh.getRange(sh.getLastRow() - qtd + 1, 2, qtd, 13).getValues(); // epoch .. dpv
  const lim = horas > 0 ? (Date.now() / 1000 - horas * 3600) : 0;
  const passo = Math.max(1, Math.ceil(vals.length / 1500));           // no máx ~1500 pontos
  for (let i = 0; i < vals.length; i += passo) {
    const r = vals[i];
    if (!r[0] || r[0] < lim) continue;
    out.t.push(r[0]);
    for (let c = 0; c < 4; c++) { out.mv[c].push(r[1 + c] === '' ? null : r[1 + c]); out.p[c].push(r[5 + c] === '' ? null : r[5 + c]); }
    out.ta.push(r[9] === '' ? null : r[9]); out.ua.push(r[10] === '' ? null : r[10]); out.dpv.push(r[11] === '' ? null : r[11]);
  }
  return out;
}

function doGet() {
  return HtmlService.createHtmlOutput(PAINEL_HTML)
    .setTitle('TCC Tubetes')
    .addMetaTag('viewport', 'width=device-width, initial-scale=1');
}

const PAINEL_HTML = [
'<!doctype html><html><head><meta charset="utf-8"><base target="_top">',
'<script src="https://cdnjs.cloudflare.com/ajax/libs/Chart.js/4.4.1/chart.umd.min.js"></script>',
'<style>',
':root{--bg:#f4f6f3;--card:#fff;--tx:#1d2a1f;--mu:#667066;--bd:#dfe5dc}',
'@media(prefers-color-scheme:dark){:root{--bg:#121613;--card:#1c221d;--tx:#e6ece6;--mu:#98a398;--bd:#2c352d}}',
'body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--tx)}',
'main{max-width:980px;margin:auto;padding:12px 12px 40px}h1{font-size:20px;margin:4px 4px 2px}small{color:var(--mu)}',
'.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px;margin-top:10px}',
'.c{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px}',
'.big{font-size:30px;font-weight:700}.mu{color:var(--mu);font-size:13px}',
'.bt{display:flex;gap:6px;margin:14px 0 6px;flex-wrap:wrap}.bt button{font:inherit;padding:6px 12px;border-radius:18px;border:1px solid var(--bd);background:var(--card);color:var(--tx)}',
'.bt button.on{background:#1b7f5b;color:#fff;border-color:#1b7f5b}h2{font-size:15px;margin:16px 4px 6px}',
'.ch{position:relative;height:260px}a{color:#1b7f5b}',
'</style></head><body><main>',
'<h1>🌱 TCC Tubetes</h1><small id="sub">carregando...</small>',
'<div class="g" id="cards"></div>',
'<div class="bt" id="bt"><button data-h="24" class="on">24 h</button><button data-h="168">7 dias</button><button data-h="720">30 dias</button><button data-h="0">Tudo</button></div>',
'<h2>Umidade do substrato (% entre seco e molhado)</h2><div class="c"><div class="ch"><canvas id="g1"></canvas></div></div>',
'<h2>Sinal bruto (mV) — menor = mais úmido</h2><div class="c"><div class="ch"><canvas id="g2"></canvas></div></div>',
'<h2>Ar: temperatura, UR e DPV</h2><div class="c"><div class="ch"><canvas id="g3"></canvas></div></div>',
'<p class="mu" id="rod"></p>',
'</main><script>',
'var COR=["#1b7f5b","#c2571a","#3a5fc8","#9b3aa8"],G={},H=24;',
'function fmt(ep){return new Date(ep*1000).toLocaleString("pt-BR",{day:"2-digit",month:"2-digit",hour:"2-digit",minute:"2-digit"})}',
'function ult(a){for(var i=a.length-1;i>=0;i--)if(a[i]!=null)return a[i];return null}',
'function carrega(){document.getElementById("sub").textContent="carregando...";google.script.run.withSuccessHandler(mostra).withFailureHandler(function(e){document.getElementById("sub").textContent="erro: "+e}).dadosPainel(H)}',
'function mostra(d){var n=d.t.length,sub=document.getElementById("sub");',
' var min=d.contato?Math.round((Date.now()-d.contato)/60000):null;',
' sub.textContent=n?("última leitura: "+fmt(d.t[n-1])+(min!=null?" · placa falou há "+min+" min":"")+(min>20?" ⚠️":"")):"sem dados ainda";',
' var h="";for(var c=0;c<4;c++){var v=ult(d.p[c]),m=ult(d.mv[c]);h+="<div class=\\"c\\"><div style=\\"color:"+COR[c]+";font-weight:600\\">T"+(c+1)+" "+(d.nomes[c]||"")+"</div><div class=\\"big\\">"+(v==null?"--":Math.round(v)+"%")+"</div><div class=\\"mu\\">"+(m==null?"":m+" mV")+"</div></div>"}',
' var ta=ult(d.ta),ua=ult(d.ua),dp=ult(d.dpv);h+="<div class=\\"c\\"><div style=\\"font-weight:600\\">Ar</div><div class=\\"big\\">"+(ta==null?"--":ta.toFixed(1)+"°")+"</div><div class=\\"mu\\">"+(ua==null?"":"UR "+Math.round(ua)+"% · DPV "+(dp==null?"--":dp.toFixed(2))+" kPa")+"</div></div>";',
' document.getElementById("cards").innerHTML=h;',
' var lab=d.t.map(fmt);',
' graf("g1",lab,d.p.map(function(s,c){return{label:"T"+(c+1)+" "+(d.nomes[c]||""),data:s,borderColor:COR[c]}}),{y:{min:0,max:100}});',
' graf("g2",lab,d.mv.map(function(s,c){return{label:"T"+(c+1),data:s,borderColor:COR[c]}}),{});',
' graf("g3",lab,[{label:"Temp (°C)",data:d.ta,borderColor:"#c62828",yAxisID:"y"},{label:"UR (%)",data:d.ua,borderColor:"#1565c0",yAxisID:"y1"},{label:"DPV (kPa)",data:d.dpv,borderColor:"#6d4c41",yAxisID:"y2"}],',
'   {y:{position:"left"},y1:{position:"right",min:0,max:100,grid:{drawOnChartArea:false}},y2:{display:false,min:0}});',
' document.getElementById("rod").innerHTML=n+" pontos · <a href=\\""+d.planilha+"\\">abrir planilha</a>"}',
'function graf(id,lab,ds,esc){ds.forEach(function(x){x.pointRadius=0;x.borderWidth=2;x.spanGaps=false;x.tension=.2});',
' var tx=getComputedStyle(document.body).color;if(G[id])G[id].destroy();',
' var s={x:{ticks:{maxTicksLimit:6,color:tx}}};for(var k in esc){s[k]=esc[k];s[k].ticks={color:tx}}',
' G[id]=new Chart(document.getElementById(id),{type:"line",data:{labels:lab,datasets:ds},options:{animation:false,maintainAspectRatio:false,interaction:{mode:"index",intersect:false},plugins:{legend:{labels:{color:tx,boxWidth:12}}},scales:s}})}',
'document.getElementById("bt").onclick=function(e){if(!e.target.dataset.h)return;H=+e.target.dataset.h;[].forEach.call(this.children,function(b){b.className=b==e.target?"on":""});carrega()};',
'carrega();setInterval(carrega,300000);',
'</script></body></html>'
].join('\n');


// ---------- vigia: placa sem comunicacao -> Telegram ----------
function telegram_(txt) {
  const p = PropertiesService.getScriptProperties();
  const tok = p.getProperty('TG_TOKEN'), chat = p.getProperty('TG_CHAT');
  if (!tok || !chat) return false;
  const r = UrlFetchApp.fetch('https://api.telegram.org/bot' + tok + '/sendMessage', {
    method: 'post', contentType: 'application/json', muteHttpExceptions: true,
    payload: JSON.stringify({ chat_id: chat, text: txt })
  });
  return r.getResponseCode() === 200;
}

function vigia() {
  const p = PropertiesService.getScriptProperties();
  const contato = Number(p.getProperty('ULTIMO_CONTATO') || 0);
  if (!contato) return;
  const min = Math.round((Date.now() - contato) / 60000);
  const avisado = p.getProperty('VIGIA_AVISADO') === '1';
  if (min >= 20 && !avisado) {
    if (telegram_('🚨 TCC Tubetes: a placa nao envia dados ha ' + min + ' min. Verifique energia, Wi-Fi e cabo USB. ' +
                  '(Se foi so a internet, os dados estao guardados na placa e sobem quando ela voltar.)')) p.setProperty('VIGIA_AVISADO', '1');
  } else if (min < 20 && avisado) {
    telegram_('✅ TCC Tubetes: a placa voltou a enviar dados.');
    p.setProperty('VIGIA_AVISADO', '0');
  }
}

function instalarVigia() {
  ScriptApp.getProjectTriggers().forEach(function (t) { if (t.getHandlerFunction() === 'vigia') ScriptApp.deleteTrigger(t); });
  ScriptApp.newTrigger('vigia').timeBased().everyMinutes(15).create();
  Logger.log('Vigia instalado: confere a cada 15 min. Telegram configurado: ' + (PropertiesService.getScriptProperties().getProperty('TG_CHAT') ? 'sim' : 'ainda nao (pareie o bot na placa)'));
}

// =====================================================================
//  ALERTA DE CHUVA — previsao horaria Open-Meteo (gratis, sem chave)
//  Modelo numerico de hora em hora: indica a HORA provavel, nao o minuto.
//  Aviso exato de minuto exige radar (nowcasting) — ver PASSO_A_PASSO.
// =====================================================================
const LAT = -23.16427, LON = -47.75754;
const CHUVA_PROB = 60;        // % de chance que dispara o aviso
const CHUVA_MM = 1.0;         // ou mm previstos na hora
const CHUVA_JANELA_H = 3;     // olha as proximas 3 h

function previsaoChuva_() {
  const url = 'https://api.open-meteo.com/v1/forecast?latitude=' + LAT + '&longitude=' + LON +
    '&hourly=precipitation_probability,precipitation&forecast_days=2&timezone=America%2FSao_Paulo&timeformat=unixtime';
  const r = UrlFetchApp.fetch(url, { muteHttpExceptions: true });
  if (r.getResponseCode() !== 200) throw new Error('Open-Meteo HTTP ' + r.getResponseCode());
  const h = JSON.parse(r.getContentText()).hourly;
  const agora = Date.now(), lista = [];
  for (let i = 0; i < h.time.length; i++) {
    const ms = h.time[i] * 1000;                       // inicio da hora (unixtime = UTC)
    if (ms + 3600000 <= agora) continue;               // hora ja terminou
    if (ms > agora + CHUVA_JANELA_H * 3600000) break;
    lista.push({ ms: ms, prob: h.precipitation_probability[i] || 0, mm: h.precipitation[i] || 0 });
  }
  return lista;
}

function hora_(ms) { return Utilities.formatDate(new Date(ms), 'America/Sao_Paulo', "HH'h'"); }

function alertaChuva() {
  const p = PropertiesService.getScriptProperties();
  let lista;
  try { lista = previsaoChuva_(); } catch (e) { Logger.log(e); return; }   // sem internet: tenta na proxima
  const ev = lista.filter(function (x) { return x.prob >= CHUVA_PROB || x.mm >= CHUVA_MM; });
  if (!ev.length) return;
  const inicio = ev[0].ms;
  const ultimo = Number(p.getProperty('CHUVA_EVENTO') || 0);
  // mesmo evento (ou continuacao dele): nao repete. Novo aviso so depois de 3 h sem chuva prevista.
  if (ultimo && inicio <= ultimo + 3 * 3600000) { p.setProperty('CHUVA_EVENTO', String(ev[ev.length - 1].ms)); return; }
  const min = Math.round((inicio - Date.now()) / 60000);
  const quando = min <= 0 ? 'já nesta hora' : 'em ~' + (min >= 60 ? Math.floor(min / 60) + ' h ' : '') + (min % 60) + ' min';
  const total = lista.reduce(function (s, x) { return s + x.mm; }, 0);
  const pmax = Math.max.apply(null, lista.map(function (x) { return x.prob; }));
  let txt = '🌧️ TCC Tubetes: chuva provável a partir das ' + hora_(inicio) + ' (' + quando + ').\n' +
            'Chance máx. ' + pmax + '% · ' + total.toFixed(1).replace('.', ',') + ' mm nas próximas ' + CHUVA_JANELA_H + ' h.\n';
  lista.forEach(function (x) { txt += hora_(x.ms) + ': ' + x.prob + '% · ' + x.mm.toFixed(1).replace('.', ',') + ' mm\n'; });
  txt += 'Fonte: Open-Meteo (modelo horário, não radar). Verifique a cobertura das mudas.';
  if (telegram_(txt)) p.setProperty('CHUVA_EVENTO', String(ev[ev.length - 1].ms));
}

function testarChuva() {
  previsaoChuva_().forEach(function (x) { Logger.log(hora_(x.ms) + '  ' + x.prob + '%  ' + x.mm + ' mm'); });
  Logger.log('Ultimo evento avisado: ' + (PropertiesService.getScriptProperties().getProperty('CHUVA_EVENTO') || 'nenhum'));
}

function instalarAlertaChuva() {
  ScriptApp.getProjectTriggers().forEach(function (t) { if (t.getHandlerFunction() === 'alertaChuva') ScriptApp.deleteTrigger(t); });
  ScriptApp.newTrigger('alertaChuva').timeBased().everyMinutes(30).create();
  Logger.log('Alerta de chuva instalado: confere a cada 30 min (chance >= ' + CHUVA_PROB + '% ou >= ' + CHUVA_MM + ' mm nas proximas ' + CHUVA_JANELA_H + ' h).');
  testarChuva();
}
