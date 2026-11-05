const sensors={
  coolant:{name:'COOLANT TEMP',unit:'F',value:196,min:0,max:300,warning:{enabled:true,condition:'above',threshold:300,message:'ENGINE OVERHEAT'}},
  oil:{name:'OIL TEMP',unit:'F',value:209,min:0,max:300,warning:{enabled:true,condition:'above',threshold:300,message:'OIL OVERHEAT'}},
  map:{name:'MANIFOLD MAP',unit:'kPa',value:96,min:0,max:250,warning:{enabled:true,condition:'above',threshold:220,message:'MANIFOLD DANGER'}},
  battery:{name:'BATTERY',unit:'V',value:14.1,min:9,max:16,warning:{enabled:true,condition:'below',threshold:11.5,message:'CHARGING DANGER'}},
  fuel:{name:'FUEL LEVEL',unit:'%',value:64,min:0,max:100,warning:{enabled:true,condition:'below',threshold:10,message:'LOW FUEL'}},
  rpm:{name:'ENGINE RPM',unit:'RPM',value:2640,min:0,max:8000,warning:{enabled:true,condition:'above',threshold:7000,message:'HIGH RPM'}},
  speed:{name:'VEHICLE SPEED',unit:'MPH',value:48,min:0,max:160,warning:{enabled:false,condition:'above',threshold:100,message:'HIGH SPEED'}},
  load:{name:'ENGINE LOAD',unit:'%',value:44,min:0,max:100,warning:{enabled:false,condition:'above',threshold:95,message:'HIGH LOAD'}},
  throttle:{name:'THROTTLE',unit:'%',value:26,min:0,max:100,warning:{enabled:false,condition:'above',threshold:90,message:'THROTTLE HIGH'}},
  intake:{name:'INTAKE TEMP',unit:'F',value:91,min:0,max:220,warning:{enabled:true,condition:'above',threshold:200,message:'INTAKE AIR DANGER'}},
  maf:{name:'AIRFLOW',unit:'lb/min',value:8.4,min:0,max:30,warning:{enabled:false,condition:'above',threshold:28,message:'AIRFLOW HIGH'}},
  rail:{name:'FUEL RAIL',unit:'PSI',value:1640,min:0,max:3000,warning:{enabled:true,condition:'below',threshold:300,message:'FUEL PRESSURE LOW'}},
  afr:{name:'CMD AIR/FUEL',unit:'AFR',value:14.7,min:8,max:20,warning:{enabled:false,condition:'above',threshold:17,message:'LEAN CONDITION'}},
  timing:{name:'IGN TIMING',unit:'deg',value:18.5,min:-20,max:60,warning:{enabled:false,condition:'below',threshold:-5,message:'TIMING RETARD'}},
  torque:{name:'ENGINE TORQUE',unit:'%',value:47,min:0,max:100,warning:{enabled:false,condition:'above',threshold:95,message:'TORQUE LIMIT'}},
  obd_replies:{name:'OBD REPLIES',unit:'',value:3842,min:0,max:5000,warning:{enabled:false,condition:'below',threshold:1,message:'NO OBD REPLIES'}}
};
const defaults={name:'My OBD Dashboard',slotCount:6,layout:'grid',decimals:'auto',brightness:80,colors:{background:'#d2d0c8',text:'#694f12',value:'#d79c18',warning:'#ef3d32'},units:{temp:'F',speed:'MPH',pressure:'KPA',airflow:'LBMIN'},slots:['coolant','oil','map','battery','fuel','rpm'].map(sensor=>({sensor,style:'number',showUnit:true,log:true,warning:{...sensors[sensor].warning}}))};
let config=structuredClone(defaults),selected=0,warningTest=false,dragFrom=null;
const $=id=>document.getElementById(id),dashboard=$('dashboard');
const escapeHtml=text=>String(text).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
Object.entries(sensors).forEach(([id,s])=>$('sensorSelect').add(new Option(s.name,id)));
function unitFor(id){const s=sensors[id];if(['coolant','oil','intake'].includes(id))return config.units.temp==='C'?'°C':'°F';if(id==='speed')return config.units.speed==='KMH'?'km/h':'MPH';if(['map','rail'].includes(id))return config.units.pressure==='PSI'?'PSI':config.units.pressure==='BAR'?'bar':'kPa';if(id==='maf')return config.units.airflow==='GS'?'g/s':'lb/min';return s.unit}
function convert(id,v){if(['coolant','oil','intake'].includes(id)&&config.units.temp==='C')return(v-32)/1.8;if(id==='speed'&&config.units.speed==='KMH')return v*1.60934;if(id==='map'){if(config.units.pressure==='PSI')return v*.145038;if(config.units.pressure==='BAR')return v/100}if(id==='rail'){if(config.units.pressure==='KPA')return v*6.89476;if(config.units.pressure==='BAR')return v*.0689476}if(id==='maf'&&config.units.airflow==='GS')return v*7.55987;return v}
function decimals(id){if(config.decimals!=='auto')return Number(config.decimals);return['maf','speed','battery','afr','timing'].includes(id)?1:0}
function resizeSlots(count){const choices=['coolant','oil','map','battery','fuel','rpm','speed','intake'];config.slotCount=count;dashboard.style.setProperty('--slot-count',count);while(config.slots.length<count){const sensor=choices[config.slots.length%choices.length];config.slots.push({sensor,style:'number',showUnit:true,log:true,warning:{...sensors[sensor].warning}})}config.slots=config.slots.slice(0,count);if(selected>=count)selected=count-1}
function render(){dashboard.dataset.slots=config.slotCount;dashboard.style.setProperty('--slot-count',config.slotCount);dashboard.style.setProperty('--hero-rows',Math.ceil((config.slotCount-1)/2));dashboard.style.filter=`brightness(${config.brightness/100})`;dashboard.className=`dashboard ${config.layout}`;if(config.layout==='grid')dashboard.style.gridTemplateRows=`repeat(${config.slotCount/2},1fr)`;else dashboard.style.gridTemplateRows='';dashboard.style.setProperty('--dash-bg',config.colors.background);dashboard.style.setProperty('--dash-value',config.colors.value);dashboard.style.setProperty('--dash-warning',config.colors.warning);dashboard.style.background=config.colors.background;dashboard.style.color=config.colors.text;dashboard.innerHTML='';config.slots.forEach((slot,i)=>{const s=sensors[slot.sensor],v=convert(slot.sensor,s.value),warn=warningTest&&slot.warning.enabled;const tile=document.createElement('article');tile.className=`tile ${i===selected?'selected':''} ${warn?'warning':''} ${slot.style==='compact'?'compact':''}`;tile.draggable=true;tile.tabIndex=0;tile.setAttribute("role","button");tile.setAttribute("aria-label",`Slot ${i+1}: ${s.name}`);tile.setAttribute("aria-pressed",String(i===selected));tile.onkeydown=e=>{if(e.key==="Enter"||e.key===" "){e.preventDefault();tile.click()}};tile.dataset.index=i;const pct=Math.max(0,Math.min(100,(s.value-s.min)/(s.max-s.min)*100));tile.innerHTML=`<div class="tile-label">${s.name}</div><div class="tile-value">${v.toFixed(decimals(slot.sensor))}${slot.showUnit?`<span class="tile-unit">${unitFor(slot.sensor)}</span>`:''}</div>${slot.style==='bar'||config.layout==='bars'?`<div class="tile-bar"><span style="width:${pct}%"></span></div>`:''}${warn?`<div class="tile-message">${escapeHtml(slot.warning.message)}</div>`:''}`;tile.onclick=()=>{selected=i;syncInspector();render()};tile.ondragstart=e=>{dragFrom=i;e.dataTransfer?.setData("text/plain",String(i));if(e.dataTransfer)e.dataTransfer.effectAllowed="move"};tile.ondragend=()=>{dragFrom=null};tile.ondragover=e=>e.preventDefault();tile.ondrop=e=>{e.preventDefault();if(dragFrom===null)return;[config.slots[dragFrom],config.slots[i]]=[config.slots[i],config.slots[dragFrom]];selected=i;dragFrom=null;syncInspector();render()};dashboard.append(tile)});updateSummary()}
function syncAll(){
  $('profileName').value=config.name;$('slotCount').value=config.slotCount;$('layout').value=config.layout;$('decimals').value=config.decimals;$('brightness').value=config.brightness;$('brightnessOut').value=config.brightness+'%';
  $('background').value=config.colors.background;$('textColor').value=config.colors.text;$('valueColor').value=config.colors.value;$('warningColor').value=config.colors.warning;
  $('tempUnit').value=config.units.temp;$('speedUnit').value=config.units.speed;$('pressureUnit').value=config.units.pressure;$('airflowUnit').value=config.units.airflow;syncInspector();render()
}
function syncInspector(){$("moveEarlier").disabled=selected===0;$("moveLater").disabled=selected===config.slots.length-1;const slot=config.slots[selected];$('tileNumber').textContent=`SLOT ${selected+1}`;$('sensorSelect').value=slot.sensor;$('widgetStyle').value=slot.style;$('showUnit').checked=slot.showUnit;$('logSensor').checked=slot.log;$('warningEnabled').checked=slot.warning.enabled;$('warningCondition').value=slot.warning.condition;$('warningThreshold').value=slot.warning.threshold;$('warningMessage').value=slot.warning.message}
function updateSummary(){$('activeCount').textContent=config.slots.length;$('loggedCount').textContent=config.slots.filter(x=>x.log).length;$('warningCount').textContent=config.slots.filter(x=>x.warning.enabled).length}
function toast(msg){const t=$('toast');t.textContent=msg;t.classList.add('show');clearTimeout(toast.timer);toast.timer=setTimeout(()=>t.classList.remove('show'),1800)}
function download(name,text,type='application/json'){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text],{type}));a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),500)}
function rgb565(hex){const n=parseInt(hex.slice(1),16),r=(n>>16)&255,g=(n>>8)&255,b=n&255;return'0x'+(((r>>3)<<11)|((g>>2)<<5)|(b>>3)).toString(16).toUpperCase().padStart(4,'0')}
function enumName(id){return'SENSOR_'+id.toUpperCase()}
function cText(text){return String(text).replace(/[^A-Za-z0-9 %/+:_.-]/g,'').slice(0,26)}
function configNamespace(){let h=2166136261,text=JSON.stringify(config);for(let i=0;i<text.length;i++){h^=text.charCodeAt(i);h=Math.imul(h,16777619)}return'dash-'+(h>>>0).toString(16).padStart(8,'0')}
function slotFor(sensor){return config.slots.find(x=>x.sensor===sensor)}
function baseThreshold(sensor,slot){let v=Number(slot?.warning.threshold??sensors[sensor].warning.threshold);if(['coolant','oil','intake'].includes(sensor)&&config.units.temp==='C')v=v*1.8+32;if(sensor==='map'&&config.units.pressure==='PSI')v/=.1450377;if(sensor==='map'&&config.units.pressure==='BAR')v*=100;return Number(v.toFixed(3))}

// Generate the receiver's custom-screen renderer from the same per-slot profile.
function receiverRenderer(){
  const pad=(items,fallback)=>[...items,...Array(8-items.length).fill(fallback)].join(',');
  const styles=pad(config.slots.map(s=>s.style==='bar'?1:s.style==='compact'?2:0),0);
  const units=pad(config.slots.map(s=>s.showUnit?'true':'false'),'false');
  const warnings=pad(config.slots.map(s=>s.warning.enabled?'true':'false'),'false');
  const below=pad(config.slots.map(s=>s.warning.condition==='below'?'true':'false'),'false');
  const thresholds=pad(config.slots.map(s=>Number(s.warning.threshold).toFixed(3)+'f'),'0.0f');
  const messages=pad(config.slots.map(s=>JSON.stringify(cText(s.warning.message)||'WARNING')),'"WARNING"');
  const layout={grid:0,bars:1,hero:2}[config.layout]??0;
  return `
// Browser designer profile: grid=0, bars=1, hero=2; number=0, bar=1, compact=2.
constexpr uint8_t DESIGNER_LAYOUT=${layout};
const uint8_t designerStyles[8]={${styles}};
const bool designerUnits[8]={${units}};
const bool designerWarnings[8]={${warnings}};
const bool designerBelow[8]={${below}};
const float designerThresholds[8]={${thresholds}};
const char *designerMessages[8]={${messages}};
void designerRect(int i,int &x,int &y,int &w,int &h){
  if(DESIGNER_LAYOUT==1){x=6;w=468;h=270/settings.slotCount-4;y=42+i*(h+4);}
  else if(DESIGNER_LAYOUT==2&&i==0){x=6;y=42;w=468;h=94;}
  else if(DESIGNER_LAYOUT==2){int rows=(settings.slotCount-1+1)/2;x=6+((i-1)%2)*240;y=142+((i-1)/2)*(170/rows);w=228;h=170/rows-6;}
  else {int rows=settings.slotCount/2;x=6+(i%2)*240;y=42+(i/2)*(270/rows);w=228;h=270/rows-6;}
}
void drawBarPage(){
  tft->fillScreen(settings.background);tft->fillRect(0,0,480,35,C_MID);
  printTft("CUSTOM OBD DASHBOARD",10,9,2,settings.value);
  printTft(String(settings.slotCount)+" SLOTS",405,11,1,settings.value);
  redraw=false;
}
void updateBars(const TelemetryPacket &p){
  for(int i=0;i<settings.slotCount;i++){
    int x,y,w,h;designerRect(i,x,y,w,h);uint8_t id=settings.slots[i];
    bool online=radioAlive();uint16_t required=sensorBit(id);
    bool valid=online&&(!required||(p.validMask&required));
    float value=sensorDisplayValue(p,id);
    bool warning=valid&&designerWarnings[i]&&(designerBelow[i]?value<designerThresholds[i]:value>designerThresholds[i]);
    uint16_t bg=warning?settings.warning:settings.background;
    uint16_t fg=warning?C_BLACK:settings.text;
    tft->fillRect(x,y,w,h,bg);tft->drawRect(x,y,w,h,settings.text);
    printTft(sensorNames[id],x+6,y+4,1,fg);
    String shown=!online?"LINK LOST":!valid?"N/A":formattedSensorValue(p,id)+(designerUnits[i]?String(" ")+sensorUnit(id):String(""));
    bool bar=DESIGNER_LAYOUT==1||designerStyles[i]==1;
    int size=designerStyles[i]==2?1:(h>=80?3:h>=50?2:1);
    int16_t bx,by;uint16_t tw,th;
    do{tft->setTextSize(size);tft->getTextBounds(shown,0,0,&bx,&by,&tw,&th);if(tw<=w-12||size==1)break;size--;}while(size>0);
    int valueY=DESIGNER_LAYOUT==1?y+4:y+18;
    int valueX=DESIGNER_LAYOUT==1?x+w-6-tw:x+6;
    printTft(shown,valueX,valueY,size,warning?fg:settings.value);
    if(bar&&valid){int barY=y+h-6;int fill=(int)((w-12)*constrain(sensorValue(p,id)/sensorMaximums[id],0.0f,1.0f));tft->drawRect(x+6,barY,w-12,4,fg);if(fill>0)tft->fillRect(x+6,barY,fill,4,warning?fg:settings.value);}
    if(warning&&h>=65)printTft(designerMessages[i],x+6,y+h-20,1,fg);
  }
}
`;
}

async function completeSketch(){
  let source=await fetch('dashboard-template.ino',{cache:'no-store'}).then(r=>{if(!r.ok)throw new Error('Dashboard template could not be loaded');return r.text()});
  const padded=config.slots.map(x=>enumName(x.sensor));while(padded.length<8)padded.push('SENSOR_RPM');
  const bits={rpm:'V_RPM',load:'V_LOAD',throttle:'V_THROTTLE',torque:'V_TORQUE',coolant:'V_COOLANT',oil:'V_OIL',intake:'V_INTAKE',map:'V_MAP',maf:'V_MAF',rail:'V_RAIL',fuel:'V_FUEL',afr:'V_AFR',timing:'V_TIMING',battery:'V_BATTERY',speed:'V_SPEED'};
  const logged=[...new Set(config.slots.filter(x=>x.log&&bits[x.sensor]).map(x=>bits[x.sensor]))];
  const warnings={coolant:'W_COOLANT',oil:'W_OIL',intake:'W_INTAKE',map:'W_MAP',fuel:'W_FUEL',battery:'W_VOLTAGE'};
  const enabled=Object.entries(warnings).filter(([sensor])=>slotFor(sensor)?.warning.enabled).map(([,flag])=>flag);
  const temp=config.units.temp==='C'?'TEMP_C':'TEMP_F',speed=config.units.speed==='KMH'?'SPEED_KMH':'SPEED_MPH';
  const pressure={KPA:'PRESSURE_KPA',PSI:'PRESSURE_PSI',BAR:'PRESSURE_BAR'}[config.units.pressure];
  const airflow=config.units.airflow==='GS'?'AIRFLOW_G_S':'AIRFLOW_LB_MIN';
  const format=config.decimals==='auto'?'FORMAT_AUTO':'FORMAT_'+config.decimals;
  source=source.replace('uint16_t background=C_LIGHT;',`uint16_t background=${rgb565(config.colors.background)};`)
    .replace('uint16_t text=C_DARK_GOLD;',`uint16_t text=${rgb565(config.colors.text)};`)
    .replace('uint16_t value=C_GOLD;',`uint16_t value=${rgb565(config.colors.value)};`)
    .replace('uint16_t warning=C_RED;',`uint16_t warning=${rgb565(config.colors.warning)};`)
    .replace(/uint8_t warningMask=[^;]+;/,`uint8_t warningMask=${enabled.length?enabled.join('|'):'0'};`)
    .replace(/uint8_t slots\[8\]=\{[^}]+\};/,`uint8_t slots[8]={${padded.join(',')}};`)
    .replace(/uint8_t slotCount=\d+;/,`uint8_t slotCount=${config.slotCount};`)
    .replace(/uint8_t tempUnit=[^;]+;/,`uint8_t tempUnit=${temp};`)
    .replace(/uint8_t speedUnit=[^;]+;/,`uint8_t speedUnit=${speed};`)
    .replace(/uint8_t pressureUnit=[^;]+;/,`uint8_t pressureUnit=${pressure};`)
    .replace(/uint8_t airflowUnit=[^;]+;/,`uint8_t airflowUnit=${airflow};`)
    .replace(/uint8_t numberFormat=[^;]+;/,`uint8_t numberFormat=${format};`)
    .replace(/uint8_t brightness=\d+;/,`uint8_t brightness=${Math.round(config.brightness*2.55)};`)
    .replace(/uint16_t logMask=[^;]+;/,`uint16_t logMask=${logged.length?logged.join('|'):'0'};`)
    .replaceAll('"dash-config"',`"${configNamespace()}"`)
    .replace('specialScreenShown=false,barMode=false;','specialScreenShown=false,barMode=true;');
  const fields={coolant:'p.coolantF',oil:'p.oilF',intake:'p.intakeF',map:'p.mapKpa',fuel:'p.fuelPercent',battery:'p.batteryV'};
  const levels={coolant:2,oil:2,intake:2,map:2,fuel:1,battery:2};
  let alert='uint8_t sensorAlert(const TelemetryPacket &p,uint8_t id){\n';
  Object.entries(warnings).forEach(([sensor,flag])=>{const slot=slotFor(sensor);if(!slot)return;const op=slot.warning.condition==='below'?'<':'>';alert+=`  if(id==${enumName(sensor)}&&(settings.warningMask&${flag})&&${fields[sensor]}${op}=${baseThreshold(sensor,slot)})return ${levels[sensor]};\n`});
  alert+='  return 0;\n}';
  source=source.replace(/uint8_t sensorAlert\(const TelemetryPacket &p,uint8_t id\)\{[\s\S]*?\n\}/,alert);
  const messages={coolant:'ENGINE OVERHEAT',oil:'OIL OVERHEAT',intake:'INTAKE AIR DANGER',map:'MANIFOLD DANGER',fuel:'LOW FUEL',battery:'CHARGING DANGER'};
  Object.entries(messages).forEach(([sensor,original])=>{const slot=slotFor(sensor);if(slot)source=source.replaceAll(`"${original}"`,`"${cText(slot.warning.message)||original}"`)});
  // Replace only the custom-screen functions; retain the supplied radio, SD,
  // diagnostics, preferences, and board setup code from the complete template.
  const renderer=receiverRenderer();
  source=source.replace(/void updateBars\(const TelemetryPacket &p\)\{[\s\S]*?\n\}/,'')
    .replace(/void drawBarPage\(\)\{[\s\S]*?\n\}/,()=>renderer);
  return `// COMPLETE DASHBOARD SKETCH GENERATED BY OBD DIGITAL DASH DESIGNER\n// Profile: ${cText(config.name)}\n// Slot count: ${config.slotCount}\n\n${source}`;
}
['profileName','slotCount','layout','decimals','brightness','background','textColor','valueColor','warningColor','tempUnit','speedUnit','pressureUnit','airflowUnit'].forEach(id=>$(id).addEventListener('input',()=>{config.name=$('profileName').value;if(id==='slotCount')resizeSlots(+$('slotCount').value);config.layout=$('layout').value;config.decimals=$('decimals').value;config.brightness=+$('brightness').value;config.colors={background:$('background').value,text:$('textColor').value,value:$('valueColor').value,warning:$('warningColor').value};config.units={temp:$('tempUnit').value,speed:$('speedUnit').value,pressure:$('pressureUnit').value,airflow:$('airflowUnit').value};$('brightnessOut').value=config.brightness+'%';syncInspector();render()}));
['sensorSelect','widgetStyle','showUnit','logSensor','warningEnabled','warningCondition','warningThreshold','warningMessage'].forEach(id=>$(id).addEventListener('input',()=>{const slot=config.slots[selected],old=slot.sensor;slot.sensor=$('sensorSelect').value;slot.style=$('widgetStyle').value;slot.showUnit=$('showUnit').checked;slot.log=$('logSensor').checked;if(old!==slot.sensor)slot.warning={...sensors[slot.sensor].warning};else slot.warning={enabled:$('warningEnabled').checked,condition:$('warningCondition').value,threshold:+$('warningThreshold').value,message:$('warningMessage').value};syncInspector();render()}));
document.querySelectorAll('[data-preset]').forEach(b=>b.onclick=()=>{const sets={daily:['coolant','oil','map','battery','fuel','rpm','speed','intake'],performance:['rpm','load','throttle','map','timing','speed','maf','torque'],towing:['coolant','oil','load','battery','map','speed','rail','intake'],diagnostic:['rpm','coolant','battery','map','maf','rail','afr','obd_replies']};config.slots=sets[b.dataset.preset].slice(0,config.slotCount).map(sensor=>({sensor,style:b.dataset.preset==='performance'?'bar':'number',showUnit:true,log:true,warning:{...sensors[sensor].warning}}));selected=0;syncAll();toast(`${b.textContent} preset applied`)});
$('warningTest').onclick=()=>{warningTest=!warningTest;$('warningTest').textContent=warningTest?'Stop warnings':'Test warnings';render()};$('resetDesign').onclick=()=>{config=structuredClone(defaults);selected=0;syncAll();toast('Design reset')};$('saveProfile').onclick=()=>{try{localStorage.setItem('obdDashProfile',JSON.stringify(config));toast('Profile saved in this browser')}catch{toast('Browser storage is unavailable; export JSON to keep this profile')}};$('exportJson').onclick=()=>download('obd-dashboard.json',JSON.stringify(config,null,2));$('exportCpp').onclick=async()=>{const button=$('exportCpp');button.disabled=true;button.textContent='Building complete sketch…';try{const sketch=await completeSketch();const name=(config.name||'obd-dashboard').toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'');download(`${name||'obd-dashboard'}.ino`,sketch,'text/plain');toast('Complete ESP32 sketch downloaded')}catch(error){toast(error.message||'Sketch export failed')}finally{button.disabled=false;button.textContent='Download complete ESP32 sketch'}};
function loadProfile(){try{const saved=localStorage.getItem('obdDashProfile');if(!saved)return;const p=JSON.parse(saved);if(!p||![4,6,8].includes(p.slotCount)||!Array.isArray(p.slots)||p.slots.length!==p.slotCount||p.slots.some(s=>!sensors[s.sensor]||!s.warning)||!p.colors||!p.units)throw Error('Invalid profile');config={...structuredClone(defaults),...p};}catch{config=structuredClone(defaults);toast('Saved profile could not be loaded; using defaults')}}
function moveSelected(direction){const target=selected+direction;if(target<0||target>=config.slots.length)return;[config.slots[selected],config.slots[target]]=[config.slots[target],config.slots[selected]];selected=target;syncInspector();render()}
$('moveEarlier').onclick=()=>moveSelected(-1);$('moveLater').onclick=()=>moveSelected(1);
loadProfile();resizeSlots(config.slotCount);syncAll();
setInterval(()=>{if(dragFrom!==null||dashboard.contains(document.activeElement))return;Object.values(sensors).forEach((s,i)=>{const center=(s.min+s.max)/2,span=(s.max-s.min)*.22;s.value=center+Math.sin(Date.now()/1700+i)*span});render()},500);
