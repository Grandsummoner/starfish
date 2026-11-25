// SPDX-License-Identifier: MPL-2.0
// MIDI keyboard input for the STARFISH web emulator.
// The emulator page loads an optional "./app.js" and calls attach({ node, ac, status }).
// The workflow publishes this file as emu/app.js. Firmware and fm1-base are not touched.

const LOW = 53;        // MIDI note that plays the FM-1's lowest key (F3 = 53). Change this to shift your keyboard's range.
const KEYS = 27;       // the FM-1 has keys 0..26
const MIN_HOLD = 50;   // ms: the firmware reads keys every 16 ms, so a quick tap must last a little

export function attach({ node, status }) {
  if (!navigator.requestMIDIAccess) {
    status("MIDI keyboards need Chrome or Edge. This browser does not support Web MIDI.");
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

  navigator.requestMIDIAccess({ sysex: false }).then((access) => {
    const hook = () => access.inputs.forEach((input) => { input.onmidimessage = onMidi; });
    hook();
    access.onstatechange = (e) => {
      hook();                                                  // a keyboard plugged in later
      if (e.port && e.port.state === "disconnected") allOff(); // and none left stuck if one is unplugged
    };
  }).catch(() => status("MIDI access was blocked. Allow MIDI for this page and reload to play a keyboard."));
}
