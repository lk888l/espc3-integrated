'use strict';
const $ = id => document.getElementById(id);
const styles = ['灵动双眼', '像素猫', '月兔', '糯米史莱姆', '自定义'];
const actions = {idle:'待机', blink:'眨眼', happy:'开心', eat:'进食', sleep:'睡觉',
                 surprised:'惊讶', sad:'难过', dance:'跳舞'};
const errors = {invalid_code:'配对码已失效或输入有误，请在设备上按 Up 重新配对。',
    sessions_full:'配对已满，可先退出一个客户端，或在设备 Network 页面按 Down 撤销全部配对。',
    pair_required:'会话已结束，请重新配对。', pet_inactive:'请先选择桌宠造型。',
    style_unavailable:'设备没有安装自定义动画。', invalid_wifi:'请检查 SSID 和密码的字节长度。',
    wifi_busy:'设备正在配网，请等待当前连接结果。', ui_busy:'设备正忙，请稍后操作。',
    ui_timeout:'设备尚未确认，请查看状态后再操作。', no_saved_wifi:'设备还没有配置局域网。'};
const provision = {idle:'', trying:'正在试连…', saved:'新网络已保存', failed:'试连失败，已恢复原配置',
                   storage_error:'保存失败，已恢复原配置'};
let token = '', state = null, busy = false, revision = 0, offline = false;
try { token = sessionStorage.getItem('c3-session') || ''; } catch {}
function remember(value) {
    token = value;
    revision++;
    try { if (value) sessionStorage.setItem('c3-session', value); else sessionStorage.removeItem('c3-session'); } catch {}
}
function say(message) { $('feedback').textContent = message; }
function showPairing() {
    state = null;
    $('screen').hidden = true;
    $('connected').hidden = true;
    $('pairing').hidden = false;
}
async function request(path, method = 'GET', data, binary = false) {
    const controller = new AbortController();
    const timeout = setTimeout(() => controller.abort(), 5000);
    const headers = {};
    const sentToken = token;
    if (sentToken) headers.Authorization = 'Bearer ' + sentToken;
    if (data !== undefined) headers['Content-Type'] = 'application/json';
    try {
        const response = await fetch('/api/v1/' + path, {
            method, headers, body: JSON.stringify(data), cache: 'no-store', signal: controller.signal
        });
        if (!response.ok) {
            const reason = (await response.json()).error;
            if (reason === 'pair_required' && token === sentToken) { remember(''); showPairing(); }
            const error = new Error(errors[reason] || '设备请求失败 (' + response.status + ')');
            error.reason = reason;
            throw error;
        }
        return await (binary ? response.arrayBuffer() : response.json());
    } finally { clearTimeout(timeout); }
}
function addButton(group, label, onClick) {
    const button = document.createElement('button');
    button.textContent = label;
    button.onclick = onClick;
    $(group).append(button);
    return button;
}
const styleButtons = styles.map((name, style) =>
    addButton('styles', name, () => mutate('pet', {style})));
const actionButtons = Object.entries(actions).map(([action, name]) =>
    addButton('actions', name, () => mutate('pet', {action})));
function render() {
    if (!state) return;
    const {pet, display, wifi} = state;
    $('pairing').hidden = true;
    $('connected').hidden = false;
    $('identity').textContent = state.id;
    $('petState').textContent = pet.active ? styles[pet.style] + ' · ' + actions[pet.action] : '当前页面 · ' + state.view;
    $('empty').hidden = pet.active && !$('screen').hidden;
    if (!pet.active) { $('screen').hidden = true; $('empty').textContent = '选择造型以打开桌宠'; }
    else $('empty').textContent = '正在读取画面…';
    $('closePet').disabled = busy || !pet.active;
    $('logout').disabled = busy;
    styleButtons.forEach((button, i) => {
        button.disabled = busy || (i === 4 && !pet.custom);
        button.setAttribute('aria-pressed', String(pet.active && pet.style === i));
    });
    actionButtons.forEach((button, i) => {
        button.disabled = busy || !pet.active;
        button.setAttribute('aria-pressed', String(pet.active && Object.keys(actions)[i] === pet.action));
    });
    for (const [key, value] of Object.entries(display)) {
        $(key).selectedIndex = value;
        $(key).disabled = busy;
    }
    $('join').disabled = busy || wifi.provision === 'trying';
    $('reconnect').disabled = $('join').disabled || !wifi.configured;
    $('wifiStatus').textContent = [wifi.connected ? '局域网 ' + wifi.sta_ip : '局域网未连接',
        '热点 ' + wifi.ap_ip, provision[wifi.provision]].filter(Boolean).join(' · ');
}
async function run(work) {
    if (busy) return;
    busy = true; revision++; $('pairSubmit').disabled = true; render();
    try { await work(); } catch (e) {
        say(e.reason ? e.message : '未收到设备确认，请检查网络连接和设备 IP。');
    } finally { busy = false; revision++; $('pairSubmit').disabled = false; render(); }
}
function mutate(path, body) {
    return run(async () => { state = await request(path, 'POST', body); say(''); });
}
$('pairing').onsubmit = event => {
    event.preventDefault();
    run(async () => {
        const result = await request('session', 'POST', {code: $('code').value});
        remember(result.token);
        $('code').value = '';
        state = await request('status'); say('');
    });
};
$('logout').onclick = () => run(async () => {
    await request('session', 'DELETE'); remember(''); showPairing(); say('配对已退出。');
});
$('closePet').onclick = () => mutate('pet', {close:true});
for (const key of ['brightness','motion','header']) {
    $(key).onchange = () => mutate('display', {[key]: $(key).selectedIndex});
}
function join(body) {
    run(async () => {
        await request('wifi', 'POST', body);
        $('password').value = '';
        state.wifi.provision = 'trying';
        say('正在试连。网络切换后可使用设备 Network 页面显示的 IP 重新打开控制台。');
    });
}
$('wifiForm').onsubmit = event => {
    event.preventDefault();
    join({ssid:$('ssid').value, password:$('password').value});
};
$('reconnect').onclick = () => join({reconnect:true});
const context = $('screen').getContext('2d');
const pixels = context.createImageData(128, 64);
function draw(buffer) {
    if (buffer.byteLength !== 1024) return;
    const bytes = new Uint8Array(buffer);
    for (let y = 0; y < 64; y++) {
        for (let x = 0; x < 128; x++) {
            const on = bytes[(y >> 3) * 128 + x] & (1 << (y % 8));
            const p = (y * 128 + x) * 4;
            pixels.data[p] = on ? 213 : 21;
            pixels.data[p + 1] = on ? 239 : 41;
            pixels.data[p + 2] = on ? 142 : 33;
            pixels.data[p + 3] = 255;
        }
    }
    context.putImageData(pixels, 0, 0);
    $('screen').hidden = false;
    $('empty').hidden = true;
}
let lastStatus = 0;
async function poll() {
    const start = revision;
    try {
        if (!busy && !document.hidden && token) {
            if (!state || Date.now() - lastStatus >= 1500) {
                const next = await request('status');
                if (start !== revision) return;
                state = next; lastStatus = Date.now(); render();
                if (offline) { offline = false; say(''); }
            }
            if (state.pet.active) {
                const frame = await request('pet/frame', 'GET', undefined, true);
                if (start === revision) draw(frame);
            }
        }
    } catch (e) {
        if (e.reason === 'frame_unavailable') { lastStatus = 0; $('screen').hidden = true; $('empty').hidden = false; }
        else if (e.reason) say(e.message);
        else {
            offline = true; $('screen').hidden = true; $('empty').hidden = false; $('empty').textContent = '等待设备连接…';
            say('连接中断，正在重新读取设备状态。'); lastStatus = 0;
        }
    } finally { setTimeout(poll, offline ? 1500 : 350); }
}
request('info').then(info => { $('identity').textContent = info.id; }).catch(() => {});
$('pairSubmit').disabled = false;
poll();
