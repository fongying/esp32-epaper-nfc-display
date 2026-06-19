// 本檔由 tools/export_web_header.ps1 產生，請不要手動修改。
// 修改網頁請編輯 web\config.html，再執行該工具同步到 Arduino 韌體。
#pragma once
#include <Arduino.h>

const char CONFIG_INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-Hant">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>裝置設定</title>
  <style>
    :root {
      --bg:#fbf8f2;
      --panel:#ffffff;
      --ink:#18181b;
      --muted:#5f6673;
      --line:#e7dfd2;
      --accent:#0ea5a3;
      --cream:#fffdf7;
      --danger:#d10f1f;
    }
    * { box-sizing:border-box; }
    html { color-scheme:light; }
    body {
      margin:0;
      min-height:100vh;
      background:
        linear-gradient(90deg, rgba(24,24,27,.035) 1px, transparent 1px),
        linear-gradient(0deg, rgba(24,24,27,.035) 1px, transparent 1px),
        var(--bg);
      background-size:24px 24px;
      color:var(--ink);
      font-family:"Microsoft JhengHei","Noto Sans TC",Arial,sans-serif;
      letter-spacing:0;
    }
    button, input, a { font:inherit; }
    a { color:inherit; text-decoration:none; }
    button, .nav-link {
      border:1px solid var(--ink);
      border-radius:8px;
      min-height:40px;
      padding:9px 12px;
      background:var(--panel);
      color:var(--ink);
      cursor:pointer;
      display:inline-flex;
      align-items:center;
      justify-content:center;
    }
    button:hover, .nav-link:hover { background:var(--cream); }
    button:focus-visible, input:focus-visible, .nav-link:focus-visible {
      outline:3px solid rgba(14,165,163,.28);
      outline-offset:2px;
    }
    button.primary { background:var(--ink); color:#fff; }
    .app {
      width:min(980px, calc(100vw - 32px));
      min-height:100dvh;
      margin:0 auto;
      padding:16px 0;
      display:grid;
      grid-template-rows:auto 1fr;
      gap:16px;
    }
    .topbar {
      display:flex;
      align-items:center;
      justify-content:space-between;
      gap:14px;
      border:1px solid var(--line);
      border-radius:8px;
      background:rgba(255,255,255,.92);
      padding:14px 16px;
    }
    .brand h1 { margin:0; font-size:24px; line-height:1.2; }
    .brand p { margin:4px 0 0; color:var(--muted); font-size:13px; }
    .actions { display:flex; gap:8px; flex-wrap:wrap; justify-content:flex-end; }
    .workspace {
      display:grid;
      grid-template-columns:minmax(280px, 360px) minmax(0, 1fr);
      gap:16px;
      align-items:start;
    }
    .panel {
      border:1px solid var(--line);
      border-radius:8px;
      background:rgba(255,255,255,.94);
      padding:16px;
      min-width:0;
    }
    .stack { display:grid; gap:14px; }
    .status-card {
      border:1px solid #b9eadb;
      border-radius:8px;
      background:#f1fffa;
      padding:14px;
    }
    .status-card span {
      display:block;
      color:var(--muted);
      font-size:13px;
      margin-bottom:6px;
    }
    .status-card strong {
      display:block;
      color:#075e54;
      font-size:20px;
      line-height:1.25;
      overflow-wrap:anywhere;
    }
    label { display:grid; gap:7px; color:var(--muted); font-size:13px; }
    input {
      width:100%;
      min-height:42px;
      border:1px solid var(--line);
      border-radius:8px;
      padding:9px 10px;
      background:#fff;
      color:var(--ink);
    }
    .button-row {
      display:grid;
      grid-template-columns:1fr 1fr;
      gap:8px;
    }
    .network-list {
      display:grid;
      gap:8px;
      max-height:430px;
      overflow:auto;
    }
    .network-item {
      display:grid;
      grid-template-columns:minmax(0, 1fr) auto;
      gap:10px;
      align-items:center;
      width:100%;
      min-height:42px;
      border-color:var(--line);
      text-align:left;
      justify-content:stretch;
    }
    .network-item span {
      overflow:hidden;
      text-overflow:ellipsis;
      white-space:nowrap;
    }
    .network-item small {
      color:var(--muted);
      white-space:nowrap;
    }
    .hint {
      margin:0;
      color:var(--muted);
      font-size:13px;
      line-height:1.55;
    }
    .message {
      border:1px solid var(--line);
      border-radius:8px;
      background:var(--cream);
      padding:11px 12px;
      min-height:42px;
      color:var(--ink);
      font-size:13px;
      line-height:1.5;
    }
    .message.error {
      border-color:rgba(209,15,31,.42);
      color:var(--danger);
      background:#fff8f8;
    }
    @media (max-width: 760px) {
      .app { width:min(100vw - 16px, 560px); padding:8px 0 18px; }
      .topbar { align-items:stretch; flex-direction:column; }
      .actions, .button-row { grid-template-columns:1fr; display:grid; }
      .workspace { grid-template-columns:1fr; gap:10px; }
      .panel { padding:12px; }
    }
  </style>
</head>
<body>
  <main class="app">
    <header class="topbar">
      <div class="brand">
        <h1>裝置設定</h1>
        <p>管理 ESP32 Wi-Fi 連線；設定會保存於 NVS，更新韌體不會覆蓋。</p>
      </div>
      <nav class="actions">
        <a class="nav-link" href="/">回設計器</a>
        <a class="nav-link" href="/nfc">NFC 管理</a>
      </nav>
    </header>

    <div class="workspace">
      <section class="panel stack">
        <h2>Wi-Fi 設定</h2>
        <div class="status-card">
          <span>目前狀態</span>
          <strong id="wifiState">讀取中...</strong>
        </div>
        <form class="stack" id="wifiForm">
          <label>
            SSID
            <input id="wifiSsid" type="text" maxlength="32" autocomplete="off" placeholder="輸入 Wi-Fi 名稱">
          </label>
          <label>
            密碼
            <input id="wifiPassword" type="password" maxlength="63" autocomplete="new-password" placeholder="留空代表開放網路">
          </label>
          <div class="button-row">
            <button id="wifiReload" type="button">重新讀取</button>
            <button class="primary" type="submit">儲存並重啟</button>
          </div>
        </form>
        <div class="message" id="wifiMessage">請掃描附近 Wi-Fi，或手動輸入 SSID。</div>
        <p class="hint">儲存後 ESP32 會重新啟動並嘗試連線。若 30 秒內連不上，會自動回到 AP 設定模式。</p>
      </section>

      <section class="panel stack">
        <div class="button-row">
          <button id="wifiScan" type="button" class="primary">掃描 Wi-Fi</button>
          <button id="clearList" type="button">清除清單</button>
        </div>
        <div class="network-list" id="wifiNetworks"></div>
      </section>
    </div>
  </main>

  <script>
    const localPreview = location.protocol === 'file:';
    if (localPreview) {
      document.querySelector('.nav-link[href="/"]').setAttribute('href', 'index.html');
      document.querySelector('.nav-link[href="/nfc"]').setAttribute('href', 'nfc.html');
    }

    const wifiStateEl = document.getElementById('wifiState');
    const wifiMessageEl = document.getElementById('wifiMessage');
    const wifiFormEl = document.getElementById('wifiForm');
    const wifiSsidEl = document.getElementById('wifiSsid');
    const wifiPasswordEl = document.getElementById('wifiPassword');
    const wifiReloadEl = document.getElementById('wifiReload');
    const wifiScanEl = document.getElementById('wifiScan');
    const wifiNetworksEl = document.getElementById('wifiNetworks');
    const clearListEl = document.getElementById('clearList');

    function setMessage(text, isError = false) {
      wifiMessageEl.textContent = text;
      wifiMessageEl.classList.toggle('error', isError);
    }

    async function loadWifiSettings() {
      if (localPreview) {
        wifiSsidEl.value = 'ICSLab';
        wifiStateEl.textContent = '本機預覽';
        setMessage('燒入 ESP32 後，此頁會讀取實際 Wi-Fi 狀態。');
        return;
      }

      try {
        const res = await fetch('/wifi', { cache:'no-store' });
        if (!res.ok) throw new Error(await res.text() || `HTTP ${res.status}`);
        const data = await res.json();
        wifiSsidEl.value = data.ssid || '';
        if (data.connected) {
          wifiStateEl.textContent = `${data.ssid || '(未命名)'} / ${data.ip}`;
          setMessage(`已連線，可使用 http://${data.ip} 或 ${data.mdns}`);
        } else if (data.apMode) {
          wifiStateEl.textContent = `${data.apSsid || 'ESP32 AP'} / ${data.ip || '192.168.0.1'}`;
          setMessage('目前在 AP 設定模式，請選擇或輸入可用 Wi-Fi。');
        } else {
          wifiStateEl.textContent = 'Wi-Fi 已關閉';
          setMessage(`目前設定 SSID：${data.ssid || '(未設定)'}`);
        }
      } catch (err) {
        wifiStateEl.textContent = '讀取失敗';
        setMessage(`讀取 Wi-Fi 狀態失敗：${err.message}`, true);
      }
    }

    function renderWifiNetworks(items) {
      wifiNetworksEl.innerHTML = '';
      if (!items || !items.length) {
        wifiNetworksEl.textContent = '沒有掃描到 Wi-Fi，仍可手動輸入 SSID。';
        return;
      }

      for (const item of items) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'network-item';
        const lock = item.secure ? '需要密碼' : '開放';
        button.innerHTML = `<span>${item.ssid}</span><small>${item.rssi} dBm · ${lock}</small>`;
        button.addEventListener('click', () => {
          wifiSsidEl.value = item.ssid;
          setMessage(`已選擇 ${item.ssid}，請輸入密碼後儲存。`);
          if (item.secure) wifiPasswordEl.focus();
        });
        wifiNetworksEl.appendChild(button);
      }
    }

    async function scanWifiNetworks() {
      if (localPreview) {
        renderWifiNetworks([
          { ssid:'ICSLab', rssi:-42, secure:true },
          { ssid:'ESP32-Test', rssi:-68, secure:true }
        ]);
        setMessage('本機預覽：這是模擬掃描結果。');
        return;
      }

      wifiScanEl.disabled = true;
      wifiScanEl.textContent = '掃描中...';
      wifiNetworksEl.textContent = 'ESP32 正在掃描附近 Wi-Fi，請稍候。';
      try {
        const res = await fetch('/wifi/scan', { cache:'no-store' });
        if (!res.ok) throw new Error(await res.text() || `HTTP ${res.status}`);
        const data = await res.json();
        renderWifiNetworks(data.items || []);
        setMessage(`掃描完成，共 ${data.count || 0} 個 Wi-Fi。`);
      } catch (err) {
        wifiNetworksEl.textContent = '';
        setMessage(`掃描失敗：${err.message}`, true);
      } finally {
        wifiScanEl.disabled = false;
        wifiScanEl.textContent = '掃描 Wi-Fi';
      }
    }

    async function saveWifiSettings(event) {
      event.preventDefault();
      const ssid = wifiSsidEl.value.trim();
      const password = wifiPasswordEl.value;
      if (!ssid) {
        setMessage('請輸入或選擇 SSID。', true);
        return;
      }

      if (localPreview) {
        setMessage('本機預覽模式不會儲存設定。');
        return;
      }

      const body = new URLSearchParams();
      body.set('ssid', ssid);
      body.set('password', password);

      setMessage('正在儲存 Wi-Fi 設定...');
      try {
        const res = await fetch('/wifi', {
          method:'POST',
          headers:{ 'Content-Type':'application/x-www-form-urlencoded;charset=UTF-8' },
          body
        });
        const text = await res.text();
        if (!res.ok) throw new Error(text || `HTTP ${res.status}`);
        wifiPasswordEl.value = '';
        setMessage('已儲存，ESP32 正在重啟。請稍後用新 IP、http://epaper.local，或 AP 設定模式重新開啟。');
      } catch (err) {
        setMessage(`儲存失敗：${err.message}`, true);
      }
    }

    wifiFormEl.addEventListener('submit', saveWifiSettings);
    wifiReloadEl.addEventListener('click', loadWifiSettings);
    wifiScanEl.addEventListener('click', scanWifiNetworks);
    clearListEl.addEventListener('click', () => {
      wifiNetworksEl.innerHTML = '';
      setMessage('已清除掃描清單。');
    });

    loadWifiSettings();
  </script>
</body>
</html>

)HTML";