// =====================================================================
//  pagina.h — painel local (http://tubetes.local). HTML fora do .ino para o
//  pre-processador do Arduino nao quebrar o JavaScript (erro ja visto).
// =====================================================================
#pragma once

const char PAGINA[] PROGMEM = R"HTML(<!doctype html>
<html lang="pt-br"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>TCC Tubetes</title>
<style>
:root{--bg:#f4f6f3;--card:#fff;--tx:#1d2a1f;--mu:#667066;--bd:#dfe5dc;--ok:#2e7d32;--wa:#c77700;--er:#c62828;
--c0:#1b7f5b;--c1:#c2571a;--c2:#3a5fc8;--c3:#9b3aa8}
@media(prefers-color-scheme:dark){:root{--bg:#121613;--card:#1c221d;--tx:#e6ece6;--mu:#98a398;--bd:#2c352d}}
*{box-sizing:border-box}body{margin:0;font:15px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--tx)}
header{padding:14px 16px 6px}h1{font-size:20px;margin:0}small,.mu{color:var(--mu)}
main{padding:0 12px 40px;max-width:980px;margin:auto}
.g{display:grid;grid-template-columns:repeat(auto-fit,minmax(210px,1fr));gap:10px}
.c{background:var(--card);border:1px solid var(--bd);border-radius:12px;padding:12px}
.big{font-size:34px;font-weight:700;font-variant-numeric:tabular-nums}
.bar{height:8px;background:var(--bd);border-radius:5px;overflow:hidden;margin:6px 0}.bar i{display:block;height:100%}
.tag{display:inline-block;font-size:12px;padding:1px 8px;border-radius:9px;border:1px solid var(--bd)}
.ok{color:var(--ok)}.wa{color:var(--wa)}.er{color:var(--er)}
h2{font-size:16px;margin:18px 4px 8px}canvas{width:100%;height:260px;display:block}
details{margin-top:14px}summary{cursor:pointer;font-weight:600;padding:8px 4px}
input,select,button{font:inherit;padding:7px 9px;border-radius:8px;border:1px solid var(--bd);background:var(--card);color:var(--tx)}
input{width:100%}button{cursor:pointer;background:#1b7f5b;color:#fff;border:0}button.s{background:transparent;color:var(--tx);border:1px solid var(--bd)}
.row{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin:6px 0}.row3{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px}
label{font-size:12px;color:var(--mu)}#msg{position:fixed;left:50%;bottom:16px;transform:translateX(-50%);background:#222;color:#fff;padding:8px 14px;border-radius:9px;display:none;max-width:90%}
.lg span{display:inline-flex;align-items:center;margin-right:12px;font-size:13px}.lg b{width:12px;height:3px;margin-right:5px;display:inline-block}
table{width:100%;border-collapse:collapse;font-size:13px}td{padding:3px 0}
</style></head><body>
<header><h1>🌱 TCC Tubetes</h1><small id="sub">carregando...</small></header>
<main>
<div class="g" id="cards"></div>
<h2>Ar</h2>
<div class="g"><div class="c"><div class="mu">Temperatura / UR</div><div class="big" id="ar">--</div><div id="ar2" class="mu"></div></div>
<div class="c"><div class="mu">DPV (kPa)</div><div class="big" id="dpv">--</div><div class="mu" id="dpvtx"></div></div></div>
<h2>Últimas 48 h <select id="modo" onchange="desenha()"><option value="p">% umidade</option><option value="mv">mV (bruto)</option></select></h2>
<div class="c"><canvas id="cv"></canvas><div class="lg" id="lg"></div></div>
<h2>Dados</h2>
<div class="c"><table id="dados"></table>
<p><a href="/dados.csv">⬇️ Baixar CSV da placa</a></p></div>

<details><summary>⚙️ Calibração e tratamentos</summary>
<div class="c">
<p class="mu">Calibre <b>cada sensor no substrato do seu tratamento</b>: SECO = substrato seco ao ar no tubete; MOLHADO = saturado e drenado por 30 min (capacidade do recipiente). Espere a leitura estabilizar (~1 min) antes de salvar.</p>
<div class="row"><div><label>PIN do painel</label><input id="pin" type="password" placeholder="PIN (padrão 1234)"></div>
<div><label>Registrar a cada (min)</label><input id="int" type="number" min="1" max="60"></div></div>
<div id="cal"></div>
<button onclick="salvaCfg()">Salvar nomes, alertas e intervalo</button>
</div></details>

<details><summary>☁️ Google Sheets (histórico permanente)</summary>
<div class="c"><p class="mu" id="gst"></p>
<label>URL do Web App (Apps Script)</label><input id="gurl" placeholder="https://script.google.com/macros/s/.../exec">
<label>Chave (a mesma do Apps Script)</label><input id="gkey">
<p><button onclick="salvaG()">Salvar</button> <button class="s" onclick="post('/api/google',{apagar:1})">Apagar</button></p></div></details>

<details><summary>📊 Grafana Cloud (tempo real, compartilhar)</summary>
<div class="c"><p class="mu" id="gfst"></p>
<label>Host do Prometheus (ex.: prometheus-prod-40-prod-sa-east-1.grafana.net)</label><input id="gfurl">
<label>ID da instância (número)</label><input id="gfusr">
<label>Token com permissão metrics:write</label><input id="gftok" type="password">
<p><button onclick="post('/api/grafana',{url:$('gfurl').value,usr:$('gfusr').value,tok:$('gftok').value})">Salvar</button> <button class="s" onclick="post('/api/grafana',{apagar:1})">Apagar</button></p></div></details>

<details><summary>📱 Telegram</summary>
<div class="c"><p class="mu" id="tst"></p>
<label>Token do bot (BotFather) — use um bot só para esta placa</label><input id="ttok">
<p><button onclick="salvaT()">Salvar token</button> <button class="s" onclick="post('/api/tg',{desparear:1})">Desparear</button></p></div></details>

<details><summary>🔧 Sistema</summary><div class="c" id="sis"></div>
<p><button class="s" onclick="if(confirm('Reiniciar a placa?'))post('/api/reiniciar',{})">Reiniciar placa</button></p></details>
</main><div id="msg"></div>
<script>
const COR=['var(--c0)','var(--c1)','var(--c2)','var(--c3)'];let S=null,H=null,editando=false;
const $=i=>document.getElementById(i);
try{$('pin').value=localStorage.getItem('pin')||''}catch(e){}
function msg(t){const m=$('msg');m.textContent=t;m.style.display='block';clearTimeout(m._t);m._t=setTimeout(()=>m.style.display='none',3500)}
async function post(u,o){o.pin=$('pin').value;try{localStorage.setItem('pin',o.pin)}catch(e){}
 try{const r=await fetch(u,{method:'POST',body:new URLSearchParams(o)});msg(await r.text());setTimeout(atualiza,800)}catch(e){msg('Falha de conexão')}}
function fmtH(ep){if(!ep)return'--';const d=new Date(ep*1000);return d.toLocaleString('pt-BR',{day:'2-digit',month:'2-digit',hour:'2-digit',minute:'2-digit'})}
function classe(e){return e==0?'ok':(e==4||e==5)?'mu':'er'}
async function atualiza(){try{S=await (await fetch('/api/status')).json()}catch(e){$('sub').textContent='sem conexão com a placa';return}
 const g=S.google,r=S.rede;
 $('sub').textContent=`${fmtH(r.hora)} · Wi-Fi ${r.rssi} dBm · ${r.linhas} linhas · Google: ${['—','ok','ERRO'][g.est]}${g.pend?' (fila)':''} · Grafana: ${['—','ok','ERRO'][S.grafana.est]} · Telegram: ${['—','aguardando','ok','ERRO'][S.tg.est]}`;
 let h='';S.canais.forEach((c,i)=>{const p=c.pct;h+=`<div class="c"><div><b style="color:${COR[i]}">T${i+1}</b> ${c.nome}</div>
 <div class="big">${p==null?'--':p.toFixed(0)+'%'}</div><div class="bar"><i style="width:${p||0}%;background:${COR[i]}"></i></div>
 <span class="tag ${classe(c.est)}">${c.estTxt}</span> <small>${c.mv} mV · ruído ${c.ruido}</small></div>`});$('cards').innerHTML=h;
 const a=S.ar;$('ar').textContent=a.t!=null?`${a.t.toFixed(1)}°C · ${a.u.toFixed(0)}%`:'sem leitura';
 $('ar2').textContent=('t2' in a)?(a.t2!=null?`SHT40: ${a.t2.toFixed(1)}°C · ${a.u2.toFixed(0)}%`:'SHT40: sem leitura'):'';
 $('dpv').textContent=a.dpv!=null?a.dpv.toFixed(2):'--';
 $('dpvtx').textContent=a.dpv==null?'':a.dpv<0.4?'ar saturado (risco de fungo)':a.dpv<=1.2?'faixa confortável':a.dpv<=1.5?'demanda moderada':'estresse hídrico';
 $('dados').innerHTML=S.canais.map((c,i)=>`<tr><td>T${i+1} ${c.nome}</td><td>seco ${c.seco} mV · molhado ${c.molh} mV · pino P${c.pino}</td></tr>`).join('');
 $('gst').textContent=`Estado: ${['não configurado','ok','ERRO'][g.est]} · ${g.envios} linhas enviadas · ${g.falhas} falhas${g.seg>=0?' · último envio há '+g.seg+' s':''}${g.erro?' · '+g.erro:''}`;
 const f=S.grafana;$('gfst').textContent=`Estado: ${['não configurado','ok','ERRO'][f.est]} · ${f.envios} envios · ${f.falhas} falhas${f.seg>=0?' · último há '+f.seg+' s':''}${f.erro?' · '+f.erro:''}`;
 const t=S.tg;$('tst').innerHTML=['Sem token.',`Aguardando pareamento: no Telegram, envie ao seu bot <b>/parear ${t.codigo}</b>`,'Pareado ✅','Erro: '+t.erro][t.est];
 $('sis').innerHTML=`Firmware ${S.fw} · IP ${r.ip} · ligada há ${(r.up/3600).toFixed(1)} h<br>Memória de dados: ${(r.fsUsado/1024).toFixed(0)} de ${(r.fsTotal/1024).toFixed(0)} KB`;
 if(!editando)montaCal();
}
function montaCal(){$('int').value=S.rede.int;let h='';S.canais.forEach((c,i)=>{h+=`<div class="c" style="margin:8px 0"><b style="color:${COR[i]}">T${i+1}</b> <small>(P${c.pino}) agora: ${c.mv} mV</small>
 <div class="row"><div><label>Nome do tratamento</label><input id="n${i}" value="${c.nome}"></div><div><label>Alerta abaixo de (%) · 0 = sem</label><input id="a${i}" type="number" min="0" max="95" value="${c.alerta}"></div></div>
 <div class="row3"><div><label>Seco (mV)</label><input id="s${i}" type="number" value="${c.seco}"></div><div><label>Molhado (mV)</label><input id="m${i}" type="number" value="${c.molh}"></div>
 <div><label>Canal</label><select id="on${i}"><option value="1"${c.ativo?' selected':''}>ativo</option><option value="0"${c.ativo?'':' selected'}>desligado</option></select></div></div>
 <p><button class="s" onclick="cal(${i},'seco')">Leitura atual = SECO</button> <button class="s" onclick="cal(${i},'molhado')">Leitura atual = MOLHADO</button>
 <button class="s" onclick="calM(${i})">Salvar valores digitados</button></p></div>`});$('cal').innerHTML=h;
 document.querySelectorAll('#cal input,#cal select,#int').forEach(e=>{e.onfocus=()=>editando=true})}
function cal(i,p){if(confirm(`Salvar a leitura atual de T${i+1} como ${p.toUpperCase()}?`)){editando=false;post('/api/calib',{c:i,ponto:p})}}
async function calM(i){editando=false;await post('/api/calib',{c:i,ponto:'seco',mv:$('s'+i).value});setTimeout(()=>post('/api/calib',{c:i,ponto:'molhado',mv:$('m'+i).value}),600)}
function salvaCfg(){const o={int:$('int').value};for(let i=0;i<4;i++){o['n'+i]=$('n'+i).value;o['a'+i]=$('a'+i).value;o['on'+i]=$('on'+i).value}editando=false;post('/api/cfg',o)}
function salvaG(){post('/api/google',{url:$('gurl').value,key:$('gkey').value})}
function salvaT(){post('/api/tg',{token:$('ttok').value})}
async function hist(){try{H=await (await fetch('/api/hist')).json();desenha()}catch(e){}}
function desenha(){const cv=$('cv'),ctx=cv.getContext('2d'),dpr=devicePixelRatio||1,W=cv.clientWidth,Hh=cv.clientHeight;cv.width=W*dpr;cv.height=Hh*dpr;ctx.scale(dpr,dpr);
 const st=getComputedStyle(document.body),tx=st.getPropertyValue('--mu'),bd=st.getPropertyValue('--bd');ctx.clearRect(0,0,W,Hh);
 if(!H||H.t.length<2){ctx.fillStyle=tx;ctx.fillText('Aguardando dados (1 ponto a cada 5 min)...',10,20);return}
 const m=$('modo').value,ser=m=='p'?H.p:H.mv.map(s=>s.map(v=>v||null));let lo=0,hi=100;
 if(m=='mv'){const v=ser.flat().filter(x=>x!=null);lo=Math.floor((Math.min(...v)-50)/100)*100;hi=Math.ceil((Math.max(...v)+50)/100)*100}
 const L=40,R=8,T=8,B=22,t0=H.t[0],t1=H.t[H.t.length-1],X=t=>L+(t-t0)/(t1-t0||1)*(W-L-R),Y=v=>T+(1-(v-lo)/(hi-lo||1))*(Hh-T-B);
 ctx.strokeStyle=bd;ctx.fillStyle=tx;ctx.font='11px system-ui';ctx.lineWidth=1;
 for(let k=0;k<=4;k++){const v=lo+(hi-lo)*k/4,y=Y(v);ctx.beginPath();ctx.moveTo(L,y);ctx.lineTo(W-R,y);ctx.stroke();ctx.fillText(Math.round(v)+(m=='p'?'%':''),2,y+4)}
 for(let k=0;k<=4;k++){const t=t0+(t1-t0)*k/4,d=new Date(t*1000);ctx.fillText(d.toLocaleString('pt-BR',{day:'2-digit',hour:'2-digit',minute:'2-digit'}),Math.min(X(t)-20,W-70),Hh-6)}
 ser.forEach((s,i)=>{ctx.strokeStyle=st.getPropertyValue('--c'+i);ctx.lineWidth=2;ctx.beginPath();let on=false;
  s.forEach((v,k)=>{if(v==null){on=false;return}const x=X(H.t[k]),y=Y(v);on?ctx.lineTo(x,y):ctx.moveTo(x,y);on=true});ctx.stroke()});
 $('lg').innerHTML=(S?S.canais:[0,1,2,3]).map((c,i)=>`<span><b style="background:${COR[i]}"></b>T${i+1} ${c.nome||''}</span>`).join('')}
atualiza();hist();setInterval(atualiza,5000);setInterval(hist,60000);addEventListener('resize',desenha);
</script></body></html>)HTML";
