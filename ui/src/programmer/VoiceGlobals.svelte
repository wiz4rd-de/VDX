<script lang="ts">
  // Voice globals (§5.5): feedback, osc key sync, transpose (note name, as the
  // LCD), name text field, pitch mod sens. The block header carries the
  // voice menu (R-U3).
  import { G } from '../params';
  import { menus } from '../ui.svelte';
  import Param from '../controls/Param.svelte';
  import NameField from './NameField.svelte';

  function toggleMenu(): void {
    menus.op = null;
    menus.voice = !menus.voice;
  }
</script>

<div
  class="block"
  style="left:640px;top:270px;width:220px;height:250px"
  role="group"
  aria-label="Voice"
  tabindex="-1"
  data-block="voice"
>
  <div class="head">
    <span class="title">VOICE</span>
    <button tabindex="0"
      type="button"
      class="menu-btn"
      aria-label="Voice menu"
      aria-haspopup="menu"
      aria-expanded={menus.voice}
      data-testid="voice-menu-button"
      onclick={toggleMenu}>MENU ▾</button
    >
  </div>
  <div class="rows">
    <div class="r"><span class="lbl">NAME</span><div class="c" style="width:120px"><NameField /></div></div>
    <div class="r"><span class="lbl">FEEDBACK</span><div class="c"><Param voice={G.FEEDBACK} /></div></div>
    <div class="r"><span class="lbl">OSC KEY SYNC</span><div class="c"><Param voice={G.OSC_KEY_SYNC} kind="toggle" /></div></div>
    <div class="r"><span class="lbl">TRANSPOSE</span><div class="c"><Param voice={G.TRANSPOSE} /></div></div>
    <div class="r"><span class="lbl">P MOD SENS</span><div class="c"><Param voice={G.PITCH_MOD_SENS} /></div></div>
  </div>
</div>

<style>
  .rows {
    position: absolute;
    left: 10px;
    right: 10px;
    top: 44px;
    display: flex;
    flex-direction: column;
    gap: 14px;
  }
  .r {
    display: flex;
    align-items: center;
    justify-content: space-between;
    height: 22px;
  }
  .c {
    width: 64px;
  }
</style>
