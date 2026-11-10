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
function download(name,text,type='application/pdf'){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text],{type}));a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(a.href),500)}
function settingsPdf(){
  const clean=s=>String(s??'').replace(/\u00b0/g,'deg ').normalize('NFKD').replace(/[\u0300-\u036f]/g,'').replace(/[^\x20-\x7e]/g,'?');
  const escape=s=>clean(s).replace(/([\\()])/g,'\\$1');
  const text=(s,x,y,size=11,bold=false)=>`BT /${bold?'F2':'F1'} ${size} Tf 1 0 0 1 ${x} ${y} Tm (${escape(s)}) Tj ET\n`;
  const line=y=>`0.83 0.85 0.87 RG 48 ${y} m 564 ${y} l S\n`;
  const heading=(subtitle,page)=>'0.06 0.11 0.16 rg\n'+text('OBD DASHBOARD SETTINGS',48,738,20,true)+text(subtitle,48,714,11)+line(698)+text(`Page ${page} of 2`,492,36,9);
  let overview=heading(config.name||'My OBD Dashboard',1);
  overview+=text('Display',48,668,14,true);
  const rows=[['Visible slots',config.slotCount],['Layout',{grid:'Tile Grid',bars:'Horizontal Bars',hero:'Hero Layout'}[config.layout]],['Number formatting',config.decimals==='auto'?'Auto':`${config.decimals} decimal places`],['Brightness',`${config.brightness}%`],['Background color',config.colors.background],['Label color',config.colors.text],['Value color',config.colors.value],['Warning color',config.colors.warning]];
  rows.forEach(([label,value],i)=>{overview+=text(label,48,640-i*24)+text(value,270,640-i*24,11,true)});
  overview+=line(445)+text('Units',48,419,14,true);
  [['Temperature',config.units.temp==='C'?'Celsius (deg C)':'Fahrenheit (deg F)'],['Speed',config.units.speed==='KMH'?'km/h':'MPH'],['Pressure',{KPA:'kPa',PSI:'PSI',BAR:'bar'}[config.units.pressure]],['Airflow',config.units.airflow==='GS'?'g/s':'lb/min']].forEach(([label,value],i)=>{overview+=text(label,48,391-i*24)+text(value,270,391-i*24,11,true)});
  overview+=line(278)+text('Selection summary',48,252,14,true)+text(`Logged sensors: ${config.slots.filter(s=>s.log).length} of ${config.slotCount}`,48,226)+text(`Warnings enabled: ${config.slots.filter(s=>s.warning.enabled).length} of ${config.slotCount}`,48,204);
  overview+=text('This PDF records your selected settings; it does not contain firmware.',48,146,10)+text('Preview readings are simulated and are not included in this settings record.',48,128,10)+text('edunleavy.github.io/tools/obd-dashboard-designer/',48,92,9);
  let slots=heading('Sensor order, display options, logging and warnings',2);
  config.slots.forEach((s,i)=>{
    const y=670-i*72;
    slots+=text(`SLOT ${i+1}  |  ${sensors[s.sensor].name}`,48,y,12,true);
    slots+=text(`Style: ${{number:'Large number',bar:'Fill bar',compact:'Compact'}[s.style]}  |  Unit: ${unitFor(s.sensor)||'None'}  |  Show unit: ${s.showUnit?'Yes':'No'}  |  SD log: ${s.log?'Yes':'No'}`,48,y-18,9);
    const warning=s.warning;
    slots+=text(`Warning: ${warning.enabled?'Enabled':'Disabled'}  |  Condition: ${warning.condition==='below'?'Below':'Above'} ${warning.threshold} ${unitFor(s.sensor)}`,48,y-34,10);
    slots+=text(`Message: ${warning.message||'(none)'}`,48,y-50,10)+line(y-59);
  });
  const objects=['<< /Type /Catalog /Pages 2 0 R >>','<< /Type /Pages /Kids [5 0 R 7 0 R] /Count 2 >>','<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>','<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica-Bold >>'];
  for(const stream of [overview,slots]){
    const contentId=objects.length+2;
    objects.push(`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792] /Resources << /Font << /F1 3 0 R /F2 4 0 R >> >> /Contents ${contentId} 0 R >>`);
    objects.push(`<< /Length ${stream.length} >>\nstream\n${stream}endstream`);
  }
  let pdf='%PDF-1.4\n',offsets=[0];
  objects.forEach((object,i)=>{offsets.push(pdf.length);pdf+=`${i+1} 0 obj\n${object}\nendobj\n`});
  const xref=pdf.length;
  pdf+=`xref\n0 ${objects.length+1}\n0000000000 65535 f \n`+offsets.slice(1).map(offset=>String(offset).padStart(10,'0')+' 00000 n \n').join('');
  return pdf+`trailer\n<< /Size ${objects.length+1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
}

['profileName','slotCount','layout','decimals','brightness','background','textColor','valueColor','warningColor','tempUnit','speedUnit','pressureUnit','airflowUnit'].forEach(id=>$(id).addEventListener('input',()=>{config.name=$('profileName').value;if(id==='slotCount')resizeSlots(+$('slotCount').value);config.layout=$('layout').value;config.decimals=$('decimals').value;config.brightness=+$('brightness').value;config.colors={background:$('background').value,text:$('textColor').value,value:$('valueColor').value,warning:$('warningColor').value};config.units={temp:$('tempUnit').value,speed:$('speedUnit').value,pressure:$('pressureUnit').value,airflow:$('airflowUnit').value};$('brightnessOut').value=config.brightness+'%';syncInspector();render()}));
['sensorSelect','widgetStyle','showUnit','logSensor','warningEnabled','warningCondition','warningThreshold','warningMessage'].forEach(id=>$(id).addEventListener('input',()=>{const slot=config.slots[selected],old=slot.sensor;slot.sensor=$('sensorSelect').value;slot.style=$('widgetStyle').value;slot.showUnit=$('showUnit').checked;slot.log=$('logSensor').checked;if(old!==slot.sensor)slot.warning={...sensors[slot.sensor].warning};else slot.warning={enabled:$('warningEnabled').checked,condition:$('warningCondition').value,threshold:+$('warningThreshold').value,message:$('warningMessage').value};syncInspector();render()}));
document.querySelectorAll('[data-preset]').forEach(b=>b.onclick=()=>{const sets={daily:['coolant','oil','map','battery','fuel','rpm','speed','intake'],performance:['rpm','load','throttle','map','timing','speed','maf','torque'],towing:['coolant','oil','load','battery','map','speed','rail','intake'],diagnostic:['rpm','coolant','battery','map','maf','rail','afr','obd_replies']};config.slots=sets[b.dataset.preset].slice(0,config.slotCount).map(sensor=>({sensor,style:b.dataset.preset==='performance'?'bar':'number',showUnit:true,log:true,warning:{...sensors[sensor].warning}}));selected=0;syncAll();toast(`${b.textContent} preset applied`)});
$('warningTest').onclick=()=>{warningTest=!warningTest;$('warningTest').textContent=warningTest?'Stop warnings':'Test warnings';render()};$('resetDesign').onclick=()=>{config=structuredClone(defaults);selected=0;syncAll();toast('Design reset')};$('saveProfile').onclick=()=>{try{localStorage.setItem('obdDashProfile',JSON.stringify(config));toast('Profile saved in this browser')}catch{toast('Browser storage is unavailable; save a PDF to keep these settings')}};$('exportPdf').onclick=()=>{const name=(config.name||'obd-dashboard').toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'')||'obd-dashboard';download(`${name}-settings.pdf`,settingsPdf(),'application/pdf');toast('Settings PDF downloaded')};
function loadProfile(){try{const saved=localStorage.getItem('obdDashProfile');if(!saved)return;const p=JSON.parse(saved);if(!p||![4,6,8].includes(p.slotCount)||!Array.isArray(p.slots)||p.slots.length!==p.slotCount||p.slots.some(s=>!sensors[s.sensor]||!s.warning)||!p.colors||!p.units)throw Error('Invalid profile');config={...structuredClone(defaults),...p};}catch{config=structuredClone(defaults);toast('Saved profile could not be loaded; using defaults')}}
function moveSelected(direction){const target=selected+direction;if(target<0||target>=config.slots.length)return;[config.slots[selected],config.slots[target]]=[config.slots[target],config.slots[selected]];selected=target;syncInspector();render()}
$('moveEarlier').onclick=()=>moveSelected(-1);$('moveLater').onclick=()=>moveSelected(1);
loadProfile();resizeSlots(config.slotCount);syncAll();
setInterval(()=>{if(dragFrom!==null||dashboard.contains(document.activeElement))return;Object.values(sensors).forEach((s,i)=>{const center=(s.min+s.max)/2,span=(s.max-s.min)*.22;s.value=center+Math.sin(Date.now()/1700+i)*span});render()},500);
