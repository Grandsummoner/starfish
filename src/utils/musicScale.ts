import { ChordMemoryMode, KEY_MIDI_BASE, ModCord, ScaleMode, SynthPatch } from '../types/firmware';

export const SCALE_MODE_ORDER: ScaleMode[] = [
  'chromatic',
  'major',
  'minor',
  'harmonic_minor',
  'melodic_minor',
  'dorian',
  'phrygian',
  'lydian',
  'mixolydian',
  'locrian',
  'pentatonic',
  'minor_pentatonic'
];

export const SCALE_INTERVALS: Record<ScaleMode, number[]> = {
  chromatic: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
  major: [0, 2, 4, 5, 7, 9, 11],
  minor: [0, 2, 3, 5, 7, 8, 10],
  harmonic_minor: [0, 2, 3, 5, 7, 8, 11],
  melodic_minor: [0, 2, 3, 5, 7, 9, 11],
  dorian: [0, 2, 3, 5, 7, 9, 10],
  phrygian: [0, 1, 3, 5, 7, 8, 10],
  lydian: [0, 2, 4, 6, 7, 9, 11],
  mixolydian: [0, 2, 4, 5, 7, 9, 10],
  locrian: [0, 1, 3, 5, 6, 8, 10],
  pentatonic: [0, 2, 4, 7, 9],
  minor_pentatonic: [0, 3, 5, 7, 10],
};

export const SCALE_LABELS: Record<ScaleMode, string> = {
  chromatic: 'Chromatic',
  major: 'Major',
  minor: 'Nat Minor',
  harmonic_minor: 'Harm Min',
  melodic_minor: 'Mel Min',
  dorian: 'Dorian',
  phrygian: 'Phrygian',
  lydian: 'Lydian',
  mixolydian: 'Mixolyd',
  locrian: 'Locrian',
  pentatonic: 'Maj Pent',
  minor_pentatonic: 'Min Pent',
};

export const ROOT_OPTIONS = [
  { pc: 5, label: 'F' },
  { pc: 0, label: 'C' },
  { pc: 7, label: 'G' },
  { pc: 9, label: 'A' },
  { pc: 2, label: 'D' },
  { pc: 4, label: 'E' },
];

export const CHORD_OFFSETS: Record<ChordMemoryMode, number[]> = {
  off: [0],
  octave: [0, 12],
  fifth: [0, 7, 12],
  minor7: [0, 3, 7, 10],
  major7: [0, 4, 7, 11],
};

export const CHORD_LABELS: Record<ChordMemoryMode, string> = {
  off: 'Off',
  octave: 'Octave',
  fifth: 'Power 5th',
  minor7: 'Min 7th',
  major7: 'Maj 7th',
};

/**
 * Returns whether a physical key index (0..26, F3..G5) lies naturally in the selected scale.
 */
export function isKeyInScale(keyIndex: number, scale: ScaleMode, rootPc: number): boolean {
  if (scale === 'chromatic') return true;
  const midi = KEY_MIDI_BASE + keyIndex;
  const rel = ((midi % 12) - rootPc + 12) % 12;
  return SCALE_INTERVALS[scale].includes(rel);
}

/**
 * Returns whether a physical key index is a root note of the selected scale.
 */
export function isKeyRoot(keyIndex: number, scale: ScaleMode, rootPc: number): boolean {
  if (scale === 'chromatic') return false;
  const midi = KEY_MIDI_BASE + keyIndex;
  return (midi % 12) === rootPc;
}

/**
 * Quantizes any MIDI note to the closest note in the active scale (Tessera / LOOM-8 Keybed Lock).
 */
export function quantizeMidiToScale(midi: number, scale: ScaleMode, rootPc: number): number {
  if (scale === 'chromatic') return midi;
  const intervals = SCALE_INTERVALS[scale];
  const rel = ((midi % 12) - rootPc + 12) % 12;
  if (intervals.includes(rel)) return midi;

  // Find closest scale interval within +/- 2 semitones
  let bestDiff = 99;
  for (let d = -2; d <= 2; d++) {
    const candidateRel = (rel + d + 12) % 12;
    if (intervals.includes(candidateRel)) {
      if (Math.abs(d) < Math.abs(bestDiff)) {
        bestDiff = d;
      }
    }
  }
  return midi + (bestDiff === 99 ? 0 : bestDiff);
}

/**
 * Expands a single MIDI note into chord intervals based on 1-Finger Chord Memory.
 */
export function expandChordNotes(baseMidi: number, chordMode: ChordMemoryMode): number[] {
  return CHORD_OFFSETS[chordMode].map(offset => Math.max(24, Math.min(108, baseMidi + offset)));
}

/**
 * Provides 4 Tessera-style Modulation Cords with sensible Virus TI2 defaults if a patch omits modMatrix.
 */
export function getEffectiveModMatrix(patch: SynthPatch): ModCord[] {
  if (patch.modMatrix && patch.modMatrix.length === 4) {
    return patch.modMatrix;
  }
  return [
    {
      id: 1,
      source: 'lfo1',
      dest: patch.lfoDest === 'pitch' ? 'pitch' : patch.lfoDest === 'pan' ? 'pan' : patch.lfoDest === 'detune' ? 'spread' : 'cutoff',
      amount: patch.lfoDepth ?? 18,
    },
    {
      id: 2,
      source: 'swarm',
      dest: 'spread',
      amount: patch.swarmDepth ?? 24,
    },
    {
      id: 3,
      source: 'filterEnv',
      dest: 'cutoff',
      amount: patch.filterEnvAmount ?? 45,
    },
    {
      id: 4,
      source: 'velocity',
      dest: 'cutoff',
      amount: 28,
    },
  ];
}

/**
 * Generates an evenly distributed Euclidean rhythm pattern E(pulses, steps) with optional rotation.
 */
export function generateEuclideanPattern(steps: number, pulses: number, rotation = 0): boolean[] {
  const n = Math.max(1, Math.min(64, Math.round(steps)));
  const k = Math.max(0, Math.min(n, Math.round(pulses)));
  if (k === 0) return Array(n).fill(false);
  if (k === n) return Array(n).fill(true);

  const pattern: boolean[] = [];
  let bucket = 0;
  for (let i = 0; i < n; i++) {
    bucket += k;
    if (bucket >= n) {
      bucket -= n;
      pattern.push(true);
    } else {
      pattern.push(false);
    }
  }
  const rot = ((rotation % n) + n) % n;
  if (rot === 0) return pattern;
  return pattern.map((_, idx) => pattern[(idx - rot + n) % n]);
}

