(() => {
  'use strict';

  const $ = id => document.getElementById(id);
  const esc = value => String(value == null ? '' : value).replace(/[&<>"']/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'}[c]));
  const labels = {idle: '未开始', waiting: '待回放', queued: '待回放', running: '回放中', replaying: '回放中', comparing: '比较中', processing: '处理中', pending: '比较未完成', identical: '一致', different: '不同', unavailable: '证据不可用', passed: '通过', failed: '失败', error: '错误', not_verified: '未验证', not_run: '未运行', blocked: '准备阻塞', incomplete: '不完整', cancelling: '正在停止', cancelled: '已中止', canceled: '已中止', skipped: '未运行', paused: '已暂停', completed: '执行完成', accepted: '已接受候选', defect: '已确认缺陷'};
  const symbols = {running: '▶', replaying: '▶', comparing: '◌', processing: '◌', pending: '◌', passed: '✓', failed: '!', error: '!', blocked: '!', cancelled: '−', canceled: '−', incomplete: '−'};
  let state = {config: {}, cases: [], task: {}, recording: {}, records: [], history: []};
  let page = 'library', authenticated = false, online = false, booted = false, authBlocked = false;
  let selected = new Set(), expanded = null, query = '', viewedRun = null, result = null, resultError = '';
  let inspected = null, checkpointId = null, imageMode = 'pair', selectedRecord = null;
  let polling = false, pollTimer = null, toastTimer = null, requestEpoch = 0;
  let renderedEvidence = '', reviewKey = '', reviewDirty = false, configDirty = false;
  let stateRequest = 0, appliedStateRequest = 0;
  let autoSelectedSession = null;
  let loginRetryAt = 0, loginRetryTimer = null;
  const commands = new Set();
  const modalFocus = new Map();
  const zoom = {scale: 1, x: 0, y: 0, pointer: null};

  const array = value => Array.isArray(value) ? value : [];
  const statusOf = value => {
    const raw = typeof value === 'string' ? value : value && (value.status || value.acceptance || value.automaticStatus);
    return String(raw || 'not_verified').toLowerCase();
  };
  const titleOf = status => labels[statusOf(status)] || String(status || '未验证');
  const passed = item => statusOf(item) === 'passed';
  const failed = item => ['failed', 'error'].includes(statusOf(item));
  const pending = item => ['pending', 'comparing', 'processing', 'running', 'replaying', 'queued', 'waiting'].includes(statusOf(item));
  const taskBusy = () => ['running', 'paused', 'queued', 'cancelling'].includes(state.task && state.task.state);
  const replayTask = () => ['regression', 'replay'].includes(state.task && state.task.kind);
  const taskAction = () => ({connect: '正在连接应用', 'record-start': '正在开始录制', 'record-checkpoint': '正在采集检查点', 'record-stop': '正在保存录制'}[state.task && state.task.kind] || '正在处理任务');
  const recordingBusy = () => !!(state.recording && (state.recording.active || state.recording.pendingSave));
  const resultIdForTask = () => state.task && (state.task.resultId || null);
  const currentTaskViewed = () => !!viewedRun && viewedRun === resultIdForTask();
  const caseById = id => array(state.cases).find(c => String(c.id) === String(id));
  const recordById = id => array(state.records).find(r => String(r.id) === String(id));
  const busy = key => commands.has(key);
  const duration = seconds => {
    seconds = Math.max(0, Math.floor(Number(seconds) || 0));
    return (seconds >= 3600 ? String(Math.floor(seconds / 3600)).padStart(2, '0') + ':' : '') + String(Math.floor(seconds / 60) % 60).padStart(2, '0') + ':' + String(seconds % 60).padStart(2, '0');
  };
  const date = value => {
    if (!value) return '时间未知';
    const d = new Date(typeof value === 'number' ? value * 1000 : value);
    return Number.isNaN(d.getTime()) ? String(value) : d.toLocaleString('zh-CN', {hour12: false});
  };
  const json = value => {
    try { return JSON.stringify(value == null ? {} : value, null, 2); } catch (_) { return String(value); }
  };
  function commandId() {
    if (typeof crypto.randomUUID === 'function') return crypto.randomUUID();
    const bytes = crypto.getRandomValues(new Uint8Array(16));
    bytes[6] = (bytes[6] & 15) | 64; bytes[8] = (bytes[8] & 63) | 128;
    const hex = Array.from(bytes, byte => byte.toString(16).padStart(2, '0')).join('');
    return [hex.slice(0, 8), hex.slice(8, 12), hex.slice(12, 16), hex.slice(16, 20), hex.slice(20)].join('-');
  }
  function safeURL(raw) {
    if (!raw || typeof raw !== 'string') return null;
    try {
      const url = new URL(raw, window.location.href);
      return url.origin === window.location.origin && ['http:', 'https:'].includes(url.protocol) ? url.href : null;
    } catch (_) { return null; }
  }
  function patch(id, html) {
    const node = $(id);
    if (node.dataset.rendered !== html) { node.innerHTML = html; node.dataset.rendered = html; }
  }
  function setError(id, message) { $(id).textContent = message || ''; $(id).hidden = !message; }
  function toast(message) {
    $('toast').textContent = message; $('toast').hidden = false;
    clearTimeout(toastTimer); toastTimer = setTimeout(() => { $('toast').hidden = true; }, 5200);
  }
  function authRequired() {
    authBlocked = true; clearTimeout(pollTimer);
    disable('open-result-submit', true);
    authenticated = false; $('application').hidden = true; $('login-page').hidden = false;
    closeModal('config-modal'); closeModal('record-point-modal'); closeModal('zoom-dialog');
  }
  async function api(path, options = {}) {
    const response = await fetch(path, {credentials: 'same-origin', cache: 'no-store', ...options});
    let payload;
    try { payload = await response.json(); } catch (_) { throw new Error('服务返回了无法读取的响应（HTTP ' + response.status + '）。'); }
    if (response.status === 401) { authRequired(); const error = new Error(payload.error || '请重新输入连接码。'); error.auth = true; throw error; }
    if (!response.ok) { const error = new Error(payload.error || '请求失败（HTTP ' + response.status + '）。'); error.status = response.status; error.retryAfter = Number(response.headers.get('Retry-After')) || 0; throw error; }
    return payload;
  }
  async function command(key, path, body = {}, errorTarget = null) {
    if (busy(key)) return null;
    commands.add(key); render();
    try {
      const payload = await api(path, {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({...body, commandId: commandId()})});
      try { await refreshState(); } catch (error) {
        online = false;
        if (!error.auth) toast('操作请求已接受，暂时无法刷新状态。请等待连接恢复，不会自动重复操作。');
      }
      if (errorTarget) setError(errorTarget, '');
      return payload;
    } catch (error) {
      const message = error instanceof TypeError ? '连接中断。请求可能已被服务器接受；请先等待状态恢复，操作不会自动重发。' : error.message;
      if (errorTarget) setError(errorTarget, message); else if (!error.auth) toast(message);
      return null;
    } finally { commands.delete(key); render(); }
  }
  function showPage(next) {
    page = next;
    ['library', 'run', 'history', 'record'].forEach(id => { $(id + '-page').hidden = id !== next; });
    $('library-nav').classList.toggle('active', next === 'library');
    $('runs-nav').classList.toggle('active', next === 'history' || next === 'run');
    $('record-nav').classList.toggle('active', next === 'record');
    render();
  }
  function openModal(id, focusId) {
    modalFocus.set(id, document.activeElement); $(id).hidden = false;
    if (focusId) $(focusId).focus();
  }
  function closeModal(id) {
    if (!$(id) || $(id).hidden) return;
    $(id).hidden = true;
    const prior = modalFocus.get(id); if (prior && prior.isConnected) prior.focus();
    modalFocus.delete(id);
  }
  function visibleCases() {
    return array(state.cases).filter(c => (String(c.name || '') + ' ' + c.id).toLowerCase().includes(query.toLowerCase()));
  }
  function disable(id, condition) { $(id).disabled = condition; }
  function render() {
    if (!authenticated) return;
    const config = state.config || {}, task = state.task || {}, recording = state.recording || {};
    $('machine-name').textContent = config.machine || '运行机器';
    $('config-machine').textContent = config.machine || '当前服务所在机器';
    $('application-name').textContent = config.application ? String(config.application).split(/[\\/]/).pop() : '尚未配置程序';
    $('application-caption').textContent = config.application ? (config.machine || '运行机器') : '点击设置可执行文件路径';
    $('active-task-nav').hidden = !taskBusy(); $('active-task-caption').textContent = task.state === 'cancelling' ? '正在停止' : replayTask() ? task.state === 'paused' ? '回放已暂停' : '回放中' : taskAction();
    $('record-live-nav').hidden = !recording.active;
    $('record-live-duration').textContent = duration(recording.startedAt ? Date.now() / 1000 - Number(recording.startedAt) : 0);
    disable('config-save', busy('config') || taskBusy() || recordingBusy() || !online);
    renderCases(); renderRecorder(); renderHistory();
    if (page === 'run') renderRun();
  }
  function renderCases() {
    const rows = visibleCases(), valid = rows.filter(c => !c.error);
    patch('case-list', rows.map(c => {
      const latest = statusOf(c.latestStatus), chosen = selected.has(String(c.id));
      return '<tr class="case-row ' + (chosen ? 'selected ' : '') + (c.error ? 'invalid' : '') + '"><td><input type="checkbox" data-select="' + esc(c.id) + '" aria-label="勾选 ' + esc(c.name || c.id) + '" ' + (chosen ? 'checked ' : '') + (c.error ? 'disabled' : '') + '></td><td><button class="case-title" data-expand="' + esc(c.id) + '" aria-expanded="' + (expanded === String(c.id)) + '">' + esc(c.name || c.id) + '</button><div class="case-id">' + esc(c.id) + '</div>' + (c.error ? '<div class="case-error">' + esc(c.error) + '</div>' : '') + '</td><td class="method-col"><span class="method">' + esc(c.method || '未提供') + '</span></td><td class="result-col"><span class="result ' + (latest === 'passed' ? 'pass' : failed(latest) ? 'fail' : 'muted') + '">' + (symbols[latest] || '○') + ' ' + esc(titleOf(latest)) + '</span></td><td><button class="row-arrow" data-expand="' + esc(c.id) + '" aria-label="查看 ' + esc(c.name || c.id) + ' 详情">' + (expanded === String(c.id) ? '⌄' : '›') + '</button></td></tr>' + (expanded === String(c.id) ? '<tr class="details-row"><td colspan="5"><div class="details-content"><p>' + esc(c.description || '此场景未提供说明。') + '</p><span>' + esc(c.checkpointCount == null ? '检查点数量未提供' : c.checkpointCount + ' 个检查点') + '</span></div></td></tr>' : '');
    }).join('') || '<tr><td colspan="5" class="empty-search">' + (array(state.cases).length ? '没有匹配的场景。' : '没有可用场景，请检查服务的资产目录。') + '</td></tr>');
    $('case-total').textContent = array(state.cases).length + ' 项';
    $('selection-count').textContent = '已选 ' + selected.size + ' 项'; $('start-count').textContent = selected.size;
    $('select-all').checked = !!valid.length && valid.every(c => selected.has(String(c.id)));
    $('select-all').indeterminate = valid.some(c => selected.has(String(c.id))) && !$('select-all').checked;
    disable('select-all', !valid.length); disable('clear-selection', !selected.size);
    disable('start-run', !selected.size || taskBusy() || recordingBusy() || !online || busy('run') || !state.config.application);
    $('start-run').firstChild.textContent = busy('run') ? '正在启动 ' : '开始回放 ';
    $('library-busy-note').textContent = recordingBusy() ? '请先停止并保存当前录制。' : taskBusy() ? replayTask() ? '当前回放未结束，可在回放记录中查看。' : taskAction() + '，请稍候。' : !state.config.application ? '请先配置被测程序。' : '';
    $('case-library-caption').textContent = state.config.assetsRoot ? '资产目录：' + state.config.assetsRoot : '场景以运行机器上的资产目录为准。';
  }
  function resultRows() {
    const byId = new Map();
    const add = (item, fallback) => {
      if (typeof item === 'string') item = {id: item};
      if (!item || item.id == null) return;
      const id = String(item.id); byId.set(id, {...(byId.get(id) || {id, status: fallback || 'waiting'}), ...item});
    };
    const task = state.task || {};
    const history = array(state.history).find(h => String(h.id) === String(viewedRun));
    array(currentTaskViewed() ? task.plannedCases : history && history.plannedCases).forEach(i => add(i, 'waiting'));
    if (currentTaskViewed()) array(task.rows).forEach(i => add(i));
    array(result && result.cases).forEach(i => add(i));
    array(result && result.checkpoints).forEach(cp => {
      const id = cp.case == null || cp.case === '' ? '__recording__' : String(cp.case);
      if (!byId.has(id)) byId.set(id, {id, status: 'not_verified'});
    });
    return Array.from(byId.values());
  }
  function checkpointsFor(id) {
    return array(result && result.checkpoints).filter(cp => String(cp.case == null || cp.case === '' ? '__recording__' : cp.case) === String(id));
  }
  function checkpointStatus(cp) {
    if (!cp) return 'not_verified';
    const automatic = statusOf(cp.automaticStatus), comparison = statusOf(cp.comparisonStatus);
    if (failed(automatic) || failed(comparison) || comparison === 'different') return 'failed';
    if (['pending', 'comparing', 'processing'].includes(automatic) || ['pending', 'comparing', 'processing'].includes(comparison)) return 'pending';
    return cp.automaticStatus ? automatic : comparison === 'identical' ? 'passed' : cp.comparisonStatus && comparison !== 'unavailable' ? comparison : 'not_verified';
  }
  function checkpointKind(cp) {
    const kind = String(cp.kind || '');
    if (kind === 'verify.db' || kind === 'db') return '完整 DB 字节比较';
    if (cp.expectedURL || cp.actualURL || kind.includes('screenshot')) return '截图对比';
    if (kind === 'artifact' || kind === 'file') return '输出文件检查';
    if (kind === 'action') return '操作检查';
    return '业务检查';
  }
  function checkpointDetails(cp) {
    return {kind: cp.kind, automaticStatus: cp.automaticStatus, comparisonStatus: cp.comparisonStatus, details: cp.details, comparisonMetadata: cp.comparisonMetadata};
  }
  function selectedCheckpoint() { return checkpointsFor(inspected).find(cp => String(cp.id) === checkpointId) || null; }
  function renderRun() {
    const task = state.task || {}, isCurrent = currentTaskViewed(), rows = resultRows();
    const running = isCurrent && taskBusy(), total = Number(isCurrent ? task.total : rows.length) || rows.length;
    const checked = rows.filter(r => passed(r) || failed(r)).length;
    const replayed = rows.filter(r => ['passed', 'failed', 'error', 'completed', 'pending', 'comparing', 'not_verified'].includes(statusOf(r))).length;
    const nPassed = rows.filter(passed).length, nFailed = rows.filter(failed).length;
    const stateStatus = result ? statusOf(result) : isCurrent ? statusOf(task.state) : 'pending';
    const stopped = !!(result && result.summary && result.summary.cancelled) || (isCurrent && task.state === 'cancelled');
    $('run-title').textContent = isCurrent && task.kind === 'replay' ? '录制回放检查' : '回放结果';
    $('batch-label').textContent = viewedRun || '结果尚未生成'; $('batch-size').textContent = total ? total + ' 个场景' : '';
    $('queue-count').textContent = rows.length + ' 项';
    $('replay-count').textContent = replayed + ' / ' + total; $('compare-count').textContent = checked + ' / ' + total;
    $('pass-count').textContent = nPassed; $('fail-count').textContent = nFailed;
    let headline = titleOf(stateStatus), subtitle = running ? '浏览器断开后，服务器仍会继续执行。' : '结果以自动验收与比较状态为准。';
    if (running && task.state === 'cancelling') { headline = '正在停止并清理应用'; subtitle = '请等待清理结束，期间不会启动新任务。'; }
    else if (running && task.state === 'paused') { headline = '已暂停，可以查看结果'; subtitle = '继续后执行下一项；当前查看的结果不会自动切换。'; }
    else if (running) { headline = (task.kind === 'replay' ? '正在回放检查' : '正在回放') + (task.case ? ' · ' + (caseById(task.case)?.name || task.case) : ''); subtitle = (task.caseIndex ? '场景 ' + task.caseIndex + ' / ' + total + ' · ' : '') + (task.totalSteps ? '步骤 ' + (task.step || 0) + ' / ' + task.totalSteps : '等待执行状态'); }
    else if (stopped) { headline = '本次回放已停止'; subtitle = '已完成结果保留；未运行和未完成项不会计为通过。'; }
    else if (stateStatus === 'pending' || array(result && result.checkpoints).some(cp => checkpointStatus(cp) === 'pending')) { headline = '执行结束，比较尚未完成'; subtitle = '待比较项不会计为通过；结果会自动更新。'; }
    if (isCurrent && task.kind === 'replay') subtitle += ' 录制诊断不等于正式场景验收。';
    $('live-title').textContent = headline; $('live-subtitle').textContent = subtitle;
    $('live-symbol').textContent = running ? task.state === 'paused' ? 'Ⅱ' : '▶' : stopped ? '−' : symbols[stateStatus] || '○';
    $('pause-run').hidden = !running || task.kind === 'replay' || task.state === 'cancelling'; $('pause-run').textContent = task.state === 'paused' ? '继续回放' : task.pauseAfterEach ? '逐项暂停已开启' : '本项后暂停';
    disable('pause-run', busy('pause') || busy('resume') || !online || (task.state !== 'paused' && task.pauseAfterEach));
    $('stop-run').hidden = !running; disable('stop-run', busy('cancel') || !online || task.state === 'cancelling');
    $('retry-run').hidden = !nFailed || taskBusy() || (isCurrent && task.kind === 'replay'); disable('retry-run', recordingBusy() || busy('run') || !online);
    const pct = total ? Math.min(100, checked / total * 100) : 0;
    $('progress-fill').style.width = pct + '%'; $('run-progress').setAttribute('aria-valuenow', Math.round(pct));
    const executionError = (result && result.error) || (isCurrent && task.error) || '';
    setError('result-error', resultError || (stopped && executionError === 'Replay cancelled by caller' ? '' : executionError));
    if (!rows.some(r => String(r.id) === inspected)) { inspected = rows[0] ? String(rows[0].id) : null; checkpointId = null; }
    patch('queue-list', rows.map((r, index) => {
      const id = String(r.id), status = statusOf(r), name = caseById(id)?.name || r.name || (id === '__recording__' ? '录制检查点' : id);
      return '<button class="queue-item ' + (id === inspected ? 'inspected' : '') + '" data-inspect="' + esc(id) + '" data-status="' + esc(status) + '" aria-pressed="' + (id === inspected) + '"><span class="queue-symbol">' + (symbols[status] || '○') + '</span><span><span class="queue-name">' + esc(name) + '</span><span class="queue-caption"><span>' + esc(titleOf(status)) + '</span><span>' + String(index + 1).padStart(2, '0') + '</span></span></span></button>';
    }).join('') || '<div class="record-point-empty">暂时没有场景结果。</div>');
    $('follow-current').hidden = !running || !task.case;
    $('run-log').textContent = array(isCurrent ? task.logs : result && result.logs).map(line => typeof line === 'string' ? line : json(line)).join('\n');
    $('run-output').textContent = isCurrent && task.output ? '运行机器上的输出目录：' + task.output : '';
    $('run-summary').textContent = json(result && (result.summary || {status: result.status, error: result.error}));
    renderEvidence();
  }
  function evidenceEmpty(title, message, symbol = '○') {
    return '<div class="evidence-empty"><div class="empty-emblem" aria-hidden="true">' + esc(symbol) + '</div><h3>' + esc(title) + '</h3><p>' + esc(message) + '</p></div>';
  }
  function renderEvidence() {
    const row = resultRows().find(r => String(r.id) === inspected), cps = checkpointsFor(inspected);
    const name = inspected === '__recording__' ? '录制检查点' : caseById(inspected)?.name || row?.name || inspected || '选择一个场景';
    $('evidence-title').textContent = name; $('evidence-id').textContent = inspected && inspected !== '__recording__' ? inspected : '';
    const rowStatus = row ? statusOf(row) : 'not_verified'; $('evidence-status').textContent = titleOf(rowStatus); $('evidence-status').className = 'status-pill ' + rowStatus;
    if (!cps.some(cp => String(cp.id) === checkpointId)) { checkpointId = String((cps.find(cp => failed(checkpointStatus(cp))) || cps[0] || {}).id || ''); }
    patch('checkpoint-list', cps.map((cp, index) => {
      const status = checkpointStatus(cp);
      return '<button class="point ' + (String(cp.id) === checkpointId ? 'active' : '') + '" data-point="' + esc(cp.id) + '" data-result="' + (failed(status) ? 'failed' : '') + '" aria-label="' + esc((cp.label || '检查点 ' + (index + 1)) + '，' + titleOf(status)) + '"><span class="point-symbol">' + (symbols[status] || '○') + '</span><span class="point-label">' + esc(cp.label || '检查点 ' + (index + 1)) + '</span></button>';
    }).join(''));
    const cp = selectedCheckpoint();
    if (!cp) {
      const message = row?.error || (pending(rowStatus) ? '执行与比较进行中，产生检查点后会在这里显示。' : '此场景还没有可查看的检查点证据。');
      setEvidence(evidenceEmpty(titleOf(rowStatus), message)); $('review-panel').hidden = true; return;
    }
    const cpStatus = checkpointStatus(cp), actual = safeURL(cp.actualURL), expected = safeURL(cp.expectedURL), diff = safeURL(cp.rawDiffURL || cp.diffURL);
    let content = '<div class="result-description"><span>' + esc(cp.label || cp.kind || '检查点') + '</span><span class="' + (passed(cpStatus) ? 'pass' : failed(cpStatus) ? 'fail' : 'muted') + '">' + esc(titleOf(cpStatus)) + '</span></div>';
    if (actual || expected || diff) {
      const image = (caption, url) => '<div><div class="image-caption"><span>' + esc(caption) + '</span></div><div class="image-box">' + (url ? '<img src="' + esc(url) + '" alt="' + esc(caption) + '" data-zoom-image loading="lazy">' : '<div class="record-point-empty">暂无' + esc(caption) + '</div>') + '</div></div>';
      content += '<div class="evidence-tools"><div class="segmented" aria-label="截图查看方式"><button data-image-mode="pair" class="' + (imageMode === 'pair' ? 'active' : '') + '">并排对比</button><button data-image-mode="actual" class="' + (imageMode === 'actual' ? 'active' : '') + '">本次截图</button><button data-image-mode="diff" class="' + (imageMode === 'diff' ? 'active' : '') + '">原始差异</button></div><button class="quiet" data-open-zoom>原尺寸 ↗</button></div>';
      if (imageMode === 'pair') content += '<div class="image-pair">' + image('标准图', expected) + image('本次截图', actual) + '</div>';
      else content += '<div class="image-pair single">' + image(imageMode === 'diff' ? '原始差异' : '本次截图', imageMode === 'diff' ? diff : actual) + '</div>';
      content += '<div class="image-note"><span>查看缩放不改变自动像素比较。</span><span>自动比较：' + esc(titleOf(cp.comparisonStatus || cp.automaticStatus)) + '</span></div>';
    } else {
      content += '<div class="data-evidence"><div class="data-row"><span>检查类型</span><span>' + esc(checkpointKind(cp)) + '</span></div><div class="data-row"><span>自动验收</span><span>' + esc(titleOf(cp.automaticStatus)) + '</span></div><div class="data-row"><span>比较状态</span><span>' + esc(titleOf(cp.comparisonStatus)) + '</span></div></div>';
    }
    if (cp.message) content += '<p class="evidence-message">' + esc(cp.message) + '</p>';
    const evidence = array(cp.evidence).map(item => ({...item, safe: safeURL(item.url)})).filter(item => item.safe);
    if (evidence.length) content += '<div class="evidence-links">' + evidence.map(item => '<a href="' + esc(item.safe) + '" target="_blank" rel="noopener">' + esc(item.name || '证据文件') + ' ↗</a>').join('') + '</div>';
    if (cp.kind || cp.details != null || cp.comparisonMetadata != null) content += '<details class="raw-details"><summary>原始数据与比较详情</summary><pre>' + esc(json(checkpointDetails(cp))) + '</pre><button class="quiet" data-copy-details>复制完整数据</button></details>';
    setEvidence(content); renderReview(cp);
  }
  function setEvidence(html) {
    if (html !== renderedEvidence) { $('evidence-content').innerHTML = html; renderedEvidence = html; }
  }
  function renderReview(cp) {
    $('review-panel').hidden = !cp.canReview;
    const key = String(viewedRun) + ':' + cp.id;
    if (key !== reviewKey) { reviewKey = key; reviewDirty = false; }
    if (!reviewDirty && document.activeElement !== $('review-note') && document.activeElement !== $('review-decision')) {
      $('review-decision').value = ['pending', 'accepted', 'defect'].includes(cp.decision) ? cp.decision : 'pending';
      $('review-note').value = cp.note || cp.reviewNote || '';
    }
    disable('save-review', busy('review') || !online || !cp.canReview);
  }
  function renderRecorder() {
    const recording = state.recording || {}, active = !!recording.active, unsaved = !!recording.pendingSave;
    $('record-connection-state').innerHTML = '<span class="dot" aria-hidden="true"></span>' + (recording.connected ? '已连接' : '未连接');
    $('record-connection-state').className = 'record-connection-tag ' + (recording.connectionError ? 'error' : recording.connected ? 'connected' : '');
    $('record-connection-caption').textContent = recording.connectionError || (recording.connected ? '已连接运行机器上的应用。' : '先在运行机器上打开应用，再连接到录制会话。');
    $('record-connect').textContent = busy('connect') ? '连接中…' : recording.connected ? '重新连接' : '连接应用';
    disable('record-connect', busy('connect') || taskBusy() || !online);
    if (document.activeElement !== $('record-port') && !busy('connect')) $('record-port').value = state.config.recordPort || $('record-port').value || 9999;
    $('record-form').hidden = active || unsaved; $('record-session').hidden = !active && !unsaved;
    disable('record-start', !recording.connected || taskBusy() || active || unsaved || busy('record-start') || !online);
    $('record-start').lastChild.textContent = busy('record-start') ? '正在开始…' : '开始录制';
    $('record-form-note').textContent = taskBusy() ? replayTask() ? '当前回放任务未结束，请先结束回放再录制。' : taskAction() + '，完成后可继续操作。' : '留空则按时间命名。建议从选择机型、导入模型等初始步骤开始录制，便于独立实例重放。';
    $('record-session-name').textContent = recording.name || '当前录制';
    $('record-duration').textContent = active ? duration(recording.startedAt ? Date.now() / 1000 - Number(recording.startedAt) : 0) : '已停止';
    $('record-session-state').textContent = active ? '正在记录 GPlatform 中的操作' : '录制已停止，等待保存';
    $('record-save').textContent = busy('record-stop') ? '保存中…' : unsaved && !active ? '保存录制' : '停止并保存';
    disable('record-save', busy('record-stop') || taskBusy() || !online); $('record-add-point').hidden = !active;
    disable('record-add-point', busy('record-point') || taskBusy() || !online);
    const cps = array(recording.checkpoints); $('record-point-count').textContent = cps.length + ' 项';
    patch('record-point-list', cps.map((cp, index) => '<div class="record-point"><span class="muted">' + String(index + 1).padStart(2, '0') + '</span><span class="point-kind">' + (String(cp.kind).includes('db') ? 'DB 对象数据' : '截图') + '</span><div class="record-point-title">' + esc(cp.label || '候选检查点') + '</div><small>' + duration(Number(cp.atMs || 0) / 1000) + '</small></div>').join('') || '<div class="record-point-empty">还没有检查点。到达需要核对的画面时，添加一个。</div>');
    $('record-history-count').textContent = array(state.records).length + ' 项';
    patch('record-history-list', array(state.records).map(record => '<div class="record-history-row"><div><button data-record-history="' + esc(record.id) + '">' + esc(record.name || record.id) + '</button><small>' + esc(record.error || date(record.mtime)) + '</small></div><div class="record-history-meta"><span>' + esc(record.checkpointCount == null ? '' : record.checkpointCount + ' 个检查点') + '</span><button data-record-history="' + esc(record.id) + '">查看 ↗</button></div></div>').join('') || '<div class="record-point-empty">还没有已保存的录制。</div>');
    const chosen = recordById(selectedRecord); $('record-detail').hidden = !chosen;
    if (chosen) { $('record-detail-name').textContent = chosen.name || chosen.id; $('record-detail-caption').textContent = chosen.error || '已保存 · ' + date(chosen.mtime) + ' · ' + (chosen.checkpointCount || 0) + ' 个候选检查点'; }
    disable('record-replay', !chosen || !!chosen.error || recordingBusy() || taskBusy() || busy('record-replay') || !online);
    disable('record-point-save', busy('record-point') || taskBusy() || !active || !online);
  }
  function renderHistory() {
    disable('open-result-submit', !authenticated || !online || busy('open-result'));
    $('open-result-submit').textContent = busy('open-result') ? '正在打开…' : '打开结果 ↗';
    patch('history-list', array(state.history).length ? '<div class="history-table"><table><thead><tr><th>回放批次</th><th>状态</th><th>场景</th><th>创建时间</th><th></th></tr></thead><tbody>' + array(state.history).map(item => '<tr><td><button class="case-title" data-history="' + esc(item.id) + '">' + esc(item.name || item.id) + '</button><div class="mono">' + esc(item.id) + '</div></td><td><span class="status-pill ' + esc(statusOf(item)) + '">' + esc(titleOf(statusOf(item))) + '</span></td><td><span class="method">' + esc(item.completedCaseCount == null ? '—' : item.completedCaseCount) + ' / ' + esc(item.plannedCaseCount == null ? array(item.plannedCases).length : item.plannedCaseCount) + '</span></td><td><span class="method">' + esc(date(item.createdAt)) + '</span></td><td><button class="quiet" data-history="' + esc(item.id) + '">查看 ↗</button></td></tr>').join('') + '</tbody></table></div>' : '<div class="history-empty"><h2>还没有回放记录。</h2><p>选择一组场景，开始第一次回放。</p><button class="primary" data-go-library>选择场景 ↗</button></div>');
  }
  async function refreshState() {
    const sequence = ++stateRequest;
    const incoming = await api('/api/state');
    if (sequence < appliedStateRequest) return state;
    appliedStateRequest = sequence;
    if (!online) renderedEvidence = '';
    state = {...incoming, config: incoming.config || {}, cases: array(incoming.cases), task: incoming.task || {}, recording: incoming.recording || {}, records: array(incoming.records), history: array(incoming.history)};
    const savedSession = state.recording.sessionId;
    if (savedSession && state.task.kind === 'record-stop' && state.task.state === 'completed' && !recordingBusy() && autoSelectedSession !== String(savedSession) && recordById(savedSession)) {
      selectedRecord = String(savedSession); autoSelectedSession = String(savedSession);
    }
    authenticated = true; online = true; $('application').hidden = false; $('login-page').hidden = true; $('connection-banner').hidden = true;
    const ids = new Set(state.cases.filter(c => !c.error).map(c => String(c.id))); selected = new Set([...selected].filter(id => ids.has(id)));
    if (!configDirty && $('config-modal').hidden) fillConfig();
    if (!booted) {
      booted = true;
      if (taskBusy() && replayTask() && resultIdForTask()) { viewedRun = resultIdForTask(); showPage('run'); }
      else if (taskBusy() && !replayTask()) showPage('record');
      else if (recordingBusy()) showPage('record');
    }
    render();
    return state;
  }
  function resultPending() { return !result || pending(statusOf(result)) || array(result.checkpoints).some(cp => checkpointStatus(cp) === 'pending'); }
  async function refreshResult(force = false) {
    if (!viewedRun || (!force && !currentTaskViewed() && !resultPending())) return;
    const wanted = viewedRun, epoch = requestEpoch;
    try {
      const incoming = await api('/api/result?id=' + encodeURIComponent(wanted));
      if (wanted !== viewedRun || epoch !== requestEpoch) return;
      result = incoming; resultError = ''; if (page === 'run') renderRun();
    } catch (error) {
      if (wanted !== viewedRun || epoch !== requestEpoch) return;
      if (!error.auth) { resultError = error.message; if (page === 'run') renderRun(); }
    }
  }
  async function poll() {
    if (polling || authBlocked) return;
    polling = true;
    try { await refreshState(); if (page === 'run' || resultPending()) await refreshResult(); }
    catch (error) {
      online = false;
      if (!error.auth && authenticated) { $('connection-banner').textContent = '与运行机器的连接中断，正在重新连接。服务器任务不会因此停止，操作不会自动重发。'; $('connection-banner').hidden = false; render(); }
      else if (!authenticated && !error.auth) setError('login-error', '无法连接工作台服务，请确认服务仍在运行。');
    } finally { polling = false; clearTimeout(pollTimer); if (!authBlocked) pollTimer = setTimeout(poll, 1000); }
  }
  async function viewRun(id) {
    if (!id) { showPage('history'); return; }
    if (viewedRun !== String(id)) { viewedRun = String(id); result = null; resultError = ''; inspected = null; checkpointId = null; renderedEvidence = ''; requestEpoch++; }
    showPage('run'); await refreshResult(true);
  }
  async function startRun(ids) {
    if (!ids.length || taskBusy() || recordingBusy()) return;
    const response = await command('run', '/api/run', {cases: ids, pauseAfterEach: $('review-each').checked});
    if (!response) return;
    try { await refreshState(); await viewRun(resultIdForTask()); } catch (error) { toast(error.message); }
  }
  function fillConfig() {
    $('config-application').value = state.config.application || ''; $('config-exports').value = state.config.exports || '';
    $('config-assets').textContent = state.config.assetsRoot || '服务未提供资产目录'; configDirty = false;
  }
  function configure() { fillConfig(); setError('config-error', ''); openModal('config-modal', 'config-application'); }
  function inspect(id) { inspected = String(id); checkpointId = null; imageMode = 'pair'; renderedEvidence = ''; renderRun(); }
  function imageSources() {
    const cp = selectedCheckpoint(); if (!cp) return [];
    const values = imageMode === 'pair' ? [['标准图', cp.expectedURL], ['本次截图', cp.actualURL]] : imageMode === 'actual' ? [['本次截图', cp.actualURL]] : [['原始差异', cp.rawDiffURL || cp.diffURL]];
    return values.map(([name, url]) => ({name, url: safeURL(url)})).filter(item => item.url);
  }
  function renderZoom() {
    const transform = 'translate(calc(-50% + ' + zoom.x + 'px),calc(-50% + ' + zoom.y + 'px)) scale(' + zoom.scale + ')';
    $('zoom-body').querySelectorAll('img').forEach(img => { img.style.transform = transform; }); $('zoom-scale').textContent = Math.round(zoom.scale * 100) + '%';
  }
  function openZoom() {
    const sources = imageSources(); if (!sources.length) { toast('此检查点尚无可查看的图片。'); return; }
    zoom.scale = 1; zoom.x = 0; zoom.y = 0; zoom.pointer = null;
    $('zoom-title').textContent = selectedCheckpoint()?.label || '原尺寸证据'; $('zoom-body').className = 'zoom-grid ' + (sources.length === 1 ? 'single' : '');
    $('zoom-body').innerHTML = sources.map(item => '<div class="zoom-pane"><span class="zoom-pane-label">' + esc(item.name) + '</span><img src="' + esc(item.url) + '" alt="' + esc(item.name) + '" draggable="false"></div>').join('');
    openModal('zoom-dialog', 'close-zoom'); renderZoom();
  }
  function changeZoom(factor) { zoom.scale = Math.min(8, Math.max(0.1, zoom.scale * factor)); renderZoom(); }

  function updateLoginCooldown() {
    clearTimeout(loginRetryTimer);
    const remaining = Math.max(0, Math.ceil((loginRetryAt - Date.now()) / 1000));
    disable('login-submit', busy('login') || remaining > 0);
    $('login-submit').firstChild.textContent = remaining ? remaining + '秒后重试 ' : '进入工作台 ';
    if (remaining) {
      setError('login-error', '尝试过多，请' + remaining + '秒后重试。');
      loginRetryTimer = setTimeout(updateLoginCooldown, 1000);
    } else if (loginRetryAt) {
      loginRetryAt = 0; setError('login-error', '');
    }
  }
  $('login-form').addEventListener('submit', async event => {
    event.preventDefault(); if (busy('login') || Date.now() < loginRetryAt) return;
    const token = $('access-token').value.trim();
    if (!/^[0-9]{6}$/.test(token)) { setError('login-error', '请输入6位数字连接码。'); return; }
    commands.add('login'); disable('login-submit', true); setError('login-error', '');
    try {
      await api('/api/login', {method: 'POST', headers: {'Content-Type': 'application/json'}, body: JSON.stringify({token, commandId: commandId()})});
      authBlocked = false; loginRetryAt = 0; $('access-token').value = ''; await refreshState();
    } catch (error) {
      if (error.status === 429) loginRetryAt = Date.now() + Math.max(1, error.retryAfter) * 1000;
      setError('login-error', error.message);
    }
    finally { commands.delete('login'); updateLoginCooldown(); if (!authBlocked) { clearTimeout(pollTimer); poll(); } }
  });
  $('library-nav').onclick = () => showPage('library'); $('record-nav').onclick = () => showPage('record'); $('runs-nav').onclick = () => showPage('history');
  $('back-library').onclick = () => showPage('library'); $('record-live-nav').onclick = () => showPage('record'); $('active-task-nav').onclick = () => replayTask() ? viewRun(resultIdForTask()) : showPage('record');
  $('case-search').oninput = event => { query = event.target.value; renderCases(); };
  $('case-list').onchange = event => { const id = event.target.dataset.select; if (id) { event.target.checked ? selected.add(id) : selected.delete(id); renderCases(); } };
  $('case-list').onclick = event => { const button = event.target.closest('[data-expand]'); if (button) { expanded = expanded === button.dataset.expand ? null : button.dataset.expand; renderCases(); } };
  $('select-all').onchange = event => { visibleCases().filter(c => !c.error).forEach(c => event.target.checked ? selected.add(String(c.id)) : selected.delete(String(c.id))); renderCases(); };
  $('clear-selection').onclick = () => { selected.clear(); renderCases(); }; $('start-run').onclick = () => startRun([...selected]);
  $('queue-list').onclick = event => { const button = event.target.closest('[data-inspect]'); if (button) inspect(button.dataset.inspect); };
  $('checkpoint-list').onclick = event => { const button = event.target.closest('[data-point]'); if (button) { checkpointId = button.dataset.point; imageMode = 'pair'; renderEvidence(); } };
  $('pause-run').onclick = async () => { const resume = state.task.state === 'paused'; await command(resume ? 'resume' : 'pause', resume ? '/api/resume' : '/api/pause'); await poll(); };
  $('stop-run').onclick = async () => { await command('cancel', '/api/cancel'); await poll(); };
  $('retry-run').onclick = () => startRun(resultRows().filter(failed).map(r => String(r.id)).filter(id => caseById(id)));
  $('follow-current').onclick = () => { if (state.task.case) inspect(state.task.case); };
  $('history-list').onclick = event => { const button = event.target.closest('[data-history]'); if (button) viewRun(button.dataset.history); else if (event.target.closest('[data-go-library]')) showPage('library'); };
  $('open-result-form').onsubmit = async event => {
    event.preventDefault();
    if (!authenticated || !online || busy('open-result')) return;
    const path = $('open-result-path').value.trim();
    if (!path) { setError('open-result-error', '请输入运行机器上的结果目录。'); return; }
    const response = await command('open-result', '/api/result/open', {path}, 'open-result-error');
    if (response && response.id) await viewRun(String(response.id));
    else if (response) setError('open-result-error', '服务未返回结果标识，请检查该目录的报告。');
  };
  $('open-config').onclick = configure; $('environment-config').onclick = configure;
  $('config-close').onclick = () => closeModal('config-modal'); $('config-cancel').onclick = () => closeModal('config-modal');
  $('config-application').oninput = $('config-exports').oninput = () => { configDirty = true; };
  $('config-form').onsubmit = async event => { event.preventDefault(); const response = await command('config', '/api/config', {application: $('config-application').value.trim(), exports: $('config-exports').value.trim()}, 'config-error'); if (response) { configDirty = false; closeModal('config-modal'); await poll(); } };
  $('record-connect').onclick = async () => { const port = Number($('record-port').value); if (!Number.isInteger(port) || port < 1 || port > 65535) { toast('请输入 1 到 65535 之间的本机端口。'); return; } await command('connect', '/api/connect', {port}); await poll(); };
  $('record-start').onclick = async () => { const response = await command('record-start', '/api/record/start', {name: $('record-name').value.trim()}); if (response) $('record-name').value = ''; await poll(); };
  $('record-save').onclick = async () => { const response = await command('record-stop', '/api/record/stop', {name: state.recording.name || ''}); if (response) toast('保存请求已发送，录制状态与已保存列表会自动更新。'); await poll(); };
  $('record-add-point').onclick = () => { $('record-point-form').reset(); $('record-db-field').hidden = true; setError('record-point-error', ''); openModal('record-point-modal', 'record-point-kind'); };
  $('record-point-kind').onchange = event => { $('record-db-field').hidden = event.target.value !== 'db'; setError('record-point-error', ''); };
  $('record-point-close').onclick = $('record-point-cancel').onclick = () => closeModal('record-point-modal');
  $('record-point-form').onsubmit = async event => {
    event.preventDefault(); const kind = $('record-point-kind').value, dbInstanceId = $('record-db-id').value.trim();
    if (kind === 'db' && !/^-?[1-9]\d*$/.test(dbInstanceId)) { setError('record-point-error', '请输入非零规范十进制对象 ID，不含 + 号或前导零。'); $('record-db-id').focus(); return; }
    const response = await command('record-point', '/api/record/checkpoint', {kind, label: $('record-point-note').value.trim(), ...(kind === 'db' ? {dbInstanceId} : {})}, 'record-point-error');
    if (response) closeModal('record-point-modal'); await poll();
  };
  $('record-history-list').onclick = event => { const button = event.target.closest('[data-record-history]'); if (button) { selectedRecord = button.dataset.recordHistory; renderRecorder(); $('record-detail').scrollIntoView({behavior: 'smooth', block: 'nearest'}); } };
  $('record-replay').onclick = async () => { if (!selectedRecord) return; const response = await command('record-replay', '/api/record/replay', {id: selectedRecord}); if (response) await viewRun(resultIdForTask()); };
  $('evidence-content').onclick = async event => {
    const mode = event.target.closest('[data-image-mode]');
    if (mode) { imageMode = mode.dataset.imageMode; renderEvidence(); }
    else if (event.target.closest('[data-open-zoom]') || event.target.closest('[data-zoom-image]')) openZoom();
    else if (event.target.closest('[data-copy-details]')) {
      const cp = selectedCheckpoint(); if (!cp) return;
      try { await navigator.clipboard.writeText(json(checkpointDetails(cp))); toast('已复制完整数据。'); } catch (_) { toast('浏览器未允许复制，可展开原始数据手动复制。'); }
    }
  };
  $('evidence-content').addEventListener('error', event => { if (event.target.tagName === 'IMG') { const message = document.createElement('span'); message.className = 'image-failure'; message.textContent = '图片未能加载。请检查连接，或打开证据文件。'; event.target.replaceWith(message); } }, true);
  $('review-note').oninput = $('review-decision').onchange = () => { reviewDirty = true; };
  $('review-form').onsubmit = async event => { event.preventDefault(); const cp = selectedCheckpoint(); if (!cp || !cp.canReview) return; const response = await command('review', '/api/review', {run: viewedRun, id: cp.id, decision: $('review-decision').value, note: $('review-note').value}); if (response) { reviewDirty = false; toast('审核意见已保存，自动验收结论保持原值。'); await refreshResult(true); } };
  $('close-zoom').onclick = () => closeModal('zoom-dialog'); $('zoom-in').onclick = () => changeZoom(1.25); $('zoom-out').onclick = () => changeZoom(0.8); $('zoom-reset').onclick = () => { zoom.scale = 1; zoom.x = 0; zoom.y = 0; renderZoom(); };
  $('zoom-body').addEventListener('wheel', event => { if (!event.target.closest('.zoom-pane')) return; event.preventDefault(); changeZoom(event.deltaY < 0 ? 1.1 : 1 / 1.1); }, {passive: false});
  $('zoom-body').addEventListener('pointerdown', event => { const pane = event.target.closest('.zoom-pane'); if (!pane || event.button !== 0) return; zoom.pointer = {id: event.pointerId, x: event.clientX, y: event.clientY, startX: zoom.x, startY: zoom.y}; pane.setPointerCapture(event.pointerId); });
  $('zoom-body').addEventListener('pointermove', event => { if (!zoom.pointer || zoom.pointer.id !== event.pointerId) return; zoom.x = zoom.pointer.startX + event.clientX - zoom.pointer.x; zoom.y = zoom.pointer.startY + event.clientY - zoom.pointer.y; renderZoom(); });
  ['pointerup', 'pointercancel', 'lostpointercapture'].forEach(name => $('zoom-body').addEventListener(name, () => { zoom.pointer = null; }));
  document.addEventListener('keydown', event => {
    const modal = ['zoom-dialog', 'record-point-modal', 'config-modal'].map($).find(el => !el.hidden); if (!modal) return;
    if (event.key === 'Escape') { event.preventDefault(); closeModal(modal.id); return; }
    if (event.key !== 'Tab') return;
    const elements = Array.from(modal.querySelectorAll('button,input,select,textarea,a[href]')).filter(el => !el.disabled && el.getClientRects().length);
    if (!elements.length) return; const first = elements[0], last = elements[elements.length - 1];
    if (event.shiftKey && document.activeElement === first) { event.preventDefault(); last.focus(); }
    else if (!event.shiftKey && document.activeElement === last) { event.preventDefault(); first.focus(); }
  });

  poll();
})();
