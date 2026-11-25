// SPDX-License-Identifier: MPL-2.0
// MIDI keyboard input for the STARFISH web emulator (page side).
// The emulator page loads an optional "./app.js" when you press Switch on, and calls attach({ node, ac, status }).
// The workflow publishes this file as emu/app.js. Each MIDI message goes to the audio worklet (emu-midi-worklet.js,
// published as emu/app_worklet.js), which hands it to the firmware's own MIDI input. Nothing in the firmware changes.

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

  // a USB-MIDI packet: cable/code index, status, data 1, data 2 (the form plat_midi_in hands the firmware)
  function send(status, d1, d2) {
    const cmd = status & 0xf0;
    if (cmd < 0x80 || cmd > 0xe0) return;                    // channel messages only (notes, bend, CC, pressure, program)
    const pkt = ((status >> 4) | (status << 8) | (d1 << 16) | (d2 << 24)) >>> 0;
    node.port.postMessage({ type: "app", midi: pkt });
  }

  function onMidi(e) {
    const d = e.data;
    send(d[0], (d[1] || 0) & 127, (d[2] || 0) & 127);
  }

  function allNotesOff() {                                     // CC 123 on every channel, e.g. when a keyboard is unplugged
    for (let ch = 0; ch < 16; ch++) send(0xb0 | ch, 123, 0);
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
          if (e.port && e.port.state === "disconnected") allNotesOff();
        };
      }
      refresh();
    } catch {
      btn.textContent = "MIDI blocked: allow it for this site, then click again";
    }
  });
}
