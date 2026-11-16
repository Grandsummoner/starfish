/**
 * Web MIDI API integration for M-Vave FM-1 Studio
 * - Listens to USB-MIDI controllers (M-Vave Chocolate, SMK-25/37, keyboards)
 * - Sends parameter changes, SysEx patch dumps, and firmware flasher payloads
 */

export interface MidiDevice {
  id: string;
  name: string;
  manufacturer: string;
  state: string;
}

export type MidiNoteCallback = (note: number, velocity: number) => void;
export type MidiCcCallback = (cc: number, value: number) => void;

interface MidiPort {
  id: string;
  name?: string;
  manufacturer?: string;
  state: string;
}

interface MidiInputPort extends MidiPort {
  onmidimessage: ((event: { data: Uint8Array }) => void) | null;
}

interface MidiOutputPort extends MidiPort {
  send: (data: Uint8Array | number[]) => void;
}

interface MidiAccessObj {
  inputs: Map<string, MidiInputPort>;
  outputs: Map<string, MidiOutputPort>;
  onstatechange: (() => void) | null;
}

class MidiManager {
  private access: MidiAccessObj | null = null;
  private selectedInput: MidiInputPort | null = null;
  private selectedOutput: MidiOutputPort | null = null;
  private isSupported = false;

  private onNoteOn: MidiNoteCallback | null = null;
  private onNoteOff: MidiNoteCallback | null = null;
  private onCc: MidiCcCallback | null = null;
  private onDevicesChanged: (() => void) | null = null;

  constructor() {
    this.isSupported = typeof navigator !== 'undefined' && 'requestMIDIAccess' in navigator;
  }

  public getIsSupported(): boolean {
    return this.isSupported;
  }

  public async initMidi(): Promise<boolean> {
    if (!this.isSupported) return false;
    const nav = navigator as unknown as { requestMIDIAccess?: (opt: { sysex: boolean }) => Promise<MidiAccessObj> };
    if (!nav.requestMIDIAccess) return false;
    const bindAccess = (acc: MidiAccessObj) => {
      this.access = acc;
      this.access.onstatechange = () => {
        if (this.onDevicesChanged) this.onDevicesChanged();
      };
      const inputs = this.getInputs();
      if (inputs.length > 0) {
        const preferredIn = inputs.find(d => /fm-?1|m-?vave|sinco|virus/i.test(d.name)) || inputs[0];
        this.selectInput(preferredIn.id);
      }
      const outputs = this.getOutputs();
      if (outputs.length > 0) {
        const preferredOut = outputs.find(d => /fm-?1|m-?vave|sinco|virus/i.test(d.name)) || outputs[0];
        this.selectOutput(preferredOut.id);
      }
    };

    try {
      const acc = await nav.requestMIDIAccess({ sysex: true });
      bindAccess(acc);
      return true;
    } catch {
      try {
        const acc = await nav.requestMIDIAccess({ sysex: false });
        bindAccess(acc);
        return true;
      } catch {
        return false;
      }
    }
  }

  public getInputs(): MidiDevice[] {
    if (!this.access) return [];
    const devices: MidiDevice[] = [];
    this.access.inputs.forEach((input: MidiInputPort) => {
      devices.push({
        id: input.id,
        name: input.name || 'MIDI Input Device',
        manufacturer: input.manufacturer || 'Generic',
        state: input.state
      });
    });
    return devices;
  }

  public getOutputs(): MidiDevice[] {
    if (!this.access) return [];
    const devices: MidiDevice[] = [];
    this.access.outputs.forEach((output: MidiOutputPort) => {
      devices.push({
        id: output.id,
        name: output.name || 'MIDI Output Device',
        manufacturer: output.manufacturer || 'Generic',
        state: output.state
      });
    });
    return devices;
  }

  public getSelectedInput(): MidiDevice | null {
    if (!this.selectedInput) return null;
    return {
      id: this.selectedInput.id,
      name: this.selectedInput.name || 'MIDI Input Device',
      manufacturer: this.selectedInput.manufacturer || 'Generic',
      state: this.selectedInput.state
    };
  }

  public getSelectedOutput(): MidiDevice | null {
    if (!this.selectedOutput) return null;
    return {
      id: this.selectedOutput.id,
      name: this.selectedOutput.name || 'MIDI Output Device',
      manufacturer: this.selectedOutput.manufacturer || 'Generic',
      state: this.selectedOutput.state
    };
  }

  public selectInput(id: string): void {
    if (!this.access) return;
    if (this.selectedInput) {
      this.selectedInput.onmidimessage = null;
    }
    if (!id) {
      this.selectedInput = null;
      return;
    }
    const input = this.access.inputs.get(id);
    if (input) {
      this.selectedInput = input;
      this.selectedInput.onmidimessage = this.handleMidiMessage;
    }
  }

  public selectOutput(id: string): void {
    if (!this.access) return;
    if (!id) {
      this.selectedOutput = null;
      return;
    }
    const output = this.access.outputs.get(id);
    if (output) {
      this.selectedOutput = output;
    }
  }

  public disconnect(): void {
    if (this.selectedInput) {
      this.selectedInput.onmidimessage = null;
      this.selectedInput = null;
    }
    this.selectedOutput = null;
  }

  public sendNoteOn(note: number, velocity: number = 110, channel: number = 0): void {
    if (!this.selectedOutput) return;
    try {
      this.selectedOutput.send([0x90 | (channel & 0x0f), note & 0x7f, velocity & 0x7f]);
    } catch {
      // ignore disconnected port
    }
  }

  public sendNoteOff(note: number, channel: number = 0): void {
    if (!this.selectedOutput) return;
    try {
      this.selectedOutput.send([0x80 | (channel & 0x0f), note & 0x7f, 0]);
    } catch {
      // ignore disconnected port
    }
  }

  public sendCc(cc: number, value: number, channel: number = 0): void {
    if (!this.selectedOutput) return;
    try {
      this.selectedOutput.send([0xb0 | (channel & 0x0f), cc & 0x7f, value & 0x7f]);
    } catch {
      // ignore disconnected port
    }
  }

  public setCallbacks(
    onNoteOn: MidiNoteCallback,
    onNoteOff: MidiNoteCallback,
    onCc?: MidiCcCallback,
    onDevicesChanged?: () => void
  ): void {
    this.onNoteOn = onNoteOn;
    this.onNoteOff = onNoteOff;
    if (onCc) this.onCc = onCc;
    if (onDevicesChanged) this.onDevicesChanged = onDevicesChanged;
  }

  private handleMidiMessage = (event: { data: Uint8Array }): void => {
    const data = event.data;
    if (!data || data.length < 2) return;
    const status = data[0] & 0xf0;
    const note = data[1];
    const velocity = data[2] || 0;

    if (status === 0x90 && velocity > 0) {
      if (this.onNoteOn) this.onNoteOn(note, velocity);
    } else if (status === 0x80 || (status === 0x90 && velocity === 0)) {
      if (this.onNoteOff) this.onNoteOff(note, 0);
    } else if (status === 0xb0) { // CC message
      const ccNumber = data[1];
      const ccVal = data[2];
      if (this.onCc) this.onCc(ccNumber, ccVal);
    }
  };

  /**
   * Send SysEx flash chunk or firmware block to physical FM-1 hardware
   */
  public async sendSysExPayload(
    payload: Uint8Array,
    onProgress?: (progressPercent: number) => void
  ): Promise<boolean> {
    if (!this.selectedOutput) {
      throw new Error("No MIDI output device selected. Please connect your M-Vave FM-1 in DFU/Update mode.");
    }

    const CHUNK_SIZE = 64; // Safe USB-MIDI SysEx chunk size
    const totalChunks = Math.ceil(payload.length / CHUNK_SIZE);

    for (let i = 0; i < totalChunks; i++) {
      const start = i * CHUNK_SIZE;
      const end = Math.min(start + CHUNK_SIZE, payload.length);
      const chunk = payload.slice(start, end);

      // Wrap in SysEx header for JieLi AC791N bootloader:
      // F0 7D [M-Vave ID 0x4D 0x56] [Cmd 0x12] [Chunk_Idx_H Chunk_Idx_L] ...data... F7
      const sysex = new Uint8Array(chunk.length + 8);
      sysex[0] = 0xF0;
      sysex[1] = 0x7D; // Educational / Custom Mfg
      sysex[2] = 0x4D; // 'M'
      sysex[3] = 0x56; // 'V'
      sysex[4] = 0x12; // FWSC_FLASH_BLOCK_CMD
      sysex[5] = (i >> 7) & 0x7F;
      sysex[6] = i & 0x7F;
      sysex.set(chunk, 7);
      sysex[sysex.length - 1] = 0xF7;

      this.selectedOutput.send(sysex);
      if (onProgress) {
        onProgress(Math.round(((i + 1) / totalChunks) * 100));
      }
      // Small delay between USB SysEx bursts to prevent FIFO buffer overrun on the AC791N
      await new Promise(r => setTimeout(r, 8));
    }
    return true;
  }
}

export const midiManager = new MidiManager();
