// Dán vào <script> của UI v1.6.3 để thay polling nhiều endpoint bằng /snapshot
async function loadSnapshotUI(){
  const j = await fetch('/snapshot').then(r=>r.json());

  // status
  for(let i=0;i<15;i++){
    let l=document.getElementById('l'+i);
    let b=document.getElementById('b'+i);
    if(l)l.className='lamp '+(j.status[i]?'on':'off');
    if(b)b.className='btn '+(j.status[i]?'on':'');
  }

  // flow
  if(window.f1) f1.innerHTML = Number(j.flow.f1).toFixed(2);
  if(window.f2) f2.innerHTML = Number(j.flow.f2).toFixed(2);

  // volume
  ['v0','v1','v2','v3','v4','v5','v6'].forEach(k=>{
    const el = document.getElementById(k);
    if(el) el.innerHTML = Number(j.vol[k]).toFixed(3);
  });

  // d100
  const hr = document.getElementById('hr_val');
  if(hr) hr.innerHTML = j.d100;

  // m522
  window.M522_state = !!j.m522;
  if(typeof updateOffTimeSwitch === 'function') updateOffTimeSwitch();

  // autoval
  const bwm=document.getElementById('bw_m'); if(bwm) bwm.innerHTML=String(j.autoval.bw_m).padStart(2,'0');
  const bws=document.getElementById('bw_s'); if(bws) bws.innerHTML=String(j.autoval.bw_s).padStart(2,'0');
  const fwm=document.getElementById('fw_m'); if(fwm) fwm.innerHTML=String(j.autoval.fw_m).padStart(2,'0');
  const fws=document.getElementById('fw_s'); if(fws) fws.innerHTML=String(j.autoval.fw_s).padStart(2,'0');

  // clock
  const t = j.clock;
  const sDeg = t.sec * 6;
  const mDeg = t.min * 6 + t.sec * 0.1;
  const hDeg = (t.hour % 12) * 30 + t.min * 0.5;
  const s = document.getElementById('s_hand'); if(s) s.style.transform=`translateX(-50%) rotate(${sDeg}deg)`;
  const m = document.getElementById('m_hand'); if(m) m.style.transform=`translateX(-50%) rotate(${mDeg}deg)`;
  const h = document.getElementById('h_hand'); if(h) h.style.transform=`translateX(-50%) rotate(${hDeg}deg)`;

  if(window.clock_day) clock_day.innerHTML = String(t.day).padStart(2,'0');
  if(window.clock_month) clock_month.innerHTML = String(t.mon).padStart(2,'0');
  if(window.clock_year) clock_year.innerHTML = t.year;

  // alarm
  const box=document.getElementById('alarmBox');
  const ul=document.getElementById('alarmList');
  if(ul && box){
    ul.innerHTML='';
    if(!j.alarm || j.alarm.length===0){
      box.className='alarm-ok';
      ul.innerHTML='<li>Bình thường</li>';
    }else{
      box.className='alarm-ng';
      j.alarm.forEach(a=>{let li=document.createElement('li');li.innerHTML=a;ul.appendChild(li);});
      if(j.alarm.includes('Mất kết nối PLC')) box.classList.add('blink');
    }
  }

  if(typeof updateAutoValUI === 'function') updateAutoValUI();
}

setInterval(()=>{ loadSnapshotUI().catch(()=>{}); },1000);
