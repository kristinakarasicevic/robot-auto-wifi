(function(){
  "use strict";

  var RESEND_MS   = 250;
  var POLL_MS     = 300;
  var TIMEOUT_MS  = 1500;
  var GAUGE_MAX   = 80;
  var STOP_CM     = 20;
  var MAX_FAILS   = 6;
  var CAM_RETRY_MS = 3000;

  var ipIn = document.getElementById('ip');
  var camIpIn = document.getElementById('camip');
  var ssidIn = document.getElementById('ssid');
  var passIn = document.getElementById('pass');
  var connectBtn = document.getElementById('connectBtn');
  var saveWifiBtn = document.getElementById('saveWifiBtn');
  var wifiMsg = document.getElementById('wifiMsg');
  var connCard = document.getElementById('connCard');
  var pill = document.getElementById('pill');
  var pillTxt = document.getElementById('pillTxt');
  var distNum = document.getElementById('distNum');
  var distUnit = document.getElementById('distUnit');
  var gaugeFill = document.getElementById('gaugeFill');
  var telemetry = document.getElementById('telemetry');
  var padBtns = Array.prototype.slice.call(document.querySelectorAll('.dbtn'));
  var camImg = document.getElementById('camImg');
  var camPh = document.getElementById('camPh');
  var camLive = document.getElementById('camLive');

  var baseUrl = "";
  var connected = false;
  var pollTimer = null;
  var driveTimer = null;
  var activeBtn = null;
  var activeDir = null;
  var failCount = 0;
  var camOn = true;            // kamera je uvek ukljucena
  var camRetryTimer = null;

  function setPill(state, txt){
    pill.className = 'pill' + (state ? ' ' + state : '');
    pillTxt.textContent = txt;
  }
  function enableControls(on){
    padBtns.forEach(function(b){ b.disabled = !on; });
    saveWifiBtn.disabled = !on;
  }

  // ---- mreza (auto) ----
  function apiGet(path){
    var ctrl = new AbortController();
    var t = setTimeout(function(){ ctrl.abort(); }, TIMEOUT_MS);
    return fetch(baseUrl + path, {signal: ctrl.signal, cache:'no-store'})
      .then(function(r){ if(!r.ok) throw new Error('HTTP ' + r.status); return r.json(); })
      .finally(function(){ clearTimeout(t); });
  }

  function connect(){
    var ip = ipIn.value.trim();
    if(!ip){ setPill('err','unesi IP adresu auta'); ipIn.focus(); return; }
    baseUrl = 'http://' + ip;
    setPill('busy','povezivanje…');
    connectBtn.disabled = true;
    apiGet('/status').then(function(data){
      connected = true; failCount = 0;
      setPill('on','povezano');
      enableControls(true);
      updateTelemetry(data);
      startPolling();
      connCard.removeAttribute('open');
      camStart();                       // pokusaj i kameru
    }).catch(function(){
      connected = false; enableControls(false); stopPolling();
      setPill('err','nema veze — proveri IP / mrežu');
    }).finally(function(){ connectBtn.disabled = false; });
  }

  function markDisconnected(){
    connected = false; stopPolling(); stopDrive(); enableControls(false);
    setPill('err','veza prekinuta');
  }

  function saveWifi(){
    if(!connected){ setPill('err','prvo se poveži na auto'); return; }
    var ssid = ssidIn.value.trim();
    if(!ssid){ setPill('err','unesi SSID mreže'); ssidIn.focus(); return; }
    var pass = passIn.value;
    var url = '/wifi?ssid=' + encodeURIComponent(ssid) + '&pass=' + encodeURIComponent(pass);
    saveWifiBtn.disabled = true;
    setPill('busy','šaljem mrežu autu…');
    wifiMsg.classList.remove('show');
    apiGet(url).then(function(){
      markDisconnected();
      setPill('busy','auto se prebacuje na „' + ssid + '"');
      wifiMsg.innerHTML = '<b>Auto je primio mrežu i restartuje se.</b><br>' +
        '1) Sačekaj ~15 s.<br>2) Prebaci telefon na mrežu „' + ssid + '".<br>' +
        '3) Nađi novi IP auta (Serial Monitor ili lista uređaja) i ponovo se poveži.';
      wifiMsg.classList.add('show');
    }).catch(function(){
      markDisconnected();
      setPill('err','nema potvrde — auto je možda već krenuo da prebacuje');
      wifiMsg.innerHTML = 'Nije stigla potvrda, ali auto je možda već primio mrežu. Prebaci telefon na „' +
        ssid + '" i potraži novi IP. Ako se ne pojavi, auto se posle 15 s sam vraća na podrazumevanu mrežu.';
      wifiMsg.classList.add('show');
    }).finally(function(){ saveWifiBtn.disabled = !connected; });
  }

  function startPolling(){
    stopPolling();
    pollTimer = setInterval(function(){
      apiGet('/status').then(function(d){ failCount = 0; updateTelemetry(d); })
      .catch(function(){ failCount++; if(failCount >= MAX_FAILS) markDisconnected(); });
    }, POLL_MS);
  }
  function stopPolling(){ if(pollTimer){ clearInterval(pollTimer); pollTimer = null; } }

  // ---- voznja ----
  function sendMove(dir){
    if(!baseUrl) return;
    apiGet('/move?dir=' + dir).then(function(d){ updateTelemetry(d); }).catch(function(){});
  }
  function startDrive(btn, dir){
    if(!connected) return;
    activeBtn = btn; activeDir = dir;
    btn.classList.add('active');
    sendMove(dir);
    clearInterval(driveTimer);
    driveTimer = setInterval(function(){ sendMove(dir); }, RESEND_MS);
  }
  function stopDrive(){
    clearInterval(driveTimer); driveTimer = null;
    if(activeBtn){ activeBtn.classList.remove('active'); }
    activeBtn = null; activeDir = null;
    if(connected) sendMove('S');
  }

  // ---- kamera (MJPEG, uvek ukljucena) ----
  function camStreamUrl(){ return 'http://' + camIpIn.value.trim() + ':81/stream?_=' + Date.now(); }
  function camStart(){
    if(!camOn || !camIpIn.value.trim()) return;
    clearTimeout(camRetryTimer);
    camPh.textContent = 'Povezivanje sa kamerom…';
    camPh.style.display = 'flex';
    camImg.style.display = 'block';
    camImg.src = camStreamUrl();
  }
  function camStop(){
    camImg.removeAttribute('src'); camImg.style.display = 'none'; camLive.classList.remove('on');
  }
  camImg.addEventListener('load', function(){
    if(camOn){ camPh.style.display = 'none'; camLive.classList.add('on'); }
  });
  camImg.addEventListener('error', function(){
    camImg.style.display = 'none'; camLive.classList.remove('on');
    camPh.textContent = 'Nema signala — pokušavam ponovo… (proveri IP kamere, mrežu, http/lokalno)';
    camPh.style.display = 'flex';
    clearTimeout(camRetryTimer);
    camRetryTimer = setTimeout(function(){ if(camOn && !document.hidden) camStart(); }, CAM_RETRY_MS);
  });

  // ---- prikaz udaljenosti (kompaktno) ----
  function updateTelemetry(d){
    if(!d) return;
    var dist = (typeof d.dist === 'number') ? d.dist : parseFloat(d.dist);
    var blocked = !!d.blocked;

    if(!isFinite(dist) || dist >= 400){
      distNum.textContent = 'čisto';
      distUnit.style.display = 'none';
      gaugeFill.style.right = '0%';
      gaugeFill.style.background = 'var(--green)';
    } else {
      distNum.textContent = dist.toFixed(0);
      distUnit.style.display = '';
      var pct = Math.max(0, Math.min(1, dist / GAUGE_MAX));
      gaugeFill.style.right = ((1 - pct) * 100).toFixed(1) + '%';
      gaugeFill.style.background = dist < STOP_CM ? 'var(--red)' : (dist < 40 ? 'var(--amber)' : 'var(--green)');
    }
    telemetry.classList.toggle('blocked', blocked);
  }

  // ---- event-i ----
  connectBtn.addEventListener('click', connect);
  saveWifiBtn.addEventListener('click', saveWifi);
  ipIn.addEventListener('keydown', function(e){ if(e.key === 'Enter') connect(); });
  camIpIn.addEventListener('change', camStart);   // cim uneses IP -> kamera krece
  camIpIn.addEventListener('keydown', function(e){ if(e.key === 'Enter') camStart(); });

  padBtns.forEach(function(btn){
    var dir = btn.getAttribute('data-dir');
    if(dir === 'S'){
      btn.addEventListener('click', function(e){ e.preventDefault(); stopDrive(); sendMove('S'); });
      return;
    }
    btn.addEventListener('pointerdown', function(e){
      e.preventDefault();
      if(btn.setPointerCapture){ try{ btn.setPointerCapture(e.pointerId); }catch(_){} }
      startDrive(btn, dir);
    });
    btn.addEventListener('pointerup',          function(){ if(activeDir === dir) stopDrive(); });
    btn.addEventListener('pointercancel',      function(){ if(activeDir === dir) stopDrive(); });
    btn.addEventListener('lostpointercapture', function(){ if(activeDir === dir) stopDrive(); });
    btn.addEventListener('contextmenu', function(e){ e.preventDefault(); });
  });

  // sigurnost + kamera: u pozadini stani i pauziraj stream, u prvom planu vrati
  document.addEventListener('visibilitychange', function(){
    if(document.hidden){ stopDrive(); if(camOn) camStop(); }
    else { if(camOn) camStart(); }
  });
  window.addEventListener('blur', stopDrive);

  // podrazumevana imena preko mDNS-a — ne mora da se kuca IP
  if(!ipIn.value)    ipIn.value    = 'auto.local';
  if(!camIpIn.value) camIpIn.value = 'cam.local';
  
  setPill('', 'nije povezan');
})();
