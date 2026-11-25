// SPDX-License-Identifier: MPL-2.0
// MIDI keyboard input for the STARFISH web emulator.
// The emulator page loads an optional "./app.js" when you press Switch on, and calls attach({ node, ac, status }).
// The workflow publishes this file as emu/app.js. Firmware and fm1-base are not touched.

const LOW = 53;        // MIDI note that plays the FM-1's lowest key (F3 = 53). Change this to shift your keyboard's range.
const KEYS = 27;       // the FM-1 has keys 0..26
const MIN_HOLD = 50;   // ms: the firmware reads keys every 16 ms, so a quick tap must last a little

export function attach({ node }) {
  // a button in the row with Mute
  const btn = document.createElement("button");
  btn.type = "button";
  btn.id = "midi";
  btn.textContent = "Connect MIDI keyboard";
  const mute = document.getElementById("mute");
  if (mute) mute.after(btn); else (document.querySelector(".bar") || document.body).append(btn);

  if (!navigator.requestMIDIAccess) {
    btn.disabled = true;
    btn.textContent = "MIDI: needs Chrome or Edge";
    return;
  }

  // The page sends the on-screen / computer-keyboard keys as one bit mask. Keep that mask, add the MIDI keys to it,
  // so playing both at once works and neither cancels the other.
  const raw = node.port.postMessage.bind(node.port);
  let pageMask = 0, midiMask = 0;
  node.port.postMessage = (m, transfer) => {
    if (m && m.type === "keys") { pageMask = m.mask; m = { ...m, mask: (m.mask | midiMask) >>> 0 }; }
    return raw(m, transfer);
  };
  const push = () => raw({ type: "keys", mask: (pageMask | midiMask) >>> 0 });

  const downAt = new Map();
  function noteOn(k) {
    downAt.set(k, performance.now());
    midiMask |= 1 << k;
    push();
  }
  function noteOff(k) {
    const at = downAt.get(k);
    const go = () => {
      if (downAt.get(k) !== at) return;        // pressed again meanwhile
      midiMask &= ~(1 << k);
      push();
    };
    const wait = MIN_HOLD - (performance.now() - (at ?? 0));
    if (wait > 0) setTimeout(go, wait); else go();
  }
  function allOff() { midiMask = 0; downAt.clear(); push(); }

  function onMidi(e) {
    const [s, n, v] = e.data;
    const cmd = s & 0xf0;
    if (cmd === 0xb0 && n === 123) { allOff(); return; }      // CC 123: all notes off
    const k = n - LOW;
    if (k < 0 || k >= KEYS) return;
    if (cmd === 0x90 && v > 0) noteOn(k);
    else if (cmd === 0x80 || (cmd === 0x90 && v === 0)) noteOff(k);
  }

  let access = null;
  function refresh() {
    const names = [];
    access.inputs.forEach((input) => { input.onmidimessage = onMidi; names.push(input.name); });
    btn.textContent = names.length ? "MIDI: " + names.join(", ") : "MIDI: no keyboard found (click to look again)";
  }

  btn.addEventListener("click", async () => {
    try {
      if (!access) {
        access = await navigator.requestMIDIAccess({ sysex: false });
        access.onstatechange = (e) => {
          refresh();                                           // a keyboard plugged in or out later
          if (e.port && e.port.state === "disconnected") allOff();
        };
      }
      refresh();
    } catch {
      btn.textContent = "MIDI blocked: allow it for this site, then click again";
    }
  });
}
