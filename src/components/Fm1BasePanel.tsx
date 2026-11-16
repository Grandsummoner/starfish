import React, { useEffect, useRef, useState, useMemo, useCallback } from 'react';
import { Download, Cable, Volume2, Power, Archive } from 'lucide-react';
import { audioEngine } from '../audio/AudioEngine';
import { midiManager, MidiDevice } from '../audio/midi';
import {
  SynthPatch,
  DrumTrack,
  DrumStep,
  PANEL_BTN,
  PANEL_WHITE_K,
  PANEL_BLACK_K,
  KEY_MIDI_BASE,
  PanelPage,
  VirusBankId,
  ModSource,
  ModDest,
  DrumKitId,
  DrumBusMode,
  LfoDest,
  ChordMemoryMode,
  ScaleMode
} from '../types/firmware';
import type { DrumKitPreset } from '../App';
import {
  VIRUS_BANKS,
  RANDOM_PATCH_STYLES,
  RandomPatchStyle,
  getBankPresets,
  getBankXCount,
  getNextFreeBankXSlot,
  savePatchToBankX,
  saveUserPreset,
  revertPresetToFactory,
  createInitSlotPatch,
  generateSafeRandomPatch,
  getPresetBankCode,
  recordRecentPreset,
  getRecentBootPresets
} from '../data/virusPresets';
import { compileFwscCode } from '../utils/fwscCompiler';
import {
  CHORD_LABELS,
  expandChordNotes,
  getEffectiveModMatrix,
  isKeyRoot,
  quantizeMidiToScale,
  SCALE_LABELS,
  SCALE_MODE_ORDER
} from '../utils/musicScale';

const NOTE_NAMES_12 = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const SCALE_MODES: ScaleMode[] = SCALE_MODE_ORDER;
const CHORD_MODES: ChordMemoryMode[] = ['off', 'octave', 'fifth', 'minor7', 'major7'];
const SAT_TYPES: Array<NonNullable<SynthPatch['saturationType']>> = ['soft', 'tube', 'hard', 'off'];
const SUB_WAVES: Array<NonNullable<SynthPatch['subWaveform']>> = ['square', 'sine', 'saw'];
const FILTER_ROUTINGS: Array<NonNullable<SynthPatch['filterRouting']>> = ['serial', 'parallel', 'split'];
const FILTER_TYPES: Array<SynthPatch['filterType']> = ['lowpass', 'bandpass', 'highpass', 'notch'];
const FILTER_TYPE_LABELS: Record<SynthPatch['filterType'], string> = {
  lowpass: 'LP',
  bandpass: 'BP',
  highpass: 'HP',
  notch: 'NOTCH'
};
const LFO_DESTS: LfoDest[] = [
  'filter',
  'filter2',
  'reso',
  'pitch',
  'pan',
  'detune',
  'amp',
  'chorus',
  'phaser',
  'delay',
  'reverb'
];
const LFO_DEST_LABELS: Record<LfoDest, string> = {
  filter: 'F1 CUT',
  filter2: 'F2 CUT',
  reso: 'F1 RESO',
  pitch: 'PITCH',
  pan: 'PAN',
  detune: 'SPREAD',
  amp: 'AMP TRM',
  chorus: 'FX CHOR',
  phaser: 'FX PHAS',
  delay: 'FX DLY',
  reverb: 'FX VERB'
};
const FILTER_COMBOS: Array<{
  short: string;
  label: string;
  sub: string;
  filterType: 'lowpass' | 'bandpass' | 'highpass' | 'notch';
  filterRouting: 'serial' | 'parallel' | 'split';
  filter2Type: 'lowpass' | 'bandpass' | 'highpass' | 'notch';
}> = [
  { short: 'LP SER', label: 'LP SERIAL', sub: 'F1 LP -> F2 LP', filterType: 'lowpass', filterRouting: 'serial', filter2Type: 'lowpass' },
  { short: 'LP+BP', label: 'LP+BP PAR', sub: 'DUAL PARALLEL', filterType: 'lowpass', filterRouting: 'parallel', filter2Type: 'bandpass' },
  { short: 'LP/HP', label: 'LP / HP SPL', sub: 'OSC1->F1 | OSC2->F2', filterType: 'lowpass', filterRouting: 'split', filter2Type: 'highpass' },
  { short: 'BP SER', label: 'BP SERIAL', sub: 'BANDPASS SERIES', filterType: 'bandpass', filterRouting: 'serial', filter2Type: 'lowpass' },
  { short: 'HP SER', label: 'HP SERIAL', sub: 'HIGHPASS SERIES', filterType: 'highpass', filterRouting: 'serial', filter2Type: 'highpass' },
  { short: 'NOTCH', label: 'BS NOTCH', sub: 'BANDSTOP NOTCH', filterType: 'notch', filterRouting: 'serial', filter2Type: 'lowpass' }
];
const STARFISH_COLORS = [
  { body: '#FF6B8B', belly: '#FF9BB2', cheek: '#FF3E6C', spot: '#FFD6E0' },
  { body: '#22D3EE', belly: '#67E8F9', cheek: '#06B6D4', spot: '#CFFAFE' },
  { body: '#FBBF24', belly: '#FDE68A', cheek: '#F59E0B', spot: '#FEF3C7' },
  { body: '#A78BFA', belly: '#C4B5FD', cheek: '#8B5CF6', spot: '#EDE9FE' },
  { body: '#34D399', belly: '#6EE7B7', cheek: '#10B981', spot: '#D1FAE5' },
  { body: '#FB923C', belly: '#FDBA74', cheek: '#EA580C', spot: '#FFEDD5' }
];

function formatMidiName(midi: number): string {
  const n = NOTE_NAMES_12[((midi % 12) + 12) % 12];
  const oct = Math.floor(midi / 12) - 1;
  return `${n}${oct}`;
}

interface Fm1BasePanelProps {
  patch: SynthPatch;
  onPatchChange: React.Dispatch<React.SetStateAction<SynthPatch>>;
  firmwareName: string;
  isPlayingSeq: boolean;
  onTogglePlaySeq: () => void;
  bpm: number;
  onBpmChange: (bpm: number) => void;
  swing: number;
  onSwingChange: (swing: number) => void;
  drumTracks: DrumTrack[];
  drumVolume: number;
  onDrumVolumeChange: (vol: number) => void;
  drumKitIndex: number;
  onDrumKitChange: (kitIdx: number) => void;
  drumBusMode: number;
  onDrumBusModeChange: (modeIdx: number) => void;
  drumKitPreset: DrumKitPreset;
  onSelectDrumKitPreset: (preset: DrumKitPreset) => void;
  onToggleDrumMute: (trackIdx: number) => void;
  seqLength: number;
  onSeqLengthChange: (len: number) => void;
  onDrumEuclidChange: (trackIdx: number, pulses: number, rotate: number) => void;
  playheadStep: number;
  onDrumStepUpdate: (trackIdx: number, stepIdx: number, update: Partial<DrumStep>) => void;
  onDrumTrackParamChange: (trackIdx: number, update: Partial<Pick<DrumTrack, 'tune' | 'decay' | 'tone' | 'level'>>) => void;
  synthLoopCount: number;
  onClearSynthLoop: () => void;
  onClearDrumTrack: (trackIdx: number) => void;
}

type HudPayload =
  | { kind: 'bank'; bankId: VirusBankId; subtitle: string }
  | { kind: 'preset'; code: string; name: string; meta: string; prevName: string; nextName: string }
  | { kind: 'drumkit'; index: number; name: string; subtitle: string; prevName: string; nextName: string }
  | { kind: 'param'; label: string; value: string; ratio: number; accent: string; knobIdx?: number }
  | { kind: 'badge'; title: string; subtitle: string; accent: string };

type HudOverlay = HudPayload & { expiresAt: number };

type PendingLeaveAction =
  | { kind: 'preset'; targetPatch: SynthPatch }
  | { kind: 'bank'; targetBankId: VirusBankId; fallbackProg: number };

type ConfirmPromptState =
  | { type: 'save'; dest: 'bankX' | 'slot'; bankXSlot: number }
  | { type: 'reset'; mode: 'factory' | 'init' }
  | { type: 'clearLoop' }
  | { type: 'randomResolve'; pending: PendingLeaveAction; bankXSlot: number }
  | null;

const BOOT_SPLASH_MS = 3800;
const BOOT_ACTS = [
  { id: 'beach', title: "BEACH CHILLIN'", sub: 'SUNGLASSES & REEF JUICE' },
  { id: 'moon', title: 'MOON EXPLORER', sub: 'PLANTING FM-1 FLAG!' },
  { id: 'sub', title: 'SUB CAPTAIN', sub: 'DEEP TRENCH DIVE' },
  { id: 'camp', title: 'CAMPFIRE JAM', sub: 'STARRY NIGHT GUITAR' },
  { id: 'skate', title: 'ROCKET SKATER', sub: '360° KICKFLIP ORBIT' },
  { id: 'chef', title: 'STARFISH CHEF', sub: 'TOSSING FRESH NOTES' }
] as const;

const BOOT_JINGLES: Array<{ name: string; notes: number[]; stepMs: number }> = [
  { name: 'CORAL MAJ9', notes: [60, 64, 67, 71, 74], stepMs: 130 },
  { name: 'LUNAR FIFTHS', notes: [62, 67, 69, 74, 79], stepMs: 125 },
  { name: 'LAGOON WAVE', notes: [64, 67, 69, 71, 76], stepMs: 120 },
  { name: 'STAR HOP', notes: [60, 67, 65, 64, 72], stepMs: 125 },
  { name: 'REEF DREAM', notes: [57, 64, 69, 71, 76], stepMs: 135 },
  { name: 'TURBO BOOT', notes: [60, 62, 64, 67, 72], stepMs: 110 }
];

const OSC_MODES: Array<NonNullable<SynthPatch['oscMode']>> = [
  'hypersaw',
  'saw',
  'pulse',
  'wavetable',
  'sync',
  'triangle'
];

const OSC_MODE_LABELS: Record<string, string> = {
  hypersaw: 'HYPERSAW',
  saw: 'CLASSIC SAW',
  pulse: 'PULSE / PWM',
  wavetable: 'WAVETABLE',
  sync: 'HARD SYNC',
  triangle: 'TRIANGLE'
};

const DRUM_KITS: Array<{ id: DrumKitId; subtitle: string }> = [
  { id: 'TR-808 CLASSIC', subtitle: 'DEEP SUB TR-808' },
  { id: 'TR-909 CLUB', subtitle: 'PUNCHY HOUSE / TECHNO' },
  { id: 'CR-78 VINTAGE', subtitle: 'BRUSH SNARE & BONGO' },
  { id: 'LINNDRUM PCM', subtitle: '80S GATED STUDIO PCM' },
  { id: 'VIRUS ELECTRO', subtitle: 'CRISP MODULAR PERC' },
  { id: 'HOUSE CHICAGO', subtitle: 'WARM 707 / 909 MIX' },
  { id: 'ACOUSTIC JAZZ', subtitle: 'WOOD KIT & BRONZE RIDE' },
  { id: 'FM PERC DX', subtitle: 'GLASSY 2-OP FM PERC' },
  { id: 'LO-FI DUST', subtitle: '12-BIT WARM SAMPLER' },
  { id: 'TRAP / UK BASS', subtitle: 'LONG 808 + SHARP HAT' }
];

const DRUM_BUS_MODES: Array<{ id: DrumBusMode; subtitle: string }> = [
  { id: 'CLEAN PCM', subtitle: 'FLAT STUDIO REFERENCE' },
  { id: 'WARM CONSOLE', subtitle: 'GENTLE ANALOG EQ TILT' },
  { id: 'PUNCH COMP', subtitle: 'TIGHT TRANSIENT GLUE' },
  { id: 'SUB BASS +', subtitle: 'DEEP CLEAN LOW-END' },
  { id: 'CLUB ROOM', subtitle: 'WIDE STEREO AMBIENCE' },
  { id: 'FM SPARKLE', subtitle: 'SILKY TOP-END AIR' }
];

const DRUM_PRESET_ORDER: DrumKitPreset[] = ['TECHNO', 'HOUSE', 'ELECTRO', 'BREAK'];

const PAGE_TOTALS: Record<PanelPage, number> = {
  HOME: 1,
  FX: 2,
  SEL: 3,
  ENV: 2,
  LFO: 3,
  EDIT: 3,
  GLO: 3,
  ARP: 2,
  SEQ: 3
};

export const Fm1BasePanel: React.FC<Fm1BasePanelProps> = ({
  patch,
  onPatchChange,
  firmwareName,
  isPlayingSeq,
  onTogglePlaySeq,
  bpm,
  onBpmChange,
  swing,
  onSwingChange,
  drumTracks,
  drumVolume,
  onDrumVolumeChange,
  drumKitIndex,
  onDrumKitChange,
  drumBusMode,
  onDrumBusModeChange,
  drumKitPreset,
  onSelectDrumKitPreset,
  onToggleDrumMute,
  seqLength,
  onSeqLengthChange,
  onDrumEuclidChange,
  playheadStep,
  onDrumStepUpdate,
  onDrumTrackParamChange,
  synthLoopCount,
  onClearSynthLoop,
  onClearDrumTrack
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  // Hardware Power Switch (Top Navigation Bar small ON/OFF control)
  const [isPoweredOn, setIsPoweredOn] = useState<boolean>(true);
  const isPoweredOnRef = useRef<boolean>(true);

  // 15% Default Master Volume (synced bidirectionally with hardware MASTER knob)
  const [volumePct, setVolumePct] = useState<number>(15);

  // Web MIDI Hardware Connection State
  const [midiConnected, setMidiConnected] = useState<boolean>(false);
  const [midiInputs, setMidiInputs] = useState<MidiDevice[]>([]);
  const [midiOutputs, setMidiOutputs] = useState<MidiDevice[]>([]);
  const [selectedInputId, setSelectedInputId] = useState<string>('');
  const [selectedOutputId, setSelectedOutputId] = useState<string>('');
  const [midiNotice, setMidiNotice] = useState<string | null>(null);

  // Hardware Unit State (HOME = Performance Page; all multi-page menus use uniform subPage 0..N-1)
  const [activePage, setActivePage] = useState<PanelPage>('HOME');
  const [subPage, setSubPage] = useState<number>(0);
  const [activeBank, setActiveBank] = useState<VirusBankId>(patch.bank || 'C');
  const [bankRevision, setBankRevision] = useState<number>(0);
  const [selectedDrumTrack, setSelectedDrumTrack] = useState<number>(0);
  const [selectedStepIdx, setSelectedStepIdx] = useState<number>(0);
  const [muteKeyHeld, setMuteKeyHeld] = useState<boolean>(false);
  const [isRecording, setIsRecording] = useState<boolean>(false);
  const [confirmPrompt, setConfirmPrompt] = useState<ConfirmPromptState>(null);
  const [saveHoldStart, setSaveHoldStart] = useState<number | null>(null);
  const [recHoldStart, setRecHoldStart] = useState<number | null>(null);
  const [drumClearHold, setDrumClearHold] = useState<{ trackIdx: number; label: string; start: number } | null>(null);
  const [saveFlashUntil, setSaveFlashUntil] = useState<number>(0);
  const [saveBannerTitle, setSaveBannerTitle] = useState<string>('PATCH SAVED');
  const [activeKeyMask, setActiveKeyMask] = useState<number>(0);
  const [activeBtnMask, setActiveBtnMask] = useState<number>(0);
  const [octaveOffset, setOctaveOffset] = useState<number>(0);

  // Global Musical Scale, Root, Chord Memory & Randomizer (GLO pages 1/3 - 3/3)
  const [rootPc, setRootPc] = useState<number>(0); // 0 = C
  const [scaleMode, setScaleMode] = useState<ScaleMode>('chromatic');
  const [chordMode, setChordMode] = useState<ChordMemoryMode>('off');
  const [randStyle, setRandStyle] = useState<RandomPatchStyle>('ALL');
  const [randChaos, setRandChaos] = useState<number>(60);
  const [randCount, setRandCount] = useState<number>(0);
  const [targetBankXSlot, setTargetBankXSlot] = useState<number>(() => getNextFreeBankXSlot());

  // OLED-Internal Boot Splash State (1 of 6 acts + 1 of 6 jingles using last 6 presets)
  const bootLastActRef = useRef<number>(-1);
  const bootLastJingleRef = useRef<number>(-1);
  const bootSplashRef = useRef<{
    startedAt: number;
    until: number;
    actIdx: number;
    jingleIdx: number;
    jinglePreset: SynthPatch;
    audioPlayed: boolean;
  } | null>(null);

  // Active envelope segment highlight during live ADSR tweaking
  const lastEnvParamRef = useRef<{ segment: 'A' | 'D' | 'S' | 'R'; expiresAt: number } | null>(null);

  // Top-right OLED Starfish Animation State (zero re-render 60fps reactive state)
  const starAnimRef = useRef({
    paletteIdx: 0,
    pulseCount: 0,
    bounceEnergy: 0,
    stepDir: 1,
    swivelAngle: 0,
    targetSwivel: 0,
    spinCurrent: 0,
    spinTarget: 0,
    danceX: 0,
    targetDanceX: 0,
    happyUntil: 0,
    nextBlinkAt: 2800,
    drumStickSide: 1 as 1 | -1,
    drumHitUntil: 0,
    lastDrumIdx: 0
  });

  // 12-second menu inactivity auto-return timer (paused in SEQ, dragging knob, recording, or confirming)
  const lastMenuActivityRef = useRef<number>(performance.now());
  const touchMenuActivity = useCallback(() => {
    lastMenuActivityRef.current = performance.now();
  }, []);

  // SAVE long-hold (1500ms Reset), REC long-hold (1500ms clear loop), ARP long-press page flip + ARP+OCT combo
  const saveHoldTimerRef = useRef<number | null>(null);
  const saveDidLongPressRef = useRef<boolean>(false);
  const savePressedRef = useRef<boolean>(false);
  const recHoldTimerRef = useRef<number | null>(null);
  const recDidLongPressRef = useRef<boolean>(false);
  const recPressedRef = useRef<boolean>(false);
  const arpPressedRef = useRef<boolean>(false);
  const arpComboUsedRef = useRef<boolean>(false);
  const arpHoldTimerRef = useRef<number | null>(null);
  const arpDidLongPressRef = useRef<boolean>(false);
  const drumClearTimerRef = useRef<number | null>(null);
  const drumClearTrackRef = useRef<number | null>(null);
  const drumClearDidFireRef = useRef<boolean>(false);

  const triggerBootSequence = useCallback((fromGesture: boolean) => {
    const pickNonRepeating = (count: number, last: number) => {
      const candidates = Array.from({ length: count }, (_, i) => i).filter((i) => i !== last);
      return candidates[Math.floor(Math.random() * candidates.length)] ?? 0;
    };
    const actIdx = pickNonRepeating(BOOT_ACTS.length, bootLastActRef.current);
    const jingleIdx = pickNonRepeating(BOOT_JINGLES.length, bootLastJingleRef.current);
    bootLastActRef.current = actIdx;
    bootLastJingleRef.current = jingleIdx;

    const pool = getRecentBootPresets();
    const jinglePreset = pool[Math.floor(Math.random() * pool.length)] || patch;
    const jingle = BOOT_JINGLES[jingleIdx];
    const now = performance.now();
    const played = audioEngine.playBootJingle(jingle.notes, jingle.stepMs, jinglePreset, fromGesture);

    bootSplashRef.current = {
      startedAt: now,
      until: now + BOOT_SPLASH_MS,
      actIdx,
      jingleIdx,
      jinglePreset,
      audioPlayed: played
    };
  }, [patch]);

  // Run OLED Boot Splash once on initial power-up; unlock boot jingle on first non-keyboard interaction if autoplay was blocked
  useEffect(() => {
    triggerBootSequence(false);

    const onFirstUnlock = () => {
      const bs = bootSplashRef.current;
      if (bs && !bs.audioPlayed && performance.now() < bs.until + 1500) {
        const jingle = BOOT_JINGLES[bs.jingleIdx];
        bs.audioPlayed = audioEngine.playBootJingle(jingle.notes, jingle.stepMs, bs.jinglePreset, true);
      }
      window.removeEventListener('pointerdown', onFirstUnlock);
    };

    window.addEventListener('pointerdown', onFirstUnlock, { passive: true });
    return () => {
      window.removeEventListener('pointerdown', onFirstUnlock);
    };
    // Run only once on initial hardware boot
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  // Transient crisp OLED overlay for knob turns, bank changes, and button toggles
  const hudRef = useRef<HudOverlay | null>(null);
  const showHud = useCallback((hud: HudPayload, durationMs = 950) => {
    hudRef.current = { ...hud, expiresAt: performance.now() + durationMs };
  }, []);

  // Held key tracking across pointer, computer keyboard, and physical MIDI input
  const pointerHeldNotesRef = useRef<Map<number, number[]>>(new Map());
  const keyboardHeldNotesRef = useRef<Map<number, number[]>>(new Map());
  const midiHeldKeysRef = useRef<Map<number, { keyIndex: number; notes: number[] }>>(new Map());
  const octaveOffsetRef = useRef<number>(0);
  const rootPcRef = useRef<number>(0);
  const scaleModeRef = useRef<ScaleMode>('chromatic');
  const chordModeRef = useRef<ChordMemoryMode>('off');
  const isRecordingRef = useRef<boolean>(false);
  const activePageRef = useRef<PanelPage>('HOME');
  const isPlayingSeqRef = useRef<boolean>(false);
  const playheadStepRef = useRef<number>(0);
  const seqLengthRef = useRef<number>(seqLength);
  const selectedStepRef = useRef<number>(0);
  const selectedDrumTrackRef = useRef<number>(0);
  const drumTracksRef = useRef<DrumTrack[]>(drumTracks);
  const muteKeyHeldRef = useRef<boolean>(false);
  const muteLatchedRef = useRef<boolean>(false);
  const muteUsedWhileHeldRef = useRef<boolean>(false);
  const onTogglePlaySeqRef = useRef(onTogglePlaySeq);

  useEffect(() => {
    isPoweredOnRef.current = isPoweredOn;
  }, [isPoweredOn]);

  useEffect(() => {
    octaveOffsetRef.current = octaveOffset;
  }, [octaveOffset]);

  useEffect(() => {
    rootPcRef.current = rootPc;
  }, [rootPc]);

  useEffect(() => {
    scaleModeRef.current = scaleMode;
  }, [scaleMode]);

  useEffect(() => {
    chordModeRef.current = chordMode;
  }, [chordMode]);

  useEffect(() => {
    isRecordingRef.current = isRecording;
  }, [isRecording]);

  useEffect(() => {
    activePageRef.current = activePage;
    if (activePage !== 'SEQ') {
      muteKeyHeldRef.current = false;
      muteLatchedRef.current = false;
      muteUsedWhileHeldRef.current = false;
      setMuteKeyHeld(false);
      if (drumClearTimerRef.current !== null) {
        window.clearTimeout(drumClearTimerRef.current);
        drumClearTimerRef.current = null;
      }
      drumClearTrackRef.current = null;
      drumClearDidFireRef.current = false;
      setDrumClearHold(null);
    }
  }, [activePage]);

  useEffect(() => {
    audioEngine.setSynthRecording(isRecording && activePage !== 'SEQ');
  }, [isRecording, activePage]);

  useEffect(() => {
    isPlayingSeqRef.current = isPlayingSeq;
  }, [isPlayingSeq]);

  useEffect(() => {
    onTogglePlaySeqRef.current = onTogglePlaySeq;
  }, [onTogglePlaySeq]);

  useEffect(() => {
    playheadStepRef.current = playheadStep;
  }, [playheadStep]);

  useEffect(() => {
    seqLengthRef.current = seqLength;
    if (selectedStepIdx >= seqLength) {
      setSelectedStepIdx(Math.max(0, seqLength - 1));
    }
  }, [seqLength, selectedStepIdx]);

  useEffect(() => {
    selectedStepRef.current = selectedStepIdx;
  }, [selectedStepIdx]);

  useEffect(() => {
    selectedDrumTrackRef.current = selectedDrumTrack;
  }, [selectedDrumTrack]);

  useEffect(() => {
    drumTracksRef.current = drumTracks;
  }, [drumTracks]);

  // Apply 15% initial master volume & subscribe OLED Starfish to all audio triggers
  useEffect(() => {
    audioEngine.setMasterVolume(volumePct / 100);
  }, [volumePct]);

  useEffect(() => {
    audioEngine.setNoteTriggerCallback((note, vel) => {
      const st = starAnimRef.current;
      st.pulseCount += 1;
      st.paletteIdx = (note + st.pulseCount) % STARFISH_COLORS.length;
      st.stepDir *= -1;
      st.bounceEnergy = 1.0;
      const spread = ((note % 7) - 3) * 2.5;
      st.targetSwivel = st.stepDir * 22 + spread;
      st.targetDanceX = st.stepDir * 3.8;
      st.happyUntil = performance.now() + 210;
      if (vel >= 115 || st.pulseCount % 4 === 0) {
        st.spinTarget += 360 * st.stepDir;
      }
    });
    audioEngine.setDrumTriggerCallback((drumIndex, vel) => {
      const st = starAnimRef.current;
      st.pulseCount += 1;
      st.paletteIdx = (drumIndex + st.pulseCount) % STARFISH_COLORS.length;
      st.drumStickSide = st.drumStickSide === 1 ? -1 : 1;
      st.lastDrumIdx = drumIndex;
      st.drumHitUntil = performance.now() + 180;
      st.bounceEnergy = Math.max(st.bounceEnergy, vel >= 110 ? 1.0 : 0.72);
      st.stepDir *= -1;
      st.targetSwivel = st.stepDir * 16;
      st.targetDanceX = st.stepDir * 2.5;
      st.happyUntil = performance.now() + 190;
      if (vel >= 118 || drumIndex === 4 || st.pulseCount % 8 === 0) {
        st.spinTarget += 360;
      }
    });
    return () => {
      audioEngine.setNoteTriggerCallback(null);
      audioEngine.setDrumTriggerCallback(null);
    };
  }, []);

  const bankPresets = useMemo(() => {
    void bankRevision;
    return getBankPresets(activeBank);
  }, [activeBank, bankRevision]);

  const currentProgramIndex = useMemo(() => {
    if (patch.programNumber !== undefined && bankPresets[patch.programNumber]?.id === patch.id) {
      return patch.programNumber;
    }
    const idx = bankPresets.findIndex((p) => p.id === patch.id);
    return idx >= 0 ? idx : 0;
  }, [bankPresets, patch.id, patch.programNumber]);

  // Recompute unified keybed illumination bitmask
  const syncActiveKeyMask = useCallback(() => {
    let mask = 0;
    pointerHeldNotesRef.current.forEach((_, idx) => {
      mask |= 1 << idx;
    });
    keyboardHeldNotesRef.current.forEach((_, idx) => {
      mask |= 1 << idx;
    });
    midiHeldKeysRef.current.forEach(({ keyIndex }) => {
      if (keyIndex >= 0 && keyIndex < 27) mask |= 1 << keyIndex;
    });
    setActiveKeyMask(mask);
  }, []);

  // Drum Machine Mode Keybed Handler:
  // - Upper Black Capsule Keys 1..8: Select & audition Drum Voice 1..8 (or toggle Mute if MUTE key held)
  // - Extreme Top-Right Black Capsule Key (keyIndex 25): MUTE modifier
  // - Bottom White Capsule Keys 1..16: Toggle Steps 1..16 on active bar for selected drum voice
  const handleSeqKeyDown = useCallback(
    (keyIndex: number, velocity = 112): boolean => {
      if (activePageRef.current !== 'SEQ') return false;
      touchMenuActivity();

      // Extreme top-right black key (PANEL_BLACK_K[10] === 25) = Hybrid Hold/Latch MUTE modifier
      if (keyIndex === PANEL_BLACK_K[10]) {
        if (muteLatchedRef.current) {
          muteLatchedRef.current = false;
          muteKeyHeldRef.current = false;
          muteUsedWhileHeldRef.current = false;
          setMuteKeyHeld(false);
          showHud({ kind: 'badge', title: 'MUTE OFF', subtitle: 'DRUM VOICE MODE', accent: '#94A3B8' }, 650);
          return true;
        }
        muteKeyHeldRef.current = true;
        muteUsedWhileHeldRef.current = false;
        setMuteKeyHeld(true);
        showHud({ kind: 'badge', title: 'MUTE ARMED', subtitle: 'TAP BLK 1-8 TO MUTE', accent: '#EF4444' }, 1100);
        return true;
      }

      // Upper black keys 1..8 = Drum Voices 0..7
      const blackIdx = PANEL_BLACK_K.indexOf(keyIndex);
      if (blackIdx >= 0) {
        if (blackIdx < 8) {
          const tr = drumTracksRef.current[blackIdx];
          if (muteKeyHeldRef.current || muteLatchedRef.current) {
            muteUsedWhileHeldRef.current = true;
            onToggleDrumMute(blackIdx);
            const willMute = !tr?.mute;
            const voiceShort = tr?.shortName || `D${blackIdx + 1}`;
            showHud(
              {
                kind: 'badge',
                title: `${voiceShort} ${willMute ? 'MUTED' : 'UNMUTED'}`,
                subtitle: 'HOLD 1.5S TO CLEAR TRACK',
                accent: willMute ? '#EF4444' : '#34D399'
              },
              900
            );
            // Start long-hold (1500ms) timer to clear this entire drum track cleanly
            if (drumClearTimerRef.current !== null) {
              window.clearTimeout(drumClearTimerRef.current);
            }
            drumClearTrackRef.current = blackIdx;
            drumClearDidFireRef.current = false;
            setDrumClearHold({ trackIdx: blackIdx, label: voiceShort, start: performance.now() });
            drumClearTimerRef.current = window.setTimeout(() => {
              drumClearTimerRef.current = null;
              drumClearDidFireRef.current = true;
              drumClearTrackRef.current = null;
              setDrumClearHold(null);
              onClearDrumTrack(blackIdx);
              showHud(
                {
                  kind: 'badge',
                  title: `${voiceShort} CLEARED`,
                  subtitle: 'TRACK STEPS & EUCLID WIPED',
                  accent: '#FBBF24'
                },
                1150
              );
            }, 1500);
          } else {
            selectedDrumTrackRef.current = blackIdx;
            setSelectedDrumTrack(blackIdx);
            audioEngine.triggerDrum(blackIdx, velocity);
            if (isRecordingRef.current && isPlayingSeqRef.current) {
              const stepIdx = playheadStepRef.current % Math.max(1, seqLengthRef.current);
              onDrumStepUpdate(blackIdx, stepIdx, {
                active: true,
                accent: velocity > 115,
                velocity
              });
            }
          }
        }
        return true;
      }

      // Bottom white keys 1..16 = Steps 1..16 of active bar
      const whiteIdx = PANEL_WHITE_K.indexOf(keyIndex);
      if (whiteIdx >= 0) {
        const activeBar =
          isPlayingSeqRef.current && !isRecordingRef.current
            ? Math.floor(playheadStepRef.current / 16)
            : Math.floor(selectedStepRef.current / 16);
        const absIdx = activeBar * 16 + whiteIdx;
        if (absIdx >= seqLengthRef.current) {
          onSeqLengthChange(absIdx + 1);
        }
        const curTrackIdx = selectedDrumTrackRef.current;
        const curTrack = drumTracksRef.current[curTrackIdx];
        const curStep = curTrack?.steps[absIdx];
        const nextActive = !curStep?.active;
        onDrumStepUpdate(curTrackIdx, absIdx, {
          active: nextActive,
          accent: false,
          velocity: 108
        });
        selectedStepRef.current = absIdx;
        setSelectedStepIdx(absIdx);
        if (nextActive && !isPlayingSeqRef.current) {
          audioEngine.triggerDrum(curTrackIdx, 102);
        }
        return true;
      }

      return true;
    },
    [onClearDrumTrack, onDrumStepUpdate, onSeqLengthChange, onToggleDrumMute, showHud, touchMenuActivity]
  );

  const handleSeqKeyUp = useCallback(
    (keyIndex: number) => {
      const blackIdx = PANEL_BLACK_K.indexOf(keyIndex);
      if (blackIdx >= 0 && blackIdx < 8 && drumClearTrackRef.current === blackIdx) {
        if (drumClearTimerRef.current !== null) {
          window.clearTimeout(drumClearTimerRef.current);
          drumClearTimerRef.current = null;
        }
        drumClearTrackRef.current = null;
        setDrumClearHold(null);
      }

      if (keyIndex === PANEL_BLACK_K[10]) {
        if (drumClearTimerRef.current !== null) {
          window.clearTimeout(drumClearTimerRef.current);
          drumClearTimerRef.current = null;
        }
        drumClearTrackRef.current = null;
        setDrumClearHold(null);

        if (muteUsedWhileHeldRef.current) {
          muteKeyHeldRef.current = false;
          muteLatchedRef.current = false;
          muteUsedWhileHeldRef.current = false;
          setMuteKeyHeld(false);
        } else if (muteKeyHeldRef.current) {
          muteLatchedRef.current = true;
          setMuteKeyHeld(true);
          showHud({ kind: 'badge', title: 'MUTE LATCHED', subtitle: 'TAP BLK1-8 · HOLD 1.5S CLR', accent: '#EF4444' }, 1350);
        }
      }
    },
    [showHud]
  );

  // Live Octave Transpose (updates Keybed, Held Voices, Running/Latched ARP, and Sequencer!)
  const applyOctaveChange = useCallback(
    (nextOct: number, announce = true) => {
      const clamped = Math.max(-2, Math.min(2, nextOct));
      const prevOct = octaveOffsetRef.current;
      const deltaSemi = (clamped - prevOct) * 12;
      setOctaveOffset(clamped);
      octaveOffsetRef.current = clamped;
      audioEngine.setSeqTranspose(clamped * 12);

      if (deltaSemi !== 0) {
        audioEngine.shiftArpOctave(deltaSemi);
        const shiftHeldMap = (map: Map<number, number[]>) => {
          map.forEach((notes, keyIdx) => {
            const shifted = notes.map((n) => Math.max(24, Math.min(108, n + deltaSemi)));
            if (!patch.arpEnabled) {
              notes.forEach((oldN) => {
                audioEngine.noteOff(oldN);
                midiManager.sendNoteOff(oldN);
              });
              shifted.forEach((newN) => {
                audioEngine.noteOn(newN, 110);
                midiManager.sendNoteOn(newN, 110);
              });
            }
            map.set(keyIdx, shifted);
          });
        };
        shiftHeldMap(pointerHeldNotesRef.current);
        shiftHeldMap(keyboardHeldNotesRef.current);
      }

      if (announce) {
        showHud(
          {
            kind: 'badge',
            title: `OCT ${clamped >= 0 ? `+${clamped}` : clamped}`,
            subtitle: patch.arpEnabled ? 'ARP & KEYBED OCT' : 'KEYBED TRANSPOSE',
            accent: '#FBBF24'
          },
          800
        );
      }
    },
    [patch.arpEnabled, showHud]
  );

  // Refresh available Web MIDI ports
  const refreshMidiPorts = useCallback(() => {
    const ins = midiManager.getInputs();
    const outs = midiManager.getOutputs();
    setMidiInputs(ins);
    setMidiOutputs(outs);
    setSelectedInputId(midiManager.getSelectedInput()?.id || '');
    setSelectedOutputId(midiManager.getSelectedOutput()?.id || '');
  }, []);

  // Register Web MIDI callbacks once so incoming hardware notes trigger sound & light keys
  useEffect(() => {
    midiManager.setCallbacks(
      (note, vel) => {
        if (!isPoweredOnRef.current) return;
        audioEngine.initAudio();
        if (activePageRef.current !== 'SEQ' && isRecordingRef.current && !isPlayingSeqRef.current) {
          onTogglePlaySeqRef.current();
        }
        const qNote = quantizeMidiToScale(note, scaleModeRef.current, rootPcRef.current);
        const notes = expandChordNotes(qNote, chordModeRef.current);
        notes.forEach((n) => audioEngine.noteOn(n, vel));
        const keyIndex = note - (KEY_MIDI_BASE + octaveOffsetRef.current * 12);
        midiHeldKeysRef.current.set(note, { keyIndex, notes });
        if (keyIndex >= 0 && keyIndex < 27) syncActiveKeyMask();
      },
      (note) => {
        const entry = midiHeldKeysRef.current.get(note);
        if (entry) {
          entry.notes.forEach((n) => audioEngine.noteOff(n));
          midiHeldKeysRef.current.delete(note);
          syncActiveKeyMask();
        } else {
          audioEngine.noteOff(note);
        }
      },
      (cc, val) => {
        if (cc === 74) {
          const cutoff = Math.round(60 + (val / 127) * 18940);
          onPatchChange((prev) => {
            const next = { ...prev, filterCutoff: cutoff };
            audioEngine.setPatch(next);
            return next;
          });
        }
      },
      () => {
        refreshMidiPorts();
      }
    );
  }, [onPatchChange, refreshMidiPorts, syncActiveKeyMask]);

  const handleToggleMidiConnect = async () => {
    if (midiConnected) {
      midiManager.disconnect();
      midiHeldKeysRef.current.clear();
      syncActiveKeyMask();
      setMidiConnected(false);
      setSelectedInputId('');
      setSelectedOutputId('');
      setMidiNotice(null);
      return;
    }

    if (!midiManager.getIsSupported()) {
      setMidiNotice('Web MIDI unsupported in this browser');
      return;
    }

    setMidiNotice(null);
    const ok = await midiManager.initMidi();
    if (!ok) {
      setMidiNotice('MIDI access denied');
      return;
    }

    refreshMidiPorts();
    const ins = midiManager.getInputs();
    const outs = midiManager.getOutputs();
    setMidiConnected(true);
    if (ins.length === 0 && outs.length === 0) {
      setMidiNotice('No MIDI device detected');
    }
  };

  useEffect(() => {
    if (patch.bank && patch.bank !== activeBank) {
      setActiveBank(patch.bank);
    }
  }, [patch.id, patch.bank, activeBank]);

  useEffect(() => {
    audioEngine.setPatch(patch);
  }, [patch]);

  // Context-sensitive parameter mapping for Knob 1 to 4 across all pages
  const getKnobDef = useCallback(
    (knobIndex: 1 | 2 | 3 | 4) => {
      switch (activePage) {
        case 'FX':
          if (subPage === 1) {
            const dlyMix = patch.fxDelayMix ?? Math.min(72, Math.max(30, Math.round(patch.fxDelayFeedback * 0.9)));
            if (knobIndex === 1) return { name: 'KNOB1', label: 'DLY MIX', display: `${dlyMix}%`, val: dlyMix / 100 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'DLY FB', display: `${patch.fxDelayFeedback}%`, val: patch.fxDelayFeedback / 95 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'PHS HZ', display: `${(patch.fxPhaserRate ?? 0.5).toFixed(1)}Hz`, val: (patch.fxPhaserRate ?? 0.5) / 6.0 };
            const sIdx = Math.max(0, SAT_TYPES.indexOf(patch.saturationType || 'soft'));
            return { name: 'KNOB4', label: 'SAT TYPE', display: (patch.saturationType || 'soft').toUpperCase(), val: sIdx / 3 };
          }
          if (knobIndex === 1) return { name: 'KNOB1', label: 'CHORUS', display: `${patch.fxChorus}%`, val: patch.fxChorus / 100 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'PHASER', display: `${patch.fxPhaserMix ?? 0}%`, val: (patch.fxPhaserMix ?? 0) / 100 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'DELAY', display: `${patch.fxDelayTime.toFixed(2)}s`, val: patch.fxDelayTime / 1.0 };
          return { name: 'KNOB4', label: 'REVERB', display: `${patch.fxReverbSize}%`, val: patch.fxReverbSize / 100 };

        case 'ENV':
          if (subPage === 1) {
            if (knobIndex === 1) return { name: 'KNOB1', label: 'F-ATK', display: `${Math.round((patch.filterAttack ?? 0.008) * 1000)}ms`, val: (patch.filterAttack ?? 0.008) / 2.0 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'F-DEC', display: `${(patch.filterDecay ?? 0.42).toFixed(2)}s`, val: (patch.filterDecay ?? 0.42) / 2.5 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'F-SUS', display: `${Math.round((patch.filterSustain ?? 0.35) * 100)}%`, val: patch.filterSustain ?? 0.35 };
            return { name: 'KNOB4', label: 'F-ENV', display: `${patch.filterEnvAmount ?? 45}%`, val: ((patch.filterEnvAmount ?? 45) + 100) / 200 };
          }
          if (knobIndex === 1) return { name: 'KNOB1', label: 'ATTACK', display: `${Math.round((patch.attack ?? 0.008) * 1000)}ms`, val: (patch.attack ?? 0.008) / 2.0 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'DECAY', display: `${(patch.decay ?? 0.85).toFixed(2)}s`, val: (patch.decay ?? 0.85) / 3.0 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'SUSTAIN', display: `${Math.round((patch.sustain ?? 0.75) * 100)}%`, val: patch.sustain ?? 0.75 };
          return { name: 'KNOB4', label: 'RELEASE', display: `${(patch.release ?? 0.35).toFixed(2)}s`, val: (patch.release ?? 0.35) / 3.0 };

        case 'SEL': {
          if (subPage === 2) {
            if (knobIndex === 1) return { name: 'KNOB1', label: 'F-BAL', display: `${Math.round(patch.filterBalance ?? 50)}%`, val: (patch.filterBalance ?? 50) / 100 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'F-ENV', display: `${patch.filterEnvAmount ?? 45}%`, val: ((patch.filterEnvAmount ?? 45) + 100) / 200 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'F-REL', display: `${(patch.filterRelease ?? 0.35).toFixed(2)}s`, val: (patch.filterRelease ?? 0.35) / 2.5 };
            return { name: 'KNOB4', label: 'KEYFLW', display: `${patch.filterKeyfollow ?? 35}%`, val: (patch.filterKeyfollow ?? 35) / 100 };
          }
          if (subPage === 1) {
            if (knobIndex === 1) return { name: 'KNOB1', label: 'CUTOFF 1', display: `${Math.round(patch.filterCutoff)}Hz`, val: patch.filterCutoff / 19000 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'RESO 1', display: `${patch.filterReso.toFixed(1)}`, val: patch.filterReso / 18 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'F2 CUT', display: `${Math.round(patch.filter2Cutoff ?? 9500)}Hz`, val: (patch.filter2Cutoff ?? 9500) / 19000 };
            return { name: 'KNOB4', label: 'F2 RESO', display: `${(patch.filter2Reso ?? 1.5).toFixed(1)}`, val: ((patch.filter2Reso ?? 1.5) - 0.5) / 17.5 };
          }
          const comboIdx = Math.max(
            0,
            FILTER_COMBOS.findIndex(
              (c) => c.filterType === patch.filterType && (patch.filterRouting || 'serial') === c.filterRouting
            )
          );
          const curRoute = patch.filterRouting || 'serial';
          const f1T = patch.filterType || 'lowpass';
          const f2T = patch.filter2Type || 'lowpass';
          if (knobIndex === 1) return { name: 'KNOB1', label: 'FLT MODE', display: FILTER_COMBOS[comboIdx].short, val: comboIdx / (FILTER_COMBOS.length - 1) };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'ROUTING', display: curRoute.toUpperCase(), val: Math.max(0, FILTER_ROUTINGS.indexOf(curRoute)) / 2 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'F1 TYPE', display: FILTER_TYPE_LABELS[f1T], val: Math.max(0, FILTER_TYPES.indexOf(f1T)) / 3 };
          return { name: 'KNOB4', label: 'F2 TYPE', display: FILTER_TYPE_LABELS[f2T], val: Math.max(0, FILTER_TYPES.indexOf(f2T)) / 3 };
        }

        case 'LFO': {
          if (subPage === 2) {
            if (knobIndex === 1) return { name: 'KNOB1', label: 'SWARM', display: `${patch.swarmDepth ?? 22}%`, val: (patch.swarmDepth ?? 22) / 100 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'SWM HZ', display: `${(patch.swarmRate ?? 0.38).toFixed(2)}Hz`, val: (patch.swarmRate ?? 0.38) / 4.0 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'GLIDE', display: `${patch.glide ?? 0}`, val: (patch.glide ?? 0) / 85 };
            return { name: 'KNOB4', label: 'KEYFLW', display: `${patch.filterKeyfollow ?? 35}%`, val: (patch.filterKeyfollow ?? 35) / 100 };
          }
          const shapes: Array<'sine' | 'triangle' | 'saw' | 'square'> = ['sine', 'triangle', 'saw', 'square'];
          if (subPage === 1) {
            const d2 = patch.lfo2Dest || 'pan';
            if (knobIndex === 1) return { name: 'KNOB1', label: 'LFO2 HZ', display: `${(patch.lfo2Rate ?? 0.9).toFixed(1)}Hz`, val: (patch.lfo2Rate ?? 0.9) / 20.0 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'LFO2 AMT', display: `${patch.lfo2Depth ?? 12}%`, val: (patch.lfo2Depth ?? 12) / 100 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'LFO2 DST', display: LFO_DEST_LABELS[d2] || d2.toUpperCase(), val: Math.max(0, LFO_DESTS.indexOf(d2)) / (LFO_DESTS.length - 1) };
            return { name: 'KNOB4', label: 'LFO2 SHP', display: (patch.lfo2Shape || 'triangle').toUpperCase().slice(0, 4), val: Math.max(0, shapes.indexOf(patch.lfo2Shape || 'triangle')) / 3 };
          }
          const d1 = patch.lfoDest || 'filter';
          if (knobIndex === 1) return { name: 'KNOB1', label: 'LFO1 HZ', display: `${(patch.lfoRate ?? 4.5).toFixed(1)}Hz`, val: (patch.lfoRate ?? 4.5) / 20.0 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'LFO1 AMT', display: `${patch.lfoDepth ?? 18}%`, val: (patch.lfoDepth ?? 18) / 100 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'LFO1 DST', display: LFO_DEST_LABELS[d1] || d1.toUpperCase(), val: Math.max(0, LFO_DESTS.indexOf(d1)) / (LFO_DESTS.length - 1) };
          return { name: 'KNOB4', label: 'LFO1 SHP', display: (patch.lfoShape || 'sine').toUpperCase().slice(0, 4), val: Math.max(0, shapes.indexOf(patch.lfoShape || 'sine')) / 3 };
        }

        case 'ARP': {
          const arpModes: Array<'up' | 'down' | 'updown' | 'random'> = ['up', 'down', 'updown', 'random'];
          const arpRates: Array<'1/8' | '1/16' | '1/16T' | '1/32'> = ['1/8', '1/16', '1/16T', '1/32'];
          if (subPage === 1) {
            const arpOct = patch.arpOctaves || 1;
            if (knobIndex === 1) return { name: 'KNOB1', label: 'OCTAVES', display: `${arpOct} OCT`, val: (arpOct - 1) / 2 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'GATE', display: `${patch.arpGate ?? 72}%`, val: ((patch.arpGate ?? 72) - 20) / 75 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'SWING', display: `${swing}%`, val: (swing - 50) / 20 };
            return { name: 'KNOB4', label: 'TEMPO', display: `${bpm}`, val: (bpm - 40) / 200 };
          }
          const isOn = Boolean(patch.arpEnabled);
          const isLatched = Boolean(patch.arpLatch);
          if (knobIndex === 1) return { name: 'KNOB1', label: 'ARP', display: isOn ? 'ON' : 'OFF', val: isOn ? 1 : 0 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'LATCH', display: isLatched ? 'ON' : 'OFF', val: isLatched ? 1 : 0 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'PATTERN', display: (patch.arpMode || 'updown').toUpperCase(), val: Math.max(0, arpModes.indexOf(patch.arpMode || 'updown')) / 3 };
          return { name: 'KNOB4', label: 'RATE', display: patch.arpRate || '1/16', val: Math.max(0, arpRates.indexOf(patch.arpRate || '1/16')) / 3 };
        }

        case 'EDIT': {
          if (subPage === 2) {
            const isSync = Boolean(patch.osc2Sync);
            if (knobIndex === 1) return { name: 'KNOB1', label: 'SYNC', display: isSync ? 'ON' : 'OFF', val: isSync ? 1 : 0 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'FM AMT', display: `${patch.fmAmount ?? 0}%`, val: (patch.fmAmount ?? 0) / 100 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'UNI PAN', display: `${patch.unisonPanSpread ?? 75}%`, val: (patch.unisonPanSpread ?? 75) / 100 };
            return { name: 'KNOB4', label: 'DRIVE', display: `${patch.fxDrive}%`, val: patch.fxDrive / 100 };
          }
          if (subPage === 1) {
            const subW = patch.subWaveform || 'square';
            if (knobIndex === 1) return { name: 'KNOB1', label: 'SUB LVL', display: `${patch.subOscLevel ?? 65}%`, val: (patch.subOscLevel ?? 65) / 100 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'SUB WAVE', display: subW.toUpperCase(), val: Math.max(0, SUB_WAVES.indexOf(subW)) / 2 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'NOISE', display: `${patch.noiseVolume ?? 0}%`, val: (patch.noiseVolume ?? 0) / 100 };
            return { name: 'KNOB4', label: 'RING MOD', display: `${patch.ringModVolume ?? 0}%`, val: (patch.ringModVolume ?? 0) / 100 };
          }
          const mode = patch.oscMode || 'hypersaw';
          if (knobIndex === 1) return { name: 'KNOB1', label: 'OSC BAL', display: `${patch.oscBalance ?? 50}%`, val: (patch.oscBalance ?? 50) / 100 };
          if (knobIndex === 2) {
            const semi = patch.osc2Semitone ?? 0;
            return { name: 'KNOB2', label: 'OSC2 ST', display: `${semi >= 0 ? `+${semi}` : semi}st`, val: (semi + 24) / 48 };
          }
          if (knobIndex === 3) {
            if (mode === 'wavetable') {
              return { name: 'KNOB3', label: 'WT POS', display: `${Math.round((patch.wavetablePos ?? 0.35) * 100)}%`, val: patch.wavetablePos ?? 0.35 };
            }
            if (mode === 'pulse') {
              return { name: 'KNOB3', label: 'PW', display: `${Math.round((patch.pulseWidth ?? 0.5) * 100)}%`, val: ((patch.pulseWidth ?? 0.5) - 0.05) / 0.9 };
            }
            return { name: 'KNOB3', label: 'SPREAD', display: `${patch.hypersawSpread ?? 85}%`, val: (patch.hypersawSpread ?? 85) / 100 };
          }
          const uni = Math.max(1, Math.min(5, patch.unisonVoices ?? 1));
          return { name: 'KNOB4', label: 'UNISON', display: `${uni}V`, val: (uni - 1) / 4 };
        }

        case 'SEQ': {
          const track = drumTracks[selectedDrumTrack] || drumTracks[0];
          if (subPage === 1) {
            if (knobIndex === 1) return { name: 'KNOB1', label: 'EUC HITS', display: `${track?.euclidPulses ?? 0}/${seqLength}`, val: (track?.euclidPulses ?? 0) / Math.max(1, seqLength) };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'ROTATE', display: `+${track?.euclidRotate ?? 0}`, val: (track?.euclidRotate ?? 0) / Math.max(1, seqLength - 1) };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'SWING', display: `${swing}%`, val: (swing - 50) / 25 };
            return { name: 'KNOB4', label: 'LENGTH', display: `${seqLength} ST`, val: (seqLength - 1) / 63 };
          }
          if (subPage === 2) {
            const activeBar = Math.floor(selectedStepIdx / 16) + 1;
            const pIdx = DRUM_PRESET_ORDER.indexOf(drumKitPreset);
            const patDisplay = drumKitPreset === 'CLEAR' ? 'CUSTOM' : drumKitPreset;
            const patVal = pIdx >= 0 ? pIdx / Math.max(1, DRUM_PRESET_ORDER.length - 1) : 0;
            if (knobIndex === 1) return { name: 'KNOB1', label: 'DRM LVL', display: `${drumVolume}%`, val: drumVolume / 100 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'MST BPM', display: `${bpm}`, val: (bpm - 40) / 200 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'BAR', display: `BAR ${activeBar}`, val: (activeBar - 1) / 3 };
            return { name: 'KNOB4', label: 'PATTERN', display: patDisplay, val: patVal };
          }
          const tune = track?.tune ?? 0;
          const decay = track?.decay ?? 100;
          const tone = track?.tone ?? 60;
          const level = track?.level ?? 90;
          if (knobIndex === 1) return { name: 'KNOB1', label: 'TUNE', display: `${tune >= 0 ? `+${tune}` : tune}st`, val: (tune + 12) / 24 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'DECAY', display: `${decay}%`, val: (decay - 25) / 175 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'TONE', display: `${tone}%`, val: tone / 100 };
          return { name: 'KNOB4', label: 'LEVEL', display: `${level}%`, val: level / 100 };
        }

        case 'GLO':
          if (subPage === 2) {
            const sIdx = Math.max(0, RANDOM_PATCH_STYLES.indexOf(randStyle));
            if (knobIndex === 1) return { name: 'KNOB1', label: 'STYLE', display: randStyle, val: sIdx / (RANDOM_PATCH_STYLES.length - 1) };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'RANDOMIZE', display: randCount > 0 ? `GEN #${randCount}` : 'TURN!', val: (randCount % 16) / 15 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'CHAOS', display: `${randChaos}%`, val: (randChaos - 20) / 80 };
            const bankXList = getBankPresets('X');
            const isSlotUsed = Boolean(bankXList[targetBankXSlot]?.isUserPreset);
            const slotStr = targetBankXSlot.toString().padStart(3, '0');
            return { name: 'KNOB4', label: 'BANK X SLOT', display: `X${slotStr}${isSlotUsed ? '*' : ''}`, val: targetBankXSlot / 127 };
          }
          if (subPage === 1) {
            const bars = Math.max(1, Math.min(4, Math.ceil(seqLength / 16)));
            const barsLabel = seqLength % 16 === 0 ? `${bars} BAR` : `${seqLength} ST`;
            if (knobIndex === 1) return { name: 'KNOB1', label: 'REC BARS', display: barsLabel, val: (bars - 1) / 3 };
            if (knobIndex === 2) return { name: 'KNOB2', label: 'POLY', display: `${patch.polyphony || 8}V`, val: ((patch.polyphony || 8) - 1) / 7 };
            if (knobIndex === 3) return { name: 'KNOB3', label: 'SWING', display: `${swing}%`, val: (swing - 50) / 25 };
            return { name: 'KNOB4', label: 'GLIDE', display: `${patch.glide ?? 0}`, val: (patch.glide ?? 0) / 85 };
          }
          if (knobIndex === 1) return { name: 'KNOB1', label: 'ROOT', display: NOTE_NAMES_12[rootPc % 12], val: (rootPc % 12) / 11 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'SCALE', display: SCALE_LABELS[scaleMode].toUpperCase(), val: Math.max(0, SCALE_MODES.indexOf(scaleMode)) / (SCALE_MODES.length - 1) };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'TEMPO', display: `${bpm} BPM`, val: (bpm - 40) / 200 };
          return { name: 'KNOB4', label: 'CHORD', display: CHORD_LABELS[chordMode].toUpperCase().slice(0, 7), val: Math.max(0, CHORD_MODES.indexOf(chordMode)) / (CHORD_MODES.length - 1) };

        case 'HOME':
        default:
          if (knobIndex === 1) return { name: 'KNOB1', label: 'CUTOFF 1', display: `${Math.round(patch.filterCutoff)}Hz`, val: patch.filterCutoff / 19000 };
          if (knobIndex === 2) return { name: 'KNOB2', label: 'RESO 1', display: `${patch.filterReso.toFixed(1)}`, val: patch.filterReso / 18 };
          if (knobIndex === 3) return { name: 'KNOB3', label: 'CUTOFF 2', display: `${Math.round(patch.filter2Cutoff ?? 9500)}Hz`, val: (patch.filter2Cutoff ?? 9500) / 19000 };
          return { name: 'KNOB4', label: 'F-BAL', display: `${Math.round(patch.filterBalance ?? 50)}%`, val: (patch.filterBalance ?? 50) / 100 };
      }
    },
    [activePage, subPage, patch, bpm, selectedDrumTrack, selectedStepIdx, seqLength, drumTracks, drumVolume, drumKitPreset, swing, octaveOffset, rootPc, scaleMode, chordMode, randStyle, randChaos, randCount, targetBankXSlot, bankRevision]
  );

  // Endless 360-degree rotary encoder angles (unbounded, no potentiometer end-stops)
  const [knobAngles, setKnobAngles] = useState<Record<string, number>>({
    MASTER: 0,
    SELECT: 0,
    PRESETS: 0,
    ALGORITHM: 0,
    KNOB1: 0,
    KNOB2: 0,
    KNOB3: 0,
    KNOB4: 0
  });

  const [draggingKnob, setDraggingKnob] = useState<string | null>(null);
  const dragLastY = useRef<number>(0);
  const dragStepAccum = useRef<number>(0);

  // Smart 12-second Menu Inactivity Auto-Return (SEQ Drum Machine latches permanently until exited)
  useEffect(() => {
    if (activePage === 'HOME' || activePage === 'SEQ') return;
    const interval = window.setInterval(() => {
      if (draggingKnob || isRecording || confirmPrompt) return;
      if (performance.now() - lastMenuActivityRef.current >= 12000) {
        setActivePage('HOME');
        setSubPage(0);
      }
    }, 300);
    return () => window.clearInterval(interval);
  }, [activePage, draggingKnob, isRecording, confirmPrompt]);

  // Embedded 240x240 Square 1.54" Pocket Hardware Display Rendering (with mini Top-Right Starfish!)
  useEffect(() => {
    let animId: number;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const timeData = new Uint8Array(256);
    const freqData = new Uint8Array(256);
    let lfoPhase = 0;

    // Draws the cute mini Starfish (with 360° spin on a dime!) or Drum Mode Starfish Drummer Boy inside the OLED top-right corner
    const drawMiniStarfish = (now: number, rms: number) => {
      const st = starAnimRef.current;
      st.bounceEnergy = Math.max(0, st.bounceEnergy * 0.89);
      st.targetSwivel *= 0.9;
      st.targetDanceX *= 0.9;
      st.swivelAngle += (st.targetSwivel - st.swivelAngle) * 0.24;
      st.danceX += (st.targetDanceX - st.danceX) * 0.24;

      st.spinCurrent += (st.spinTarget - st.spinCurrent) * 0.26;
      if (Math.abs(st.spinTarget - st.spinCurrent) < 0.45) {
        st.spinCurrent = st.spinTarget % 360;
        st.spinTarget = st.spinCurrent;
      }

      const t = now / 1000;
      const isSeq = activePage === 'SEQ';
      const grooveAmp = Math.max(st.bounceEnergy, rms * 0.85, isPlayingSeq ? 0.3 : 0);
      const idleSwivel = Math.sin(t * 2.3) * 5;
      const idleBob = Math.sin(t * 3.1) * 1.1;
      const stepY = -grooveAmp * 3.0 + (isPlayingSeq ? -Math.abs(Math.sin(t * 8.2)) * 1.6 : 0);
      const rotDeg = st.spinCurrent + st.swivelAngle + idleSwivel + (isPlayingSeq ? Math.sin(t * 4.1) * 7 : 0);
      const sx = 1 + st.bounceEnergy * 0.09 + rms * 0.06;
      const sy = 1 - st.bounceEnergy * 0.06 - rms * 0.04;

      if (now > st.nextBlinkAt) {
        st.nextBlinkAt = now + 3200 + Math.random() * 1400;
      }
      const blinking = st.nextBlinkAt - now < 130 && st.nextBlinkAt - now > 0;
      const happy = now < st.happyUntil;
      const pal = STARFISH_COLORS[st.paletteIdx];
      const drumHitting = isSeq && now < st.drumHitUntil;

      // Mini Glowing Snare/Pad Drum beneath Starfish Drummer Boy in SEQ mode
      if (isSeq) {
        ctx.save();
        ctx.translate(221, 26);
        ctx.fillStyle = drumHitting ? '#FBBF24' : '#334155';
        ctx.beginPath();
        ctx.ellipse(0, 2.5, 11.5, 4.2, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#0F172A';
        ctx.fillRect(-11, 2.5, 22, 4.8);
        ctx.fillStyle = drumHitting ? '#FEF3C7' : '#22D3EE';
        ctx.beginPath();
        ctx.ellipse(0, 2.5, 9.5, 3.0, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = drumHitting ? '#FDE68A' : '#64748B';
        ctx.lineWidth = 1.2;
        ctx.beginPath();
        ctx.moveTo(-11, 2.5);
        ctx.lineTo(-11, 7.2);
        ctx.moveTo(11, 2.5);
        ctx.lineTo(11, 7.2);
        ctx.moveTo(-11, 7.2);
        ctx.quadraticCurveTo(0, 10.5, 11, 7.2);
        ctx.stroke();
        ctx.restore();
      }

      ctx.save();
      ctx.translate(221 + st.danceX, (isSeq ? 14 : 17) + idleBob + stepY);
      ctx.rotate((rotDeg * Math.PI) / 180);
      ctx.scale((isSeq ? 0.22 : 0.24) * sx, (isSeq ? 0.22 : 0.24) * sy);
      ctx.translate(-60, -60);

      // Soft glow aura
      ctx.shadowColor = pal.body;
      ctx.shadowBlur = 8;
      // 5-arm organic starfish body path
      ctx.beginPath();
      ctx.moveTo(60, 10);
      ctx.bezierCurveTo(67, 10, 70, 30, 75, 35);
      ctx.bezierCurveTo(81, 41, 103, 36, 107, 43);
      ctx.bezierCurveTo(111, 50, 93, 62, 91, 69);
      ctx.bezierCurveTo(89, 77, 104, 94, 98, 100);
      ctx.bezierCurveTo(91, 107, 75, 94, 67, 93);
      ctx.bezierCurveTo(59, 92, 49, 107, 41, 104);
      ctx.bezierCurveTo(33, 100, 40, 80, 37, 72);
      ctx.bezierCurveTo(34, 65, 13, 59, 14, 50);
      ctx.bezierCurveTo(16, 42, 38, 43, 44, 37);
      ctx.bezierCurveTo(50, 31, 53, 10, 60, 10);
      ctx.closePath();
      ctx.fillStyle = pal.body;
      ctx.fill();
      ctx.shadowBlur = 0;
      ctx.lineWidth = 3.2;
      ctx.strokeStyle = 'rgba(255,255,255,0.45)';
      ctx.stroke();

      // Belly highlight & rosy cheeks
      ctx.beginPath();
      ctx.arc(60, 63, 19, 0, Math.PI * 2);
      ctx.fillStyle = pal.belly;
      ctx.globalAlpha = 0.45;
      ctx.fill();
      ctx.globalAlpha = 1;

      ctx.fillStyle = pal.cheek;
      ctx.beginPath();
      ctx.ellipse(46, 67, 4.5, 2.8, 0, 0, Math.PI * 2);
      ctx.ellipse(74, 67, 4.5, 2.8, 0, 0, Math.PI * 2);
      ctx.fill();

      // Eyes & Happy Smile
      ctx.strokeStyle = '#0F131C';
      ctx.fillStyle = '#0F131C';
      ctx.lineCap = 'round';
      ctx.lineWidth = 4.2;
      if (happy) {
        ctx.beginPath();
        ctx.moveTo(47, 58);
        ctx.quadraticCurveTo(51.5, 52, 56, 58);
        ctx.moveTo(64, 58);
        ctx.quadraticCurveTo(68.5, 52, 73, 58);
        ctx.stroke();
      } else if (blinking) {
        ctx.beginPath();
        ctx.moveTo(47, 58);
        ctx.lineTo(56, 58);
        ctx.moveTo(64, 58);
        ctx.lineTo(73, 58);
        ctx.stroke();
      } else {
        ctx.beginPath();
        ctx.arc(51.5, 57.5, 4.4, 0, Math.PI * 2);
        ctx.arc(68.5, 57.5, 4.4, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#FFFFFF';
        ctx.beginPath();
        ctx.arc(53, 56, 1.5, 0, Math.PI * 2);
        ctx.arc(70, 56, 1.5, 0, Math.PI * 2);
        ctx.fill();
      }

      ctx.beginPath();
      if (happy) {
        ctx.moveTo(54, 66);
        ctx.quadraticCurveTo(60, 75, 66, 66);
        ctx.closePath();
        ctx.fillStyle = '#0F131C';
        ctx.fill();
      } else {
        ctx.lineWidth = 3.8;
        ctx.moveTo(54, 66);
        ctx.quadraticCurveTo(60, 72, 66, 66);
        ctx.stroke();
      }

      // Drum Mode: Cute Drummer Boy Cap + Dual Animated Drumsticks!
      if (isSeq) {
        // Drummer Snapback Cap perched on top arm
        ctx.fillStyle = '#0EA5E9';
        ctx.beginPath();
        ctx.arc(60, 29, 17, Math.PI, 0);
        ctx.closePath();
        ctx.fill();
        ctx.strokeStyle = '#FFFFFF';
        ctx.lineWidth = 2.5;
        ctx.stroke();
        // Cap visor pointing slightly right
        ctx.fillStyle = '#FBBF24';
        ctx.beginPath();
        ctx.roundRect(56, 26, 24, 6, 3);
        ctx.fill();

        // Left & Right Drumsticks alternating strikes onto the mini drum below
        const doubleHit = st.lastDrumIdx === 1 || st.lastDrumIdx === 4;
        const leftDown = drumHitting && (doubleHit || st.drumStickSide === 1);
        const rightDown = drumHitting && (doubleHit || st.drumStickSide === -1);

        // Left stick
        ctx.strokeStyle = '#FDE68A';
        ctx.lineWidth = 5;
        ctx.beginPath();
        ctx.moveTo(24, 58);
        ctx.lineTo(leftDown ? 48 : 30, leftDown ? 108 : 84);
        ctx.stroke();
        ctx.fillStyle = '#FFFFFF';
        ctx.beginPath();
        ctx.arc(leftDown ? 48 : 30, leftDown ? 108 : 84, 5, 0, Math.PI * 2);
        ctx.fill();

        // Right stick
        ctx.beginPath();
        ctx.moveTo(96, 58);
        ctx.lineTo(rightDown ? 72 : 90, rightDown ? 108 : 84);
        ctx.stroke();
        ctx.beginPath();
        ctx.arc(rightDown ? 72 : 90, rightDown ? 108 : 84, 5, 0, Math.PI * 2);
        ctx.fill();
      }

      ctx.restore();
    };

    const renderOled = () => {
      animId = requestAnimationFrame(renderOled);
      const W = canvas.width;  // 240
      const H = canvas.height; // 240
      const now = performance.now();
      const activeLfoRate = activePage === 'LFO' && subPage === 1 ? (patch.lfo2Rate ?? 0.9) : (patch.lfoRate ?? 4.5);
      lfoPhase += activeLfoRate * 0.045;

      // Sample audio waveform & FFT spectrum for Scope, Analyzer & Starfish bounce
      audioEngine.getOscilloscopeData(timeData);
      audioEngine.getFrequencyData(freqData);
      let sumSq = 0;
      for (let i = 0; i < timeData.length; i += 2) {
        const c = (timeData[i] - 128) / 128;
        sumSq += c * c;
      }
      const rms = Math.min(1, Math.sqrt(sumSq / (timeData.length / 2)) * 4.5);

      // Deep OLED Obsidian Background
      ctx.fillStyle = '#07090E';
      ctx.fillRect(0, 0, W, H);

      // Sleek Standby Screen when Power Switch in Top Nav Bar is OFF
      if (!isPoweredOn) {
        ctx.fillStyle = '#04060A';
        ctx.fillRect(0, 0, W, H);
        ctx.beginPath();
        ctx.arc(W / 2, H / 2 - 14, 4, 0, Math.PI * 2);
        ctx.fillStyle = '#475569';
        ctx.fill();
        ctx.fillStyle = '#334155';
        ctx.font = '700 12px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText('STARFISH · OFF', W / 2, H / 2 + 10);
        ctx.fillStyle = '#1E293B';
        ctx.font = '600 9.5px "JetBrains Mono", monospace';
        ctx.fillText('POWER ON IN TOP BAR', W / 2, H / 2 + 28);
        ctx.textAlign = 'left';
        return;
      }

      // 0. Boot-Up Starfish Animation INSIDE Hardware OLED Display (1 of 6 acts + 1 of 6 jingles)
      const activeBoot = bootSplashRef.current;
      if (activeBoot && activeBoot.until > now) {
        const elapsedSec = (now - activeBoot.startedAt) / 1000;
        const progress = Math.max(0, Math.min(1, (now - activeBoot.startedAt) / BOOT_SPLASH_MS));
        const act = BOOT_ACTS[activeBoot.actIdx] || BOOT_ACTS[0];
        const jingle = BOOT_JINGLES[activeBoot.jingleIdx] || BOOT_JINGLES[0];
        const pal = STARFISH_COLORS[activeBoot.actIdx % STARFISH_COLORS.length];

        const drawSceneStar = (
          cx: number,
          cy: number,
          sc: number,
          rotRad: number,
          opts?: { sunglasses?: boolean; helmet?: boolean; captainHat?: boolean; skateCap?: boolean; chefHat?: boolean }
        ) => {
          ctx.save();
          ctx.translate(cx, cy);
          ctx.rotate(rotRad);
          ctx.scale(sc, sc);
          ctx.translate(-60, -60);

          ctx.shadowColor = pal.body;
          ctx.shadowBlur = 10;
          ctx.beginPath();
          ctx.moveTo(60, 10);
          ctx.bezierCurveTo(67, 10, 70, 30, 75, 35);
          ctx.bezierCurveTo(81, 41, 103, 36, 107, 43);
          ctx.bezierCurveTo(111, 50, 93, 62, 91, 69);
          ctx.bezierCurveTo(89, 77, 104, 94, 98, 100);
          ctx.bezierCurveTo(91, 107, 75, 94, 67, 93);
          ctx.bezierCurveTo(59, 92, 49, 107, 41, 104);
          ctx.bezierCurveTo(33, 100, 40, 80, 37, 72);
          ctx.bezierCurveTo(34, 65, 13, 59, 14, 50);
          ctx.bezierCurveTo(16, 42, 38, 43, 44, 37);
          ctx.bezierCurveTo(50, 31, 53, 10, 60, 10);
          ctx.closePath();
          ctx.fillStyle = pal.body;
          ctx.fill();
          ctx.shadowBlur = 0;
          ctx.lineWidth = 3;
          ctx.strokeStyle = 'rgba(255,255,255,0.55)';
          ctx.stroke();

          // Belly + Rosy cheeks
          ctx.beginPath();
          ctx.arc(60, 63, 18, 0, Math.PI * 2);
          ctx.fillStyle = pal.belly;
          ctx.globalAlpha = 0.45;
          ctx.fill();
          ctx.globalAlpha = 1;
          ctx.fillStyle = pal.cheek;
          ctx.beginPath();
          ctx.ellipse(45, 67, 4.5, 2.8, 0, 0, Math.PI * 2);
          ctx.ellipse(75, 67, 4.5, 2.8, 0, 0, Math.PI * 2);
          ctx.fill();

          if (opts?.sunglasses) {
            ctx.fillStyle = '#0B111C';
            ctx.strokeStyle = '#F8FAFC';
            ctx.lineWidth = 2;
            ctx.beginPath();
            ctx.roundRect(41, 51, 17, 11, 4);
            ctx.roundRect(62, 51, 17, 11, 4);
            ctx.fill();
            ctx.stroke();
            ctx.fillRect(57, 54, 6, 3);
            ctx.strokeStyle = 'rgba(255,255,255,0.75)';
            ctx.lineWidth = 1.6;
            ctx.beginPath();
            ctx.moveTo(44, 58);
            ctx.lineTo(49, 53);
            ctx.moveTo(65, 58);
            ctx.lineTo(70, 53);
            ctx.stroke();
          } else {
            ctx.fillStyle = '#0F131C';
            ctx.beginPath();
            ctx.arc(51.5, 57.5, 4.3, 0, Math.PI * 2);
            ctx.arc(68.5, 57.5, 4.3, 0, Math.PI * 2);
            ctx.fill();
            ctx.fillStyle = '#FFFFFF';
            ctx.beginPath();
            ctx.arc(53, 56, 1.5, 0, Math.PI * 2);
            ctx.arc(70, 56, 1.5, 0, Math.PI * 2);
            ctx.fill();
          }

          // Smile
          ctx.beginPath();
          ctx.moveTo(54, 67);
          ctx.quadraticCurveTo(60, 75, 66, 67);
          ctx.closePath();
          ctx.fillStyle = '#0F131C';
          ctx.fill();

          if (opts?.helmet) {
            ctx.strokeStyle = '#67E8F9';
            ctx.lineWidth = 3.2;
            ctx.fillStyle = 'rgba(103, 232, 249, 0.14)';
            ctx.beginPath();
            ctx.arc(60, 59, 31, 0, Math.PI * 2);
            ctx.fill();
            ctx.stroke();
            ctx.strokeStyle = 'rgba(255,255,255,0.65)';
            ctx.lineWidth = 2.2;
            ctx.beginPath();
            ctx.arc(60, 59, 25, -Math.PI * 0.78, -Math.PI * 0.42);
            ctx.stroke();
          }
          if (opts?.captainHat) {
            ctx.fillStyle = '#F8FAFC';
            ctx.beginPath();
            ctx.roundRect(42, 23, 36, 12, 5);
            ctx.fill();
            ctx.fillStyle = '#0F172A';
            ctx.fillRect(42, 32, 36, 5);
            ctx.fillStyle = '#FBBF24';
            ctx.beginPath();
            ctx.arc(60, 29, 3, 0, Math.PI * 2);
            ctx.fill();
          }
          if (opts?.skateCap) {
            ctx.fillStyle = '#F59E0B';
            ctx.beginPath();
            ctx.arc(60, 32, 16, Math.PI, 0);
            ctx.closePath();
            ctx.fill();
            ctx.fillStyle = '#0EA5E9';
            ctx.beginPath();
            ctx.roundRect(38, 29, 24, 5.5, 2.5);
            ctx.fill();
          }
          if (opts?.chefHat) {
            ctx.fillStyle = '#F8FAFC';
            ctx.beginPath();
            ctx.arc(49, 21, 10, 0, Math.PI * 2);
            ctx.arc(60, 17, 12, 0, Math.PI * 2);
            ctx.arc(71, 21, 10, 0, Math.PI * 2);
            ctx.fill();
            ctx.fillRect(44, 24, 32, 10);
            ctx.fillStyle = '#CBD5E1';
            ctx.fillRect(44, 31, 32, 3);
          }

          ctx.restore();
        };

        // Scene Backdrop (y: 0..186)
        if (activeBoot.actIdx === 0) {
          // 1. BEACH CHAIR + SUNGLASSES + REEF JUICE
          const skyGrad = ctx.createLinearGradient(0, 0, 0, 145);
          skyGrad.addColorStop(0, '#0E172A');
          skyGrad.addColorStop(1, '#1E293B');
          ctx.fillStyle = skyGrad;
          ctx.fillRect(0, 0, W, 145);

          // Glowing sunset disc + ocean band + sandy beach
          ctx.fillStyle = '#FB923C';
          ctx.beginPath();
          ctx.arc(185, 78, 22, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = '#0284C7';
          ctx.fillRect(0, 96, W, 42);
          ctx.fillStyle = 'rgba(255,255,255,0.25)';
          ctx.fillRect(24, 108, 48, 2);
          ctx.fillRect(140, 118, 62, 2);
          ctx.fillStyle = '#FDE68A';
          ctx.beginPath();
          ctx.ellipse(W / 2, 178, 150, 44, 0, Math.PI, Math.PI * 2);
          ctx.fill();

          // Striped lounge beach chair
          ctx.strokeStyle = '#92400E';
          ctx.lineWidth = 4;
          ctx.beginPath();
          ctx.moveTo(64, 152);
          ctx.lineTo(54, 174);
          ctx.moveTo(154, 152);
          ctx.lineTo(164, 174);
          ctx.stroke();
          ctx.save();
          ctx.translate(106, 134);
          ctx.rotate(-0.34);
          ctx.fillStyle = '#F43F5E';
          ctx.beginPath();
          ctx.roundRect(-48, -8, 96, 14, 5);
          ctx.fill();
          ctx.fillStyle = '#FFF1F2';
          ctx.fillRect(-36, -8, 12, 14);
          ctx.fillRect(-8, -8, 12, 14);
          ctx.fillRect(20, -8, 12, 14);
          ctx.restore();

          // Lounging Starfish with sunglasses + juice sip bob
          const sipBob = Math.sin(elapsedSec * 3.2) * 2.2;
          drawSceneStar(102, 108 + sipBob, 0.56, -0.35, { sunglasses: true });

          // Tropical Juice Cup + Bendy Straw + Tiny Umbrella
          ctx.fillStyle = '#22D3EE';
          ctx.beginPath();
          ctx.roundRect(142, 110, 17, 22, 4);
          ctx.fill();
          ctx.fillStyle = '#FBBF24';
          ctx.fillRect(143, 112, 15, 5);
          ctx.strokeStyle = '#FDE047';
          ctx.lineWidth = 2.4;
          ctx.beginPath();
          ctx.moveTo(148, 112);
          ctx.lineTo(144, 98);
          ctx.lineTo(126, 98 + sipBob * 0.5);
          ctx.stroke();
          ctx.fillStyle = '#EC4899';
          ctx.beginPath();
          ctx.moveTo(154, 102);
          ctx.lineTo(168, 106);
          ctx.lineTo(159, 95);
          ctx.closePath();
          ctx.fill();
        } else if (activeBoot.actIdx === 1) {
          // 2. EXPLORING MOON + PLANTING FM-1 FLAG
          ctx.fillStyle = '#050814';
          ctx.fillRect(0, 0, W, 188);
          // Twinkling stars
          for (let i = 0; i < 18; i++) {
            const sx = ((i * 53 + 17) % 220) + 10;
            const sy = ((i * 37 + 11) % 112) + 8;
            const tw = 0.4 + 0.6 * Math.abs(Math.sin(elapsedSec * 2.5 + i));
            ctx.fillStyle = `rgba(226, 232, 240, ${tw.toFixed(2)})`;
            ctx.fillRect(sx, sy, i % 3 === 0 ? 2 : 1.5, i % 3 === 0 ? 2 : 1.5);
          }
          // Glowing blue Earth in top-left sky
          ctx.fillStyle = '#0EA5E9';
          ctx.beginPath();
          ctx.arc(44, 42, 16, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = '#34D399';
          ctx.beginPath();
          ctx.arc(40, 39, 6, 0, Math.PI * 2);
          ctx.arc(50, 47, 4.5, 0, Math.PI * 2);
          ctx.fill();

          // Cratered Lunar Surface
          ctx.fillStyle = '#475569';
          ctx.beginPath();
          ctx.ellipse(W / 2, 198, 160, 62, 0, Math.PI, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = '#334155';
          ctx.beginPath();
          ctx.ellipse(66, 158, 18, 6, 0, 0, Math.PI * 2);
          ctx.ellipse(174, 164, 14, 5, 0, 0, Math.PI * 2);
          ctx.fill();

          // FM-1 Flag planted on Moon!
          const flagWave = Math.sin(elapsedSec * 5.2) * 3;
          ctx.strokeStyle = '#E2E8F0';
          ctx.lineWidth = 3;
          ctx.beginPath();
          ctx.moveTo(156, 152);
          ctx.lineTo(156, 72);
          ctx.stroke();
          ctx.fillStyle = '#22D3EE';
          ctx.beginPath();
          ctx.moveTo(157, 74);
          ctx.quadraticCurveTo(182, 70 + flagWave, 206, 77 - flagWave * 0.5);
          ctx.lineTo(206, 98 - flagWave * 0.5);
          ctx.quadraticCurveTo(182, 93 + flagWave, 157, 98);
          ctx.closePath();
          ctx.fill();
          ctx.fillStyle = '#07090E';
          ctx.font = '800 9.5px "JetBrains Mono", monospace';
          ctx.fillText('FM-1', 166, 89 + flagWave * 0.4);

          const floatY = Math.sin(elapsedSec * 2.4) * 5;
          drawSceneStar(94, 114 + floatY, 0.62, Math.sin(elapsedSec * 1.8) * 0.14, { helmet: true });
        } else if (activeBoot.actIdx === 2) {
          // 3. YELLOW SUBMARINE CAPTAIN
          const seaGrad = ctx.createLinearGradient(0, 0, 0, 186);
          seaGrad.addColorStop(0, '#041C32');
          seaGrad.addColorStop(1, '#082F49');
          ctx.fillStyle = seaGrad;
          ctx.fillRect(0, 0, W, 186);

          // Animated rising bubbles
          for (let b = 0; b < 9; b++) {
            const bx = 24 + b * 24;
            const by = ((180 - elapsedSec * (32 + (b % 3) * 14) - b * 29) % 175 + 175) % 175;
            ctx.strokeStyle = 'rgba(103, 232, 249, 0.55)';
            ctx.lineWidth = 1.4;
            ctx.beginPath();
            ctx.arc(bx, by, 3 + (b % 3) * 1.5, 0, Math.PI * 2);
            ctx.stroke();
          }

          // Yellow Submarine Hull + Brass Porthole
          ctx.fillStyle = '#F59E0B';
          ctx.beginPath();
          ctx.roundRect(36, 48, 168, 108, 52);
          ctx.fill();
          ctx.fillStyle = '#0F172A';
          ctx.beginPath();
          ctx.arc(120, 104, 42, 0, Math.PI * 2);
          ctx.fill();
          ctx.strokeStyle = '#FDE68A';
          ctx.lineWidth = 7;
          ctx.stroke();

          drawSceneStar(120, 105 + Math.sin(elapsedSec * 3.4) * 2.5, 0.55, Math.sin(elapsedSec * 2.2) * 0.1, { captainHat: true });
        } else if (activeBoot.actIdx === 3) {
          // 4. CAMPFIRE ACOUSTIC JAM
          ctx.fillStyle = '#070B19';
          ctx.fillRect(0, 0, W, 186);
          for (let i = 0; i < 14; i++) {
            ctx.fillStyle = 'rgba(248, 250, 252, 0.7)';
            ctx.fillRect(((i * 47 + 19) % 210) + 14, ((i * 23 + 9) % 80) + 8, 1.8, 1.8);
          }
          ctx.fillStyle = '#1E293B';
          ctx.fillRect(0, 146, W, 42);

          // Flickering Campfire
          const flick = Math.sin(elapsedSec * 15) * 4;
          ctx.fillStyle = '#78350F';
          ctx.fillRect(142, 146, 38, 7);
          ctx.fillRect(146, 142, 30, 6);
          ctx.fillStyle = '#F97316';
          ctx.beginPath();
          ctx.moveTo(148, 144);
          ctx.quadraticCurveTo(161, 98 + flick, 174, 144);
          ctx.fill();
          ctx.fillStyle = '#FBBF24';
          ctx.beginPath();
          ctx.moveTo(153, 144);
          ctx.quadraticCurveTo(161, 114 - flick, 169, 144);
          ctx.fill();

          const strum = Math.sin(elapsedSec * 6.5) * 0.08;
          drawSceneStar(82, 116 + Math.sin(elapsedSec * 3.2) * 2, 0.58, strum);

          // Mini Acoustic Guitar across Starfish
          ctx.save();
          ctx.translate(92, 126);
          ctx.rotate(-0.35 + strum);
          ctx.fillStyle = '#D97706';
          ctx.beginPath();
          ctx.ellipse(0, 0, 14, 10, 0, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = '#451A03';
          ctx.beginPath();
          ctx.arc(-2, 0, 4, 0, Math.PI * 2);
          ctx.fill();
          ctx.fillStyle = '#92400E';
          ctx.fillRect(10, -2.5, 22, 5);
          ctx.restore();

          // Floating Music Notes
          const noteRise = (elapsedSec * 24) % 44;
          ctx.fillStyle = '#22D3EE';
          ctx.font = '800 16px "Plus Jakarta Sans", sans-serif';
          ctx.fillText('♪', 112, 82 - noteRise * 0.6);
          ctx.fillStyle = '#FBBF24';
          ctx.fillText('♫', 132, 92 - ((noteRise + 18) % 44) * 0.6);
        } else if (activeBoot.actIdx === 4) {
          // 5. ROCKET SKATEBOARDER 360° KICKFLIP
          ctx.fillStyle = '#09071A';
          ctx.fillRect(0, 0, W, 186);
          // Synthwave grid lines
          ctx.strokeStyle = 'rgba(34, 211, 238, 0.28)';
          ctx.lineWidth = 1;
          for (let y = 132; y <= 180; y += 12) {
            ctx.beginPath();
            ctx.moveTo(0, y);
            ctx.lineTo(W, y);
            ctx.stroke();
          }
          for (let x = -40; x <= 280; x += 34) {
            ctx.beginPath();
            ctx.moveTo(120 + (x - 120) * 0.3, 132);
            ctx.lineTo(x, 182);
            ctx.stroke();
          }

          // Kickflip arc + spin
          const jumpPhase = Math.abs(Math.sin(elapsedSec * 2.4));
          const starY = 104 - jumpPhase * 32;
          const flipSpin = elapsedSec * 5.2;

          // Skateboard Deck spinning cleanly beneath
          ctx.save();
          ctx.translate(120, starY + 34);
          ctx.rotate(Math.sin(elapsedSec * 2.4) * 0.25);
          ctx.scale(1, Math.max(0.28, Math.abs(Math.cos(flipSpin))));
          ctx.fillStyle = '#22D3EE';
          ctx.beginPath();
          ctx.roundRect(-34, -5, 68, 10, 5);
          ctx.fill();
          ctx.fillStyle = '#F43F5E';
          ctx.fillRect(-16, -4, 32, 8);
          ctx.restore();

          drawSceneStar(120, starY, 0.58, Math.sin(elapsedSec * 4.8) * 0.2, { skateCap: true });
        } else {
          // 6. STARFISH CHEF FLIPPING MUSICAL NOTES
          ctx.fillStyle = '#111827';
          ctx.fillRect(0, 0, W, 186);
          ctx.fillStyle = '#334155';
          ctx.fillRect(0, 148, W, 38);

          drawSceneStar(86, 114 + Math.sin(elapsedSec * 3.8) * 2.5, 0.58, Math.sin(elapsedSec * 2.8) * 0.08, { chefHat: true });

          // Tilting Frying Pan + Tossed Bouncing Notes
          const panTilt = Math.sin(elapsedSec * 5.6) * 0.24;
          ctx.save();
          ctx.translate(152, 132);
          ctx.rotate(panTilt);
          ctx.fillStyle = '#94A3B8';
          ctx.fillRect(-44, -3, 28, 6);
          ctx.fillStyle = '#1E293B';
          ctx.beginPath();
          ctx.roundRect(-16, -7, 56, 15, 6);
          ctx.fill();
          ctx.strokeStyle = '#CBD5E1';
          ctx.lineWidth = 2;
          ctx.stroke();
          ctx.restore();

          const hop1 = Math.abs(Math.sin(elapsedSec * 4.8)) * 46;
          const hop2 = Math.abs(Math.cos(elapsedSec * 4.2)) * 38;
          ctx.fillStyle = '#FBBF24';
          ctx.font = '800 18px "Plus Jakarta Sans", sans-serif';
          ctx.fillText('♪', 148, 118 - hop1);
          ctx.fillStyle = '#34D399';
          ctx.fillText('♫', 170, 116 - hop2);
        }

        // OLED Boot Footer Plate (y: 186..240)
        ctx.fillStyle = '#090D16';
        ctx.fillRect(0, 186, W, H - 186);
        ctx.strokeStyle = 'rgba(34, 211, 238, 0.35)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, 186);
        ctx.lineTo(W, 186);
        ctx.stroke();

        ctx.textAlign = 'center';
        ctx.fillStyle = '#22D3EE';
        ctx.font = '800 12.5px "JetBrains Mono", monospace';
        ctx.fillText(`STARFISH FM-1 · ${act.title}`, W / 2, 204, W - 16);

        ctx.fillStyle = '#CBD5E1';
        ctx.font = '600 10px "JetBrains Mono", monospace';
        ctx.fillText(`♫ ${jingle.name} · ${activeBoot.jinglePreset.name.slice(0, 13)}`, W / 2, 221, W - 16);

        ctx.fillStyle = '#1E293B';
        ctx.fillRect(20, 230, W - 40, 4);
        ctx.fillStyle = '#22D3EE';
        ctx.fillRect(20, 230, (W - 40) * progress, 4);
        ctx.textAlign = 'left';
        return;
      } else if (activeBoot && activeBoot.until <= now) {
        bootSplashRef.current = null;
      }

      const bankCode = getPresetBankCode(patch, activeBank);

      // 1. Check Full-Screen Confirmation Prompt & Bank / Preset Selector Overlays (No border overlapping Starfish!)
      const activeHud = hudRef.current && hudRef.current.expiresAt > now ? hudRef.current : null;
      if (hudRef.current && hudRef.current.expiresAt <= now) {
        hudRef.current = null;
      }

      if (confirmPrompt) {
        ctx.fillStyle = '#090D16';
        ctx.fillRect(0, 0, W, H);

        // Top banner stops safely left of the top-right Starfish
        ctx.fillStyle = '#0D131F';
        ctx.fillRect(0, 0, W, 34);
        ctx.fillStyle =
          confirmPrompt.type === 'save'
            ? '#34D399'
            : confirmPrompt.type === 'randomResolve'
            ? '#F97316'
            : '#FBBF24';
        ctx.font = '700 12px "JetBrains Mono", monospace';
        const bannerTxt =
          confirmPrompt.type === 'save'
            ? 'MEMORY WRITE'
            : confirmPrompt.type === 'randomResolve'
            ? 'UNSAVED RANDOM'
            : confirmPrompt.type === 'clearLoop'
            ? 'LOOP MEMORY'
            : 'SLOT RESET';
        ctx.fillText(bannerTxt, 10, 21.5, W - 56);
        drawMiniStarfish(now, rms);

        ctx.textAlign = 'center';
        const isFactory = confirmPrompt.type === 'reset' && confirmPrompt.mode === 'factory';
        const headline =
          confirmPrompt.type === 'save'
            ? 'SAVE PATCH?'
            : confirmPrompt.type === 'randomResolve'
            ? 'SAVE BEFORE LEAVE?'
            : confirmPrompt.type === 'clearLoop'
            ? 'CLEAR SYNTH LOOP?'
            : isFactory
            ? 'FACTORY RESET?'
            : 'LOAD VIRUS INIT?';

        ctx.fillStyle = '#F8FAFC';
        ctx.font = '800 19px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(headline, W / 2, 70, W - 20);

        ctx.fillStyle = '#22D3EE';
        ctx.font = '700 12.5px "JetBrains Mono", monospace';
        if (confirmPrompt.type === 'clearLoop') {
          ctx.fillText(`${synthLoopCount} RECORDED NOTES`, W / 2, 100, W - 24);
          ctx.fillStyle = '#94A3B8';
          ctx.font = '600 10.5px "JetBrains Mono", monospace';
          ctx.fillText('WIPE OVERDUB LOOP TRACK', W / 2, 128, W - 24);
        } else if (confirmPrompt.type === 'randomResolve') {
          const slotCode = `X${confirmPrompt.bankXSlot.toString().padStart(3, '0')}`;
          const existing = getBankPresets('X')[confirmPrompt.bankXSlot];
          const slotState = existing?.isUserPreset ? `USED: ${existing.name.slice(0, 10)}` : 'EMPTY SLOT';
          ctx.fillText(`${patch.name.slice(0, 14)} -> ${slotCode}`, W / 2, 96, W - 20);
          ctx.fillStyle = existing?.isUserPreset ? '#F97316' : '#34D399';
          ctx.font = '700 10px "JetBrains Mono", monospace';
          ctx.fillText(`KNOBS/SEL: PICK ${slotCode} (${slotState})`, W / 2, 112, W - 20);

          // SEL: Discard & Leave button cue
          ctx.fillStyle = 'rgba(249, 115, 22, 0.16)';
          ctx.fillRect(16, 120, W - 32, 26);
          ctx.strokeStyle = '#F97316';
          ctx.lineWidth = 1.4;
          ctx.strokeRect(16, 120, W - 32, 26);
          ctx.fillStyle = '#FDBA74';
          ctx.font = '700 11px "JetBrains Mono", monospace';
          ctx.fillText('SEL : DISCARD & LEAVE', W / 2, 136.5, W - 40);
        } else if (confirmPrompt.type === 'save') {
          const slotCode = `X${confirmPrompt.bankXSlot.toString().padStart(3, '0')}`;
          const existing = getBankPresets('X')[confirmPrompt.bankXSlot];
          const slotState = existing?.isUserPreset ? `OVERWRITE: ${existing.name.slice(0, 9)}` : 'EMPTY SLOT';
          const isRandPatch = patch.id.startsWith('rand-');
          ctx.fillText(
            confirmPrompt.dest === 'bankX'
              ? `BANK X · ${slotCode} (${slotState})`
              : `SLOT ${bankCode} · ${patch.name.slice(0, 12)}`,
            W / 2,
            98,
            W - 20
          );
          ctx.fillStyle = '#141B29';
          ctx.fillRect(16, 118, W - 32, 24);
          ctx.fillStyle = '#FBBF24';
          ctx.font = '700 10px "JetBrains Mono", monospace';
          ctx.fillText(
            confirmPrompt.dest === 'bankX'
              ? isRandPatch || activeBank === 'X'
                ? `KNOBS/SELECT: PICK SLOT (${slotCode})`
                : `KNOBS: PICK ${slotCode} · SEL: ${bankCode}`
              : `SEL: SAVE TO BANK X (${slotCode})`,
            W / 2,
            133.5,
            W - 36
          );
        } else {
          ctx.fillText(`${bankCode} · ${patch.name.slice(0, 14)}`, W / 2, 100, W - 24);

          if (confirmPrompt.type === 'reset') {
            ctx.fillStyle = '#141B29';
            ctx.fillRect(24, 118, W - 48, 22);
            ctx.fillStyle = '#94A3B8';
            ctx.font = '700 10.5px "JetBrains Mono", monospace';
            const rstLabel = isFactory ? (activeBank === 'X' ? 'CLEAR USER SLOT' : 'FACTORY PRESET') : 'VIRUS INIT PATCH';
            ctx.fillText(`SEL: ${rstLabel}`, W / 2, 132.5, W - 56);
          }
        }

        // OCT+ YES button cue
        ctx.fillStyle = 'rgba(16, 185, 129, 0.18)';
        ctx.fillRect(16, 158, W - 32, 30);
        ctx.strokeStyle = '#10B981';
        ctx.lineWidth = 1.5;
        ctx.strokeRect(16, 158, W - 32, 30);
        ctx.fillStyle = '#34D399';
        ctx.font = '800 12px "JetBrains Mono", monospace';
        ctx.fillText(
          confirmPrompt.type === 'randomResolve'
            ? `OCT+ : SAVE X${confirmPrompt.bankXSlot.toString().padStart(3, '0')} & GO`
            : 'OCT+ : YES · CONFIRM',
          W / 2,
          177.5,
          W - 40
        );

        // OCT- CANCEL button cue
        ctx.fillStyle = 'rgba(239, 68, 68, 0.14)';
        ctx.fillRect(16, 196, W - 32, 28);
        ctx.strokeStyle = '#EF4444';
        ctx.lineWidth = 1.5;
        ctx.strokeRect(16, 196, W - 32, 28);
        ctx.fillStyle = '#FCA5A5';
        ctx.font = '700 12px "JetBrains Mono", monospace';
        ctx.fillText(
          confirmPrompt.type === 'randomResolve' ? 'OCT- : CANCEL · STAY' : 'OCT- : CANCEL',
          W / 2,
          214,
          W - 44
        );

        ctx.textAlign = 'left';
        return;
      }

      if (activeHud?.kind === 'bank') {
        ctx.fillStyle = '#0B111C';
        ctx.fillRect(0, 0, W, H);

        ctx.fillStyle = '#94A3B8';
        ctx.font = '700 13px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText('PRESET BANK', W / 2 - 12, 34, W - 64);

        ctx.fillStyle = '#22D3EE';
        ctx.font = '800 44px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(`BANK ${activeHud.bankId}`, W / 2, 108, W - 24);

        ctx.fillStyle = '#F8FAFC';
        ctx.font = '700 13px "JetBrains Mono", monospace';
        ctx.fillText(activeHud.subtitle, W / 2, 142, W - 24);

        const cellW = (W - 24) / VIRUS_BANKS.length;
        VIRUS_BANKS.forEach((b, idx) => {
          const bx = 12 + idx * cellW;
          const isSel = b.id === activeHud.bankId;
          ctx.fillStyle = isSel ? '#22D3EE' : '#161D2B';
          ctx.fillRect(bx + 1.5, 178, cellW - 3, 30);
          ctx.fillStyle = isSel ? '#07090E' : '#94A3B8';
          ctx.font = '700 13px "JetBrains Mono", monospace';
          ctx.fillText(b.id, bx + cellW / 2, 198, cellW - 4);
        });
        ctx.textAlign = 'left';
        drawMiniStarfish(now, rms);
        return;
      }

      if (activeHud?.kind === 'preset') {
        ctx.fillStyle = '#0B111C';
        ctx.fillRect(0, 0, W, H);

        ctx.textAlign = 'center';
        ctx.fillStyle = '#FBBF24';
        ctx.font = '700 28px "JetBrains Mono", monospace';
        ctx.fillText(activeHud.code, W / 2 - 12, 46, W - 64);

        // Neighbor previous preset
        if (activeHud.prevName) {
          ctx.fillStyle = '#475569';
          ctx.font = '600 13px "Plus Jakarta Sans", sans-serif';
          ctx.fillText(activeHud.prevName.slice(0, 18), W / 2, 82, W - 28);
        }

        // Active preset highlight pill
        ctx.fillStyle = 'rgba(34, 211, 238, 0.14)';
        ctx.fillRect(14, 94, W - 28, 40);
        ctx.fillStyle = '#F8FAFC';
        ctx.font = '800 21px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(activeHud.name.slice(0, 16), W / 2, 121, W - 36);

        // Neighbor next preset
        if (activeHud.nextName) {
          ctx.fillStyle = '#475569';
          ctx.font = '600 13px "Plus Jakarta Sans", sans-serif';
          ctx.fillText(activeHud.nextName.slice(0, 18), W / 2, 154, W - 28);
        }

        ctx.fillStyle = '#22D3EE';
        ctx.font = '700 11.5px "JetBrains Mono", monospace';
        ctx.fillText(activeHud.meta, W / 2, 186, W - 24);

        ctx.fillStyle = '#64748B';
        ctx.font = '600 11px "JetBrains Mono", monospace';
        ctx.fillText(
          activeBank === 'X'
            ? `BANK X · ${getBankXCount()}/128 USER SAVED`
            : `BANK ${activeBank} · ${bankPresets.length} SOUNDS`,
          W / 2,
          214,
          W - 24
        );
        ctx.textAlign = 'left';
        drawMiniStarfish(now, rms);
        return;
      }

      if (activeHud?.kind === 'drumkit') {
        ctx.fillStyle = '#0B111C';
        ctx.fillRect(0, 0, W, H);

        ctx.textAlign = 'center';
        ctx.fillStyle = '#34D399';
        ctx.font = '700 24px "JetBrains Mono", monospace';
        ctx.fillText(`KIT ${activeHud.index + 1}/${DRUM_KITS.length}`, W / 2 - 12, 46, W - 64);

        if (activeHud.prevName) {
          ctx.fillStyle = '#475569';
          ctx.font = '600 13px "Plus Jakarta Sans", sans-serif';
          ctx.fillText(activeHud.prevName, W / 2, 82, W - 28);
        }

        ctx.fillStyle = 'rgba(52, 211, 153, 0.16)';
        ctx.fillRect(14, 94, W - 28, 40);
        ctx.fillStyle = '#F8FAFC';
        ctx.font = '800 20px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(activeHud.name, W / 2, 121, W - 36);

        if (activeHud.nextName) {
          ctx.fillStyle = '#475569';
          ctx.font = '600 13px "Plus Jakarta Sans", sans-serif';
          ctx.fillText(activeHud.nextName, W / 2, 154, W - 28);
        }

        ctx.fillStyle = '#22D3EE';
        ctx.font = '700 11.5px "JetBrains Mono", monospace';
        ctx.fillText(activeHud.subtitle, W / 2, 186, W - 24);

        ctx.fillStyle = '#64748B';
        ctx.font = '600 11px "JetBrains Mono", monospace';
        ctx.fillText(`DRUM MACHINE · 8 VOICES`, W / 2, 214, W - 24);
        ctx.textAlign = 'left';
        drawMiniStarfish(now, rms);
        return;
      }

      // 2. Main Top Bar (y: 0..34): Slot/Kit Code + Preset/Kit Name + Status Pill + BPM + Top-Right Mascot!
      const curKit = DRUM_KITS[drumKitIndex] || DRUM_KITS[0];
      const curBus = DRUM_BUS_MODES[drumBusMode] || DRUM_BUS_MODES[0];
      const curDrum = drumTracks[selectedDrumTrack] || drumTracks[0];
      const curDrumStep = curDrum?.steps[selectedStepIdx];

      ctx.fillStyle = '#0D131F';
      ctx.fillRect(0, 0, W, 34);

      const codeStr = activePage === 'SEQ' ? `K${drumKitIndex + 1}` : bankCode;
      ctx.fillStyle = activePage === 'SEQ' ? '#34D399' : '#22D3EE';
      ctx.font = '700 13px "JetBrains Mono", monospace';
      ctx.fillText(codeStr, 7, 21.5, 34);

      const hasStatusPill =
        (muteKeyHeld && activePage === 'SEQ') ||
        isRecording ||
        (patch.arpEnabled && activePage !== 'SEQ') ||
        synthLoopCount > 0;

      // Preset Name or Drum Kit Name moved into the Top Bar!
      const topBarTitle = activePage === 'SEQ' ? curKit.id : patch.name;
      ctx.fillStyle = '#F8FAFC';
      ctx.font = '700 12.5px "Plus Jakarta Sans", sans-serif';
      ctx.fillText(topBarTitle, 44, 21.5, hasStatusPill ? 78 : 114);

      // Status pill (MUTE / REC / ARP / LOOP)
      if (muteKeyHeld && activePage === 'SEQ') {
        ctx.fillStyle = '#EF4444';
        ctx.fillRect(125, 7, 29, 20);
        ctx.fillStyle = '#FFFFFF';
        ctx.font = '700 9px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText('MUTE', 139.5, 20.5, 27);
        ctx.textAlign = 'left';
      } else if (isRecording) {
        ctx.fillStyle = '#EF4444';
        ctx.fillRect(125, 7, 29, 20);
        ctx.fillStyle = '#FFFFFF';
        ctx.font = '700 9.5px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText('REC', 139.5, 20.5, 27);
        ctx.textAlign = 'left';
      } else if (patch.arpEnabled && activePage !== 'SEQ') {
        ctx.fillStyle = patch.arpLatch ? '#FBBF24' : '#10B981';
        ctx.fillRect(125, 7, 29, 20);
        ctx.fillStyle = '#07090E';
        ctx.font = '700 9px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText(synthLoopCount > 0 ? 'A+LP' : patch.arpLatch ? 'HLD' : 'ARP', 139.5, 20.5, 27);
        ctx.textAlign = 'left';
      } else if (synthLoopCount > 0) {
        ctx.fillStyle = '#10B981';
        ctx.fillRect(125, 7, 29, 20);
        ctx.fillStyle = '#07090E';
        ctx.font = '700 9px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText('LOOP', 139.5, 20.5, 27);
        ctx.textAlign = 'left';
      }

      // Shared Master Clock BPM left of Top-Right Starfish / Drummer Boy
      ctx.fillStyle = isPlayingSeq ? '#34D399' : '#94A3B8';
      ctx.font = '700 10.5px "JetBrains Mono", monospace';
      ctx.textAlign = 'right';
      ctx.fillText(`${bpm}B`, W - 40, 21.5, 42);
      ctx.textAlign = 'left';

      // Mini Cute Dancing Starfish / Drum Mode Drummer Boy in OLED Top-Right Corner!
      drawMiniStarfish(now, rms);

      // 3. Main Page Strip (y: 36..82): Active Page Concerned + Pagination LEDs OR Hold Progress / Badge
      const oscBadgeMap: Record<string, string> = {
        hypersaw: 'HYPERSAW',
        saw: 'SAW',
        pulse: 'PWM',
        wavetable: 'WAVETABL',
        sync: 'SYNC',
        triangle: 'TRIANGLE'
      };
      const routeShort =
        patch.filterRouting === 'split'
          ? 'SPL'
          : patch.filterRouting === 'parallel'
          ? 'PAR'
          : 'SER';
      const f1Short = patch.filterType === 'bandpass' ? 'BP' : patch.filterType === 'highpass' ? 'HP' : patch.filterType === 'notch' ? 'BS' : 'LP';
      const oscLabel = oscBadgeMap[patch.oscMode || 'hypersaw'] || 'HYPERSAW';
      const catShort = patch.category.toUpperCase().split('/')[0].trim().slice(0, 6);
      const scaleTag = scaleMode !== 'chromatic' ? `${NOTE_NAMES_12[rootPc]} ${SCALE_LABELS[scaleMode].slice(0, 3).toUpperCase()}` : catShort;

      const totalPages = PAGE_TOTALS[activePage] || 1;
      const curPageNum = subPage + 1;

      if (saveHoldStart !== null) {
        const elapsed = now - saveHoldStart;
        const holdRatio = Math.max(0, Math.min(1, elapsed / 1500));
        ctx.fillStyle = 'rgba(251, 191, 36, 0.12)';
        ctx.fillRect(8, 38, W - 16, 42);
        ctx.fillStyle = '#FBBF24';
        ctx.font = '800 13px "JetBrains Mono", monospace';
        ctx.fillText('HOLD SAVE FOR RESET...', 14, 57, W - 28);
        ctx.fillStyle = '#1E293B';
        ctx.fillRect(14, 66, W - 28, 7);
        ctx.fillStyle = '#FBBF24';
        ctx.fillRect(14, 66, (W - 28) * holdRatio, 7);
      } else if (recHoldStart !== null) {
        const elapsed = now - recHoldStart;
        const holdRatio = Math.max(0, Math.min(1, elapsed / 1500));
        ctx.fillStyle = 'rgba(239, 68, 68, 0.14)';
        ctx.fillRect(8, 38, W - 16, 42);
        ctx.fillStyle = '#FCA5A5';
        ctx.font = '800 13px "JetBrains Mono", monospace';
        ctx.fillText('HOLD REC TO CLR LOOP...', 14, 57, W - 28);
        ctx.fillStyle = '#1E293B';
        ctx.fillRect(14, 66, W - 28, 7);
        ctx.fillStyle = '#EF4444';
        ctx.fillRect(14, 66, (W - 28) * holdRatio, 7);
      } else if (drumClearHold !== null && now - drumClearHold.start >= 200) {
        const elapsed = now - drumClearHold.start;
        const holdRatio = Math.max(0, Math.min(1, elapsed / 1500));
        ctx.fillStyle = 'rgba(249, 115, 22, 0.14)';
        ctx.fillRect(8, 38, W - 16, 42);
        ctx.fillStyle = '#FBBF24';
        ctx.font = '800 13px "JetBrains Mono", monospace';
        ctx.fillText(`HOLD TO CLR ${drumClearHold.label}...`, 14, 57, W - 28);
        ctx.fillStyle = '#1E293B';
        ctx.fillRect(14, 66, W - 28, 7);
        ctx.fillStyle = '#F97316';
        ctx.fillRect(14, 66, (W - 28) * holdRatio, 7);
      } else if (activeHud?.kind === 'badge') {
        ctx.fillStyle = '#111826';
        ctx.fillRect(8, 38, W - 16, 42);
        ctx.fillStyle = activeHud.accent;
        ctx.fillRect(8, 38, 4, 42);
        ctx.font = '800 17px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(activeHud.title, 18, 57, W - 30);
        ctx.fillStyle = '#CBD5E1';
        ctx.font = '600 10.5px "JetBrains Mono", monospace';
        ctx.fillText(activeHud.subtitle, 18, 74, W - 30);
      } else {
        const recBars = Math.max(1, Math.min(4, Math.ceil(seqLength / 16)));
        let pageTitle = 'PERFORMANCE';
        let pageSub =
          isRecording || synthLoopCount > 0
            ? `${isRecording ? 'REC' : `${synthLoopCount}N`} ${recBars}B(${seqLength}ST) · ${oscLabel} · ${f1Short}`
            : `${scaleTag} · ${oscLabel} · ${f1Short} ${routeShort}`;
        let pageAccent = '#22D3EE';

        if (activePage === 'FX') {
          pageTitle = subPage === 1 ? 'DLY MIX & SAT (2/2)' : 'FX RACK MIX (1/2)';
          pageSub = subPage === 1 ? 'DLY BAL · FEEDBACK · PHASER · SAT' : 'CHORUS · PHASER · DELAY · REVERB';
        } else if (activePage === 'SEL') {
          pageTitle =
            subPage === 2
              ? 'FILTER ENV (3/3)'
              : subPage === 1
              ? 'FILTER TONE (2/3)'
              : 'FILTER MODE (1/3)';
          pageSub =
            subPage === 2
              ? 'F-BAL · F-ENV AMT · F-REL · KEYFLW'
              : subPage === 1
              ? `F1 ${Math.round(patch.filterCutoff)}Hz Q${patch.filterReso.toFixed(1)} · F2 ${Math.round(patch.filter2Cutoff ?? 9500)}Hz`
              : `${f1Short} ${routeShort} · SEL: NEXT · HOME: EXIT`;
          if (subPage === 2) pageAccent = '#FBBF24';
        } else if (activePage === 'ENV') {
          pageTitle = subPage === 1 ? 'FILTER ENV (2/2)' : 'AMP ENV (1/2)';
          pageSub = subPage === 1 ? 'ENV: HOME · F-ATK/DEC/SUS/AMT' : 'ENV: FLT ENV · A/D/S/R ENVELOPE';
          if (subPage === 1) pageAccent = '#FBBF24';
        } else if (activePage === 'LFO') {
          pageTitle =
            subPage === 2
              ? 'SWARM & GLIDE (3/3)'
              : subPage === 1
              ? 'LFO 2 MATRIX (2/3)'
              : 'LFO 1 MATRIX (1/3)';
          const dKey = subPage === 1 ? (patch.lfo2Dest || 'pan') : (patch.lfoDest || 'filter');
          pageSub =
            subPage === 2
              ? 'ANALOG DRIFT · PORTAMENTO · KEYFLW'
              : `DEST: ${LFO_DEST_LABELS[dKey] || dKey.toUpperCase()} · LFO: NEXT PAGE`;
          pageAccent = subPage === 1 ? '#A78BFA' : '#34D399';
        } else if (activePage === 'EDIT') {
          pageTitle =
            subPage === 2
              ? 'SYNC / FM / DRV (3/3)'
              : subPage === 1
              ? 'SUB / NOISE / RNG (2/3)'
              : 'OSC & UNISON (1/3)';
          pageSub =
            subPage === 2
              ? 'HARD SYNC · FM MOD · UNI PAN · DRIVE'
              : subPage === 1
              ? 'SUB LEVEL · SUB WAVE · NOISE · RING'
              : 'OSC MIX · DETUNE · UNISON VOICES';
        } else if (activePage === 'ARP') {
          pageTitle = subPage === 1 ? 'ARP GROOVE (2/2)' : 'ARP SETUP (1/2)';
          pageSub =
            subPage === 1
              ? `OCT · GATE · SWING · HOLD ARP:PG 1/2`
              : `ARP:ON/OFF · HOLD ARP:PG 2/2 · +OCT±:LCH`;
          pageAccent = patch.arpLatch ? '#FBBF24' : '#34D399';
        } else if (activePage === 'GLO') {
          pageTitle =
            subPage === 2
              ? 'RANDOMIZER (3/3)'
              : subPage === 1
              ? 'VOICE & SYSTEM (2/3)'
              : 'SCALE & CHORD (1/3)';
          pageSub =
            subPage === 2
              ? `K2:GEN SAFE · K4:SLOT X${targetBankXSlot.toString().padStart(3, '0')} · SAVE`
              : subPage === 1
              ? `REC ${recBars}B (${seqLength}ST) · ${patch.polyphony || 8}V · SWING ${swing}%`
              : `${NOTE_NAMES_12[rootPc]} ${SCALE_LABELS[scaleMode].toUpperCase()} · ${CHORD_LABELS[chordMode].toUpperCase()} · ${bpm}B`;
          if (subPage === 2) pageAccent = '#FBBF24';
        } else if (activePage === 'SEQ') {
          pageTitle =
            subPage === 2
              ? 'KIT GLOBAL (3/3)'
              : subPage === 1
              ? 'EUCLID & GRV (2/3)'
              : 'DRUM VOICE (1/3)';
          const stepFlag = curDrum?.mute ? 'MUTED' : curDrumStep?.active ? (curDrumStep.accent ? 'ACC' : 'ON') : 'OFF';
          pageSub =
            subPage === 2
              ? `KIT-WIDE MASTER MIX · ${curBus.id}`
              : `${curDrum?.shortName || 'BD'} ST#${selectedStepIdx + 1}/${seqLength}:${stepFlag} · ${curBus.id}`;
          pageAccent = muteKeyHeld ? '#EF4444' : '#34D399';
        }

        const ledsSpace = totalPages > 1 ? totalPages * 10 + 12 : 0;
        ctx.fillStyle = '#F8FAFC';
        ctx.font = '800 18px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(pageTitle, 10, 58, W - 20 - ledsSpace);

        // Micro-LED pagination dots inside OLED main strip
        if (totalPages > 1) {
          const ledSpacing = 10;
          const rightAnchor = W - 12;
          for (let p = 0; p < totalPages; p++) {
            const isCur = p + 1 === curPageNum;
            const lx = rightAnchor - (totalPages - 1 - p) * ledSpacing;
            ctx.beginPath();
            ctx.arc(lx, 52.5, isCur ? 3.2 : 2.1, 0, Math.PI * 2);
            ctx.fillStyle = isCur ? pageAccent : '#334155';
            ctx.fill();
          }
        }

        ctx.fillStyle = activePage === 'SEQ' ? pageAccent : activePage === 'ARP' ? '#34D399' : '#94A3B8';
        ctx.font = '600 10.5px "JetBrains Mono", monospace';
        ctx.fillText(pageSub, 10, 77, W - 20);
      }

      // Hairline divider
      ctx.strokeStyle = 'rgba(148, 163, 184, 0.16)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, 84);
      ctx.lineTo(W, 84);
      ctx.stroke();

      // 4. Center Graphic Stage (y: 86..162) - ALWAYS LIVE & VISIBLE even while turning knobs!
      if (saveFlashUntil > now) {
        ctx.fillStyle = 'rgba(16, 185, 129, 0.16)';
        ctx.fillRect(12, 92, W - 24, 62);
        ctx.strokeStyle = '#34D399';
        ctx.lineWidth = 2;
        ctx.strokeRect(12, 92, W - 24, 62);
        ctx.textAlign = 'center';
        ctx.fillStyle = '#34D399';
        ctx.font = '800 19px "Plus Jakarta Sans", sans-serif';
        ctx.fillText(saveBannerTitle, W / 2, 122, W - 36);
        ctx.fillStyle = '#E2E8F0';
        ctx.font = '600 12px "JetBrains Mono", monospace';
        ctx.fillText(`${bankCode} ${patch.name}`, W / 2, 142, W - 36);
        ctx.textAlign = 'left';
      } else if (activePage === 'ENV') {
        // Full-Width ADSR Envelope Graphic spanning x = 10 .. W - 10 (220px wide)
        const x0 = 10;
        const xEnd = W - 10;
        const totalW = xEnd - x0; // 220px
        const yBot = 156;
        const yTop = 98;

        const computeAdsrCoords = (atk: number, dec: number, sus: number, rel: number) => {
          const wA = Math.sqrt(Math.max(0.004, atk) / 2.0) * 52 + 8;
          const wD = Math.sqrt(Math.max(0.02, dec) / 3.0) * 66 + 14;
          const wR = Math.sqrt(Math.max(0.02, rel) / 3.0) * 66 + 14;
          const sumADR = wA + wD + wR;
          const holdMin = 38;
          const scale = sumADR + holdMin > totalW ? (totalW - holdMin) / sumADR : 1;
          const xA = x0 + wA * scale;
          const xD = xA + wD * scale;
          const xRStart = xEnd - wR * scale;
          const yS = yBot - (yBot - yTop) * Math.max(0.05, Math.min(1, sus));
          return { xA, xD, xRStart, yS };
        };

        const amp = computeAdsrCoords(patch.attack ?? 0.008, patch.decay ?? 0.85, patch.sustain ?? 0.75, patch.release ?? 0.35);
        const flt = computeAdsrCoords(patch.filterAttack ?? 0.008, patch.filterDecay ?? 0.42, patch.filterSustain ?? 0.35, patch.filterRelease ?? 0.35);
        const isFltEdit = subPage === 1;

        // Clear Envelope Mode Badge inside graphic stage (top-left)
        ctx.fillStyle = isFltEdit ? 'rgba(251, 191, 36, 0.16)' : 'rgba(34, 211, 238, 0.16)';
        ctx.fillRect(10, 87, 108, 13);
        ctx.fillStyle = isFltEdit ? '#FBBF24' : '#22D3EE';
        ctx.font = '800 9px "JetBrains Mono", monospace';
        ctx.fillText(isFltEdit ? 'FILTER ADSR (2/2)' : 'AMP ADSR (1/2)', 14, 96.5);

        ctx.fillStyle = '#64748B';
        ctx.font = '700 9px "JetBrains Mono", monospace';
        ctx.textAlign = 'right';
        ctx.fillText(isFltEdit ? `ENV ${patch.filterEnvAmount ?? 45}%` : `REL ${(patch.release ?? 0.35).toFixed(2)}s`, xEnd, 96.5);
        ctx.textAlign = 'left';

        // Subtle grid markers
        ctx.strokeStyle = 'rgba(148, 163, 184, 0.14)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(x0, yBot);
        ctx.lineTo(xEnd, yBot);
        ctx.moveTo(x0, (yTop + yBot) / 2);
        ctx.lineTo(xEnd, (yTop + yBot) / 2);
        ctx.stroke();

        // Draw inactive reference curve behind active curve
        const drawCurve = (c: { xA: number; xD: number; xRStart: number; yS: number }, strokeCol: string, fillCol: string, lw: number, topOffset = 0) => {
          ctx.fillStyle = fillCol;
          ctx.beginPath();
          ctx.moveTo(x0, yBot);
          ctx.lineTo(c.xA, yTop + topOffset);
          ctx.lineTo(c.xD, c.yS);
          ctx.lineTo(c.xRStart, c.yS);
          ctx.lineTo(xEnd, yBot);
          ctx.closePath();
          ctx.fill();

          ctx.strokeStyle = strokeCol;
          ctx.lineWidth = lw;
          ctx.beginPath();
          ctx.moveTo(x0, yBot);
          ctx.lineTo(c.xA, yTop + topOffset);
          ctx.lineTo(c.xD, c.yS);
          ctx.lineTo(c.xRStart, c.yS);
          ctx.lineTo(xEnd, yBot);
          ctx.stroke();
        };

        if (isFltEdit) {
          drawCurve(amp, 'rgba(34, 211, 238, 0.35)', 'rgba(34, 211, 238, 0.04)', 1.3, 0);
          drawCurve(flt, '#FBBF24', 'rgba(251, 191, 36, 0.18)', 2.8, 4);
        } else {
          drawCurve(flt, 'rgba(251, 191, 36, 0.4)', 'rgba(251, 191, 36, 0.04)', 1.3, 4);
          drawCurve(amp, '#22D3EE', 'rgba(34, 211, 238, 0.18)', 2.8, 0);
        }

        // Breakpoint Nodes + Live Knob-Turn Segment Highlight
        const activeCurve = isFltEdit ? flt : amp;
        const topY = isFltEdit ? yTop + 4 : yTop;
        const activePts: Array<{ seg: 'A' | 'D' | 'S' | 'R'; x: number; y: number }> = [
          { seg: 'A', x: activeCurve.xA, y: topY },
          { seg: 'D', x: activeCurve.xD, y: activeCurve.yS },
          { seg: 'S', x: (activeCurve.xD + activeCurve.xRStart) / 2, y: activeCurve.yS },
          { seg: 'R', x: activeCurve.xRStart, y: activeCurve.yS }
        ];
        const liveSeg = lastEnvParamRef.current && lastEnvParamRef.current.expiresAt > now ? lastEnvParamRef.current.segment : null;
        const nodeCol = isFltEdit ? '#FBBF24' : '#22D3EE';
        activePts.forEach((pt) => {
          const isHot = liveSeg === pt.seg;
          if (isHot) {
            ctx.beginPath();
            ctx.arc(pt.x, pt.y, 6.2, 0, Math.PI * 2);
            ctx.strokeStyle = '#FFFFFF';
            ctx.lineWidth = 1.6;
            ctx.stroke();
          }
          ctx.fillStyle = isHot ? '#FFFFFF' : nodeCol;
          ctx.beginPath();
          ctx.arc(pt.x, pt.y, isHot ? 3.8 : 2.8, 0, Math.PI * 2);
          ctx.fill();
        });
      } else if (activePage === 'ARP') {
        const arpStateCol = patch.arpLatch ? '#FBBF24' : patch.arpEnabled ? '#34D399' : '#64748B';
        ctx.fillStyle = 'rgba(52, 211, 153, 0.12)';
        ctx.fillRect(10, 88, 66, 18);
        ctx.fillStyle = arpStateCol;
        ctx.font = '800 10.5px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText(patch.arpLatch ? 'LATCH' : patch.arpEnabled ? 'ARP ON' : 'ARP OFF', 43, 100.5, 62);
        ctx.textAlign = 'left';

        ctx.fillStyle = '#E2E8F0';
        ctx.font = '700 10.5px "JetBrains Mono", monospace';
        ctx.fillText(`${(patch.arpMode || 'updown').toUpperCase()} · ${patch.arpRate || '1/16'} · ${patch.arpOctaves || 1}OCT`, 84, 100.5, W - 94);

        // Animated Arpeggiator Pattern Step Bars
        const mode = patch.arpMode || 'updown';
        const octs = Math.max(1, Math.min(3, patch.arpOctaves || 1));
        const heights =
          mode === 'down'
            ? [1.0, 0.85, 0.7, 0.55, 0.4, 0.28, 0.18, 0.12]
            : mode === 'updown'
            ? [0.2, 0.45, 0.7, 0.95, 0.8, 0.55, 0.35, 0.2]
            : mode === 'random'
            ? [0.6, 0.25, 0.9, 0.4, 0.8, 0.18, 0.7, 0.5]
            : [0.18, 0.3, 0.44, 0.58, 0.72, 0.84, 0.95, 1.0];
        const beatStep = Math.floor((now / (60000 / Math.max(40, bpm) / 4)) % 8);
        const gateScale = ((patch.arpGate ?? 72) - 20) / 75;
        heights.forEach((hNorm, i) => {
          const bx = 14 + i * 27;
          const bh = Math.round(8 + hNorm * (24 + octs * 4));
          const by = 154 - bh;
          const isBeat = patch.arpEnabled && i === beatStep;
          ctx.fillStyle = isBeat ? '#FBBF24' : patch.arpEnabled ? '#22D3EE' : '#1E293B';
          ctx.fillRect(bx, by, 18 * (0.55 + gateScale * 0.45), bh);
        });
      } else if (activePage === 'LFO') {
        const isLfo2 = subPage === 1;
        const isSwarm = subPage === 2;
        const lfoRate = isSwarm ? (patch.swarmRate ?? 0.38) : isLfo2 ? (patch.lfo2Rate ?? 0.9) : (patch.lfoRate ?? 4.5);
        const lfoDepth = (isSwarm ? (patch.swarmDepth ?? 22) : isLfo2 ? (patch.lfo2Depth ?? 12) : (patch.lfoDepth ?? 18)) / 100;
        const shape = isSwarm ? 'sine' : (isLfo2 ? patch.lfo2Shape : patch.lfoShape) || (isLfo2 ? 'triangle' : 'sine');

        ctx.strokeStyle = 'rgba(148, 163, 184, 0.25)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(10, 124);
        ctx.lineTo(W - 10, 124);
        ctx.stroke();

        ctx.strokeStyle = isLfo2 ? '#A78BFA' : isSwarm ? '#22D3EE' : '#34D399';
        ctx.lineWidth = 2.5;
        ctx.beginPath();
        for (let x = 10; x < W - 10; x++) {
          const normX = (x - 10) / (W - 20);
          const ph = normX * Math.PI * 4 * Math.max(0.5, lfoRate / 4) + lfoPhase;
          let waveVal = Math.sin(ph);
          if (shape === 'square') waveVal = Math.sin(ph) >= 0 ? 1 : -1;
          else if (shape === 'saw') waveVal = 2 * ((ph / (2 * Math.PI)) % 1) - 1;
          else if (shape === 'triangle') waveVal = (2 / Math.PI) * Math.asin(Math.sin(ph));
          const y = 124 - waveVal * (24 * Math.max(0.2, lfoDepth));
          if (x === 10) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
      } else if (activePage === 'FX') {
        const dlyMixNorm = (patch.fxDelayMix ?? Math.min(72, Math.max(30, Math.round(patch.fxDelayFeedback * 0.9)))) / 100;
        const satIdx = Math.max(0, SAT_TYPES.indexOf(patch.saturationType || 'soft'));
        const fxBars =
          subPage === 1
            ? [
                { lbl: 'DMX', val: dlyMixNorm, col: '#22D3EE' },
                { lbl: 'DFB', val: patch.fxDelayFeedback / 95, col: '#FBBF24' },
                { lbl: 'PHZ', val: (patch.fxPhaserRate ?? 0.5) / 6.0, col: '#34D399' },
                { lbl: 'SAT', val: (3 - satIdx) / 3, col: '#F97316' }
              ]
            : [
                { lbl: 'CHO', val: patch.fxChorus / 100, col: '#22D3EE' },
                { lbl: 'PHS', val: (patch.fxPhaserMix ?? 0) / 100, col: '#34D399' },
                { lbl: 'DLY', val: patch.fxDelayTime / 1.0, col: '#FBBF24' },
                { lbl: 'REV', val: patch.fxReverbSize / 100, col: '#F97316' }
              ];
        fxBars.forEach((b, i) => {
          const bx = 18 + i * 54;
          ctx.fillStyle = '#141B29';
          ctx.fillRect(bx, 94, 40, 44);
          const fillH = Math.round(44 * Math.max(0, Math.min(1, b.val)));
          ctx.fillStyle = b.col;
          ctx.fillRect(bx, 94 + (44 - fillH), 40, fillH);
          ctx.fillStyle = '#CBD5E1';
          ctx.font = '700 11px "JetBrains Mono", monospace';
          ctx.textAlign = 'center';
          ctx.fillText(b.lbl, bx + 20, 154);
        });
        ctx.textAlign = 'left';
      } else if (activePage === 'SEL') {
        const route = patch.filterRouting || 'serial';
        const c1N = Math.max(0.06, Math.min(0.94, Math.log10(Math.max(60, patch.filterCutoff) / 50) / Math.log10(19000 / 50)));
        const c2N = Math.max(0.06, Math.min(0.94, Math.log10(Math.max(60, patch.filter2Cutoff ?? 9500) / 50) / Math.log10(19000 / 50)));
        const f1T = patch.filterType || 'lowpass';
        const f2T = patch.filter2Type || 'lowpass';
        const f2Short = FILTER_TYPE_LABELS[f2T] || 'LP';

        // Topology header pills
        ctx.fillStyle = 'rgba(34, 211, 238, 0.14)';
        ctx.fillRect(10, 88, 56, 17);
        ctx.fillStyle = '#22D3EE';
        ctx.font = '800 9.5px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText(`F1 ${f1Short}`, 38, 99.5);

        ctx.fillStyle = '#FBBF24';
        ctx.fillText(route === 'parallel' ? '||' : route === 'split' ? '//' : '->', 77, 99.5);

        ctx.fillStyle = 'rgba(251, 191, 36, 0.14)';
        ctx.fillRect(88, 88, 56, 17);
        ctx.fillStyle = '#FBBF24';
        ctx.fillText(`F2 ${f2Short}`, 116, 99.5);

        ctx.fillStyle = '#94A3B8';
        ctx.textAlign = 'right';
        ctx.fillText(route.toUpperCase(), W - 10, 99.5);
        ctx.textAlign = 'left';

        // Live Dual-Filter Response Graph (x = 12..W-12, y = 110..157)
        ctx.strokeStyle = 'rgba(148, 163, 184, 0.15)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(12, 156);
        ctx.lineTo(W - 12, 156);
        ctx.stroke();

        const evalResponse = (normX: number, cutN: number, qVal: number, fType: SynthPatch['filterType']) => {
          const d = normX - cutN;
          const resPeak = Math.min(1, qVal / 16) * 0.42 * Math.exp(-Math.pow(d / 0.065, 2));
          if (fType === 'highpass') {
            return Math.min(1.08, (1 / (1 + Math.exp(-d * 18))) * 0.72 + resPeak);
          }
          if (fType === 'bandpass') {
            return Math.min(1.08, Math.exp(-Math.pow(d / 0.15, 2)) * 0.74 + resPeak * 0.8);
          }
          if (fType === 'notch') {
            return Math.min(1.08, (1 - Math.exp(-Math.pow(d / 0.1, 2)) * 0.75) * 0.72);
          }
          return Math.min(1.08, (1 / (1 + Math.exp(d * 18))) * 0.72 + resPeak);
        };

        // F2 Curve
        ctx.strokeStyle = 'rgba(251, 191, 36, 0.75)';
        ctx.lineWidth = 1.8;
        ctx.beginPath();
        for (let i = 0; i <= 54; i++) {
          const nx = i / 54;
          const mag = evalResponse(nx, c2N, patch.filter2Reso ?? 1.5, f2T);
          const x = 12 + nx * (W - 24);
          const y = 155 - mag * 40;
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();

        // F1 Curve
        ctx.strokeStyle = '#22D3EE';
        ctx.lineWidth = 2.4;
        ctx.beginPath();
        for (let i = 0; i <= 54; i++) {
          const nx = i / 54;
          const mag = evalResponse(nx, c1N, patch.filterReso || 4, f1T);
          const x = 12 + nx * (W - 24);
          const y = 155 - mag * 40;
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
      } else if (activePage === 'EDIT') {
        if (subPage === 2) {
          ctx.fillStyle = 'rgba(249, 115, 22, 0.14)';
          ctx.fillRect(12, 92, 96, 26);
          ctx.fillStyle = patch.osc2Sync ? '#34D399' : '#64748B';
          ctx.font = '800 11px "JetBrains Mono", monospace';
          ctx.fillText(`SYNC: ${patch.osc2Sync ? 'ON' : 'OFF'}`, 20, 109);

          ctx.fillStyle = '#FBBF24';
          ctx.fillText(`FM MOD: ${patch.fmAmount ?? 0}%`, 120, 109);

          ctx.fillStyle = '#E2E8F0';
          ctx.font = '700 11px "JetBrains Mono", monospace';
          ctx.fillText(`UNI PAN: ${patch.unisonPanSpread ?? 75}% · DRIVE: ${patch.fxDrive}%`, 14, 144, W - 28);
        } else {
          ctx.strokeStyle = '#F97316';
          ctx.lineWidth = 2.5;
          ctx.beginPath();
          for (let x = 14; x < 146; x++) {
            const inX = ((x - 14) / 132) * 3.6 - 1.8;
            const x2 = inX * inX;
            const sat = (inX * (27 + x2)) / (27 + 9 * x2);
            const y = 124 - sat * 26;
            if (x === 14) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
          }
          ctx.stroke();

          const uni = Math.max(1, Math.min(5, patch.unisonVoices ?? 1));
          ctx.fillStyle = '#94A3B8';
          ctx.font = '700 11px "JetBrains Mono", monospace';
          ctx.fillText(`UNI ${uni}V`, 162, 108);
          for (let u = 0; u < 5; u++) {
            ctx.fillStyle = u < uni ? '#22D3EE' : '#1E293B';
            ctx.fillRect(162 + u * 13, 118, 9, 32);
          }
        }
      } else if (activePage === 'SEQ') {
        // 8-Voice Drum Machine Step Matrix (1..64 steps / BAR 1..4)
        const activeBar = isPlayingSeq && !isRecording
          ? Math.floor(playheadStep / 16)
          : Math.floor(selectedStepIdx / 16);
        const activeBarsCount = Math.max(1, Math.ceil(seqLength / 16));

        // Top 8 Voice Tabs (BD, SD, CH, OH, CP, TM, RD, RS) + Bar Selector Pill
        drumTracks.slice(0, 8).forEach((tr, ti) => {
          const tx = 8 + ti * 23;
          const isSel = ti === selectedDrumTrack;
          ctx.fillStyle = tr.mute ? (isSel ? '#EF4444' : '#450A0A') : isSel ? '#22D3EE' : '#1B2333';
          ctx.fillRect(tx, 86, 21, 15);
          ctx.fillStyle = tr.mute ? (isSel ? '#FFFFFF' : '#FCA5A5') : isSel ? '#07090E' : '#CBD5E1';
          ctx.font = '700 8.5px "JetBrains Mono", monospace';
          ctx.textAlign = 'center';
          ctx.fillText(tr.shortName, tx + 10.5, 96.5, 20);
        });
        ctx.fillStyle = '#1E293B';
        ctx.fillRect(194, 86, 38, 15);
        ctx.fillStyle = '#FBBF24';
        ctx.font = '700 9.5px "JetBrains Mono", monospace';
        ctx.textAlign = 'center';
        ctx.fillText(`B${activeBar + 1}/${activeBarsCount}`, 213, 96.5, 36);
        ctx.textAlign = 'left';

        const curTrack = drumTracks[selectedDrumTrack] || drumTracks[0];
        const cellW = 24;
        const cellH = 22;
        const barOffset = activeBar * 16;
        for (let s = 0; s < 16; s++) {
          const absIdx = barOffset + s;
          const inLength = absIdx < seqLength;
          const col = s % 8;
          const row = Math.floor(s / 8);
          const sx = 14 + col * 27;
          const sy = 106 + row * 27;
          const step = curTrack?.steps[absIdx];
          const isPlayhead = isPlayingSeq && playheadStep === absIdx;
          const isCursor = selectedStepIdx === absIdx;

          ctx.fillStyle = !inLength
            ? '#0B0F18'
            : isPlayhead
            ? '#34D399'
            : step?.active
            ? step.accent
              ? '#FBBF24'
              : '#22D3EE'
            : '#141B29';
          ctx.fillRect(sx, sy, cellW, cellH);

          if (isCursor) {
            ctx.strokeStyle = isRecording ? '#EF4444' : '#FFFFFF';
            ctx.lineWidth = 2;
            ctx.strokeRect(sx - 1, sy - 1, cellW + 2, cellH + 2);
          }

          ctx.fillStyle = !inLength ? '#1E293B' : isPlayhead || step?.active ? '#07090E' : '#64748B';
          ctx.font = '700 10.5px "JetBrains Mono", monospace';
          ctx.textAlign = 'center';
          ctx.fillText(`${absIdx + 1}`, sx + cellW / 2, sy + 14.5, cellW - 2);
        }
        ctx.textAlign = 'left';
      } else if (activePage === 'GLO') {
        const recBars = Math.max(1, Math.min(4, Math.ceil(seqLength / 16)));
        if (subPage === 2) {
          const slotCode = `X${targetBankXSlot.toString().padStart(3, '0')}`;
          const slotPatch = getBankPresets('X')[targetBankXSlot];
          const slotTag = slotPatch?.isUserPreset ? `USED (${slotPatch.name.slice(0, 8)})` : 'EMPTY';
          ctx.fillStyle = 'rgba(251, 191, 36, 0.12)';
          ctx.fillRect(10, 90, W - 20, 66);
          ctx.strokeStyle = 'rgba(251, 191, 36, 0.45)';
          ctx.lineWidth = 1.2;
          ctx.strokeRect(10, 90, W - 20, 66);
          ctx.fillStyle = '#FBBF24';
          ctx.font = '800 12.5px "JetBrains Mono", monospace';
          ctx.fillText(`STYLE: ${randStyle} · CHAOS ${randChaos}%`, 18, 110, W - 36);
          ctx.fillStyle = '#34D399';
          ctx.font = '700 12px "JetBrains Mono", monospace';
          ctx.fillText(randCount > 0 ? `PATCH #${randCount}: ${patch.name.slice(0, 14)}` : 'SAFE ANTI-SCREECH ENGINE', 18, 130, W - 36);
          ctx.fillStyle = '#94A3B8';
          ctx.font = '600 10px "JetBrains Mono", monospace';
          ctx.fillText(`K2:GEN · K4:${slotCode} ${slotTag}`, 18, 148, W - 36);
        } else if (subPage === 1) {
          ctx.fillStyle = '#34D399';
          ctx.font = '700 12.5px "JetBrains Mono", monospace';
          ctx.fillText(`REC LEN: ${recBars} BAR (${seqLength} ST)`, 12, 108, W - 24);
          ctx.fillStyle = '#FBBF24';
          ctx.fillText(`POLY: ${patch.polyphony || 8}V · SWING: ${swing}%`, 12, 130, W - 24);
          ctx.fillStyle = '#E2E8F0';
          ctx.fillText(`GLIDE: ${patch.glide ?? 0} · OCT: ${octaveOffset >= 0 ? `+${octaveOffset}` : octaveOffset} · ${midiConnected && selectedOutputId ? 'MIDI OUT' : 'AUDIO'}`, 12, 152, W - 24);
        } else {
          ctx.fillStyle = '#22D3EE';
          ctx.font = '700 12.5px "JetBrains Mono", monospace';
          ctx.fillText(`KEY: ${NOTE_NAMES_12[rootPc]} ${SCALE_LABELS[scaleMode].toUpperCase()}`, 12, 108, W - 24);
          ctx.fillStyle = '#FBBF24';
          ctx.fillText(`CHORD: ${CHORD_LABELS[chordMode].toUpperCase()} · ${bpm} BPM`, 12, 130, W - 24);
          ctx.fillStyle = '#E2E8F0';
          ctx.fillText(`REC LOOP: ${recBars} BAR (${seqLength} STEPS)`, 12, 152, W - 24);
        }
      } else {
        // HOME: Live 32-Band FFT Spectrum Analyzer + Cutoff Passband + Glowing Oscilloscope
        const c1Norm = Math.max(0.06, Math.min(0.94, Math.log10(Math.max(60, patch.filterCutoff) / 50) / Math.log10(19000 / 50)));
        const c2Norm = Math.max(0.06, Math.min(0.94, Math.log10(Math.max(60, patch.filter2Cutoff ?? 9500) / 50) / Math.log10(19000 / 50)));
        const cx1 = 12 + c1Norm * (W - 24);
        const cx2 = 12 + c2Norm * (W - 24);
        const resoBoost = Math.min(1, (patch.filterReso || 1) / 14);
        const tSec = now * 0.003;

        // Subtle baseline & passband window glow
        const xLeft = Math.min(cx1, cx2);
        const xRight = Math.max(cx1, cx2);
        ctx.fillStyle = 'rgba(34, 211, 238, 0.06)';
        ctx.fillRect(12, 92, Math.max(8, cx1 - 12), 66);
        ctx.fillStyle = 'rgba(251, 191, 36, 0.05)';
        ctx.fillRect(xLeft, 92, Math.max(4, xRight - xLeft), 66);

        // 32 live FFT spectrum bars rising from y = 158 (with ambient shimmer + audio energy)
        const numBars = 32;
        const barStep = (W - 24) / numBars;
        for (let b = 0; b < numBars; b++) {
          const binIdx = Math.min(freqData.length - 1, Math.floor(Math.pow(b / numBars, 1.45) * 96) + 1);
          const fftNorm = freqData[binIdx] / 255;
          const idleShimmer = (0.06 + 0.04 * Math.sin(tSec * 1.4 + b * 0.45)) * (1 - Math.min(1, rms * 1.6));
          const mag = Math.max(idleShimmer, fftNorm * (0.9 + resoBoost * 0.35));
          const barH = Math.max(3, Math.min(58, mag * 58));
          const bx = 12 + b * barStep;
          const by = 158 - barH;
          const inPass = bx <= cx1 + 4;
          ctx.fillStyle = fftNorm > 0.65
            ? 'rgba(251, 191, 36, 0.72)'
            : inPass
            ? 'rgba(34, 211, 238, 0.46)'
            : 'rgba(100, 116, 139, 0.25)';
          ctx.fillRect(bx, by, barStep - 1.5, barH);
        }

        // Animated Cutoff 1 & Cutoff 2 Marker Lines
        ctx.fillStyle = 'rgba(34, 211, 238, 0.75)';
        ctx.fillRect(cx1 - 1, 92, 2, 66);
        ctx.fillStyle = 'rgba(251, 191, 36, 0.6)';
        ctx.fillRect(cx2 - 1, 96, 1.5, 62);

        // Glowing Oscilloscope Waveform overlay
        ctx.save();
        ctx.shadowColor = '#22D3EE';
        ctx.shadowBlur = rms > 0.02 ? 7 : 3;
        ctx.strokeStyle = rms > 0.02 ? '#E0F2FE' : '#22D3EE';
        ctx.lineWidth = 2.2;
        ctx.beginPath();
        const sliceW = (W - 24) / 128;
        let sx = 12;
        for (let i = 0; i < 128; i++) {
          const raw = (timeData[i * 2] - 128) / 128;
          const idleRipple = rms < 0.01 ? Math.sin(tSec * 1.6 + i * 0.14) * 0.045 : 0;
          const y = 122 + (raw + idleRipple) * 28;
          if (i === 0) ctx.moveTo(sx, y);
          else ctx.lineTo(sx, y);
          sx += sliceW;
        }
        ctx.stroke();
        ctx.restore();
      }

      // 5. Bottom Zone (y: 164..240) - Either Bottom-Dock Live Parameter Inspector OR 2x2 Parameter Grid
      ctx.strokeStyle = 'rgba(148, 163, 184, 0.2)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(0, 164);
      ctx.lineTo(W, 164);
      ctx.stroke();

      if (activeHud?.kind === 'param') {
        ctx.fillStyle = '#0D1320';
        ctx.fillRect(0, 165, W, H - 165);

        ctx.fillStyle = '#94A3B8';
        ctx.font = '700 10.5px "JetBrains Mono", monospace';
        const tagPrefix = activeHud.knobIdx ? `K${activeHud.knobIdx} · ` : '';
        ctx.fillText(`${tagPrefix}${activeHud.label}`, 12, 183, W - 24);

        ctx.fillStyle = '#F8FAFC';
        ctx.font = '800 23px "JetBrains Mono", monospace';
        ctx.textAlign = 'right';
        ctx.fillText(activeHud.value, W - 12, 194, W - 108);
        ctx.textAlign = 'left';

        ctx.fillStyle = '#1E293B';
        ctx.fillRect(12, 206, W - 24, 12);
        ctx.fillStyle = activeHud.accent;
        ctx.fillRect(12, 206, (W - 24) * Math.max(0, Math.min(1, activeHud.ratio)), 12);

        ctx.fillStyle = '#64748B';
        ctx.font = '600 9.5px "JetBrains Mono", monospace';
        const curDrum = drumTracks[selectedDrumTrack];
        const footerTxt =
          activePage === 'SEQ'
            ? `KIT${drumKitIndex + 1} · ${DRUM_KITS[drumKitIndex]?.id || 'DRUM'} · ${curDrum?.shortName || 'BD'}`
            : `${bankCode} · ${patch.name.slice(0, 18)}`;
        ctx.fillText(footerTxt, 12, 232, W - 24);
        return;
      }

      ctx.beginPath();
      ctx.moveTo(W / 2, 164);
      ctx.lineTo(W / 2, H);
      ctx.moveTo(0, 202);
      ctx.lineTo(W, 202);
      ctx.stroke();

      for (let i = 1; i <= 4; i++) {
        const kDef = getKnobDef(i as 1 | 2 | 3 | 4);
        const col = (i - 1) % 2;
        const row = Math.floor((i - 1) / 2);
        const cx = col * (W / 2);
        const cy = 164 + row * 38;

        ctx.fillStyle = '#94A3B8';
        ctx.font = '700 10px "JetBrains Mono", monospace';
        ctx.fillText(kDef.label, cx + 8, cy + 14, W / 2 - 16);

        ctx.fillStyle = '#F8FAFC';
        ctx.font = '700 14px "JetBrains Mono", monospace';
        ctx.textAlign = 'right';
        ctx.fillText(kDef.display, cx + W / 2 - 8, cy + 28, W / 2 - 16);
        ctx.textAlign = 'left';

        ctx.fillStyle = '#1E293B';
        ctx.fillRect(cx + 8, cy + 32, W / 2 - 16, 3);
        ctx.fillStyle = i <= 2 ? '#22D3EE' : '#FBBF24';
        ctx.fillRect(cx + 8, cy + 32, (W / 2 - 16) * Math.max(0, Math.min(1, kDef.val)), 3);
      }
    };

    renderOled();
    return () => cancelAnimationFrame(animId);
  }, [
    patch,
    activePage,
    subPage,
    activeBank,
    bpm,
    swing,
    isPlayingSeq,
    isRecording,
    confirmPrompt,
    saveHoldStart,
    recHoldStart,
    drumClearHold,
    saveFlashUntil,
    saveBannerTitle,
    octaveOffset,
    rootPc,
    scaleMode,
    chordMode,
    randStyle,
    randChaos,
    randCount,
    targetBankXSlot,
    volumePct,
    isPoweredOn,
    bankPresets.length,
    midiConnected,
    selectedOutputId,
    selectedDrumTrack,
    selectedStepIdx,
    seqLength,
    playheadStep,
    drumTracks,
    drumKitPreset,
    drumKitIndex,
    drumBusMode,
    muteKeyHeld,
    synthLoopCount,
    getKnobDef
  ]);

  // Release all pointer-held capsule keys safely (preserving latched MUTE mode)
  const releaseAllPointerKeys = useCallback(() => {
    if (drumClearTimerRef.current !== null) {
      window.clearTimeout(drumClearTimerRef.current);
      drumClearTimerRef.current = null;
    }
    drumClearTrackRef.current = null;
    setDrumClearHold(null);
    if (muteKeyHeldRef.current && !muteLatchedRef.current) {
      handleSeqKeyUp(PANEL_BLACK_K[10]);
    }
    if (pointerHeldNotesRef.current.size === 0) return;
    pointerHeldNotesRef.current.forEach((notes) => {
      notes.forEach((midiNote) => {
        audioEngine.noteOff(midiNote);
        midiManager.sendNoteOff(midiNote);
      });
    });
    pointerHeldNotesRef.current.clear();
    syncActiveKeyMask();
  }, [syncActiveKeyMask, handleSeqKeyUp]);

  const handlePanicAllNotesOff = useCallback(() => {
    if (drumClearTimerRef.current !== null) {
      window.clearTimeout(drumClearTimerRef.current);
      drumClearTimerRef.current = null;
    }
    drumClearTrackRef.current = null;
    setDrumClearHold(null);
    muteKeyHeldRef.current = false;
    muteLatchedRef.current = false;
    muteUsedWhileHeldRef.current = false;
    setMuteKeyHeld(false);
    pointerHeldNotesRef.current.forEach((notes) => notes.forEach((n) => midiManager.sendNoteOff(n)));
    keyboardHeldNotesRef.current.forEach((notes) => notes.forEach((n) => midiManager.sendNoteOff(n)));
    pointerHeldNotesRef.current.clear();
    keyboardHeldNotesRef.current.clear();
    midiHeldKeysRef.current.clear();
    setActiveKeyMask(0);
    audioEngine.panic();
  }, []);

  const handlePointerKeyDown = (keyIndex: number, e: React.PointerEvent) => {
    e.preventDefault();
    if (!isPoweredOnRef.current) return;
    touchMenuActivity();
    audioEngine.initAudio();
    if (pointerHeldNotesRef.current.has(keyIndex)) return;
    if (handleSeqKeyDown(keyIndex, 112)) {
      pointerHeldNotesRef.current.set(keyIndex, []);
      syncActiveKeyMask();
      return;
    }
    if (activePageRef.current !== 'SEQ' && isRecordingRef.current && !isPlayingSeqRef.current) {
      onTogglePlaySeqRef.current();
    }
    const rawMidi = KEY_MIDI_BASE + keyIndex + octaveOffset * 12;
    const quantized = quantizeMidiToScale(rawMidi, scaleMode, rootPc);
    const notes = expandChordNotes(quantized, chordMode);
    pointerHeldNotesRef.current.set(keyIndex, notes);
    syncActiveKeyMask();
    notes.forEach((n) => {
      audioEngine.noteOn(n, 112);
      midiManager.sendNoteOn(n, 112);
    });
  };

  const handlePointerKeyUpOrLeave = (keyIndex: number, isLeave = false) => {
    if (!isLeave || keyIndex !== PANEL_BLACK_K[10]) {
      handleSeqKeyUp(keyIndex);
    }
    const notes = pointerHeldNotesRef.current.get(keyIndex);
    if (notes !== undefined) {
      pointerHeldNotesRef.current.delete(keyIndex);
      notes.forEach((n) => {
        audioEngine.noteOff(n);
        midiManager.sendNoteOff(n);
      });
      syncActiveKeyMask();
    }
  };

  // Global window pointerup / mouseup / blur safety release so no mouse note ever hangs
  useEffect(() => {
    const onGlobalRelease = () => {
      releaseAllPointerKeys();
      setActiveBtnMask(0);
    };
    const onVisibility = () => {
      if (document.hidden) handlePanicAllNotesOff();
    };
    window.addEventListener('pointerup', onGlobalRelease);
    window.addEventListener('mouseup', onGlobalRelease);
    window.addEventListener('blur', onGlobalRelease);
    document.addEventListener('visibilitychange', onVisibility);
    return () => {
      window.removeEventListener('pointerup', onGlobalRelease);
      window.removeEventListener('mouseup', onGlobalRelease);
      window.removeEventListener('blur', onGlobalRelease);
      document.removeEventListener('visibilitychange', onVisibility);
    };
  }, [releaseAllPointerKeys, handlePanicAllNotesOff]);

  // Computer Keyboard Support (Z–M / Q–O) + Esc Panic
  useEffect(() => {
    const KEY_MAP: Record<string, number> = {
      z: 0, s: 1, x: 2, d: 3, c: 4, v: 5, g: 6, b: 7, h: 8, n: 9, j: 10, m: 11,
      q: 12, '2': 13, w: 14, e: 15, '4': 16, r: 17, '5': 18, t: 19, '6': 20, y: 21, u: 22, '8': 23, i: 24, '9': 25, o: 26
    };

    const onKeyDown = (e: KeyboardEvent) => {
      if (!isPoweredOnRef.current) return;
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLSelectElement) return;
      if (e.key === 'Escape') {
        e.preventDefault();
        handlePanicAllNotesOff();
        return;
      }
      if (e.repeat) return;
      if (e.code === 'Space') {
        e.preventDefault();
        onTogglePlaySeq();
        return;
      }
      if (e.key === '-') {
        buttonDownRef.current('OCT-');
        return;
      }
      if (e.key === '=' || e.key === '+') {
        buttonDownRef.current('OCT+');
        return;
      }
      const k = e.key.toLowerCase();
      if (k in KEY_MAP) {
        e.preventDefault();
        const keyIndex = KEY_MAP[k];
        if (!keyboardHeldNotesRef.current.has(keyIndex)) {
          touchMenuActivity();
          audioEngine.initAudio();
          if (handleSeqKeyDown(keyIndex, 110)) {
            keyboardHeldNotesRef.current.set(keyIndex, []);
            syncActiveKeyMask();
            return;
          }
          if (activePageRef.current !== 'SEQ' && isRecordingRef.current && !isPlayingSeqRef.current) {
            onTogglePlaySeqRef.current();
          }
          const rawMidi = KEY_MIDI_BASE + keyIndex + octaveOffset * 12;
          const quantized = quantizeMidiToScale(rawMidi, scaleMode, rootPc);
          const notes = expandChordNotes(quantized, chordMode);
          keyboardHeldNotesRef.current.set(keyIndex, notes);
          syncActiveKeyMask();
          notes.forEach((n) => {
            audioEngine.noteOn(n, 110);
            midiManager.sendNoteOn(n, 110);
          });
        }
      }
    };

    const onKeyUp = (e: KeyboardEvent) => {
      if (e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement || e.target instanceof HTMLSelectElement) return;
      const k = e.key.toLowerCase();
      if (k in KEY_MAP) {
        e.preventDefault();
        const keyIndex = KEY_MAP[k];
        handleSeqKeyUp(keyIndex);
        const notes = keyboardHeldNotesRef.current.get(keyIndex);
        if (notes !== undefined) {
          keyboardHeldNotesRef.current.delete(keyIndex);
          notes.forEach((n) => {
            audioEngine.noteOff(n);
            midiManager.sendNoteOff(n);
          });
          syncActiveKeyMask();
        }
      }
    };

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
    };
  }, [octaveOffset, scaleMode, rootPc, chordMode, onTogglePlaySeq, handlePanicAllNotesOff, syncActiveKeyMask, handleSeqKeyDown, handleSeqKeyUp, touchMenuActivity]);

  const handleSelectPreset = useCallback(
    (newPreset: SynthPatch, announce = true) => {
      releaseAllPointerKeys();
      recordRecentPreset(newPreset);
      onPatchChange(newPreset);
      audioEngine.setPatch(newPreset);
      if (announce) {
        const list = getBankPresets(newPreset.bank || activeBank);
        const curIdx = list.findIndex((p) => p.id === newPreset.id);
        const prevName = curIdx >= 0 ? list[(curIdx - 1 + list.length) % list.length]?.name || '' : '';
        const nextName = curIdx >= 0 ? list[(curIdx + 1) % list.length]?.name || '' : '';
        const code = getPresetBankCode(newPreset);
        const routeShort =
          newPreset.filterRouting === 'split'
            ? 'SPL'
            : newPreset.filterRouting === 'parallel'
            ? 'PAR'
            : 'SER';
        const catShort = newPreset.category.toUpperCase().split('/')[0].trim().slice(0, 6);
        showHud(
          {
            kind: 'preset',
            code,
            name: newPreset.name,
            prevName,
            nextName,
            meta: `${catShort} · ${(newPreset.oscMode || 'hypersaw').toUpperCase()} · ${routeShort}`
          },
          1050
        );
      }
    },
    [onPatchChange, releaseAllPointerKeys, showHud, activeBank]
  );

  const handleSaveToMemory = useCallback(
    (dest: 'bankX' | 'slot', bankXSlot: number) => {
      if (dest === 'bankX') {
        const saved = savePatchToBankX(patch, bankXSlot);
        recordRecentPreset(saved);
        setActiveBank('X');
        setBankRevision((r) => r + 1);
        setTargetBankXSlot(getNextFreeBankXSlot());
        onPatchChange(saved);
        audioEngine.setPatch(saved);
        const code = getPresetBankCode(saved, 'X');
        setSaveBannerTitle('SAVED TO BANK X');
        setSaveFlashUntil(performance.now() + 1400);
        showHud(
          {
            kind: 'badge',
            title: 'SAVED TO BANK X',
            subtitle: `${code} · ${saved.name}`,
            accent: '#34D399'
          },
          1350
        );
      } else {
        const saved = saveUserPreset(patch);
        recordRecentPreset(saved);
        setBankRevision((r) => r + 1);
        setSaveBannerTitle('PATCH SAVED');
        setSaveFlashUntil(performance.now() + 1400);
        showHud(
          {
            kind: 'badge',
            title: 'PATCH SAVED',
            subtitle: `${getPresetBankCode(saved, activeBank)} ${saved.name}`,
            accent: '#34D399'
          },
          1250
        );
      }
    },
    [patch, activeBank, onPatchChange, showHud]
  );

  const handleRevertFactory = useCallback(() => {
    const restored = revertPresetToFactory(activeBank, currentProgramIndex, patch.id);
    setBankRevision((r) => r + 1);
    onPatchChange(restored);
    audioEngine.setPatch(restored);
    const title = activeBank === 'X' ? 'USER SLOT CLEARED' : 'FACTORY RESET';
    setSaveBannerTitle(title);
    setSaveFlashUntil(performance.now() + 1400);
    showHud(
      {
        kind: 'badge',
        title,
        subtitle: `${getPresetBankCode(restored, activeBank)} ${restored.name}`,
        accent: '#22D3EE'
      },
      1300
    );
  }, [activeBank, currentProgramIndex, patch.id, onPatchChange, showHud]);

  const handleInitCurrentSlot = useCallback(() => {
    const initPatch = createInitSlotPatch(activeBank, currentProgramIndex, patch.id);
    onPatchChange(initPatch);
    audioEngine.setPatch(initPatch);
    setSaveBannerTitle('VIRUS INIT');
    setSaveFlashUntil(performance.now() + 1400);
    showHud(
      {
        kind: 'badge',
        title: 'VIRUS INIT',
        subtitle: `${getPresetBankCode(initPatch, activeBank)} CLEAN TEMPLATE`,
        accent: '#FBBF24'
      },
      1300
    );
  }, [activeBank, currentProgramIndex, patch.id, onPatchChange, showHud]);

  const handleGenerateRandomPatch = useCallback(
    (styleOverride?: RandomPatchStyle, chaosOverride?: number) => {
      const nextCount = randCount + 1;
      const rndPatch = generateSafeRandomPatch(styleOverride ?? randStyle, chaosOverride ?? randChaos, nextCount);
      setRandCount(nextCount);
      onPatchChange(rndPatch);
      audioEngine.setPatch(rndPatch);
      showHud(
        {
          kind: 'badge',
          title: `RAND #${nextCount}: ${rndPatch.name.slice(0, 14)}`,
          subtitle: `${rndPatch.category.toUpperCase()} · K4:X${targetBankXSlot.toString().padStart(3, '0')} · SAVE`,
          accent: '#FBBF24'
        },
        1150
      );
    },
    [randCount, randStyle, randChaos, targetBankXSlot, onPatchChange, showHud]
  );

  // Latching Hardware Action Buttons: active mode latches ON; pressing again or HOME returns cleanly
  const toggleModeLatch = (target: Exclude<PanelPage, 'HOME'>, label: string, sub: string) => {
    if (activePage === target) {
      muteKeyHeldRef.current = false;
      muteLatchedRef.current = false;
      muteUsedWhileHeldRef.current = false;
      setMuteKeyHeld(false);
      setActivePage('HOME');
      setSubPage(0);
      showHud({ kind: 'badge', title: 'SYNTH MODE', subtitle: 'MAIN PERFORMANCE PAGE', accent: '#22D3EE' }, 750);
    } else {
      setActivePage(target);
      setSubPage(0);
      showHud({ kind: 'badge', title: label, subtitle: sub, accent: target === 'SEQ' ? '#34D399' : '#22D3EE' }, 850);
    }
  };

  const executePendingLeave = useCallback(
    (pending: PendingLeaveAction) => {
      if (pending.kind === 'preset') {
        const target =
          pending.targetPatch.bank === 'X' && pending.targetPatch.programNumber !== undefined
            ? getBankPresets('X')[pending.targetPatch.programNumber] || pending.targetPatch
            : pending.targetPatch;
        handleSelectPreset(target, true);
      } else {
        const meta = VIRUS_BANKS.find((b) => b.id === pending.targetBankId);
        setActiveBank(pending.targetBankId);
        const bankList = getBankPresets(pending.targetBankId);
        const targetProg = Math.min(bankList.length - 1, pending.fallbackProg);
        const nextPatch = bankList[targetProg] || bankList[0];
        if (nextPatch) handleSelectPreset(nextPatch, false);
        showHud(
          {
            kind: 'bank',
            bankId: pending.targetBankId,
            subtitle:
              pending.targetBankId === 'X'
                ? `USER PRESETS · ${getBankXCount()}/128 SAVED`
                : meta?.subtitle || ''
          },
          1250
        );
      }
    },
    [handleSelectPreset, showHud]
  );

  const handleTogglePower = () => {
    if (isPoweredOn) {
      if (saveHoldTimerRef.current !== null) {
        window.clearTimeout(saveHoldTimerRef.current);
        saveHoldTimerRef.current = null;
      }
      if (recHoldTimerRef.current !== null) {
        window.clearTimeout(recHoldTimerRef.current);
        recHoldTimerRef.current = null;
      }
      if (arpHoldTimerRef.current !== null) {
        window.clearTimeout(arpHoldTimerRef.current);
        arpHoldTimerRef.current = null;
      }
      arpPressedRef.current = false;
      arpComboUsedRef.current = false;
      arpDidLongPressRef.current = false;
      bootSplashRef.current = null;
      audioEngine.stopBootJingle();
      setSaveHoldStart(null);
      setRecHoldStart(null);
      setConfirmPrompt(null);
      setIsRecording(false);
      handlePanicAllNotesOff();
      if (isPlayingSeq) {
        onTogglePlaySeq();
      }
      setIsPoweredOn(false);
    } else {
      audioEngine.initAudio();
      setIsPoweredOn(true);
      setActivePage('HOME');
      setSubPage(0);
      triggerBootSequence(true);
    }
  };

  const handleButtonDown = (btnName: keyof typeof PANEL_BTN) => {
    if (!isPoweredOn) return;
    audioEngine.initAudio();
    touchMenuActivity();
    if (bootSplashRef.current) {
      bootSplashRef.current = null;
      audioEngine.stopBootJingle();
    }
    const bit = PANEL_BTN[btnName];
    setActiveBtnMask((prev) => prev | (1 << bit));

    // Secret Hardware Combo: Hold ARP + OCT+ (Latch ON) or ARP + OCT- (Latch OFF)
    if (arpPressedRef.current && (btnName === 'OCT+' || btnName === 'OCT-')) {
      arpComboUsedRef.current = true;
      if (arpHoldTimerRef.current !== null) {
        window.clearTimeout(arpHoldTimerRef.current);
        arpHoldTimerRef.current = null;
      }
      const nextLatch = btnName === 'OCT+';
      const nextEnabled = nextLatch ? true : Boolean(patch.arpEnabled);
      onPatchChange({ ...patch, arpEnabled: nextEnabled, arpLatch: nextLatch });
      audioEngine.toggleArp(nextEnabled, nextLatch);
      showHud(
        {
          kind: 'badge',
          title: nextLatch ? 'ARP LATCH ON' : 'ARP LATCH OFF',
          subtitle: nextLatch ? 'COMBO: ARP + OCT+ · NOTES HOLD' : 'COMBO: ARP + OCT- · RELEASE STOPS',
          accent: nextLatch ? '#FBBF24' : '#94A3B8'
        },
        1050
      );
      return;
    }

    // Handle active Save / Factory Reset / Clear Loop / Unsaved Random confirmation prompt first
    if (confirmPrompt) {
      if (confirmPrompt.type === 'randomResolve') {
        if (btnName === 'OCT+') {
          const saved = savePatchToBankX(patch, confirmPrompt.bankXSlot);
          recordRecentPreset(saved);
          setBankRevision((r) => r + 1);
          setTargetBankXSlot(getNextFreeBankXSlot());
          const pending = confirmPrompt.pending;
          setConfirmPrompt(null);
          setSaveBannerTitle(`SAVED X${confirmPrompt.bankXSlot.toString().padStart(3, '0')}`);
          setSaveFlashUntil(performance.now() + 950);
          executePendingLeave(pending);
          return;
        }
        if (btnName === 'SEL') {
          const pending = confirmPrompt.pending;
          setConfirmPrompt(null);
          executePendingLeave(pending);
          return;
        }
        if (btnName === 'OCT-') {
          setConfirmPrompt(null);
          showHud({ kind: 'badge', title: 'STAYED ON RANDOM', subtitle: 'UNSAVED PATCH KEPT', accent: '#FBBF24' }, 850);
          return;
        }
        return;
      }

      if (btnName === 'OCT+') {
        const promptType = confirmPrompt;
        setConfirmPrompt(null);
        if (promptType.type === 'save') {
          handleSaveToMemory(promptType.dest, promptType.bankXSlot);
        } else if (promptType.type === 'clearLoop') {
          onClearSynthLoop();
          setIsRecording(false);
          showHud(
            {
              kind: 'badge',
              title: 'LOOP CLEARED',
              subtitle: 'SYNTH OVERDUB TRACK EMPTY',
              accent: '#FBBF24'
            },
            1050
          );
        } else if (promptType.mode === 'factory') {
          handleRevertFactory();
        } else {
          handleInitCurrentSlot();
        }
        return;
      }
      if (btnName === 'OCT-') {
        setConfirmPrompt(null);
        showHud({ kind: 'badge', title: 'CANCELLED', subtitle: 'NO CHANGES MADE', accent: '#94A3B8' }, 800);
        return;
      }
      if (btnName === 'SEL' && confirmPrompt.type === 'reset') {
        setConfirmPrompt({
          type: 'reset',
          mode: confirmPrompt.mode === 'factory' ? 'init' : 'factory'
        });
        return;
      }
      if (btnName === 'SEL' && confirmPrompt.type === 'save') {
        // Only toggle Bank X <-> Current Slot when editing a Bank A-I preset; random & Bank X patches save to Bank X
        if (activeBank !== 'X' && !patch.id.startsWith('rand-')) {
          setConfirmPrompt({
            ...confirmPrompt,
            dest: confirmPrompt.dest === 'bankX' ? 'slot' : 'bankX'
          });
          return;
        }
      }
      setConfirmPrompt(null);
    }

    const SYNTH_PAGE_SUBS: Record<Exclude<PanelPage, 'HOME' | 'SEQ'>, Array<{ title: string; sub: string; accent: string }>> = {
      FX: [
        { title: 'FX RACK MIX (1/2)', sub: 'CHORUS · PHASER · DELAY · REVERB', accent: '#22D3EE' },
        { title: 'DLY MIX & SAT (2/2)', sub: 'DLY MIX · FDBK · PHS RATE · SAT', accent: '#22D3EE' }
      ],
      SEL: [
        { title: 'FILTER MODE (1/3)', sub: 'FLT MODE · ROUTING · F1/F2 TYPE', accent: '#22D3EE' },
        { title: 'FILTER TONE (2/3)', sub: 'CUTOFF 1 · RESO 1 · F2 CUT/RESO', accent: '#22D3EE' },
        { title: 'FILTER ENV (3/3)', sub: 'F-BAL · F-ENV · F-REL · KEYFLW', accent: '#FBBF24' }
      ],
      ENV: [
        { title: 'AMP ENV (1/2)', sub: 'ATTACK · DECAY · SUSTAIN · RELEASE', accent: '#22D3EE' },
        { title: 'FILTER ENV (2/2)', sub: 'F-ATK · F-DEC · F-SUS · F-ENV', accent: '#FBBF24' }
      ],
      LFO: [
        { title: 'LFO 1 MATRIX (1/3)', sub: 'RATE · AMT · DEST (INCL FX) · WAVE', accent: '#34D399' },
        { title: 'LFO 2 MATRIX (2/3)', sub: 'RATE · AMT · DEST (INCL FX) · WAVE', accent: '#A78BFA' },
        { title: 'SWARM & GLIDE (3/3)', sub: 'ANALOG DRIFT · PORTAMENTO · KEYFLW', accent: '#22D3EE' }
      ],
      EDIT: [
        { title: 'OSC & UNISON (1/3)', sub: 'OSC BAL · OSC2 ST · SPREAD · UNISON', accent: '#22D3EE' },
        { title: 'SUB / NOISE / RNG (2/3)', sub: 'SUB LVL · SUB WAVE · NOISE · RING', accent: '#22D3EE' },
        { title: 'SYNC / FM / DRV (3/3)', sub: 'HARD SYNC · FM AMT · UNI PAN · DRV', accent: '#F97316' }
      ],
      GLO: [
        { title: 'SCALE & CHORD (1/3)', sub: 'ROOT · 12 SCALES · TEMPO · CHORD', accent: '#22D3EE' },
        { title: 'VOICE & SYSTEM (2/3)', sub: 'REC BARS · POLY · SWING · GLIDE', accent: '#34D399' },
        { title: 'RANDOMIZER (3/3)', sub: 'STYLE · K2:GEN · CHAOS · SAVE->X', accent: '#FBBF24' }
      ],
      ARP: [
        { title: 'ARP SETUP (1/2)', sub: 'K1:ARP ON/OFF · K2:LATCH · PATTERN · RATE', accent: '#34D399' },
        { title: 'ARP GROOVE (2/2)', sub: 'OCTAVES · GATE · SWING · TEMPO', accent: '#34D399' }
      ]
    };

    const clearMuteState = () => {
      muteKeyHeldRef.current = false;
      muteLatchedRef.current = false;
      muteUsedWhileHeldRef.current = false;
      setMuteKeyHeld(false);
    };

    const cycleSynthPageButton = (target: Exclude<PanelPage, 'HOME' | 'SEQ'>) => {
      clearMuteState();
      if (activePage !== target) {
        setActivePage(target);
        setSubPage(0);
        const meta = SYNTH_PAGE_SUBS[target][0];
        showHud({ kind: 'badge', title: meta.title, subtitle: meta.sub, accent: meta.accent }, 800);
        return;
      }
      const pages = SYNTH_PAGE_SUBS[target];
      const nextSub = subPage + 1;
      if (nextSub < pages.length) {
        setSubPage(nextSub);
        const meta = pages[nextSub];
        showHud({ kind: 'badge', title: meta.title, subtitle: meta.sub, accent: meta.accent }, 800);
      } else {
        setActivePage('HOME');
        setSubPage(0);
        showHud({ kind: 'badge', title: 'PERFORMANCE', subtitle: 'MAIN HOME PAGE', accent: '#22D3EE' }, 700);
      }
    };

    if (btnName === 'FX' || btnName === 'ENV' || btnName === 'LFO' || btnName === 'EDIT' || btnName === 'GLO') {
      cycleSynthPageButton(btnName);
    } else if (btnName === 'SEL') {
      if (activePage === 'SEQ') {
        const nextSub = (subPage + 1) % PAGE_TOTALS.SEQ;
        setSubPage(nextSub);
        const seqTitles = ['DRUM VOICE (1/3)', 'EUCLID & GRV (2/3)', 'KIT GLOBAL (3/3)'];
        showHud(
          {
            kind: 'badge',
            title: seqTitles[nextSub],
            subtitle: 'KNOBS 1-4 UPDATED · SEQ:EXIT',
            accent: '#34D399'
          },
          750
        );
        return;
      }
      cycleSynthPageButton('SEL');
    } else if (btnName === 'ARP') {
      // Short press ARP alone = toggle ARP ON/OFF; Hold ARP (420ms) = flip ARP (1/2) <-> (2/2); Hold ARP + OCT± = Latch ON/OFF
      clearMuteState();
      arpPressedRef.current = true;
      arpComboUsedRef.current = false;
      arpDidLongPressRef.current = false;
      if (arpHoldTimerRef.current !== null) window.clearTimeout(arpHoldTimerRef.current);
      arpHoldTimerRef.current = window.setTimeout(() => {
        arpHoldTimerRef.current = null;
        if (arpComboUsedRef.current || !isPoweredOnRef.current) return;
        arpDidLongPressRef.current = true;
        if (activePageRef.current !== 'ARP') {
          setActivePage('ARP');
          setSubPage(0);
          showHud({ kind: 'badge', title: 'ARP SETUP (1/2)', subtitle: 'PATTERN · RATE · LATCH · ARP:OFF', accent: '#34D399' }, 850);
        } else {
          setSubPage((prev) => {
            const next = prev === 0 ? 1 : 0;
            showHud(
              {
                kind: 'badge',
                title: next === 1 ? 'ARP GROOVE (2/2)' : 'ARP SETUP (1/2)',
                subtitle: next === 1 ? 'OCTAVES · GATE · SWING · TEMPO' : 'K1:ARP · K2:LATCH · PATTERN · RATE',
                accent: '#34D399'
              },
              850
            );
            return next;
          });
        }
      }, 420);
    } else if (btnName === 'SEQ') {
      toggleModeLatch('SEQ', 'DRUM MACHINE', 'SEL: 1/3-3/3 · BLK1-8 · WHT:STEP');
    } else if (btnName === 'HOME') {
      clearMuteState();
      setActivePage('HOME');
      setSubPage(0);
      showHud({ kind: 'badge', title: 'PERFORMANCE', subtitle: 'SYNTH HOME PAGE', accent: '#22D3EE' }, 700);
    } else if (btnName === 'PLAY') onTogglePlaySeq();
    else if (btnName === 'REC') {
      recPressedRef.current = true;
      recDidLongPressRef.current = false;
      setRecHoldStart(performance.now());
      if (recHoldTimerRef.current !== null) window.clearTimeout(recHoldTimerRef.current);
      recHoldTimerRef.current = window.setTimeout(() => {
        recDidLongPressRef.current = true;
        setRecHoldStart(null);
        setIsRecording(false);
        setConfirmPrompt({ type: 'clearLoop' });
      }, 1500);
    } else if (btnName === 'SAVE') {
      savePressedRef.current = true;
      saveDidLongPressRef.current = false;
      setSaveHoldStart(performance.now());
      if (saveHoldTimerRef.current !== null) window.clearTimeout(saveHoldTimerRef.current);
      saveHoldTimerRef.current = window.setTimeout(() => {
        saveDidLongPressRef.current = true;
        setSaveHoldStart(null);
        setConfirmPrompt({ type: 'reset', mode: 'factory' });
      }, 1500);
    } else if (btnName === 'OCT-') {
      applyOctaveChange(octaveOffset - 1, true);
    } else if (btnName === 'OCT+') {
      applyOctaveChange(octaveOffset + 1, true);
    }
  };

  const buttonDownRef = useRef(handleButtonDown);
  useEffect(() => {
    buttonDownRef.current = handleButtonDown;
  });

  const handleButtonUp = (btnName: keyof typeof PANEL_BTN) => {
    const bit = PANEL_BTN[btnName];
    setActiveBtnMask((prev) => prev & ~(1 << bit));

    if (btnName === 'ARP') {
      if (!arpPressedRef.current) return;
      arpPressedRef.current = false;
      if (arpHoldTimerRef.current !== null) {
        window.clearTimeout(arpHoldTimerRef.current);
        arpHoldTimerRef.current = null;
      }
      if (!arpComboUsedRef.current && !arpDidLongPressRef.current && isPoweredOn) {
        muteKeyHeldRef.current = false;
        muteLatchedRef.current = false;
        muteUsedWhileHeldRef.current = false;
        setMuteKeyHeld(false);
        const nextEnabled = !patch.arpEnabled;
        const nextLatch = nextEnabled ? Boolean(patch.arpLatch) : false;
        onPatchChange({ ...patch, arpEnabled: nextEnabled, arpLatch: nextLatch });
        audioEngine.toggleArp(nextEnabled, nextLatch);
        if (nextEnabled) {
          setActivePage('ARP');
          setSubPage(0);
          showHud({ kind: 'badge', title: 'ARP ON', subtitle: 'HOLD ARP:PG 2/2 · ARP+OCT±:LATCH', accent: '#34D399' }, 900);
        } else {
          if (activePage === 'ARP') {
            setActivePage('HOME');
            setSubPage(0);
          }
          showHud({ kind: 'badge', title: 'ARP OFF', subtitle: 'ARPEGGIATOR STOPPED', accent: '#94A3B8' }, 800);
        }
      }
      arpComboUsedRef.current = false;
      arpDidLongPressRef.current = false;
    }

    if (btnName === 'REC') {
      if (!recPressedRef.current) return;
      recPressedRef.current = false;
      setRecHoldStart(null);
      if (recHoldTimerRef.current !== null) {
        window.clearTimeout(recHoldTimerRef.current);
        recHoldTimerRef.current = null;
      }
      if (!recDidLongPressRef.current && isPoweredOn) {
        setIsRecording((prev) => {
          const next = !prev;
          const inSeq = activePage === 'SEQ';
          const recBars = Math.max(1, Math.min(4, Math.ceil(seqLength / 16)));
          showHud(
            {
              kind: 'badge',
              title: next ? (inSeq ? 'DRUM REC ARMED' : 'SYNTH LOOP REC') : 'REC OFF',
              subtitle: next
                ? inSeq
                  ? 'BLK 1-8 TAP OVERDUB · WHT 1-16'
                  : `${recBars} BAR (${seqLength}ST) · GLO 2/3: BARS`
                : synthLoopCount > 0 && !inSeq
                ? `${synthLoopCount} NOTES (${recBars}B) · HOLD:CLR`
                : 'RECORDING STOPPED',
              accent: next ? '#EF4444' : '#94A3B8'
            },
            1050
          );
          return next;
        });
      }
    }

    if (btnName === 'SAVE') {
      if (!savePressedRef.current) return;
      savePressedRef.current = false;
      setSaveHoldStart(null);
      if (saveHoldTimerRef.current !== null) {
        window.clearTimeout(saveHoldTimerRef.current);
        saveHoldTimerRef.current = null;
      }
      if (!saveDidLongPressRef.current && isPoweredOn) {
        // Short press opens YES/CANCEL Save Confirmation prompt targeting Bank X (User Presets) or current slot
        const targetSlot = activeBank === 'X' ? currentProgramIndex : targetBankXSlot;
        setConfirmPrompt({
          type: 'save',
          dest: 'bankX',
          bankXSlot: targetSlot
        });
      }
    }
  };

  // Endless Rotary Encoder Delta Applier (Mouse Drag & Wheel - no potentiometer stops)
  const applyEncoderStep = useCallback(
    (knobName: string, deltaNorm: number, discreteSteps: number) => {
      if (!isPoweredOnRef.current) return;
      touchMenuActivity();
      if (bootSplashRef.current) {
        bootSplashRef.current = null;
        audioEngine.stopBootJingle();
      }
      const shapes: Array<'sine' | 'triangle' | 'saw' | 'square'> = ['sine', 'triangle', 'saw', 'square'];
      const arpModes: Array<'up' | 'down' | 'updown' | 'random'> = ['up', 'down', 'updown', 'random'];
      const arpRates: Array<'1/8' | '1/16' | '1/16T' | '1/32'> = ['1/8', '1/16', '1/16T', '1/32'];
      const clamp01 = (n: number) => Math.max(0, Math.min(1, n));

      // When Save or Unsaved Random Resolution prompt is open, SELECT / PRESETS / KNOBS 1-4 pick the Bank X slot cleanly
      if (
        (confirmPrompt?.type === 'randomResolve' || confirmPrompt?.type === 'save') &&
        knobName !== 'MASTER' &&
        knobName !== 'ALGORITHM'
      ) {
        if (discreteSteps === 0) return;
        const nextSlot = (((confirmPrompt.bankXSlot + discreteSteps) % 128) + 128) % 128;
        setTargetBankXSlot(nextSlot);
        if (confirmPrompt.type === 'save') {
          setConfirmPrompt({ ...confirmPrompt, dest: 'bankX', bankXSlot: nextSlot });
        } else {
          setConfirmPrompt({ ...confirmPrompt, bankXSlot: nextSlot });
        }
        return;
      }

      if (knobName === 'MASTER') {
        const nextNorm = clamp01(volumePct / 100 + deltaNorm);
        const nextPct = Math.round(nextNorm * 100);
        setVolumePct(nextPct);
        audioEngine.setMasterVolume(nextNorm);
        showHud({ kind: 'param', label: 'MASTER VOL', value: `${nextPct}%`, ratio: nextNorm, accent: '#22D3EE' }, 800);
      } else if (knobName === 'SELECT') {
        if (discreteSteps === 0) return;
        if (activePage === 'SEQ') {
          const nextTr = (((selectedDrumTrack + discreteSteps) % 8) + 8) % 8;
          setSelectedDrumTrack(nextTr);
          const tr = drumTracks[nextTr];
          audioEngine.triggerDrum(nextTr, 108);
          showHud(
            {
              kind: 'param',
              label: 'DRUM VOICE',
              value: `${tr?.shortName || 'BD'} ${tr?.name?.toUpperCase() || ''}`,
              ratio: nextTr / 7,
              accent: '#22D3EE'
            },
            850
          );
          return;
        }
        const len = bankPresets.length;
        const nextIdx = (((currentProgramIndex + discreteSteps) % len) + len) % len;
        const newP = bankPresets[nextIdx];
        if (newP && newP.id !== patch.id) {
          if (patch.id.startsWith('rand-')) {
            setConfirmPrompt({
              type: 'randomResolve',
              pending: { kind: 'preset', targetPatch: newP },
              bankXSlot: targetBankXSlot
            });
            return;
          }
          handleSelectPreset(newP, true);
        }
      } else if (knobName === 'PRESETS') {
        if (discreteSteps === 0) return;
        if (activePage === 'SEQ') {
          const nextIdx = (((drumKitIndex + discreteSteps) % DRUM_KITS.length) + DRUM_KITS.length) % DRUM_KITS.length;
          onDrumKitChange(nextIdx);
          audioEngine.setDrumKit(nextIdx);
          audioEngine.triggerDrum(selectedDrumTrack, 106);
          const curKit = DRUM_KITS[nextIdx];
          const prevKit = DRUM_KITS[(nextIdx - 1 + DRUM_KITS.length) % DRUM_KITS.length];
          const nextKit = DRUM_KITS[(nextIdx + 1) % DRUM_KITS.length];
          showHud(
            {
              kind: 'drumkit',
              index: nextIdx,
              name: curKit.id,
              subtitle: curKit.subtitle,
              prevName: prevKit.id,
              nextName: nextKit.id
            },
            1150
          );
          return;
        }
        const curIdx = Math.max(0, VIRUS_BANKS.findIndex((b) => b.id === activeBank));
        const nextIdx = (((curIdx + discreteSteps) % VIRUS_BANKS.length) + VIRUS_BANKS.length) % VIRUS_BANKS.length;
        const targetMeta = VIRUS_BANKS[nextIdx];
        if (targetMeta && targetMeta.id !== activeBank) {
          if (patch.id.startsWith('rand-')) {
            setConfirmPrompt({
              type: 'randomResolve',
              pending: { kind: 'bank', targetBankId: targetMeta.id, fallbackProg: currentProgramIndex },
              bankXSlot: targetBankXSlot
            });
            return;
          }
          setActiveBank(targetMeta.id);
          const bankList = getBankPresets(targetMeta.id);
          const targetProg = Math.min(bankList.length - 1, currentProgramIndex);
          const nextPatch = bankList[targetProg] || bankList[0];
          if (nextPatch) handleSelectPreset(nextPatch, false);
          showHud(
            {
              kind: 'bank',
              bankId: targetMeta.id,
              subtitle:
                targetMeta.id === 'X'
                  ? `USER PRESETS · ${getBankXCount()}/128 SAVED`
                  : targetMeta.subtitle
            },
            1250
          );
        }
      } else if (knobName === 'ALGORITHM') {
        if (discreteSteps === 0) return;
        if (activePage === 'SEQ') {
          const nextIdx = (((drumBusMode + discreteSteps) % DRUM_BUS_MODES.length) + DRUM_BUS_MODES.length) % DRUM_BUS_MODES.length;
          onDrumBusModeChange(nextIdx);
          const mode = DRUM_BUS_MODES[nextIdx];
          showHud(
            {
              kind: 'badge',
              title: mode.id,
              subtitle: `DRUM BUS · ${mode.subtitle}`,
              accent: '#FBBF24'
            },
            1050
          );
          return;
        }
        const curIdx = Math.max(0, OSC_MODES.indexOf(patch.oscMode || 'hypersaw'));
        const nextIdx = (((curIdx + discreteSteps) % OSC_MODES.length) + OSC_MODES.length) % OSC_MODES.length;
        const nextMode = OSC_MODES[nextIdx];
        onPatchChange({
          ...patch,
          oscMode: nextMode,
          osc2Sync: nextMode === 'sync',
          algorithm: nextIdx * 5 + 1
        });
        showHud(
          {
            kind: 'badge',
            title: OSC_MODE_LABELS[nextMode] || nextMode.toUpperCase(),
            subtitle: 'SYNTH / OSC ENGINE',
            accent: '#FBBF24'
          },
          950
        );
      } else if (knobName === 'KNOB1') {
        const curNorm = getKnobDef(1).val;
        const nextNorm = clamp01(curNorm + deltaNorm);
        if (activePage === 'HOME') {
          const val = Math.round(60 + nextNorm * 18940);
          onPatchChange({ ...patch, filterCutoff: val });
          showHud({ kind: 'param', label: 'CUTOFF 1', value: `${val}Hz`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
        } else if (activePage === 'ENV') {
          lastEnvParamRef.current = { segment: 'A', expiresAt: performance.now() + 1400 };
          if (subPage === 1) {
            const val = parseFloat(Math.max(0.004, nextNorm * 2.0).toFixed(3));
            onPatchChange({ ...patch, filterAttack: val });
            showHud({ kind: 'param', label: 'FLT ATTACK', value: `${Math.round(val * 1000)}ms`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 1 }, 900);
          } else {
            const val = parseFloat(Math.max(0.005, nextNorm * 2.0).toFixed(3));
            onPatchChange({ ...patch, attack: val });
            showHud({ kind: 'param', label: 'AMP ATTACK', value: `${Math.round(val * 1000)}ms`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 900);
          }
        } else if (activePage === 'LFO') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, swarmDepth: val });
            showHud({ kind: 'param', label: 'SWARM DRIFT', value: `${val}%`, ratio: nextNorm, accent: '#34D399', knobIdx: 1 }, 850);
          } else if (subPage === 1) {
            const val = parseFloat(Math.max(0.1, nextNorm * 20.0).toFixed(1));
            onPatchChange({ ...patch, lfo2Rate: val });
            showHud({ kind: 'param', label: 'LFO 2 RATE', value: `${val}Hz`, ratio: nextNorm, accent: '#A78BFA', knobIdx: 1 }, 850);
          } else {
            const val = parseFloat(Math.max(0.1, nextNorm * 20.0).toFixed(1));
            onPatchChange({ ...patch, lfoRate: val });
            showHud({ kind: 'param', label: 'LFO 1 RATE', value: `${val}Hz`, ratio: nextNorm, accent: '#34D399', knobIdx: 1 }, 850);
          }
        } else if (activePage === 'FX') {
          if (subPage === 1) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, fxDelayMix: val });
            showHud({ kind: 'param', label: 'DELAY MIX', value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
          } else {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, fxChorus: val });
            showHud({ kind: 'param', label: 'CHORUS MIX', value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
          }
        } else if (activePage === 'ARP') {
          if (subPage === 1) {
            if (discreteSteps === 0) return;
            const curOct = Math.max(1, Math.min(3, patch.arpOctaves || 1));
            const nextOct = Math.max(1, Math.min(3, curOct + discreteSteps)) as 1 | 2 | 3;
            onPatchChange({ ...patch, arpOctaves: nextOct });
            showHud({ kind: 'param', label: 'ARP OCTAVES', value: `${nextOct} OCT`, ratio: (nextOct - 1) / 2, accent: '#34D399', knobIdx: 1 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const nextEnabled = !patch.arpEnabled;
            const nextLatch = nextEnabled ? Boolean(patch.arpLatch) : false;
            onPatchChange({ ...patch, arpEnabled: nextEnabled, arpLatch: nextLatch });
            audioEngine.toggleArp(nextEnabled, nextLatch);
            showHud({ kind: 'param', label: 'ARP', value: nextEnabled ? 'ON' : 'OFF', ratio: nextEnabled ? 1 : 0, accent: '#34D399', knobIdx: 1 }, 850);
          }
        } else if (activePage === 'SEL') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, filterBalance: val });
            showHud({ kind: 'param', label: 'FILTER BAL', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 1 }, 850);
          } else if (subPage === 1) {
            const val = Math.round(60 + nextNorm * 18940);
            onPatchChange({ ...patch, filterCutoff: val });
            showHud({ kind: 'param', label: 'CUTOFF 1', value: `${val}Hz`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curIdx = Math.max(
              0,
              FILTER_COMBOS.findIndex(
                (c) => c.filterType === patch.filterType && (patch.filterRouting || 'serial') === c.filterRouting
              )
            );
            const next = FILTER_COMBOS[(((curIdx + discreteSteps) % FILTER_COMBOS.length) + FILTER_COMBOS.length) % FILTER_COMBOS.length];
            onPatchChange({
              ...patch,
              filterType: next.filterType,
              filterRouting: next.filterRouting,
              filter2Type: next.filter2Type
            });
            showHud({ kind: 'param', label: 'FILTER MODE', value: next.label, ratio: FILTER_COMBOS.indexOf(next) / (FILTER_COMBOS.length - 1), accent: '#22D3EE', knobIdx: 1 }, 900);
          }
        } else if (activePage === 'EDIT') {
          if (subPage === 2) {
            if (discreteSteps === 0) return;
            const nextSync = !patch.osc2Sync;
            onPatchChange({ ...patch, osc2Sync: nextSync });
            showHud({ kind: 'param', label: 'OSC 2 SYNC', value: nextSync ? 'ON' : 'OFF', ratio: nextSync ? 1 : 0, accent: '#F97316', knobIdx: 1 }, 850);
          } else if (subPage === 1) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, subOscLevel: val });
            showHud({ kind: 'param', label: 'SUB OSC LVL', value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
          } else {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, oscBalance: val });
            showHud({ kind: 'param', label: 'OSC 1/2 BAL', value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
          }
        } else if (activePage === 'SEQ') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onDrumVolumeChange(val);
            showHud({ kind: 'param', label: 'DRUM LEVEL', value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 1 }, 850);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const tr = drumTracks[selectedDrumTrack] || drumTracks[0];
            const nextP = Math.max(0, Math.min(seqLength, (tr?.euclidPulses ?? 0) + discreteSteps));
            onDrumEuclidChange(selectedDrumTrack, nextP, tr?.euclidRotate ?? 0);
            showHud({ kind: 'param', label: `${tr?.shortName || 'BD'} EUCLID`, value: `${nextP}/${seqLength}`, ratio: nextP / Math.max(1, seqLength), accent: '#34D399', knobIdx: 1 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const tr = drumTracks[selectedDrumTrack] || drumTracks[0];
            const nextTune = Math.max(-12, Math.min(12, (tr?.tune ?? 0) + discreteSteps));
            onDrumTrackParamChange(selectedDrumTrack, { tune: nextTune });
            if (!isPlayingSeq) audioEngine.triggerDrum(selectedDrumTrack, 102);
            showHud({ kind: 'param', label: `${tr?.shortName || 'BD'} TUNE`, value: `${nextTune >= 0 ? `+${nextTune}` : nextTune}st`, ratio: (nextTune + 12) / 24, accent: '#22D3EE', knobIdx: 1 }, 850);
          }
        } else if (activePage === 'GLO') {
          if (subPage === 2) {
            if (discreteSteps === 0) return;
            const curIdx = Math.max(0, RANDOM_PATCH_STYLES.indexOf(randStyle));
            const nextStyle = RANDOM_PATCH_STYLES[(((curIdx + discreteSteps) % RANDOM_PATCH_STYLES.length) + RANDOM_PATCH_STYLES.length) % RANDOM_PATCH_STYLES.length];
            setRandStyle(nextStyle);
            showHud({ kind: 'param', label: 'RAND STYLE', value: nextStyle, ratio: RANDOM_PATCH_STYLES.indexOf(nextStyle) / (RANDOM_PATCH_STYLES.length - 1), accent: '#FBBF24', knobIdx: 1 }, 850);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const curBars = Math.max(1, Math.min(4, Math.ceil(seqLength / 16)));
            const nextBars = Math.max(1, Math.min(4, curBars + discreteSteps));
            const nextLen = nextBars * 16;
            onSeqLengthChange(nextLen);
            showHud({ kind: 'param', label: 'REC / LOOP BARS', value: `${nextBars} BAR (${nextLen}ST)`, ratio: (nextBars - 1) / 3, accent: '#34D399', knobIdx: 1 }, 900);
          } else {
            if (discreteSteps === 0) return;
            const nextRoot = (((rootPc + discreteSteps) % 12) + 12) % 12;
            setRootPc(nextRoot);
            showHud({ kind: 'param', label: 'ROOT NOTE', value: NOTE_NAMES_12[nextRoot], ratio: nextRoot / 11, accent: '#22D3EE', knobIdx: 1 }, 850);
          }
        }
      } else if (knobName === 'KNOB2') {
        const curNorm = getKnobDef(2).val;
        const nextNorm = clamp01(curNorm + deltaNorm);
        if (activePage === 'HOME') {
          const val = parseFloat((0.5 + nextNorm * 17.5).toFixed(1));
          onPatchChange({ ...patch, filterReso: val });
          showHud({ kind: 'param', label: 'RESO 1', value: `${val}`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 2 }, 850);
        } else if (activePage === 'ENV') {
          lastEnvParamRef.current = { segment: 'D', expiresAt: performance.now() + 1400 };
          if (subPage === 1) {
            const val = parseFloat(Math.max(0.02, nextNorm * 2.5).toFixed(2));
            onPatchChange({ ...patch, filterDecay: val });
            showHud({ kind: 'param', label: 'FLT DECAY', value: `${val}s`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 2 }, 900);
          } else {
            const val = parseFloat(Math.max(0.02, nextNorm * 3.0).toFixed(2));
            onPatchChange({ ...patch, decay: val });
            showHud({ kind: 'param', label: 'AMP DECAY', value: `${val}s`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 2 }, 900);
          }
        } else if (activePage === 'LFO') {
          if (subPage === 2) {
            const val = parseFloat(Math.max(0.05, nextNorm * 4.0).toFixed(2));
            onPatchChange({ ...patch, swarmRate: val });
            showHud({ kind: 'param', label: 'SWARM RATE', value: `${val}Hz`, ratio: nextNorm, accent: '#34D399', knobIdx: 2 }, 850);
          } else if (subPage === 1) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, lfo2Depth: val });
            showHud({ kind: 'param', label: 'LFO 2 AMOUNT', value: `${val}%`, ratio: nextNorm, accent: '#A78BFA', knobIdx: 2 }, 850);
          } else {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, lfoDepth: val });
            showHud({ kind: 'param', label: 'LFO 1 AMOUNT', value: `${val}%`, ratio: nextNorm, accent: '#34D399', knobIdx: 2 }, 850);
          }
        } else if (activePage === 'FX') {
          if (subPage === 1) {
            const val = Math.round(nextNorm * 95);
            onPatchChange({ ...patch, fxDelayFeedback: val });
            showHud({ kind: 'param', label: 'DELAY FDBK', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 2 }, 850);
          } else {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, fxPhaserMix: val });
            showHud({ kind: 'param', label: 'PHASER MIX', value: `${val}%`, ratio: nextNorm, accent: '#34D399', knobIdx: 2 }, 850);
          }
        } else if (activePage === 'ARP') {
          if (subPage === 1) {
            const val = Math.round(20 + nextNorm * 75);
            onPatchChange({ ...patch, arpGate: val });
            showHud({ kind: 'param', label: 'ARP GATE', value: `${val}%`, ratio: nextNorm, accent: '#34D399', knobIdx: 2 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const nextLatch = !patch.arpLatch;
            const nextEnabled = nextLatch ? true : Boolean(patch.arpEnabled);
            onPatchChange({ ...patch, arpEnabled: nextEnabled, arpLatch: nextLatch });
            audioEngine.toggleArp(nextEnabled, nextLatch);
            showHud({ kind: 'param', label: 'ARP LATCH', value: nextLatch ? 'ON' : 'OFF', ratio: nextLatch ? 1 : 0, accent: '#FBBF24', knobIdx: 2 }, 850);
          }
        } else if (activePage === 'SEL') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 200 - 100);
            onPatchChange({ ...patch, filterEnvAmount: val });
            showHud({ kind: 'param', label: 'FILTER ENV', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 2 }, 850);
          } else if (subPage === 1) {
            const val = parseFloat((0.5 + nextNorm * 17.5).toFixed(1));
            onPatchChange({ ...patch, filterReso: val });
            showHud({ kind: 'param', label: 'RESO 1', value: `${val}`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 2 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curR = Math.max(0, FILTER_ROUTINGS.indexOf(patch.filterRouting || 'serial'));
            const nextR = FILTER_ROUTINGS[(((curR + discreteSteps) % 3) + 3) % 3];
            onPatchChange({ ...patch, filterRouting: nextR });
            showHud({ kind: 'param', label: 'FLT ROUTING', value: nextR.toUpperCase(), ratio: FILTER_ROUTINGS.indexOf(nextR) / 2, accent: '#22D3EE', knobIdx: 2 }, 850);
          }
        } else if (activePage === 'EDIT') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, fmAmount: val });
            showHud({ kind: 'param', label: 'OSC FM AMT', value: `${val}%`, ratio: nextNorm, accent: '#F97316', knobIdx: 2 }, 850);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const curW = Math.max(0, SUB_WAVES.indexOf(patch.subWaveform || 'square'));
            const nextW = SUB_WAVES[(((curW + discreteSteps) % SUB_WAVES.length) + SUB_WAVES.length) % SUB_WAVES.length];
            onPatchChange({ ...patch, subWaveform: nextW });
            showHud({ kind: 'param', label: 'SUB WAVEFORM', value: nextW.toUpperCase(), ratio: SUB_WAVES.indexOf(nextW) / 2, accent: '#22D3EE', knobIdx: 2 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const nextSemi = Math.max(-24, Math.min(24, (patch.osc2Semitone ?? 0) + discreteSteps));
            onPatchChange({ ...patch, osc2Semitone: nextSemi });
            showHud({ kind: 'param', label: 'OSC 2 PITCH', value: `${nextSemi >= 0 ? `+${nextSemi}` : nextSemi}st`, ratio: (nextSemi + 24) / 48, accent: '#22D3EE', knobIdx: 2 }, 850);
          }
        } else if (activePage === 'SEQ') {
          if (subPage === 2) {
            const nextBpm = Math.round(40 + nextNorm * 200);
            onBpmChange(nextBpm);
            showHud({ kind: 'param', label: 'MASTER CLOCK', value: `${nextBpm} BPM`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 2 }, 850);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const tr = drumTracks[selectedDrumTrack] || drumTracks[0];
            const maxRot = Math.max(1, seqLength);
            const nextRot = ((((tr?.euclidRotate ?? 0) + discreteSteps) % maxRot) + maxRot) % maxRot;
            onDrumEuclidChange(selectedDrumTrack, tr?.euclidPulses ?? 0, nextRot);
            showHud({ kind: 'param', label: `${tr?.shortName || 'BD'} ROTATE`, value: `+${nextRot}`, ratio: nextRot / Math.max(1, seqLength - 1), accent: '#22D3EE', knobIdx: 2 }, 850);
          } else {
            const tr = drumTracks[selectedDrumTrack] || drumTracks[0];
            const val = Math.round(25 + nextNorm * 175);
            onDrumTrackParamChange(selectedDrumTrack, { decay: val });
            showHud({ kind: 'param', label: `${tr?.shortName || 'BD'} DECAY`, value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 2 }, 850);
          }
        } else if (activePage === 'GLO') {
          if (subPage === 2) {
            if (discreteSteps === 0) return;
            handleGenerateRandomPatch();
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const nextPoly = Math.max(1, Math.min(8, (patch.polyphony || 8) + discreteSteps));
            onPatchChange({ ...patch, polyphony: nextPoly });
            showHud({ kind: 'param', label: 'POLYPHONY', value: `${nextPoly} VOICES`, ratio: (nextPoly - 1) / 7, accent: '#22D3EE', knobIdx: 2 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curS = Math.max(0, SCALE_MODES.indexOf(scaleMode));
            const nextS = SCALE_MODES[(((curS + discreteSteps) % SCALE_MODES.length) + SCALE_MODES.length) % SCALE_MODES.length];
            setScaleMode(nextS);
            showHud({ kind: 'param', label: 'KEYBED SCALE', value: SCALE_LABELS[nextS].toUpperCase(), ratio: SCALE_MODES.indexOf(nextS) / (SCALE_MODES.length - 1), accent: '#22D3EE', knobIdx: 2 }, 900);
          }
        }
      } else if (knobName === 'KNOB3') {
        const curNorm = getKnobDef(3).val;
        const nextNorm = clamp01(curNorm + deltaNorm);
        if (activePage === 'HOME') {
          const val = Math.round(60 + nextNorm * 18940);
          onPatchChange({ ...patch, filter2Cutoff: val });
          showHud({ kind: 'param', label: 'CUTOFF 2', value: `${val}Hz`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
        } else if (activePage === 'ENV') {
          lastEnvParamRef.current = { segment: 'S', expiresAt: performance.now() + 1400 };
          if (subPage === 1) {
            const val = parseFloat(nextNorm.toFixed(2));
            onPatchChange({ ...patch, filterSustain: val });
            showHud({ kind: 'param', label: 'FLT SUSTAIN', value: `${Math.round(val * 100)}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 900);
          } else {
            const val = parseFloat(nextNorm.toFixed(2));
            onPatchChange({ ...patch, sustain: val });
            showHud({ kind: 'param', label: 'AMP SUSTAIN', value: `${Math.round(val * 100)}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 900);
          }
        } else if (activePage === 'LFO') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 85);
            onPatchChange({ ...patch, glide: val });
            showHud({ kind: 'param', label: 'PORTAMENTO', value: `${val}`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const curD = Math.max(0, LFO_DESTS.indexOf(patch.lfo2Dest || 'pan'));
            const dst = LFO_DESTS[(((curD + discreteSteps) % LFO_DESTS.length) + LFO_DESTS.length) % LFO_DESTS.length];
            onPatchChange({ ...patch, lfo2Dest: dst });
            showHud({ kind: 'param', label: 'LFO 2 DEST', value: LFO_DEST_LABELS[dst], ratio: LFO_DESTS.indexOf(dst) / (LFO_DESTS.length - 1), accent: '#A78BFA', knobIdx: 3 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curD = Math.max(0, LFO_DESTS.indexOf(patch.lfoDest || 'filter'));
            const dst = LFO_DESTS[(((curD + discreteSteps) % LFO_DESTS.length) + LFO_DESTS.length) % LFO_DESTS.length];
            onPatchChange({ ...patch, lfoDest: dst });
            showHud({ kind: 'param', label: 'LFO 1 DEST', value: LFO_DEST_LABELS[dst], ratio: LFO_DESTS.indexOf(dst) / (LFO_DESTS.length - 1), accent: '#FBBF24', knobIdx: 3 }, 850);
          }
        } else if (activePage === 'FX') {
          if (subPage === 1) {
            const val = parseFloat(Math.max(0.1, nextNorm * 6.0).toFixed(1));
            onPatchChange({ ...patch, fxPhaserRate: val });
            showHud({ kind: 'param', label: 'PHASER RATE', value: `${val}Hz`, ratio: nextNorm, accent: '#34D399', knobIdx: 3 }, 850);
          } else {
            const val = parseFloat((nextNorm * 1.0).toFixed(2));
            onPatchChange({ ...patch, fxDelayTime: val });
            showHud({ kind: 'param', label: 'DELAY TIME', value: `${val}s`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          }
        } else if (activePage === 'ARP') {
          if (subPage === 1) {
            const nextSw = Math.round(50 + nextNorm * 20);
            onSwingChange(nextSw);
            showHud({ kind: 'param', label: 'ARP SWING', value: `${nextSw}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curM = Math.max(0, arpModes.indexOf(patch.arpMode || 'updown'));
            const m = arpModes[(((curM + discreteSteps) % 4) + 4) % 4];
            onPatchChange({ ...patch, arpMode: m });
            showHud({ kind: 'param', label: 'ARP PATTERN', value: m.toUpperCase(), ratio: arpModes.indexOf(m) / 3, accent: '#22D3EE', knobIdx: 3 }, 850);
          }
        } else if (activePage === 'SEL') {
          if (subPage === 2) {
            const val = parseFloat(Math.max(0.02, nextNorm * 2.5).toFixed(2));
            onPatchChange({ ...patch, filterRelease: val });
            showHud({ kind: 'param', label: 'FILTER RELEASE', value: `${val}s`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else if (subPage === 1) {
            const val = Math.round(60 + nextNorm * 18940);
            onPatchChange({ ...patch, filter2Cutoff: val });
            showHud({ kind: 'param', label: 'CUTOFF 2', value: `${val}Hz`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curT = Math.max(0, FILTER_TYPES.indexOf(patch.filterType || 'lowpass'));
            const nextT = FILTER_TYPES[(((curT + discreteSteps) % 4) + 4) % 4];
            onPatchChange({ ...patch, filterType: nextT });
            showHud({ kind: 'param', label: 'FILTER 1 TYPE', value: nextT.toUpperCase(), ratio: FILTER_TYPES.indexOf(nextT) / 3, accent: '#22D3EE', knobIdx: 3 }, 850);
          }
        } else if (activePage === 'EDIT') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, unisonPanSpread: val });
            showHud({ kind: 'param', label: 'UNISON PAN', value: `${val}%`, ratio: nextNorm, accent: '#F97316', knobIdx: 3 }, 850);
          } else if (subPage === 1) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, noiseVolume: val });
            showHud({ kind: 'param', label: 'NOISE GEN', value: `${val}%`, ratio: nextNorm, accent: '#22D3EE', knobIdx: 3 }, 850);
          } else {
            const mode = patch.oscMode || 'hypersaw';
            if (mode === 'wavetable') {
              const val = parseFloat(nextNorm.toFixed(2));
              onPatchChange({ ...patch, wavetablePos: val });
              showHud({ kind: 'param', label: 'WAVETABLE', value: `${Math.round(val * 100)}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
            } else if (mode === 'pulse') {
              const val = parseFloat((0.05 + nextNorm * 0.9).toFixed(2));
              onPatchChange({ ...patch, pulseWidth: val });
              showHud({ kind: 'param', label: 'PULSE WIDTH', value: `${Math.round(val * 100)}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
            } else {
              const val = Math.round(nextNorm * 100);
              onPatchChange({ ...patch, hypersawSpread: val });
              showHud({ kind: 'param', label: 'DETUNE SPREAD', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
            }
          }
        } else if (activePage === 'SEQ') {
          if (subPage === 2) {
            if (discreteSteps === 0) return;
            const curBar = Math.floor(selectedStepIdx / 16);
            const nextBar = (((curBar + discreteSteps) % 4) + 4) % 4;
            const target = nextBar * 16 + (selectedStepIdx % 16);
            if (target + 1 > seqLength) onSeqLengthChange((nextBar + 1) * 16);
            setSelectedStepIdx(target);
            showHud({ kind: 'param', label: 'VIEW BAR', value: `BAR ${nextBar + 1}`, ratio: nextBar / 3, accent: '#FBBF24', knobIdx: 3 }, 800);
          } else if (subPage === 1) {
            const nextSw = Math.round(50 + nextNorm * 25);
            onSwingChange(nextSw);
            showHud({ kind: 'param', label: 'DRUM SWING', value: `${nextSw}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else {
            const tr = drumTracks[selectedDrumTrack] || drumTracks[0];
            const val = Math.round(nextNorm * 100);
            onDrumTrackParamChange(selectedDrumTrack, { tone: val });
            showHud({ kind: 'param', label: `${tr?.shortName || 'BD'} TONE`, value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          }
        } else if (activePage === 'GLO') {
          if (subPage === 2) {
            const nextChaos = Math.round(20 + nextNorm * 80);
            setRandChaos(nextChaos);
            showHud({ kind: 'param', label: 'RAND CHAOS', value: `${nextChaos}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else if (subPage === 1) {
            const nextSw = Math.round(50 + nextNorm * 25);
            onSwingChange(nextSw);
            showHud({ kind: 'param', label: 'GLOBAL SWING', value: `${nextSw}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          } else {
            const nextBpm = Math.round(40 + nextNorm * 200);
            onBpmChange(nextBpm);
            showHud({ kind: 'param', label: 'TEMPO', value: `${nextBpm} BPM`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 3 }, 850);
          }
        }
      } else if (knobName === 'KNOB4') {
        const curNorm = getKnobDef(4).val;
        const nextNorm = clamp01(curNorm + deltaNorm);
        if (activePage === 'HOME') {
          const val = Math.round(nextNorm * 100);
          onPatchChange({ ...patch, filterBalance: val });
          showHud({ kind: 'param', label: 'FILTER BAL', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
        } else if (activePage === 'ENV') {
          lastEnvParamRef.current = { segment: 'R', expiresAt: performance.now() + 1400 };
          if (subPage === 1) {
            const val = Math.round(nextNorm * 200 - 100);
            onPatchChange({ ...patch, filterEnvAmount: val });
            showHud({ kind: 'param', label: 'FILTER ENV', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 900);
          } else {
            const val = parseFloat(Math.max(0.02, nextNorm * 3.0).toFixed(2));
            onPatchChange({ ...patch, release: val });
            showHud({ kind: 'param', label: 'AMP RELEASE', value: `${val}s`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 900);
          }
        } else if (activePage === 'LFO') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, filterKeyfollow: val });
            showHud({ kind: 'param', label: 'KEYFOLLOW', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const curS = Math.max(0, shapes.indexOf(patch.lfo2Shape || 'triangle'));
            const shp = shapes[(((curS + discreteSteps) % 4) + 4) % 4];
            onPatchChange({ ...patch, lfo2Shape: shp });
            showHud({ kind: 'param', label: 'LFO 2 WAVE', value: shp.toUpperCase(), ratio: shapes.indexOf(shp) / 3, accent: '#A78BFA', knobIdx: 4 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curS = Math.max(0, shapes.indexOf(patch.lfoShape || 'sine'));
            const shp = shapes[(((curS + discreteSteps) % 4) + 4) % 4];
            onPatchChange({ ...patch, lfoShape: shp });
            showHud({ kind: 'param', label: 'LFO 1 WAVE', value: shp.toUpperCase(), ratio: shapes.indexOf(shp) / 3, accent: '#FBBF24', knobIdx: 4 }, 850);
          }
        } else if (activePage === 'FX') {
          if (subPage === 1) {
            if (discreteSteps === 0) return;
            const curSat = Math.max(0, SAT_TYPES.indexOf(patch.saturationType || 'soft'));
            const nextSat = SAT_TYPES[(((curSat + discreteSteps) % SAT_TYPES.length) + SAT_TYPES.length) % SAT_TYPES.length];
            onPatchChange({ ...patch, saturationType: nextSat });
            showHud({ kind: 'param', label: 'SATURATOR', value: nextSat.toUpperCase(), ratio: SAT_TYPES.indexOf(nextSat) / 3, accent: '#F97316', knobIdx: 4 }, 850);
          } else {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, fxReverbSize: val });
            showHud({ kind: 'param', label: 'REVERB MIX', value: `${val}%`, ratio: nextNorm, accent: '#F97316', knobIdx: 4 }, 850);
          }
        } else if (activePage === 'ARP') {
          if (subPage === 1) {
            const nextBpm = Math.round(40 + nextNorm * 200);
            onBpmChange(nextBpm);
            showHud({ kind: 'param', label: 'TEMPO', value: `${nextBpm} BPM`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curR = Math.max(0, arpRates.indexOf(patch.arpRate || '1/16'));
            const nxtR = arpRates[(((curR + discreteSteps) % 4) + 4) % 4];
            onPatchChange({ ...patch, arpRate: nxtR });
            showHud({ kind: 'param', label: 'ARP RATE', value: nxtR, ratio: arpRates.indexOf(nxtR) / 3, accent: '#FBBF24', knobIdx: 4 }, 850);
          }
        } else if (activePage === 'SEL') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, filterKeyfollow: val });
            showHud({ kind: 'param', label: 'KEYFOLLOW', value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
          } else if (subPage === 1) {
            const val = parseFloat((0.5 + nextNorm * 17.5).toFixed(1));
            onPatchChange({ ...patch, filter2Reso: val });
            showHud({ kind: 'param', label: 'FILTER 2 RESO', value: `${val}`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curT = Math.max(0, FILTER_TYPES.indexOf(patch.filter2Type || 'lowpass'));
            const nextT = FILTER_TYPES[(((curT + discreteSteps) % 4) + 4) % 4];
            onPatchChange({ ...patch, filter2Type: nextT });
            showHud({ kind: 'param', label: 'FILTER 2 TYPE', value: nextT.toUpperCase(), ratio: FILTER_TYPES.indexOf(nextT) / 3, accent: '#FBBF24', knobIdx: 4 }, 850);
          }
        } else if (activePage === 'EDIT') {
          if (subPage === 2) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, fxDrive: val });
            showHud({ kind: 'param', label: 'SAT DRIVE', value: `${val}%`, ratio: nextNorm, accent: '#F97316', knobIdx: 4 }, 850);
          } else if (subPage === 1) {
            const val = Math.round(nextNorm * 100);
            onPatchChange({ ...patch, ringModVolume: val });
            showHud({ kind: 'param', label: 'RING MOD', value: `${val}%`, ratio: nextNorm, accent: '#F97316', knobIdx: 4 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curU = Math.max(1, Math.min(5, patch.unisonVoices ?? 1));
            const uni = Math.max(1, Math.min(5, curU + discreteSteps));
            onPatchChange({ ...patch, unisonVoices: uni });
            showHud({ kind: 'param', label: 'UNISON VOICES', value: `${uni}V`, ratio: (uni - 1) / 4, accent: '#22D3EE', knobIdx: 4 }, 850);
          }
        } else if (activePage === 'SEQ') {
          if (subPage === 2) {
            if (discreteSteps === 0) return;
            const curIdx = DRUM_PRESET_ORDER.indexOf(drumKitPreset);
            const nextPreset =
              curIdx < 0
                ? DRUM_PRESET_ORDER[0]
                : DRUM_PRESET_ORDER[(((curIdx + discreteSteps) % DRUM_PRESET_ORDER.length) + DRUM_PRESET_ORDER.length) % DRUM_PRESET_ORDER.length];
            onSelectDrumKitPreset(nextPreset);
            showHud({ kind: 'param', label: 'KIT PATTERN', value: nextPreset, ratio: DRUM_PRESET_ORDER.indexOf(nextPreset) / (DRUM_PRESET_ORDER.length - 1), accent: '#34D399', knobIdx: 4 }, 900);
          } else if (subPage === 1) {
            if (discreteSteps === 0) return;
            const nextLen = Math.max(1, Math.min(64, seqLength + discreteSteps));
            onSeqLengthChange(nextLen);
            showHud({ kind: 'param', label: 'DRUM LENGTH', value: `${nextLen} STEPS`, ratio: (nextLen - 1) / 63, accent: '#FBBF24', knobIdx: 4 }, 850);
          } else {
            const tr = drumTracks[selectedDrumTrack] || drumTracks[0];
            const val = Math.round(nextNorm * 100);
            onDrumTrackParamChange(selectedDrumTrack, { level: val });
            showHud({ kind: 'param', label: `${tr?.shortName || 'BD'} LEVEL`, value: `${val}%`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
          }
        } else if (activePage === 'GLO') {
          if (subPage === 2) {
            if (discreteSteps === 0) return;
            const nextSlot = (((targetBankXSlot + discreteSteps) % 128) + 128) % 128;
            setTargetBankXSlot(nextSlot);
            const existing = getBankPresets('X')[nextSlot];
            const slotStr = `X${nextSlot.toString().padStart(3, '0')}`;
            const status = existing?.isUserPreset ? `${slotStr} (${existing.name.slice(0, 8)})` : `${slotStr} (EMPTY)`;
            showHud({ kind: 'param', label: 'BANK X SLOT', value: status, ratio: nextSlot / 127, accent: '#34D399', knobIdx: 4 }, 900);
          } else if (subPage === 1) {
            const val = Math.round(nextNorm * 85);
            onPatchChange({ ...patch, glide: val });
            showHud({ kind: 'param', label: 'GLIDE / PORTA', value: `${val}`, ratio: nextNorm, accent: '#FBBF24', knobIdx: 4 }, 850);
          } else {
            if (discreteSteps === 0) return;
            const curC = Math.max(0, CHORD_MODES.indexOf(chordMode));
            const nextC = CHORD_MODES[(((curC + discreteSteps) % CHORD_MODES.length) + CHORD_MODES.length) % CHORD_MODES.length];
            setChordMode(nextC);
            showHud({ kind: 'param', label: 'CHORD MEMORY', value: CHORD_LABELS[nextC].toUpperCase(), ratio: CHORD_MODES.indexOf(nextC) / (CHORD_MODES.length - 1), accent: '#FBBF24', knobIdx: 4 }, 850);
          }
        }
      }
    },
    [
      volumePct,
      activePage,
      subPage,
      activeBank,
      bankPresets,
      currentProgramIndex,
      confirmPrompt,
      selectedDrumTrack,
      selectedStepIdx,
      seqLength,
      drumTracks,
      drumKitPreset,
      drumKitIndex,
      drumBusMode,
      isPlayingSeq,
      octaveOffset,
      rootPc,
      scaleMode,
      chordMode,
      randStyle,
      randChaos,
      targetBankXSlot,
      patch,
      onPatchChange,
      handleSelectPreset,
      handleGenerateRandomPatch,
      applyOctaveChange,
      onBpmChange,
      onSwingChange,
      onDrumVolumeChange,
      onDrumKitChange,
      onDrumBusModeChange,
      onSelectDrumKitPreset,
      onSeqLengthChange,
      onDrumEuclidChange,
      onDrumTrackParamChange,
      getKnobDef,
      showHud,
      touchMenuActivity
    ]
  );

  const encoderHandlerRef = useRef(applyEncoderStep);
  useEffect(() => {
    encoderHandlerRef.current = applyEncoderStep;
  }, [applyEncoderStep]);

  const handleKnobMouseDown = (knobName: string, e: React.MouseEvent) => {
    e.preventDefault();
    if (!isPoweredOn) return;
    audioEngine.initAudio();
    touchMenuActivity();
    setDraggingKnob(knobName);
    dragLastY.current = e.clientY;
    dragStepAccum.current = 0;
  };

  // Endless encoder detent notches on mouse wheel
  const handleKnobWheel = (knobName: string, e: React.WheelEvent) => {
    e.preventDefault();
    if (!isPoweredOn) return;
    audioEngine.initAudio();
    touchMenuActivity();
    const dir = e.deltaY < 0 ? 1 : -1;
    setKnobAngles((prev) => ({ ...prev, [knobName]: (prev[knobName] || 0) + dir * 18 }));
    encoderHandlerRef.current(knobName, dir * 0.04, dir);
  };

  useEffect(() => {
    const handleMouseMove = (e: MouseEvent) => {
      if (!draggingKnob) return;
      touchMenuActivity();
      const dy = dragLastY.current - e.clientY;
      dragLastY.current = e.clientY;
      if (dy === 0) return;

      // Rotate endless encoder cap freely 360 degrees
      setKnobAngles((prev) => ({ ...prev, [draggingKnob]: (prev[draggingKnob] || 0) + dy * 2.6 }));

      // Accumulate pixels for discrete-detent encoders (SELECT, PRESETS, ALGORITHM, stepped params)
      const pxPerDetent = draggingKnob === 'SELECT' ? 10 : 18;
      dragStepAccum.current += dy;
      let steps = 0;
      if (Math.abs(dragStepAccum.current) >= pxPerDetent) {
        steps = Math.trunc(dragStepAccum.current / pxPerDetent);
        dragStepAccum.current -= steps * pxPerDetent;
      }

      encoderHandlerRef.current(draggingKnob, dy / 140, steps);
    };

    const handleMouseUp = () => {
      touchMenuActivity();
      setDraggingKnob(null);
    };

    if (draggingKnob) {
      window.addEventListener('mousemove', handleMouseMove);
      window.addEventListener('mouseup', handleMouseUp);
    }
    return () => {
      window.removeEventListener('mousemove', handleMouseMove);
      window.removeEventListener('mouseup', handleMouseUp);
    };
  }, [draggingKnob, touchMenuActivity]);

  // Exact M-VAVE FM-1 Hardware Geometry Constants (1:1 from charlesvestal/fm1-base web/fm1panel.js)
  const TRAY = '#22252A';
  const KEY_W = '#DCE0E5';
  const KEY_B = '#B8BDC6';
  const KNOB = '#1B1D21';
  const BTNC = '#2C3036';
  const LABEL = '#E8ECF1';

  const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];
  const ROW1 = ['FX', 'SEL', 'ENV', 'LFO', 'EDIT', 'GLO'] as const;
  const ROW2 = ['HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC'] as const;

  const keyPitch = (928 - 24) / 16; // 56.5
  const keyWidth = keyPitch * 0.78;  // 44.07
  const keyRx = keyWidth / 2;        // 22.035

  const handleDownloadFwsc = () => {
    const modCords = getEffectiveModMatrix(patch);
    const oscModeMap: Record<string, number> = {
      hypersaw: 0,
      saw: 1,
      pulse: 2,
      wavetable: 3,
      sync: 4,
      triangle: 5
    };
    const exportPresets = [
      patch,
      ...bankPresets.filter((p) => p.id !== patch.id).slice(0, 31)
    ];
    const presetRows = exportPresets.map((p) => {
      const fType = p.filterType === 'bandpass' ? 1 : p.filterType === 'highpass' ? 2 : p.filterType === 'notch' ? 3 : 0;
      const fRoute = p.filterRouting === 'parallel' ? 1 : p.filterRouting === 'split' ? 2 : 0;
      const lDest = p.lfoDest === 'pitch' ? 1 : p.lfoDest === 'pan' ? 2 : p.lfoDest === 'detune' ? 3 : 0;
      const lShape = p.lfoShape === 'triangle' ? 1 : p.lfoShape === 'saw' ? 2 : p.lfoShape === 'square' ? 3 : 0;
      const oMode = oscModeMap[p.oscMode || 'hypersaw'] ?? 0;
      return `    {"${p.name.slice(0, 23)}", "${p.category.slice(0, 9)}", ${p.filterCutoff.toFixed(1)}f, ${p.filterReso.toFixed(1)}f, ${(p.filter2Cutoff ?? 9500).toFixed(1)}f, ${(p.filter2Reso ?? 1.5).toFixed(1)}f, ${(p.filterBalance ?? 50).toFixed(1)}f, ${(p.fxDrive ?? 35).toFixed(1)}f, ${(p.hypersawSpread ?? 80).toFixed(1)}f, ${(p.subOscLevel ?? 70).toFixed(1)}f, ${(p.fxPhaserMix ?? 0).toFixed(1)}f, ${(p.ringModVolume ?? 0).toFixed(1)}f, ${(p.attack ?? 0.008).toFixed(3)}f, ${(p.decay ?? 0.85).toFixed(2)}f, ${(p.sustain ?? 0.75).toFixed(2)}f, ${(p.release ?? 0.35).toFixed(2)}f, ${(p.filterEnvAmount ?? 45).toFixed(1)}f, ${(p.filterDecay ?? 0.42).toFixed(2)}f, ${(p.lfoRate ?? 4.5).toFixed(1)}f, ${(p.lfoDepth ?? 18).toFixed(1)}f, ${(p.swarmDepth ?? 24).toFixed(1)}f, ${p.osc2Semitone ?? 0}, ${lDest}, ${lShape}, ${fType}, ${fRoute}, ${oMode}, ${Math.max(1, Math.min(5, p.unisonVoices ?? 1))}, ${p.osc2Sync ? 1 : 0}, ${p.arpEnabled ? 1 : 0}}`;
    }).join(',\n');

    const srcMap: Record<ModSource, number> = { lfo1: 0, swarm: 1, filterEnv: 2, velocity: 3 };
    const dstMap: Record<ModDest, number> = { cutoff: 0, reso: 1, spread: 2, pitch: 3, drive: 4, pan: 5 };
    const cordRows = modCords.map((c) => `    {${srcMap[c.source]}, ${dstMap[c.dest]}, ${(c.amount / 100).toFixed(2)}f}`).join(',\n');

    const masterQ12 = Math.round((volumePct / 100) * 4096);

    const fullSource = `/* SPDX-License-Identifier: MPL-2.0
 * Virus-FM1 v3.0 (Virus B/C ROM Bank ${activeBank} + Dual-Filter & Phaser) on charlesvestal/fm1-base
 * Target Hardware: JieLi AC791N SoC (240MHz RISC, 256KB SRAM, 1024KB SPI NOR Flash)
 */
#include "plat.h"
#include "fastmath.h"

#define MAX_VOICES 8
#define NUM_OSCS 7
#define NUM_PRESETS ${exportPresets.length}
#define NUM_CORDS 4

static const float PAN_L[NUM_OSCS] = {0.95f, 0.85f, 0.65f, 0.50f, 0.35f, 0.15f, 0.05f};
static const float PAN_R[NUM_OSCS] = {0.05f, 0.15f, 0.35f, 0.50f, 0.65f, 0.85f, 0.95f};

typedef struct { uint8_t src; uint8_t dst; float amount; } mod_cord_t;

typedef struct {
    char name[24];
    char category[10];
    float cutoff, resonance, cutoff2, res2, filter_bal;
    float drive, hypersaw, sub_level, phaser_mix, ring_mix;
    float attack, decay, sustain, release, filter_env_amt, filter_decay;
    float lfo_rate, lfo_depth, swarm_depth;
    int8_t osc2_semi;
    uint8_t lfo_dest, lfo_shape, filter_type, filter_routing, osc_mode, unison_voices, sync_enable, arp_enable;
} virus_patch_t;

static const virus_patch_t FACTORY_PRESETS[NUM_PRESETS] = {
${presetRows}
};

static mod_cord_t mod_cords[NUM_CORDS] = {
${cordRows}
};

static virus_patch_t cur_patch;
static uint8_t active_preset_idx = 0;
static uint32_t master_volume_q12 = ${masterQ12};

void app_init(void) { cur_patch = FACTORY_PRESETS[0]; }
void app_panic(void) {}
void app_button_event(uint32_t btn_id, uint32_t pressed) {
    if (!pressed) return;
    if (btn_id == 1) cur_patch.filter_routing = (cur_patch.filter_routing + 1) % 3;
    if (btn_id == 7) plat_store_write(0, &cur_patch, sizeof(cur_patch));
}
void app_encoder_event(uint32_t enc_id, int32_t delta) {
    if (enc_id == 0) master_volume_q12 = (uint32_t)fm_clamp((int32_t)master_volume_q12 + delta * 64, 0, 4096);
    if (enc_id == 1) { active_preset_idx = (active_preset_idx + delta + NUM_PRESETS) % NUM_PRESETS; cur_patch = FACTORY_PRESETS[active_preset_idx]; }
}
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12) {
    (void)master_q12;
    for (uint32_t i = 0; i < n; i++) { lr[2*i] = 0; lr[2*i+1] = 0; }
}
`;
    const compiled = compileFwscCode(fullSource, firmwareName, 'Community', 'v3.0.0');
    if (!compiled.binaryBlob) return;
    const blob = new Blob([compiled.binaryBlob as unknown as BlobPart], { type: 'application/octet-stream' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `${firmwareName.toLowerCase().replace(/[^a-z0-9_-]/g, '_')}_bank${activeBank}_v3.fwsc`;
    document.body.appendChild(a);
    a.click();
    document.body.removeChild(a);
    URL.revokeObjectURL(url);
  };

  const handleDownloadRepoZip = async () => {
    try {
      const res = await fetch('/starfish.zip', { method: 'HEAD' });
      if (!res.ok) {
        setMidiNotice('Repo zip not built locally — run scripts/export_github_zip.py');
        return;
      }
      const contentType = res.headers.get('content-type') || '';
      if (contentType.includes('text/html')) {
        setMidiNotice('Repo zip not built locally — run scripts/export_github_zip.py');
        return;
      }
      const a = document.createElement('a');
      a.href = '/starfish.zip';
      a.download = 'starfish.zip';
      document.body.appendChild(a);
      a.click();
      document.body.removeChild(a);
    } catch {
      setMidiNotice('Unable to download starfish.zip');
    }
  };

  return (
    <div className="w-full max-w-[1040px] flex flex-col items-center gap-4 select-none">
      {/* Top Bar: Power ON/OFF, 15% Default Volume Slider, MIDI Connect, Export .fwsc */}
      <div className="w-full flex flex-wrap items-center justify-between gap-3 px-4 py-2.5 bg-[#12161F] border border-slate-800/90 rounded-xl">
        {/* 1. Small Power ON/OFF Switch + Volume Slider */}
        <div className="flex items-center gap-3 min-w-[240px]">
          <button
            type="button"
            onClick={handleTogglePower}
            aria-pressed={isPoweredOn}
            title={isPoweredOn ? 'Turn Power Off' : 'Turn Power On'}
            className={`px-2 py-1 rounded-md border text-[11px] font-bold font-mono transition-colors flex items-center gap-1 whitespace-nowrap cursor-pointer ${
              isPoweredOn
                ? 'bg-emerald-500/15 border-emerald-400/60 text-emerald-300 hover:bg-emerald-500/25'
                : 'bg-rose-500/15 border-rose-500/50 text-rose-300 hover:bg-rose-500/25'
            }`}
          >
            <Power className="w-3 h-3 shrink-0" />
            <span>{isPoweredOn ? 'ON' : 'OFF'}</span>
          </button>

          <div className="flex items-center gap-2">
            <Volume2 className="w-4 h-4 text-cyan-400 shrink-0" />
            <input
              type="range"
              min={0}
              max={100}
              value={volumePct}
              aria-label="Master Volume"
              onChange={(e) => {
                audioEngine.initAudio();
                const val = parseInt(e.target.value, 10);
                setVolumePct(val);
                audioEngine.setMasterVolume(val / 100);
              }}
              className="w-28 sm:w-36 ti2-slider cursor-pointer"
            />
            <span className="text-xs font-mono font-semibold text-slate-200 tabular-nums w-9 text-right whitespace-nowrap">
              {volumePct}%
            </span>
          </div>
        </div>

        {/* 2. MIDI Connect & 3. Export .fwsc */}
        <div className="flex flex-wrap items-center gap-2">
          {midiConnected && (midiInputs.length > 0 || midiOutputs.length > 0) && (
            <div className="flex items-center gap-1.5 text-xs font-mono">
              {midiInputs.length > 0 && (
                <select
                  aria-label="MIDI Input Port"
                  value={selectedInputId}
                  onChange={(e) => {
                    setSelectedInputId(e.target.value);
                    midiManager.selectInput(e.target.value);
                  }}
                  className="bg-[#0A0C10] border border-slate-700 rounded-md px-2 py-1.5 text-xs text-slate-200 max-w-[155px] outline-none"
                >
                  <option value="">In: Off</option>
                  {midiInputs.map((d) => (
                    <option key={d.id} value={d.id}>
                      In: {d.name}
                    </option>
                  ))}
                </select>
              )}
              {midiOutputs.length > 0 && (
                <select
                  aria-label="MIDI Output Port"
                  value={selectedOutputId}
                  onChange={(e) => {
                    setSelectedOutputId(e.target.value);
                    midiManager.selectOutput(e.target.value);
                  }}
                  className="bg-[#0A0C10] border border-slate-700 rounded-md px-2 py-1.5 text-xs text-slate-200 max-w-[155px] outline-none"
                >
                  <option value="">Out: Off</option>
                  {midiOutputs.map((d) => (
                    <option key={d.id} value={d.id}>
                      Out: {d.name}
                    </option>
                  ))}
                </select>
              )}
            </div>
          )}

          {midiNotice && (
            <span className="text-xs font-mono text-amber-400 whitespace-nowrap">
              {midiNotice}
            </span>
          )}

          <button
            onClick={handleToggleMidiConnect}
            className={`px-3 py-1.5 rounded-lg border text-xs font-semibold font-mono transition-colors flex items-center gap-1.5 whitespace-nowrap cursor-pointer ${
              midiConnected
                ? 'bg-cyan-500/15 border-cyan-400/60 text-cyan-300 hover:bg-cyan-500/25'
                : 'bg-[#1A1F29] border-slate-700 text-slate-200 hover:border-slate-500 hover:text-white'
            }`}
          >
            <Cable className="w-3.5 h-3.5 shrink-0" />
            <span>{midiConnected ? 'MIDI Connected' : 'Connect MIDI'}</span>
          </button>

          <button
            onClick={handleDownloadFwsc}
            className="px-3.5 py-1.5 bg-emerald-600 hover:bg-emerald-500 text-white text-xs font-semibold font-mono rounded-lg transition-colors flex items-center gap-1.5 whitespace-nowrap cursor-pointer"
          >
            <Download className="w-3.5 h-3.5 shrink-0" />
            <span>Export .fwsc</span>
          </button>

          <button
            onClick={handleDownloadRepoZip}
            title="Download full Starfish GitHub repository (.zip)"
            className="px-3.5 py-1.5 bg-indigo-600 hover:bg-indigo-500 text-white text-xs font-semibold font-mono rounded-lg transition-colors flex items-center gap-1.5 whitespace-nowrap cursor-pointer"
          >
            <Archive className="w-3.5 h-3.5 shrink-0" />
            <span>Export GitHub .zip</span>
          </button>
        </div>
      </div>

      {/* 1:1 M-VAVE FM-1 Hardware Panel (charlesvestal/fm1-base fm1panel.js) */}
      <div className="w-full aspect-[1000/640] relative rounded-[44px] overflow-hidden shadow-[0_28px_80px_rgba(0,0,0,0.88)]">
        <svg viewBox="0 0 1000 640" className="w-full h-full block">
          <defs>
            <linearGradient id="fm1-chassis" x1="0" y1="0" x2="0" y2="1">
              <stop offset="0%" stopColor="#41454B" />
              <stop offset="50%" stopColor="#383B41" />
              <stop offset="100%" stopColor="#2F3237" />
            </linearGradient>
            <radialGradient id="fm1-knobcap" cx="35%" cy="30%" r="70%">
              <stop offset="0%" stopColor="#2E3238" />
              <stop offset="100%" stopColor="#16181C" />
            </radialGradient>
          </defs>

          {/* Exact Rounded Pocket Chassis */}
          <rect
            x={4}
            y={4}
            width={992}
            height={632}
            rx={44}
            fill="url(#fm1-chassis)"
            stroke="#525760"
            strokeWidth={1.5}
          />

          {/* 8 Hardware Endless Rotary Encoders */}
          {[
            { x: 105, y: 100, r: 24, name: 'MASTER', label: 'MASTER' },
            { x: 215, y: 100, r: 24, name: 'SELECT', label: 'SELECT' },
            { x: 105, y: 205, r: 24, name: 'PRESETS', label: 'PRESETS' },
            { x: 215, y: 205, r: 24, name: 'ALGORITHM', label: 'ALGORITHM' },
            { x: 610, y: 100, r: 22, name: 'KNOB1', label: 'KNOB 1' },
            { x: 714, y: 100, r: 22, name: 'KNOB2', label: 'KNOB 2' },
            { x: 818, y: 100, r: 22, name: 'KNOB3', label: 'KNOB 3' },
            { x: 922, y: 100, r: 22, name: 'KNOB4', label: 'KNOB 4' }
          ].map((k) => {
            const angle = knobAngles[k.name] || 0;
            const isDragging = draggingKnob === k.name;
            const dimpleX = k.x + Math.sin((angle * Math.PI) / 180) * (k.r - 8);
            const dimpleY = k.y - Math.cos((angle * Math.PI) / 180) * (k.r - 8);
            return (
              <g
                key={k.name}
                className="cursor-ns-resize group"
                onMouseDown={(e) => handleKnobMouseDown(k.name, e)}
                onWheel={(e) => handleKnobWheel(k.name, e)}
              >
                <circle cx={k.x} cy={k.y} r={k.r + 6} fill={TRAY} />
                <circle
                  cx={k.x}
                  cy={k.y}
                  r={k.r + 10}
                  fill="none"
                  stroke={isDragging ? '#22D3EE' : 'transparent'}
                  strokeWidth={2}
                />
                {/* Knurled 360° endless encoder outer ring */}
                <circle
                  cx={k.x}
                  cy={k.y}
                  r={k.r}
                  fill="url(#fm1-knobcap)"
                  stroke={isDragging ? '#22D3EE' : '#525862'}
                  strokeWidth={2}
                  strokeDasharray="3 3.2"
                  transform={`rotate(${angle} ${k.x} ${k.y})`}
                  className="group-hover:stroke-cyan-400 transition-colors"
                />
                {/* Machined inner cap */}
                <circle
                  cx={k.x}
                  cy={k.y}
                  r={k.r - 3.5}
                  fill="url(#fm1-knobcap)"
                  stroke="#363A42"
                  strokeWidth={1}
                />
                {/* Endless encoder finger dimple (spins 360° continuously) */}
                <circle
                  cx={dimpleX}
                  cy={dimpleY}
                  r={2.8}
                  fill={isDragging ? '#22D3EE' : '#94A3B8'}
                  stroke="#0F1115"
                  strokeWidth={0.8}
                />
                <circle cx={k.x} cy={k.y} r={k.r + 14} fill="transparent" />
                <text
                  x={k.x}
                  y={k.y - k.r - 12}
                  textAnchor="middle"
                  fontFamily="'Plus Jakarta Sans', sans-serif"
                  fontSize={13}
                  fontWeight={700}
                  letterSpacing={0.5}
                  fill={LABEL}
                  className="pointer-events-none select-none"
                >
                  {k.label}
                </text>
              </g>
            );
          })}

          {/* Octave Tray */}
          <rect x={58} y={258} width={210} height={58} rx={14} fill={TRAY} />

          {/* OCT- Fixed Hardware Silicone Button */}
          <g
            className="cursor-pointer"
            onPointerDown={() => handleButtonDown('OCT-')}
            onPointerUp={() => handleButtonUp('OCT-')}
            onPointerLeave={() => handleButtonUp('OCT-')}
          >
            <rect
              x={72}
              y={268}
              width={82}
              height={38}
              rx={9}
              fill={activeBtnMask & (1 << PANEL_BTN['OCT-']) ? '#f28a5a' : BTNC}
              stroke="#4a4d52"
              strokeWidth={1.5}
            />
            <text
              x={113}
              y={292}
              textAnchor="middle"
              fontFamily="'Plus Jakarta Sans', sans-serif"
              fontSize={14}
              fontWeight={700}
              fill={activeBtnMask & (1 << PANEL_BTN['OCT-']) ? '#2a140a' : LABEL}
              className="pointer-events-none"
            >
              OCT-
            </text>
          </g>

          {/* OCT+ Fixed Hardware Silicone Button */}
          <g
            className="cursor-pointer"
            onPointerDown={() => handleButtonDown('OCT+')}
            onPointerUp={() => handleButtonUp('OCT+')}
            onPointerLeave={() => handleButtonUp('OCT+')}
          >
            <rect
              x={170}
              y={268}
              width={82}
              height={38}
              rx={9}
              fill={activeBtnMask & (1 << PANEL_BTN['OCT+']) ? '#f28a5a' : BTNC}
              stroke="#4a4d52"
              strokeWidth={1.5}
            />
            <text
              x={211}
              y={292}
              textAnchor="middle"
              fontFamily="'Plus Jakarta Sans', sans-serif"
              fontSize={14}
              fontWeight={700}
              fill={activeBtnMask & (1 << PANEL_BTN['OCT+']) ? '#2a140a' : LABEL}
              className="pointer-events-none"
            >
              OCT+
            </text>
          </g>

          {/* Square 1.54" Display Bezel (236x236 centered at x=300, y=44) */}
          <rect
            x={300}
            y={44}
            width={236}
            height={236}
            rx={26}
            fill="#0D0F13"
            stroke="#4E535C"
            strokeWidth={1.5}
          />

          {/* Action Buttons Tray */}
          <rect x={568} y={160} width={400} height={140} rx={18} fill={TRAY} />

          {/* Row 1 Buttons: FX, SEL, ENV, LFO, EDIT, GLO */}
          {ROW1.map((bName, i) => {
            const isPageActive = isPoweredOn && activePage === bName;
            const isPressed = isPoweredOn && (activeBtnMask & (1 << PANEL_BTN[bName])) !== 0;
            const isLit = isPageActive || isPressed;
            const bx = 582 + i * 63;
            const by = 174;
            return (
              <g
                key={bName}
                className="cursor-pointer"
                onPointerDown={() => handleButtonDown(bName)}
                onPointerUp={() => handleButtonUp(bName)}
                onPointerLeave={() => handleButtonUp(bName)}
              >
                <rect
                  x={bx}
                  y={by}
                  width={54}
                  height={48}
                  rx={9}
                  fill={isLit ? '#22D3EE' : BTNC}
                  stroke={isLit ? '#67E8F9' : '#4a4d52'}
                  strokeWidth={1.5}
                />
                <text
                  x={bx + 27}
                  y={by + 29}
                  textAnchor="middle"
                  fontFamily="'Plus Jakarta Sans', sans-serif"
                  fontSize={14}
                  fontWeight={700}
                  fill={isLit ? '#07090E' : LABEL}
                  className="pointer-events-none"
                >
                  {bName}
                </text>
              </g>
            );
          })}

          {/* Row 2 Buttons: HOME, SAVE, ARP, SEQ, PLAY/STOP, REC */}
          {ROW2.map((bName, i) => {
            const isPageActive = isPoweredOn && activePage === bName;
            const isPressed = isPoweredOn && (activeBtnMask & (1 << PANEL_BTN[bName])) !== 0;
            let ledColor = '#22D3EE';
            let inkColor = '#07090E';
            let isLit = isPageActive || isPressed;

            if (!isPoweredOn) {
              isLit = false;
            } else if (bName === 'PLAY') {
              isLit = isPlayingSeq;
              ledColor = '#34D399';
              inkColor = '#062C1E';
            } else if (bName === 'REC') {
              isLit = isRecording;
              ledColor = '#EF4444';
              inkColor = '#FFFFFF';
            } else if (bName === 'ARP') {
              isLit = isPageActive || Boolean(patch.arpEnabled);
              ledColor = patch.arpLatch ? '#FBBF24' : patch.arpEnabled ? '#34D399' : '#22D3EE';
              inkColor = '#07090E';
            } else if (bName === 'SAVE') {
              isLit = isPressed || saveFlashUntil > performance.now() || saveHoldStart !== null;
              ledColor = saveHoldStart !== null ? '#FBBF24' : '#34D399';
              inkColor = '#062C1E';
            } else if (bName === 'SEQ') {
              isLit = isPageActive || isPressed;
              ledColor = '#34D399';
              inkColor = '#062C1E';
            }

            const bx = 582 + i * 63;
            const by = 236;
            return (
              <g
                key={bName}
                className="cursor-pointer"
                onPointerDown={() => handleButtonDown(bName)}
                onPointerUp={() => handleButtonUp(bName)}
                onPointerLeave={() => handleButtonUp(bName)}
              >
                <rect
                  x={bx}
                  y={by}
                  width={54}
                  height={48}
                  rx={9}
                  fill={isLit ? ledColor : BTNC}
                  stroke={isLit ? ledColor : '#4a4d52'}
                  strokeWidth={1.5}
                />
                {bName === 'PLAY' ? (
                  <>
                    <text
                      x={bx + 27}
                      y={by + 23}
                      textAnchor="middle"
                      fontFamily="'Plus Jakarta Sans', sans-serif"
                      fontSize={11.5}
                      fontWeight={700}
                      fill={isLit ? inkColor : LABEL}
                      className="pointer-events-none"
                    >
                      PLAY
                    </text>
                    <text
                      x={bx + 27}
                      y={by + 36}
                      textAnchor="middle"
                      fontFamily="'Plus Jakarta Sans', sans-serif"
                      fontSize={11.5}
                      fontWeight={700}
                      fill={isLit ? inkColor : LABEL}
                      className="pointer-events-none"
                    >
                      STOP
                    </text>
                  </>
                ) : (
                  <text
                    x={bx + 27}
                    y={by + 29}
                    textAnchor="middle"
                    fontFamily="'Plus Jakarta Sans', sans-serif"
                    fontSize={14}
                    fontWeight={700}
                    fill={isLit ? inkColor : LABEL}
                    className="pointer-events-none"
                  >
                    {bName}
                  </text>
                )}
              </g>
            );
          })}

          {/* 1:1 Hardware Silicone Two-Row Equal-Size Capsule Keybed (27 identical capsules) */}
          <rect x={36} y={340} width={928} height={270} rx={18} fill={TRAY} />

          {/* Upper Row: 11 Chromatic Silicone Capsules (equal width & height = 112) */}
          {PANEL_BLACK_K.map((keyIndex, i) => {
            const isPressed = (activeKeyMask & (1 << keyIndex)) !== 0;
            const isSeqMode = activePage === 'SEQ';
            const isDrumVoiceKey = isSeqMode && i < 8;
            const isMuteKey = isSeqMode && i === 10;
            const drumTr = isDrumVoiceKey ? drumTracks[i] : undefined;
            const isVoiceSel = isDrumVoiceKey && i === selectedDrumTrack;
            const isVoiceMuted = Boolean(drumTr?.mute);
            const isRoot = !isSeqMode && isKeyRoot(keyIndex, scaleMode, rootPc);
            const afterIdx = BLACK_AFTER[i];
            const kx = 36 + 12 + (afterIdx + 1) * keyPitch - keyWidth / 2;
            const ky = 354;
            const kh = 112;
            const sharpLabels = ['F#', 'G#', 'A#', 'C#', 'D#', 'F#', 'G#', 'A#', 'C#', 'D#', 'F#'];

            let capFill = isPressed ? '#22D3EE' : KEY_B;
            let capStroke = isPressed ? '#A5F3FC' : 'none';
            let textFill = isPressed ? '#07090E' : '#475569';
            let ledFill: string | null = isRoot ? (isPressed ? '#07090E' : '#0891B2') : null;

            if (isMuteKey) {
              const hot = muteKeyHeld || isPressed;
              capFill = hot ? '#EF4444' : KEY_B;
              capStroke = hot ? '#FCA5A5' : '#991B1B';
              textFill = hot ? '#FFFFFF' : '#7F1D1D';
              ledFill = hot ? '#FFFFFF' : '#EF4444';
            } else if (isDrumVoiceKey) {
              if (isVoiceMuted) {
                capFill = isPressed ? '#F87171' : '#EF4444';
                capStroke = isVoiceSel ? '#FFFFFF' : '#FCA5A5';
                textFill = '#FFFFFF';
                ledFill = '#FFFFFF';
              } else if (isVoiceSel || isPressed) {
                capFill = '#22D3EE';
                capStroke = '#A5F3FC';
                textFill = '#07090E';
                ledFill = '#07090E';
              } else {
                ledFill = '#334155';
              }
            }

            return (
              <g
                key={`bk-${i}`}
                className="cursor-pointer select-none touch-none"
                onPointerDown={(e) => handlePointerKeyDown(keyIndex, e)}
                onPointerUp={() => handlePointerKeyUpOrLeave(keyIndex, false)}
                onPointerLeave={() => handlePointerKeyUpOrLeave(keyIndex, true)}
                onPointerCancel={() => handlePointerKeyUpOrLeave(keyIndex, true)}
              >
                <rect
                  x={kx}
                  y={ky}
                  width={keyWidth}
                  height={kh}
                  rx={keyRx}
                  fill={capFill}
                  stroke={capStroke}
                  strokeWidth={capStroke !== 'none' ? 2 : 0}
                />
                {ledFill && (
                  <circle
                    cx={kx + keyWidth / 2}
                    cy={ky + 16}
                    r={3.5}
                    fill={ledFill}
                    className="pointer-events-none"
                  />
                )}
                <text
                  x={kx + keyWidth / 2}
                  y={ky + kh - 16}
                  textAnchor="middle"
                  fontFamily="'JetBrains Mono', monospace"
                  fontSize={10.5}
                  fontWeight={700}
                  fill={textFill}
                  className="pointer-events-none"
                >
                  {sharpLabels[i]}
                </text>
              </g>
            );
          })}

          {/* Lower Row: 16 Natural Silicone Capsules (identical size: width = keyWidth, height = 112) */}
          {Array.from({ length: 16 }).map((_, i) => {
            const keyIndex = PANEL_WHITE_K[i];
            const isPressed = (activeKeyMask & (1 << keyIndex)) !== 0;
            const isSeqMode = activePage === 'SEQ';
            const activeBar =
              isPlayingSeq && !isRecording
                ? Math.floor(playheadStep / 16)
                : Math.floor(selectedStepIdx / 16);
            const absIdx = activeBar * 16 + i;
            const curTrack = drumTracks[selectedDrumTrack];
            const curStep = curTrack?.steps[absIdx];
            const inLen = absIdx < seqLength;
            const isStepOn = isSeqMode && inLen && Boolean(curStep?.active);
            const isPlayheadHit = isSeqMode && isPlayingSeq && playheadStep === absIdx;
            const isCursor = isSeqMode && selectedStepIdx === absIdx;

            const isRoot = !isSeqMode && isKeyRoot(keyIndex, scaleMode, rootPc);
            const kx = 36 + 12 + i * keyPitch + (keyPitch - keyWidth) / 2;
            const ky = 484;
            const kh = 112;
            const noteNames = ['F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'];

            let capFill = isPressed ? '#FBBF24' : KEY_W;
            let capStroke = isPressed ? '#FDE68A' : 'none';
            let textFill = isPressed ? '#07090E' : '#334155';
            let ledFill: string | null = isRoot ? (isPressed ? '#07090E' : '#0891B2') : null;

            if (isSeqMode) {
              if (isPlayheadHit) {
                capFill = '#34D399';
                capStroke = '#A7F3D0';
                textFill = '#042F1E';
                ledFill = '#042F1E';
              } else if (isStepOn) {
                capFill = curStep?.accent ? '#FBBF24' : '#22D3EE';
                capStroke = isCursor ? '#FFFFFF' : '#A5F3FC';
                textFill = '#07090E';
                ledFill = '#07090E';
              } else if (isPressed) {
                capFill = '#FBBF24';
                capStroke = '#FDE68A';
                textFill = '#07090E';
              } else if (isCursor) {
                capStroke = '#22D3EE';
                ledFill = '#22D3EE';
              } else if (!inLen) {
                capFill = '#94A3B8';
                textFill = '#475569';
              }
            }

            return (
              <g
                key={`wk-${i}`}
                className="cursor-pointer select-none touch-none"
                onPointerDown={(e) => handlePointerKeyDown(keyIndex, e)}
                onPointerUp={() => handlePointerKeyUpOrLeave(keyIndex, false)}
                onPointerLeave={() => handlePointerKeyUpOrLeave(keyIndex, true)}
                onPointerCancel={() => handlePointerKeyUpOrLeave(keyIndex, true)}
              >
                <rect
                  x={kx}
                  y={ky}
                  width={keyWidth}
                  height={kh}
                  rx={keyRx}
                  fill={capFill}
                  stroke={capStroke}
                  strokeWidth={capStroke !== 'none' ? 2 : 0}
                />
                {ledFill && (
                  <circle
                    cx={kx + keyWidth / 2}
                    cy={ky + 16}
                    r={3.5}
                    fill={ledFill}
                    className="pointer-events-none"
                  />
                )}
                <text
                  x={kx + keyWidth / 2}
                  y={ky + kh - 16}
                  textAnchor="middle"
                  fontFamily="'JetBrains Mono', monospace"
                  fontSize={10.5}
                  fontWeight={700}
                  fill={textFill}
                  className="pointer-events-none"
                >
                  {noteNames[i]}
                </text>
              </g>
            );
          })}
        </svg>

        {/* Exact 1:1 Square 1.54" Display (208x208 centered in 236x236 bezel -> 240x240px canvas) */}
        <div
          onClick={(e) => {
              if (!isPoweredOn) return;
              touchMenuActivity();
              if (bootSplashRef.current) {
                bootSplashRef.current = null;
                audioEngine.stopBootJingle();
                return;
              }
              const rect = e.currentTarget.getBoundingClientRect();
              const nx = ((e.clientX - rect.left) / rect.width) * 240;
              const ny = ((e.clientY - rect.top) / rect.height) * 240;
              // Tap top-right Starfish / Drummer Boy inside OLED to spin 360° on a dime & change color
              if (nx >= 198 && ny <= 40) {
                const st = starAnimRef.current;
                st.paletteIdx = (st.paletteIdx + 1) % STARFISH_COLORS.length;
                st.stepDir *= -1;
                st.bounceEnergy = 1.0;
                st.spinTarget += 360 * st.stepDir;
                st.targetSwivel = st.stepDir * 24;
                st.targetDanceX = st.stepDir * 3.5;
                st.happyUntil = performance.now() + 280;
                return;
              }
              // Tap confirmation prompt bars directly if open
              if (confirmPrompt) {
                if (ny >= 116 && ny <= 146) {
                  handleButtonDown('SEL');
                  return;
                }
                if (ny >= 156 && ny <= 190) {
                  handleButtonDown('OCT+');
                  return;
                }
                if (ny >= 194 && ny <= 228) {
                  handleButtonDown('OCT-');
                  return;
                }
                return;
              }
              // Tap main page strip / pagination dots (y: 36..83) to step subpages directly on multi-page screens
              if (ny >= 36 && ny <= 83 && PAGE_TOTALS[activePage] > 1) {
                setSubPage((prev) => (prev + 1) % PAGE_TOTALS[activePage]);
                return;
              }
              // Tap GLO (3/3) Randomizer card to generate a fresh safe patch
              if (activePage === 'GLO' && subPage === 2 && ny >= 88 && ny <= 158) {
                handleGenerateRandomPatch();
                return;
              }
              // Tap OLED drum voices, BAR badge, or step matrix when in SEQ mode
              if (activePage === 'SEQ') {
              const activeBar = isPlayingSeq && !isRecording
                ? Math.floor(playheadStep / 16)
                : Math.floor(selectedStepIdx / 16);
              if (ny >= 84 && ny <= 103) {
                if (nx >= 193) {
                  const nextBar = (activeBar + 1) % 4;
                  const target = nextBar * 16 + (selectedStepIdx % 16);
                  if (target + 1 > seqLength) onSeqLengthChange((nextBar + 1) * 16);
                  setSelectedStepIdx(target);
                  return;
                }
                const trIdx = Math.floor((nx - 8) / 23);
                if (trIdx >= 0 && trIdx < 8) {
                  if (trIdx === selectedDrumTrack) {
                    onToggleDrumMute(trIdx);
                  } else {
                    setSelectedDrumTrack(trIdx);
                    audioEngine.initAudio();
                    audioEngine.triggerDrum(trIdx, 108);
                  }
                  return;
                }
              }
              if (ny >= 104 && ny <= 162) {
                const col = Math.floor((nx - 14) / 27);
                const row = Math.floor((ny - 106) / 27);
                if (col >= 0 && col < 8 && row >= 0 && row < 2) {
                  const absIdx = activeBar * 16 + row * 8 + col;
                  if (absIdx >= seqLength) {
                    onSeqLengthChange(absIdx + 1);
                  }
                  const curTrack = drumTracks[selectedDrumTrack];
                  const cur = curTrack?.steps[absIdx];
                  if (absIdx === selectedStepIdx) {
                    onDrumStepUpdate(selectedDrumTrack, absIdx, {
                      active: !cur?.active,
                      accent: false,
                      velocity: 108
                    });
                  } else {
                    setSelectedStepIdx(absIdx);
                    if (!cur?.active) {
                      onDrumStepUpdate(selectedDrumTrack, absIdx, {
                        active: true,
                        accent: false,
                        velocity: 108
                      });
                    }
                  }
                }
              }
            }
          }}
          className="absolute overflow-hidden rounded-2xl border border-slate-700/70 shadow-inner cursor-pointer"
          style={{
            left: '31.4%',
            top: '9.06%',
            width: '20.8%',
            height: '32.5%'
          }}
        >
          <canvas
            ref={canvasRef}
            width={240}
            height={240}
            className="w-full h-full block bg-[#07090E]"
          />
        </div>
      </div>
    </div>
  );
};
