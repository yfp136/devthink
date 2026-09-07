// 内嵌 Web 前端（单页应用，零依赖纯 HTML/JS/CSS）
// 由 WebGateway 在 GET / 时直接返回此 HTML。
// 功能：登录页 → 管控仪表盘（引擎状态/GO触发/场景切换/播放控制/PGM预览/事件日志）
// 适配手机/平板/电脑浏览器，自适应布局。

namespace sm {
namespace web {

const char* kWebUIHtml = R"HTML(<!DOCTYPE html>
<html lang="zh-CN"><head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1,user-scalable=no">
<title>ShowMaster 远程管控</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;background:#0a0a0f;color:#e0e0e8;min-height:100vh}
.login{display:flex;align-items:center;justify-content:center;min-height:100vh;background:linear-gradient(135deg,#0f0f1a,#1a1a2e)}
.login-box{background:rgba(255,255,255,0.05);border:1px solid rgba(255,255,255,0.1);padding:40px;border-radius:12px;width:90%;max-width:360px}
.login-box h1{font-size:22px;margin-bottom:6px;text-align:center;color:#6c8aff}
.login-box p{text-align:center;color:#888;margin-bottom:24px;font-size:13px}
.login-box input{width:100%;padding:12px 14px;background:rgba(0,0,0,0.3);border:1px solid rgba(255,255,255,0.15);border-radius:8px;color:#fff;font-size:14px;margin-bottom:12px}
.login-box input:focus{border-color:#6c8aff;outline:none}
.login-box button{width:100%;padding:12px;background:linear-gradient(135deg,#6c8aff,#8a5cff);border:none;border-radius:8px;color:#fff;font-size:15px;cursor:pointer;transition:.2s}
.login-box button:hover{opacity:.9;transform:translateY(-1px)}
.login-box .err{color:#ff6b6b;font-size:12px;text-align:center;margin-top:8px;display:none}
.dashboard{display:none}
.topbar{height:52px;display:flex;align-items:center;justify-content:space-between;padding:0 16px;background:rgba(255,255,255,0.03);border-bottom:1px solid rgba(255,255,255,0.08)}
.topbar .title{font-size:16px;font-weight:600;color:#6c8aff}
.topbar .user{font-size:12px;color:#888}
.topbar .btn-logout{padding:6px 14px;background:rgba(255,107,107,0.15);border:1px solid rgba(255,107,107,0.3);border-radius:6px;color:#ff6b6b;font-size:12px;cursor:pointer;margin-left:10px}
.main{padding:12px;display:grid;grid-template-columns:1fr;gap:12px;max-width:1400px;margin:0 auto}
@media(min-width:768px){.main{grid-template-columns:1fr 1fr 1fr}}
.card{background:rgba(255,255,255,0.03);border:1px solid rgba(255,255,255,0.08);border-radius:10px;padding:14px}
.card h2{font-size:13px;color:#888;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}
.btn{padding:10px 16px;border:none;border-radius:8px;font-size:14px;cursor:pointer;transition:.2s;width:100%}
.btn-go{background:linear-gradient(135deg,#00c896,#00b4d8);color:#fff;font-weight:bold;font-size:16px;padding:14px}
.btn-go:hover{transform:translateY(-2px);box-shadow:0 4px 12px rgba(0,200,150,0.3)}
.btn-play{background:#6c8aff;color:#fff}
.btn-stop{background:#ff6b6b;color:#fff}
.btn:disabled{opacity:.4;cursor:not-allowed}
.engine-row{display:flex;align-items:center;justify-content:space-between;padding:6px 0;border-bottom:1px solid rgba(255,255,255,0.05)}
.engine-row:last-child{border:none}
.dot{width:8px;height:8px;border-radius:50%;display:inline-block;margin-right:8px}
.dot.on{background:#00c896;box-shadow:0 0 6px #00c896}
.dot.off{background:#ff6b6b}
.engine-name{font-size:13px}
.preview{width:100%;background:#000;border-radius:8px;aspect-ratio:16/9;object-fit:contain}
.event-log{max-height:200px;overflow-y:auto;font-size:11px;font-family:monospace;line-height:1.6}
.event-log .entry{padding:2px 0;border-bottom:1px solid rgba(255,255,255,0.03)}
.queue-stats{display:flex;gap:12px;margin-top:8px}
.queue-stats div{flex:1;text-align:center;padding:6px;background:rgba(0,0,0,0.2);border-radius:6px}
.queue-stats .num{font-size:18px;font-weight:bold;color:#6c8aff}
.queue-stats .lbl{font-size:10px;color:#666}
.scene-grid{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:8px}
.scene-btn{padding:8px;background:rgba(108,138,255,0.1);border:1px solid rgba(108,138,255,0.3);border-radius:6px;color:#e0e0e8;cursor:pointer;font-size:12px;text-align:center}
.scene-btn:hover{background:rgba(108,138,255,0.2)}
</style>
</head><body>
<div id="loginPage" class="login">
<div class="login-box">
<h1>ShowMaster</h1>
<p>全域智能舞美管控平台 · 远程管控</p>
<input id="username" type="text" placeholder="用户名" value="admin">
<input id="password" type="password" placeholder="密码" value="admin123">
<button id="loginBtn" onclick="doLogin()">登录</button>
<div id="loginErr" class="err">用户名或密码错误</div>
</div>
</div>

<div id="dashboard" class="dashboard">
<div class="topbar">
<span class="title">ShowMaster 远程管控</span>
<span style="display:flex;align-items:center">
<span class="user" id="userInfo"></span>
<button class="btn-logout" onclick="doLogout()">登出</button>
</span>
</div>
<div class="main">
<div class="card"><h2>引擎状态</h2><div id="engineList"></div>
<div class="queue-stats"><div><div class="num" id="qPending">0</div><div class="lbl">待处理</div></div>
<div><div class="num" id="qPushed">0</div><div class="lbl">总入队</div></div>
<div><div class="num" id="qDropped">0</div><div class="lbl">丢弃</div></div></div>
</div>
<div class="card"><h2>演出控制</h2>
<button class="btn btn-go" onclick="sendGo()">GO ▶ 下一节目</button>
<div style="display:flex;gap:8px;margin-top:8px">
<button class="btn btn-play" onclick="sendPlay()" style="flex:1">播放</button>
<button class="btn btn-stop" onclick="sendStop()" style="flex:1">停止</button>
</div>
<div class="queue-stats" style="margin-top:8px"><div><div class="num" id="sessionCount">0</div><div class="lbl">在线会话</div></div></div>
</div>
<div class="card"><h2>PGM 预览</h2><img id="pgmPreview" class="preview" src="" alt="等待画面..."></div>
<div class="card"><h2>事件日志</h2><div class="event-log" id="eventLog"></div></div>
</div>
</div>

<script>
let token='';
let role='';
let wsEvents=null,wsPreview=null;
async function doLogin(){
const u=document.getElementById('username').value,p=document.getElementById('password').value;
try{const r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({username:u,password:p})});
if(!r.ok){showLoginErr();return}
const d=await r.json();token=d.token;role=d.role;
localStorage.setItem('sm_token',token);localStorage.setItem('sm_role',role);
showDashboard(d.username,d.role)}catch(e){showLoginErr()}}
function showLoginErr(){document.getElementById('loginErr').style.display='block'}
function doLogout(){fetch('/api/logout',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({token})});token='';localStorage.removeItem('sm_token');location.reload()}
function showDashboard(user,role){
document.getElementById('loginPage').style.display='none';
document.getElementById('dashboard').style.display='block';
document.getElementById('userInfo').textContent=user+' ('+role+')';
connectWS();refreshStatus()}
async function sendCmd(op,params={}){
await fetch('/api/cmd',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+token},body:JSON.stringify({op,params})})}
function sendGo(){fetch('/api/go',{method:'POST',headers:{'Authorization':'Bearer '+token}})}
function sendPlay(){fetch('/api/transport/play',{method:'POST',headers:{'Content-Type':'application/json','Authorization':'Bearer '+token},body:JSON.stringify({media_id:''})})}
function sendStop(){fetch('/api/transport/stop',{method:'POST',headers:{'Authorization':'Bearer '+token}})}
function connectWS(){
const proto=location.protocol==='https:'?'wss:':'ws:';
wsEvents=new WebSocket(proto+'//'+location.host+'/ws/events');
wsEvents.onmessage=e=>{const d=JSON.parse(e.data);logEvent(d.op||'evt',JSON.stringify(d.params||{}))};
wsPreview=new WebSocket(proto+'//'+location.host+'/ws/preview');
wsPreview.onmessage=e=>{const d=JSON.parse(e.data);if(d.type==='pgm_frame')document.getElementById('pgmPreview').src=d.data}}
function logEvent(op,detail){const el=document.getElementById('eventLog');const t=new Date().toLocaleTimeString();const entry=document.createElement('div');entry.className='entry';entry.textContent='['+t+'] '+op+' '+detail;el.insertBefore(entry,el.firstChild);if(el.children.length>100)el.removeChild(el.lastChild)}
async function refreshStatus(){
if(!token)return;
try{const r=await fetch('/api/status?token='+token);if(!r.ok){if(r.status===401){doLogout();return}return}
const d=await r.json();
let el=document.getElementById('engineList');el.innerHTML='';
(d.engines||[]).forEach(e=>{const row=document.createElement('div');row.className='engine-row';const dot=e.online?'on':'off';row.innerHTML='<span><span class="dot '+dot+'"></span><span class="engine-name">'+e.id+'</span></span><span style="font-size:11px;color:#666">'+e.phase+'</span>';el.appendChild(row)});
document.getElementById('qPending').textContent=d.queue?d.queue.pending:0;
document.getElementById('qPushed').textContent=d.queue?d.queue.total_pushed:0;
document.getElementById('qDropped').textContent=d.queue?d.queue.total_dropped:0;
document.getElementById('sessionCount').textContent=d.sessions||0;
}catch(e){}setTimeout(refreshStatus,3000)}
// 自动登录
const saved=localStorage.getItem('sm_token');if(saved){token=saved;role=localStorage.getItem('sm_role')||'';fetch('/api/status?token='+token).then(r=>{if(r.ok){showDashboard(role,role)}else{localStorage.removeItem('sm_token')}}).catch(()=>{})}
</script>
</body></html>)HTML";

}  // namespace web
}  // namespace sm
