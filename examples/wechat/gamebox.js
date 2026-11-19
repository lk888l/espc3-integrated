// One paired device per instance. The session stays in RAM.
function gamebox(ip) {
  const parts = ip.split('.');
  if (parts.length !== 4 || parts.some(p => !/^(0|[1-9]\d{0,2})$/.test(p) || Number(p) > 255))
    throw new Error('请输入设备 Network 页上的 IPv4 地址');
  const base = 'http://' + ip + '/api/v1/';
  let token = '';
  function request(path, method = 'GET', data) {
    return new Promise((resolve, reject) => {
      const header = {};
      if (token) header.Authorization = 'Bearer ' + token;
      if (data !== undefined) header['Content-Type'] = 'application/json';
      wx.request({url: base + path, method, header, data, timeout: 5000,
        success(r) {
          if (r.statusCode >= 200 && r.statusCode < 300) resolve(r.data);
          else reject(new Error(r.data.error || 'HTTP ' + r.statusCode));
        },
        fail(e) { reject(new Error(e.errMsg)); }
      });
    });
  }
  return {
    async pair(code) { token = (await request('session', 'POST', {code: String(code)})).token; },
    status: () => request('status'),
    pet: change => request('pet', 'POST', change),
    display: change => request('display', 'POST', change),
    wifi: (ssid, password) => request('wifi', 'POST', {ssid, password}),
    async logout() { await request('session', 'DELETE'); token = ''; }
  };
}
module.exports = gamebox;
