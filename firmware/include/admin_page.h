#pragma once

#include <Arduino.h>

const char ADMIN_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
  <meta name="theme-color" content="#0b0d0e">
  <title>AI Clock 控制台</title>
  <style>
    :root {
      color-scheme: dark;
      --canvas: #0b0d0e;
      --surface: #121516;
      --raised: #191d1e;
      --line: rgba(255,255,255,.08);
      --line-soft: rgba(255,255,255,.05);
      --text: #eef1ef;
      --muted: #929a96;
      --green: #4adf82;
      --red: #f06a66;
      --weather: #63c7ad;
      --amber: #e8b45f;
      --r-sm: 4px;
      --r-md: 6px;
    }
    * { box-sizing: border-box; }
    html { background: var(--canvas); -webkit-font-smoothing: antialiased; }
    body {
      margin: 0;
      min-width: 300px;
      color: var(--text);
      background: var(--canvas);
      font-family: -apple-system, "SF Pro Text", "PingFang SC", "Noto Sans SC", sans-serif;
      font-size: 14px;
      line-height: 1.55;
      letter-spacing: 0;
      touch-action: manipulation;
    }
    button, input, select { font: inherit; letter-spacing: 0; }
    button, .tab { min-height: 40px; }
    button {
      border: 0;
      border-radius: var(--r-sm);
      padding: 8px 14px;
      color: var(--text);
      background: var(--raised);
      cursor: pointer;
      transition: transform 120ms cubic-bezier(.2,0,0,1), opacity 120ms cubic-bezier(.2,0,0,1);
    }
    button:active { transform: scale(.96); }
    button:disabled { opacity: .45; cursor: not-allowed; }
    button:focus-visible, input:focus-visible, select:focus-visible {
      outline: 2px solid var(--weather);
      outline-offset: 2px;
    }
    .primary { color: #07110c; background: var(--green); font-weight: 700; }
    .danger { color: #190807; background: var(--red); font-weight: 700; }
    .shell { min-height: 100vh; display: grid; grid-template-columns: 188px minmax(0, 1fr); }
    aside {
      position: sticky;
      top: 0;
      align-self: start;
      height: 100vh;
      padding: max(18px, env(safe-area-inset-top)) 14px 18px max(14px, env(safe-area-inset-left));
      background: var(--surface);
      border-right: 1px solid var(--line-soft);
    }
    .brand { padding: 4px 8px 18px; }
    .brand strong { display: block; font-size: 18px; line-height: 1.2; }
    .brand span { color: var(--muted); font-size: 12px; }
    nav { display: grid; gap: 4px; }
    .tab {
      width: 100%;
      text-align: left;
      color: var(--muted);
      background: transparent;
      font-weight: 650;
    }
    .tab[aria-selected="true"] { color: var(--text); background: var(--raised); }
    main { min-width: 0; padding: 26px clamp(18px, 4vw, 56px) 64px; }
    .topbar {
      display: grid;
      grid-template-columns: minmax(0, 1fr) auto;
      align-items: center;
      gap: 20px;
      max-width: 980px;
      margin: 0 auto 24px;
    }
    h1, h2, h3, p { margin: 0; }
    h1 { font-size: 22px; line-height: 1.25; font-weight: 760; }
    h2 { font-size: 16px; line-height: 1.3; }
    h3 { font-size: 13px; color: var(--muted); font-weight: 650; }
    .sub { color: var(--muted); margin-top: 3px; font-size: 13px; }
    .statusline { display: flex; flex-wrap: wrap; justify-content: flex-end; gap: 7px; }
    .chip {
      display: inline-flex;
      align-items: center;
      min-height: 28px;
      padding: 3px 9px;
      border-radius: 999px;
      color: var(--muted);
      background: var(--surface);
      font-size: 12px;
      font-variant-numeric: tabular-nums;
      white-space: nowrap;
    }
    .chip::before { content: ""; width: 7px; height: 7px; margin-right: 7px; border-radius: 50%; background: var(--muted); }
    .chip.online::before { background: var(--green); }
    .chip.warn::before { background: var(--amber); }
    .chip[hidden] { display: none; }
    .view { display: none; max-width: 980px; margin: 0 auto; }
    .view.active { display: block; }
    .band { padding: 22px 0 26px; border-top: 1px solid var(--line); }
    .band:first-child { border-top: 0; padding-top: 0; }
    .bandhead { display: flex; justify-content: space-between; align-items: end; gap: 16px; margin-bottom: 14px; }
    .metricrow { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 1px; background: var(--line); }
    .metric { min-width: 0; padding: 15px; background: var(--surface); }
    .metric label { display: block; color: var(--muted); font-size: 12px; }
    .metric strong {
      display: block;
      margin-top: 4px;
      font-size: clamp(18px, 2.8vw, 28px);
      line-height: 1.15;
      font-variant-numeric: tabular-nums;
      overflow-wrap: anywhere;
    }
    .weather strong { color: var(--weather); }
    .grid2 { display: grid; grid-template-columns: repeat(2, minmax(0, 1fr)); gap: 20px 28px; }
    .field { min-width: 0; }
    .field > label, .field > .labelrow { display: flex; justify-content: space-between; gap: 12px; margin-bottom: 7px; color: var(--muted); font-size: 12px; }
    input[type="text"], input[type="password"], input[type="time"], select {
      width: 100%;
      min-height: 42px;
      border: 1px solid var(--line);
      border-radius: var(--r-sm);
      padding: 8px 11px;
      color: var(--text);
      background: var(--surface);
    }
    input[type="range"] { width: 100%; accent-color: var(--green); }
    input:disabled { opacity: .45; cursor: not-allowed; }
    .seg {
      display: grid;
      grid-template-columns: repeat(3, minmax(0, 1fr));
      gap: 4px;
      padding: 4px;
      border-radius: var(--r-md);
      background: var(--surface);
    }
    .seg button { min-width: 0; overflow-wrap: anywhere; color: var(--muted); background: transparent; }
    .seg button.active { color: var(--text); background: var(--raised); box-shadow: inset 0 0 0 1px var(--line); }
    .actions { display: flex; flex-wrap: wrap; gap: 8px; align-items: center; }
    .switchline { display: flex; align-items: center; justify-content: space-between; min-height: 46px; gap: 16px; border-bottom: 1px solid var(--line-soft); }
    .switchline:last-child { border-bottom: 0; }
    .switchline input { width: 20px; height: 20px; accent-color: var(--green); }
    .stocklist { display: grid; gap: 8px; counter-reset: stock; }
    .stockstatus { display: flex; flex-wrap: wrap; gap: 7px; margin-bottom: 14px; }
    .stockrow { display: grid; grid-template-columns: 34px minmax(0, 1fr) 42px; gap: 8px; align-items: center; }
    .stockrow::before { counter-increment: stock; content: counter(stock, decimal-leading-zero); color: var(--muted); font-variant-numeric: tabular-nums; }
    .iconbtn { width: 42px; padding: 0; font-size: 18px; }
    .note { color: var(--muted); font-size: 12px; }
    .fileline { display: grid; grid-template-columns: 110px minmax(0, 1fr) auto auto; gap: 8px; align-items: center; }
    input[type="file"] { width: 100%; min-width: 0; color: var(--muted); }
    .toast {
      position: fixed;
      right: max(18px, env(safe-area-inset-right));
      bottom: max(18px, env(safe-area-inset-bottom));
      z-index: 10;
      max-width: min(360px, calc(100vw - 36px));
      padding: 11px 14px;
      border-radius: var(--r-sm);
      color: var(--text);
      background: #242a28;
      box-shadow: 0 8px 28px rgba(0,0,0,.35);
      opacity: 0;
      transform: translateY(8px);
      pointer-events: none;
      transition: transform 160ms cubic-bezier(.16,1,.3,1), opacity 160ms cubic-bezier(.16,1,.3,1);
    }
    .toast.show { opacity: 1; transform: translateY(0); }
    .toast.error { background: #3b1d1c; }
    .sr-only { position: absolute; width: 1px; height: 1px; padding: 0; margin: -1px; overflow: hidden; clip: rect(0,0,0,0); white-space: nowrap; border: 0; }
    @media (hover:hover) { button:hover { filter: brightness(1.12); } }
    @media (max-width: 760px) {
      .shell { display: block; }
      aside {
        position: sticky;
        z-index: 5;
        height: auto;
        padding: max(10px, env(safe-area-inset-top)) max(12px, env(safe-area-inset-right)) 10px max(12px, env(safe-area-inset-left));
        border-right: 0;
        border-bottom: 1px solid var(--line);
      }
      .brand { display: none; }
      nav { display: grid; grid-template-columns: repeat(4, minmax(0, 1fr)); gap: 3px; }
      .tab { text-align: center; padding: 7px 5px; }
      main { padding: 20px 16px 72px; }
      .topbar { grid-template-columns: 1fr; gap: 12px; }
      .statusline { justify-content: flex-start; }
      .metricrow { grid-template-columns: repeat(2, minmax(0, 1fr)); }
      .grid2 { grid-template-columns: 1fr; gap: 16px; }
      .fileline { grid-template-columns: 1fr; }
      .seg { grid-template-columns: repeat(2, minmax(0, 1fr)); }
    }
    @media (max-width: 360px) {
      main { padding-left: 12px; padding-right: 12px; }
      .tab { font-size: 13px; }
      .metric { padding: 12px; }
      .actions button { flex: 1 1 120px; }
    }
    @media (prefers-reduced-motion: reduce) {
      button, .toast { transition: none; }
    }
  </style>
</head>
<body>
  <a class="sr-only" href="#content">跳到主要内容</a>
  <div class="shell">
    <aside>
      <div class="brand"><strong>AI Clock</strong><span id="fwSide">设备控制台</span></div>
      <nav aria-label="设置分类">
        <button class="tab" data-view="screen" aria-selected="true">屏幕</button>
        <button class="tab" data-view="markets" aria-selected="false">行情</button>
        <button class="tab" data-view="clock" aria-selected="false">时钟</button>
        <button class="tab" data-view="system" aria-selected="false">系统</button>
      </nav>
    </aside>
    <main id="content">
      <header class="topbar">
        <div><h1 id="pageTitle">屏幕</h1><p class="sub" id="deviceLine">正在读取设备状态</p></div>
        <div class="statusline">
          <span class="chip" id="deviceChip">设备</span>
          <span class="chip" id="bridgeChip">Bridge</span>
          <span class="chip" id="timeChip">NTP</span>
        </div>
      </header>

      <section class="view active" data-view-panel="screen">
        <div class="band">
          <div class="bandhead"><div><h2>当前画面</h2><p class="sub" id="effectiveText">读取中</p></div></div>
          <div class="seg" id="modeSeg">
            <button data-mode="auto">自动</button><button data-mode="claude">Claude</button>
            <button data-mode="codex">Codex</button><button data-mode="net">网速</button>
            <button data-mode="weather">天气时钟</button><button data-mode="stock">股票</button>
          </div>
        </div>
        <div class="band">
          <div class="stockstatus"><span class="chip" id="weatherSourceChip">天气来源读取中</span></div>
          <div class="metricrow">
            <div class="metric weather"><label>本地天气</label><strong id="temp">--</strong></div>
            <div class="metric"><label>体感</label><strong id="feels">--</strong></div>
            <div class="metric"><label>湿度</label><strong id="humidity">--</strong></div>
            <div class="metric"><label>今日</label><strong id="range">--</strong></div>
          </div>
        </div>
        <div class="band grid2">
          <div class="field">
            <div class="labelrow"><span>屏幕亮度</span><strong id="brightnessValue">--%</strong></div>
            <input id="brightness" type="range" min="1" max="100" value="50">
          </div>
          <div class="field">
            <div class="labelrow"><span>屏幕电源</span><span id="screenState">读取中</span></div>
            <div class="actions"><button class="primary" id="screenOn">临时点亮</button><button id="screenAuto">按计划</button><button id="screenOff">立即熄屏</button></div>
          </div>
        </div>
        <div class="band">
          <div class="bandhead"><div><h2>定时熄屏</h2><p class="sub" id="screenScheduleSummary">每天 00:00-07:00 熄屏</p></div><span class="chip" id="screenScheduleChip">计划读取中</span></div>
          <label class="switchline" for="screenScheduleEnabled"><span>启用定时熄屏</span><input id="screenScheduleEnabled" type="checkbox" checked></label>
          <div class="grid2" style="margin-top:14px">
            <div class="field"><label for="screenOffStart">关闭开始</label><input id="screenOffStart" type="time" value="00:00" required></div>
            <div class="field"><label for="screenOffEnd">恢复点亮</label><input id="screenOffEnd" type="time" value="07:00" required></div>
          </div>
          <div class="actions" style="margin-top:14px"><button class="primary" id="saveScreenSchedule">保存定时设置</button></div>
        </div>
      </section>

      <section class="view" data-view-panel="markets">
        <div class="band">
          <div class="bandhead"><div><h2>行情列表</h2><p class="sub">最多 4 个，按当前顺序显示</p></div><button id="addStock">添加</button></div>
          <div class="stockstatus" aria-live="polite">
            <span class="chip" id="stockSourceChip">行情来源读取中</span>
            <span class="chip warn" id="stockSyncChip" hidden>股票配置待同步</span>
          </div>
          <div class="stocklist" id="stockList"></div>
          <div class="actions" style="margin-top:14px"><button class="primary" id="saveStocks">保存行情</button></div>
        </div>
        <div class="band"><p class="note">QQQ 使用腾讯行情；币价在 Mac Bridge 在线时使用 OKX，设备独立更新时使用 Binance Vision。其他腾讯代码需带市场前缀，如 usAAPL、hk00700、sh600519。</p></div>
      </section>

      <section class="view" data-view-panel="clock">
        <div class="band">
          <div class="bandhead"><div><h2>时钟主题</h2><p class="sub" id="lunarText">农历读取中</p></div></div>
          <div class="seg" id="themeSeg"><button data-theme="classic">晨光数字</button><button data-theme="minimal">港湾表盘</button><button data-theme="dashboard">气象仪表</button></div>
        </div>
        <div class="band">
          <div class="bandhead"><div><h2>和风天气</h2><p class="sub" id="qweatherState">配置状态读取中</p></div><span class="chip" id="qweatherChip">读取中</span></div>
          <div class="grid2">
            <div class="field"><label for="qweatherHost">API Host</label><input id="qweatherHost" type="text" maxlength="95" autocomplete="off" autocapitalize="none" spellcheck="false" placeholder="项目 API Host"></div>
            <div class="field"><label for="qweatherKey">API Key</label><input id="qweatherKey" type="password" maxlength="64" autocomplete="new-password" autocapitalize="none" spellcheck="false" placeholder="已配置时留空不变"></div>
          </div>
          <div class="actions" style="margin-top:14px"><button class="primary" id="saveQweather">保存和风配置</button></div>
        </div>
        <div class="band grid2">
          <div>
            <div class="switchline"><span>显示农历</span><input id="lunar" type="checkbox"></div>
            <div class="switchline"><span>夜间模式</span><input id="nightEnabled" type="checkbox"></div>
          </div>
          <div class="field">
            <div class="labelrow"><span>夜间亮度</span><strong id="nightBrightnessValue">--%</strong></div>
            <input id="nightBrightness" type="range" min="1" max="50" value="10">
          </div>
          <div class="field"><label for="nightStart">开始时间</label><input id="nightStart" type="time" value="22:00"></div>
          <div class="field"><label for="nightEnd">结束时间</label><input id="nightEnd" type="time" value="07:00"></div>
        </div>
        <div class="band">
          <div class="grid2">
            <div class="field"><label for="ntpServer">NTP 服务器</label><input id="ntpServer" type="text" maxlength="63" value="ntp.aliyun.com"></div>
            <div class="actions" style="align-self:end"><button class="primary" id="saveClock">保存时钟设置</button><button id="syncTime">立即对时</button></div>
          </div>
        </div>
      </section>

      <section class="view" data-view-panel="system">
        <div class="band grid2">
          <div class="field"><label for="bridgeHost">Bridge host</label><input id="bridgeHost" type="text" maxlength="64" placeholder="192.0.2.20:8765"></div>
          <div class="actions" style="align-self:end"><button class="primary" id="saveBridge">保存 Bridge</button><button id="refresh">刷新状态</button></div>
        </div>
        <div class="band">
          <div class="metricrow">
            <div class="metric"><label>固件</label><strong id="fw">--</strong></div>
            <div class="metric"><label>可用堆</label><strong id="heap">--</strong></div>
            <div class="metric"><label>LittleFS</label><strong id="fs">--</strong></div>
            <div class="metric"><label>链路</label><strong id="transport">--</strong></div>
          </div>
        </div>
        <div class="band">
          <div class="bandhead"><div><h2>桌宠动画</h2><p class="sub">GIF 上传到设备并立即应用</p></div></div>
          <form class="fileline" id="gifForm"><select id="gifTarget"><option value="claude">Claude</option><option value="codex">Codex</option></select><input id="gifFile" type="file" accept=".gif,image/gif" required><button type="submit">上传</button><button type="button" id="resetGif">恢复默认</button></form>
        </div>
        <div class="band">
          <div class="bandhead"><div><h2>Wi-Fi 配置</h2><p class="sub">重置后设备将开启 AI-Clock-Setup 热点</p></div><button class="danger" id="resetWifi">重置 Wi-Fi</button></div>
        </div>
      </section>
    </main>
  </div>
  <div class="toast" id="toast" role="status" aria-live="polite"></div>
  <script>
    const $ = s => document.querySelector(s);
    const $$ = s => [...document.querySelectorAll(s)];
    const titles = {screen:'屏幕', markets:'行情', clock:'时钟', system:'系统'};
    let info = null;
    let stockStatus = {};
    let stockMetadataAvailable = false;
    let qweatherConfigured = false;
    let toastTimer = 0;

    function toast(message, error=false) {
      const el = $('#toast');
      el.textContent = message;
      el.classList.toggle('error', error);
      el.classList.add('show');
      clearTimeout(toastTimer);
      toastTimer = setTimeout(() => el.classList.remove('show'), 2600);
    }
    async function api(path, options={}) {
      const response = await fetch(path, options);
      const text = await response.text();
      if (!response.ok) throw new Error(text || `HTTP ${response.status}`);
      const type = response.headers.get('content-type') || '';
      return type.includes('json') ? JSON.parse(text || '{}') : text;
    }
    function formBody(values) {
      return {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'}, body:new URLSearchParams(values)};
    }
    function setActive(group, key, value) {
      $$(`${group} button`).forEach(b => b.classList.toggle('active', b.dataset[key] === value));
    }
    function minutesToTime(value) {
      const m = Number(value) || 0;
      return `${String(Math.floor(m/60)%24).padStart(2,'0')}:${String(m%60).padStart(2,'0')}`;
    }
    function timeToMinutes(value) {
      const [h,m] = value.split(':').map(Number);
      return h*60+m;
    }
    function renderScheduleSummary(enabled, start, end) {
      const summary = $('#screenScheduleSummary');
      if (!enabled) summary.textContent = '定时熄屏已关闭';
      else if (Number(start) === Number(end)) summary.textContent = '关闭时段为零，不会自动熄屏';
      else summary.textContent = `每天 ${minutesToTime(start)}-${minutesToTime(end)} 熄屏`;
    }
    function updateScheduleInputs() {
      const enabled = $('#screenScheduleEnabled').checked;
      $('#screenOffStart').disabled = !enabled;
      $('#screenOffEnd').disabled = !enabled;
      renderScheduleSummary(enabled, timeToMinutes($('#screenOffStart').value), timeToMinutes($('#screenOffEnd').value));
    }
    function renderQweatherState(configured) {
      qweatherConfigured = configured;
      $('#qweatherChip').textContent = configured ? '已配置' : '未配置';
      $('#qweatherChip').className = `chip ${configured ? 'online' : 'warn'}`;
      $('#qweatherState').textContent = configured ? 'API Key 已保存到设备' : '需要 Host 和 API Key 才能设备直连更新';
      $('#qweatherKey').placeholder = configured ? '已配置，留空不变' : '输入和风天气 API Key';
    }
    function modeName(value) {
      return ({auto:'自动',claude:'Claude',codex:'Codex',net:'网速',weather:'天气时钟',clock:'天气时钟',music:'天气时钟',stock:'股票'})[value] || value;
    }
    function ageText(value) {
      if (value === null || value === undefined || value === '') return '';
      const seconds = Math.floor(Number(value));
      if (!Number.isFinite(seconds) || seconds < 0) return '';
      if (seconds < 60) return `${seconds}秒前`;
      if (seconds < 3600) return `${Math.floor(seconds/60)}分钟前`;
      return `${Math.floor(seconds/3600)}小时前`;
    }
    function renderStockStatus(next={}, authoritative=true) {
      const keys = ['source','stale','age_s','sync_pending'];
      if (authoritative && keys.some(key => Object.prototype.hasOwnProperty.call(next,key))) stockMetadataAvailable = true;
      keys.forEach(key => {
        if (Object.prototype.hasOwnProperty.call(next,key)) stockStatus[key] = next[key];
      });
      const source = stockStatus.source || 'none';
      const sourceLabel = ({bridge:'Mac Bridge',direct:'设备直连',cache:'缓存',none:'等待行情'})[source] || '行情来源未知';
      const stale = source !== 'none' && (stockStatus.stale === true || source === 'cache');
      const parts = [sourceLabel];
      if (stale) parts.push('数据已过期');
      const age = ageText(stockStatus.age_s);
      if (age) parts.push(age);
      const sourceChip = $('#stockSourceChip');
      sourceChip.textContent = parts.join(' · ');
      sourceChip.className = `chip ${stale ? 'warn' : (source === 'bridge' || source === 'direct' ? 'online' : '')}`;
      const syncChip = $('#stockSyncChip');
      syncChip.hidden = stockStatus.sync_pending !== true;
    }
    function renderInfo(data) {
      info = data;
      $('#deviceLine').textContent = `${data.ip || '--'} · ${data.ssid || '未连接 Wi-Fi'} · ${modeName(data.effective)}`;
      $('#deviceChip').textContent = data.ip || '设备';
      $('#deviceChip').className = `chip ${data.ip ? 'online' : ''}`;
      $('#bridgeChip').textContent = data.last_update_s >= 0 ? `Bridge ${data.last_update_s}s` : 'Bridge 未更新';
      $('#bridgeChip').className = `chip ${data.last_update_s >= 0 && data.last_update_s < 20 ? 'online' : 'warn'}`;
      $('#timeChip').textContent = data.time_valid ? 'NTP 已同步' : 'NTP 对时中';
      $('#timeChip').className = `chip ${data.time_valid ? 'online' : 'warn'}`;
      $('#effectiveText').textContent = `正在显示 ${modeName(data.effective)}${data.mode !== data.effective ? `，设定为 ${modeName(data.mode)}` : ''}`;
      setActive('#modeSeg','mode',(data.mode === 'music' || data.mode === 'clock') ? 'weather' : data.mode);
      $('#brightness').value = data.brightness ?? 50;
      $('#brightnessValue').textContent = `${data.brightness ?? '--'}%`;
      renderQweatherState(data.qweather_configured === true);
      if (data.screen_on) {
        $('#screenState').textContent = data.screen_schedule_wake_override ? '临时点亮' : (data.night_active ? '夜间亮度' : '已点亮');
      } else {
        $('#screenState').textContent = data.manual_screen_off ? '手动熄屏' : (data.screen_schedule_active ? '按计划熄屏' : '已熄屏');
      }
      const scheduleChip = $('#screenScheduleChip');
      if (data.screen_schedule_enabled === false) {
        scheduleChip.textContent = '计划已关闭';
        scheduleChip.className = 'chip';
      } else if (!data.time_valid) {
        scheduleChip.textContent = '等待 NTP';
        scheduleChip.className = 'chip warn';
      } else if (data.screen_schedule_active && data.screen_schedule_wake_override) {
        scheduleChip.textContent = '计划时段 · 临时点亮';
        scheduleChip.className = 'chip online';
      } else if (data.screen_schedule_active) {
        scheduleChip.textContent = '定时熄屏中';
        scheduleChip.className = 'chip warn';
      } else {
        scheduleChip.textContent = '计划待命';
        scheduleChip.className = 'chip online';
      }
      renderScheduleSummary(data.screen_schedule_enabled !== false, data.screen_off_start_min ?? 0, data.screen_off_end_min ?? 420);
      $('#bridgeHost').value = data.bridge || '';
      $('#fw').textContent = data.fw || '--';
      $('#fwSide').textContent = `固件 ${data.fw || '--'}`;
      $('#heap').textContent = data.free_heap ? `${Math.round(data.free_heap/1024)} KB` : '--';
      $('#fs').textContent = data.fs_total ? `${Math.round(data.fs_used/1024)}/${Math.round(data.fs_total/1024)} KB` : '--';
      $('#transport').textContent = data.wired ? 'USB' : 'Wi-Fi';
      const w = data.weather || {};
      const weatherSource = w.source || (data.last_update_s >= 0 ? 'bridge' : 'none');
      const providerName = w.provider === 'qweather' ? '和风' : (w.provider === 'caiyun' ? '彩云缓存' : (w.provider === 'legacy' ? '旧缓存' : '天气'));
      const weatherSourceLabel = ({bridge:`${providerName} · Mac Bridge`,direct:`${providerName} · 设备直连`,cache:`${providerName} · 缓存`,none:'等待天气'})[weatherSource] || '天气来源未知';
      const weatherStale = w.stale === true || weatherSource === 'cache';
      const weatherParts = [weatherSourceLabel];
      if (weatherStale && weatherSource !== 'none') weatherParts.push('数据已过期');
      const weatherAge = ageText(w.age_s);
      if (weatherAge) weatherParts.push(weatherAge);
      $('#weatherSourceChip').textContent = weatherParts.join(' · ');
      $('#weatherSourceChip').className = `chip ${weatherStale ? 'warn' : (weatherSource === 'bridge' || weatherSource === 'direct' ? 'online' : '')}`;
      $('#temp').textContent = w.available ? `${Number(w.temperature_c).toFixed(1)}°` : '--';
      $('#feels').textContent = w.available ? `${Number(w.apparent_c).toFixed(1)}°` : '--';
      $('#humidity').textContent = w.available ? `${w.humidity_pct}%` : '--';
      $('#range').textContent = w.available ? `${Number(w.low_c).toFixed(0)}°/${Number(w.high_c).toFixed(0)}°` : '--';
      $('#lunarText').textContent = w.lunar_zh || (w.available ? (w.condition_zh || w.location || '本地天气') : '等待天气数据');
      if (data.stock && typeof data.stock === 'object') {
        renderStockStatus(data.stock);
      } else if (!stockMetadataAvailable) {
        renderStockStatus({source:data.last_update_s >= 0 ? 'bridge' : 'none',stale:data.last_update_s < 0 || data.last_update_s >= 20,age_s:data.last_update_s}, false);
      }
    }
    async function loadInfo(showError=true) {
      try { renderInfo(await api('/api/info')); }
      catch (e) { if (showError) toast(`状态读取失败：${e.message}`, true); }
    }
    async function loadSettings() {
      try {
        const s = await api('/api/settings');
        setActive('#themeSeg','theme',s.clock_theme);
        $('#lunar').checked = !!s.lunar_enabled;
        $('#nightEnabled').checked = !!s.night_enabled;
        $('#nightStart').value = minutesToTime(s.night_start_min);
        $('#nightEnd').value = minutesToTime(s.night_end_min);
        $('#nightBrightness').value = s.night_brightness;
        $('#nightBrightnessValue').textContent = `${s.night_brightness}%`;
        $('#screenScheduleEnabled').checked = s.screen_schedule_enabled !== false;
        $('#screenOffStart').value = minutesToTime(s.screen_off_start_min ?? 0);
        $('#screenOffEnd').value = minutesToTime(s.screen_off_end_min ?? 420);
        updateScheduleInputs();
        $('#qweatherHost').value = s.qweather_host || '';
        $('#qweatherKey').value = '';
        renderQweatherState(s.qweather_configured === true);
        $('#ntpServer').value = s.ntp_server || 'ntp.aliyun.com';
      } catch (e) { toast(`时钟设置读取失败：${e.message}`, true); }
    }
    function stockRow(value='') {
      const row = document.createElement('div');
      row.className = 'stockrow';
      row.innerHTML = `<input type="text" maxlength="16" value="${value.replace(/[&<>"']/g,'')}" placeholder="输入行情代码"><button class="iconbtn" type="button" aria-label="删除行情">×</button>`;
      row.querySelector('button').onclick = () => { row.remove(); if (!$('#stockList').children.length) addStock(); };
      return row;
    }
    function addStock(value='') {
      if ($('#stockList').children.length >= 4) return toast('最多显示 4 个行情', true);
      $('#stockList').append(stockRow(value));
    }
    async function loadStocks() {
      try {
        const data = await api('/api/stocks');
        renderStockStatus(data);
        $('#stockList').replaceChildren();
        (data.symbols || []).forEach(addStock);
        if (!$('#stockList').children.length) addStock('usQQQ');
      } catch (e) { $('#stockList').replaceChildren(); ['usQQQ','BTCUSDT','ETHUSDT','ETHBTC'].forEach(addStock); toast(`行情配置读取失败：${e.message}`, true); }
    }

    $$('.tab').forEach(tab => tab.onclick = () => {
      const view = tab.dataset.view;
      $$('.tab').forEach(x => x.setAttribute('aria-selected', String(x === tab)));
      $$('.view').forEach(x => x.classList.toggle('active', x.dataset.viewPanel === view));
      $('#pageTitle').textContent = titles[view];
    });
    $$('#modeSeg button').forEach(button => button.onclick = async () => {
      try { await api('/api/display', formBody({mode:button.dataset.mode})); await loadInfo(false); toast(`已切换到${button.textContent}`); }
      catch (e) { toast(`切换失败：${e.message}`, true); }
    });
    $$('#themeSeg button').forEach(button => button.onclick = () => setActive('#themeSeg','theme',button.dataset.theme));
    $('#brightness').oninput = e => $('#brightnessValue').textContent = `${e.target.value}%`;
    $('#brightness').onchange = async e => {
      try { await api('/api/brightness', formBody({level:e.target.value})); await loadInfo(false); }
      catch (err) { toast(`亮度设置失败：${err.message}`, true); }
    };
    $('#nightBrightness').oninput = e => $('#nightBrightnessValue').textContent = `${e.target.value}%`;
    $('#screenScheduleEnabled').onchange = updateScheduleInputs;
    $('#screenOffStart').onchange = updateScheduleInputs;
    $('#screenOffEnd').onchange = updateScheduleInputs;
    $('#screenOn').onclick = () => api('/api/screen', formBody({state:'on'})).then(() => loadInfo(false)).then(() => toast('已临时点亮')).catch(e => toast(e.message,true));
    $('#screenAuto').onclick = () => api('/api/screen', formBody({state:'auto'})).then(() => loadInfo(false)).then(() => toast('已恢复定时计划')).catch(e => toast(e.message,true));
    $('#screenOff').onclick = () => api('/api/screen', formBody({state:'off'})).then(() => loadInfo(false)).then(() => toast('屏幕已熄灭')).catch(e => toast(e.message,true));
    $('#saveScreenSchedule').onclick = async () => {
      const start = $('#screenOffStart').value;
      const end = $('#screenOffEnd').value;
      if (!start || !end) return toast('请选择完整的关闭时段', true);
      try {
        await api('/api/settings', formBody({screen_schedule_enabled:$('#screenScheduleEnabled').checked?'1':'0',screen_off_start_min:timeToMinutes(start),screen_off_end_min:timeToMinutes(end)}));
        await Promise.all([loadSettings(),loadInfo(false)]); toast('定时熄屏已保存');
      } catch (e) { toast(`保存失败：${e.message}`, true); }
    };
    $('#saveQweather').onclick = async () => {
      const host = $('#qweatherHost').value.trim().toLowerCase();
      const key = $('#qweatherKey').value.trim();
      if (!/^(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+re\.qweatherapi\.com$/.test(host) || host.length > 95) return toast('API Host 格式不正确', true);
      if (key && !/^[A-Za-z0-9_-]{8,64}$/.test(key)) return toast('API Key 格式不正确', true);
      if (!key && !qweatherConfigured) return toast('首次配置需要输入 API Key', true);
      try {
        const values = {qweather_host:host};
        if (key) values.qweather_key = key;
        await api('/api/settings', formBody(values));
        $('#qweatherKey').value = '';
        await Promise.all([loadSettings(),loadInfo(false)]); toast('和风天气配置已保存');
      } catch (e) { toast(`保存失败：${e.message}`, true); }
    };
    $('#addStock').onclick = () => addStock();
    $('#saveStocks').onclick = async () => {
      const symbols = $$('#stockList input').map(x => x.value.trim()).filter(Boolean);
      if (!symbols.length) return toast('至少保留一个行情', true);
      try {
        const result = await api('/api/stocks', {method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({symbols})});
        if (!result.ok) throw new Error(result.error || '保存失败');
        renderStockStatus(result);
        $('#stockList').replaceChildren(); (result.symbols || symbols).forEach(addStock); toast(result.sync_pending ? '行情列表已保存，等待同步' : '行情列表已保存');
      } catch (e) { toast(`行情保存失败：${e.message}`, true); }
    };
    $('#saveClock').onclick = async () => {
      const theme = $('#themeSeg button.active')?.dataset.theme || 'classic';
      const ntp = $('#ntpServer').value.trim();
      if (!/^[A-Za-z0-9.-]{1,63}$/.test(ntp)) return toast('NTP 服务器格式不正确', true);
      try {
        await api('/api/settings', formBody({clock_theme:theme,lunar_enabled:$('#lunar').checked?'1':'0',night_enabled:$('#nightEnabled').checked?'1':'0',night_start_min:timeToMinutes($('#nightStart').value),night_end_min:timeToMinutes($('#nightEnd').value),night_brightness:$('#nightBrightness').value,ntp_server:ntp}));
        await loadInfo(false); toast('时钟设置已保存');
      } catch (e) { toast(`保存失败：${e.message}`, true); }
    };
    $('#syncTime').onclick = async () => { try { await api('/api/time/sync',{method:'POST'}); toast('已重新发起 NTP 对时'); setTimeout(() => loadInfo(false),1500); } catch(e) { toast(e.message,true); } };
    $('#saveBridge').onclick = async () => { const host=$('#bridgeHost').value.trim(); if(!/^[A-Za-z0-9.-]+:\d{1,5}$/.test(host)) return toast('Bridge 地址格式应为 IP:端口',true); try { await api('/api/bridge',formBody({host})); toast('Bridge 已保存'); await loadInfo(false); } catch(e) { toast(e.message,true); } };
    $('#refresh').onclick = () => Promise.all([loadInfo(),loadSettings(),loadStocks()]).then(() => toast('状态已刷新'));
    $('#gifForm').onsubmit = async e => { e.preventDefault(); const file=$('#gifFile').files[0]; if(!file) return; if(file.size>262144) return toast('GIF 不能超过 256KB',true); const button=e.submitter; button.disabled=true; const body=new FormData(); body.append('file',file); try { await api(`/sprite/${$('#gifTarget').value}`,{method:'POST',body}); await loadInfo(false); toast('动画已上传'); } catch(err) { toast(`上传失败：${err.message}`,true); } finally { button.disabled=false; } };
    $('#resetGif').onclick = async e => { const slot=$('#gifTarget').value; const label=$('#gifTarget').selectedOptions[0].textContent; if(!confirm(`恢复 ${label} 默认动画？当前自定义动画会被移除。`)) return; e.currentTarget.disabled=true; try { const current=await api('/api/info'); renderInfo(current); if(!Number.isInteger(current.sprite_rev)) throw new Error('设备未返回动画版本'); await api(`/sprite/${slot}/reset`,formBody({expected_rev:String(current.sprite_rev)})); await loadInfo(false); toast(`${label} 已恢复默认动画`); } catch(err) { toast(`恢复失败：${err.message}`,true); } finally { e.currentTarget.disabled=false; } };
    $('#resetWifi').onclick = async () => { if(!confirm('清除 Wi-Fi 设置并重启设备？')) return; try { await api('/reset-wifi',formBody({confirm:'RESET'})); toast('设备正在重启'); } catch(e) { toast(e.message,true); } };

    Promise.all([loadInfo(),loadSettings(),loadStocks()]);
    setInterval(() => loadInfo(false), 5000);
  </script>
</body>
</html>)HTML";
