// SPDX-License-Identifier: MPL-2.0
// STARFISH web emulator: MIDI in, audio side.
// fm1-base's worklet loads an optional "app_worklet.js" first and passes the page's { type: "app" } messages to
// globalThis.fm1App.message(fm1, m). The emulator already exports web_midi_in(port, packet), which queues a
// USB-MIDI packet for the same plat_midi_in() the hardware reads, so velocity, pitch bend, mod wheel and the rest
// reach the firmware's own MIDI code unchanged. Nothing in the firmware or fm1-base is modified.

const PORT_USB = 0;   // a Web MIDI input is USB, as the emulator's comments say

globalThis.fm1App = {
  message(fm1, m) {
    if (m.midi !== undefined && fm1.ex && fm1.ex.web_midi_in) {
      fm1.ex.web_midi_in(PORT_USB, m.midi >>> 0);
    }
  },
};
