// WebConfig.cpp -- Embedded web server and REST API implementation
//
// This file contains:
//   1. The complete HTML/CSS/JS single-page app as a PROGMEM string literal
//   2. REST API endpoint handlers for configuration CRUD
//   3. Captive portal DNS setup for AP mode
//   4. WiFi scan orchestration (async scan + polling pattern)
//   5. Clock face image serving from SPIFFS
//
// Memory strategy: The HTML SPA is stored in PROGMEM (flash) rather than
// SPIFFS or heap. This avoids filesystem overhead and ensures the UI is
// always available even if SPIFFS is corrupted. The trade-off is that UI
// changes require recompilation. server.send_P() streams directly from
// flash without copying to RAM.

#include "WebConfig.h"
#include "WifiManager.h"
#include "Timezones.h"
#include "TFTs.h"
#include "Clock.h"
#include "Backlights.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <TimeLib.h>
#include <Update.h>
#include <mbedtls/sha256.h>
#include "SPIFFS.h"

// External globals -- these are owned by the main .ino file and shared
// across modules. The web UI needs direct access to apply settings in
// real time (e.g., changing the clock face redraws the displays immediately).
extern TFTs tfts;
extern Clock uclock;
extern Backlights backlights;
extern StoredConfig stored_config;

// -----------------------------------------------------------------------
// Embedded SPA (Single Page Application)
// The entire web UI is a single HTML page with inline CSS and JS.
// PROGMEM places it in flash (not RAM) -- critical on ESP32 where heap
// is limited to ~320 KB but flash is 4 MB.
// The R"rawliteral(...)rawliteral" syntax is a C++ raw string literal
// that avoids escaping quotes and special characters in the HTML.
// -----------------------------------------------------------------------
static const char html_page[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>EleksTube IPS Clock</title>
<style>
:root{
 --bg:#f4f5f7;--card:#fff;--card-border:#e2e5e9;--text:#1c2024;--muted:#6b7785;
 --accent:#0d6efd;--accent-hover:#0b5ed7;--input-bg:#f0f2f5;--input-border:#d1d5db;
 --ok-bg:#d1fae5;--ok-text:#065f46;--err-bg:#fee2e2;--err-text:#991b1b;
 --sel-border:#0d6efd;--unsel-border:#d1d5db;--status-val:#0d6efd;
 --danger:#dc3545;--danger-hover:#b02a37
}
[data-theme=dark]{
 --bg:#0f172a;--card:#1e293b;--card-border:#334155;--text:#e2e8f0;--muted:#94a3b8;
 --accent:#38bdf8;--accent-hover:#0ea5e9;--input-bg:#0f172a;--input-border:#475569;
 --ok-bg:#064e3b;--ok-text:#6ee7b7;--err-bg:#7f1d1d;--err-text:#fca5a5;
 --sel-border:#38bdf8;--unsel-border:#475569;--status-val:#38bdf8;
 --danger:#f87171;--danger-hover:#ef4444
}
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:-apple-system,system-ui,'Segoe UI',sans-serif;background:var(--bg);color:var(--text);min-height:100vh;transition:background .3s,color .3s}
.container{max-width:480px;margin:0 auto;padding:60px 16px 16px}
.grid{display:flex;flex-direction:column}
.grid>div{display:contents}
#card-wifi{order:-1}
@media(min-width:768px){
.container{max-width:960px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:8px}
.grid>div{display:block}
#card-wifi{order:0}
.grid .full{grid-column:1/-1}
}
.hdr{position:fixed;top:0;left:0;right:0;z-index:100;display:flex;justify-content:space-between;align-items:center;padding:8px 16px;background:var(--bg);border-bottom:1px solid var(--card-border);transition:background .3s}
.hdr-inner{display:flex;align-items:center;gap:8px;max-width:960px;margin:0 auto;width:100%}
h1{font-size:1.1em;font-weight:700;flex:1;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.btn-save{background:var(--accent);color:#fff;padding:6px 14px;font-size:.82em;border-radius:6px;border:none;cursor:pointer;font-weight:600;white-space:nowrap}
.btn-save:hover{background:var(--accent-hover)}
h2{font-size:.9em;font-weight:600;color:var(--accent);margin:14px 0 6px;text-transform:uppercase;letter-spacing:.5px}
.card{background:var(--card);border:1px solid var(--card-border);border-radius:10px;padding:14px;margin:8px 0}
label{display:block;font-size:.82em;color:var(--muted);margin:6px 0 2px;font-weight:500}
input[type=text],input[type=password],select{width:100%;padding:8px 10px;background:var(--input-bg);border:1px solid var(--input-border);border-radius:6px;color:var(--text);font-size:.9em;transition:border-color .2s}
input:focus,select:focus{outline:none;border-color:var(--accent)}
.row{display:flex;gap:8px;align-items:end}
.row>*{flex:1}
button{padding:8px 14px;border:none;border-radius:6px;cursor:pointer;font-size:.85em;font-weight:600;transition:background .2s}
.btn-primary{background:var(--accent);color:#fff;width:100%;padding:10px;margin-top:12px}
.btn-primary:hover{background:var(--accent-hover)}
.btn-sm{background:var(--input-bg);color:var(--text);border:1px solid var(--input-border);padding:6px 10px;flex:none}
.btn-sm:hover{border-color:var(--accent)}
.btn-danger{background:var(--danger);color:#fff;padding:8px 14px}
.btn-danger:hover{background:var(--danger-hover)}
.btn-icon{background:none;border:none;font-size:1.2em;padding:4px 8px;cursor:pointer;color:var(--muted)}
.sw{position:relative;display:inline-block;width:40px;height:22px;flex-shrink:0}
.sw input{opacity:0;width:0;height:0}
.sw .slider{position:absolute;inset:0;background:var(--input-border);border-radius:22px;transition:.3s;cursor:pointer}
.sw .slider:before{content:"";position:absolute;width:16px;height:16px;left:3px;bottom:3px;background:#fff;border-radius:50%;transition:.3s}
.sw input:checked+.slider{background:var(--accent)}
.sw input:checked+.slider:before{transform:translateX(18px)}
.opt-row{display:flex;align-items:center;justify-content:space-between;padding:4px 0}
.opt-row span{font-size:.9em}
.radio-group{display:flex;gap:4px;background:var(--input-bg);border-radius:6px;padding:2px;border:1px solid var(--input-border)}
.radio-group label{margin:0;padding:6px 14px;border-radius:4px;cursor:pointer;font-size:.85em;color:var(--text);text-align:center;flex:1;transition:background .2s}
.radio-group input{display:none}
.radio-group input:checked+span{background:var(--accent);color:#fff;display:block;margin:-6px -14px;padding:6px 14px;border-radius:4px}
.faces{display:flex;gap:8px;margin:8px 0;justify-content:center;max-width:100%;padding:3px}
.faces img{border:3px solid var(--unsel-border);border-radius:6px;cursor:pointer;background:#000;transition:border-color .2s,transform .15s}
.faces img:hover{transform:scale(1.03)}
.faces img.sel{border-color:var(--sel-border)}
.status{font-size:.82em;color:var(--muted)}
.status .val{color:var(--status-val);font-weight:500}
.status-row{display:flex;justify-content:space-between;align-items:center;padding:4px 0}
.ntp-row{display:flex;align-items:center;gap:8px}
.sig{font-size:.75em;padding:2px 6px;border-radius:3px;font-weight:600}
.sig-good{background:#d1fae5;color:#065f46}
.sig-fair{background:#fef3c7;color:#92400e}
.sig-poor{background:#fee2e2;color:#991b1b}
[data-theme=dark] .sig-good{background:#064e3b;color:#6ee7b7}
[data-theme=dark] .sig-fair{background:#78350f;color:#fcd34d}
[data-theme=dark] .sig-poor{background:#7f1d1d;color:#fca5a5}
.cpick-overlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.5);z-index:300;justify-content:center;align-items:center}
.cpick-overlay.open{display:flex}
.cpick-modal{background:var(--card);border:1px solid var(--card-border);border-radius:12px;padding:16px;max-width:340px;width:90%;box-shadow:0 8px 24px rgba(0,0,0,.2)}
.cpick-modal h3{font-size:.95em;margin:0 0 10px;color:var(--text)}
.cpick-grid{display:grid;gap:2px}
.cpick-grid div{width:100%;aspect-ratio:1;border-radius:3px;cursor:pointer;border:2px solid transparent;transition:border-color .15s,transform .1s}
.cpick-grid div:hover{transform:scale(1.2);z-index:1;border-color:var(--text)}
.cpick-preview{display:flex;align-items:center;gap:10px;margin-top:10px;padding-top:10px;border-top:1px solid var(--card-border)}
.cpick-swatch{width:36px;height:36px;border-radius:6px;border:1px solid var(--card-border)}
.cpick-hex{font-family:monospace;font-size:.9em;color:var(--muted)}
.ftr{border-top:1px solid var(--card-border);padding:12px 16px;margin-top:16px;text-align:center;font-size:.75em;color:var(--muted)}
.ftr-inner{max-width:960px;margin:0 auto}
.ftr a{color:var(--accent);text-decoration:none}
.ftr a:hover{text-decoration:underline}
.ftr-links{display:flex;gap:16px;justify-content:center;margin-top:4px}
.toast-stack{position:fixed;top:52px;left:50%;transform:translateX(-50%);z-index:200;display:flex;flex-direction:column;gap:6px;width:90%;max-width:440px;pointer-events:none}
.toast{padding:10px 16px;border-radius:8px;font-size:.9em;font-weight:500;text-align:center;pointer-events:auto;animation:toast-in .3s ease-out,toast-out .3s ease-in forwards;animation-delay:0s,3s;box-shadow:0 4px 12px rgba(0,0,0,.15)}
.toast.ok{background:var(--ok-bg);color:var(--ok-text);border:1px solid var(--ok-text)}
.unsaved-bar{display:none;position:fixed;bottom:0;left:0;right:0;z-index:200;background:var(--accent);color:#fff;text-align:center;padding:8px;font-size:.85em;font-weight:600}
.toast.err{background:var(--err-bg);color:var(--err-text);border:1px solid var(--err-text)}
@keyframes toast-in{from{opacity:0;transform:translateY(-20px)}to{opacity:1;transform:translateY(0)}}
@keyframes toast-out{from{opacity:1}to{opacity:0}}
</style>
</head>
<body>
<div class="container">
<div class="hdr">
<div class="hdr-inner">
<h1 id="devname">EleksTube IPS Clock</h1>
<button class="btn-save" onclick="saveConfig()">Save</button>
<button class="btn-icon" onclick="toggleTheme()" title="Toggle theme" id="themeBtn">&#9790;</button>
<button class="btn-icon" onclick="doLogout()" title="Logout" id="logoutBtn" style="display:none"><svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="2.5"><path d="M9 21H5a2 2 0 01-2-2V5a2 2 0 012-2h4"/><polyline points="16 17 21 12 16 7"/><line x1="21" y1="12" x2="9" y2="12"/></svg></button>
</div>
</div>
<div id="toast-stack" class="toast-stack"></div>
<div id="login-overlay" style="display:none;position:fixed;inset:0;background:var(--bg);z-index:500;justify-content:center;align-items:center">
<div style="max-width:320px;width:90%;text-align:center">
<h1 style="margin-bottom:16px" id="login_title">EleksTube IPS Clock</h1>
<p style="color:var(--muted);margin-bottom:16px;font-size:.9em">Enter the dashboard password to continue</p>
<input type="password" id="login_pass" placeholder="Password" style="margin-bottom:8px" onkeydown="if(event.key==='Enter')doLogin()">
<button class="btn-save" onclick="doLogin()" style="width:100%;padding:10px;border-radius:6px;font-size:.9em">Login</button>
<p id="login_err" style="color:var(--danger);font-size:.85em;margin-top:8px;display:none"></p>
</div>
</div>

<div class="grid">
<div>
<div class="card">
<h2>Timezone</h2>
<div id="tz_detect" class="status" style="margin-bottom:6px;display:none;align-items:center;gap:6px"><span>Detected: </span><span class="val" id="tz_browser">--</span> <button class="btn-sm" onclick="useDetectedTZ()" style="padding:3px 8px;font-size:.8em">Use</button></div>
<select id="tz" onchange="previewDebounce({tz:this.value})"></select>
</div>

<div class="card">
<h2>Display</h2>
<div class="opt-row">
<span>Hour Format</span>
<div class="radio-group">
<label><input type="radio" name="hfmt" value="0" id="h24" onchange="previewHourFmt()"><span>24h</span></label>
<label><input type="radio" name="hfmt" value="1" id="h12" onchange="previewHourFmt()"><span>12h</span></label>
</div>
</div>
<div class="opt-row">
<span>Blank leading zero</span>
<label class="sw"><input type="checkbox" id="blankz" onchange="previewDebounce({blank_zero:this.checked})"><span class="slider"></span></label>
</div>
<label>Clock Face</label>
<div class="faces" id="face_preview"></div>
<label>Display hue shift <span id="hue_val" style="color:var(--accent)">0</span>&deg;</label>
<input type="range" id="hue_shift" min="0" max="359" value="0" style="width:100%;accent-color:var(--accent)" oninput="document.getElementById('hue_val').textContent=this.value;previewHue()">
<label>Display brightness <span id="day_tft_val" style="color:var(--accent)">100</span>%</label>
<input type="range" id="day_tft" min="1" max="100" value="100" style="width:100%;accent-color:var(--accent)" oninput="document.getElementById('day_tft_val').textContent=this.value;previewTFT()">
<label>Filament Glow / Rear LED brightness <span id="led_int_val" style="color:var(--accent)">7</span>/7</label>
<input type="range" id="led_int" min="0" max="7" value="7" style="width:100%;accent-color:var(--accent)" oninput="document.getElementById('led_int_val').textContent=this.value;previewLED()">
<label>Filament Glow / Rear LED pattern</label>
<select id="led_pattern" onchange="document.getElementById('cpick_wrap').style.display=(this.value!=='3'&&this.value!=='0'?'block':'none');previewLED()">
<option value="0">Off</option>
<option value="2">Constant</option>
<option value="3">Rainbow</option>
<option value="4">Pulse</option>
<option value="5">Breath</option>
</select>
<div id="cpick_wrap" style="display:none;margin-top:6px">
<div style="display:flex;align-items:center;gap:8px">
<label style="margin:0">Colour</label>
<div id="led_swatch_btn" onclick="openColourPicker()" style="width:32px;height:32px;border-radius:6px;border:2px solid var(--input-border);cursor:pointer;background:#ff6600"></div>
<span id="led_hex" style="font-family:monospace;font-size:.82em;color:var(--muted)">#FF6600</span>
</div>
<input type="hidden" id="led_color" value="#ff6600">
</div>

<div id="cpick_overlay" class="cpick-overlay" onclick="if(event.target===this)closeColourPicker()">
<div class="cpick-modal">
<h3>Choose Colour</h3>
<div id="cpick_grid" class="cpick-grid"></div>
<div class="cpick-preview">
<div id="cpick_preview_swatch" class="cpick-swatch" style="background:#ff6600"></div>
<span id="cpick_preview_hex" class="cpick-hex">#FF6600</span>
<div style="flex:1"></div>
<button class="btn-sm" onclick="closeColourPicker()">Done</button>
</div>
</div>
</div>
</div>

<div class="card">
<h2>Night Mode</h2>
<div class="opt-row" style="margin-bottom:8px">
<span>Enable night dimming</span>
<label class="sw"><input type="checkbox" id="night_enabled" onchange="markDirty()"><span class="slider"></span></label>
</div>
<div id="night_opts">
<p style="font-size:.8em;color:var(--muted);margin-bottom:6px">Displays and backlights dim or turn off between these hours</p>
<div class="row">
<div><label>Dim at (hour)</label><input type="text" id="night_time" style="width:70px" inputmode="numeric" oninput="markDirty()"></div>
<div><label>Bright at (hour)</label><input type="text" id="day_time" style="width:70px" inputmode="numeric" oninput="markDirty()"></div>
</div>
<label>Display brightness <span id="tft_val" style="color:var(--accent)">0</span>% <span style="font-size:.85em;color:var(--muted)">(0% = off)</span></label>
<input type="range" id="night_tft" min="0" max="100" style="width:100%;accent-color:var(--accent)" oninput="document.getElementById('tft_val').textContent=this.value;markDirty()">
<label>Filament Glow / Rear LED brightness <span id="led_val" style="color:var(--accent)">0</span>/7 <span style="font-size:.85em;color:var(--muted)">(0 = off)</span></label>
<input type="range" id="night_led" min="0" max="7" style="width:100%;accent-color:var(--accent)" oninput="document.getElementById('led_val').textContent=this.value;markDirty()">
</div>
</div>
</div>
<div>
<div class="card">
<h2>Status</h2>
<div class="status">
<div class="status-row"><span>Time</span><span class="val" id="st_time">--</span></div>
<div class="status-row"><span>WiFi</span><span class="val" id="st_wifi">--</span></div>
<div class="status-row"><span>IP</span><span class="val" id="st_ip">--</span></div>
<div class="status-row"><span>Signal</span><span id="st_rssi">--</span></div>
<div class="status-row">
<div class="ntp-row"><span>NTP</span><button class="btn-sm" onclick="ntpSync()" style="padding:2px 8px;font-size:.75em">Sync</button></div>
<span class="val" id="st_ntp">--</span>
</div>
<div class="status-row"><span>Uptime</span><span class="val" id="st_up">--</span></div>
<div class="status-row"><span>AP</span><span class="val" id="st_ap">--</span></div>
<div class="status-row"><span>Firmware</span><span class="val" id="st_ver">--</span></div>
</div>
</div>

<div class="card" id="card-wifi">
<h2>WiFi</h2>
<div class="row">
<div><label>SSID</label><input type="text" id="ssid" autocomplete="off" oninput="markDirty()"></div>
<button type="button" class="btn-sm" onclick="scanWifi()">Scan</button>
</div>
<div id="networks" style="display:none"><label>Networks</label><select id="netlist" onchange="document.getElementById('ssid').value=this.value"></select></div>
<label>Password</label>
<input type="password" id="pass" maxlength="31" oninput="markDirty()">
<div class="opt-row" style="margin-top:10px">
<span>Onboard AP</span>
<label class="sw"><input type="checkbox" id="ap_toggle" onchange="toggleAP()"><span class="slider"></span></label>
</div>
</div>

<div class="card">
<h2>Dashboard Password</h2>
<div id="pw_section">
<div id="pw_set">
<label id="pw_old_label" style="display:none">Current Password</label>
<input type="password" id="pw_old" style="display:none" placeholder="Enter current password">
<label>New Password</label>
<input type="password" id="pw_new" placeholder="Enter password (min 4 chars)">
<label>Confirm Password</label>
<input type="password" id="pw_confirm" placeholder="Confirm new password">
<p id="pw_match_err" style="display:none;color:var(--danger);font-size:.8em;margin-top:2px">Passwords do not match</p>
<div style="display:flex;gap:8px;margin-top:8px">
<button class="btn-sm" onclick="setPassword()" style="flex:1">Set Password</button>
<button class="btn-sm" onclick="removePassword()" id="pw_remove_btn" style="flex:1;display:none">Remove Password</button>
</div>
</div>
</div>
</div>

<div class="card">
<h2>Firmware Update</h2>
<p style="font-size:.82em;color:var(--muted);margin-bottom:8px">Upload a .bin file to update firmware or clock faces over WiFi. The file type is detected automatically.</p>
<input type="file" id="ota_file" accept=".bin" style="font-size:.85em;color:var(--text);margin-bottom:8px">
<button class="btn-sm" onclick="uploadFirmware()" style="width:100%">Upload &amp; Install</button>
<div id="ota_progress" style="display:none;margin-top:8px">
<div style="background:var(--input-bg);border-radius:4px;overflow:hidden;height:6px">
<div id="ota_bar" style="height:100%;background:var(--accent);width:0%;transition:width .3s"></div>
</div>
<p id="ota_status" style="font-size:.8em;color:var(--muted);margin-top:4px;text-align:center">Uploading...</p>
</div>
</div>

<div class="card">
<h2>Maintenance</h2>
<div style="display:flex;gap:8px;margin-bottom:8px">
<button class="btn-sm" onclick="rebootClock()" style="flex:1">Reboot</button>
<button class="btn-danger" onclick="document.getElementById('rst_init').style.display='none';document.getElementById('rst_confirm').style.display='block'" style="flex:1">Factory Reset</button>
</div>
<div id="rst_init"><p style="font-size:.75em;color:var(--muted)">Factory reset erases all settings and reboots into setup mode</p></div>
<div id="rst_confirm" style="display:none;text-align:center">
<p style="font-size:.9em;margin-bottom:8px;color:var(--danger)">This will erase ALL settings including WiFi. The clock will reboot into setup mode.</p>
<div style="display:flex;gap:8px;justify-content:center">
<button class="btn-danger" onclick="factoryReset()">Yes, Reset</button>
<button class="btn-sm" onclick="document.getElementById('rst_init').style.display='block';document.getElementById('rst_confirm').style.display='none'">Cancel</button>
</div>
</div>
</div>
</div>
</div>
</div>
<div class="ftr"><div class="ftr-inner">&copy; 2026 Andy Taylor (MW0MWZ)<div class="ftr-links"><a href="https://github.com/MW0MWZ/EleksTubeIPS" target="_blank">GitHub</a><a href="https://github.com/MW0MWZ/EleksTubeIPS/releases/latest" target="_blank">Latest Release</a></div></div></div>
<div id="unsaved-bar" class="unsaved-bar">You have unsaved changes</div>
<script>
const TZ_LIST=[];
let cfg={},detectedTZ=null,selectedGraphic=1;

function toggleTheme(){
 let d=document.documentElement;
 let t=d.getAttribute('data-theme')==='dark'?'light':'dark';
 d.setAttribute('data-theme',t);
 localStorage.setItem('theme',t);
 document.getElementById('themeBtn').innerHTML=t==='dark'?'&#9788;':'&#9790;';
}
(function(){
 let t=localStorage.getItem('theme')||(matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');
 document.documentElement.setAttribute('data-theme',t);
 document.getElementById('themeBtn').innerHTML=t==='dark'?'&#9788;':'&#9790;';
})();

document.getElementById('night_enabled').addEventListener('change',function(){
 document.getElementById('night_opts').style.opacity=this.checked?'1':'.4';
 document.getElementById('night_opts').style.pointerEvents=this.checked?'auto':'none';
});

function rssiLabel(v){
 if(!v||v===0)return'<span class="val">N/A</span>';
 let cls='sig-poor',lbl='Poor';
 if(v>-50){cls='sig-good';lbl='Excellent';}
 else if(v>-65){cls='sig-good';lbl='Good';}
 else if(v>-75){cls='sig-fair';lbl='Fair';}
 return'<span class="val">'+v+' dBm</span> <span class="sig '+cls+'">'+lbl+'</span>';
}

async function loadTZ(){
 let r=await fetch('/api/timezones');
 let d=await r.json();
 let sel=document.getElementById('tz');
 sel.innerHTML='';
 d.forEach(t=>{
  let o=document.createElement('option');
  o.value=t.tz;o.textContent=t.label;
  TZ_LIST.push(t);
  sel.appendChild(o);
 });
 try{
  let btz=Intl.DateTimeFormat().resolvedOptions().timeZone;
  let match=TZ_LIST.find(t=>t.iana===btz);
  if(match){
   detectedTZ=match;
   document.getElementById('tz_browser').textContent=match.label;
   document.getElementById('tz_detect').style.display='flex';
  }
 }catch(e){}
}

function useDetectedTZ(){
 if(detectedTZ){
  document.getElementById('tz').value=detectedTZ.tz;
  document.getElementById('tz_detect').style.display='none';
 }
}

async function loadConfig(){
 let r=await fetch('/api/config');
 if(!r.ok){showMsg('Failed to load config','err');return;}
 cfg=await r.json();
 document.getElementById('ssid').value=cfg.ssid||'';
 document.getElementById('pass').value='';
 let savedTZ=cfg.tz||'UTC0';
 document.getElementById('tz').value=savedTZ;
 if(savedTZ==='UTC0'&&detectedTZ) document.getElementById('tz').value=detectedTZ.tz;
 if(cfg.twelve_hour) document.getElementById('h12').checked=true;
 else document.getElementById('h24').checked=true;
 document.getElementById('blankz').checked=cfg.blank_zero||false;
 document.getElementById('ap_toggle').checked=cfg.ap_enabled!==false;
 let ne=document.getElementById('night_enabled');
 ne.checked=cfg.night_enabled!==false;
 ne.dispatchEvent(new Event('change'));
 document.getElementById('night_time').value=cfg.night_time!=null?cfg.night_time:1;
 document.getElementById('day_time').value=cfg.day_time!=null?cfg.day_time:7;
 let ntft=cfg.night_tft!=null?cfg.night_tft:0;
 let nled=cfg.night_led!=null?cfg.night_led:0;
 let ntftPct=Math.round(ntft*100/255);
 document.getElementById('night_tft').value=ntftPct;
 document.getElementById('tft_val').textContent=ntftPct;
 document.getElementById('night_led').value=nled;
 document.getElementById('led_val').textContent=nled;
 selectedGraphic=cfg.graphic||1;
 loadFacePreviews(cfg.num_faces,selectedGraphic);
 let dtft=cfg.day_tft!=null?cfg.day_tft:255;
 let dtftPct=Math.round(dtft*100/255);
 document.getElementById('day_tft').value=dtftPct;
 document.getElementById('day_tft_val').textContent=dtftPct;
 let hs=cfg.hue_shift||0;
 document.getElementById('hue_shift').value=hs;
 document.getElementById('hue_val').textContent=hs;
 if(hs>0) document.querySelectorAll('.faces img').forEach(img=>{img.style.filter='hue-rotate('+hs+'deg)';});
 let lint=cfg.led_intensity!=null?cfg.led_intensity:7;
 document.getElementById('led_int').value=lint;
 document.getElementById('led_int_val').textContent=lint;
 let lp=cfg.led_pattern!=null?cfg.led_pattern:3;
 document.getElementById('led_pattern').value=lp;
 if(lp!=0&&lp!=3){
  document.getElementById('cpick_wrap').style.display='block';
  if(cfg.led_color&&cfg.led_color!=='#000000'){
   document.getElementById('led_color').value=cfg.led_color;
   document.getElementById('led_swatch_btn').style.background=cfg.led_color;
   document.getElementById('led_hex').textContent=cfg.led_color.toUpperCase();
  }
 }
 // Form is now populated — enable dirty tracking
 loading=false;
}

async function saveConfig(){
 let body={
  ssid:document.getElementById('ssid').value,
  password:document.getElementById('pass').value,
  tz:document.getElementById('tz').value,
  twelve_hour:document.getElementById('h12').checked,
  blank_zero:document.getElementById('blankz').checked,
  graphic:selectedGraphic,
  night_enabled:document.getElementById('night_enabled').checked,
  night_time:parseInt(document.getElementById('night_time').value)||0,
  day_time:parseInt(document.getElementById('day_time').value)||7,
  night_tft:Math.round(parseInt(document.getElementById('night_tft').value)*255/100),
  night_led:parseInt(document.getElementById('night_led').value)||0,
  day_tft:Math.round(parseInt(document.getElementById('day_tft').value)*255/100)||255,
  led_intensity:parseInt(document.getElementById('led_int').value),
  led_pattern:parseInt(document.getElementById('led_pattern').value),
  led_color:(document.getElementById('led_pattern').value!=='0'&&document.getElementById('led_pattern').value!=='3')?document.getElementById('led_color').value:'',
  hue_shift:parseInt(document.getElementById('hue_shift').value)||0
 };
 if(!body.password) delete body.password;
 try{
  let r=await secPost('/api/config',body);
  let d=await r.json();
  if(r.ok){showMsg(d.msg||'Saved!','ok');markClean();if(d.restart)setTimeout(()=>showMsg('Restarting...','ok'),1000);}
  else{showMsg(d.msg||'Save failed','err');}
 }catch(e){showMsg('Error: '+e,'err');}
}

async function scanWifi(){
 document.getElementById('networks').style.display='block';
 let sel=document.getElementById('netlist');
 sel.innerHTML='<option>Scanning...</option>';
 try{
  let init=await fetch('/api/scan');
  if(!init.ok){sel.innerHTML='<option>Login required</option>';return;}
  // Poll for results
  let attempts=0;
  let poll=setInterval(async()=>{
   attempts++;
   if(attempts>15){clearInterval(poll);sel.innerHTML='<option>Scan timeout</option>';return;}
   try{
    let r=await fetch('/api/scan');
    if(!r.ok){clearInterval(poll);sel.innerHTML='<option>Login required</option>';return;}
    let d=await r.json();
    if(d.scanning)return;
    clearInterval(poll);
    sel.innerHTML='<option value="">Select network...</option>';
    d.forEach(n=>{
     let o=document.createElement('option');
     o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+'dBm)';
     sel.appendChild(o);
    });
   }catch(e){}
  },1000);
 }catch(e){sel.innerHTML='<option>Scan failed</option>';}
}

async function loadStatus(){
 try{
  let r=await fetch('/api/status');
  let d=await r.json();
  if(d.name){document.getElementById('devname').textContent=d.name;document.title=d.name;}
  document.getElementById('st_time').textContent=d.time||'--';
  document.getElementById('st_wifi').textContent=d.wifi||'--';
  document.getElementById('st_ip').textContent=d.ip||'--';
  document.getElementById('st_rssi').innerHTML=rssiLabel(d.rssi);
  document.getElementById('st_ntp').textContent=d.ntp||'--';
  document.getElementById('st_up').textContent=d.uptime||'--';
  document.getElementById('st_ap').textContent=d.ap?'Active':'Off';
  if(!hasUnsaved) document.getElementById('ap_toggle').checked=d.ap;
  if(d.version) document.getElementById('st_ver').textContent=d.version;
 }catch(e){}
}

// Helper for POST requests -- includes CSRF token and Content-Type headers
function secPost(url,body){
 let h={'X-CSRF-Token':CSRF_TOKEN};
 let opts={method:'POST',headers:h};
 if(body){h['Content-Type']='application/json';opts.body=JSON.stringify(body);}
 return fetch(url,opts);
}

function showMsg(t,c){
 let stack=document.getElementById('toast-stack');
 let toast=document.createElement('div');
 toast.className='toast '+c;
 toast.textContent=t;
 stack.appendChild(toast);
 setTimeout(()=>{toast.remove();},3500);
}

async function ntpSync(){
 try{
  let r=await secPost('/api/ntp_sync');
  let d=await r.json();
  showMsg(d.msg||'Sync done',d.ok?'ok':'err');
  loadStatus();
 }catch(e){showMsg('Sync error','err');}
}

async function toggleAP(){
 try{
  let r=await secPost('/api/toggle_ap');
  let d=await r.json();
  showMsg(d.msg||'Done','ok');
  loadStatus();
 }catch(e){showMsg('AP toggle failed','err');}
}

async function rebootClock(){
 showMsg('Rebooting...','ok');
 try{await secPost('/api/reboot');}catch(e){}
 setTimeout(()=>{showMsg('Clock is rebooting. Page will reload shortly.','ok');},2000);
 setTimeout(()=>{location.reload();},8000);
}

async function factoryReset(){
 showMsg('Performing factory reset...','ok');
 try{
  let r=await secPost('/api/factory_reset');
  let d=await r.json();
  showMsg(d.msg||'Rebooting...','ok');
 }catch(e){}
 setTimeout(()=>{showMsg('Clock is rebooting. Connect to the AP to reconfigure.','ok');},2000);
}

// Unsaved changes tracking. loading=true suppresses dirty tracking
// while loadConfig() populates form fields (programmatic value changes
// fire oninput/onchange events which would falsely trigger the bar).
let hasUnsaved=false;
let loading=true;
function markDirty(){
 if(loading)return;
 if(!hasUnsaved){hasUnsaved=true;document.getElementById('unsaved-bar').style.display='block';}
}
function markClean(){
 hasUnsaved=false;document.getElementById('unsaved-bar').style.display='none';
}

// Live preview -- sends display changes to hardware immediately without saving
let previewTimer=null;
function previewDebounce(body){
 markDirty();
 clearTimeout(previewTimer);
 previewTimer=setTimeout(async()=>{
  try{
   let r=await secPost('/api/preview',body);
   let d=await r.json();
   if(d.night) showMsg('Night mode is active at this time — display may be dimmed or off','err');
  }catch(e){}
 },100);
}
function previewHourFmt(){
 previewDebounce({twelve_hour:document.getElementById('h12').checked});
}
function previewTFT(){
 previewDebounce({day_tft:Math.round(parseInt(document.getElementById('day_tft').value)*255/100)});
}
function previewHue(){
 let deg=document.getElementById('hue_shift').value;
 document.querySelectorAll('.faces img').forEach(img=>{img.style.filter=deg>0?'hue-rotate('+deg+'deg)':'';});
 previewDebounce({hue_shift:parseInt(deg)});
}
function previewFace(v){
 previewDebounce({graphic:parseInt(v)});
}
function previewLED(){
 let p=parseInt(document.getElementById('led_pattern').value);
 let body={led_pattern:p,led_intensity:parseInt(document.getElementById('led_int').value)};
 if(p!==0&&p!==3) body.led_color=document.getElementById('led_color').value;
 previewDebounce(body);
}

// Colour swatches for quick selection
// Colour picker modal -- 18 columns x 14 rows arranged by hue/brightness
// Row 0: pure saturated hues (red -> yellow -> green -> cyan -> blue -> magenta)
// Rows 1-11: progressively lighter/darker tints and shades
// Row 12-13: greyscale ramp from black to white
function buildColourPicker(){
 let grid=document.getElementById('cpick_grid');
 let cols=18;
 grid.style.gridTemplateColumns='repeat('+cols+',1fr)';
 grid.innerHTML='';

 // Generate hue rows: 12 rows of 18 hues at varying saturation/lightness
 for(let row=0;row<12;row++){
  let l=row<6?50+row*8:80-((row-6)*12); // lightness curve
  let s=row<6?100:100-((row-6)*12);     // saturation curve
  for(let col=0;col<cols;col++){
   let h=col*(360/cols);
   let hex=hslToHex(h,s,l);
   addSwatch(grid,hex);
  }
 }
 // Greyscale rows (2 rows of 18)
 for(let i=0;i<cols*2;i++){
  let v=Math.round(i*255/(cols*2-1));
  let hex='#'+v.toString(16).padStart(2,'0').repeat(3).toUpperCase();
  addSwatch(grid,hex);
 }
}

function addSwatch(grid,hex){
 let s=document.createElement('div');
 s.style.background=hex;
 s.title=hex;
 s.onclick=()=>selectColour(hex);
 grid.appendChild(s);
}

function selectColour(hex){
 document.getElementById('led_color').value=hex;
 document.getElementById('led_swatch_btn').style.background=hex;
 document.getElementById('led_hex').textContent=hex.toUpperCase();
 document.getElementById('cpick_preview_swatch').style.background=hex;
 document.getElementById('cpick_preview_hex').textContent=hex.toUpperCase();
 previewLED();
}

function openColourPicker(){
 document.getElementById('cpick_overlay').classList.add('open');
}
function closeColourPicker(){
 document.getElementById('cpick_overlay').classList.remove('open');
}

// Convert HSL to hex string
function hslToHex(h,s,l){
 s/=100;l/=100;
 let c=(1-Math.abs(2*l-1))*s,x=c*(1-Math.abs((h/60)%2-1)),m=l-c/2;
 let r=0,g=0,b=0;
 if(h<60){r=c;g=x}else if(h<120){r=x;g=c}else if(h<180){g=c;b=x}
 else if(h<240){g=x;b=c}else if(h<300){r=x;b=c}else{r=c;b=x}
 r=Math.round((r+m)*255);g=Math.round((g+m)*255);b=Math.round((b+m)*255);
 return'#'+[r,g,b].map(v=>v.toString(16).padStart(2,'0')).join('').toUpperCase();
}

function syncFaceSelection(v){
 document.getElementById('face_preview').querySelectorAll('img').forEach(x=>{
  x.classList.toggle('sel',x.dataset.face===String(v));
 });
}

function loadFacePreviews(n,sel){
 let c=document.getElementById('face_preview');
 c.innerHTML='';
 for(let i=1;i<=n;i++){
  let img=document.createElement('img');
  img.src='/api/face?f='+i;
  img.alt='Face '+i;
  img.dataset.face=i;
  let hd=document.getElementById('hue_shift').value;
  img.style.cssText='height:auto;background:#000;width:calc((100% - '+((n-1)*8)+'px) / '+n+')'+(hd>0?';filter:hue-rotate('+hd+'deg)':'');
  if(i===sel) img.classList.add('sel');
  img.onclick=(()=>{let fi=i;return()=>{
   selectedGraphic=fi;
   syncFaceSelection(fi);
   previewFace(fi);
  }})();
  c.appendChild(img);
 }
}

// Login handling
async function doLogin(){
 let p=document.getElementById('login_pass').value;
 if(!p){document.getElementById('login_err').textContent='Enter password';document.getElementById('login_err').style.display='block';return;}
 try{
  let r=await fetch('/api/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:p})});
  let d=await r.json();
  if(d.ok){location.reload();}
  else{document.getElementById('login_err').textContent=d.msg||'Login failed';document.getElementById('login_err').style.display='block';}
 }catch(e){document.getElementById('login_err').textContent='Connection error';document.getElementById('login_err').style.display='block';}
}

async function setPassword(){
 let np=document.getElementById('pw_new').value;
 let nc=document.getElementById('pw_confirm').value;
 let op=document.getElementById('pw_old').value;
 document.getElementById('pw_match_err').style.display='none';
 if(!np||np.length<4){showMsg('Password must be at least 4 characters','err');return;}
 if(np!==nc){document.getElementById('pw_match_err').style.display='block';return;}
 let body={new_password:np};
 if(op) body.old_password=op;
 try{
  let r=await secPost('/api/set_password',body);
  let d=await r.json();
  showMsg(d.msg||'Done',d.ok?'ok':'err');
  if(d.ok){
   document.getElementById('pw_new').value='';
   document.getElementById('pw_confirm').value='';
   document.getElementById('pw_old').value='';
   // Force immediate login prompt so the user verifies their new password
   showMsg('Password set. Please log in.','ok');
   setTimeout(()=>{location.reload();},1500);
  }
 }catch(e){showMsg('Error setting password','err');}
}

async function removePassword(){
 let op=document.getElementById('pw_old').value;
 if(!op){showMsg('Enter current password first','err');return;}
 try{
  let r=await secPost('/api/set_password',{old_password:op,new_password:''});
  let d=await r.json();
  showMsg(d.msg||'Done',d.ok?'ok':'err');
  if(d.ok){document.getElementById('pw_old').value='';updatePwUI(false);}
 }catch(e){showMsg('Error removing password','err');}
}

function updatePwUI(has_password){
 document.getElementById('pw_old').style.display=has_password?'block':'none';
 document.getElementById('pw_old_label').style.display=has_password?'block':'none';
 document.getElementById('pw_remove_btn').style.display=has_password?'block':'none';
}

// OTA firmware upload
async function uploadFirmware(){
 let file=document.getElementById('ota_file').files[0];
 if(!file){showMsg('Select a .bin file first','err');return;}
 if(!file.name.endsWith('.bin')){showMsg('File must be a .bin firmware file','err');return;}

 document.getElementById('ota_progress').style.display='block';
 document.getElementById('ota_status').textContent='Uploading...';
 document.getElementById('ota_bar').style.width='0%';

 let form=new FormData();
 form.append('firmware',file);

 try{
  let xhr=new XMLHttpRequest();
  xhr.open('POST','/api/ota');
  xhr.setRequestHeader('X-CSRF-Token',CSRF_TOKEN);
  xhr.upload.onprogress=function(e){
   if(e.lengthComputable){
    let pct=Math.round((e.loaded/e.total)*100);
    document.getElementById('ota_bar').style.width=pct+'%';
    document.getElementById('ota_status').textContent='Uploading... '+pct+'%';
   }
  };
  xhr.onload=function(){
   if(xhr.status===200){
    document.getElementById('ota_status').textContent='Update complete. Rebooting...';
    document.getElementById('ota_bar').style.width='100%';
    setTimeout(()=>{location.reload();},10000);
   }else{
    let msg='Upload failed';
    try{msg=JSON.parse(xhr.responseText).msg;}catch(e){}
    document.getElementById('ota_status').textContent=msg;
    showMsg(msg,'err');
   }
  };
  xhr.onerror=function(){
   document.getElementById('ota_status').textContent='Upload error';
   showMsg('Upload failed','err');
  };
  xhr.send(form);
 }catch(e){showMsg('Upload error','err');}
}

async function doLogout(){
 try{await secPost('/api/logout');}catch(e){}
 location.reload();
}

// Init -- check auth state before loading the main UI
if(AUTH_REQUIRED && !HAS_SESSION){
 document.getElementById('login-overlay').style.display='flex';
}else{
 loadTZ().then(loadConfig);
 loadStatus();
}
updatePwUI(AUTH_REQUIRED);
if(AUTH_REQUIRED && HAS_SESSION) document.getElementById('logoutBtn').style.display='';
buildColourPicker();
setInterval(loadStatus,5000);
</script>
</body>
</html>
)rawliteral";


// -----------------------------------------------------------------------
// Security -- CSRF token and request validation
// -----------------------------------------------------------------------

// Generate a 16-character hex token from the ESP32's hardware RNG.
// Called once at boot. The token is embedded in the SPA HTML and must
// be sent back in the X-CSRF-Token header on every POST request.
// This prevents drive-by attacks from scripts that haven't loaded our page.
void WebConfig::generate_csrf_token() {
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  snprintf(csrf_token, sizeof(csrf_token), "%08x%08x", r1, r2);
}

// -----------------------------------------------------------------------
// Authentication -- dashboard password and session management
// -----------------------------------------------------------------------

// SHA-256 hash a string and write the 64-char hex digest to output.
// Uses mbedTLS which is included in the ESP32 ROM (no extra flash cost).
void WebConfig::sha256_hex(const char *input, char *output) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx, (const uint8_t *)input, strlen(input));
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  for (int i = 0; i < 32; i++) {
    sprintf(output + i * 2, "%02x", hash[i]);
  }
  output[64] = '\0';
}

// Returns true if a dashboard password has been configured.
// Also validates that the stored hash looks like a valid SHA-256 hex digest
// (64 hex chars). This handles the case where the NVS was written by an
// older firmware version that didn't have this field — the garbage bytes
// won't pass the hex validation check.
bool WebConfig::auth_required() {
  const char *h = config->wifi.dashboard_password_hash;
  if (h[0] == '\0') return false;
  // Validate it looks like a 64-char hex digest
  for (int i = 0; i < 64; i++) {
    char c = h[i];
    if (c == '\0') return false;  // Too short
    if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
  }
  return h[64] == '\0';  // Must be exactly 64 chars
}

// Check if the current request has a valid session cookie.
// Returns true if auth is not required OR session is valid.
// Sends 401 and returns false if auth is required but session is invalid.
bool WebConfig::check_auth() {
  if (!auth_required()) return true;

  // Check for session cookie
  if (server.hasHeader("Cookie")) {
    String cookie = server.header("Cookie");
    String expected = "session=" + String(session_token);
    if (cookie.indexOf(expected) >= 0 && session_token[0] != '\0') {
      return true;
    }
  }

  server.send(401, "application/json", "{\"msg\":\"Login required\",\"auth\":true}");
  return false;
}

// POST /api/login -- authenticate with the dashboard password
void WebConfig::handleLogin() {
  // Rate limit login attempts to prevent brute-force (uses the global
  // token bucket rather than a blocking delay that would freeze the server)
  if (!check_rate_limit()) return;

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"msg\":\"No data\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"msg\":\"Invalid JSON\"}");
    return;
  }

  const char *password = doc["password"];
  if (!password) {
    server.send(400, "application/json", "{\"msg\":\"Missing password\"}");
    return;
  }

  // Hash the submitted password and compare to stored hash
  char hash[65];
  sha256_hex(password, hash);

  // Constant-time comparison prevents timing side-channel attacks.
  // XOR-accumulate pattern: the loop always runs all 64 iterations
  // regardless of where the first mismatch is, so execution time
  // does not reveal the position of correct hash characters.
  volatile uint8_t diff = 0;
  for (int i = 0; i < 64; i++) {
    diff |= (uint8_t)hash[i] ^ (uint8_t)config->wifi.dashboard_password_hash[i];
  }
  if (diff != 0) {
    server.send(401, "application/json", "{\"msg\":\"Wrong password\"}");
    return;
  }

  // Generate a session token
  uint32_t r1 = esp_random();
  uint32_t r2 = esp_random();
  snprintf(session_token, sizeof(session_token), "%08x%08x", r1, r2);

  // Set session cookie (HttpOnly prevents JS access, SameSite=Strict
  // prevents CSRF via cookie — belt-and-suspenders with our CSRF token)
  String cookie = "session=" + String(session_token) + "; HttpOnly; SameSite=Strict; Path=/";
  server.sendHeader("Set-Cookie", cookie);
  server.send(200, "application/json", "{\"msg\":\"Login successful\",\"ok\":true}");
}

// POST /api/set_password -- set or change the dashboard password
void WebConfig::handleSetPassword() {
  if (!validate_request()) return;

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"msg\":\"No data\"}");
    return;
  }

  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"msg\":\"Invalid JSON\"}");
    return;
  }

  // If a password is already set, require the old password
  if (auth_required()) {
    const char *old_pass = doc["old_password"];
    if (!old_pass) {
      server.send(400, "application/json", "{\"msg\":\"Old password required\"}");
      return;
    }
    char old_hash[65];
    sha256_hex(old_pass, old_hash);
    volatile uint8_t old_diff = 0;
    for (int i = 0; i < 64; i++) {
      old_diff |= (uint8_t)old_hash[i] ^ (uint8_t)config->wifi.dashboard_password_hash[i];
    }
    if (old_diff != 0) {
      server.send(401, "application/json", "{\"msg\":\"Old password incorrect\"}");
      return;
    }
  }

  const char *new_pass = doc["new_password"];
  if (!new_pass || strlen(new_pass) == 0) {
    // Clear the password (disable auth)
    config->wifi.dashboard_password_hash[0] = '\0';
    storedConfig->save();
    server.send(200, "application/json", "{\"msg\":\"Password removed\",\"ok\":true}");
    return;
  }

  if (strlen(new_pass) < 4) {
    server.send(400, "application/json", "{\"msg\":\"Password must be at least 4 characters\"}");
    return;
  }

  // Hash and store
  sha256_hex(new_pass, config->wifi.dashboard_password_hash);
  storedConfig->save();

  // Invalidate the current session so the user is prompted to log in
  // with their new password immediately (the JS reloads the page)
  session_token[0] = '\0';
  String cookie = "session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
  server.sendHeader("Set-Cookie", cookie);
  server.send(200, "application/json", "{\"msg\":\"Password set\",\"ok\":true}");
}

// Check the X-CSRF-Token header against our stored token.
// Returns true if valid. Sends 403 Forbidden and returns false if not.
bool WebConfig::validate_csrf_token() {
  if (!server.hasHeader("X-CSRF-Token") ||
      strcmp(server.header("X-CSRF-Token").c_str(), csrf_token) != 0) {
    server.send(403, "application/json", "{\"msg\":\"Invalid or missing CSRF token\"}");
    return false;
  }
  return true;
}

// Rate limiter using the token bucket algorithm.
// Allows burst of 10 requests, refills at 2 tokens/sec sustained.
// A global bucket is appropriate for a single-user config device -- per-IP
// tracking would waste heap RAM on a 328KB device.
bool WebConfig::check_rate_limit() {
  uint32_t now = millis();
  // Refill tokens based on elapsed time (2 tokens per second)
  rate_tokens += (now - rate_last_update) * 0.002f;
  if (rate_tokens > 10.0f) rate_tokens = 10.0f;  // Cap at burst size
  rate_last_update = now;

  if (rate_tokens >= 1.0f) {
    rate_tokens -= 1.0f;
    return true;
  }
  server.send(429, "application/json", "{\"msg\":\"Too many requests\"}");
  return false;
}

// Combined validation for all requests. Checks:
// 1. Rate limit -- prevents brute-force and DoS attacks
// 2. Host header -- defeats DNS rebinding by rejecting requests where the
//    browser thinks it's talking to a different domain. Uses exact IP match
//    (not startsWith) to prevent crafted domains like "192.168.evil.com".
// 3. CSRF token (POST only) -- prevents unauthenticated state mutations
bool WebConfig::validate_request() {
  // Rate limiting first -- reject floods before doing any work
  if (!check_rate_limit()) return false;

  // Host header validation: accept only our actual IPs or hostname.
  String host = server.hostHeader();
  // Strip port if present (e.g. "192.168.4.1:80" -> "192.168.4.1")
  int colon = host.indexOf(':');
  if (colon > 0) host = host.substring(0, colon);

  if (host.length() > 0) {
    // DNS hostnames are case-insensitive (RFC 4343), so compare lowercase.
    // Browsers typically send the Host header in lowercase even if the
    // user typed mixed case in the URL bar.
    host.toLowerCase();
    String expected_name = String(WifiAPName);
    expected_name.toLowerCase();
    String expected_local = expected_name + ".local";
    bool valid_host = (host == "192.168.4.1") ||
                      (host == WiFi.localIP().toString()) ||
                      (host == expected_name) ||
                      (host == expected_local);
    if (!valid_host) {
      server.send(403, "application/json", "{\"msg\":\"Invalid host\"}");
      return false;
    }
  }
  // Authentication check -- if a password is set, require a valid session
  if (!check_auth()) return false;

  // POST requests must include a valid CSRF token
  if (server.method() == HTTP_POST) {
    return validate_csrf_token();
  }
  return true;
}

// -----------------------------------------------------------------------
// Route registration and server startup
// -----------------------------------------------------------------------

void WebConfig::begin(StoredConfig *sc) {
  storedConfig = sc;
  config = &sc->config;

  // Generate CSRF token and clear session state
  generate_csrf_token();
  session_token[0] = '\0';  // No active session until login
  Serial.println("CSRF token generated");
  Serial.print("Auth required: ");
  Serial.println(auth_required() ? "yes" : "no");

  // Tell the WebServer to capture these headers (they're not collected by default)
  const char *headers[] = {"X-CSRF-Token", "Host", "Cookie"};
  server.collectHeaders(headers, 3);

  // Authentication and password management (no CSRF needed for login)
  server.on("/api/login", HTTP_POST, [this]() { handleLogin(); });
  server.on("/api/set_password", HTTP_POST, [this]() { handleSetPassword(); });

  // Logout -- clears the session cookie and token
  server.on("/api/logout", HTTP_POST, [this]() {
    if (!validate_request()) return;
    session_token[0] = '\0';
    String cookie = "session=; HttpOnly; SameSite=Strict; Path=/; Max-Age=0";
    server.sendHeader("Set-Cookie", cookie);
    server.send(200, "application/json", "{\"msg\":\"Logged out\",\"ok\":true}");
  });

  // OTA firmware upload -- uses the WebServer's built-in upload handler
  // for streaming multipart/form-data file uploads. The first lambda handles
  // the final response, the second handles each data chunk as it arrives.
  server.on("/api/ota", HTTP_POST,
    [this]() { handleOTAUpload(); },
    [this]() { handleOTAUploadData(); }
  );

  // Register all HTTP routes. ESP32 WebServer uses exact path matching
  // with optional HTTP method filtering. Lambda captures [this] to
  // forward to member functions (WebServer callbacks must be free functions
  // or lambdas, not direct member function pointers).
  server.on("/", [this]() { handleRoot(); });
  server.on("/api/config", HTTP_GET, [this]() { handleGetConfig(); });
  server.on("/api/config", HTTP_POST, [this]() { handleSetConfig(); });
  server.on("/api/scan", [this]() { handleScanWifi(); });
  server.on("/api/status", [this]() { handleGetStatus(); });
  server.on("/api/preview", HTTP_POST, [this]() { handlePreview(); });

  // Timezone list endpoint -- built manually instead of using ArduinoJson
  // because the timezone table can be large and we want to avoid allocating
  // a huge JsonDocument. String concatenation with reserve() is more
  // memory-efficient for this simple array-of-objects structure.
  server.on("/api/timezones", [this]() {
    String json;
    json.reserve(4096);  // Pre-allocate to avoid repeated reallocation
    json = "[";
    for (int i = 0; i < tz_entry_count; i++) {
      if (i > 0) json += ",";
      json += "{\"label\":\"";
      json += tz_entries[i].label;
      json += "\",\"tz\":\"";
      json += tz_entries[i].posix_tz;    // POSIX TZ string (e.g., "EST5EDT,M3.2.0,M11.1.0")
      json += "\",\"iana\":\"";
      json += tz_entries[i].iana;        // IANA name (e.g., "America/New_York") for browser matching
      json += "\"}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });

  server.on("/api/ntp_sync", HTTP_POST, [this]() { handleNTPSync(); });
  server.on("/api/face", [this]() { handleFacePreview(); });
  server.on("/api/factory_reset", HTTP_POST, [this]() { handleFactoryReset(); });
  server.on("/api/reboot", HTTP_POST, [this]() {
    if (!validate_request()) return;
    server.send(200, "application/json", "{\"msg\":\"Rebooting...\",\"ok\":true}");
    server.client().flush();
    delay(200);
    server.client().stop();
    delay(300);
    ESP.restart();
  });

  // AP toggle -- also starts/stops the captive portal DNS so that newly
  // connected clients are automatically redirected to the config page
  server.on("/api/toggle_ap", HTTP_POST, [this]() {
    if (!validate_request()) return;
    WifiToggleAP();
    if (APRunning) { startDNS(); } else { stopDNS(); }
    String json = "{\"ap\":";
    json += APRunning ? "true" : "false";
    json += ",\"msg\":\"AP ";
    json += APRunning ? "enabled" : "disabled";
    json += "\"}";
    server.send(200, "application/json", json);
  });

  // Captive portal detection endpoints -- different OSes/browsers probe
  // specific URLs to detect captive portals. By intercepting these and
  // redirecting to our config page, the OS auto-opens a browser window.
  //   generate_204:       Android/Chrome
  //   hotspot-detect.html: Apple iOS/macOS
  //   connecttest.txt:     Windows 10/11
  //   redirect:            Firefox
  //   canonical.html:      Chromium fallback
  server.on("/generate_204", [this]() { handleNotFound(); });
  server.on("/hotspot-detect.html", [this]() { handleNotFound(); });
  server.on("/connecttest.txt", [this]() { handleNotFound(); });
  server.on("/redirect", [this]() { handleNotFound(); });
  server.on("/canonical.html", [this]() { handleNotFound(); });

  // Catch-all for any unregistered path
  server.onNotFound([this]() { handleNotFound(); });

  server.begin();
  Serial.println("Web server started on port 80");

  // If the clock booted into AP mode, start captive portal immediately
  if (APRunning) {
    startDNS();
  }
}

// -----------------------------------------------------------------------
// Captive portal DNS -- resolves ALL domains to the ESP32's AP IP
// -----------------------------------------------------------------------

void WebConfig::startDNS() {
  if (!dns_running && APRunning) {
    // Port 53 is standard DNS. The wildcard "*" means every domain name
    // resolves to our softAP IP, forcing the client's browser to us.
    dnsServer.start(53, "*", WiFi.softAPIP());
    dns_running = true;
    Serial.println("DNS captive portal started");
  }
}

void WebConfig::stopDNS() {
  if (dns_running) {
    dnsServer.stop();
    dns_running = false;
    Serial.println("DNS captive portal stopped");
  }
}

// -----------------------------------------------------------------------
// Main loop integration -- must be called every iteration
// -----------------------------------------------------------------------

void WebConfig::loop() {
  // Process one pending HTTP request (if any). This is non-blocking:
  // it returns immediately if no client is waiting.
  server.handleClient();
  // Process one pending DNS query. Required for captive portal to work --
  // without this, connected clients can't resolve any domain names.
  if (dns_running) {
    dnsServer.processNextRequest();
  }
}

// -----------------------------------------------------------------------
// HTTP endpoint handlers
// -----------------------------------------------------------------------

void WebConfig::handleRoot() {
  // Disable caching so the browser always gets the latest version after
  // a firmware update (the HTML is baked into the firmware binary).
  // We inject the CSRF token as a JS variable just before the closing
  // </head> tag. The SPA's fetch() calls include it in X-CSRF-Token headers.
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");

  // Inject CSRF token and auth state into the SPA so the JS can
  // decide whether to show a login form and include the token in requests
  String page = FPSTR(html_page);
  // Only inject the real CSRF token if the user is authenticated (or no
  // password is set). This prevents unauthenticated AP visitors from
  // extracting the token and using it to call protected endpoints.
  String inject = "<script>const CSRF_TOKEN='";
  bool has_session = false;
  if (server.hasHeader("Cookie") && session_token[0] != '\0') {
    has_session = server.header("Cookie").indexOf("session=" + String(session_token)) >= 0;
  }
  inject += (has_session || !auth_required()) ? csrf_token : "";
  inject += "';const AUTH_REQUIRED=";
  inject += auth_required() ? "true" : "false";
  inject += ";const HAS_SESSION=";
  inject += (has_session || !auth_required()) ? "true" : "false";
  inject += ";</script></head>";
  page.replace("</head>", inject);

  server.send(200, "text/html", page);
}

// GET /api/config -- Serializes all current settings to JSON for the browser.
// The browser calls this on page load to populate form fields.
// Note: password is intentionally omitted (write-only for security).
void WebConfig::handleGetConfig() {
  if (!validate_request()) return;
  JsonDocument doc;
  doc["ssid"] = config->wifi.ssid;
  doc["tz"] = config->uclock.tz_string;
  doc["twelve_hour"] = config->uclock.twelve_hour;
  doc["blank_zero"] = config->uclock.blank_hours_zero;
  doc["graphic"] = config->uclock.selected_graphic;
  doc["num_faces"] = tfts.NumberOfClockFaces;  // Detected at boot from SPIFFS file count
  doc["night_time"] = config->uclock.night_time;
  doc["day_time"] = config->uclock.day_time;
  doc["night_enabled"] = config->uclock.night_mode_enabled;
  doc["night_tft"] = config->uclock.night_tft_intensity;
  doc["night_led"] = config->uclock.night_led_intensity;
  doc["day_tft"] = config->uclock.day_tft_intensity;
  doc["hue_shift"] = config->uclock.hue_shift;
  doc["led_intensity"] = config->backlights.intensity;
  doc["led_pattern"] = config->backlights.pattern;
  // Convert the stored 24-bit RGB integer to a CSS hex color string
  char colbuf[8];
  snprintf(colbuf, sizeof(colbuf), "#%06X", (unsigned int)(config->backlights.fixed_color & 0xFFFFFF));
  doc["led_color"] = config->backlights.fixed_color != 0 ? colbuf : "";
  doc["ap_enabled"] = APRunning;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// POST /api/config -- Receives a JSON body with new settings from the browser.
// Each field is optional (partial updates are supported). WiFi credential
// changes trigger a full ESP32 restart because the WiFi stack must
// reinitialize with new credentials.
void WebConfig::handleSetConfig() {
  // Security: validate CSRF token and Host header
  if (!validate_request()) return;

  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"msg\":\"No data\"}");
    return;
  }

  // Reject oversized payloads to prevent heap exhaustion DoS.
  // Normal config JSON is well under 500 bytes.
  if (server.arg("plain").length() > 1024) {
    server.send(413, "application/json", "{\"msg\":\"Request too large\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server.arg("plain"));
  if (err) {
    server.send(400, "application/json", "{\"msg\":\"Invalid JSON\"}");
    return;
  }

  bool wifi_changed = false;
  bool needs_restart = false;

  // --- WiFi credentials ---
  // Only update if the SSID actually changed (avoids unnecessary restarts).
  // strncpy + explicit null termination prevents buffer overflow from
  // malicious or oversized input.
  if (doc.containsKey("ssid")) {
    const char *new_ssid = doc["ssid"];
    if (strcmp(config->wifi.ssid, new_ssid) != 0) {
      strncpy(config->wifi.ssid, new_ssid, sizeof(config->wifi.ssid) - 1);
      config->wifi.ssid[sizeof(config->wifi.ssid) - 1] = '\0';
      wifi_changed = true;
    }
  }
  if (doc.containsKey("password")) {
    const char *new_pass = doc["password"];
    // Empty password means "keep existing" -- the browser sends empty
    // string when the user hasn't changed the password field
    if (strlen(new_pass) > 0) {
      if (strlen(new_pass) > sizeof(config->wifi.password) - 1) {
        server.send(400, "application/json", "{\"msg\":\"WiFi password too long (max 31 chars)\"}");
        return;
      }
      strncpy(config->wifi.password, new_pass, sizeof(config->wifi.password) - 1);
      config->wifi.password[sizeof(config->wifi.password) - 1] = '\0';
      wifi_changed = true;
    }
  }
  if (wifi_changed) {
    // Mark WiFi config as valid so the boot sequence knows credentials
    // were intentionally set (distinguishes from zeroed-out flash)
    config->wifi.is_valid = StoredConfig::valid;
    needs_restart = true;
  }

  // --- Timezone ---
  // Apply immediately via POSIX setenv/tzset so the running clock
  // updates without a reboot. The TZ string follows POSIX format
  // (e.g., "EST5EDT,M3.2.0,M11.1.0") which the ESP32's newlib understands.
  if (doc.containsKey("tz")) {
    const char *new_tz = doc["tz"];
    if (!new_tz) new_tz = "";  // Guard against JSON null -> nullptr
    // Validate against the compiled timezone table to prevent injection
    // of malformed POSIX TZ strings into setenv/tzset
    bool tz_valid = false;
    for (int i = 0; i < tz_entry_count; i++) {
      if (strcmp(new_tz, tz_entries[i].posix_tz) == 0) {
        tz_valid = true;
        break;
      }
    }
    if (tz_valid) {
      strncpy(config->uclock.tz_string, new_tz, sizeof(config->uclock.tz_string) - 1);
      config->uclock.tz_string[sizeof(config->uclock.tz_string) - 1] = '\0';
      setenv("TZ", config->uclock.tz_string, 1);
      tzset();
    }
  }

  // --- Display settings ---
  if (doc.containsKey("twelve_hour")) {
    config->uclock.twelve_hour = doc["twelve_hour"];
  }
  if (doc.containsKey("blank_zero")) {
    config->uclock.blank_hours_zero = doc["blank_zero"];
  }
  if (doc.containsKey("graphic")) {
    int8_t g = doc["graphic"];
    if (g >= 1 && g <= tfts.NumberOfClockFaces) {
      config->uclock.selected_graphic = g;
      tfts.current_graphic = g;
      // Force all six displays to reload their images from SPIFFS
      // with the new graphic set on the next display update cycle
      tfts.InvalidateImageInBuffer();
    }
  }

  // --- Night mode settings ---
  if (doc.containsKey("night_enabled")) {
    config->uclock.night_mode_enabled = doc["night_enabled"];
  }
  // Night/day hour boundaries are validated to 0-23 range
  if (doc.containsKey("night_time")) {
    uint8_t nt = doc["night_time"];
    if (nt <= 23) config->uclock.night_time = nt;
  }
  if (doc.containsKey("day_time")) {
    uint8_t dt = doc["day_time"];
    if (dt <= 23) config->uclock.day_time = dt;
  }
  if (doc.containsKey("night_tft")) {
    config->uclock.night_tft_intensity = doc["night_tft"];
  }
  if (doc.containsKey("night_led")) {
    uint8_t nl = doc["night_led"];
    if (nl <= 7) config->uclock.night_led_intensity = nl;
  }
  if (doc.containsKey("day_tft")) {
    uint8_t dtft = doc["day_tft"];
    if (dtft >= 1) config->uclock.day_tft_intensity = dtft;
  }
  if (doc.containsKey("hue_shift")) {
    uint16_t hs = doc["hue_shift"];
    if (hs < 360) {
      config->uclock.hue_shift = hs;
      tfts.hue_shift = hs;
      tfts.computeHueMatrix(hs);
      tfts.InvalidateImageInBuffer();
    }
  }

  // --- Backlight LED settings ---
  if (doc.containsKey("led_intensity")) {
    uint8_t li = doc["led_intensity"];
    if (li <= 7) config->backlights.intensity = li;  // 0-7 maps to WS2812 brightness
  }
  if (doc.containsKey("led_pattern")) {
    uint8_t lp = doc["led_pattern"];
    if (lp < 6) config->backlights.pattern = lp;
  }
  if (doc.containsKey("led_color")) {
    const char *lc = doc["led_color"];
    // Parse CSS hex color (#RRGGBB) into a 24-bit integer for WS2812 output
    if (lc && strlen(lc) == 7 && lc[0] == '#') {
      config->backlights.fixed_color = strtoul(lc + 1, NULL, 16);
    } else {
      config->backlights.fixed_color = 0;
    }
  }
  // Push backlight changes to hardware immediately so the user sees
  // the effect without waiting for the next main loop cycle
  backlights.setIntensity(config->backlights.intensity);
  backlights.forceRefresh();

  // Mark all config sections as valid so they survive reboots
  // (prevents default-overwrite on next boot)
  config->uclock.is_valid = StoredConfig::valid;
  config->backlights.is_valid = StoredConfig::valid;

  // Persist all changes to ESP32 NVS (non-volatile storage)
  storedConfig->save();
  Serial.println("Config saved via web UI");

  // Tell the browser whether a restart is coming (WiFi credential change).
  // The browser shows a "Restarting..." message so the user knows to
  // reconnect after the clock reboots.
  String resp = "{\"msg\":\"Settings saved!\"";
  if (needs_restart) {
    resp += ",\"restart\":true";
  }
  resp += "}";
  server.send(200, "application/json", resp);

  if (needs_restart) {
    // Brief delay ensures the HTTP response is fully sent before reboot
    delay(1000);
    ESP.restart();
  }
}

// GET /api/scan -- Asynchronous WiFi network scanning
//
// Uses a poll-based pattern because WiFi.scanNetworks() takes several
// seconds and we can't block the web server that long. Flow:
//   1. First call: starts an async scan, returns {"scanning":true}
//   2. Browser polls every 1 second
//   3. Subsequent calls: if scan is still running, return {"scanning":true}
//   4. When complete: return the results array and free scan memory
void WebConfig::handleScanWifi() {
  if (!check_auth()) return;
  int n = WiFi.scanComplete();
  if (n == WIFI_SCAN_FAILED) {
    // No scan in progress -- start one. The `true` parameter makes it
    // asynchronous (returns immediately, runs in background).
    WiFi.scanNetworks(true);
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }
  if (n == WIFI_SCAN_RUNNING) {
    // Scan still in progress -- tell the browser to keep polling
    server.send(200, "application/json", "{\"scanning\":true}");
    return;
  }

  // Scan complete -- n is the number of networks found
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (int i = 0; i < n && i < 20; i++) {  // Cap at 20 to limit response size
    JsonObject net = arr.add<JsonObject>();
    net["ssid"] = WiFi.SSID(i);
    net["rssi"] = WiFi.RSSI(i);  // Signal strength in dBm (more negative = weaker)
    net["enc"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
  }
  // Free the scan results from ESP32 memory -- without this, subsequent
  // scans would fail with WIFI_SCAN_FAILED
  WiFi.scanDelete();

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// GET /api/status -- Returns live system info (polled every 5 seconds by the browser)
void WebConfig::handleGetStatus() {
  JsonDocument doc;

  // Check if the caller is authenticated. If not, return only
  // non-sensitive fields needed by the login page.
  bool authed = !auth_required();
  if (!authed && server.hasHeader("Cookie") && session_token[0] != '\0') {
    authed = server.header("Cookie").indexOf("session=" + String(session_token)) >= 0;
  }

  doc["name"] = WifiAPName;
  doc["ap"] = APRunning;

  if (!authed) {
    // Unauthenticated: return only device name and AP state
    String json;
    serializeJson(doc, json);
    server.send(200, "application/json", json);
    return;
  }

  // Authenticated: return full status
  time_t now_t = time(nullptr);
  struct tm timeinfo;
  localtime_r(&now_t, &timeinfo);
  char timebuf[20];
  strftime(timebuf, sizeof(timebuf), "%H:%M:%S", &timeinfo);
  doc["time"] = timebuf;

  if (WifiState == connected) {
    doc["wifi"] = "Connected";
    doc["ip"] = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  } else {
    doc["wifi"] = "Disconnected";
    doc["ip"] = APRunning ? WiFi.softAPIP().toString() : "--";
    doc["rssi"] = 0;
  }

  // NTP sync status from the TimeLib library
  doc["ntp"] = (timeStatus() != timeNotSet) ? "Synced" : "Not synced";

  // Human-readable uptime calculated from millis() (resets on reboot)
  unsigned long up = millis() / 1000;
  char upbuf[20];
  snprintf(upbuf, sizeof(upbuf), "%lud %luh %lum", up / 86400, (up % 86400) / 3600, (up % 3600) / 60);
  doc["uptime"] = upbuf;

  // MAC address intentionally omitted from API response for privacy --
  // the device name (which includes last 4 hex digits of MAC) is sufficient
  // for identification without exposing the full address to AP clients.
  doc["name"] = WifiAPName;
  doc["version"] = FIRMWARE_VERSION;

  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// POST /api/ntp_sync -- Forces an immediate NTP time synchronization
void WebConfig::handleNTPSync() {
  if (!validate_request()) return;
  bool ok = uclock.forceNTPSync();
  String json = "{\"ok\":";
  json += ok ? "true" : "false";
  json += ",\"msg\":\"";
  json += ok ? "NTP sync successful" : "NTP sync failed";
  json += "\"}";
  server.send(200, "application/json", json);
}

// GET /api/face?f=N -- Serves a clock face image file from SPIFFS
//
// The .clk files are a custom binary format: a 6-byte header (magic 0x4B43,
// width, height as uint16 LE) followed by raw RGB565 pixel data. The browser's
// JavaScript decodes RGB565 into an HTML canvas for preview thumbnails.
// Serves pre-generated PNG thumbnail images for clock face previews.
// Files are stored on SPIFFS as p1.png, p2.png, etc. (half-size, ~10-15KB each).
// PNG is a native browser format — no decompression or canvas decoding needed.
void WebConfig::handleFacePreview() {
  if (!check_auth()) return;
  if (!server.hasArg("f")) {
    server.send(400, "text/plain", "Missing face number");
    return;
  }
  int face = server.arg("f").toInt();
  if (face < 1 || face > tfts.NumberOfClockFaces) {
    server.send(404, "text/plain", "Face not found");
    return;
  }
  char filename[12];
  snprintf(filename, sizeof(filename), "/p%d.png", face);

  fs::File f = SPIFFS.open(filename, "r");
  if (!f) {
    server.send(404, "text/plain", "Preview not found");
    return;
  }

  size_t sz = f.size();
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.setContentLength(sz);
  server.send(200, "image/png", "");

  uint8_t buf[512];
  while (f.available()) {
    size_t n = f.read(buf, sizeof(buf));
    server.client().write(buf, n);
  }
  f.close();
}

// POST /api/factory_reset -- Erases all settings and reboots
//
// The careful sequencing here ensures the HTTP response reaches the
// browser before the ESP32 reboots. Without the flush/stop/delay chain,
// the response would be lost and the browser would show a connection error.
// POST /api/preview -- Applies display settings to hardware immediately
// without saving to NVS. Called by the UI on every slider/picker change
// so the user sees the effect in real-time. The Save button still
// persists to NVS separately.
void WebConfig::handlePreview() {
  if (!validate_request()) return;
  if (!server.hasArg("plain")) {
    server.send(400, "application/json", "{\"msg\":\"No data\"}");
    return;
  }
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "application/json", "{\"msg\":\"Invalid JSON\"}");
    return;
  }

  bool redraw_all = false;

  // Apply timezone change immediately via POSIX setenv/tzset
  if (doc.containsKey("tz")) {
    const char *new_tz = doc["tz"];
    if (!new_tz) new_tz = "";  // Guard against JSON null -> nullptr
    for (int i = 0; i < tz_entry_count; i++) {
      if (strcmp(new_tz, tz_entries[i].posix_tz) == 0) {
        strncpy(config->uclock.tz_string, new_tz, sizeof(config->uclock.tz_string) - 1);
        config->uclock.tz_string[sizeof(config->uclock.tz_string) - 1] = '\0';
        setenv("TZ", config->uclock.tz_string, 1);
        tzset();
        redraw_all = true;
        break;
      }
    }
  }

  // Apply 12/24 hour format and leading zero immediately
  if (doc.containsKey("twelve_hour")) {
    config->uclock.twelve_hour = doc["twelve_hour"];
    redraw_all = true;
  }
  if (doc.containsKey("blank_zero")) {
    config->uclock.blank_hours_zero = doc["blank_zero"];
    redraw_all = true;
  }

  // Apply display brightness immediately (minimum 1 to prevent blank display)
  if (doc.containsKey("day_tft")) {
    uint8_t v = doc["day_tft"];
    if (v < 1) v = 1;
    config->uclock.day_tft_intensity = v;
    tfts.dimming = v;
    tfts.InvalidateImageInBuffer();
    redraw_all = true;
  }

  // Apply hue shift immediately
  if (doc.containsKey("hue_shift")) {
    uint16_t hs = doc["hue_shift"];
    if (hs < 360) {
      config->uclock.hue_shift = hs;
      tfts.hue_shift = hs;
      tfts.computeHueMatrix(hs);
      tfts.InvalidateImageInBuffer();
      redraw_all = true;
    }
  }

  // Apply clock face selection immediately
  if (doc.containsKey("graphic")) {
    int8_t g = doc["graphic"];
    if (g >= 1 && g <= tfts.NumberOfClockFaces) {
      config->uclock.selected_graphic = g;
      tfts.current_graphic = g;
      tfts.InvalidateImageInBuffer();
      redraw_all = true;
    }
  }

  // Force all six displays to reload with the new graphic/brightness
  if (redraw_all) {
    for (uint8_t d = 0; d < NUM_DIGITS; d++) {
      tfts.setDigit(d, tfts.getDigit(d), TFTs::force);
    }
  }

  // Apply LED brightness immediately
  if (doc.containsKey("led_intensity")) {
    uint8_t li = doc["led_intensity"];
    if (li <= 7) {
      config->backlights.intensity = li;
      backlights.setIntensity(li);
    }
  }

  // Apply LED pattern immediately
  if (doc.containsKey("led_pattern")) {
    uint8_t lp = doc["led_pattern"];
    if (lp < 6) config->backlights.pattern = lp;
  }

  // Apply LED colour immediately
  if (doc.containsKey("led_color")) {
    const char *lc = doc["led_color"];
    if (lc && strlen(lc) == 7 && lc[0] == '#') {
      config->backlights.fixed_color = strtoul(lc + 1, NULL, 16);
    } else {
      config->backlights.fixed_color = 0;
    }
  }

  backlights.forceRefresh();

  // Check if the previewed settings put the clock into night mode
  // and warn the user (timezone changes can shift the local hour
  // into the night mode window, dimming or blanking the display)
  bool in_night = false;
  if (config->uclock.night_mode_enabled) {
    time_t sys = time(nullptr);
    struct tm ltm;
    localtime_r(&sys, &ltm);
    uint8_t h = ltm.tm_hour;
    uint8_t day = config->uclock.day_time;
    uint8_t night = config->uclock.night_time;
    if (day < night) {
      in_night = (h < day) || (h >= night);
    } else {
      in_night = (h >= night) && (h < day);
    }
  }

  if (in_night) {
    server.send(200, "application/json", "{\"ok\":true,\"night\":true}");
  } else {
    server.send(200, "application/json", "{\"ok\":true}");
  }
}

// POST /api/ota — completion handler. Runs after all upload chunks are processed.
// Auth was already checked in handleOTAUploadData at UPLOAD_FILE_START.
// Handles both firmware and SPIFFS uploads (auto-detected from magic byte).
void WebConfig::handleOTAUpload() {
  if (!ota_authenticated) {
    server.send(403, "application/json", "{\"msg\":\"Session expired — please reload and log in again\"}");
    return;
  }
  if (ota_success) {
    if (ota_is_spiffs) {
      // SPIFFS update: just reboot to remount with new images.
      // No config wipe — SPIFFS only contains clock face graphics.
      Serial.println("OTA: SPIFFS update complete");
      server.send(200, "application/json", "{\"msg\":\"Clock faces updated. Rebooting...\",\"ok\":true}");
    } else {
      // Firmware update: wipe all stored config (WiFi credentials, dashboard
      // password, etc.) before booting the new firmware. This prevents a
      // malicious firmware image from inheriting the user's network access.
      storedConfig->factory_reset();
      Serial.println("OTA: config wiped for security");
      server.send(200, "application/json", "{\"msg\":\"Firmware updated. Rebooting...\",\"ok\":true}");
    }
    server.client().flush();
    delay(500);
    ESP.restart();
  } else {
    server.send(500, "application/json", "{\"msg\":\"Update failed\"}");
  }
}

// Called by the WebServer for each chunk of the uploaded file.
// Auto-detects firmware vs SPIFFS from the first byte:
//   0xE9 = ESP32 firmware → flash to app partition (U_FLASH)
//   anything else = SPIFFS image → flash to SPIFFS partition (U_SPIFFS)
// Streams directly to flash — no need to buffer the entire file in RAM.
// Auth and CSRF are checked on the first chunk (UPLOAD_FILE_START).
// Do NOT call server.send() in this handler — the completion handler
// (handleOTAUpload) handles the HTTP response.
void WebConfig::handleOTAUploadData() {
  HTTPUpload &upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    ota_success = false;
    ota_is_spiffs = false;
    ota_bytes_written = 0;
    // Check auth and CSRF. Do not send a response here — the completion
    // handler will send 403 if ota_authenticated is false.
    if (!check_auth()) {
      ota_authenticated = false;
      return;
    }
    if (!validate_csrf_token()) {
      ota_authenticated = false;
      return;
    }
    ota_authenticated = true;
    Serial.printf("OTA: receiving %s\n", upload.filename.c_str());
    // Update.begin() is deferred to the first WRITE chunk so we can
    // inspect the magic byte and choose firmware vs SPIFFS mode.

  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!ota_authenticated) return;

    // First chunk: detect image type from magic byte and begin the update
    if (ota_bytes_written == 0 && upload.currentSize > 0) {
      if (upload.buf[0] == 0xE9) {
        // ESP32 firmware image
        ota_is_spiffs = false;
        Serial.println("OTA: detected firmware image");

        // Validate file size from Content-Length if available.
        // upload.totalSize is not available until UPLOAD_FILE_END, but
        // we enforce the cap per-chunk below.
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
          Serial.println("OTA: Update.begin(U_FLASH) failed");
          Update.printError(Serial);
          ota_success = false;
          return;
        }
      } else {
        // Not firmware — treat as SPIFFS image
        ota_is_spiffs = true;
        Serial.println("OTA: detected SPIFFS image");

        // Look up the SPIFFS partition to get its exact size for validation
        const esp_partition_t *spiffs_part = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (!spiffs_part) {
          Serial.println("OTA: no SPIFFS partition found");
          ota_success = false;
          return;
        }
        Serial.printf("OTA: SPIFFS partition size: 0x%X (%u bytes)\n",
                       spiffs_part->size, spiffs_part->size);

        if (!Update.begin(spiffs_part->size, U_SPIFFS)) {
          Serial.println("OTA: Update.begin(U_SPIFFS) failed");
          Update.printError(Serial);
          ota_success = false;
          return;
        }
      }
    }

    // Enforce partition size cap
    size_t max_size = ota_is_spiffs ? 0x120000 : 0x160000;
    ota_bytes_written += upload.currentSize;
    if (ota_bytes_written > max_size) {
      Serial.printf("OTA: image too large for %s partition (%u > 0x%X)\n",
                     ota_is_spiffs ? "SPIFFS" : "app", ota_bytes_written, max_size);
      Update.abort();
      ota_success = false;
      return;
    }

    if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.println("OTA: write failed");
      Update.printError(Serial);
      ota_success = false;
    }

  } else if (upload.status == UPLOAD_FILE_END) {
    if (!ota_authenticated) return;
    if (Update.end(true)) {
      Serial.printf("OTA: %s update success, %u bytes\n",
                     ota_is_spiffs ? "SPIFFS" : "firmware", upload.totalSize);
      ota_success = true;
    } else {
      Serial.println("OTA: finalize failed");
      Update.printError(Serial);
      ota_success = false;
    }

  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    Update.abort();
    Serial.println("OTA: aborted");
    ota_success = false;
  }
}

void WebConfig::handleFactoryReset() {
  if (!validate_request()) return;
  Serial.println("Factory reset requested");
  server.send(200, "application/json", "{\"msg\":\"Factory reset complete. Rebooting...\",\"ok\":true}");
  server.client().flush();   // Ensure TCP send buffer is drained
  delay(200);
  server.client().stop();    // Close the TCP connection cleanly
  delay(300);
  storedConfig->factory_reset();  // Zeroes the config struct and clears NVS
  delay(500);
  ESP.restart();  // Full hardware reset -- will boot into first-time setup
}

// Fallback handler for unregistered URLs.
// In AP mode: redirect everything to the config portal (captive portal behavior).
// In station mode: return a standard 404.
void WebConfig::handleNotFound() {
  if (APRunning) {
    // 192.168.4.1 is the ESP32's default softAP IP address
    server.sendHeader("Location", "http://192.168.4.1/");
    server.send(302, "text/plain", "Redirecting to config portal");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}
