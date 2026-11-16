/**
 * M-Vave FM-1 Real-Time Web Audio Simulation Engine
 * Emulates the JieLi AC791N SoC DSP pipeline:
 * - 6-Operator Yamaha DX7 FM synthesis with true AudioParam frequency modulation
 * - Virtual Analog (OberVave) with resonant ladder filter & unison
 * - Juno-106 style polysynth with dual BBD stereo chorus
 * - MOS 6581 SID chiptune engine with arpeggiator
 * - 808/909 percussion synthesis
 * - Stereo FX chain: Multimode filter, Ping-Pong Delay, Algorithmic Reverb, Tape Drive
 * - Sample-accurate 64-step sequencer clock
 * - Master AnalyserNode for the 128x64 OLED display
 */

import { SynthPatch, DrumTrack, ThrowFxType } from '../types/firmware';
import { getAlgorithm } from '../data/dx7Algorithms';
import { getEffectiveModMatrix } from '../utils/musicScale';

const DEFAULT_VIRUS_PATCH: SynthPatch = {
  id: 'virus-hypersaw-lead',
  name: 'Virus Cyber Hypersaw Lead',
  category: 'Lead',
  engine: 'virus_va',
  wave: 'saw',
  octave: 0,
  masterGain: 3700,
  algorithm: 32,
  feedback: 6,
  filterCutoff: 6500,
  filterReso: 7.2,
  filterType: 'lowpass',
  fxChorus: 60,
  fxDelayTime: 0.33,
  fxDelayFeedback: 45,
  fxReverbSize: 65,
  fxDrive: 40,
  glide: 30,
  polyphony: 8,
  operators: [
    { id: 1, ratio: 1.0, detune: -16.8, level: 99, rate1: 99, rate2: 60, rate3: 85, rate4: 65, level1: 99, level2: 85, level3: 85, level4: 0, isCarrier: true },
    { id: 2, ratio: 1.0, detune: 16.8, level: 99, rate1: 99, rate2: 60, rate3: 85, rate4: 65, level1: 99, level2: 85, level3: 85, level4: 0, isCarrier: true },
    { id: 3, ratio: 0.5, detune: 0, level: 85, rate1: 99, rate2: 60, rate3: 85, rate4: 65, level1: 99, level2: 85, level3: 85, level4: 0, isCarrier: true },
    { id: 4, ratio: 2.0, detune: 6, level: 75, rate1: 95, rate2: 60, rate3: 75, rate4: 60, level1: 99, level2: 75, level3: 75, level4: 0, isCarrier: true },
    { id: 5, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false },
    { id: 6, ratio: 1.0, detune: 0, level: 0, rate1: 99, rate2: 99, rate3: 0, rate4: 99, level1: 0, level2: 0, level3: 0, level4: 0, isCarrier: false }
  ]
};

export class Fm1AudioEngine {
  private ctx: AudioContext | null = null;
  private masterGain: GainNode | null = null;
  private analyser: AnalyserNode | null = null;
  
  // FX Nodes
  private filterNode: BiquadFilterNode | null = null;
  private driveNode: WaveShaperNode | null = null;
  private delayNodeL: DelayNode | null = null;
  private delayNodeR: DelayNode | null = null;
  private delayFeedbackL: GainNode | null = null;
  private delayFeedbackR: GainNode | null = null;
  private delayToneLpf: BiquadFilterNode | null = null;
  private delayToneHpf: BiquadFilterNode | null = null;
  private delayPanL: StereoPannerNode | null = null;
  private delayPanR: StereoPannerNode | null = null;
  private delayMixNode: GainNode | null = null;
  private reverbPreDelay: DelayNode | null = null;
  private reverbHpf: BiquadFilterNode | null = null;
  private reverbLpf: BiquadFilterNode | null = null;
  private reverbConvolver: ConvolverNode | null = null;
  private reverbMixNode: GainNode | null = null;
  private chorusDelayL: DelayNode | null = null;
  private chorusDelayR: DelayNode | null = null;
  private chorusLfo: OscillatorNode | null = null;
  private chorusMixNode: GainNode | null = null;
  private phaserInputNode: GainNode | null = null;
  private phaserDryGain: GainNode | null = null;
  private phaserWetGain: GainNode | null = null;
  private phaserFilters: BiquadFilterNode[] = [];
  private phaserLfo: OscillatorNode | null = null;
  private phaserLfoDepth: GainNode | null = null;
  private phaserFeedbackGain: GainNode | null = null;
  private phaserOutputNode: GainNode | null = null;
  private drumBusGain: GainNode | null = null;
  private drumLowShelf: BiquadFilterNode | null = null;
  private drumHighShelf: BiquadFilterNode | null = null;
  private drumBusComp: DynamicsCompressorNode | null = null;
  private drumReverbSend: GainNode | null = null;
  private drumVolumeLevel = 0.85;
  private drumKitIndex = 0; // 0..9 across 10 iconic drum kits
  private drumBusMode = 0;  // 0..5 clean studio drum DSP engines (no distortion)
  private noiseBuffer: AudioBuffer | null = null;
  private dryGain: GainNode | null = null;
  private masterCompressor: DynamicsCompressorNode | null = null;
  private fxLfo1Osc: OscillatorNode | null = null;
  private fxLfo1Gain: GainNode | null = null;
  private fxLfo2Osc: OscillatorNode | null = null;
  private fxLfo2Gain: GainNode | null = null;

  // Momentary Performance Throw FX Nodes
  private throwFilterNode: BiquadFilterNode | null = null;
  private throwGateGain: GainNode | null = null;
  private activeThrows: Set<ThrowFxType> = new Set();
  private stutterTimerId: number | null = null;
  private stutterPhase = false;

  // Active Voices
  private activeVoices: Map<number, Fm1Voice> = new Map();
  private currentPatch: SynthPatch = DEFAULT_VIRUS_PATCH;

  // Dedicated 64-Step Drum Machine + Live Synth Loop Sequencer Clock
  private isPlayingSeq = false;
  private bpm = 124;
  private swing = 50; // 50% = straight, 66% = triplet shuffle
  private seqLength = 16;
  private currentStep = 0;
  private timerId: number | null = null;
  private nextStepTime = 0;
  private lastScheduledStep = 0;
  private lastScheduledStepTime = 0;
  private drumTracks: DrumTrack[] = [];
  private onStepCallback: ((step: number) => void) | null = null;
  private onNoteTriggerCallback: ((note: number, velocity: number) => void) | null = null;
  private onDrumTriggerCallback: ((drumIndex: number, velocity: number) => void) | null = null;

  // Live Polyphonic Synth Loop Overdub Track (synced to drum clock)
  private synthLoopSteps: Array<Array<{ note: number; velocity: number; gateSteps: number; recordedAtMs: number }>> = Array.from({ length: 64 }, () => []);
  private isSynthRecording = false;
  private pendingRecNotes: Map<number, { step: number; event: { note: number; velocity: number; gateSteps: number; recordedAtMs: number }; startTime: number }> = new Map();
  private activeSynthLoopVoices: Map<number, { voice: Fm1Voice; timerId: number }> = new Map();
  private onSynthLoopChange: ((count: number) => void) | null = null;

  // Boot Splash Jingle Playback
  private jingleTimerIds: number[] = [];
  private activeJingleVoices: Set<Fm1Voice> = new Set();

  private masterVolumeLevel = 0.15;

  constructor() {
    // Audio nodes initialize on first gesture
  }

  public initAudio(): AudioContext {
    if (!this.ctx || this.ctx.state === 'closed') {
      const AudioContextClass = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      this.ctx = new AudioContextClass({ latencyHint: 'interactive', sampleRate: 48000 });

      // Master nodes
      this.analyser = this.ctx.createAnalyser();
      this.analyser.fftSize = 512;
      this.analyser.smoothingTimeConstant = 0.8;

      this.masterGain = this.ctx.createGain();
      this.masterGain.gain.setValueAtTime(this.masterVolumeLevel * 0.85, this.ctx.currentTime);

      // Filter (keep transparent for virus_va since per-voice dual filters handle filtering)
      this.filterNode = this.ctx.createBiquadFilter();
      if (this.currentPatch.engine === 'virus_va') {
        this.filterNode.type = 'lowpass';
        this.filterNode.frequency.setValueAtTime(19500, this.ctx.currentTime);
        this.filterNode.Q.setValueAtTime(0.7, this.ctx.currentTime);
      } else {
        this.filterNode.type = this.currentPatch.filterType || 'lowpass';
        this.filterNode.frequency.setValueAtTime(this.currentPatch.filterCutoff || 6500, this.ctx.currentTime);
        this.filterNode.Q.setValueAtTime(Math.min(this.currentPatch.filterReso || 7.2, 20), this.ctx.currentTime);
      }

      // Overdrive / WaveShaper
      this.driveNode = this.ctx.createWaveShaper();
      this.updateDriveCurve(this.currentPatch.fxDrive || 40);

      // Chorus
      this.chorusDelayL = this.ctx.createDelay();
      this.chorusDelayR = this.ctx.createDelay();
      this.chorusDelayL.delayTime.setValueAtTime(0.015, this.ctx.currentTime);
      this.chorusDelayR.delayTime.setValueAtTime(0.019, this.ctx.currentTime);
      this.chorusMixNode = this.ctx.createGain();
      this.chorusMixNode.gain.setValueAtTime((this.currentPatch.fxChorus || 40) / 100, this.ctx.currentTime);

      // Chorus LFO
      this.chorusLfo = this.ctx.createOscillator();
      this.chorusLfo.frequency.setValueAtTime(0.75, this.ctx.currentTime);
      const chorusDepthL = this.ctx.createGain();
      const chorusDepthR = this.ctx.createGain();
      chorusDepthL.gain.setValueAtTime(0.003, this.ctx.currentTime);
      chorusDepthR.gain.setValueAtTime(-0.003, this.ctx.currentTime);
      this.chorusLfo.connect(chorusDepthL);
      this.chorusLfo.connect(chorusDepthR);
      chorusDepthL.connect(this.chorusDelayL.delayTime);
      chorusDepthR.connect(this.chorusDelayR.delayTime);
      this.chorusLfo.start();

      // Studio Stereo Ping-Pong Tape/Analog Delay
      this.delayNodeL = this.ctx.createDelay(2.0);
      this.delayNodeR = this.ctx.createDelay(2.0);
      const dt = Math.max(0.01, this.currentPatch.fxDelayTime || 0.33);
      this.delayNodeL.delayTime.setValueAtTime(dt, this.ctx.currentTime);
      this.delayNodeR.delayTime.setValueAtTime(dt * 0.75, this.ctx.currentTime);

      this.delayToneHpf = this.ctx.createBiquadFilter();
      this.delayToneHpf.type = 'highpass';
      this.delayToneHpf.frequency.setValueAtTime(140, this.ctx.currentTime);

      this.delayToneLpf = this.ctx.createBiquadFilter();
      this.delayToneLpf.type = 'lowpass';
      this.delayToneLpf.frequency.setValueAtTime(3800, this.ctx.currentTime);

      this.delayFeedbackL = this.ctx.createGain();
      this.delayFeedbackR = this.ctx.createGain();
      const fb = Math.min(0.86, (this.currentPatch.fxDelayFeedback || 40) / 100);
      this.delayFeedbackL.gain.setValueAtTime(fb, this.ctx.currentTime);
      this.delayFeedbackR.gain.setValueAtTime(fb * 0.92, this.ctx.currentTime);

      this.delayPanL = this.ctx.createStereoPanner();
      this.delayPanR = this.ctx.createStereoPanner();
      this.delayPanL.pan.setValueAtTime(-0.68, this.ctx.currentTime);
      this.delayPanR.pan.setValueAtTime(0.68, this.ctx.currentTime);

      this.delayNodeL.connect(this.delayFeedbackL);
      this.delayFeedbackL.connect(this.delayToneLpf);
      this.delayToneLpf.connect(this.delayToneHpf);
      this.delayToneHpf.connect(this.delayNodeR);
      this.delayNodeR.connect(this.delayFeedbackR);
      this.delayFeedbackR.connect(this.delayNodeL);

      this.delayMixNode = this.ctx.createGain();
      const initDelayMixPct = this.currentPatch.fxDelayMix ?? Math.min(72, Math.max(32, (this.currentPatch.fxDelayFeedback || 35) * 0.9));
      this.delayMixNode.gain.setValueAtTime(
        (this.currentPatch.fxDelayTime || 0) > 0.005 ? (initDelayMixPct / 100) * 0.85 : 0,
        this.ctx.currentTime
      );

      // Studio Lush Hall / Plate Reverb (Pre-delay + Early Reflections + Damped Stereo Tail)
      this.reverbPreDelay = this.ctx.createDelay(0.15);
      this.reverbPreDelay.delayTime.setValueAtTime(0.018, this.ctx.currentTime);
      this.reverbHpf = this.ctx.createBiquadFilter();
      this.reverbHpf.type = 'highpass';
      this.reverbHpf.frequency.setValueAtTime(115, this.ctx.currentTime);
      this.reverbLpf = this.ctx.createBiquadFilter();
      this.reverbLpf.type = 'lowpass';
      this.reverbLpf.frequency.setValueAtTime(6400, this.ctx.currentTime);

      this.reverbConvolver = this.ctx.createConvolver();
      this.generateReverbImpulse(3.1, 0.86);
      this.reverbMixNode = this.ctx.createGain();
      const revNorm = (this.currentPatch.fxReverbSize || 40) / 100;
      this.reverbMixNode.gain.setValueAtTime(Math.pow(revNorm, 1.1) * 1.12, this.ctx.currentTime);

      // 6-Stage Allpass Phaser (HelgeSverre/supersaw fx.js architecture)
      this.phaserInputNode = this.ctx.createGain();
      this.phaserDryGain = this.ctx.createGain();
      this.phaserWetGain = this.ctx.createGain();
      this.phaserOutputNode = this.ctx.createGain();
      this.phaserFilters = [];
      for (let i = 0; i < 6; i++) {
        const ap = this.ctx.createBiquadFilter();
        ap.type = 'allpass';
        ap.frequency.setValueAtTime(900 + i * 220, this.ctx.currentTime);
        this.phaserFilters.push(ap);
      }
      for (let i = 1; i < this.phaserFilters.length; i++) {
        this.phaserFilters[i - 1].connect(this.phaserFilters[i]);
      }
      this.phaserLfo = this.ctx.createOscillator();
      this.phaserLfo.type = 'triangle';
      this.phaserLfo.frequency.setValueAtTime(this.currentPatch.fxPhaserRate ?? 0.5, this.ctx.currentTime);
      this.phaserLfoDepth = this.ctx.createGain();
      const initPhDepth = ((this.currentPatch.fxPhaserDepth ?? 65) / 100) * 1400;
      this.phaserLfoDepth.gain.setValueAtTime(initPhDepth, this.ctx.currentTime);
      this.phaserFeedbackGain = this.ctx.createGain();
      this.phaserFeedbackGain.gain.setValueAtTime((this.currentPatch.fxPhaserFeedback ?? 45) / 100 * 0.78, this.ctx.currentTime);

      this.phaserLfo.connect(this.phaserLfoDepth);
      this.phaserFilters.forEach((ap) => this.phaserLfoDepth!.connect(ap.frequency));
      this.phaserLfo.start();

      this.phaserInputNode.connect(this.phaserDryGain);
      this.phaserInputNode.connect(this.phaserFilters[0]);
      this.phaserFilters[this.phaserFilters.length - 1].connect(this.phaserFeedbackGain);
      this.phaserFeedbackGain.connect(this.phaserFilters[0]);
      this.phaserFilters[this.phaserFilters.length - 1].connect(this.phaserWetGain);
      this.phaserDryGain.connect(this.phaserOutputNode);
      this.phaserWetGain.connect(this.phaserOutputNode);

      const initPhMix = (this.currentPatch.fxPhaserMix ?? 0) / 100;
      this.phaserDryGain.gain.setValueAtTime(1 - initPhMix * 0.45, this.ctx.currentTime);
      this.phaserWetGain.gain.setValueAtTime(initPhMix * 0.7, this.ctx.currentTime);

      // Dry
      this.dryGain = this.ctx.createGain();
      this.dryGain.gain.setValueAtTime(0.85, this.ctx.currentTime);

      // Signal routing:
      // Filter -> 6-Stage Phaser -> Dry / Chorus / Delay / Reverb -> MasterGain -> Throw -> Limiter
      this.filterNode.connect(this.phaserInputNode);
      this.phaserOutputNode.connect(this.dryGain);
      this.dryGain.connect(this.masterGain);

      // FX sends
      this.phaserOutputNode.connect(this.chorusDelayL);
      this.phaserOutputNode.connect(this.chorusDelayR);
      this.chorusDelayL.connect(this.chorusMixNode);
      this.chorusDelayR.connect(this.chorusMixNode);
      this.chorusMixNode.connect(this.masterGain);

      this.phaserOutputNode.connect(this.delayNodeL);
      this.delayNodeL.connect(this.delayPanL);
      this.delayNodeR.connect(this.delayPanR);
      this.delayPanL.connect(this.delayMixNode);
      this.delayPanR.connect(this.delayMixNode);
      this.delayMixNode.connect(this.masterGain);

      this.phaserOutputNode.connect(this.reverbPreDelay);
      this.reverbPreDelay.connect(this.reverbHpf);
      this.reverbHpf.connect(this.reverbConvolver);
      this.reverbConvolver.connect(this.reverbLpf);
      this.reverbLpf.connect(this.reverbMixNode);
      this.reverbMixNode.connect(this.masterGain);

      // Clean Studio Drum Machine Bus (Tone Shelves + Punch Compressor + Room Reverb Send, Zero Distortion)
      this.drumBusGain = this.ctx.createGain();
      this.drumBusGain.gain.setValueAtTime(this.drumVolumeLevel, this.ctx.currentTime);

      this.drumLowShelf = this.ctx.createBiquadFilter();
      this.drumLowShelf.type = 'lowshelf';
      this.drumLowShelf.frequency.setValueAtTime(95, this.ctx.currentTime);
      this.drumLowShelf.gain.setValueAtTime(0, this.ctx.currentTime);

      this.drumHighShelf = this.ctx.createBiquadFilter();
      this.drumHighShelf.type = 'highshelf';
      this.drumHighShelf.frequency.setValueAtTime(6200, this.ctx.currentTime);
      this.drumHighShelf.gain.setValueAtTime(0, this.ctx.currentTime);

      this.drumBusComp = this.ctx.createDynamicsCompressor();
      this.drumBusComp.threshold.setValueAtTime(-16, this.ctx.currentTime);
      this.drumBusComp.knee.setValueAtTime(8, this.ctx.currentTime);
      this.drumBusComp.ratio.setValueAtTime(2.0, this.ctx.currentTime);
      this.drumBusComp.attack.setValueAtTime(0.012, this.ctx.currentTime);
      this.drumBusComp.release.setValueAtTime(0.14, this.ctx.currentTime);

      this.drumBusGain.connect(this.drumLowShelf);
      this.drumLowShelf.connect(this.drumHighShelf);
      this.drumHighShelf.connect(this.drumBusComp);
      this.drumBusComp.connect(this.masterGain);

      this.drumReverbSend = this.ctx.createGain();
      this.drumReverbSend.gain.setValueAtTime(0.16, this.ctx.currentTime);
      this.drumReverbSend.connect(this.reverbPreDelay);
      this.applyDrumBusFx();

      // Pre-build shared stereo noise buffer for Virus noise generator & drums
      const nLen = this.ctx.sampleRate * 2;
      this.noiseBuffer = this.ctx.createBuffer(1, nLen, this.ctx.sampleRate);
      const nData = this.noiseBuffer.getChannelData(0);
      for (let i = 0; i < nLen; i++) {
        nData[i] = Math.random() * 2 - 1;
      }

      // Studio Multi-band Dynamics Compressor / Limiter for automatic volume normalization
      this.masterCompressor = this.ctx.createDynamicsCompressor();
      this.masterCompressor.threshold.setValueAtTime(-14, this.ctx.currentTime);
      this.masterCompressor.knee.setValueAtTime(6, this.ctx.currentTime);
      this.masterCompressor.ratio.setValueAtTime(4.0, this.ctx.currentTime);
      this.masterCompressor.attack.setValueAtTime(0.003, this.ctx.currentTime);
      this.masterCompressor.release.setValueAtTime(0.18, this.ctx.currentTime);

      // LOOM-8 Momentary Throw Stage (placed cleanly after masterGain so patch settings stay untouched)
      this.throwFilterNode = this.ctx.createBiquadFilter();
      this.throwFilterNode.type = 'highpass';
      this.throwFilterNode.frequency.setValueAtTime(20, this.ctx.currentTime);
      this.throwFilterNode.Q.setValueAtTime(0.7, this.ctx.currentTime);

      this.throwGateGain = this.ctx.createGain();
      this.throwGateGain.gain.setValueAtTime(1.0, this.ctx.currentTime);

      this.masterGain.connect(this.throwFilterNode);
      this.throwFilterNode.connect(this.throwGateGain);
      this.throwGateGain.connect(this.masterCompressor);
      this.masterCompressor.connect(this.analyser);
      this.analyser.connect(this.ctx.destination);

      // Dedicated Global FX LFO 1 & 2 modulators (Chorus / Phaser / Delay / Reverb modulation)
      this.fxLfo1Osc = this.ctx.createOscillator();
      this.fxLfo1Gain = this.ctx.createGain();
      this.fxLfo1Gain.gain.setValueAtTime(0, this.ctx.currentTime);
      this.fxLfo1Osc.connect(this.fxLfo1Gain);
      this.fxLfo1Osc.start();

      this.fxLfo2Osc = this.ctx.createOscillator();
      this.fxLfo2Gain = this.ctx.createGain();
      this.fxLfo2Gain.gain.setValueAtTime(0, this.ctx.currentTime);
      this.fxLfo2Osc.connect(this.fxLfo2Gain);
      this.fxLfo2Osc.start();

      this.updateFxLfo(this.currentPatch);
    }

    if (this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }

    return this.ctx;
  }

  private updateDriveCurve(amount: number): void {
    if (!this.ctx || !this.driveNode) return;
    const k = Math.max(0, Math.min(amount * 1.5, 95));
    if (k === 0) {
      this.driveNode.curve = null;
      return;
    }
    const n_samples = 44100;
    const curve = new Float32Array(n_samples);
    const deg = Math.PI / 180;
    for (let i = 0; i < n_samples; ++i) {
      const x = (i * 2) / n_samples - 1;
      curve[i] = ((3 + k) * x * 20 * deg) / (Math.PI + k * Math.abs(x));
    }
    this.driveNode.curve = curve;
  }

  private generateReverbImpulse(duration: number, decay: number): void {
    if (!this.ctx || !this.reverbConvolver) return;
    const sampleRate = this.ctx.sampleRate;
    const length = Math.floor(sampleRate * duration);
    const impulse = this.ctx.createBuffer(2, length, sampleRate);
    const left = impulse.getChannelData(0);
    const right = impulse.getChannelData(1);

    // Lexicon-style velvet dense stereo tail + early reflections + progressive air damping
    let lpL = 0;
    let lpR = 0;
    const erTaps = [0.007, 0.013, 0.019, 0.029, 0.041, 0.055];
    for (let i = 0; i < length; i++) {
      const t = i / sampleRate;
      const buildup = 1 - Math.exp(-t * 38);
      const tailEnv = Math.exp(-t * (1.05 + (1.0 - decay) * 2.2)) * buildup;
      const dampCoeff = Math.max(0.18, 0.85 - (t / duration) * 0.62);

      const nL = Math.random() * 2 - 1;
      const nR = Math.random() * 2 - 1;
      lpL = lpL + dampCoeff * (nL - lpL);
      lpR = lpR + dampCoeff * (nR - lpR);

      left[i] = (lpL * 0.85 + lpR * 0.25) * tailEnv;
      right[i] = (lpR * 0.85 - lpL * 0.25) * tailEnv;
    }
    erTaps.forEach((tapSec, idx) => {
      const tapIdx = Math.min(length - 1, Math.floor(tapSec * sampleRate));
      const amp = 0.42 * Math.pow(0.76, idx);
      left[tapIdx] += (idx % 2 === 0 ? 1 : 0.45) * amp;
      right[tapIdx] += (idx % 2 === 1 ? 1 : 0.45) * amp;
    });
    this.reverbConvolver.buffer = impulse;
  }

  public setPatch(patch: SynthPatch): void {
    this.currentPatch = patch;
    if (!this.ctx) return;

    const t = this.ctx.currentTime;
    if (this.filterNode) {
      if (patch.engine === 'virus_va') {
        this.filterNode.type = 'lowpass';
        this.filterNode.frequency.setTargetAtTime(19500, t, 0.04);
        this.filterNode.Q.setTargetAtTime(0.7, t, 0.04);
      } else {
        this.filterNode.type = patch.filterType || 'lowpass';
        this.filterNode.frequency.setTargetAtTime(patch.filterCutoff, t, 0.05);
        this.filterNode.Q.setTargetAtTime(Math.min(patch.filterReso, 20), t, 0.05);
      }
    }
    this.activeVoices.forEach(v => v.updateLivePatch(patch));
    if (this.chorusMixNode) {
      this.chorusMixNode.gain.setTargetAtTime((patch.fxChorus / 100) * 0.85, t, 0.05);
    }
    if (this.delayMixNode && this.delayFeedbackL && this.delayFeedbackR && this.delayNodeL && this.delayNodeR) {
      const dTime = Math.max(0.01, patch.fxDelayTime);
      const dMixPct = patch.fxDelayMix ?? Math.min(72, Math.max(30, patch.fxDelayFeedback * 0.9));
      const dMix = patch.fxDelayTime > 0.005 ? (dMixPct / 100) * 0.88 : 0;
      const fbVal = Math.min(0.86, patch.fxDelayFeedback / 100);
      this.delayMixNode.gain.setTargetAtTime(dMix, t, 0.04);
      this.delayFeedbackL.gain.setTargetAtTime(fbVal, t, 0.04);
      this.delayFeedbackR.gain.setTargetAtTime(fbVal * 0.92, t, 0.04);
      this.delayNodeL.delayTime.setTargetAtTime(dTime, t, 0.05);
      this.delayNodeR.delayTime.setTargetAtTime(dTime * 0.75, t, 0.05);
    }
    if (this.reverbMixNode && this.reverbLpf) {
      const revNorm = Math.max(0, Math.min(1, patch.fxReverbSize / 100));
      this.reverbMixNode.gain.setTargetAtTime(Math.pow(revNorm, 1.1) * 1.15, t, 0.05);
      this.reverbLpf.frequency.setTargetAtTime(3400 + revNorm * 4800, t, 0.06);
    }
    if (this.phaserDryGain && this.phaserWetGain && this.phaserLfo && this.phaserLfoDepth && this.phaserFeedbackGain) {
      const phMix = Math.max(0, Math.min(1, (patch.fxPhaserMix ?? 0) / 100));
      this.phaserDryGain.gain.setTargetAtTime(1 - phMix * 0.45, t, 0.04);
      this.phaserWetGain.gain.setTargetAtTime(phMix * 0.72, t, 0.04);
      this.phaserLfo.frequency.setTargetAtTime(Math.max(0.08, patch.fxPhaserRate ?? 0.55), t, 0.04);
      this.phaserLfoDepth.gain.setTargetAtTime(((patch.fxPhaserDepth ?? 65) / 100) * 1400, t, 0.04);
      this.phaserFeedbackGain.gain.setTargetAtTime(((patch.fxPhaserFeedback ?? 45) / 100) * 0.78, t, 0.04);
    }
    if (this.driveNode) {
      this.updateDriveCurve(patch.fxDrive);
    }
    this.arpLatch = Boolean(patch.arpLatch);
    this.updateFxLfo(patch);
  }

  private updateFxLfo(patch: SynthPatch): void {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    const waveMap: Record<string, OscillatorType> = {
      sine: 'sine',
      triangle: 'triangle',
      saw: 'sawtooth',
      square: 'square'
    };

    const routeOneFxLfo = (
      osc: OscillatorNode | null,
      gain: GainNode | null,
      dest: string | undefined,
      rate: number,
      depthPct: number,
      shape: string | undefined
    ) => {
      if (!osc || !gain) return;
      try {
        gain.disconnect();
      } catch {
        // ignore if nothing connected
      }
      osc.type = waveMap[shape || 'sine'] || 'sine';
      osc.frequency.setTargetAtTime(Math.max(0.05, Math.min(30, rate)), t, 0.03);

      const amt = Math.max(0, Math.min(1, depthPct / 100));
      if (amt <= 0.01) {
        gain.gain.setTargetAtTime(0, t, 0.03);
        return;
      }

      if (dest === 'chorus' && this.chorusMixNode) {
        gain.gain.setTargetAtTime(amt * 0.48, t, 0.03);
        gain.connect(this.chorusMixNode.gain);
      } else if (dest === 'delay' && this.delayMixNode) {
        gain.gain.setTargetAtTime(amt * 0.45, t, 0.03);
        gain.connect(this.delayMixNode.gain);
      } else if (dest === 'reverb' && this.reverbMixNode) {
        gain.gain.setTargetAtTime(amt * 0.55, t, 0.03);
        gain.connect(this.reverbMixNode.gain);
      } else if (dest === 'phaser' && this.phaserFilters.length > 0) {
        gain.gain.setTargetAtTime(amt * 950, t, 0.03);
        this.phaserFilters.forEach((ap) => gain.connect(ap.frequency));
      } else {
        gain.gain.setTargetAtTime(0, t, 0.03);
      }
    };

    routeOneFxLfo(
      this.fxLfo1Osc,
      this.fxLfo1Gain,
      patch.lfoDest,
      patch.lfoRate ?? 4.5,
      patch.lfoDepth ?? 0,
      patch.lfoShape
    );
    routeOneFxLfo(
      this.fxLfo2Osc,
      this.fxLfo2Gain,
      patch.lfo2Dest,
      patch.lfo2Rate ?? 0.9,
      patch.lfo2Depth ?? 0,
      patch.lfo2Shape
    );
  }

  // Arpeggiator Engine (with Latch/Hold & Live Octave Transposition)
  private arpKeys: Set<number> = new Set();
  private physicalArpKeys: Set<number> = new Set();
  private arpLatch = false;
  private arpLatchFreshPress = false;
  private isArpRunning = false;
  private arpStep = 0;
  private arpTimerId: number | null = null;
  private seqTranspose = 0;

  public setMasterVolume(vol: number): void {
    const clamped = Math.max(0, Math.min(1.0, vol));
    this.masterVolumeLevel = clamped;
    if (!this.ctx || !this.masterGain) return;
    this.masterGain.gain.setTargetAtTime(clamped * 0.85, this.ctx.currentTime, 0.04);
  }

  /**
   * LOOM-8 Momentary Performance "Throws": punch-in FX that never overwrite saved SynthPatch settings.
   */
  public setThrowActive(type: ThrowFxType, active: boolean): void {
    this.initAudio();
    if (!this.ctx) return;
    const t = this.ctx.currentTime;

    if (active) {
      this.activeThrows.add(type);
    } else {
      this.activeThrows.delete(type);
    }

    if (type === 'stutter') {
      if (active && this.stutterTimerId === null) {
        const stepMs = Math.max(35, Math.round((60 / this.bpm / 8) * 1000)); // 1/32 rhythm chop
        this.stutterPhase = false;
        this.stutterTimerId = window.setInterval(() => {
          if (!this.ctx || !this.throwGateGain) return;
          this.stutterPhase = !this.stutterPhase;
          this.throwGateGain.gain.setTargetAtTime(this.stutterPhase ? 0.08 : 1.0, this.ctx.currentTime, 0.004);
        }, stepMs);
      } else if (!active && this.stutterTimerId !== null) {
        window.clearInterval(this.stutterTimerId);
        this.stutterTimerId = null;
        if (this.throwGateGain) {
          this.throwGateGain.gain.setTargetAtTime(1.0, t, 0.012);
        }
      }
    } else if (type === 'hpsweep' && this.throwFilterNode) {
      if (active) {
        this.throwFilterNode.type = 'highpass';
        this.throwFilterNode.Q.setTargetAtTime(4.8, t, 0.03);
        this.throwFilterNode.frequency.cancelScheduledValues(t);
        this.throwFilterNode.frequency.setValueAtTime(Math.max(40, this.throwFilterNode.frequency.value), t);
        this.throwFilterNode.frequency.exponentialRampToValueAtTime(2600, t + 0.45);
      } else {
        this.throwFilterNode.frequency.cancelScheduledValues(t);
        this.throwFilterNode.Q.setTargetAtTime(0.7, t, 0.04);
        this.throwFilterNode.frequency.setTargetAtTime(20, t, 0.04);
      }
    } else if (type === 'freeze') {
      if (this.reverbMixNode) {
        const revNorm = Math.max(0, Math.min(1, this.currentPatch.fxReverbSize / 100));
        const targetVerb = active ? 1.05 : Math.pow(revNorm, 1.1) * 1.15;
        this.reverbMixNode.gain.setTargetAtTime(targetVerb, t, 0.05);
      }
      if (this.delayFeedbackL && this.delayFeedbackR) {
        const targetFb = active ? 0.85 : this.currentPatch.fxDelayFeedback / 100;
        this.delayFeedbackL.gain.setTargetAtTime(targetFb, t, 0.05);
        this.delayFeedbackR.gain.setTargetAtTime(targetFb, t, 0.05);
      }
    } else if (type === 'tapestop') {
      this.activeVoices.forEach(voice => voice.setTapeStop(active));
    }
  }

  public clearAllThrows(): void {
    (['stutter', 'tapestop', 'freeze', 'hpsweep'] as ThrowFxType[]).forEach(th => {
      if (this.activeThrows.has(th)) {
        this.setThrowActive(th, false);
      }
    });
  }

  public toggleArp(enable?: boolean, latch?: boolean): boolean {
    const next = enable !== undefined ? enable : !this.currentPatch.arpEnabled;
    const nextLatch = latch !== undefined ? latch : Boolean(this.currentPatch.arpLatch);
    this.arpLatch = nextLatch;
    this.currentPatch = { ...this.currentPatch, arpEnabled: next, arpLatch: nextLatch };
    if (!next) {
      this.arpLatch = false;
      this.stopArp();
    } else if (!nextLatch && this.physicalArpKeys.size === 0 && this.isArpRunning) {
      this.stopArp();
    }
    return next;
  }

  public setArpLatch(latch: boolean): void {
    this.arpLatch = latch;
    this.currentPatch = { ...this.currentPatch, arpLatch: latch };
    if (!latch && this.physicalArpKeys.size === 0 && this.isArpRunning) {
      this.stopArp();
    }
  }

  public shiftArpOctave(deltaSemitones: number): void {
    if (deltaSemitones === 0) return;
    const shiftSet = (src: Set<number>) => {
      const next = new Set<number>();
      src.forEach((n) => next.add(Math.max(24, Math.min(108, n + deltaSemitones))));
      return next;
    };
    this.arpKeys = shiftSet(this.arpKeys);
    this.physicalArpKeys = shiftSet(this.physicalArpKeys);
  }

  public setSeqTranspose(semitones: number): void {
    this.seqTranspose = semitones;
  }

  private startArp(): void {
    if (this.isArpRunning) return;
    this.isArpRunning = true;
    this.arpStep = 0;
    this.scheduleArp();
  }

  private stopArp(): void {
    this.isArpRunning = false;
    if (this.arpTimerId !== null) {
      window.clearTimeout(this.arpTimerId);
      this.arpTimerId = null;
    }
    this.allNotesOff();
  }

  private scheduleArp = (): void => {
    if (!this.isArpRunning || this.arpKeys.size === 0 || !this.ctx || !this.filterNode) {
      this.stopArp();
      return;
    }

    const baseSorted = Array.from(this.arpKeys).sort((a, b) => a - b);
    const octaves = this.currentPatch.arpOctaves || 1;
    const sorted: number[] = [];
    for (let o = 0; o < octaves; o++) {
      for (const n of baseSorted) {
        sorted.push(n + o * 12);
      }
    }

    let noteToPlay = sorted[0];
    const mode = this.currentPatch.arpMode || 'up';

    if (mode === 'up') {
      noteToPlay = sorted[this.arpStep % sorted.length];
    } else if (mode === 'down') {
      noteToPlay = sorted[(sorted.length - 1 - (this.arpStep % sorted.length))];
    } else if (mode === 'updown') {
      const cycle = Math.max(1, sorted.length * 2 - 2);
      const pos = this.arpStep % cycle;
      noteToPlay = pos < sorted.length ? sorted[pos] : sorted[cycle - pos];
    } else if (mode === 'random') {
      noteToPlay = sorted[Math.floor(Math.random() * sorted.length)];
    }

    this.activeVoices.forEach(voice => voice.release());
    this.activeVoices.clear();
    const clampedMidi = Math.max(24, Math.min(108, noteToPlay));
    const freq = 440 * Math.pow(2, (clampedMidi - 69) / 12);
    const voice = new Fm1Voice(this.ctx, this.filterNode, freq, 110, this.currentPatch, clampedMidi, this.noiseBuffer);
    this.activeVoices.set(clampedMidi, voice);
    this.onNoteTriggerCallback?.(clampedMidi, 110);

    const rateFactor = this.currentPatch.arpRate === '1/8' ? 2 : (this.currentPatch.arpRate === '1/32' ? 0.5 : (this.currentPatch.arpRate === '1/16T' ? 0.667 : 1));
    const baseStepDuration = (60 / this.bpm / 4) * rateFactor;
    const isOdd = this.arpStep % 2 === 1;
    const swingDelta = ((this.swing - 50) / 100) * baseStepDuration * 0.58;
    const stepDuration = Math.max(0.03, isOdd ? baseStepDuration - swingDelta : baseStepDuration + swingDelta);
    const gateRatio = Math.max(0.2, Math.min(0.95, (this.currentPatch.arpGate ?? 72) / 100));
    const gateTime = baseStepDuration * gateRatio;

    window.setTimeout(() => {
      voice.release();
    }, gateTime * 1000);

    this.arpStep++;
    this.arpTimerId = window.setTimeout(this.scheduleArp, stepDuration * 1000);
  };

  public noteOn(note: number, velocity: number = 100): void {
    this.initAudio();
    if (!this.ctx || !this.filterNode) return;
    if (this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }

    if (this.currentPatch.arpEnabled) {
      if (this.arpLatch && this.physicalArpKeys.size === 0 && this.arpKeys.size > 0 && this.arpLatchFreshPress) {
        // Fresh note/chord replaces previously latched arp pattern
        this.arpKeys.clear();
        this.arpLatchFreshPress = false;
      }
      this.physicalArpKeys.add(note);
      this.arpKeys.add(note);
      if (!this.isArpRunning) {
        this.startArp();
      }
      return;
    }

    // Handle polyphony voice stealing if limit reached
    const maxVoices = this.currentPatch.polyphony || 12;
    if (this.activeVoices.size >= maxVoices) {
      const oldestKey = this.activeVoices.keys().next().value;
      if (oldestKey !== undefined) {
        this.noteOff(oldestKey);
      }
    }

    // Stop existing note if already held
    if (this.activeVoices.has(note)) {
      this.noteOff(note);
    }

    // Record into Live Synth Loop Track when REC is armed in Synth mode
    if (this.isSynthRecording) {
      if (!this.isPlayingSeq) {
        this.startSequencer();
        this.lastScheduledStep = 0;
        this.lastScheduledStepTime = this.ctx.currentTime;
      }
      const stepDuration = 60 / this.bpm / 4;
      const loopLen = Math.max(1, Math.min(64, this.seqLength));
      const elapsed = Math.max(0, this.ctx.currentTime - this.lastScheduledStepTime);
      const qStep =
        elapsed > stepDuration * 0.52
          ? (this.lastScheduledStep + 1) % loopLen
          : this.lastScheduledStep % loopLen;
      const lane = this.synthLoopSteps[qStep];
      const existing = lane.find((ev) => ev.note === note);
      const nowMs = performance.now();
      if (existing) {
        existing.velocity = velocity;
        existing.recordedAtMs = nowMs;
        this.pendingRecNotes.set(note, { step: qStep, event: existing, startTime: this.ctx.currentTime });
      } else {
        const evt = { note, velocity, gateSteps: 2, recordedAtMs: nowMs };
        lane.push(evt);
        this.pendingRecNotes.set(note, { step: qStep, event: evt, startTime: this.ctx.currentTime });
      }
      this.onSynthLoopChange?.(this.getSynthLoopNoteCount());
    }

    // Frequency from MIDI note number
    const freq = 440 * Math.pow(2, (note - 69) / 12);
    const voice = new Fm1Voice(this.ctx, this.filterNode, freq, velocity, this.currentPatch, note, this.noiseBuffer);
    this.activeVoices.set(note, voice);
    this.onNoteTriggerCallback?.(note, velocity);
  }

  public noteOff(note: number): void {
    const rec = this.pendingRecNotes.get(note);
    if (rec && this.ctx) {
      const stepDuration = 60 / this.bpm / 4;
      const loopLen = Math.max(1, Math.min(64, this.seqLength));
      const durSteps = Math.max(1, Math.min(loopLen, Math.round((this.ctx.currentTime - rec.startTime) / stepDuration)));
      rec.event.gateSteps = durSteps;
      this.pendingRecNotes.delete(note);
    }

    if (this.physicalArpKeys.has(note)) {
      this.physicalArpKeys.delete(note);
      if (this.arpLatch) {
        if (this.physicalArpKeys.size === 0) {
          this.arpLatchFreshPress = true;
        }
      } else {
        this.arpKeys.delete(note);
        if (this.arpKeys.size === 0 && this.isArpRunning) {
          this.stopArp();
        }
      }
    } else if (!this.arpLatch && this.arpKeys.has(note)) {
      this.arpKeys.delete(note);
      if (this.arpKeys.size === 0 && this.isArpRunning) {
        this.stopArp();
      }
    }

    const voice = this.activeVoices.get(note);
    if (voice) {
      voice.release();
      this.activeVoices.delete(note);
    }
  }

  public allNotesOff(): void {
    this.arpKeys.clear();
    this.physicalArpKeys.clear();
    this.arpLatchFreshPress = false;
    if (this.arpTimerId !== null) {
      window.clearTimeout(this.arpTimerId);
      this.arpTimerId = null;
    }
    this.isArpRunning = false;
    this.activeVoices.forEach(voice => voice.release());
    this.activeVoices.clear();
  }

  public stopBootJingle(): void {
    this.jingleTimerIds.forEach((id) => window.clearTimeout(id));
    this.jingleTimerIds = [];
    this.activeJingleVoices.forEach((v) => v.forceKill());
    this.activeJingleVoices.clear();
  }

  public playBootJingle(
    notes: number[],
    stepMs: number,
    preset: SynthPatch,
    fromGesture = false
  ): boolean {
    if (!fromGesture && (!this.ctx || this.ctx.state !== 'running')) {
      return false;
    }
    const ctx = this.initAudio();
    if (!this.filterNode) return false;

    const launch = () => {
      this.stopBootJingle();
      const filterTarget = this.filterNode;
      if (!this.ctx || !filterTarget) return;

      // Sanitize preset envelope & cutoff so even deep basses or slow pads sound crisp as a welcome jingle
      const safePreset: SynthPatch = {
        ...preset,
        attack: Math.min(0.025, Math.max(0.006, preset.attack ?? 0.01)),
        decay: Math.min(0.45, Math.max(0.18, preset.decay ?? 0.35)),
        sustain: Math.max(0.35, Math.min(0.8, preset.sustain ?? 0.65)),
        release: Math.min(0.45, Math.max(0.22, preset.release ?? 0.32)),
        filterAttack: Math.min(0.025, preset.filterAttack ?? 0.01),
        filterCutoff:
          preset.filterType === 'lowpass'
            ? Math.max(1800, preset.filterCutoff || 3500)
            : preset.filterCutoff || 3500,
        arpEnabled: false,
        arpLatch: false,
        glide: 0,
      };

      const startOffsetMs = 20;
      notes.forEach((midi, idx) => {
        const isLast = idx === notes.length - 1;
        const holdMs = isLast ? stepMs * 2.4 : stepMs * 0.92;
        const tStart = window.setTimeout(() => {
          if (!this.ctx || !this.filterNode || this.ctx.state !== 'running') return;
          const clampedMidi = Math.max(36, Math.min(96, midi));
          const freq = 440 * Math.pow(2, (clampedMidi - 69) / 12);
          const vel = isLast ? 86 : 74;
          const voice = new Fm1Voice(
            this.ctx,
            this.filterNode,
            freq,
            vel,
            safePreset,
            clampedMidi,
            this.noiseBuffer
          );
          this.activeJingleVoices.add(voice);
          this.onNoteTriggerCallback?.(clampedMidi, vel);

          const tRel = window.setTimeout(() => {
            voice.release();
            this.activeJingleVoices.delete(voice);
          }, holdMs);
          this.jingleTimerIds.push(tRel);
        }, startOffsetMs + idx * stepMs);

        this.jingleTimerIds.push(tStart);
      });
    };

    if (ctx.state === 'running') {
      launch();
    } else {
      ctx
        .resume()
        .then(() => {
          launch();
        })
        .catch(() => {});
    }
    return true;
  }

  public panic(): void {
    this.stopBootJingle();
    this.clearAllThrows();
    this.pendingRecNotes.clear();
    this.arpKeys.clear();
    this.physicalArpKeys.clear();
    this.arpLatchFreshPress = false;
    if (this.arpTimerId !== null) {
      window.clearTimeout(this.arpTimerId);
      this.arpTimerId = null;
    }
    this.isArpRunning = false;
    this.activeVoices.forEach(voice => voice.forceKill());
    this.activeVoices.clear();
    this.activeSynthLoopVoices.forEach((entry) => {
      window.clearTimeout(entry.timerId);
      entry.voice.forceKill();
    });
    this.activeSynthLoopVoices.clear();
  }

  public setSynthRecording(active: boolean): void {
    this.isSynthRecording = active;
    if (!active) {
      this.pendingRecNotes.clear();
    }
  }

  public getSynthLoopNoteCount(): number {
    return this.synthLoopSteps.reduce((acc, st) => acc + st.length, 0);
  }

  public clearSynthLoop(): void {
    this.synthLoopSteps = Array.from({ length: 64 }, () => []);
    this.pendingRecNotes.clear();
    this.activeSynthLoopVoices.forEach((entry) => {
      window.clearTimeout(entry.timerId);
      entry.voice.release();
    });
    this.activeSynthLoopVoices.clear();
    this.onSynthLoopChange?.(0);
  }

  public setSynthLoopChangeCallback(cb: ((count: number) => void) | null): void {
    this.onSynthLoopChange = cb;
  }

  private applyDrumBusFx(): void {
    if (!this.ctx) return;
    const t = this.ctx.currentTime;
    // 6 clean studio drum bus algorithms (Zero overdrive/distortion)
    const lowDb =
      this.drumBusMode === 3 ? 4.2 : this.drumBusMode === 1 ? 2.0 : this.drumBusMode === 2 ? 1.5 : this.drumBusMode === 5 ? -1.0 : 0;
    const highDb =
      this.drumBusMode === 5 ? 3.8 : this.drumBusMode === 1 ? -1.6 : this.drumBusMode === 2 ? 1.4 : this.drumBusMode === 4 ? 1.0 : 0;
    const compRatio = this.drumBusMode === 2 ? 3.8 : this.drumBusMode === 0 ? 1.4 : 2.2;
    const compThresh = this.drumBusMode === 2 ? -20 : -14;
    const revSendAmt =
      this.drumBusMode === 4 ? 0.48 : this.drumBusMode === 5 ? 0.24 : this.drumBusMode === 1 ? 0.18 : 0.14;

    if (this.drumLowShelf) this.drumLowShelf.gain.setTargetAtTime(lowDb, t, 0.04);
    if (this.drumHighShelf) this.drumHighShelf.gain.setTargetAtTime(highDb, t, 0.04);
    if (this.drumBusComp) {
      this.drumBusComp.ratio.setTargetAtTime(compRatio, t, 0.04);
      this.drumBusComp.threshold.setTargetAtTime(compThresh, t, 0.04);
    }
    if (this.drumReverbSend) {
      this.drumReverbSend.gain.setTargetAtTime(revSendAmt, t, 0.04);
    }
  }

  public setDrumKit(kitIndex: number): void {
    this.drumKitIndex = ((kitIndex % 10) + 10) % 10;
  }

  public setDrumBusMode(modeIndex: number): void {
    this.drumBusMode = ((modeIndex % 6) + 6) % 6;
    this.applyDrumBusFx();
  }

  // Diverse 10-Kit Drum Synthesizer (8 Voices: BD, SD, CH, OH, CP, TM, RD, RS)
  public triggerDrum(drumIndex: number, velocity: number = 100, when?: number): void {
    this.initAudio();
    if (!this.ctx || !this.drumBusGain) return;
    if (this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }

    const t = when ?? this.ctx.currentTime;
    const velNorm = Math.max(0.05, Math.min(1.1, velocity / 127));
    const dest = this.drumBusGain;
    const revSend = this.drumReverbSend;

    // Schedule Drummer animation callback at exact drum hit time
    const delayMs = Math.max(0, (t - this.ctx.currentTime) * 1000);
    if (delayMs < 12) {
      this.onDrumTriggerCallback?.(drumIndex, velocity);
    } else {
      window.setTimeout(() => this.onDrumTriggerCallback?.(drumIndex, velocity), delayMs);
    }

    // 10 distinct kit sonic profiles:
    // 0: TR-808, 1: TR-909, 2: CR-78, 3: LINNDRUM, 4: VIRUS ELECTRO,
    // 5: HOUSE CHICAGO, 6: ACOUSTIC JAZZ, 7: FM PERC DX, 8: LO-FI DUST, 9: TRAP/UK BASS
    const KIT_PROFILES = [
      { kStart: 132, kEnd: 36, kDec: 0.50, snTone: 182, hatHz: 7400, cpHz: 1350, pScale: 1.00 }, // 0: TR-808
      { kStart: 175, kEnd: 48, kDec: 0.34, snTone: 222, hatHz: 9600, cpHz: 1600, pScale: 1.06 }, // 1: TR-909
      { kStart: 118, kEnd: 56, kDec: 0.19, snTone: 215, hatHz: 5600, cpHz: 2100, pScale: 0.94 }, // 2: CR-78
      { kStart: 162, kEnd: 52, kDec: 0.25, snTone: 195, hatHz: 8800, cpHz: 1480, pScale: 1.02 }, // 3: LINNDRUM
      { kStart: 260, kEnd: 42, kDec: 0.32, snTone: 280, hatHz: 10800, cpHz: 1900, pScale: 1.18 }, // 4: VIRUS ELECTRO
      { kStart: 148, kEnd: 50, kDec: 0.29, snTone: 235, hatHz: 9800, cpHz: 1680, pScale: 1.08 }, // 5: HOUSE CHICAGO
      { kStart: 108, kEnd: 58, kDec: 0.23, snTone: 205, hatHz: 6200, cpHz: 1220, pScale: 0.96 }, // 6: ACOUSTIC JAZZ
      { kStart: 270, kEnd: 44, kDec: 0.35, snTone: 315, hatHz: 7900, cpHz: 2150, pScale: 1.24 }, // 7: FM PERC DX
      { kStart: 112, kEnd: 44, kDec: 0.28, snTone: 170, hatHz: 4600, cpHz: 1020, pScale: 0.85 }, // 8: LO-FI DUST
      { kStart: 142, kEnd: 37, kDec: 0.62, snTone: 245, hatHz: 11200, cpHz: 1750, pScale: 1.04 }  // 9: TRAP / UK BASS
    ];
    const kitIdx = this.drumKitIndex;
    const kit = KIT_PROFILES[kitIdx] || KIT_PROFILES[0];
    const isCr78 = kitIdx === 2;
    const isLinn = kitIdx === 3;
    const isJazz = kitIdx === 6;
    const isFm = kitIdx === 7;
    const isLofi = kitIdx === 8;
    const isTrap = kitIdx === 9;
    const subBoost = this.drumBusMode === 3 ? 1.15 : 1.0;
    const sparkleBoost = this.drumBusMode === 5 ? 1.25 : 1.0;

    // Per-voice sound shaping from SEQ Page 1/3 Knobs (Tune, Decay, Tone, Level)
    const tr = this.drumTracks[drumIndex];
    const tuneSemis = tr?.tune ?? 0;
    const tuneScale = Math.pow(2, tuneSemis / 12);
    const decayScale = Math.max(0.25, Math.min(2.0, (tr?.decay ?? 100) / 100));
    const toneScale = Math.max(0.35, Math.min(1.65, 0.35 + ((tr?.tone ?? 60) / 100) * 1.3));
    const trLevel = Math.max(0, Math.min(1.15, (tr?.level ?? 90) / 90));
    const vNorm = velNorm * trLevel;
    const pScale = kit.pScale * tuneScale;

    switch (drumIndex) {
      case 0: { // BD: Kick Drum
        const osc = this.ctx.createOscillator();
        const clickOsc = this.ctx.createOscillator();
        const gain = this.ctx.createGain();
        const clickGain = this.ctx.createGain();
        const dec = kit.kDec * decayScale;
        osc.type = isFm || kitIdx === 4 ? 'triangle' : 'sine';
        osc.frequency.setValueAtTime(kit.kStart * tuneScale, t);
        osc.frequency.exponentialRampToValueAtTime((kit.kEnd + 10) * tuneScale, t + (isCr78 ? 0.032 : 0.055));
        osc.frequency.exponentialRampToValueAtTime(Math.max(24, kit.kEnd * tuneScale), t + dec * 0.82);

        gain.gain.setValueAtTime(0.82 * vNorm * subBoost, t);
        gain.gain.exponentialRampToValueAtTime(0.001, t + dec);
        osc.connect(gain);
        gain.connect(dest);

        // CR-78 & Jazz use gentle soft felt beater; 909/Linn/Electro have crisp punchy beater
        if (!isCr78) {
          clickOsc.type = isFm ? 'sine' : 'triangle';
          clickOsc.frequency.setValueAtTime((isLinn ? 720 : isJazz ? 220 : 420) * pScale * toneScale, t);
          clickOsc.frequency.exponentialRampToValueAtTime(85 * tuneScale, t + 0.02);
          const clickLv = isJazz ? 0.16 : isLofi ? 0.14 : isTrap || kitIdx === 0 ? 0.22 : 0.36;
          clickGain.gain.setValueAtTime(clickLv * vNorm * toneScale, t);
          clickGain.gain.exponentialRampToValueAtTime(0.001, t + 0.024);
          clickOsc.connect(clickGain);
          clickGain.connect(dest);
          clickOsc.start(t);
          clickOsc.stop(t + 0.03);
        }

        osc.start(t);
        osc.stop(t + dec + 0.03);
        break;
      }
      case 1: { // SD: Snare (808, 909, CR-78 brush, Linn gated, Jazz wire, DX FM)
        const dec = (isCr78 ? 0.11 : isJazz ? 0.25 : isLinn ? 0.20 : 0.21) * decayScale;
        const osc = this.ctx.createOscillator();
        const oscGain = this.ctx.createGain();
        osc.type = isCr78 || isJazz || isFm ? 'sine' : 'triangle';
        osc.frequency.setValueAtTime(kit.snTone * tuneScale, t);
        osc.frequency.exponentialRampToValueAtTime(kit.snTone * (isCr78 ? 0.72 : 0.54) * tuneScale, t + 0.05);
        oscGain.gain.setValueAtTime((isCr78 ? 0.38 : 0.54) * vNorm, t);
        oscGain.gain.exponentialRampToValueAtTime(0.001, t + 0.15 * decayScale);
        osc.connect(oscGain);
        oscGain.connect(dest);
        osc.start(t);
        osc.stop(t + 0.17 * decayScale);

        if (this.noiseBuffer) {
          const whiteNoise = this.ctx.createBufferSource();
          whiteNoise.buffer = this.noiseBuffer;
          const filter = this.ctx.createBiquadFilter();
          filter.type = isCr78 ? 'bandpass' : isJazz ? 'highpass' : 'bandpass';
          filter.frequency.setValueAtTime((isCr78 ? 1450 : isLofi ? 1650 : 2550) * pScale * toneScale, t);
          filter.Q.setValueAtTime(isCr78 ? 2.8 : isFm ? 2.2 : 0.85, t);
          const noiseGain = this.ctx.createGain();
          if (isLinn) {
            // Iconic 80s LinnDrum gated snare plateau + sharp gate cut
            noiseGain.gain.setValueAtTime(0.62 * vNorm, t);
            noiseGain.gain.setValueAtTime(0.56 * vNorm, t + dec * 0.65);
            noiseGain.gain.exponentialRampToValueAtTime(0.001, t + dec);
          } else {
            noiseGain.gain.setValueAtTime((isCr78 ? 0.42 : 0.58) * vNorm, t);
            noiseGain.gain.exponentialRampToValueAtTime(0.001, t + dec);
          }
          whiteNoise.connect(filter);
          filter.connect(noiseGain);
          noiseGain.connect(dest);
          if (revSend) noiseGain.connect(revSend);
          whiteNoise.start(t);
          whiteNoise.stop(t + dec + 0.02);
        }
        break;
      }
      case 2: { // CH: Closed Hi-Hat (Authentic CR-78 dual-osc maraca/hat & Acoustic/Trap hats)
        const dec = (isCr78 ? 0.042 : isTrap ? 0.038 : isJazz ? 0.068 : 0.052) * decayScale;
        if (isCr78 || isFm) {
          // Authentic CR-78 / DX metallic dual-oscillator tick
          const o1 = this.ctx.createOscillator();
          const o2 = this.ctx.createOscillator();
          const hpf = this.ctx.createBiquadFilter();
          const g = this.ctx.createGain();
          o1.type = 'square';
          o2.type = 'triangle';
          o1.frequency.setValueAtTime((isCr78 ? 4980 : 3420) * tuneScale, t);
          o2.frequency.setValueAtTime((isCr78 ? 7340 : 5180) * tuneScale, t);
          hpf.type = 'highpass';
          hpf.frequency.setValueAtTime(4200 * toneScale, t);
          g.gain.setValueAtTime(0.24 * vNorm, t);
          g.gain.exponentialRampToValueAtTime(0.001, t + dec);
          o1.connect(hpf);
          o2.connect(hpf);
          hpf.connect(g);
          g.connect(dest);
          o1.start(t);
          o2.start(t);
          o1.stop(t + dec + 0.01);
          o2.stop(t + dec + 0.01);
        }
        if (this.noiseBuffer) {
          const noise = this.ctx.createBufferSource();
          noise.buffer = this.noiseBuffer;
          const hpf = this.ctx.createBiquadFilter();
          hpf.type = 'highpass';
          hpf.frequency.setValueAtTime(kit.hatHz * 0.72 * tuneScale * toneScale, t);
          const bpf = this.ctx.createBiquadFilter();
          bpf.type = isLofi ? 'lowpass' : 'bandpass';
          bpf.frequency.setValueAtTime(kit.hatHz * tuneScale * toneScale, t);
          bpf.Q.setValueAtTime(isJazz ? 0.9 : 2.2 * sparkleBoost, t);
          const gain = this.ctx.createGain();
          gain.gain.setValueAtTime((isCr78 ? 0.22 : 0.38) * vNorm, t);
          gain.gain.exponentialRampToValueAtTime(0.001, t + dec);
          noise.connect(hpf);
          hpf.connect(bpf);
          bpf.connect(gain);
          gain.connect(dest);
          if (revSend) gain.connect(revSend);
          noise.start(t);
          noise.stop(t + dec + 0.02);
        }
        break;
      }
      case 3: { // OH: Open Hi-Hat
        const dec = (isCr78 ? 0.22 : isJazz ? 0.42 : 0.34) * decayScale;
        if (isCr78 || isFm) {
          const o1 = this.ctx.createOscillator();
          const o2 = this.ctx.createOscillator();
          const hpf = this.ctx.createBiquadFilter();
          const g = this.ctx.createGain();
          o1.type = 'square';
          o2.type = 'triangle';
          o1.frequency.setValueAtTime((isCr78 ? 4980 : 3420) * tuneScale, t);
          o2.frequency.setValueAtTime((isCr78 ? 7340 : 5180) * tuneScale, t);
          hpf.type = 'highpass';
          hpf.frequency.setValueAtTime(3800 * toneScale, t);
          g.gain.setValueAtTime(0.20 * vNorm, t);
          g.gain.exponentialRampToValueAtTime(0.001, t + dec);
          o1.connect(hpf);
          o2.connect(hpf);
          hpf.connect(g);
          g.connect(dest);
          o1.start(t);
          o2.start(t);
          o1.stop(t + dec + 0.01);
          o2.stop(t + dec + 0.01);
        }
        if (this.noiseBuffer) {
          const noise = this.ctx.createBufferSource();
          noise.buffer = this.noiseBuffer;
          const hpf = this.ctx.createBiquadFilter();
          hpf.type = 'highpass';
          hpf.frequency.setValueAtTime(kit.hatHz * 0.65 * tuneScale * toneScale, t);
          const bpf = this.ctx.createBiquadFilter();
          bpf.type = isLofi ? 'lowpass' : 'bandpass';
          bpf.frequency.setValueAtTime(kit.hatHz * 0.9 * tuneScale * toneScale, t);
          bpf.Q.setValueAtTime(isJazz ? 0.8 : 1.8 * sparkleBoost, t);
          const gain = this.ctx.createGain();
          gain.gain.setValueAtTime((isCr78 ? 0.26 : 0.40) * vNorm, t);
          gain.gain.exponentialRampToValueAtTime(0.001, t + dec);
          noise.connect(hpf);
          hpf.connect(bpf);
          bpf.connect(gain);
          gain.connect(dest);
          if (revSend) gain.connect(revSend);
          noise.start(t);
          noise.stop(t + dec + 0.02);
        }
        break;
      }
      case 4: { // CP: Handclap / CR-78 Guiro / Jazz Hand Brush
        if (this.noiseBuffer) {
          const dec = (isCr78 ? 0.16 : 0.24) * decayScale;
          const bpf = this.ctx.createBiquadFilter();
          bpf.type = 'bandpass';
          bpf.frequency.setValueAtTime(kit.cpHz * tuneScale * toneScale, t);
          bpf.Q.setValueAtTime(isCr78 ? 3.2 : 1.7, t);
          const g = this.ctx.createGain();
          g.gain.setValueAtTime(0.0001, t);
          const taps = isCr78 ? [0, 0.018, 0.036, 0.054] : [0, 0.011, 0.022, 0.033];
          taps.forEach((offset, idx) => {
            const pk = idx === 3 ? 0.56 * vNorm : 0.36 * vNorm;
            g.gain.setValueAtTime(pk, t + offset);
            if (idx < 3) g.gain.exponentialRampToValueAtTime(0.06 * vNorm, t + offset + 0.009);
          });
          g.gain.exponentialRampToValueAtTime(0.001, t + dec);
          const src = this.ctx.createBufferSource();
          src.buffer = this.noiseBuffer;
          src.connect(bpf);
          bpf.connect(g);
          g.connect(dest);
          if (revSend) g.connect(revSend);
          src.start(t);
          src.stop(t + dec + 0.02);
        }
        break;
      }
      case 5: { // TM: Tom / CR-78 Bongo / Acoustic Rack Tom / Linn Rototom
        const dec = (isCr78 ? 0.16 : isJazz ? 0.34 : 0.28) * decayScale;
        const osc = this.ctx.createOscillator();
        const mod = this.ctx.createOscillator();
        const modGain = this.ctx.createGain();
        const mainGain = this.ctx.createGain();

        const startHz = isCr78 ? 330 * tuneScale : isJazz ? 195 * tuneScale : isLinn ? 310 * tuneScale : 230 * pScale;
        const endHz = isCr78 ? 225 * tuneScale : isJazz ? 128 * tuneScale : isLinn ? 135 * tuneScale : 92 * pScale;
        osc.type = isCr78 || isJazz ? 'sine' : 'triangle';
        osc.frequency.setValueAtTime(startHz, t);
        osc.frequency.exponentialRampToValueAtTime(endHz, t + dec * 0.72);
        mod.frequency.setValueAtTime(360 * pScale * toneScale, t);
        mod.frequency.exponentialRampToValueAtTime(135 * pScale, t + 0.16 * decayScale);

        const modAmt = isCr78 || isJazz ? 12 : isFm ? 460 : 180;
        modGain.gain.setValueAtTime(modAmt * toneScale, t);
        modGain.gain.exponentialRampToValueAtTime(1, t + 0.15 * decayScale);

        mainGain.gain.setValueAtTime(0.56 * vNorm * subBoost, t);
        mainGain.gain.exponentialRampToValueAtTime(0.001, t + dec);

        mod.connect(modGain);
        modGain.connect(osc.frequency);
        osc.connect(mainGain);
        mainGain.connect(dest);
        if (revSend) mainGain.connect(revSend);

        mod.start(t);
        osc.start(t);
        mod.stop(t + dec + 0.02);
        osc.stop(t + dec + 0.02);
        break;
      }
      case 6: { // RD: Metallic Ride / Jazz Bronze Ride / CR-78 Tambourine
        const dec = (isJazz ? 0.64 : isCr78 ? 0.28 : 0.45) * decayScale;
        const carrier = this.ctx.createOscillator();
        const mod = this.ctx.createOscillator();
        const modGain = this.ctx.createGain();
        const hpf = this.ctx.createBiquadFilter();
        const g = this.ctx.createGain();

        carrier.type = isJazz ? 'triangle' : 'square';
        mod.type = isJazz ? 'sine' : 'square';
        carrier.frequency.setValueAtTime((isJazz ? 520 : 490) * pScale, t);
        mod.frequency.setValueAtTime((isJazz ? 815 : 731) * pScale, t);
        modGain.gain.setValueAtTime((isJazz ? 420 : 780) * sparkleBoost * toneScale, t);
        modGain.gain.exponentialRampToValueAtTime(120, t + dec * 0.9);

        hpf.type = 'highpass';
        hpf.frequency.setValueAtTime((isLofi ? 2400 : 3400) * toneScale, t);
        g.gain.setValueAtTime(0.25 * vNorm, t);
        g.gain.exponentialRampToValueAtTime(0.001, t + dec);

        mod.connect(modGain);
        modGain.connect(carrier.frequency);
        carrier.connect(hpf);
        hpf.connect(g);
        g.connect(dest);
        if (revSend) g.connect(revSend);

        mod.start(t);
        carrier.start(t);
        mod.stop(t + dec + 0.02);
        carrier.stop(t + dec + 0.02);
        break;
      }
      case 7: { // RS: Rimshot / CR-78 Woodblock / Jazz Cross-Stick
        const dec = (isCr78 ? 0.044 : isJazz ? 0.062 : 0.048) * decayScale;
        const osc1 = this.ctx.createOscillator();
        const osc2 = this.ctx.createOscillator();
        const bpf = this.ctx.createBiquadFilter();
        const g = this.ctx.createGain();

        osc1.type = isCr78 || isJazz ? 'sine' : 'triangle';
        osc2.type = isCr78 || isJazz ? 'triangle' : 'square';
        osc1.frequency.setValueAtTime((isCr78 ? 1580 : isJazz ? 880 : 460) * pScale, t);
        osc2.frequency.setValueAtTime((isCr78 ? 2370 : isJazz ? 1320 : 1680) * pScale * toneScale, t);
        bpf.type = 'bandpass';
        bpf.frequency.setValueAtTime((isCr78 ? 1750 : isJazz ? 980 : 1550) * pScale * toneScale, t);
        bpf.Q.setValueAtTime(isCr78 ? 4.2 : 2.6 * sparkleBoost, t);

        g.gain.setValueAtTime(0.50 * vNorm, t);
        g.gain.exponentialRampToValueAtTime(0.001, t + dec);

        osc1.connect(bpf);
        osc2.connect(bpf);
        bpf.connect(g);
        g.connect(dest);
        if (revSend) g.connect(revSend);

        osc1.start(t);
        osc2.start(t);
        osc1.stop(t + dec + 0.01);
        osc2.stop(t + dec + 0.01);
        break;
      }
    }
  }

  public setDrumVolume(vol: number): void {
    const clamped = Math.max(0, Math.min(1.0, vol));
    this.drumVolumeLevel = clamped;
    if (this.ctx && this.drumBusGain) {
      this.drumBusGain.gain.setTargetAtTime(clamped, this.ctx.currentTime, 0.03);
    }
  }

  // Dedicated 64-Step Drum Machine Sequencer
  public setDrumTracks(tracks: DrumTrack[], seqLength: number, bpm: number, swing: number = 50): void {
    this.drumTracks = tracks;
    this.seqLength = Math.max(1, Math.min(64, seqLength));
    this.bpm = bpm;
    this.swing = swing;
  }

  public setStepCallback(cb: (step: number) => void): void {
    this.onStepCallback = cb;
  }

  public setNoteTriggerCallback(cb: ((note: number, velocity: number) => void) | null): void {
    this.onNoteTriggerCallback = cb;
  }

  public setDrumTriggerCallback(cb: ((drumIndex: number, velocity: number) => void) | null): void {
    this.onDrumTriggerCallback = cb;
  }

  public startSequencer(): void {
    this.initAudio();
    if (this.isPlayingSeq) return;
    this.isPlayingSeq = true;
    this.currentStep = 0;
    if (this.ctx && this.ctx.state === 'suspended') {
      this.ctx.resume().catch(() => {});
    }
    this.nextStepTime = this.ctx ? this.ctx.currentTime : 0;
    this.scheduleSequencer();
  }

  public stopSequencer(): void {
    this.isPlayingSeq = false;
    if (this.timerId !== null) {
      window.clearTimeout(this.timerId);
      this.timerId = null;
    }
    this.activeSynthLoopVoices.forEach((entry) => {
      window.clearTimeout(entry.timerId);
      entry.voice.release();
    });
    this.activeSynthLoopVoices.clear();
  }

  private scheduleSequencer = (): void => {
    if (!this.isPlayingSeq || !this.ctx) return;

    const currentTime = this.ctx.currentTime;
    const stepDuration = 60 / this.bpm / 4; // 16th note
    const loopLen = Math.max(1, Math.min(64, this.seqLength));

    while (this.nextStepTime < currentTime + 0.1) {
      const stepToPlay = this.currentStep % loopLen;
      const timeToPlay = this.nextStepTime;
      this.lastScheduledStep = stepToPlay;
      this.lastScheduledStepTime = timeToPlay;

      let beatPulse = false;
      this.drumTracks.forEach((track) => {
        if (track.mute) return;
        const stepData = track.steps[stepToPlay];
        if (stepData && stepData.active) {
          const vel = stepData.accent ? 124 : stepData.velocity || 100;
          this.triggerDrum(track.id % 8, vel, timeToPlay);
          if (track.id === 0 || track.id === 1) beatPulse = true;
        }
      });

      // Play recorded Synth Loop notes in sync with the Drum Sequencer
      const loopEvents = this.synthLoopSteps[stepToPlay];
      if (loopEvents && loopEvents.length > 0 && this.filterNode) {
        const nowMs = performance.now();
        const delayMs = Math.max(0, (timeToPlay - currentTime) * 1000);
        const ctxRef = this.ctx;
        const destNode = this.filterNode;
        const patchSnapshot = this.currentPatch;
        const noiseBuf = this.noiseBuffer;

        loopEvents.forEach((ev) => {
          // Skip immediately after live keydown so the first pass doesn't double-trigger
          if (nowMs - ev.recordedAtMs < 180) return;
          const gateMs = Math.max(45, ev.gateSteps * stepDuration * 0.9 * 1000);
          window.setTimeout(() => {
            if (!this.isPlayingSeq || !ctxRef || ctxRef.state === 'closed') return;
            const prev = this.activeSynthLoopVoices.get(ev.note);
            if (prev) {
              window.clearTimeout(prev.timerId);
              prev.voice.release();
              this.activeSynthLoopVoices.delete(ev.note);
            }
            const freq = 440 * Math.pow(2, (ev.note - 69) / 12);
            const v = new Fm1Voice(ctxRef, destNode, freq, ev.velocity, patchSnapshot, ev.note, noiseBuf);
            this.onNoteTriggerCallback?.(ev.note, ev.velocity);
            const relId = window.setTimeout(() => {
              v.release();
              const cur = this.activeSynthLoopVoices.get(ev.note);
              if (cur && cur.voice === v) {
                this.activeSynthLoopVoices.delete(ev.note);
              }
            }, gateMs);
            this.activeSynthLoopVoices.set(ev.note, { voice: v, timerId: relId });
          }, delayMs);
        });
      }

      if (beatPulse) {
        this.onNoteTriggerCallback?.(60 + (stepToPlay % 7), 100);
      }

      if (this.onStepCallback) {
        this.onStepCallback(stepToPlay);
      }

      const isOdd = stepToPlay % 2 === 1;
      const swingOffset = isOdd ? ((this.swing - 50) / 100) * stepDuration * 0.6 : 0;
      this.nextStepTime += stepDuration + swingOffset;
      this.currentStep = (this.currentStep + 1) % loopLen;
    }

    this.timerId = window.setTimeout(this.scheduleSequencer, 25);
  };

  // Real-time Visualizer Audio Data
  public getOscilloscopeData(dataArray: Uint8Array): void {
    if (this.analyser) {
      this.analyser.getByteTimeDomainData(dataArray as unknown as Uint8Array<ArrayBuffer>);
    }
  }

  public getFrequencyData(dataArray: Uint8Array): void {
    if (this.analyser) {
      this.analyser.getByteFrequencyData(dataArray as unknown as Uint8Array<ArrayBuffer>);
    }
  }
}

const VIRUS_SPECTRAL_CURVES: Float32Array[] = (() => {
  const mk = (fn: (x: number) => number) => {
    const c = new Float32Array(256);
    for (let i = 0; i < 256; i++) {
      const x = (i / 256) * Math.PI * 2;
      c[i] = Math.max(-1, Math.min(1, fn(x)));
    }
    return c;
  };
  return [
    mk((x) => Math.sin(x) + 0.5 * Math.sin(2 * x) + 0.25 * Math.sin(3 * x)),
    mk((x) => Math.sin(x) + 0.5 * Math.sin(3 * x) + 0.3 * Math.sin(5 * x)),
    mk((x) => Math.sin(x) * Math.cos(2 * x)),
    mk((x) => Math.sin(x) + Math.sin(2 * x) * Math.sin(3 * x)),
    mk((x) => Math.sin(x) * (1 + 0.45 * Math.sin(5 * x))),
    mk((x) => Math.sin(x) + 0.6 * Math.sin(4 * x) + 0.35 * Math.sin(7 * x))
  ];
})();

function makeVirusPulseCurve(pw: number): Float32Array<ArrayBuffer> {
  const curve = new Float32Array(new ArrayBuffer(256 * 4));
  const clamped = Math.max(0.05, Math.min(0.95, pw));
  for (let i = 0; i < 256; i++) {
    curve[i] = i / 256 < clamped ? -1 : 1;
  }
  return curve;
}

function makeVirusSpectralCurve(pos: number): Float32Array<ArrayBuffer> {
  const curve = new Float32Array(new ArrayBuffer(256 * 4));
  const scaled = Math.max(0, Math.min(1, pos)) * (VIRUS_SPECTRAL_CURVES.length - 1);
  const idx = Math.floor(scaled);
  const next = Math.min(idx + 1, VIRUS_SPECTRAL_CURVES.length - 1);
  const frac = scaled - idx;
  const a = VIRUS_SPECTRAL_CURVES[idx];
  const b = VIRUS_SPECTRAL_CURVES[next];
  for (let i = 0; i < 256; i++) {
    curve[i] = (1 - frac) * a[i] + frac * b[i];
  }
  return curve;
}

function makeVirusSatCurve(type: 'soft' | 'tube' | 'hard' | 'off', drivePct: number): Float32Array<ArrayBuffer> | null {
  if (type === 'off' || drivePct <= 2) return null;
  const curve = new Float32Array(new ArrayBuffer(2048 * 4));
  const amt = (drivePct / 100) * (type === 'hard' ? 14 : type === 'tube' ? 7 : 4);
  for (let i = 0; i < 2048; i++) {
    const x = (i * 2) / 2048 - 1;
    if (type === 'tube') {
      curve[i] = Math.tanh((1 + amt) * x + 0.14 * x * x);
    } else if (type === 'hard') {
      curve[i] = Math.max(-0.92, Math.min(0.92, x * (1 + amt * 0.65)));
    } else {
      curve[i] = (Math.tanh((1 + amt * 0.6) * x) + x * 0.35) / 1.35;
    }
  }
  return curve;
}

/**
 * Individual Polyphonic Voice
 * Handles Access Virus Dual-Filter + Unison/Sync/RingMod/Spectral VA, 6-Op FM, Juno, or SID synthesis
 */
class Fm1Voice {
  private ctx: AudioContext;
  private patch: SynthPatch;
  private midiNote: number;
  private noiseBuffer: AudioBuffer | null;
  private voiceGain: GainNode;
  private tremoloGain: GainNode | null = null;
  private voiceFilter: BiquadFilterNode | null = null;
  private voiceFilter2: BiquadFilterNode | null = null;
  private filterBal1: GainNode | null = null;
  private filterBal2: GainNode | null = null;
  private voicePanner: StereoPannerNode | null = null;
  private operators: {
    osc: OscillatorNode;
    gain: GainNode;
    baseFreq: number;
    feedbackGain?: GainNode;
  }[] = [];
  private extraNodes: AudioNode[] = [];
  private noiseSrc: AudioBufferSourceNode | null = null;
  private isReleased = false;

  private lfo: OscillatorNode | null = null;
  private lfoGains: GainNode[] = [];
  private lfo2: OscillatorNode | null = null;
  private lfo2Gains: GainNode[] = [];
  private swarmOsc: OscillatorNode | null = null;
  private swarmGains: GainNode[] = [];

  constructor(
    ctx: AudioContext,
    destination: AudioNode,
    fundamentalFreq: number,
    velocity: number,
    patch: SynthPatch,
    midiNote: number = 60,
    noiseBuffer: AudioBuffer | null = null
  ) {
    this.ctx = ctx;
    this.patch = patch;
    this.midiNote = midiNote;
    this.noiseBuffer = noiseBuffer;

    this.voiceGain = ctx.createGain();
    const velNorm = velocity / 127;
    this.voiceGain.gain.setValueAtTime(0.0001, ctx.currentTime);

    if (patch.engine === 'virus_va') {
      this.voiceFilter = ctx.createBiquadFilter();
      this.voiceFilter2 = ctx.createBiquadFilter();
      this.tremoloGain = ctx.createGain();
      this.tremoloGain.gain.setValueAtTime(1.0, ctx.currentTime);
      this.voicePanner = ctx.createStereoPanner();
      this.voiceGain.connect(this.tremoloGain);
      this.tremoloGain.connect(this.voicePanner);
      this.voicePanner.connect(destination);
      this.initVirusVoice(fundamentalFreq, velNorm);
    } else if (patch.engine === 'va_poly' || patch.engine === 'juno_106') {
      this.voiceGain.connect(destination);
      this.initVirtualAnalogVoice(fundamentalFreq, velNorm);
    } else if (patch.engine === 'sid_6581') {
      this.voiceGain.connect(destination);
      this.initSidVoice(fundamentalFreq, velNorm);
    } else {
      this.voiceGain.connect(destination);
      this.initFm6Voice(fundamentalFreq, velNorm);
    }
  }

  private getKeyfollowCutoff(baseCutoff: number): number {
    const kf = (this.patch.filterKeyfollow ?? 50) / 100;
    const semitones = (this.midiNote - 60) * kf;
    return Math.max(50, Math.min(19000, baseCutoff * Math.pow(2, semitones / 12)));
  }

  public updateLivePatch(patch: SynthPatch): void {
    if (this.isReleased || !this.voiceFilter) return;
    this.patch = patch;
    const t = this.ctx.currentTime;
    const rawF1 = this.getKeyfollowCutoff(patch.filterCutoff || 4000);
    const f1Type = patch.filterType || 'lowpass';
    const f1Freq = f1Type === 'highpass' ? Math.min(5500, rawF1) : rawF1;
    const f1Q = f1Type === 'bandpass' ? Math.min(10, patch.filterReso || 6) : Math.min(patch.filterReso || 6, 18);
    this.voiceFilter.type = f1Type;
    this.voiceFilter.Q.setTargetAtTime(f1Q, t, 0.03);
    this.voiceFilter.frequency.setTargetAtTime(f1Freq, t, 0.04);

    if (this.voiceFilter2) {
      const rawF2 = this.getKeyfollowCutoff(patch.filter2Cutoff ?? 9500);
      const f2Type = patch.filter2Type || 'lowpass';
      const f2Freq = f2Type === 'highpass' ? Math.min(5500, rawF2) : rawF2;
      const f2Q = f2Type === 'bandpass' ? Math.min(10, patch.filter2Reso ?? 1.5) : Math.min(patch.filter2Reso ?? 1.5, 18);
      this.voiceFilter2.type = f2Type;
      this.voiceFilter2.Q.setTargetAtTime(f2Q, t, 0.03);
      this.voiceFilter2.frequency.setTargetAtTime(f2Freq, t, 0.04);
    }
    if (this.filterBal1 && this.filterBal2) {
      const bal = (patch.filterBalance ?? 50) / 100;
      this.filterBal1.gain.setTargetAtTime(1 - bal * 0.5, t, 0.03);
      this.filterBal2.gain.setTargetAtTime(0.5 + bal * 0.5, t, 0.03);
    }
    if (this.lfo && patch.lfoRate) {
      this.lfo.frequency.setTargetAtTime(patch.lfoRate, t, 0.03);
    }
    if (this.lfo2 && patch.lfo2Rate) {
      this.lfo2.frequency.setTargetAtTime(patch.lfo2Rate, t, 0.03);
    }
  }

  public setTapeStop(active: boolean): void {
    if (this.isReleased) return;
    const t = this.ctx.currentTime;
    this.operators.forEach(op => {
      try {
        op.osc.frequency.cancelScheduledValues(t);
        op.osc.frequency.setValueAtTime(Math.max(12, op.osc.frequency.value || op.baseFreq), t);
        if (active) {
          op.osc.frequency.exponentialRampToValueAtTime(Math.max(12, op.baseFreq * 0.34), t + 0.28);
        } else {
          op.osc.frequency.exponentialRampToValueAtTime(Math.max(15, op.baseFreq), t + 0.08);
        }
      } catch {
        // ignore stopped osc
      }
    });
    if (this.voiceFilter) {
      const baseCut = Math.max(60, Math.min(18500, this.patch.filterCutoff || 4000));
      this.voiceFilter.frequency.setTargetAtTime(active ? 420 : baseCut, t, 0.07);
    }
  }

  private initVirusVoice(f0: number, velNorm: number): void {
    const t = Math.max(0, this.ctx.currentTime);
    const f1 = this.voiceFilter!;
    const f2 = this.voiceFilter2!;
    const routing = this.patch.filterRouting || 'serial';

    // Dual-Filter Topology (HelgeSverre/supersaw voice.js: serial, parallel, split)
    const osc1Bus = this.ctx.createGain();
    const osc2Bus = this.ctx.createGain();
    const sat1 = this.ctx.createWaveShaper();
    const sat2 = this.ctx.createWaveShaper();
    const satCurve = makeVirusSatCurve(this.patch.saturationType || 'soft', this.patch.fxDrive ?? 35);
    sat1.curve = satCurve;
    sat2.curve = satCurve;
    this.extraNodes.push(osc1Bus, osc2Bus, sat1, sat2);

    const bal = (this.patch.filterBalance ?? 50) / 100;
    const f1TypePreview = this.patch.filterType || 'lowpass';
    const f2TypePreview = this.patch.filter2Type || 'lowpass';
    const c1Preview = f1TypePreview === 'highpass' ? Math.min(5500, this.patch.filterCutoff || 4500) : (this.patch.filterCutoff || 4500);
    const c2Preview = f2TypePreview === 'highpass' ? Math.min(5500, this.patch.filter2Cutoff ?? 11500) : (this.patch.filter2Cutoff ?? 11500);
    const serialBandsConflict =
      (f1TypePreview === 'highpass' && f2TypePreview === 'lowpass' && c1Preview >= c2Preview * 0.8) ||
      (f1TypePreview === 'lowpass' && f2TypePreview === 'highpass' && c2Preview >= c1Preview * 0.8);

    if (routing === 'serial') {
      osc1Bus.connect(sat1);
      osc2Bus.connect(sat1);
      if (bal <= 0.14) {
        // Balance full Filter 1: bypass Filter 2 so closed F2 cannot mute F1 patches
        sat1.connect(f1);
        f1.connect(this.voiceGain);
      } else if (bal >= 0.86) {
        // Balance full Filter 2: bypass Filter 1 so highpassed F1 cannot mute F2 patches
        sat1.connect(f2);
        f2.connect(this.voiceGain);
      } else if (serialBandsConflict) {
        // Avoid brickwall HP > LP deadzone when both filters target opposing ends of the spectrum
        this.filterBal1 = this.ctx.createGain();
        this.filterBal2 = this.ctx.createGain();
        this.filterBal1.gain.setValueAtTime(1 - bal * 0.65, t);
        this.filterBal2.gain.setValueAtTime(0.35 + bal * 0.65, t);
        this.extraNodes.push(this.filterBal1, this.filterBal2);
        sat1.connect(f1);
        sat1.connect(f2);
        f1.connect(this.filterBal1);
        f2.connect(this.filterBal2);
        this.filterBal1.connect(this.voiceGain);
        this.filterBal2.connect(this.voiceGain);
      } else {
        sat1.connect(f1);
        f1.connect(f2);
        f2.connect(this.voiceGain);
      }
    } else if (routing === 'parallel') {
      this.filterBal1 = this.ctx.createGain();
      this.filterBal2 = this.ctx.createGain();
      this.filterBal1.gain.setValueAtTime(1 - bal * 0.65, t);
      this.filterBal2.gain.setValueAtTime(0.35 + bal * 0.65, t);
      this.extraNodes.push(this.filterBal1, this.filterBal2);

      osc1Bus.connect(sat1);
      osc2Bus.connect(sat1);
      sat1.connect(f1);
      sat1.connect(f2);
      f1.connect(this.filterBal1);
      f2.connect(this.filterBal2);
      this.filterBal1.connect(this.voiceGain);
      this.filterBal2.connect(this.voiceGain);
    } else {
      // 'split': Osc1+Sub -> Filter 1, Osc2+Ring+Noise -> Filter 2
      this.filterBal1 = this.ctx.createGain();
      this.filterBal2 = this.ctx.createGain();
      this.filterBal1.gain.setValueAtTime(1 - bal * 0.5, t);
      this.filterBal2.gain.setValueAtTime(0.5 + bal * 0.5, t);
      this.extraNodes.push(this.filterBal1, this.filterBal2);

      osc1Bus.connect(sat1);
      osc2Bus.connect(sat2);
      sat1.connect(f1);
      sat2.connect(f2);
      f1.connect(this.filterBal1);
      f2.connect(this.filterBal2);
      this.filterBal1.connect(this.voiceGain);
      this.filterBal2.connect(this.voiceGain);
    }

    // Evaluate Mod Matrix
    const cords = getEffectiveModMatrix(this.patch);
    let velSpreadDelta = 0;
    let velCutoffFactor = 1.0;
    let velResoDelta = 0;
    let velDriveDelta = 0;
    let envCutoffOverride: number | null = null;

    cords.forEach(cord => {
      const amt = cord.amount / 100;
      if (cord.source === 'velocity') {
        if (cord.dest === 'cutoff') velCutoffFactor += (velNorm - 0.5) * amt * 1.4;
        if (cord.dest === 'spread') velSpreadDelta += (velNorm - 0.5) * amt * 0.5;
        if (cord.dest === 'reso') velResoDelta += velNorm * amt * 5.0;
        if (cord.dest === 'drive') velDriveDelta += velNorm * amt * 35;
      } else if (cord.source === 'filterEnv' && cord.dest === 'cutoff') {
        envCutoffOverride = cord.amount;
      }
    });

    // Oscillator Mode, Pulse Width Shaper & Spectral Wavetable Curve
    const oscMode = this.patch.oscMode || (this.patch.wave === 'fm' ? 'pulse' : this.patch.wave === 'triangle' ? 'triangle' : 'hypersaw');
    const oscWaveMap: Record<string, OscillatorType> = {
      saw: 'sawtooth',
      triangle: 'triangle',
      sine: 'sine',
      fm: 'square'
    };
    const fallbackType: OscillatorType = oscWaveMap[this.patch.wave || 'saw'] || 'sawtooth';
    const pulseCurve = oscMode === 'pulse' ? makeVirusPulseCurve(this.patch.pulseWidth ?? 0.35) : null;
    const spectralCurve = oscMode === 'wavetable' ? makeVirusSpectralCurve(this.patch.wavetablePos ?? 0.4) : null;

    const oscBal = (this.patch.oscBalance ?? 48) / 100; // 0=Osc1, 1=Osc2
    const osc1Weight = (1 - oscBal * 0.55) * velNorm;
    const osc2Weight = (0.35 + oscBal * 0.65) * velNorm;
    const osc2Semi = this.patch.osc2Semitone ?? 0;
    const osc2Det = this.patch.osc2Detune ?? 8;
    const unisonVoices = Math.max(1, Math.min(5, this.patch.unisonVoices ?? 1));
    const userSpread = Math.max(0, Math.min(1.5, ((this.patch.hypersawSpread ?? 80) / 100) + velSpreadDelta));
    const panSpread = (this.patch.unisonPanSpread ?? 75) / 100;

    if (oscMode === 'hypersaw' && unisonVoices <= 1) {
      // Classic 7-oscillator Virus TI Hypersaw stack with stereo spread
      const detunes = [-16.8, -9.2, -3.5, 0.0, 3.5, 9.2, 16.8];
      detunes.forEach((cents, idx) => {
        const osc = this.ctx.createOscillator();
        osc.type = fallbackType;
        const freq = Math.max(15, f0 * Math.pow(2, (cents * userSpread) / 1200));
        osc.frequency.setValueAtTime(freq, t);

        const gain = this.ctx.createGain();
        gain.gain.setValueAtTime(0.115 * osc1Weight, t);

        const pan = this.ctx.createStereoPanner();
        pan.pan.setValueAtTime(((idx - 3) / 3) * panSpread * 0.75, t);
        this.extraNodes.push(pan);

        osc.connect(gain);
        gain.connect(pan);
        pan.connect(osc1Bus);
        osc.start(t);
        this.operators.push({ osc, gain, baseFreq: freq });
      });

      // Harmonic / interval Osc 2 layered alongside Hypersaw
      if (osc2Semi !== 0 || Math.abs(osc2Det) > 1 || this.patch.osc2Sync) {
        const osc2 = this.ctx.createOscillator();
        osc2.type = 'sawtooth';
        const f2Hz = Math.max(15, f0 * Math.pow(2, osc2Semi / 12 + osc2Det / 1200));
        osc2.frequency.setValueAtTime(f2Hz, t);
        const g2 = this.ctx.createGain();
        g2.gain.setValueAtTime(0.22 * osc2Weight, t);
        osc2.connect(g2);
        g2.connect(osc2Bus);
        osc2.start(t);
        this.operators.push({ osc: osc2, gain: g2, baseFreq: f2Hz });
      }
    } else {
      // Dual-Oscillator + Stereo Unison Stack (HelgeSverre/supersaw virus.js + osc.js)
      const count = unisonVoices > 1 ? unisonVoices : 2;
      const perVoiceScale = 0.38 / Math.sqrt(count);
      const half = (count - 1) / 2;

      for (let u = 0; u < count; u++) {
        const offsetNorm = half > 0 ? (u - half) / half : 0; // -1..+1
        const uniCents = offsetNorm * (24 * Math.max(0.2, userSpread));
        const panNode = this.ctx.createStereoPanner();
        panNode.pan.setValueAtTime(Math.max(-1, Math.min(1, offsetNorm * panSpread)), t);
        this.extraNodes.push(panNode);

        // Oscillator 1
        const osc1 = this.ctx.createOscillator();
        osc1.type = oscMode === 'triangle' ? 'triangle' : 'sawtooth';
        const f1Hz = Math.max(15, f0 * Math.pow(2, uniCents / 1200));
        osc1.frequency.setValueAtTime(f1Hz, t);

        const g1 = this.ctx.createGain();
        g1.gain.setValueAtTime(perVoiceScale * osc1Weight, t);

        if (pulseCurve || spectralCurve) {
          const shaper = this.ctx.createWaveShaper();
          shaper.curve = pulseCurve || spectralCurve;
          this.extraNodes.push(shaper);
          osc1.connect(shaper);
          shaper.connect(g1);
        } else {
          osc1.connect(g1);
        }
        g1.connect(panNode);
        panNode.connect(osc1Bus);
        osc1.start(t);
        this.operators.push({ osc: osc1, gain: g1, baseFreq: f1Hz });

        // Oscillator 2 (with Semitone, Detune, Hard Sync / FM Cross-Mod)
        const osc2 = this.ctx.createOscillator();
        osc2.type = oscMode === 'triangle' ? 'triangle' : 'sawtooth';
        const f2Hz = Math.max(15, f0 * Math.pow(2, osc2Semi / 12 + (uniCents + osc2Det) / 1200));
        osc2.frequency.setValueAtTime(f2Hz, t);

        const g2 = this.ctx.createGain();
        g2.gain.setValueAtTime(perVoiceScale * osc2Weight, t);

        // Hard Sync / FM modulation into Osc 1 (matching Virooos osc.js syncGain)
        if (this.patch.osc2Sync || (this.patch.fmAmount ?? 0) > 0) {
          const crossGain = this.ctx.createGain();
          const modAmt = (this.patch.osc2Sync ? 550 : 0) + ((this.patch.fmAmount ?? 0) / 100) * f0 * 2.2;
          crossGain.gain.setValueAtTime(modAmt, t);
          this.extraNodes.push(crossGain);
          osc2.connect(crossGain);
          crossGain.connect(osc1.frequency);
        }

        const pan2 = this.ctx.createStereoPanner();
        pan2.pan.setValueAtTime(Math.max(-1, Math.min(1, -offsetNorm * panSpread * 0.7)), t);
        this.extraNodes.push(pan2);

        osc2.connect(g2);
        g2.connect(pan2);
        pan2.connect(osc2Bus);
        osc2.start(t);
        this.operators.push({ osc: osc2, gain: g2, baseFreq: f2Hz });
      }
    }

    // Ring Modulator (Osc 1 carrier * Osc 2 modulator)
    const ringAmt = (this.patch.ringModVolume ?? 0) / 100;
    if (ringAmt > 0.02) {
      const rCarrier = this.ctx.createOscillator();
      const rMod = this.ctx.createOscillator();
      const rMult = this.ctx.createGain();
      const rOut = this.ctx.createGain();
      rCarrier.type = 'sine';
      rMod.type = 'sine';
      rCarrier.frequency.setValueAtTime(Math.max(15, f0), t);
      rMod.frequency.setValueAtTime(Math.max(15, f0 * Math.pow(2, osc2Semi / 12 + osc2Det / 1200)), t);
      rMult.gain.setValueAtTime(0, t);
      rOut.gain.setValueAtTime(0.24 * ringAmt * velNorm, t);

      rCarrier.connect(rMult);
      rMod.connect(rMult.gain);
      rMult.connect(rOut);
      rOut.connect(osc2Bus);
      rCarrier.start(t);
      rMod.start(t);
      this.extraNodes.push(rMult, rOut);
      this.operators.push(
        { osc: rCarrier, gain: rOut, baseFreq: f0 },
        { osc: rMod, gain: rMult, baseFreq: f0 }
      );
    }

    // Sub-oscillator (-1 octave)
    const subLevel = (this.patch.subOscLevel ?? 75) / 100;
    if (subLevel > 0.01) {
      const sub = this.ctx.createOscillator();
      const subWaveMap: Record<string, OscillatorType> = {
        square: 'square',
        sine: 'sine',
        saw: 'sawtooth'
      };
      sub.type = subWaveMap[this.patch.subWaveform || 'square'] || 'square';
      const subFreq = Math.max(10, f0 * 0.5);
      sub.frequency.setValueAtTime(subFreq, t);
      const subGain = this.ctx.createGain();
      subGain.gain.setValueAtTime(0.21 * velNorm * subLevel, t);
      sub.connect(subGain);
      subGain.connect(osc1Bus);
      sub.start(t);
      this.operators.push({ osc: sub, gain: subGain, baseFreq: subFreq });
    }

    // Noise Generator (crucial for Virus C pads like ZartPad J & breathy leads)
    const noiseVol = (this.patch.noiseVolume ?? 0) / 100;
    if (noiseVol > 0.01 && this.noiseBuffer) {
      const nSrc = this.ctx.createBufferSource();
      nSrc.buffer = this.noiseBuffer;
      nSrc.loop = true;
      const nGain = this.ctx.createGain();
      nGain.gain.setValueAtTime(0.14 * noiseVol * velNorm, t);
      nSrc.connect(nGain);
      nGain.connect(osc2Bus);
      nSrc.start(t);
      this.noiseSrc = nSrc;
      this.extraNodes.push(nGain);
    }

    // Per-Voice Dual Filter 1 + Filter 2 Envelopes with Key Tracking
    const f1Type = this.patch.filterType || 'lowpass';
    const rawBaseCutoff1 = this.getKeyfollowCutoff(Math.max(60, Math.min(18500, (this.patch.filterCutoff || 4500) * Math.max(0.25, velCutoffFactor))));
    const baseCutoff = f1Type === 'highpass' ? Math.min(5500, rawBaseCutoff1) : rawBaseCutoff1;
    const rawReso1 = Math.max(0.5, Math.min(18, (this.patch.filterReso || 6.0) + velResoDelta));
    const effReso = f1Type === 'bandpass' ? Math.min(10, rawReso1) : rawReso1;
    const fAtk = Math.max(0.004, this.patch.filterAttack ?? 0.008);
    const fDec = Math.max(0.02, this.patch.filterDecay ?? 0.42);
    const fSus = Math.max(0.0, Math.min(1.0, this.patch.filterSustain ?? 0.35));

    f1.type = f1Type;
    f1.Q.setValueAtTime(effReso, t);
    const envAmt1 = (envCutoffOverride ?? this.patch.filterEnvAmount ?? 45) / 100;
    const peak1 = envAmt1 >= 0
      ? Math.min(f1Type === 'highpass' ? 6500 : 19000, baseCutoff + (18500 - baseCutoff) * envAmt1 * 0.85)
      : Math.max(60, baseCutoff * (1 + envAmt1 * 0.85));
    const sus1 = baseCutoff + (peak1 - baseCutoff) * fSus;
    f1.frequency.setValueAtTime(baseCutoff, t);
    f1.frequency.linearRampToValueAtTime(peak1, t + fAtk);
    f1.frequency.exponentialRampToValueAtTime(Math.max(60, sus1), t + fAtk + fDec);

    const f2Type = this.patch.filter2Type || 'lowpass';
    const rawBaseCutoff2 = this.getKeyfollowCutoff(Math.max(60, Math.min(18500, this.patch.filter2Cutoff ?? 11500)));
    const baseCutoff2 = f2Type === 'highpass' ? Math.min(5500, rawBaseCutoff2) : rawBaseCutoff2;
    const rawReso2 = Math.max(0.5, Math.min(18, this.patch.filter2Reso ?? 1.2));
    const effReso2 = f2Type === 'bandpass' ? Math.min(10, rawReso2) : rawReso2;
    f2.type = f2Type;
    f2.Q.setValueAtTime(effReso2, t);
    const envAmt2 = (this.patch.filter2EnvAmount ?? Math.round(envAmt1 * 60)) / 100;
    const peak2 = envAmt2 >= 0
      ? Math.min(f2Type === 'highpass' ? 6500 : 19000, baseCutoff2 + (18500 - baseCutoff2) * envAmt2 * 0.75)
      : Math.max(60, baseCutoff2 * (1 + envAmt2 * 0.75));
    const sus2 = baseCutoff2 + (peak2 - baseCutoff2) * fSus;
    f2.frequency.setValueAtTime(baseCutoff2, t);
    f2.frequency.linearRampToValueAtTime(peak2, t + fAtk);
    f2.frequency.exponentialRampToValueAtTime(Math.max(60, sus2), t + fAtk + fDec);

    // Helper to connect a continuous modulation oscillator into any Mod Matrix destination
    const routeModSignal = (modOsc: OscillatorNode, dest: string, amountPct: number, gainStore: GainNode[]) => {
      if (Math.abs(amountPct) < 1) return;
      const g = this.ctx.createGain();
      const absNorm = amountPct / 100;
      if (dest === 'pitch') {
        g.gain.setValueAtTime(f0 * (Math.pow(2, (absNorm * 1.5) / 12) - 1), t);
        modOsc.connect(g);
        this.operators.forEach(op => g.connect(op.osc.frequency));
      } else if (dest === 'cutoff' || dest === 'filter') {
        g.gain.setValueAtTime(baseCutoff * absNorm * 0.85, t);
        modOsc.connect(g);
        g.connect(f1.frequency);
        if (routing !== 'serial') g.connect(f2.frequency);
      } else if (dest === 'filter2') {
        g.gain.setValueAtTime(baseCutoff2 * absNorm * 0.85, t);
        modOsc.connect(g);
        g.connect(f2.frequency);
      } else if (dest === 'reso') {
        g.gain.setValueAtTime(absNorm * 7.5, t);
        modOsc.connect(g);
        g.connect(f1.Q);
      } else if (dest === 'pan' && this.voicePanner) {
        g.gain.setValueAtTime(Math.max(-0.95, Math.min(0.95, absNorm)), t);
        modOsc.connect(g);
        g.connect(this.voicePanner.pan);
      } else if (dest === 'amp' && this.tremoloGain) {
        g.gain.setValueAtTime(Math.min(0.75, Math.abs(absNorm) * 0.72), t);
        modOsc.connect(g);
        g.connect(this.tremoloGain.gain);
      } else if (dest === 'spread' || dest === 'detune') {
        g.gain.setValueAtTime(absNorm * 35, t);
        modOsc.connect(g);
        this.operators.slice(0, 7).forEach((op, idx) => {
          if (idx % 2 === 0) g.connect(op.osc.detune);
        });
      }
      gainStore.push(g);
    };

    const lfoWaveMap: Record<string, OscillatorType> = {
      sine: 'sine',
      triangle: 'triangle',
      saw: 'sawtooth',
      square: 'square'
    };

    // LFO 1
    const lfoCords = cords.filter(c => c.source === 'lfo1' && Math.abs(c.amount) > 0);
    if (lfoCords.length > 0 || (this.patch.lfoDepth ?? 0) > 0) {
      const lfo = this.ctx.createOscillator();
      lfo.type = lfoWaveMap[this.patch.lfoShape || 'sine'] || 'sine';
      lfo.frequency.setValueAtTime(this.patch.lfoRate || 4.5, t);

      if ((this.patch.lfoDepth ?? 0) > 0) {
        routeModSignal(lfo, this.patch.lfoDest || 'filter', this.patch.lfoDepth ?? 0, this.lfoGains);
      } else {
        lfoCords.forEach(cord => routeModSignal(lfo, cord.dest, cord.amount, this.lfoGains));
      }
      lfo.start(t);
      this.lfo = lfo;
    }

    // LFO 2 (Independent secondary Virus LFO)
    if ((this.patch.lfo2Depth ?? 0) > 0) {
      const lfo2 = this.ctx.createOscillator();
      lfo2.type = lfoWaveMap[this.patch.lfo2Shape || 'triangle'] || 'triangle';
      lfo2.frequency.setValueAtTime(this.patch.lfo2Rate || 1.2, t);
      routeModSignal(lfo2, this.patch.lfo2Dest || 'pan', this.patch.lfo2Depth ?? 0, this.lfo2Gains);
      lfo2.start(t);
      this.lfo2 = lfo2;
    }

    // Free-Running Analog Swarm Drift Oscillator
    const swarmCords = cords.filter(c => c.source === 'swarm' && Math.abs(c.amount) > 0);
    if (swarmCords.length > 0) {
      const swarmOsc = this.ctx.createOscillator();
      swarmOsc.type = 'triangle';
      const voiceJitter = 0.88 + ((f0 % 17) / 17) * 0.28;
      swarmOsc.frequency.setValueAtTime(Math.max(0.05, (this.patch.swarmRate ?? 0.38) * voiceJitter), t);
      swarmCords.forEach(cord => routeModSignal(swarmOsc, cord.dest, cord.amount, this.swarmGains));
      swarmOsc.start(t);
      this.swarmOsc = swarmOsc;
    }

    // Dynamic Psychoacoustic Loudness Equalization & Normalization (calibrated for 15% default slider)
    const filterModeBoost =
      f1Type === 'bandpass' ? 1.55 : f1Type === 'highpass' ? 1.32 : 1.0;
    const cutoffComp =
      baseCutoff < 3200
        ? 1.0 + ((3200 - baseCutoff) / 3200) * 1.05
        : baseCutoff > 8500
        ? 0.85
        : 1.0;
    const drive = Math.max(0, Math.min(100, (this.patch.fxDrive || 40) + velDriveDelta));
    const driveComp = 1.0 / (1.0 + (drive / 100) * 0.45);
    const masterGainNorm = Math.max(0.85, (this.patch.masterGain || 3700) / 3700);
    const resoComp = effReso > 10 ? 1.0 / (1.0 + (effReso - 10) * 0.04) : 1.0;

    const normalizedVoiceGain = Math.min(
      1.35,
      0.8 * filterModeBoost * cutoffComp * driveComp * masterGainNorm * resoComp
    );

    // Dynamic Amp ADSR Envelope
    const attack = Math.max(0.005, this.patch.attack ?? 0.008);
    const decay = Math.max(0.01, this.patch.decay ?? 0.85);
    const sustain = Math.max(0.001, Math.min(1.0, this.patch.sustain ?? 0.75));

    this.voiceGain.gain.setValueAtTime(0.0001, t);
    this.voiceGain.gain.linearRampToValueAtTime(Math.max(0.001, normalizedVoiceGain * velNorm), t + attack);
    this.voiceGain.gain.linearRampToValueAtTime(Math.max(0.0001, normalizedVoiceGain * velNorm * sustain), t + attack + decay);
  }

  private initFm6Voice(f0: number, velNorm: number): void {
    const t = this.ctx.currentTime;
    const algo = getAlgorithm(this.patch.algorithm);

    // Create 6 Operators
    this.patch.operators.forEach(opConfig => {
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();

      // Ratio & Detune
      const freq = f0 * opConfig.ratio * Math.pow(2, opConfig.detune / 1200);
      osc.frequency.setValueAtTime(Math.max(10, freq), t);
      osc.type = 'sine';

      // DX Envelope Mapping to Time & Gain
      // Rate: 99 = fast (~5ms), 0 = very slow (~8s)
      const attackTime = Math.max(0.005, (99 - opConfig.rate1) / 99 * 1.5);
      const decayTime = Math.max(0.01, (99 - opConfig.rate2) / 99 * 2.5);
      const sustainLevel = (opConfig.level3 / 99) * (opConfig.level / 99);
      const peakLevel = (opConfig.level1 / 99) * (opConfig.level / 99);

      // Scale carrier gain by master voice, modulator by FM index (typically 0 - 3000 Hz)
      const maxFmIndex = opConfig.isCarrier ? 0.35 * velNorm : 1800 * (opConfig.level / 99);

      gain.gain.setValueAtTime(0.0001, t);
      gain.gain.exponentialRampToValueAtTime(Math.max(0.0001, peakLevel * maxFmIndex), t + attackTime);
      gain.gain.exponentialRampToValueAtTime(Math.max(0.0001, sustainLevel * maxFmIndex), t + attackTime + decayTime);

      osc.connect(gain);
      osc.start(t);

      this.operators.push({ osc, gain, baseFreq: freq });
    });

    // Wire up algorithm routes (Modulator Gain -> Target Osc.frequency)
    algo.routes.forEach(([from, to]) => {
      const fromIdx = from - 1;
      const toIdx = to - 1;
      if (from === to) {
        // Feedback loop
        if (this.patch.feedback > 0 && this.operators[fromIdx]) {
          const fbGain = this.ctx.createGain();
          const fbAmount = Math.pow(2, this.patch.feedback) * 60;
          fbGain.gain.setValueAtTime(fbAmount, t);
          this.operators[fromIdx].gain.connect(fbGain);
          fbGain.connect(this.operators[fromIdx].osc.frequency);
          this.operators[fromIdx].feedbackGain = fbGain;
        }
      } else {
        if (this.operators[fromIdx] && this.operators[toIdx]) {
          this.operators[fromIdx].gain.connect(this.operators[toIdx].osc.frequency);
        }
      }
    });

    // Wire carriers to voice output
    algo.carriers.forEach(carrierId => {
      const cIdx = carrierId - 1;
      if (this.operators[cIdx]) {
        this.operators[cIdx].gain.connect(this.voiceGain);
      }
    });

    // Voice master envelope
    this.voiceGain.gain.setValueAtTime(0.0001, t);
    this.voiceGain.gain.exponentialRampToValueAtTime(0.35 * velNorm, t + 0.01);
  }

  private initVirtualAnalogVoice(f0: number, velNorm: number): void {
    const t = this.ctx.currentTime;
    // Dual saw/square oscillators with detune + sub-oscillator
    const osc1 = this.ctx.createOscillator();
    const osc2 = this.ctx.createOscillator();
    const subOsc = this.ctx.createOscillator();

    osc1.type = 'sawtooth';
    osc1.frequency.setValueAtTime(f0 * 0.998, t);

    osc2.type = 'sawtooth';
    osc2.frequency.setValueAtTime(f0 * 1.002, t);

    subOsc.type = 'square';
    subOsc.frequency.setValueAtTime(f0 * 0.5, t); // Sub octave

    const g1 = this.ctx.createGain();
    const g2 = this.ctx.createGain();
    const gSub = this.ctx.createGain();

    g1.gain.setValueAtTime(0.25 * velNorm, t);
    g2.gain.setValueAtTime(0.25 * velNorm, t);
    gSub.gain.setValueAtTime(0.20 * velNorm, t);

    osc1.connect(g1);
    osc2.connect(g2);
    subOsc.connect(gSub);

    g1.connect(this.voiceGain);
    g2.connect(this.voiceGain);
    gSub.connect(this.voiceGain);

    osc1.start(t);
    osc2.start(t);
    subOsc.start(t);

    this.operators.push(
      { osc: osc1, gain: g1, baseFreq: f0 * 0.998 },
      { osc: osc2, gain: g2, baseFreq: f0 * 1.002 },
      { osc: subOsc, gain: gSub, baseFreq: f0 * 0.5 }
    );
    this.voiceGain.gain.setValueAtTime(0.0001, t);
    this.voiceGain.gain.exponentialRampToValueAtTime(0.4 * velNorm, t + 0.015);
  }

  private initSidVoice(f0: number, velNorm: number): void {
    const t = this.ctx.currentTime;
    // C64 Pulse/Square with bright 8-bit harmonic bite
    const osc = this.ctx.createOscillator();
    osc.type = 'square';
    osc.frequency.setValueAtTime(f0, t);

    // Arp fast vibrato
    const vibrato = this.ctx.createOscillator();
    vibrato.frequency.setValueAtTime(50, t); // 50Hz Commodore frame rate!
    const vibGain = this.ctx.createGain();
    vibGain.gain.setValueAtTime(15, t);
    vibrato.connect(vibGain);
    vibGain.connect(osc.frequency);

    const gain = this.ctx.createGain();
    gain.gain.setValueAtTime(0.35 * velNorm, t);

    osc.connect(gain);
    gain.connect(this.voiceGain);

    osc.start(t);
    vibrato.start(t);

    this.operators.push({ osc, gain, baseFreq: f0 });
    this.voiceGain.gain.setValueAtTime(0.0001, t);
    this.voiceGain.gain.exponentialRampToValueAtTime(0.45 * velNorm, t + 0.005);
  }

  public release(): void {
    if (this.isReleased) return;
    this.isReleased = true;

    const t = this.ctx.currentTime;
    const releaseTime = Math.max(0.02, Math.min(this.patch.release ?? 0.35, 4.0));

    // Ramp down voice filter & master gain safely
    if (this.voiceFilter) {
      const fRel = Math.max(0.02, Math.min(this.patch.filterRelease ?? releaseTime, 3.0));
      const baseCut = Math.max(60, Math.min(18500, this.patch.filterCutoff || 4000));
      this.voiceFilter.frequency.cancelScheduledValues(t);
      this.voiceFilter.frequency.setValueAtTime(Math.max(60, this.voiceFilter.frequency.value || baseCut), t);
      this.voiceFilter.frequency.exponentialRampToValueAtTime(Math.max(60, baseCut * 0.5), t + fRel);
    }

    this.voiceGain.gain.cancelScheduledValues(t);
    const curr = Math.max(0.0001, this.voiceGain.gain.value || 0.4);
    this.voiceGain.gain.setValueAtTime(curr, t);
    this.voiceGain.gain.linearRampToValueAtTime(0.0001, t + releaseTime);

    this.scheduleCleanup(releaseTime);
  }

  public forceKill(): void {
    if (this.isReleased) return;
    this.isReleased = true;
    const t = this.ctx.currentTime;
    this.voiceGain.gain.cancelScheduledValues(t);
    this.voiceGain.gain.setValueAtTime(Math.max(0.0001, this.voiceGain.gain.value || 0.2), t);
    this.voiceGain.gain.linearRampToValueAtTime(0.0001, t + 0.015);
    this.scheduleCleanup(0.02);
  }

  private scheduleCleanup(delaySec: number): void {
    window.setTimeout(() => {
      this.operators.forEach(op => {
        try {
          op.osc.stop();
          op.osc.disconnect();
          op.gain.disconnect();
        } catch {
          // already stopped
        }
      });
      if (this.noiseSrc) {
        try {
          this.noiseSrc.stop();
          this.noiseSrc.disconnect();
        } catch {
          // ignore
        }
      }
      this.extraNodes.forEach(node => {
        try {
          node.disconnect();
        } catch {
          // ignore
        }
      });
      if (this.lfo) {
        try {
          this.lfo.stop();
          this.lfo.disconnect();
          this.lfoGains.forEach(g => g.disconnect());
        } catch {
          // ignore
        }
      }
      if (this.lfo2) {
        try {
          this.lfo2.stop();
          this.lfo2.disconnect();
          this.lfo2Gains.forEach(g => g.disconnect());
        } catch {
          // ignore
        }
      }
      if (this.swarmOsc) {
        try {
          this.swarmOsc.stop();
          this.swarmOsc.disconnect();
          this.swarmGains.forEach(g => g.disconnect());
        } catch {
          // ignore
        }
      }
      try {
        this.voiceFilter?.disconnect();
        this.voiceFilter2?.disconnect();
        this.tremoloGain?.disconnect();
        this.voicePanner?.disconnect();
        this.voiceGain.disconnect();
      } catch {
        // ignore
      }
    }, (delaySec + 0.05) * 1000);
  }
}

// Global Audio Engine Singleton instance
export const audioEngine = new Fm1AudioEngine();
