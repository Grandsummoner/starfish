/**
 * Types aligned with Charles Vestal's fm1-base platform layer (https://github.com/charlesvestal/fm1-base)
 * Built on felucca-bsp.
 */

// Hardware Buttons (plat.h B_*)
export const PANEL_BTN = {
  FX: 0,
  SEL: 1,
  ENV: 2,
  LFO: 3,
  EDIT: 4,
  GLO: 5,
  HOME: 6,
  SAVE: 7,
  ARP: 8,
  SEQ: 9,
  PLAY: 10,
  REC: 11,
  "OCT-": 12,
  "OCT+": 13
} as const;

export type ButtonName = keyof typeof PANEL_BTN;

// Hardware Encoders (plat.h EN_*)
export const PANEL_ENC = {
  SELECT: 0,
  ALGORITHM: 1,
  PRESETS: 2,
  KNOB1: 3,
  KNOB2: 4,
  KNOB3: 5,
  KNOB4: 6
} as const;

export type EncoderName = keyof typeof PANEL_ENC;

// 27 Physical Keys: F3 (53) to G5 (79)
export const PANEL_WHITE_K = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
export const PANEL_BLACK_K = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];

export const KEY_MIDI_BASE = 53; // F3 is key index 0

export type EngineType = 
  | 'fm1_base_template' // Charles Vestal's standard fm1-base template
  | 'ctl1'             // Charles Vestal's CTL-1 (General MIDI controller)
  | 'amb1'             // Charles Vestal's AMB-1 (Music for Airports ambient)
  | 'x0x'              // Charles Vestal's fm1-x0x (303 acid + 808/909 drums)
  | 'fm6'              // 6-Operator DX7 style FM
  | 'va_poly'          // OberVave Polyphonic Virtual Analog
  | 'juno_106'         // FM-106 Juno DCO + Dual Chorus
  | 'sid_6581'         // FM-1 SID Chiptune MOS 6581
  | 'beast_drum'       // 808/909 Groovebox drum synth
  | 'sloop_tracker'    // SLOOP Tracker & Step Sequencer
  | 'ambient_drone'    // Ambient Generative Drone Matrix
  | 'virus_va';        // Access Virus Virtual Analog Hypersaw Engine

export interface OperatorConfig {
  id: number;
  ratio: number;
  detune: number;
  level: number;
  rate1: number;
  rate2: number;
  rate3: number;
  rate4: number;
  level1: number;
  level2: number;
  level3: number;
  level4: number;
  feedback?: number;
  isCarrier: boolean;
}

export type PanelPage = 'HOME' | 'FX' | 'SEL' | 'ENV' | 'LFO' | 'EDIT' | 'GLO' | 'ARP' | 'SEQ';

export type VirusBankId = 'A' | 'B' | 'C' | 'D' | 'E' | 'F' | 'G' | 'H' | 'I' | 'X';

export interface VirusBankMeta {
  id: VirusBankId;
  name: string;
  subtitle: string;
}

export type ModSource = 'lfo1' | 'swarm' | 'filterEnv' | 'velocity';
export type ModDest = 'cutoff' | 'reso' | 'spread' | 'pitch' | 'drive' | 'pan';

export interface ModCord {
  id: number;
  source: ModSource;
  dest: ModDest;
  amount: number; // -100 to +100
}

export type ScaleMode =
  | 'chromatic'
  | 'major'
  | 'minor'
  | 'harmonic_minor'
  | 'melodic_minor'
  | 'dorian'
  | 'phrygian'
  | 'lydian'
  | 'mixolydian'
  | 'locrian'
  | 'pentatonic'
  | 'minor_pentatonic';

export type LfoDest =
  | 'filter'
  | 'filter2'
  | 'reso'
  | 'pitch'
  | 'pan'
  | 'detune'
  | 'amp'
  | 'chorus'
  | 'phaser'
  | 'delay'
  | 'reverb';

export type ChordMemoryMode = 'off' | 'octave' | 'fifth' | 'minor7' | 'major7';
export type ThrowFxType = 'stutter' | 'tapestop' | 'freeze' | 'hpsweep';

export interface SynthPatch {
  id: string;
  name: string;
  bank?: VirusBankId;
  programNumber?: number;
  isUserPreset?: boolean;
  category: 'Template' | 'Controller' | 'Ambient' | 'Acid / Groove' | 'Keys' | 'Bass' | 'Pad' | 'Lead' | 'Pluck' | 'Percussion' | 'FX' | 'Chiptune' | 'Drone' | 'Arp';
  engine: EngineType;
  wave?: 'sine' | 'triangle' | 'saw' | 'fm';
  octave?: number;      // -2 to +2
  masterGain?: number;  // 0 to 4096 (plat master_q12)
  algorithm: number;   // 1 to 32
  feedback: number;    // 0 to 7
  operators: OperatorConfig[];
  filterCutoff: number;
  filterReso: number;
  filterType: 'lowpass' | 'highpass' | 'bandpass' | 'notch';
  fxChorus: number;
  fxDelayTime: number;
  fxDelayFeedback: number;
  fxDelayMix?: number;   // 0 to 100%
  fxReverbSize: number;
  fxDrive: number;
  glide: number;
  polyphony: number;
  // Access Virus Expanded Parameters (Filter Env, Amp Env, LFO, Osc, Arp)
  attack?: number;       // Amp Attack seconds (0.005 to 2.5)
  decay?: number;        // Amp Decay seconds (0.01 to 4.0)
  sustain?: number;      // Amp Sustain level (0.0 to 1.0)
  release?: number;      // Amp Release seconds (0.01 to 5.0)
  filterEnvAmount?: number; // Filter Envelope Depth (-100 to +100%)
  filterAttack?: number;    // Filter Attack seconds (0.005 to 2.0)
  filterDecay?: number;     // Filter Decay seconds (0.01 to 3.0)
  filterSustain?: number;   // Filter Sustain level (0.0 to 1.0)
  filterRelease?: number;   // Filter Release seconds (0.01 to 3.0)
  lfoRate?: number;      // Hz (0.1 to 30.0)
  lfoDepth?: number;     // 0 to 100%
  lfoDest?: LfoDest;
  lfoShape?: 'sine' | 'triangle' | 'saw' | 'square';
  lfo2Rate?: number;     // Hz (0.1 to 30.0)
  lfo2Depth?: number;    // 0 to 100%
  lfo2Dest?: LfoDest;
  lfo2Shape?: 'sine' | 'triangle' | 'saw' | 'square';
  arpEnabled?: boolean;
  arpLatch?: boolean;
  arpMode?: 'up' | 'down' | 'updown' | 'random';
  arpRate?: '1/8' | '1/16' | '1/16T' | '1/32';
  arpOctaves?: 1 | 2 | 3;
  arpGate?: number;      // 20 to 95%
  saturationType?: 'soft' | 'tube' | 'hard' | 'off';
  subOscLevel?: number;  // 0 to 100%
  subWaveform?: 'square' | 'sine' | 'saw';
  hypersawSpread?: number; // 0 to 100%
  // Tessera-inspired Modulation Matrix ("Mod Cords") & Free-Running Swarm Drift
  modMatrix?: ModCord[];
  swarmDepth?: number;   // 0 to 100%
  swarmRate?: number;    // 0.05 to 4.0 Hz
  // Access Virus C / TI2 Dual Filter, Spectral/Sync Oscs, Unison & Phaser FX (Virooos DSP parity)
  filterRouting?: 'serial' | 'parallel' | 'split';
  filter2Cutoff?: number;    // Hz (60 to 19000)
  filter2Reso?: number;      // Q (0.5 to 18)
  filter2Type?: 'lowpass' | 'highpass' | 'bandpass' | 'notch';
  filter2EnvAmount?: number; // -100 to +100%
  filterBalance?: number;    // 0 (Filter 1) to 100 (Filter 2)
  filterKeyfollow?: number;  // 0 to 100%
  oscMode?: 'hypersaw' | 'saw' | 'pulse' | 'wavetable' | 'sync' | 'triangle';
  pulseWidth?: number;       // 0.05 to 0.95
  wavetablePos?: number;     // 0.0 to 1.0 (6 spectral curves)
  osc2Semitone?: number;     // -24 to +24
  osc2Detune?: number;       // cents (-50 to +50)
  oscBalance?: number;       // 0 (Osc1) to 100 (Osc2)
  osc2Sync?: boolean;
  fmAmount?: number;         // 0 to 100%
  noiseVolume?: number;      // 0 to 100%
  ringModVolume?: number;    // 0 to 100%
  unisonVoices?: number;     // 1 to 5
  unisonPanSpread?: number;  // 0 to 100%
  fxPhaserMix?: number;      // 0 to 100%
  fxPhaserRate?: number;     // Hz (0.1 to 8.0)
  fxPhaserDepth?: number;    // 0 to 100%
  fxPhaserFeedback?: number; // 0 to 90%
}

export interface SequencerStep {
  note: number;
  notes?: number[];     // Legacy compatibility
  active: boolean;
  velocity: number;
  gate: number;
  accent: boolean;
  prob: number;
  pLockCutoff?: number;
  pLockReso?: number;
}

export interface SequencerTrack {
  id: number;
  name: string;
  channel: number;
  mute: boolean;
  solo: boolean;
  steps: SequencerStep[];
}

export type DrumKitId =
  | 'TR-808 CLASSIC'
  | 'TR-909 CLUB'
  | 'CR-78 VINTAGE'
  | 'LINNDRUM PCM'
  | 'VIRUS ELECTRO'
  | 'HOUSE CHICAGO'
  | 'ACOUSTIC JAZZ'
  | 'FM PERC DX'
  | 'LO-FI DUST'
  | 'TRAP / UK BASS';

export type DrumBusMode =
  | 'CLEAN PCM'
  | 'WARM CONSOLE'
  | 'PUNCH COMP'
  | 'SUB BASS +'
  | 'CLUB ROOM'
  | 'FM SPARKLE';

export interface DrumStep {
  active: boolean;
  accent: boolean;
  velocity: number;
}

export interface DrumTrack {
  id: number;        // 0..7 (BD, SD, CH, OH, CP, TM, RD, RS)
  shortName: string; // 'BD' | 'SD' | 'CH' | 'OH' | 'CP' | 'TM' | 'RD' | 'RS'
  name: string;
  mute: boolean;
  tune?: number;     // -12 to +12 semitones
  decay?: number;    // 25 to 200%
  tone?: number;     // 0 to 100%
  level?: number;    // 0 to 100%
  euclidPulses: number;
  euclidRotate: number;
  steps: DrumStep[]; // Length 64 max
}

export interface Fm1BaseTomlConfig {
  name: string;
  id_prefix: string;
  usb_manufacturer: string;
  usb_product: string;
  words?: string;
  ble: boolean;
  ble_central: boolean;
  author: string;
  description: string;
  tags: string[];
  repo: string;
  licence: string;
  credits: string;
}

export interface Fm1BaseFirmware {
  id: string;
  name: string;
  author: string;
  version: string;
  repoUrl: string;
  description: string;
  tagline: string;
  engine: EngineType;
  category: 'Platform Template' | 'MIDI Controller' | 'Ambient Generator' | 'Acid Groovebox' | 'Synthesizer';
  toml: Fm1BaseTomlConfig;
  sourceCode: string;
  appHeader: string;
  defaultPatch: SynthPatch;
  features: string[];
}

export interface CommunityFirmware {
  id: string;
  name: string;
  author: string;
  version: string;
  releaseDate: string;
  description: string;
  tagline: string;
  engine: EngineType;
  category: 'Groovebox' | 'Synthesizer' | 'Tracker' | 'Chiptune' | 'Drum Machine' | 'Ambient' | 'Utility';
  stars: number;
  downloads: number;
  sizeKb: number;
  memoryUsage: {
    flashKb: number;
    ramKb: number;
    cpuUsage: number;
    polyphony: number;
  };
  features: string[];
  defaultPatch: SynthPatch;
  sourceCode: string;
  checksum: string;
  hardwareTested: boolean;
}

export interface FwscBinaryMetadata {
  magic: string;
  targetChip: string;
  firmwareName: string;
  version: string;
  author: string;
  binarySize: number;
  entryPoint: string;
  crc32: string;
  buildTimestamp: number;
  featuresMask: number;
  partsMultitimbral: number;
  maxPolyphony: number;
}

export interface CompileResult {
  success: boolean;
  errors: Array<{ line: number; message: string; severity: 'error' | 'warning' }>;
  memoryReport: {
    flashUsed: number;
    flashTotal: number;
    ramUsed: number;
    ramTotal: number;
    dspCycles: number;
    cpuLoadEst: number;
  };
  binaryBlob?: Uint8Array;
  logicalImageBlob?: Uint8Array;
  productId?: string;
  metadata?: FwscBinaryMetadata;
}
