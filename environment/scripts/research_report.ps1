# environment/scripts/research_report.ps1
#
# Regenerates $ResearchHtmlPath -- a single self-contained HTML view of
# the Phase 3 research DB -- by dumping each table as JSON via the
# sqlite3 CLI and embedding it in a static page (vanilla JS, no external
# assets, opens from file://). Re-run after every batch of DB writes.
#
#   .\environment\scripts\research_report.ps1            # regenerate once
#   .\environment\scripts\research_report.ps1 -Serve     # + serve on :8899
#   .\environment\scripts\research_report.ps1 -Watch 30  # regenerate every 30s
#
# Implementation note: the HTML/CSS/JS below is a single-quoted here-string
# (@'...'@) so PowerShell does NOT touch the JS ${...} template literals.
# The DB data and timestamp are spliced in afterwards by .Replace().

param(
    [switch]$Serve,
    [int]$Watch = 0,
    [int]$Port = 8899
)

. (Join-Path $PSScriptRoot '..\config.ps1')

if (-not (Test-Path $SqliteExe))     { throw "sqlite3 not found at $SqliteExe" }
if (-not (Test-Path $ResearchDbPath)) { throw "research.db not found at $ResearchDbPath -- run init_research_db.ps1" }

function Get-Json([string]$sql) {
    $raw = (& $SqliteExe $ResearchDbPath -json $sql | Out-String).Trim()
    if ([string]::IsNullOrWhiteSpace($raw)) { return '[]' }
    return $raw
}

$Template = @'
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FDNY decomp - research state</title>
<style>
:root{--bg:#fff;--fg:#1b1f24;--muted:#5b6570;--line:#e2e6ea;--card:#f6f8fa;--accent:#0969da;
      --hi:#1a7f37;--med:#9a6700;--lo:#8b949e;--chip:#eaeef2;}
@media (prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--muted:#8b949e;--line:#30363d;
      --card:#161b22;--accent:#4493f8;--hi:#3fb950;--med:#d29922;--lo:#6e7681;--chip:#21262d;}}
*{box-sizing:border-box}
body{margin:0;font:14px/1.5 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif;background:var(--bg);color:var(--fg)}
header{padding:18px 22px;border-bottom:1px solid var(--line)}
h1{margin:0 0 4px;font-size:17px}
.sub{color:var(--muted);font-size:12px}
.counts{display:flex;flex-wrap:wrap;gap:14px;margin-top:12px}
.count{background:var(--card);border:1px solid var(--line);border-radius:8px;padding:8px 12px;min-width:96px}
.count b{display:block;font-size:20px}
.count span{color:var(--muted);font-size:11px;text-transform:uppercase;letter-spacing:.04em}
nav{display:flex;gap:2px;padding:0 22px;border-bottom:1px solid var(--line);flex-wrap:wrap}
nav button{background:none;border:none;border-bottom:2px solid transparent;color:var(--muted);
      padding:10px 14px;font:inherit;cursor:pointer}
nav button.active{color:var(--fg);border-bottom-color:var(--accent)}
main{padding:18px 22px;max-width:1400px}
.panel{display:none}.panel.active{display:block}
table{border-collapse:collapse;width:100%;font-size:13px}
th,td{text-align:left;padding:6px 9px;border-bottom:1px solid var(--line);vertical-align:top}
th{position:sticky;top:0;background:var(--bg);cursor:pointer;white-space:nowrap}
tr:hover td{background:var(--card)}
code,.mono{font-family:ui-monospace,SFMono-Regular,Consolas,monospace;font-size:12px}
.chip{display:inline-block;background:var(--chip);border-radius:10px;padding:1px 8px;font-size:11px;white-space:nowrap}
.c-high{color:var(--hi);font-weight:600}.c-medium{color:var(--med);font-weight:600}.c-low{color:var(--lo)}
.controls{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:12px}
.controls input,.controls select{background:var(--card);border:1px solid var(--line);color:var(--fg);
      border-radius:6px;padding:6px 8px;font:inherit}
.bar{height:8px;border-radius:4px;background:var(--chip);overflow:hidden;display:inline-flex;width:120px;vertical-align:middle}
.bar>i{display:block;height:100%}.bar .h{background:var(--hi)}.bar .m{background:var(--med)}.bar .l{background:var(--lo)}
.ev{color:var(--muted);font-size:11px}
details{margin:2px 0}summary{cursor:pointer}
.wrap{overflow-x:auto}
.kb-card{border:1px solid var(--line);border-radius:8px;margin:8px 0;background:var(--card)}
.kb-card>summary{padding:10px 14px;font-weight:600;list-style:none;display:flex;gap:10px;align-items:baseline}
.kb-card>summary::-webkit-details-marker{display:none}
.kb-card[open]>summary{border-bottom:1px solid var(--line)}
.kb-body{padding:4px 16px 14px}
.kb-body h4{margin:14px 0 4px;font-size:13px;color:var(--muted);text-transform:uppercase;letter-spacing:.04em}
.kb-body p{margin:8px 0}.kb-body ul{margin:8px 0;padding-left:20px}.kb-body li{margin:3px 0}
.kb-body code{background:var(--chip);border-radius:4px;padding:0 4px}
.kb-body table.mdt{border-collapse:collapse;margin:12px 0;font-size:12px;display:block;max-width:100%;overflow-x:auto}
.kb-body table.mdt th,.kb-body table.mdt td{border:1px solid var(--line);padding:5px 9px;text-align:left;vertical-align:top;position:static}
.kb-body table.mdt th{background:var(--chip);font-weight:600;white-space:nowrap;cursor:default}
.kb-body table.mdt tr:hover td{background:transparent}
.kb-body table.mdt code{background:var(--chip)}
.kb-sub{color:var(--muted);font-weight:400;font-size:12px}
</style>
</head>
<body>
<header>
  <h1>FDNY Firefighter: American Hero &mdash; decompilation research state</h1>
  <div class="sub">Genesis3D v1.1 engine &middot; "Everblaze 2" &middot; generated __GENERATED__ &middot; auto-built from research.db</div>
  <div class="counts" id="counts"></div>
</header>
<nav id="nav"></nav>
<main>
  <section class="panel" id="p-Overview"></section>
  <section class="panel" id="p-Symbols"></section>
  <section class="panel" id="p-Hypotheses"></section>
  <section class="panel" id="p-Knowledgebase"></section>
  <section class="panel" id="p-Tasks"></section>
  <section class="panel" id="p-Events"></section>
</main>
<script>
const DATA = __DATA__;
const esc = s => (s==null?'':String(s)).replace(/[&<>]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;'}[c]));
const conf = c => `<span class="c-${esc(c)}">${esc(c||'')}</span>`;
const tabs = ['Overview','Symbols','Hypotheses','Knowledgebase','Tasks','Events'];

document.getElementById('counts').innerHTML = [
  ['symbols',DATA.symbols.length],
  ['high conf', DATA.symbols.filter(s=>s.confidence==='high').length],
  ['hypotheses',DATA.hypotheses.length],
  ['evidence',DATA.evidence.length],
  ['tasks open',DATA.tasks.filter(t=>t.status!=='done').length],
  ['kb articles',(DATA.kb||[]).length],
  ['events',DATA.events.length],
].map(([k,v])=>`<div class="count"><b>${v}</b><span>${k}</span></div>`).join('');

const nav = document.getElementById('nav');
tabs.forEach((t,i)=>{const b=document.createElement('button');b.textContent=t;
  b.onclick=()=>{document.querySelectorAll('nav button').forEach(x=>x.classList.remove('active'));
    document.querySelectorAll('.panel').forEach(x=>x.classList.remove('active'));
    b.classList.add('active');document.getElementById('p-'+t).classList.add('active');};
  nav.appendChild(b); if(i===0)b.click();});

function sortable(table){
  table.querySelectorAll('th').forEach((th,idx)=>{th.onclick=()=>{
    const tb=table.tBodies[0], rows=[...tb.rows];
    const asc=!(th.dataset.asc==='1'); th.dataset.asc=asc?'1':'0';
    rows.sort((a,b)=>a.cells[idx].innerText.localeCompare(b.cells[idx].innerText,undefined,{numeric:true})*(asc?1:-1));
    rows.forEach(r=>tb.appendChild(r));
  };});
}

// ---- Overview ----
(function(){
  const p=document.getElementById('p-Overview');
  let h='<h3>Symbols by subsystem</h3><div class="wrap"><table><thead><tr><th>subsystem</th><th>total</th><th>high</th><th>med</th><th>low</th><th>documented</th><th>mix</th></tr></thead><tbody>';
  DATA.bySub.forEach(r=>{const t=r.total||1;
    h+=`<tr><td>${esc(r.subsystem)}</td><td>${r.total}</td><td>${r.high||0}</td><td>${r.medium||0}</td><td>${r.low||0}</td><td>${r.documented||0}</td>`+
    `<td><span class="bar"><i class="h" style="width:${120*(r.high||0)/t}px"></i><i class="m" style="width:${120*(r.medium||0)/t}px"></i><i class="l" style="width:${120*(r.low||0)/t}px"></i></span></td></tr>`;});
  h+='</tbody></table></div><h3>Recent activity</h3><div class="wrap"><table><thead><tr><th>when</th><th>kind</th><th>summary</th></tr></thead><tbody>';
  DATA.events.slice(0,25).forEach(e=>h+=`<tr><td class="ev mono">${esc(e.ts)}</td><td><span class="chip">${esc(e.kind)}</span></td><td>${esc(e.summary)}</td></tr>`);
  h+='</tbody></table></div>';
  if(!DATA.symbols.length) h='<p class="ev">No symbols recorded yet. Seeded task queue is on the Tasks tab.</p>'+h;
  p.innerHTML=h; p.querySelectorAll('table').forEach(sortable);
})();

// ---- Symbols ----
(function(){
  const p=document.getElementById('p-Symbols');
  const subs=[...new Set(DATA.symbols.map(s=>s.subsystem).filter(Boolean))].sort();
  p.innerHTML=`<div class="controls">
    <input id="sq" placeholder="filter name / summary / file...">
    <select id="ss"><option value="">all subsystems</option>${subs.map(s=>`<option>${esc(s)}</option>`).join('')}</select>
    <select id="sc"><option value="">any confidence</option><option>high</option><option>medium</option><option>low</option></select>
    </div><div class="wrap"><table id="st"><thead><tr>
    <th>address</th><th>name</th><th>kind</th><th>class</th><th>subsystem</th><th>source file</th><th>conf</th><th>status</th><th>summary</th>
    </tr></thead><tbody></tbody></table></div>`;
  const tb=p.querySelector('tbody');
  function render(){
    const q=p.querySelector('#sq').value.toLowerCase(), fs=p.querySelector('#ss').value, fc=p.querySelector('#sc').value;
    tb.innerHTML=DATA.symbols.filter(s=>{
      if(fs&&s.subsystem!==fs)return false;
      if(fc&&s.confidence!==fc)return false;
      if(q){const hay=(s.name+' '+(s.summary||'')+' '+(s.source_file||'')+' '+(s.orig_name||'')).toLowerCase();if(!hay.includes(q))return false;}
      return true;
    }).map(s=>`<tr><td class="mono">${esc(s.address||'')}</td><td class="mono">${esc(s.name)}</td><td>${esc(s.kind)}</td>`+
      `<td>${esc(s.class_name||'')}</td><td>${esc(s.subsystem||'')}</td><td class="mono">${esc(s.source_file||'')}</td>`+
      `<td>${conf(s.confidence)}</td><td>${esc(s.status)}</td><td>${esc(s.summary||'')}</td></tr>`).join('')
      || '<tr><td colspan="9" class="ev">no rows match</td></tr>';
  }
  p.querySelectorAll('input,select').forEach(el=>el.oninput=render);
  render(); sortable(p.querySelector('#st'));
})();

// ---- Hypotheses ----
(function(){
  const p=document.getElementById('p-Hypotheses');
  const evByH={}; DATA.evidence.forEach(e=>{(evByH[e.hypothesis_id]=evByH[e.hypothesis_id]||[]).push(e);});
  let h='<div class="wrap"><table><thead><tr><th>#</th><th>statement</th><th>about</th><th>conf</th><th>status</th><th>evidence</th></tr></thead><tbody>';
  DATA.hypotheses.forEach(x=>{
    const ev=(evByH[x.id]||[]).map(e=>`<details><summary><span class="chip">${esc(e.kind)}</span> ${esc(e.ref||'')}</summary><div class="ev">${esc(e.detail)}</div></details>`).join('')||'<span class="ev">none linked</span>';
    h+=`<tr><td class="mono">${x.id}${x.superseded_by?' <span class="chip">superseded</span>':''}</td>`+
      `<td>${esc(x.statement)}</td><td class="mono">${esc(x.symbol_name||x.subsystem||'')}</td>`+
      `<td>${conf(x.confidence)}</td><td>${esc(x.status)}</td><td>${ev}</td></tr>`;
  });
  h+='</tbody></table></div>'; if(!DATA.hypotheses.length)h='<p class="ev">No hypotheses recorded yet.</p>';
  p.innerHTML=h; p.querySelectorAll('table').forEach(sortable);
})();

// ---- Knowledgebase ----
(function(){
  const p=document.getElementById('p-Knowledgebase');
  const arts=(DATA.kb||[]).slice();
  if(!arts.length){p.innerHTML='<p class="ev">No knowledgebase articles yet.</p>';return;}
  // tiny markdown: escape first, then ## headings / - bullets / GFM tables / blank-line paras / **bold** / `code`
  function md(src){
    const lines=String(src).replace(/\r/g,'').split('\n');
    let out='',para=[],list=[];
    const inl=s=>esc(s).replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>').replace(/`([^`]+)`/g,'<code>$1</code>');
    const flushP=()=>{if(para.length){out+='<p>'+inl(para.join(' '))+'</p>';para=[];}};
    const flushL=()=>{if(list.length){out+='<ul>'+list.map(li=>'<li>'+inl(li)+'</li>').join('')+'</ul>';list=[];}};
    const cells=r=>r.replace(/^\s*\|/,'').replace(/\|\s*$/,'').split('|').map(c=>c.trim());
    const isSep=l=>/\|/.test(l)&&/-/.test(l)&&/^[\s|:-]+$/.test(l.trim());
    for(let i=0;i<lines.length;i++){
      const line=lines[i].replace(/\s+$/,'');
      if(/\|/.test(line)&&i+1<lines.length&&isSep(lines[i+1])){
        flushP();flushL();
        const head=cells(line);let body='';i+=2;
        while(i<lines.length&&/\S/.test(lines[i])&&/\|/.test(lines[i])){
          const c=cells(lines[i]);
          body+='<tr>'+head.map((_,k)=>'<td>'+inl(c[k]||'')+'</td>').join('')+'</tr>';i++;
        }
        i--;
        out+='<table class="mdt"><thead><tr>'+head.map(h=>'<th>'+inl(h)+'</th>').join('')+'</tr></thead><tbody>'+body+'</tbody></table>';
        continue;
      }
      if(/^##\s+/.test(line)){flushP();flushL();out+='<h4>'+inl(line.replace(/^##\s+/,''))+'</h4>';}
      else if(/^[-*]\s+/.test(line)){flushP();list.push(line.replace(/^[-*]\s+/,''));}
      else if(line===''){flushP();flushL();}
      else{flushL();para.push(line);}
    }
    flushP();flushL();return out;
  }
  const subs=[...new Set(arts.map(a=>a.subsystem).filter(Boolean))].sort();
  p.innerHTML=`<div class="controls">
    <input id="kq" placeholder="search titles + article text..." style="min-width:280px">
    <select id="ks"><option value="">all areas</option>${subs.map(s=>`<option>${esc(s)}</option>`).join('')}</select>
    <span class="ev" id="kn"></span></div><div id="klist"></div>`;
  const list=p.querySelector('#klist');
  function render(){
    const q=p.querySelector('#kq').value.toLowerCase(), fs=p.querySelector('#ks').value;
    const shown=arts.filter(a=>{
      if(fs&&a.subsystem!==fs)return false;
      if(q){const hay=(a.title+' '+a.subsystem+' '+a.body).toLowerCase();if(!hay.includes(q))return false;}
      return true;
    });
    p.querySelector('#kn').textContent=`${shown.length} / ${arts.length} articles`;
    let cur='';let h='';
    shown.forEach(a=>{
      if(a.subsystem!==cur){cur=a.subsystem;h+=`<h3 style="margin:18px 0 4px">${esc(cur||'(area)')}</h3>`;}
      const open=(q||fs)?' open':'';
      h+=`<details class="kb-card"${open}><summary>${esc(a.title)}`+
        `<span class="kb-sub">${esc(a.slug)}${a.status!=='stable'?' &middot; '+esc(a.status):''}</span></summary>`+
        `<div class="kb-body">${md(a.body)}</div></details>`;
    });
    list.innerHTML=h||'<p class="ev">no articles match</p>';
  }
  p.querySelectorAll('input,select').forEach(el=>el.oninput=render);
  render();
})();

// ---- Tasks ----
(function(){
  const p=document.getElementById('p-Tasks');
  let h='<div class="wrap"><table><thead><tr><th>status</th><th>pri</th><th>title</th><th>subsystem</th><th>notes</th></tr></thead><tbody>';
  DATA.tasks.forEach(t=>h+=`<tr><td><span class="chip">${esc(t.status)}</span></td><td>${t.priority}</td>`+
    `<td>${esc(t.title)}</td><td>${esc(t.subsystem||'')}</td><td class="ev">${esc(t.notes||'')}</td></tr>`);
  h+='</tbody></table></div>';
  p.innerHTML=h; p.querySelectorAll('table').forEach(sortable);
})();

// ---- Events ----
(function(){
  const p=document.getElementById('p-Events');
  let h='<div class="wrap"><table><thead><tr><th>when</th><th>kind</th><th>summary</th><th>detail</th></tr></thead><tbody>';
  DATA.events.forEach(e=>h+=`<tr><td class="ev mono">${esc(e.ts)}</td><td><span class="chip">${esc(e.kind)}</span></td><td>${esc(e.summary)}</td><td class="ev">${esc(e.detail||'')}</td></tr>`);
  h+='</tbody></table></div>';
  p.innerHTML=h; p.querySelectorAll('table').forEach(sortable);
})();
</script>
</body>
</html>
'@

function Build-Report {
    $dataBlock = @"
{
  "symbols": $(Get-Json "SELECT address,name,orig_name,kind,class_name,subsystem,source_file,signature,confidence,status,summary,updated_at FROM symbols ORDER BY COALESCE(address,'zzz'), name;"),
  "hypotheses": $(Get-Json "SELECT h.id,h.statement,h.confidence,h.status,h.subsystem,h.superseded_by,s.name AS symbol_name,s.address AS symbol_address,h.created_at FROM hypotheses h LEFT JOIN symbols s ON s.id=h.symbol_id ORDER BY h.id DESC;"),
  "evidence": $(Get-Json "SELECT id,hypothesis_id,symbol_id,kind,ref,detail,created_at FROM evidence ORDER BY id;"),
  "tasks": $(Get-Json "SELECT id,title,subsystem,status,priority,notes,updated_at FROM research_tasks ORDER BY CASE status WHEN 'doing' THEN 0 WHEN 'todo' THEN 1 WHEN 'blocked' THEN 2 ELSE 3 END, priority, id;"),
  "events": $(Get-Json "SELECT ts,kind,summary,detail FROM events ORDER BY id DESC LIMIT 400;"),
  "kb": $(Get-Json "SELECT slug,title,subsystem,body,status,updated_at FROM kb_articles ORDER BY (subsystem='overview') DESC, subsystem, title;"),
  "bySub": $(Get-Json "SELECT COALESCE(subsystem,'(unset)') AS subsystem, count(*) AS total, sum(confidence='high') AS high, sum(confidence='medium') AS medium, sum(confidence='low') AS low, sum(status IN ('documented','verified')) AS documented FROM symbols GROUP BY 1 ORDER BY total DESC;")
}
"@
    $html = $Template.Replace('__DATA__', $dataBlock).Replace('__GENERATED__', (Get-Date).ToString('yyyy-MM-dd HH:mm:ss'))
    New-Item -ItemType Directory -Force -Path (Split-Path $ResearchHtmlPath) | Out-Null
    [System.IO.File]::WriteAllText($ResearchHtmlPath, $html, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "wrote $ResearchHtmlPath ($((Get-Item $ResearchHtmlPath).Length) bytes)"
}

Build-Report

if ($Watch -gt 0) {
    Write-Host "watching: regenerating every $Watch s (Ctrl+C to stop)"
    while ($true) { Start-Sleep -Seconds $Watch; try { Build-Report } catch { Write-Warning $_ } }
}
elseif ($Serve) {
    $listener = New-Object System.Net.HttpListener
    $listener.Prefixes.Add("http://localhost:$Port/")
    $listener.Start()
    Write-Host "serving at http://localhost:$Port/  (regenerates on each request; Ctrl+C to stop)"
    try {
        while ($listener.IsListening) {
            $ctx = $listener.GetContext()
            try { Build-Report } catch { Write-Warning $_ }
            $bytes = [System.IO.File]::ReadAllBytes($ResearchHtmlPath)
            $ctx.Response.ContentType = 'text/html; charset=utf-8'
            $ctx.Response.OutputStream.Write($bytes, 0, $bytes.Length)
            $ctx.Response.Close()
        }
    } finally { $listener.Stop() }
}
