#include "web_dashboard.h"
#include <WebServer.h>

static WebServer server(80);

static const uint8_t MAX_FAILED_PINS = 5;
static const uint32_t LOCKOUT_MS = 60000;

static const char PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Auto Mixer</title>
<style>
:root{--o2:#2be82b;--he:#2e7bff;--dim:#8a93a6;--panel:#0e1426;--warn:#ffb020}
*{box-sizing:border-box}
body{margin:0;background:#000;color:#e8ecf2;font-family:system-ui,-apple-system,sans-serif}
header{background:#121829;padding:12px 16px;display:flex;align-items:center;gap:12px}
header h1{margin:0;font-size:22px;color:#9aa4dc;flex:1;text-align:center}
header .brand{color:#3d63ff;font-size:14px}
#badge{color:var(--warn);font-weight:700;font-size:14px;visibility:hidden}
main{max-width:720px;margin:0 auto;padding:16px}
.notice{padding:10px 12px;border-radius:8px;margin-bottom:14px;background:var(--panel);color:var(--dim)}
.notice.armed{color:var(--warn);border:1px solid var(--warn)}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:14px}
.col{background:var(--panel);border-radius:10px;padding:14px;text-align:center}
.col h2{margin:0;font-size:24px}
.big{font-size:52px;font-variant-numeric:tabular-nums;margin:4px 0}
.small{color:var(--dim);font-size:14px}
.o2 h2,.o2 .big{color:var(--o2)} .he h2,.he .big{color:var(--he)}
.pr{grid-column:1/-1;display:flex;justify-content:space-around;padding:12px}
.pr h3{margin:0;font-size:18px;color:#9aa4dc}
.pr .big{font-size:40px}
.solo{grid-column:1/-1}
[hidden]{display:none!important}
.ctl{display:flex;justify-content:center;align-items:center;gap:8px;margin-top:12px}
.ctl button{width:44px;height:44px;font-size:22px;border-radius:8px;border:0;background:#1c2c63;color:#fff}
input::-webkit-outer-spin-button,input::-webkit-inner-spin-button{-webkit-appearance:none;margin:0}
input{-moz-appearance:textfield;appearance:textfield}
.ctl input{width:70px;height:44px;font-size:22px;text-align:center;border-radius:8px;border:1px solid #2a3450;background:#000;color:#fff}
.set{margin-top:10px;width:100%;height:40px;border:0;border-radius:8px;font-size:16px;color:#fff}
.o2 .set{background:#1d6b1d} .he .set{background:#1d4a99}
button:disabled,input:disabled{opacity:.35}
.row{display:flex;gap:10px;align-items:center;margin-top:14px;flex-wrap:wrap}
.row input{height:40px;font-size:18px;width:130px;border-radius:8px;border:1px solid #2a3450;background:#000;color:#fff;padding:0 10px}
#msg{min-height:20px;margin-top:10px;color:var(--dim)}
#msg.err{color:#ff5566}
.status{margin-top:14px;display:flex;justify-content:space-between;color:var(--dim)}
#estop{width:100%;height:64px;margin-bottom:14px;border:0;border-radius:10px;background:#d32f2f;color:#fff;font-size:24px;font-weight:700}
#estop.on{background:#7a1a1a;font-size:18px;font-weight:400}
</style></head><body>
<header><span class="brand">DarkWaterDiving.com</span><h1>Auto Mixer</h1><span id="badge">REMOTE</span></header>
<main>
<div id="notice" class="notice">Connecting...</div>
<button id="estop">EMERGENCY STOP</button>
<div class="cols">
 <div class="col o2" id="o2card"><h2>Oxygen</h2><div class="big" id="o2pct">--.-</div>
  <div class="small" id="o2mv">S2: -- mV</div><div class="small" id="o2info"></div>
  <div class="ctl"><button data-g="o2" data-d="-1">-</button><input id="o2t" type="text" inputmode="numeric" maxlength="3" autocomplete="off">
  <button data-g="o2" data-d="1">+</button></div><button class="set" data-set="o2">Set O2 target</button></div>
 <div class="col he" id="hecard"><h2>Helium</h2><div class="big" id="hepct">--.-</div>
  <div class="small" id="hemv">S1: -- mV</div><div class="small" id="heinfo"></div>
  <div class="ctl"><button data-g="he" data-d="-1">-</button><input id="het" type="text" inputmode="numeric" maxlength="3" autocomplete="off">
  <button data-g="he" data-d="1">+</button></div><button class="set" data-set="he">Set He target</button></div>
 <div class="col pr" id="prcard"><div><h3>Bank</h3><div class="big" id="bank">----</div><div class="small punit">PSI</div></div>
  <div><h3>Fill</h3><div class="big" id="fill">----</div><div class="small punit">PSI</div></div></div>
</div>
<div class="row"><label for="pin">PIN</label><input id="pin" type="password" inputmode="numeric" maxlength="6" placeholder="6 digits"></div>
<div id="msg"></div>
<div class="status"><span id="comp"></span><span id="sys"></span></div>
</main>
<script>
const $=id=>document.getElementById(id);
let armed=false, editing={o2:false,he:false};
function fmt(v){return v==null?'--.-':v.toFixed(1)}
function setControls(on){document.querySelectorAll('.ctl button,.ctl input,.set,#pin').forEach(e=>e.disabled=!on)}
async function poll(){
 try{
  const s=await (await fetch('/api/state')).json();
  armed=s.remote;
  $('badge').style.visibility=armed?'visible':'hidden';
  const n=$('notice');
  if(s.locked){n.textContent='Too many wrong PINs - remote changes locked for a minute.';n.className='notice armed'}
  else if(armed){n.textContent='Remote control is ARMED at the unit. Changes need the PIN from its Setup > Network page.';n.className='notice armed'}
  else{n.textContent='View only. To make changes, turn on Remote control under Setup > Network on the unit.';n.className='notice'}
  setControls(armed&&!s.locked);
  // Follow the unit's Blender Setup: Nitrox only hides helium, no transducers hides
  // pressure, and pressure comes already converted to the unit's units.
  $('hecard').hidden=!s.he_on; $('o2card').classList.toggle('solo',!s.he_on);
  $('prcard').hidden=!s.pressure;
  document.querySelectorAll('.punit').forEach(e=>e.textContent=s.punit);
  for(const g of ['o2','he']){
   const d=s[g];
   $(g+'pct').textContent=s.sensors?fmt(d.pct):'--.-';
   $(g+'mv').textContent=(g=='o2'?'S2: ':'S1: ')+(s.sensors&&d.mv!=null?d.mv.toFixed(1):'--')+' mV';
   $(g+'info').textContent='target '+d.target.toFixed(0)+'%  valve '+d.valve.toFixed(0)+'%';
   if(!editing[g]) $(g+'t').value=d.target.toFixed(0);
  }
  $('bank').textContent=s.bank==null?'----':s.bank.toFixed(0);
  $('fill').textContent=s.fill==null?'----':s.fill.toFixed(0);
  $('comp').textContent='Compressor: '+s.compressor;
  $('sys').textContent=s.status;
  $('estop').className=s.estop?'on':'';
  $('estop').textContent=s.estop?'STOPPED - resume at the unit':'EMERGENCY STOP';
 }catch(e){$('notice').textContent='Lost connection to the unit - retrying...';$('notice').className='notice'}
}
function msg(t,err){$('msg').textContent=t;$('msg').className=err?'err':''}
document.querySelectorAll('[data-d]').forEach(b=>b.onclick=()=>{
 const g=b.dataset.g,i=$(g+'t');editing[g]=true;
 i.value=clampPct((parseInt(i.value)||0)+parseInt(b.dataset.d))});
const clampPct=v=>Math.max(0,Math.min(100,parseInt(v)||0));
['o2t','het'].forEach(id=>$(id).oninput=()=>{
 const i=$(id);i.value=i.value.replace(/[^0-9]/g,'');
 if(i.value!=='')i.value=clampPct(i.value);editing[id.slice(0,2)]=true});
document.querySelectorAll('[data-set]').forEach(b=>b.onclick=async()=>{
 const g=b.dataset.set,v=clampPct($(g+'t').value);$(g+'t').value=v;
 const body=new URLSearchParams({gas:g,value:v,pin:$('pin').value});
 const r=await fetch('/api/target',{method:'POST',body});
 const j=await r.json().catch(()=>({}));
 if(r.ok){msg((g=='o2'?'O2':'He')+' target set to '+v+'%');editing[g]=false}
 else msg(j.error||('failed ('+r.status+')'),true);
 poll()});
// No PIN and no arming: anyone who can see the page can stop the unit, never restart it.
$('estop').onclick=async()=>{
 try{const r=await fetch('/api/estop',{method:'POST'});
  msg(r.ok?'Emergency stop sent - valves closed.':'Emergency stop failed ('+r.status+')',!r.ok)}
 catch(e){msg('Emergency stop failed - no connection to the unit',true)}
 poll()};
poll();setInterval(poll,500);
</script></body></html>)HTML";

// Commands from the web task to the main loop. The E-STOP is a flag of its own rather
// than a queue entry, so it can never be dropped by a full queue.
enum CommandType : uint8_t { CMD_O2_TARGET, CMD_HE_TARGET };
struct Command {
    CommandType type;
    uint8_t value;
};
static QueueHandle_t commands = NULL;
static volatile bool estop_requested = false;

static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static WebSnapshot shared_snapshot;

static const uint32_t PUBLISH_INTERVAL_MS = 100;
static const uint32_t WEB_TASK_STACK = 8192;

WebDashboard::WebDashboard(OxygenSensor* s, ValveController* v, FillStationGUI* g)
    : sensors(s), valves(v), gui(g), last_publish_ms(0), failed_pins(0), locked_until_ms(0) {
}

void WebDashboard::begin() {
    commands = xQueueCreate(8, sizeof(Command));
    publish();

    server.on("/", HTTP_GET, []() {
        // No caching, so a reflashed page shows up without a hard refresh.
        server.sendHeader("Cache-Control", "no-store");
        server.send(200, "text/html", PAGE);
    });
    server.on("/api/state", HTTP_GET, [this]() { handleState(); });
    server.on("/api/target", HTTP_POST, [this]() { handleTarget(); });
    server.on("/api/estop", HTTP_POST, [this]() { handleEstop(); });
    server.onNotFound([]() { server.send(404, "text/plain", "not found"); });
    server.begin();

    // Core 0 with the WiFi stack, leaving core 1 to the control loop and LVGL.
    xTaskCreatePinnedToCore(task, "web", WEB_TASK_STACK, this, 1, NULL, 0);
    Serial.println("Web dashboard listening on port 80");
}

void WebDashboard::task(void* arg) {
    for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void WebDashboard::loop() {
    if (estop_requested) {
        estop_requested = false;
        gui->triggerEmergencyStop();
    }

    Command cmd;
    while (commands && xQueueReceive(commands, &cmd, 0) == pdTRUE) {
        // Re-checked here: the switch may have been turned off since the request passed.
        if (!gui->remoteControlEnabled()) continue;
        if (cmd.type == CMD_O2_TARGET) gui->setOxygenTarget(cmd.value);
        else if (gui->heliumEnabled()) gui->setHeliumTarget(cmd.value);
    }

    if (millis() - last_publish_ms >= PUBLISH_INTERVAL_MS) publish();
}

void WebDashboard::publish() {
    last_publish_ms = millis();
    WebSnapshot s;
    s.sensors = sensors->isAvailable();
    s.remote = gui->remoteControlEnabled();
    s.estop = gui->emergencyStopped();
    s.he_on = gui->heliumEnabled();
    s.pressure = gui->transducersFitted();
    s.bar = gui->pressureInBar();
    s.o2_pct = sensors->getOxygenPercent();
    s.o2_mv = sensors->getOxygenMillivolts();
    s.o2_target = gui->getOxygenTarget();
    s.o2_valve = valves->getO2ValvePosition();
    s.he_pct = sensors->getHeliumPercent();
    s.he_mv = sensors->getHeliumMillivolts();
    s.he_target = gui->getHeliumTarget();
    s.he_valve = valves->getHeValvePosition();
    const float scale = s.bar ? 1.0f / PSI_PER_BAR : 1.0f;
    s.bank = sensors->getBankPSI() * scale;
    s.fill = sensors->getFillPSI() * scale;
    strlcpy(s.pin, gui->remotePin(), sizeof(s.pin));
    strlcpy(s.compressor, gui->compressorText(), sizeof(s.compressor));
    strlcpy(s.status, gui->systemStatusText(), sizeof(s.status));

    portENTER_CRITICAL(&snapshot_lock);
    shared_snapshot = s;
    portEXIT_CRITICAL(&snapshot_lock);
}

WebSnapshot WebDashboard::snapshot() {
    portENTER_CRITICAL(&snapshot_lock);
    const WebSnapshot s = shared_snapshot;
    portEXIT_CRITICAL(&snapshot_lock);
    return s;
}

bool WebDashboard::locked() {
    if (locked_until_ms == 0) return false;
    if ((int32_t)(millis() - locked_until_ms) >= 0) {
        locked_until_ms = 0;
        failed_pins = 0;
        return false;
    }
    return true;
}

// JSON has no NaN, so an unavailable reading goes out as null.
static void num_json(char* out, size_t len, float v, const char* fmt) {
    if (isnan(v)) snprintf(out, len, "null");
    else snprintf(out, len, fmt, v);
}

void WebDashboard::handleState() {
    const WebSnapshot s = snapshot();
    char bank[16], fill[16], o2[16], he[16], o2mv[16], hemv[16];
    num_json(bank, sizeof(bank), s.bank, "%.0f");
    num_json(fill, sizeof(fill), s.fill, "%.0f");
    num_json(o2, sizeof(o2), s.o2_pct, "%.2f");
    num_json(he, sizeof(he), s.he_pct, "%.2f");
    num_json(o2mv, sizeof(o2mv), s.o2_mv, "%.2f");
    num_json(hemv, sizeof(hemv), s.he_mv, "%.2f");

    char json[700];
    snprintf(json, sizeof(json),
             "{\"sensors\":%s,\"remote\":%s,\"locked\":%s,\"estop\":%s,"
             "\"o2\":{\"pct\":%s,\"mv\":%s,\"target\":%.1f,\"valve\":%.1f},"
             "\"he\":{\"pct\":%s,\"mv\":%s,\"target\":%.1f,\"valve\":%.1f},"
             "\"bank\":%s,\"fill\":%s,\"punit\":\"%s\",\"he_on\":%s,\"pressure\":%s,"
             "\"compressor\":\"%s\",\"status\":\"%s\"}",
             s.sensors ? "true" : "false", s.remote ? "true" : "false",
             locked() ? "true" : "false", s.estop ? "true" : "false",
             o2, o2mv, s.o2_target, s.o2_valve,
             he, hemv, s.he_target, s.he_valve, bank, fill,
             s.bar ? "BAR" : "PSI", s.he_on ? "true" : "false", s.pressure ? "true" : "false",
             s.compressor, s.status);
    server.send(200, "application/json", json);
}

// Deliberately needs neither the PIN nor remote control armed: stopping is always safe,
// and a PIN prompt is the last thing wanted in a hurry. Resuming is only possible at the
// unit.
void WebDashboard::handleEstop() {
    estop_requested = true;
    Serial.printf("web: EMERGENCY STOP from %s\n", server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}

void WebDashboard::handleTarget() {
    const WebSnapshot s = snapshot();
    if (!s.remote) {
        server.send(403, "application/json",
                    "{\"error\":\"remote control is off at the unit\"}");
        return;
    }
    if (locked()) {
        server.send(429, "application/json",
                    "{\"error\":\"too many wrong PINs - try again in a minute\"}");
        return;
    }
    if (server.arg("pin") != s.pin) {
        if (++failed_pins >= MAX_FAILED_PINS) locked_until_ms = millis() + LOCKOUT_MS;
        Serial.printf("web: wrong PIN from %s (%u/%u)\n",
                      server.client().remoteIP().toString().c_str(), failed_pins,
                      MAX_FAILED_PINS);
        server.send(401, "application/json", "{\"error\":\"wrong PIN\"}");
        return;
    }
    failed_pins = 0;

    const String gas = server.arg("gas");
    const String raw = server.arg("value");
    const long value = raw.toInt();
    if ((gas != "o2" && gas != "he") || raw.length() == 0 || value < 0 || value > 100) {
        server.send(400, "application/json",
                    "{\"error\":\"expected gas=o2|he and a value from 0 to 100\"}");
        return;
    }
    const bool is_he = gas == "he";
    if (is_he && !s.he_on) {
        server.send(409, "application/json",
                    "{\"error\":\"the unit is set to Nitrox only\"}");
        return;
    }
    const float other = is_he ? s.o2_target : s.he_target;
    if (value + other > 100.0f) {
        server.send(409, "application/json",
                    "{\"error\":\"O2 + He can't be more than 100%\"}");
        return;
    }

    const Command cmd = {is_he ? CMD_HE_TARGET : CMD_O2_TARGET, (uint8_t)value};
    if (xQueueSend(commands, &cmd, 0) != pdTRUE) {
        server.send(503, "application/json", "{\"error\":\"busy - try again\"}");
        return;
    }
    Serial.printf("web: %s target set to %ld%% by %s\n", gas.c_str(), value,
                  server.client().remoteIP().toString().c_str());
    server.send(200, "application/json", "{\"ok\":true}");
}
