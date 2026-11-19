const gamebox = require('../../gamebox');
const styles = ['灵动双眼', '像素猫', '月兔', '糯米史莱姆', '自定义'];
const actions = ['idle', 'blink', 'happy', 'eat', 'sleep', 'surprised', 'sad', 'dance'];
const actionLabels = ['待机', '眨眼', '开心', '进食', '睡觉', '惊讶', '难过', '跳舞'];
Page({
  data: {connected:false, busy:false, active:false, style:0, styles, actionLabels, summary:'', message:''},
  async run(work) {
    if (this.data.busy) return;
    this.setData({busy:true, message:''});
    try { await work(); }
    catch (e) {
      if (e.message === 'pair_required') this.setData({connected:false});
      this.setData({message:e.message});
    } finally { this.setData({busy:false}); }
  },
  show(state) {
    const p = state.pet;
    this.setData({connected:true, style:p.style, active:p.active, styles:styles.slice(0, p.custom ? 5 : 4),
      summary:state.id + ' · ' + (p.active ? styles[p.style] + ' / ' + actionLabels[actions.indexOf(p.action)] : state.view)});
  },
  connect(e) {
    const {ip, code} = e.detail.value;
    this.run(async () => {
      this.client = gamebox(ip.trim());
      await this.client.pair(code);
      this.show(await this.client.status());
    });
  },
  change(body) { this.run(async () => this.show(await this.client.pet(body))); },
  setStyle(e) { this.change({style:Number(e.currentTarget.dataset.index)}); },
  act(e) { this.change({action:actions[Number(e.currentTarget.dataset.index)]}); },
  closePet() { this.change({close:true}); },
  onShow() {
    if (this.data.connected) this.run(async () => this.show(await this.client.status()));
  },
  logout() {
    this.run(async () => { await this.client.logout(); this.setData({connected:false}); });
  }
});
