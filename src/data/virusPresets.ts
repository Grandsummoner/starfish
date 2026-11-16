/**
 * Access Virus B & C Full Factory ROM/RAM Soundbank (Banks A–H + X = 1,152 Presets)
 * Decoded directly from the official Virus B & C SysEx ROM dumps (Virooos collection).
 */

import { SynthPatch, VirusBankId, VirusBankMeta } from '../types/firmware';
import { VIRUS_ROM_PACK, VIRUS_ROM_RECORD_SIZE } from './virusRomPack';

export const VIRUS_BANKS: VirusBankMeta[] = [
  { id: 'A', name: 'BANK A', subtitle: 'RAM A · 128 SOUNDS' },
  { id: 'B', name: 'BANK B', subtitle: 'RAM B · 128 SOUNDS' },
  { id: 'C', name: 'BANK C', subtitle: 'C ROM · 128 SOUNDS' },
  { id: 'D', name: 'BANK D', subtitle: 'D ROM · 128 SOUNDS' },
  { id: 'E', name: 'BANK E', subtitle: 'E ROM · 128 SOUNDS' },
  { id: 'F', name: 'BANK F', subtitle: 'F ROM · 128 SOUNDS' },
  { id: 'G', name: 'BANK G', subtitle: 'G ROM · 128 SOUNDS' },
  { id: 'H', name: 'BANK H', subtitle: 'H ROM · 128 SOUNDS' },
  { id: 'I', name: 'BANK I', subtitle: 'STARFISH I · 32 SOUNDS' },
  { id: 'X', name: 'BANK X', subtitle: 'USER PRESETS ONLY' },
];

const USER_STORAGE_KEY = 'fm1_virus_user_presets_v1';
const USER_BANK_X_KEY = 'fm1_starfish_bank_x_v1';
const RECENT_PRESETS_KEY = 'fm1_recent_presets_v1';

const VIRUS_CATEGORY_MAP: Record<number, SynthPatch['category']> = {
  0: 'Lead',
  1: 'Lead',
  2: 'Bass',
  3: 'Pad',
  4: 'Pluck',
  5: 'Pluck',
  6: 'Acid / Groove',
  7: 'Keys',
  8: 'Arp',
  9: 'FX',
  10: 'Percussion',
  11: 'Percussion',
  12: 'FX',
  13: 'FX',
  14: 'Lead',
  15: 'Pad',
  16: 'Arp',
};

const makeOps = (ratio4 = 2.0, d1 = -16, d2 = 16, l3 = 85) => [
  { id: 1, ratio: 1.0, detune: d1, level: 99, rate1: 99, rate2: 60, rate3: 85, rate4: 65, level1: 99, level2: 85, level3: l3, level4: 0, isCarrier: true },
  { id: 2, ratio: 1.0, detune: d2, level: 99, rate1: 99, rate2: 60, rate3: 85, rate4: 65, level1: 99, level2: 85, level3: l3, level4: 0, isCarrier: true },
  { id: 3, ratio: 0.5, detune: 0, level: 85, rate1: 99, rate2: 60, rate3: 85, rate4: 65, level1: 99, level2: 85, level3: l3, level4: 0, isCarrier: true },
  { id: 4, ratio: ratio4, detune: 6, level: 75, rate1: 95, rate2: 60, rate3: 75, rate4: 60, level1: 99, level2: 75, level3: 75, level4: 0, isCarrier: true },
  { id: 5, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
  { id: 6, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
];

// Authentic Access Virus exponential scaling helpers
function byteToCutoffHz(val: number): number {
  const norm = Math.max(0, Math.min(127, val)) / 127;
  return Math.round(45 * Math.pow(410, norm));
}

function byteToResoQ(val: number): number {
  const norm = Math.max(0, Math.min(127, val)) / 127;
  return parseFloat((0.7 + Math.pow(norm, 1.45) * 15.5).toFixed(1));
}

function byteToEnvSec(val: number, maxSec: number, minSec = 0.005): number {
  const norm = Math.max(0, Math.min(127, val)) / 127;
  return parseFloat((minSec + Math.pow(norm, 2.2) * maxSec).toFixed(3));
}

function byteToFilterType(modeByte: number): 'lowpass' | 'highpass' | 'bandpass' | 'notch' {
  if (modeByte === 1) return 'highpass';
  if (modeByte === 2) return 'bandpass';
  if (modeByte === 3) return 'notch';
  return 'lowpass';
}

function decodeBase64Bytes(b64: string): Uint8Array {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) {
    out[i] = bin.charCodeAt(i);
  }
  return out;
}

function decodeRecordToPatch(bankId: VirusBankId, progNum: number, rec: Uint8Array): SynthPatch {
  let rawName = '';
  for (let i = 0; i < 10; i++) {
    rawName += String.fromCharCode(rec[57 + i] || 32);
  }
  const name = rawName.replace(/\s+/g, ' ').trim() || `${bankId}${progNum.toString().padStart(3, '0')}`;
  const progStr = progNum.toString().padStart(3, '0');
  const isUtilitySlot = name === '- Init -' || name === '- START -' || name === '>>INPUT<<';

  // Return clean, normal-sounding Access Virus Hypersaw default for utility/input-only ROM slots
  if (isUtilitySlot) {
    return {
      id: `rom-${bankId}-${progStr}`,
      name: name === '- Init -' ? 'Virus Init' : name === '- START -' ? 'Virus Start' : 'Virus Lead',
      bank: bankId,
      programNumber: progNum,
      category: 'Lead',
      engine: 'virus_va',
      wave: 'saw',
      oscMode: 'hypersaw',
      pulseWidth: 0.5,
      wavetablePos: 0.25,
      osc2Semitone: 0,
      osc2Detune: 14,
      oscBalance: 50,
      osc2Sync: false,
      fmAmount: 0,
      noiseVolume: 0,
      ringModVolume: 0,
      octave: 0,
      masterGain: 3700,
      algorithm: 1,
      feedback: 5,
      filterCutoff: 5200,
      filterReso: 3.5,
      filterType: 'lowpass',
      filter2Cutoff: 12000,
      filter2Reso: 1.2,
      filter2Type: 'lowpass',
      filterRouting: 'serial',
      filterBalance: 0,
      filterKeyfollow: 35,
      filterEnvAmount: 42,
      filter2EnvAmount: 25,
      filterAttack: 0.008,
      filterDecay: 0.45,
      filterSustain: 0.45,
      filterRelease: 0.35,
      attack: 0.008,
      decay: 0.85,
      sustain: 0.78,
      release: 0.35,
      fxChorus: 48,
      fxPhaserMix: 0,
      fxPhaserRate: 0.45,
      fxPhaserDepth: 55,
      fxPhaserFeedback: 40,
      fxDelayTime: 0.32,
      fxDelayFeedback: 36,
      fxReverbSize: 45,
      fxDrive: 28,
      glide: 0,
      polyphony: 8,
      hypersawSpread: 82,
      unisonVoices: 1,
      unisonPanSpread: 78,
      lfoRate: 4.2,
      lfoDepth: 10,
      lfoDest: 'filter',
      lfoShape: 'sine',
      lfo2Rate: 0.9,
      lfo2Depth: 12,
      lfo2Dest: 'pan',
      lfo2Shape: 'triangle',
      arpEnabled: false,
      arpLatch: false,
      arpMode: 'updown',
      arpRate: '1/16',
      arpOctaves: 2,
      saturationType: 'soft',
      subOscLevel: 65,
      subWaveform: 'square',
      swarmDepth: 20,
      swarmRate: 0.38,
      operators: makeOps(2.0, -16.8, 16.8, 82),
    };
  }

  const porta = rec[0];
  const osc1Shape = rec[1];
  const osc1PW = rec[2];
  const osc1Wave = rec[3];
  const osc2Shape = rec[5];
  const osc2Wave = rec[7];
  const osc2Semi = rec[8] - 64;
  // Virus osc2Detune is 0..127 spread (32..48 is typical chorus detune)
  const osc2Detune = Math.round((rec[9] / 127) * 38);
  const fmAmt = rec[10];
  const sync = rec[11] >= 64;
  const oscBal = Math.round((rec[12] / 127) * 100);
  const subVol = Math.round((rec[13] / 127) * 100);
  const subShape: 'square' | 'sine' = rec[14] === 1 ? 'sine' : 'square';
  const noiseVol = Math.round((rec[15] / 127) * 100);
  const ringVol = Math.round((rec[16] / 127) * 100);

  const f1Mode = byteToFilterType(rec[26]);
  const f2Mode = byteToFilterType(rec[27]);
  const reso1 = byteToResoQ(rec[19]);
  const reso2 = byteToResoQ(rec[20]);
  // Access Virus filter1EnvAmt / filter2EnvAmt (bytes 44, 45) are unsigned 0..127 depth (0 = 0%, 127 = +100%)
  const f1EnvAmt = Math.round((rec[21] / 127) * 100);
  const f2EnvAmt = Math.round((rec[22] / 127) * 100);
  const cutoff1 = Math.max(
    f1Mode === 'lowpass' && f1EnvAmt < 15 ? 340 : f1Mode === 'bandpass' && f1EnvAmt < 15 ? 240 : 140,
    byteToCutoffHz(rec[17])
  );
  const cutoff2 = Math.max(
    f2Mode === 'lowpass' && f2EnvAmt < 15 ? 340 : f2Mode === 'bandpass' && f2EnvAmt < 15 ? 240 : 140,
    byteToCutoffHz(rec[18])
  );
  // Access Virus filter1Keyfollow is signed centered at 64
  const f1Kf = Math.max(0, Math.round(((rec[23] - 64) / 63) * 100));
  const fBal = Math.round((rec[24] / 127) * 100);
  const satByte = rec[25];
  const fRouteByte = rec[28];
  const filterRouting: 'serial' | 'parallel' | 'split' =
    fRouteByte === 3 ? 'split' : fRouteByte === 2 ? 'parallel' : 'serial';

  const fAtk = byteToEnvSec(rec[29], 1.2, 0.005);
  const fDec = byteToEnvSec(rec[30], 2.4, 0.04);
  const fSus = parseFloat((rec[31] / 127).toFixed(2));
  const fRel = byteToEnvSec(rec[32], 2.5, 0.04);

  const aAtk = byteToEnvSec(rec[33], 0.55, 0.005);
  const aDec = byteToEnvSec(rec[34], 3.2, 0.08);
  const aSus = parseFloat(Math.max(0.08, rec[35] / 127).toFixed(2));
  const aRel = byteToEnvSec(rec[36], 3.2, 0.04);

  const lfo1Rate = parseFloat((0.1 + Math.pow(rec[37] / 127, 2.0) * 18.0).toFixed(1));
  const lfoShapeIdx = rec[38] % 4;
  const lfoShape: 'sine' | 'triangle' | 'saw' | 'square' =
    lfoShapeIdx === 1 ? 'triangle' : lfoShapeIdx === 2 ? 'saw' : lfoShapeIdx === 3 ? 'square' : 'sine';
  const lfoOscMod = Math.abs(rec[39] - 64);
  const lfoCutMod = Math.abs(rec[40] - 64);
  const lfoPanMod = Math.abs(rec[41] - 64);
  let lfoDest: 'filter' | 'pitch' | 'pan' | 'detune' = 'filter';
  let lfoDepth = Math.round((lfoCutMod / 63) * 85);
  if (lfoPanMod > lfoCutMod && lfoPanMod >= lfoOscMod) {
    lfoDest = 'pan';
    lfoDepth = Math.round((lfoPanMod / 63) * 85);
  } else if (lfoOscMod > lfoCutMod) {
    lfoDest = 'pitch';
    lfoDepth = Math.round((lfoOscMod / 63) * 32);
  }
  if (lfoDepth < 6 && (rec[44] > 0 || bankId === 'C' || bankId === 'E')) {
    lfoDepth = 12;
  }

  // Normalize patch volume into a tight, consistent range so quiet ROM slots still project at 15% master volume
  const patchVol = Math.max(96, Math.min(127, rec[42] || 112));
  const transposeOct = Math.max(-2, Math.min(2, Math.round((rec[43] - 64) / 12)));
  const unisonByte = rec[44];
  const unisonVoices = unisonByte === 0 ? 1 : Math.min(5, Math.max(2, unisonByte + 1));
  const unisonPanSpread = Math.round((rec[45] / 127) * 100);

  const chorusMix = Math.round((rec[46] / 127) * 100);
  const delayReverbMode = rec[47];
  const fxSend = Math.round((rec[48] / 127) * 100);
  const delTime = parseFloat((0.08 + (rec[49] / 127) * 0.65).toFixed(2));
  const delFb = Math.round((rec[50] / 127) * 78);

  const phaserMix = Math.round((rec[51] / 127) * 100);
  const phaserRate = parseFloat((0.12 + (rec[52] / 127) * 4.5).toFixed(2));
  const phaserDepth = Math.round((rec[53] / 127) * 100);
  const phaserFeedback = Math.round((rec[54] / 127) * 85);
  const distByte = rec[55];
  const catByte = rec[56];

  // Map Virus Oscillator Shape -> FM-1 Virus oscMode
  let oscMode: SynthPatch['oscMode'] = 'hypersaw';
  if (sync) {
    oscMode = 'sync';
  } else if (osc1Shape > 88) {
    oscMode = 'pulse';
  } else if (osc1Shape < 40 && (osc1Wave > 0 || osc2Wave > 0)) {
    oscMode = 'wavetable';
  } else if (osc1Shape < 24 && osc2Shape < 24) {
    oscMode = 'triangle';
  } else if (unisonVoices >= 2) {
    oscMode = 'hypersaw';
  } else if (osc1Shape >= 40 && osc1Shape <= 88 && osc2Detune < 10) {
    oscMode = 'saw';
  }

  const saturationType: 'soft' | 'tube' | 'hard' | 'off' =
    satByte === 0 && distByte < 10
      ? 'off'
      : satByte >= 8 || distByte > 75
      ? 'hard'
      : satByte >= 4
      ? 'tube'
      : 'soft';

  const fxDrive = Math.min(100, Math.round((satByte > 0 ? 18 + satByte * 5 : 15) + distByte * 0.5));
  const hasReverb = delayReverbMode >= 2;
  const fxReverbSize = hasReverb ? Math.min(95, Math.round(fxSend * 0.85 + 15)) : Math.round(fxSend * 0.35);

  const cat: SynthPatch['category'] = VIRUS_CATEGORY_MAP[catByte] || 'Lead';

  return {
    id: `rom-${bankId}-${progStr}`,
    name,
    bank: bankId,
    programNumber: progNum,
    category: cat,
    engine: 'virus_va',
    wave: oscMode === 'triangle' ? 'triangle' : 'saw',
    oscMode,
    pulseWidth: parseFloat((0.1 + (osc1PW / 127) * 0.8).toFixed(2)),
    wavetablePos: parseFloat((Math.max(osc1Wave, osc2Wave) / 63).toFixed(2)),
    osc2Semitone: Math.max(-24, Math.min(24, osc2Semi)),
    osc2Detune,
    oscBalance: oscBal,
    osc2Sync: sync,
    fmAmount: Math.round((fmAmt / 127) * 100),
    noiseVolume: noiseVol,
    ringModVolume: ringVol,
    octave: transposeOct,
    masterGain: Math.round((patchVol / 127) * 4096),
    algorithm: 1,
    feedback: 5,
    filterCutoff: cutoff1,
    filterReso: reso1,
    filterType: f1Mode,
    filter2Cutoff: cutoff2,
    filter2Reso: reso2,
    filter2Type: f2Mode,
    filterRouting,
    filterBalance: fBal,
    filterKeyfollow: f1Kf,
    filterEnvAmount: f1EnvAmt,
    filter2EnvAmount: f2EnvAmt,
    filterAttack: fAtk,
    filterDecay: fDec,
    filterSustain: fSus,
    filterRelease: fRel,
    attack: aAtk,
    decay: aDec,
    sustain: aSus,
    release: aRel,
    fxChorus: chorusMix,
    fxPhaserMix: phaserMix,
    fxPhaserRate: phaserRate,
    fxPhaserDepth: phaserDepth,
    fxPhaserFeedback: phaserFeedback,
    fxDelayTime: delTime,
    fxDelayFeedback: Math.round((delFb * Math.max(25, fxSend)) / 100),
    fxReverbSize,
    fxDrive,
    glide: Math.round((porta / 127) * 85),
    polyphony: unisonVoices >= 4 ? 4 : 8,
    hypersawSpread: Math.max(25, Math.min(100, Math.round(35 + Math.abs(osc2Detune) * 1.4 + unisonVoices * 10))),
    unisonVoices,
    unisonPanSpread,
    lfoRate: lfo1Rate,
    lfoDepth,
    lfoDest,
    lfoShape,
    lfo2Rate: parseFloat((0.2 + ((rec[38] * 3 + progNum) % 35) * 0.18).toFixed(1)),
    lfo2Depth: cat === 'Pad' ? 22 : cat === 'Lead' ? 10 : 0,
    lfo2Dest: 'pan',
    lfo2Shape: 'triangle',
    arpEnabled: cat === 'Arp',
    arpLatch: false,
    arpMode: 'updown',
    arpRate: '1/16',
    arpOctaves: 2,
    saturationType,
    subOscLevel: subVol,
    subWaveform: subShape,
    swarmDepth: 22,
    swarmRate: 0.38,
    operators: makeOps(2.0, -16.8, 16.8, 82),
  };
}

function loadUserSavedMap(): Record<string, SynthPatch> {
  try {
    if (typeof window === 'undefined' || !window.localStorage) return {};
    const raw = window.localStorage.getItem(USER_STORAGE_KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw) as Record<string, SynthPatch>;
    // Migrate legacy rom-S- keys cleanly to rom-I-
    Object.keys(parsed).forEach((k) => {
      if (k.startsWith('rom-S-')) {
        const newKey = `rom-I-${k.slice(6)}`;
        if (!parsed[newKey]) {
          parsed[newKey] = { ...parsed[k], id: newKey, bank: 'I' };
        }
        delete parsed[k];
      }
    });
    return parsed;
  } catch {
    return {};
  }
}

function loadBankXSavedMap(): Record<string, SynthPatch> {
  try {
    if (typeof window === 'undefined' || !window.localStorage) return {};
    const raw = window.localStorage.getItem(USER_BANK_X_KEY);
    if (!raw) return {};
    return JSON.parse(raw) as Record<string, SynthPatch>;
  } catch {
    return {};
  }
}

const decodedBanksCache: Partial<Record<VirusBankId, SynthPatch[]>> = {};

const STARFISH_WILD_SPECS: Array<{
  name: string;
  cat: SynthPatch['category'];
  oscMode: NonNullable<SynthPatch['oscMode']>;
  cutoff: number;
  reso: number;
  fType: SynthPatch['filterType'];
  routing: NonNullable<SynthPatch['filterRouting']>;
  f2Cut: number;
  f2Type: NonNullable<SynthPatch['filter2Type']>;
  lfoDest: NonNullable<SynthPatch['lfoDest']>;
  lfoRate: number;
  lfoDepth: number;
  lfoShape: NonNullable<SynthPatch['lfoShape']>;
  lfo2Dest: NonNullable<SynthPatch['lfo2Dest']>;
  lfo2Rate: number;
  lfo2Depth: number;
  chorus: number;
  phaser: number;
  delayTime: number;
  delayFb: number;
  reverb: number;
  drive: number;
  sat: NonNullable<SynthPatch['saturationType']>;
  uni: number;
  semi2: number;
  sub: number;
  noise: number;
  ring: number;
  atk: number;
  rel: number;
  arp?: boolean;
}> = [
  { name: 'Star Phaser Pad', cat: 'Pad', oscMode: 'hypersaw', cutoff: 3200, reso: 4.5, fType: 'lowpass', routing: 'parallel', f2Cut: 1900, f2Type: 'bandpass', lfoDest: 'phaser', lfoRate: 0.4, lfoDepth: 72, lfoShape: 'sine', lfo2Dest: 'pan', lfo2Rate: 0.3, lfo2Depth: 45, chorus: 75, phaser: 78, delayTime: 0.38, delayFb: 48, reverb: 72, drive: 18, sat: 'soft', uni: 3, semi2: 7, sub: 45, noise: 12, ring: 0, atk: 0.28, rel: 1.4 },
  { name: 'Dub Tape Swell', cat: 'Ambient', oscMode: 'wavetable', cutoff: 2100, reso: 5.8, fType: 'lowpass', routing: 'serial', f2Cut: 7800, f2Type: 'lowpass', lfoDest: 'delay', lfoRate: 0.7, lfoDepth: 65, lfoShape: 'triangle', lfo2Dest: 'filter', lfo2Rate: 0.25, lfo2Depth: 32, chorus: 55, phaser: 20, delayTime: 0.36, delayFb: 64, reverb: 68, drive: 22, sat: 'tube', uni: 2, semi2: 12, sub: 55, noise: 8, ring: 0, atk: 0.06, rel: 0.85 },
  { name: 'Acid Reso Siren', cat: 'Acid / Groove', oscMode: 'saw', cutoff: 950, reso: 11.8, fType: 'lowpass', routing: 'serial', f2Cut: 6400, f2Type: 'lowpass', lfoDest: 'reso', lfoRate: 3.4, lfoDepth: 62, lfoShape: 'saw', lfo2Dest: 'filter', lfo2Rate: 0.8, lfo2Depth: 44, chorus: 18, phaser: 0, delayTime: 0.24, delayFb: 42, reverb: 28, drive: 48, sat: 'tube', uni: 1, semi2: 0, sub: 80, noise: 0, ring: 0, atk: 0.006, rel: 0.24 },
  { name: 'Reverb Breath Choir', cat: 'Pad', oscMode: 'wavetable', cutoff: 4200, reso: 3.2, fType: 'bandpass', routing: 'parallel', f2Cut: 5600, f2Type: 'lowpass', lfoDest: 'reverb', lfoRate: 0.3, lfoDepth: 75, lfoShape: 'sine', lfo2Dest: 'detune', lfo2Rate: 0.6, lfo2Depth: 38, chorus: 80, phaser: 35, delayTime: 0.45, delayFb: 45, reverb: 84, drive: 12, sat: 'soft', uni: 4, semi2: 12, sub: 30, noise: 24, ring: 0, atk: 0.42, rel: 1.9 },
  { name: 'Tremolo Juno Glass', cat: 'Keys', oscMode: 'pulse', cutoff: 5800, reso: 2.6, fType: 'lowpass', routing: 'serial', f2Cut: 11000, f2Type: 'lowpass', lfoDest: 'amp', lfoRate: 5.5, lfoDepth: 55, lfoShape: 'triangle', lfo2Dest: 'chorus', lfo2Rate: 0.4, lfo2Depth: 50, chorus: 85, phaser: 0, delayTime: 0.28, delayFb: 34, reverb: 52, drive: 16, sat: 'soft', uni: 2, semi2: 0, sub: 45, noise: 0, ring: 0, atk: 0.01, rel: 0.55 },
  { name: 'Split Ring Bell', cat: 'Pluck', oscMode: 'triangle', cutoff: 7200, reso: 3.8, fType: 'lowpass', routing: 'split', f2Cut: 2400, f2Type: 'highpass', lfoDest: 'filter2', lfoRate: 2.8, lfoDepth: 58, lfoShape: 'sine', lfo2Dest: 'pan', lfo2Rate: 1.9, lfo2Depth: 65, chorus: 42, phaser: 25, delayTime: 0.32, delayFb: 52, reverb: 66, drive: 14, sat: 'soft', uni: 2, semi2: 19, sub: 20, noise: 0, ring: 55, atk: 0.005, rel: 0.9 },
  { name: 'Sync Laser Swarm', cat: 'Lead', oscMode: 'sync', cutoff: 4800, reso: 6.2, fType: 'lowpass', routing: 'serial', f2Cut: 9200, f2Type: 'lowpass', lfoDest: 'pitch', lfoRate: 6.2, lfoDepth: 22, lfoShape: 'sine', lfo2Dest: 'phaser', lfo2Rate: 1.4, lfo2Depth: 60, chorus: 48, phaser: 52, delayTime: 0.22, delayFb: 38, reverb: 42, drive: 38, sat: 'tube', uni: 3, semi2: 7, sub: 50, noise: 0, ring: 15, atk: 0.012, rel: 0.38 },
  { name: 'Chorus Ocean Drift', cat: 'Ambient', oscMode: 'hypersaw', cutoff: 2900, reso: 3.4, fType: 'lowpass', routing: 'parallel', f2Cut: 4100, f2Type: 'bandpass', lfoDest: 'chorus', lfoRate: 0.5, lfoDepth: 82, lfoShape: 'sine', lfo2Dest: 'reverb', lfo2Rate: 0.2, lfo2Depth: 55, chorus: 92, phaser: 28, delayTime: 0.52, delayFb: 54, reverb: 80, drive: 14, sat: 'soft', uni: 4, semi2: 0, sub: 60, noise: 10, ring: 0, atk: 0.5, rel: 2.2 },
  { name: 'FM Metal Arp', cat: 'Arp', oscMode: 'sync', cutoff: 6400, reso: 4.8, fType: 'lowpass', routing: 'split', f2Cut: 1600, f2Type: 'highpass', lfoDest: 'delay', lfoRate: 2.4, lfoDepth: 48, lfoShape: 'square', lfo2Dest: 'pan', lfo2Rate: 3.1, lfo2Depth: 55, chorus: 38, phaser: 18, delayTime: 0.19, delayFb: 48, reverb: 44, drive: 26, sat: 'tube', uni: 2, semi2: 12, sub: 40, noise: 0, ring: 42, atk: 0.005, rel: 0.22, arp: true },
  { name: 'Dual Notch Vapor', cat: 'Pad', oscMode: 'wavetable', cutoff: 1800, reso: 5.5, fType: 'notch', routing: 'serial', f2Cut: 4800, f2Type: 'lowpass', lfoDest: 'filter', lfoRate: 0.9, lfoDepth: 68, lfoShape: 'triangle', lfo2Dest: 'phaser', lfo2Rate: 0.45, lfo2Depth: 64, chorus: 62, phaser: 68, delayTime: 0.41, delayFb: 46, reverb: 70, drive: 18, sat: 'soft', uni: 3, semi2: -12, sub: 55, noise: 14, ring: 0, atk: 0.22, rel: 1.2 },
  { name: 'Cyber Reese Bass', cat: 'Bass', oscMode: 'hypersaw', cutoff: 1400, reso: 6.4, fType: 'lowpass', routing: 'serial', f2Cut: 4200, f2Type: 'lowpass', lfoDest: 'detune', lfoRate: 1.6, lfoDepth: 60, lfoShape: 'sine', lfo2Dest: 'filter', lfo2Rate: 0.7, lfo2Depth: 42, chorus: 40, phaser: 22, delayTime: 0.12, delayFb: 16, reverb: 22, drive: 42, sat: 'tube', uni: 3, semi2: 0, sub: 90, noise: 0, ring: 0, atk: 0.01, rel: 0.3 },
  { name: 'Psychodelic Wobble', cat: 'FX', oscMode: 'pulse', cutoff: 2600, reso: 8.6, fType: 'lowpass', routing: 'parallel', f2Cut: 3400, f2Type: 'bandpass', lfoDest: 'filter', lfoRate: 4.8, lfoDepth: 75, lfoShape: 'sine', lfo2Dest: 'delay', lfo2Rate: 1.5, lfo2Depth: 62, chorus: 50, phaser: 72, delayTime: 0.27, delayFb: 58, reverb: 55, drive: 34, sat: 'tube', uni: 2, semi2: 5, sub: 65, noise: 6, ring: 22, atk: 0.01, rel: 0.48 },
  { name: 'Glass Arp Pulsar', cat: 'Arp', oscMode: 'wavetable', cutoff: 7600, reso: 4.0, fType: 'lowpass', routing: 'serial', f2Cut: 11500, f2Type: 'lowpass', lfoDest: 'pan', lfoRate: 3.6, lfoDepth: 70, lfoShape: 'triangle', lfo2Dest: 'reverb', lfo2Rate: 0.6, lfo2Depth: 48, chorus: 64, phaser: 32, delayTime: 0.24, delayFb: 52, reverb: 62, drive: 14, sat: 'soft', uni: 2, semi2: 12, sub: 35, noise: 0, ring: 18, atk: 0.005, rel: 0.26, arp: true },
  { name: 'Sub Bass Tremor', cat: 'Bass', oscMode: 'triangle', cutoff: 920, reso: 4.2, fType: 'lowpass', routing: 'serial', f2Cut: 6500, f2Type: 'lowpass', lfoDest: 'amp', lfoRate: 6.4, lfoDepth: 42, lfoShape: 'sine', lfo2Dest: 'reso', lfo2Rate: 1.2, lfo2Depth: 35, chorus: 12, phaser: 0, delayTime: 0.14, delayFb: 12, reverb: 16, drive: 32, sat: 'tube', uni: 1, semi2: -12, sub: 100, noise: 0, ring: 0, atk: 0.006, rel: 0.22 },
  { name: 'Solar Wind Drone', cat: 'Drone', oscMode: 'wavetable', cutoff: 2400, reso: 6.8, fType: 'bandpass', routing: 'parallel', f2Cut: 3600, f2Type: 'lowpass', lfoDest: 'phaser', lfoRate: 0.22, lfoDepth: 85, lfoShape: 'sine', lfo2Dest: 'filter2', lfo2Rate: 0.35, lfo2Depth: 62, chorus: 78, phaser: 84, delayTime: 0.48, delayFb: 62, reverb: 88, drive: 16, sat: 'soft', uni: 4, semi2: 7, sub: 50, noise: 28, ring: 12, atk: 0.65, rel: 2.4 },
  { name: 'Neon SuperSaw', cat: 'Lead', oscMode: 'hypersaw', cutoff: 9800, reso: 2.8, fType: 'lowpass', routing: 'serial', f2Cut: 14000, f2Type: 'lowpass', lfoDest: 'detune', lfoRate: 1.1, lfoDepth: 38, lfoShape: 'triangle', lfo2Dest: 'chorus', lfo2Rate: 0.5, lfo2Depth: 40, chorus: 68, phaser: 10, delayTime: 0.31, delayFb: 38, reverb: 58, drive: 24, sat: 'soft', uni: 5, semi2: 0, sub: 70, noise: 0, ring: 0, atk: 0.008, rel: 0.42 },
  { name: 'Tape Ghost Keys', cat: 'Keys', oscMode: 'pulse', cutoff: 3900, reso: 3.1, fType: 'lowpass', routing: 'serial', f2Cut: 8600, f2Type: 'lowpass', lfoDest: 'delay', lfoRate: 1.2, lfoDepth: 56, lfoShape: 'sine', lfo2Dest: 'pitch', lfo2Rate: 3.2, lfo2Depth: 12, chorus: 58, phaser: 16, delayTime: 0.34, delayFb: 54, reverb: 64, drive: 18, sat: 'soft', uni: 2, semi2: 12, sub: 40, noise: 6, ring: 0, atk: 0.015, rel: 0.72 },
  { name: 'Rave Hoover Stab', cat: 'Lead', oscMode: 'hypersaw', cutoff: 5400, reso: 5.2, fType: 'lowpass', routing: 'serial', f2Cut: 9500, f2Type: 'lowpass', lfoDest: 'pitch', lfoRate: 5.8, lfoDepth: 28, lfoShape: 'triangle', lfo2Dest: 'chorus', lfo2Rate: 1.8, lfo2Depth: 52, chorus: 82, phaser: 28, delayTime: 0.25, delayFb: 36, reverb: 48, drive: 44, sat: 'tube', uni: 4, semi2: -12, sub: 75, noise: 4, ring: 0, atk: 0.01, rel: 0.34 },
  { name: 'Crystal Harp Arp', cat: 'Arp', oscMode: 'triangle', cutoff: 8400, reso: 3.0, fType: 'lowpass', routing: 'split', f2Cut: 1800, f2Type: 'highpass', lfoDest: 'reverb', lfoRate: 0.8, lfoDepth: 55, lfoShape: 'sine', lfo2Dest: 'pan', lfo2Rate: 2.2, lfo2Depth: 65, chorus: 54, phaser: 20, delayTime: 0.3, delayFb: 55, reverb: 76, drive: 8, sat: 'off', uni: 2, semi2: 12, sub: 25, noise: 0, ring: 20, atk: 0.005, rel: 0.65, arp: true },
  { name: 'Talking Formant', cat: 'FX', oscMode: 'saw', cutoff: 1450, reso: 9.2, fType: 'bandpass', routing: 'parallel', f2Cut: 2900, f2Type: 'bandpass', lfoDest: 'filter', lfoRate: 2.6, lfoDepth: 66, lfoShape: 'sine', lfo2Dest: 'filter2', lfo2Rate: 1.7, lfo2Depth: 62, chorus: 36, phaser: 48, delayTime: 0.28, delayFb: 42, reverb: 50, drive: 30, sat: 'tube', uni: 2, semi2: 0, sub: 60, noise: 10, ring: 14, atk: 0.02, rel: 0.45 },
  { name: 'PWM Sunset Lead', cat: 'Lead', oscMode: 'pulse', cutoff: 6200, reso: 4.1, fType: 'lowpass', routing: 'serial', f2Cut: 11200, f2Type: 'lowpass', lfoDest: 'chorus', lfoRate: 0.9, lfoDepth: 52, lfoShape: 'triangle', lfo2Dest: 'pan', lfo2Rate: 0.65, lfo2Depth: 48, chorus: 74, phaser: 15, delayTime: 0.35, delayFb: 44, reverb: 60, drive: 22, sat: 'soft', uni: 3, semi2: 0, sub: 65, noise: 0, ring: 0, atk: 0.015, rel: 0.55 },
  { name: 'Hydra Sub Pluck', cat: 'Pluck', oscMode: 'saw', cutoff: 2800, reso: 4.6, fType: 'lowpass', routing: 'serial', f2Cut: 7400, f2Type: 'lowpass', lfoDest: 'delay', lfoRate: 1.8, lfoDepth: 44, lfoShape: 'triangle', lfo2Dest: 'pan', lfo2Rate: 2.5, lfo2Depth: 40, chorus: 40, phaser: 0, delayTime: 0.22, delayFb: 48, reverb: 46, drive: 24, sat: 'soft', uni: 2, semi2: 7, sub: 85, noise: 0, ring: 0, atk: 0.005, rel: 0.35 },
  { name: 'Strobe Gate Pad', cat: 'Pad', oscMode: 'hypersaw', cutoff: 4500, reso: 3.9, fType: 'lowpass', routing: 'parallel', f2Cut: 3100, f2Type: 'bandpass', lfoDest: 'amp', lfoRate: 8.2, lfoDepth: 72, lfoShape: 'square', lfo2Dest: 'phaser', lfo2Rate: 0.55, lfo2Depth: 58, chorus: 62, phaser: 54, delayTime: 0.25, delayFb: 46, reverb: 68, drive: 20, sat: 'soft', uni: 3, semi2: 12, sub: 45, noise: 8, ring: 0, atk: 0.04, rel: 0.85 },
  { name: 'Metallic Gong FX', cat: 'FX', oscMode: 'sync', cutoff: 5900, reso: 7.4, fType: 'bandpass', routing: 'split', f2Cut: 1900, f2Type: 'highpass', lfoDest: 'reso', lfoRate: 1.3, lfoDepth: 58, lfoShape: 'triangle', lfo2Dest: 'reverb', lfo2Rate: 0.3, lfo2Depth: 64, chorus: 50, phaser: 62, delayTime: 0.44, delayFb: 60, reverb: 82, drive: 22, sat: 'tube', uni: 2, semi2: 17, sub: 20, noise: 18, ring: 72, atk: 0.01, rel: 1.8 },
  { name: 'Bioluminescent Arp', cat: 'Arp', oscMode: 'wavetable', cutoff: 4900, reso: 5.1, fType: 'lowpass', routing: 'serial', f2Cut: 9800, f2Type: 'lowpass', lfoDest: 'phaser', lfoRate: 1.1, lfoDepth: 66, lfoShape: 'sine', lfo2Dest: 'delay', lfo2Rate: 0.75, lfo2Depth: 50, chorus: 58, phaser: 60, delayTime: 0.29, delayFb: 52, reverb: 64, drive: 18, sat: 'soft', uni: 2, semi2: 7, sub: 50, noise: 0, ring: 10, atk: 0.005, rel: 0.3, arp: true },
  { name: 'FM Kalimba Keys', cat: 'Keys', oscMode: 'triangle', cutoff: 5200, reso: 2.5, fType: 'lowpass', routing: 'serial', f2Cut: 10500, f2Type: 'lowpass', lfoDest: 'chorus', lfoRate: 1.4, lfoDepth: 36, lfoShape: 'sine', lfo2Dest: 'pan', lfo2Rate: 2.1, lfo2Depth: 38, chorus: 46, phaser: 0, delayTime: 0.27, delayFb: 42, reverb: 56, drive: 12, sat: 'soft', uni: 1, semi2: 12, sub: 35, noise: 4, ring: 28, atk: 0.005, rel: 0.6 },
  { name: 'Deep Trench Sub', cat: 'Bass', oscMode: 'sine' as unknown as NonNullable<SynthPatch['oscMode']>, cutoff: 680, reso: 3.4, fType: 'lowpass', routing: 'serial', f2Cut: 5200, f2Type: 'lowpass', lfoDest: 'filter', lfoRate: 0.6, lfoDepth: 48, lfoShape: 'sine', lfo2Dest: 'amp', lfo2Rate: 3.2, lfo2Depth: 25, chorus: 18, phaser: 0, delayTime: 0.15, delayFb: 12, reverb: 24, drive: 28, sat: 'tube', uni: 1, semi2: -12, sub: 100, noise: 0, ring: 0, atk: 0.008, rel: 0.35 },
  { name: 'Prism Shimmer', cat: 'Ambient', oscMode: 'hypersaw', cutoff: 6800, reso: 3.2, fType: 'lowpass', routing: 'parallel', f2Cut: 3600, f2Type: 'highpass', lfoDest: 'reverb', lfoRate: 0.4, lfoDepth: 80, lfoShape: 'sine', lfo2Dest: 'chorus', lfo2Rate: 0.7, lfo2Depth: 65, chorus: 88, phaser: 42, delayTime: 0.42, delayFb: 58, reverb: 86, drive: 10, sat: 'soft', uni: 4, semi2: 12, sub: 30, noise: 10, ring: 0, atk: 0.38, rel: 2.1 },
  { name: 'Electro Zap Lead', cat: 'Acid / Groove', oscMode: 'sync', cutoff: 3400, reso: 8.8, fType: 'lowpass', routing: 'serial', f2Cut: 8200, f2Type: 'lowpass', lfoDest: 'reso', lfoRate: 4.5, lfoDepth: 55, lfoShape: 'saw', lfo2Dest: 'pitch', lfo2Rate: 6.5, lfo2Depth: 18, chorus: 32, phaser: 34, delayTime: 0.21, delayFb: 40, reverb: 34, drive: 40, sat: 'tube', uni: 2, semi2: 12, sub: 60, noise: 0, ring: 20, atk: 0.005, rel: 0.24 },
  { name: 'Velvet Brass Swell', cat: 'Pad', oscMode: 'saw', cutoff: 3600, reso: 3.6, fType: 'lowpass', routing: 'serial', f2Cut: 9400, f2Type: 'lowpass', lfoDest: 'pitch', lfoRate: 4.8, lfoDepth: 14, lfoShape: 'sine', lfo2Dest: 'phaser', lfo2Rate: 0.35, lfo2Depth: 48, chorus: 66, phaser: 36, delayTime: 0.33, delayFb: 34, reverb: 62, drive: 26, sat: 'tube', uni: 4, semi2: 0, sub: 55, noise: 0, ring: 0, atk: 0.16, rel: 0.9 },
  { name: 'Spiral Echo Pluck', cat: 'Arp', oscMode: 'pulse', cutoff: 4600, reso: 4.9, fType: 'lowpass', routing: 'parallel', f2Cut: 2400, f2Type: 'bandpass', lfoDest: 'delay', lfoRate: 1.9, lfoDepth: 72, lfoShape: 'triangle', lfo2Dest: 'filter2', lfo2Rate: 1.1, lfo2Depth: 54, chorus: 50, phaser: 28, delayTime: 0.26, delayFb: 62, reverb: 58, drive: 18, sat: 'soft', uni: 2, semi2: 7, sub: 50, noise: 0, ring: 0, atk: 0.005, rel: 0.28, arp: true },
  { name: 'Starfish Supernova', cat: 'FX', oscMode: 'wavetable', cutoff: 5800, reso: 6.5, fType: 'lowpass', routing: 'split', f2Cut: 2200, f2Type: 'bandpass', lfoDest: 'phaser', lfoRate: 1.5, lfoDepth: 84, lfoShape: 'sine', lfo2Dest: 'delay', lfo2Rate: 0.9, lfo2Depth: 68, chorus: 76, phaser: 78, delayTime: 0.37, delayFb: 60, reverb: 78, drive: 24, sat: 'soft', uni: 4, semi2: 12, sub: 55, noise: 16, ring: 32, atk: 0.12, rel: 1.4 },
];

function buildStarfishBankPresets(): SynthPatch[] {
  const saved = loadUserSavedMap();
  return STARFISH_WILD_SPECS.map((sp, i) => {
    const progStr = i.toString().padStart(3, '0');
    const safeMode: NonNullable<SynthPatch['oscMode']> =
      sp.oscMode === ('sine' as unknown as SynthPatch['oscMode']) ? 'triangle' : sp.oscMode;
    const base: SynthPatch = {
      id: `rom-I-${progStr}`,
      name: sp.name,
      bank: 'I',
      programNumber: i,
      category: sp.cat,
      engine: 'virus_va',
      wave: safeMode === 'triangle' ? 'triangle' : 'saw',
      oscMode: safeMode,
      pulseWidth: 0.36,
      wavetablePos: 0.45,
      osc2Semitone: sp.semi2,
      osc2Detune: 14,
      oscBalance: 52,
      osc2Sync: safeMode === 'sync',
      fmAmount: safeMode === 'sync' ? 32 : 0,
      noiseVolume: sp.noise,
      ringModVolume: sp.ring,
      octave: 0,
      masterGain: 3700,
      algorithm: (i % 6) * 5 + 1,
      feedback: 5,
      filterCutoff: sp.cutoff,
      filterReso: sp.reso,
      filterType: sp.fType,
      filter2Cutoff: sp.f2Cut,
      filter2Reso: 2.2,
      filter2Type: sp.f2Type,
      filterRouting: sp.routing,
      filterBalance: sp.routing === 'serial' ? 20 : 50,
      filterKeyfollow: 40,
      filterEnvAmount: 48,
      filter2EnvAmount: 34,
      filterAttack: Math.min(0.4, sp.atk),
      filterDecay: 0.48,
      filterSustain: 0.42,
      filterRelease: Math.min(1.5, sp.rel),
      attack: sp.atk,
      decay: 0.9,
      sustain: 0.78,
      release: sp.rel,
      fxChorus: sp.chorus,
      fxPhaserMix: sp.phaser,
      fxPhaserRate: 0.55,
      fxPhaserDepth: 72,
      fxPhaserFeedback: 52,
      fxDelayTime: sp.delayTime,
      fxDelayFeedback: sp.delayFb,
      fxDelayMix: sp.delayFb > 0 ? Math.min(72, Math.round(sp.delayFb * 0.95)) : 0,
      fxReverbSize: sp.reverb,
      fxDrive: sp.drive,
      glide: sp.cat === 'Lead' ? 12 : 0,
      polyphony: sp.uni >= 4 ? 6 : 8,
      hypersawSpread: 84,
      unisonVoices: sp.uni,
      unisonPanSpread: 82,
      lfoRate: sp.lfoRate,
      lfoDepth: sp.lfoDepth,
      lfoDest: sp.lfoDest,
      lfoShape: sp.lfoShape,
      lfo2Rate: sp.lfo2Rate,
      lfo2Depth: sp.lfo2Depth,
      lfo2Dest: sp.lfo2Dest,
      lfo2Shape: 'triangle',
      arpEnabled: Boolean(sp.arp),
      arpLatch: false,
      arpMode: 'updown',
      arpRate: '1/16',
      arpOctaves: 2,
      arpGate: 68,
      saturationType: sp.sat,
      subOscLevel: sp.sub,
      subWaveform: 'square',
      swarmDepth: 26,
      swarmRate: 0.42,
      operators: makeOps(2.0, -16.8, 16.8, 82),
    };
    return saved[base.id] ? { ...base, ...saved[base.id] } : base;
  });
}

function createEmptyUserSlotPatch(slotIndex: number): SynthPatch {
  const progNum = ((slotIndex % 128) + 128) % 128;
  const progStr = progNum.toString().padStart(3, '0');
  return {
    ...createInitSlotPatch('X', progNum, `user-X-${progStr}`),
    name: `Empty ${progStr}`,
    bank: 'X',
    programNumber: progNum,
    isUserPreset: false,
  };
}

function buildBankXUserPresets(): SynthPatch[] {
  const saved = loadBankXSavedMap();
  const list: SynthPatch[] = [];
  for (let i = 0; i < 128; i++) {
    const key = String(i);
    const progStr = i.toString().padStart(3, '0');
    const custom = saved[key];
    if (custom) {
      list.push({
        ...createEmptyUserSlotPatch(i),
        ...custom,
        id: `user-X-${progStr}`,
        bank: 'X',
        programNumber: i,
        isUserPreset: true,
      });
    } else {
      list.push(createEmptyUserSlotPatch(i));
    }
  }
  return list;
}

export function getBankPresets(bankId: VirusBankId): SynthPatch[] {
  if (!decodedBanksCache[bankId]) {
    if (bankId === 'X') {
      decodedBanksCache.X = buildBankXUserPresets();
      return decodedBanksCache.X!;
    }
    if (bankId === 'I') {
      decodedBanksCache[bankId] = buildStarfishBankPresets();
      return decodedBanksCache[bankId]!;
    }
    const b64 = VIRUS_ROM_PACK[bankId] || VIRUS_ROM_PACK.A;
    const raw = decodeBase64Bytes(b64);
    const saved = loadUserSavedMap();
    const list: SynthPatch[] = [];
    const count = Math.floor(raw.length / VIRUS_ROM_RECORD_SIZE);
    for (let i = 0; i < count; i++) {
      const slice = raw.subarray(i * VIRUS_ROM_RECORD_SIZE, (i + 1) * VIRUS_ROM_RECORD_SIZE);
      const base = decodeRecordToPatch(bankId, i, slice);
      list.push(saved[base.id] ? { ...base, ...saved[base.id] } : base);
    }
    decodedBanksCache[bankId] = list;
  }
  return decodedBanksCache[bankId]!;
}

export function getBankXCount(): number {
  const bankX = getBankPresets('X');
  return bankX.filter((p) => Boolean(p.isUserPreset)).length;
}

export function getNextFreeBankXSlot(): number {
  const bankX = getBankPresets('X');
  const openIdx = bankX.findIndex((p) => !p.isUserPreset);
  return openIdx >= 0 ? openIdx : 0;
}

export function savePatchToBankX(patch: SynthPatch, slotIndex?: number): SynthPatch {
  const bankList = getBankPresets('X');
  const resolvedSlot =
    slotIndex !== undefined
      ? ((slotIndex % 128) + 128) % 128
      : patch.bank === 'X' && patch.programNumber !== undefined
      ? ((patch.programNumber % 128) + 128) % 128
      : getNextFreeBankXSlot();
  const progStr = resolvedSlot.toString().padStart(3, '0');
  const cleanName =
    patch.name.startsWith('Empty ')
      ? `Starfish ${progStr}`
      : patch.name;
  const savedPatch: SynthPatch = {
    ...patch,
    id: `user-X-${progStr}`,
    name: cleanName,
    bank: 'X',
    programNumber: resolvedSlot,
    isUserPreset: true,
  };
  bankList[resolvedSlot] = savedPatch;
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      const map = loadBankXSavedMap();
      map[String(resolvedSlot)] = savedPatch;
      window.localStorage.setItem(USER_BANK_X_KEY, JSON.stringify(map));
    }
  } catch {
    // ignore quota errors
  }
  return savedPatch;
}

export function clearBankXSlot(slotIndex: number): SynthPatch {
  const resolvedSlot = ((slotIndex % 128) + 128) % 128;
  const blank = createEmptyUserSlotPatch(resolvedSlot);
  const bankList = getBankPresets('X');
  bankList[resolvedSlot] = blank;
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      const map = loadBankXSavedMap();
      delete map[String(resolvedSlot)];
      window.localStorage.setItem(USER_BANK_X_KEY, JSON.stringify(map));
    }
  } catch {
    // ignore storage errors
  }
  return blank;
}

export function saveUserPreset(patch: SynthPatch): SynthPatch {
  const bankId: VirusBankId = patch.bank || 'A';
  if (bankId === 'X') {
    return savePatchToBankX(patch, patch.programNumber);
  }
  const bankList = getBankPresets(bankId);
  const idx = bankList.findIndex((p) => p.id === patch.id);
  if (idx >= 0) {
    bankList[idx] = { ...patch };
  }
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      const saved = loadUserSavedMap();
      saved[patch.id] = patch;
      window.localStorage.setItem(USER_STORAGE_KEY, JSON.stringify(saved));
    }
  } catch {
    // ignore quota errors
  }
  return patch;
}

export function revertPresetToFactory(bankId: VirusBankId, progNum: number, patchId?: string): SynthPatch {
  if (bankId === 'X') {
    return clearBankXSlot(progNum);
  }
  if (bankId === 'I') {
    const count = STARFISH_WILD_SPECS.length;
    const clampedProg = ((progNum % count) + count) % count;
    const targetId = patchId || `rom-I-${clampedProg.toString().padStart(3, '0')}`;
    try {
      if (typeof window !== 'undefined' && window.localStorage) {
        const saved = loadUserSavedMap();
        delete saved[targetId];
        window.localStorage.setItem(USER_STORAGE_KEY, JSON.stringify(saved));
      }
    } catch {
      // ignore storage errors
    }
    delete decodedBanksCache.I;
    const fresh = getBankPresets('I');
    return fresh[clampedProg] || fresh[0];
  }
  const b64 = VIRUS_ROM_PACK[bankId] || VIRUS_ROM_PACK.A;
  const raw = decodeBase64Bytes(b64);
  const count = Math.max(1, Math.floor(raw.length / VIRUS_ROM_RECORD_SIZE));
  const clampedProg = ((progNum % count) + count) % count;
  const slice = raw.subarray(clampedProg * VIRUS_ROM_RECORD_SIZE, (clampedProg + 1) * VIRUS_ROM_RECORD_SIZE);
  const factoryPatch = decodeRecordToPatch(bankId, clampedProg, slice);
  const targetId = patchId || factoryPatch.id;

  const bankList = getBankPresets(bankId);
  if (bankList[clampedProg]) {
    bankList[clampedProg] = { ...factoryPatch };
  }

  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      const saved = loadUserSavedMap();
      if (targetId in saved) {
        delete saved[targetId];
      }
      if (factoryPatch.id in saved) {
        delete saved[factoryPatch.id];
      }
      window.localStorage.setItem(USER_STORAGE_KEY, JSON.stringify(saved));
    }
  } catch {
    // ignore storage errors
  }

  return factoryPatch;
}

export type RandomPatchStyle = 'ALL' | 'PAD' | 'LEAD' | 'BASS' | 'PLUCK' | 'ARP' | 'KEYS' | 'FX';

export const RANDOM_PATCH_STYLES: RandomPatchStyle[] = [
  'ALL',
  'PAD',
  'LEAD',
  'BASS',
  'PLUCK',
  'ARP',
  'KEYS',
  'FX',
];

const RAND_PREFIXES = [
  'Coral', 'Tide', 'Reef', 'Prism', 'Abyss', 'Lagoon', 'Pearl', 'Drift',
  'Solar', 'Velvet', 'Neon', 'Kelp', 'Azure', 'Echo', 'Nova', 'Siren'
];
const RAND_NOUNS: Record<Exclude<RandomPatchStyle, 'ALL'>, string[]> = {
  PAD: ['Pad', 'Swell', 'Mist', 'Choir'],
  LEAD: ['Lead', 'Beam', 'Laser', 'Saw'],
  BASS: ['Sub', 'Bass', 'Pulse', 'Growl'],
  PLUCK: ['Pluck', 'Harp', 'Bell', 'Drop'],
  ARP: ['Arp', 'Seq', 'Spark', 'Orbit'],
  KEYS: ['Keys', 'Glass', 'EP', 'Tine'],
  FX: ['Sweep', 'Wave', 'Morph', 'Dream'],
};

/**
 * Generates a musical, loudness-calibrated, screech-safe random SynthPatch.
 * Enforces strict Q-factor, highpass, feedback, FM, and gain limits so output stays clean & ear-friendly.
 */
export function generateSafeRandomPatch(
  style: RandomPatchStyle = 'ALL',
  chaosPct = 60,
  serial = 1
): SynthPatch {
  const rand = (min: number, max: number) => min + Math.random() * (max - min);
  const randInt = (min: number, max: number) => Math.floor(rand(min, max + 0.999));
  const pick = <T,>(arr: readonly T[]): T => arr[Math.floor(Math.random() * arr.length)];
  const chaos = Math.max(0.2, Math.min(1.0, chaosPct / 100));

  const resolvedStyle: Exclude<RandomPatchStyle, 'ALL'> =
    style === 'ALL'
      ? pick(['PAD', 'LEAD', 'BASS', 'PLUCK', 'ARP', 'KEYS', 'FX'] as const)
      : style;

  const oscModes: Array<NonNullable<SynthPatch['oscMode']>> =
    resolvedStyle === 'BASS'
      ? ['hypersaw', 'saw', 'pulse', 'triangle']
      : resolvedStyle === 'PAD' || resolvedStyle === 'KEYS'
      ? ['hypersaw', 'pulse', 'wavetable', 'triangle']
      : ['hypersaw', 'saw', 'pulse', 'wavetable', 'sync', 'triangle'];
  const oscMode = pick(oscModes);

  // Musical Osc 2 pitch intervals only (unison, fifth, octave up/down)
  const osc2SemiChoices =
    resolvedStyle === 'BASS'
      ? [0, 0, -12, 7, 12]
      : resolvedStyle === 'FX'
      ? [-12, -5, 0, 7, 12]
      : [0, 0, 7, 12, -12];
  const osc2Semitone = pick(osc2SemiChoices);

  // Strict resonance & cutoff guardrails to prevent whistling or silent highpass traps
  const f1Types: Array<SynthPatch['filterType']> =
    resolvedStyle === 'BASS'
      ? ['lowpass']
      : resolvedStyle === 'FX' && chaos > 0.55
      ? ['lowpass', 'bandpass', 'notch']
      : ['lowpass', 'lowpass', 'lowpass', 'bandpass'];
  const filterType = pick(f1Types);
  const filterRouting: NonNullable<SynthPatch['filterRouting']> =
    chaos > 0.5 ? pick(['serial', 'parallel', 'split']) : pick(['serial', 'parallel']);
  const filter2Type: NonNullable<SynthPatch['filter2Type']> =
    filterRouting === 'split'
      ? pick(['lowpass', 'bandpass', 'highpass'])
      : pick(['lowpass', 'bandpass']);

  let filterCutoff = 3200;
  let attack = 0.01;
  let decay = 0.7;
  let sustain = 0.7;
  let release = 0.4;
  let filterEnvAmount = 35;
  let filterAttack = 0.01;
  let filterDecay = 0.45;
  let filterSustain = 0.38;
  let fxChorus = 42;
  let fxReverbSize = 48;
  let fxDelayMix = 28;
  let fxDelayTime = 0.3;
  let fxDelayFeedback = 34;
  let subOscLevel = 55;
  let unisonVoices = pick([1, 2, 3]);
  let category: SynthPatch['category'] = 'Lead';

  if (resolvedStyle === 'PAD') {
    category = 'Pad';
    filterCutoff = Math.round(rand(1100, 6200));
    attack = parseFloat(rand(0.18, 0.75).toFixed(2));
    decay = parseFloat(rand(0.65, 1.9).toFixed(2));
    sustain = parseFloat(rand(0.58, 0.9).toFixed(2));
    release = parseFloat(rand(0.75, 2.1).toFixed(2));
    filterAttack = parseFloat(rand(0.12, 0.65).toFixed(2));
    filterEnvAmount = Math.round(rand(18, 52));
    fxChorus = Math.round(rand(45, 84));
    fxReverbSize = Math.round(rand(52, 82));
    fxDelayMix = Math.round(rand(18, 44));
    unisonVoices = pick([2, 3, 4]);
    subOscLevel = Math.round(rand(25, 65));
  } else if (resolvedStyle === 'BASS') {
    category = 'Bass';
    filterCutoff = Math.round(rand(420, 3200));
    attack = parseFloat(rand(0.005, 0.022).toFixed(3));
    decay = parseFloat(rand(0.22, 0.72).toFixed(2));
    sustain = parseFloat(rand(0.25, 0.72).toFixed(2));
    release = parseFloat(rand(0.09, 0.3).toFixed(2));
    filterEnvAmount = Math.round(rand(28, 72));
    filterDecay = parseFloat(rand(0.16, 0.5).toFixed(2));
    fxChorus = Math.round(rand(8, 36));
    fxReverbSize = Math.round(rand(4, 24));
    fxDelayMix = Math.round(rand(0, 18));
    fxDelayFeedback = Math.round(rand(10, 26));
    unisonVoices = pick([1, 2]);
    subOscLevel = Math.round(rand(65, 95));
  } else if (resolvedStyle === 'PLUCK') {
    category = 'Pluck';
    filterCutoff = Math.round(rand(700, 4600));
    attack = parseFloat(rand(0.005, 0.015).toFixed(3));
    decay = parseFloat(rand(0.24, 0.68).toFixed(2));
    sustain = parseFloat(rand(0.08, 0.28).toFixed(2));
    release = parseFloat(rand(0.2, 0.7).toFixed(2));
    filterEnvAmount = Math.round(rand(32, 76));
    filterDecay = parseFloat(rand(0.18, 0.55).toFixed(2));
    fxChorus = Math.round(rand(25, 62));
    fxReverbSize = Math.round(rand(34, 68));
    fxDelayMix = Math.round(rand(24, 54));
    unisonVoices = pick([1, 2, 3]);
    subOscLevel = Math.round(rand(25, 60));
  } else if (resolvedStyle === 'ARP') {
    category = 'Arp';
    filterCutoff = Math.round(rand(1200, 6800));
    attack = parseFloat(rand(0.005, 0.02).toFixed(3));
    decay = parseFloat(rand(0.26, 0.72).toFixed(2));
    sustain = parseFloat(rand(0.18, 0.52).toFixed(2));
    release = parseFloat(rand(0.18, 0.55).toFixed(2));
    filterEnvAmount = Math.round(rand(25, 68));
    fxChorus = Math.round(rand(30, 68));
    fxReverbSize = Math.round(rand(32, 65));
    fxDelayMix = Math.round(rand(26, 56));
    unisonVoices = pick([1, 2, 3]);
    subOscLevel = Math.round(rand(35, 70));
  } else if (resolvedStyle === 'KEYS') {
    category = 'Keys';
    filterCutoff = Math.round(rand(1500, 7400));
    attack = parseFloat(rand(0.006, 0.032).toFixed(3));
    decay = parseFloat(rand(0.45, 1.35).toFixed(2));
    sustain = parseFloat(rand(0.32, 0.72).toFixed(2));
    release = parseFloat(rand(0.28, 0.85).toFixed(2));
    filterEnvAmount = Math.round(rand(20, 56));
    fxChorus = Math.round(rand(32, 74));
    fxReverbSize = Math.round(rand(34, 66));
    fxDelayMix = Math.round(rand(16, 44));
    unisonVoices = pick([1, 2, 3]);
    subOscLevel = Math.round(rand(25, 58));
  } else if (resolvedStyle === 'FX') {
    category = 'FX';
    filterCutoff = Math.round(rand(1300, 6200));
    attack = parseFloat(rand(0.02, 0.38).toFixed(2));
    decay = parseFloat(rand(0.55, 1.65).toFixed(2));
    sustain = parseFloat(rand(0.42, 0.82).toFixed(2));
    release = parseFloat(rand(0.5, 1.65).toFixed(2));
    filterEnvAmount = Math.round(rand(-35, 65));
    fxChorus = Math.round(rand(40, 82));
    fxReverbSize = Math.round(rand(48, 82));
    fxDelayMix = Math.round(rand(32, 58));
    unisonVoices = pick([2, 3, 4]);
    subOscLevel = Math.round(rand(30, 65));
  } else {
    category = 'Lead';
    filterCutoff = Math.round(rand(1800, 10800));
    attack = parseFloat(rand(0.006, 0.045).toFixed(3));
    decay = parseFloat(rand(0.35, 1.15).toFixed(2));
    sustain = parseFloat(rand(0.52, 0.84).toFixed(2));
    release = parseFloat(rand(0.2, 0.65).toFixed(2));
    filterEnvAmount = Math.round(rand(20, 58));
    fxChorus = Math.round(rand(26, 66));
    fxReverbSize = Math.round(rand(30, 62));
    fxDelayMix = Math.round(rand(20, 48));
    unisonVoices = pick([1, 2, 3, 4]);
    subOscLevel = Math.round(rand(40, 78));
  }

  // Filter resonance clamped safely by filter topology (prevents high-Q piercing whistles)
  const maxSafeReso = filterType === 'bandpass' ? 5.2 : 6.6;
  const filterReso = parseFloat(rand(1.0, 1.0 + (maxSafeReso - 1.0) * chaos).toFixed(1));
  const filter2Cutoff =
    filterRouting === 'split' && filter2Type === 'highpass'
      ? Math.round(rand(450, 1650))
      : Math.round(rand(2400, 12500));
  const filter2Reso = parseFloat(rand(0.8, 3.8).toFixed(1));

  // LFO targets & safe depth scaling
  const safeLfoDests: NonNullable<SynthPatch['lfoDest']>[] = [
    'filter', 'filter2', 'pan', 'detune', 'amp', 'chorus', 'phaser', 'delay', 'reverb', 'reso', 'pitch'
  ];
  const lfoDest = pick(safeLfoDests);
  const lfo2Dest = pick(safeLfoDests.filter((d) => d !== lfoDest));
  const lfoDepth =
    lfoDest === 'pitch'
      ? Math.round(rand(4, 16 * chaos + 4))
      : lfoDest === 'reso'
      ? Math.round(rand(14, 42 * chaos + 10))
      : Math.round(rand(16, 60 * chaos + 12));
  const lfo2Depth =
    lfo2Dest === 'pitch'
      ? Math.round(rand(4, 14 * chaos + 4))
      : lfo2Dest === 'reso'
      ? Math.round(rand(10, 34 * chaos + 8))
      : Math.round(rand(12, 52 * chaos + 10));

  const fmAmount =
    oscMode === 'sync' || resolvedStyle === 'FX'
      ? Math.round(rand(8, 34 * chaos + 8))
      : chaos > 0.65 && Math.random() > 0.6
      ? Math.round(rand(4, 24))
      : 0;
  const ringModVolume =
    resolvedStyle === 'FX' || resolvedStyle === 'KEYS'
      ? Math.round(rand(0, 28 * chaos))
      : 0;
  const fxPhaserMix =
    resolvedStyle === 'PAD' || resolvedStyle === 'FX' || Math.random() > 0.55
      ? Math.round(rand(15, 68 * chaos + 10))
      : 0;

  fxDelayTime = parseFloat(rand(0.16, 0.52).toFixed(2));
  fxDelayFeedback = Math.round(rand(16, Math.min(58, 28 + chaos * 30)));

  const nextSlot = getNextFreeBankXSlot();
  const slotStr = nextSlot.toString().padStart(3, '0');
  const tagNum = ((serial % 99) + 1).toString().padStart(2, '0');
  const name = `${pick(RAND_PREFIXES)} ${pick(RAND_NOUNS[resolvedStyle])} ${tagNum}`.slice(0, 18);

  return {
    id: `rand-X-${slotStr}-${Date.now()}`,
    name,
    bank: 'X',
    programNumber: nextSlot,
    isUserPreset: false,
    category,
    engine: 'virus_va',
    wave: oscMode === 'triangle' ? 'triangle' : 'saw',
    oscMode,
    pulseWidth: parseFloat(rand(0.18, 0.78).toFixed(2)),
    wavetablePos: parseFloat(rand(0.12, 0.88).toFixed(2)),
    osc2Semitone,
    osc2Detune: Math.round(rand(4, 18)),
    oscBalance: Math.round(rand(28, 72)),
    osc2Sync: oscMode === 'sync',
    fmAmount,
    noiseVolume: resolvedStyle === 'PAD' || resolvedStyle === 'FX' ? Math.round(rand(0, 14)) : 0,
    ringModVolume,
    octave: resolvedStyle === 'BASS' ? -1 : 0,
    masterGain: 3650,
    algorithm: randInt(1, 32),
    feedback: 5,
    filterCutoff,
    filterReso,
    filterType,
    filter2Cutoff,
    filter2Reso,
    filter2Type,
    filterRouting,
    filterBalance: filterRouting === 'serial' ? Math.round(rand(10, 42)) : Math.round(rand(35, 65)),
    filterKeyfollow: Math.round(rand(20, 60)),
    filterEnvAmount,
    filter2EnvAmount: Math.round(rand(15, 45)),
    filterAttack,
    filterDecay,
    filterSustain,
    filterRelease: Math.min(1.5, release),
    attack,
    decay,
    sustain,
    release,
    fxChorus,
    fxPhaserMix,
    fxPhaserRate: parseFloat(rand(0.25, 1.8).toFixed(2)),
    fxPhaserDepth: Math.round(rand(35, 72)),
    fxPhaserFeedback: Math.round(rand(18, 54)),
    fxDelayTime,
    fxDelayFeedback,
    fxDelayMix,
    fxReverbSize,
    fxDrive: Math.round(rand(10, Math.min(36, 16 + chaos * 20))),
    saturationType: pick(['soft', 'soft', 'tube', 'off']),
    glide: resolvedStyle === 'LEAD' && Math.random() > 0.65 ? Math.round(rand(6, 22)) : 0,
    polyphony: unisonVoices >= 4 ? 6 : 8,
    hypersawSpread: Math.round(rand(42, 88)),
    unisonVoices,
    unisonPanSpread: Math.round(rand(48, 86)),
    lfoRate: parseFloat(rand(0.25, lfoDest === 'amp' ? 6.5 : 4.8).toFixed(1)),
    lfoDepth,
    lfoDest,
    lfoShape: pick(['sine', 'triangle']),
    lfo2Rate: parseFloat(rand(0.2, 3.8).toFixed(1)),
    lfo2Depth,
    lfo2Dest,
    lfo2Shape: pick(['sine', 'triangle']),
    arpEnabled: resolvedStyle === 'ARP',
    arpLatch: false,
    arpMode: pick(['up', 'down', 'updown', 'random']),
    arpRate: pick(['1/8', '1/16', '1/16T']),
    arpOctaves: pick([1, 2]),
    arpGate: Math.round(rand(48, 82)),
    subOscLevel,
    subWaveform: pick(['square', 'sine', 'saw']),
    swarmDepth: Math.round(rand(12, 34)),
    swarmRate: parseFloat(rand(0.22, 0.72).toFixed(2)),
    operators: makeOps(2.0, -16.8, 16.8, 82),
  };
}

export function createInitSlotPatch(bankId: VirusBankId, progNum: number, id?: string): SynthPatch {
  const baseInit = decodeRecordToPatch(bankId, progNum, new Uint8Array(VIRUS_ROM_RECORD_SIZE));
  return {
    ...baseInit,
    id: id || `rom-${bankId}-${progNum.toString().padStart(3, '0')}`,
    name: 'Virus Init',
    bank: bankId,
    programNumber: progNum,
    category: 'Lead',
    engine: 'virus_va',
    oscMode: 'hypersaw',
    oscBalance: 50,
    osc2Semitone: 0,
    osc2Detune: 14,
    osc2Sync: false,
    filterCutoff: 5200,
    filterReso: 3.5,
    filterType: 'lowpass',
    filter2Cutoff: 12000,
    filter2Reso: 1.2,
    filter2Type: 'lowpass',
    filterRouting: 'serial',
    filterBalance: 0,
    filterEnvAmount: 42,
    attack: 0.008,
    decay: 0.85,
    sustain: 0.78,
    release: 0.35,
    fxChorus: 48,
    fxPhaserMix: 0,
    fxDelayTime: 0.32,
    fxDelayFeedback: 36,
    fxReverbSize: 45,
    fxDrive: 28,
    saturationType: 'soft',
    subOscLevel: 65,
    noiseVolume: 0,
    ringModVolume: 0,
    unisonVoices: 1,
    lfoRate: 4.2,
    lfoDepth: 10,
    lfoDest: 'filter',
    lfoShape: 'sine',
    lfo2Rate: 0.9,
    lfo2Depth: 12,
    lfo2Dest: 'pan',
    lfo2Shape: 'triangle',
    arpEnabled: false,
    arpLatch: false,
  };
}

export function getPresetBankCode(patchOrIndex: SynthPatch | number, bankId?: VirusBankId): string {
  if (typeof patchOrIndex === 'number') {
    const b = bankId || 'A';
    const prog = (patchOrIndex % 128).toString().padStart(3, '0');
    return `${b}${prog}`;
  }
  const b = patchOrIndex.bank || 'A';
  const prog = (patchOrIndex.programNumber ?? 0).toString().padStart(3, '0');
  return `${b}${prog}`;
}

export function getDefaultVirusPatch(): SynthPatch {
  const bankC = getBankPresets('C');
  // Start on clean "normal" Access Virus Init Hypersaw (C126) so launch sounds balanced & musical
  return bankC[126] || getBankPresets('A')[126] || bankC[0];
}

export function recordRecentPreset(patch: SynthPatch): void {
  if (!patch || patch.id.startsWith('rand-') || patch.name.startsWith('Empty ')) return;
  try {
    if (typeof window === 'undefined' || !window.localStorage) return;
    const raw = window.localStorage.getItem(RECENT_PRESETS_KEY);
    const existing: SynthPatch[] = raw ? (JSON.parse(raw) as SynthPatch[]) : [];
    const deduped = [patch, ...existing.filter((p) => p && p.id !== patch.id)].slice(0, 6);
    window.localStorage.setItem(RECENT_PRESETS_KEY, JSON.stringify(deduped));
  } catch {
    // ignore quota errors
  }
}

export function getRecentBootPresets(): SynthPatch[] {
  let recent: SynthPatch[] = [];
  try {
    if (typeof window !== 'undefined' && window.localStorage) {
      const raw = window.localStorage.getItem(RECENT_PRESETS_KEY);
      if (raw) {
        const parsed = JSON.parse(raw) as SynthPatch[];
        recent = parsed.filter((p) => p && p.id && !p.name.startsWith('Empty '));
      }
    }
  } catch {
    recent = [];
  }
  if (recent.length > 0) {
    return recent.slice(0, 6);
  }
  // Curated fallback pool of 6 distinct sounds when user hasn't browsed presets yet
  const bankA = getBankPresets('A');
  const bankC = getBankPresets('C');
  const bankI = getBankPresets('I');
  return [
    getDefaultVirusPatch(),
    bankC[1] || bankA[0],
    bankA[4] || bankA[1],
    bankI[4] || bankC[2],
    bankI[18] || bankC[3],
    bankI[25] || bankA[5],
  ].filter(Boolean);
}

// Export presets for compact firmware .fwsc initialization compatibility
export const VIRUS_FACTORY_PRESETS: SynthPatch[] = [
  getBankPresets('C')[126],
  getBankPresets('A')[126],
  getBankPresets('C')[1],
  getBankPresets('E')[126],
  getBankPresets('D')[126],
  getBankPresets('F')[126],
  getBankPresets('G')[126],
  getBankPresets('H')[126],
  ...getBankPresets('A').slice(0, 25),
];
