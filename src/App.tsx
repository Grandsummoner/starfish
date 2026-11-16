/**
 * @license
 * SPDX-License-Identifier: Apache-2.0
 */

import React, { useState, useEffect, useCallback } from 'react';
import { Fm1BasePanel } from './components/Fm1BasePanel';
import { CHARLES_VESTAL_FIRMWARES } from './data/charlesvestalFirmwares';
import { getDefaultVirusPatch } from './data/virusPresets';
import { SynthPatch, DrumTrack, DrumStep } from './types/firmware';
import { audioEngine } from './audio/AudioEngine';
import { generateEuclideanPattern } from './utils/musicScale';

export type DrumKitPreset = 'TECHNO' | 'HOUSE' | 'ELECTRO' | 'BREAK' | 'CLEAR';

const DRUM_META: Array<{ id: number; shortName: string; name: string }> = [
  { id: 0, shortName: 'BD', name: 'Kick' },
  { id: 1, shortName: 'SD', name: 'Snare' },
  { id: 2, shortName: 'CH', name: 'Closed Hat' },
  { id: 3, shortName: 'OH', name: 'Open Hat' },
  { id: 4, shortName: 'CP', name: 'Clap' },
  { id: 5, shortName: 'TM', name: 'FM Tom' },
  { id: 6, shortName: 'RD', name: 'Ride' },
  { id: 7, shortName: 'RS', name: 'Rimshot' }
];

function createDrumPresetTracks(preset: DrumKitPreset, seqLength: number): DrumTrack[] {
  const bar16: Record<DrumKitPreset, number[][]> = {
    TECHNO: [
      [0, 4, 8, 12],                    // BD
      [4, 12],                          // SD
      [0, 2, 3, 6, 8, 10, 11, 14, 15],  // CH
      [2, 10],                          // OH
      [4, 12],                          // CP
      [7, 14],                          // TM
      [0, 4, 8, 12],                    // RD
      [3, 11]                           // RS
    ],
    HOUSE: [
      [0, 4, 8, 12],                    // BD
      [],                               // SD
      [0, 1, 4, 5, 8, 9, 12, 13],       // CH
      [2, 6, 10, 14],                   // OH
      [4, 12],                          // CP
      [3, 11, 15],                      // TM
      [2, 6, 10, 14],                   // RD
      [7, 15]                           // RS
    ],
    ELECTRO: [
      [0, 3, 6, 10, 14],                // BD
      [4, 12],                          // SD
      [0, 2, 4, 6, 8, 10, 12, 14, 15],  // CH
      [11],                             // OH
      [4, 12, 15],                      // CP
      [1, 7, 9],                        // TM
      [0, 8],                           // RD
      [5, 13]                           // RS
    ],
    BREAK: [
      [0, 3, 6, 10],                    // BD
      [4, 11, 12, 15],                  // SD
      [0, 2, 4, 6, 8, 10, 12, 14],      // CH
      [7],                              // OH
      [4, 12],                          // CP
      [5, 9, 13],                       // TM
      [0, 6, 12],                       // RD
      [2, 14]                           // RS
    ],
    CLEAR: [[], [], [], [], [], [], [], []]
  };

  const lanes = bar16[preset];
  return DRUM_META.map((meta, trackIdx) => {
    const hits = new Set(lanes[trackIdx] || []);
    const steps: DrumStep[] = Array.from({ length: 64 }, (_, s) => {
      const m = s % 16;
      const active = hits.has(m);
      return {
        active,
        accent: active && (m === 0 || m === 4 || m === 12),
        velocity: m % 4 === 0 ? 118 : 96
      };
    });
    const activeCount = steps.slice(0, seqLength).filter((st) => st.active).length;
    return {
      id: meta.id,
      shortName: meta.shortName,
      name: meta.name,
      mute: false,
      tune: 0,
      decay: 100,
      tone: 60,
      level: 90,
      euclidPulses: activeCount,
      euclidRotate: 0,
      steps
    };
  });
}

export default function App() {
  const defaultFw = CHARLES_VESTAL_FIRMWARES.find((f) => f.id === 'fm1-virus-va') || CHARLES_VESTAL_FIRMWARES[0];
  const [currentPatch, setCurrentPatch] = useState<SynthPatch>(() => getDefaultVirusPatch());
  const [isPlayingSeq, setIsPlayingSeq] = useState<boolean>(false);
  const [bpm, setBpm] = useState<number>(124);
  const [swing, setSwing] = useState<number>(50);
  const [seqLength, setSeqLength] = useState<number>(16);
  const [drumVolume, setDrumVolume] = useState<number>(85);
  const [drumKitIndex, setDrumKitIndex] = useState<number>(0);
  const [drumBusMode, setDrumBusMode] = useState<number>(0);
  const [drumKitPreset, setDrumKitPreset] = useState<DrumKitPreset>('CLEAR');
  const [drumTracks, setDrumTracks] = useState<DrumTrack[]>(() => createDrumPresetTracks('CLEAR', 16));
  const [playheadStep, setPlayheadStep] = useState<number>(0);
  const [synthLoopCount, setSynthLoopCount] = useState<number>(0);

  useEffect(() => {
    audioEngine.setPatch(currentPatch);
  }, [currentPatch]);

  useEffect(() => {
    audioEngine.setDrumTracks(drumTracks, seqLength, bpm, swing);
  }, [drumTracks, seqLength, bpm, swing]);

  useEffect(() => {
    audioEngine.setDrumVolume(drumVolume / 100);
  }, [drumVolume]);

  useEffect(() => {
    audioEngine.setDrumKit(drumKitIndex);
  }, [drumKitIndex]);

  useEffect(() => {
    audioEngine.setDrumBusMode(drumBusMode);
  }, [drumBusMode]);

  useEffect(() => {
    audioEngine.setStepCallback((step) => {
      setPlayheadStep(step % Math.max(1, seqLength));
    });
  }, [seqLength]);

  useEffect(() => {
    audioEngine.setSynthLoopChangeCallback((count) => {
      setSynthLoopCount(count);
    });
    return () => audioEngine.setSynthLoopChangeCallback(null);
  }, []);

  const handleTogglePlaySeq = useCallback(() => {
    audioEngine.initAudio();
    if (isPlayingSeq) {
      audioEngine.stopSequencer();
      setIsPlayingSeq(false);
    } else {
      audioEngine.setDrumTracks(drumTracks, seqLength, bpm, swing);
      audioEngine.startSequencer();
      setIsPlayingSeq(true);
    }
  }, [isPlayingSeq, drumTracks, seqLength, bpm, swing]);

  const handleBpmChange = useCallback((newBpm: number) => {
    setBpm(newBpm);
  }, []);

  const handleSwingChange = useCallback((newSwing: number) => {
    setSwing(newSwing);
  }, []);

  const handleSeqLengthChange = useCallback((len: number) => {
    setSeqLength(Math.max(1, Math.min(64, Math.round(len))));
  }, []);

  const handleDrumKitChange = useCallback((kitIdx: number) => {
    const wrapped = ((kitIdx % 10) + 10) % 10;
    setDrumKitIndex(wrapped);
  }, []);

  const handleDrumBusModeChange = useCallback((modeIdx: number) => {
    const wrapped = ((modeIdx % 6) + 6) % 6;
    setDrumBusMode(wrapped);
  }, []);

  const handleDrumTrackParamChange = useCallback(
    (trackIdx: number, update: Partial<Pick<DrumTrack, 'tune' | 'decay' | 'tone' | 'level'>>) => {
      setDrumTracks((prev) =>
        prev.map((tr, idx) => (idx === trackIdx ? { ...tr, ...update } : tr))
      );
    },
    []
  );

  const handleClearSynthLoop = useCallback(() => {
    audioEngine.clearSynthLoop();
    setSynthLoopCount(0);
  }, []);

  const handleDrumStepUpdate = useCallback(
    (trackIdx: number, stepIdx: number, update: Partial<DrumStep>) => {
      const wrappedStep = ((stepIdx % 64) + 64) % 64;
      setDrumTracks((prev) =>
        prev.map((tr, tIdx) =>
          tIdx === trackIdx
            ? {
                ...tr,
                steps: tr.steps.map((st, sIdx) => (sIdx === wrappedStep ? { ...st, ...update } : st))
              }
            : tr
        )
      );
    },
    []
  );

  const handleDrumEuclidChange = useCallback(
    (trackIdx: number, pulses: number, rotate: number) => {
      const clampedPulses = Math.max(0, Math.min(seqLength, Math.round(pulses)));
      const clampedRotate = Math.max(0, Math.min(Math.max(1, seqLength) - 1, Math.round(rotate)));
      const pattern = generateEuclideanPattern(seqLength, clampedPulses, clampedRotate);
      setDrumTracks((prev) =>
        prev.map((tr, tIdx) =>
          tIdx === trackIdx
            ? {
                ...tr,
                euclidPulses: clampedPulses,
                euclidRotate: clampedRotate,
                steps: tr.steps.map((st, sIdx) =>
                  sIdx < seqLength ? { ...st, active: pattern[sIdx] } : st
                )
              }
            : tr
        )
      );
    },
    [seqLength]
  );

  const handleSelectDrumKitPreset = useCallback(
    (preset: DrumKitPreset) => {
      setDrumKitPreset(preset);
      setDrumTracks(createDrumPresetTracks(preset, seqLength));
    },
    [seqLength]
  );

  const handleToggleDrumMute = useCallback((trackIdx: number) => {
    setDrumTracks((prev) =>
      prev.map((tr, idx) => (idx === trackIdx ? { ...tr, mute: !tr.mute } : tr))
    );
  }, []);

  const handleClearDrumTrack = useCallback((trackIdx: number) => {
    setDrumTracks((prev) =>
      prev.map((tr, idx) =>
        idx === trackIdx
          ? {
              ...tr,
              mute: false,
              euclidPulses: 0,
              euclidRotate: 0,
              steps: tr.steps.map((st) => ({ ...st, active: false, accent: false }))
            }
          : tr
      )
    );
  }, []);

  return (
    <main className="min-h-screen bg-[#090B10] text-slate-100 flex flex-col items-center justify-center p-4 sm:p-6">
      <Fm1BasePanel
        patch={currentPatch}
        onPatchChange={setCurrentPatch}
        firmwareName={defaultFw.toml.name}
        isPlayingSeq={isPlayingSeq}
        onTogglePlaySeq={handleTogglePlaySeq}
        bpm={bpm}
        onBpmChange={handleBpmChange}
        swing={swing}
        onSwingChange={handleSwingChange}
        drumTracks={drumTracks}
        drumVolume={drumVolume}
        onDrumVolumeChange={setDrumVolume}
        drumKitIndex={drumKitIndex}
        onDrumKitChange={handleDrumKitChange}
        drumBusMode={drumBusMode}
        onDrumBusModeChange={handleDrumBusModeChange}
        drumKitPreset={drumKitPreset}
        onSelectDrumKitPreset={handleSelectDrumKitPreset}
        onToggleDrumMute={handleToggleDrumMute}
        onDrumTrackParamChange={handleDrumTrackParamChange}
        seqLength={seqLength}
        onSeqLengthChange={handleSeqLengthChange}
        onDrumEuclidChange={handleDrumEuclidChange}
        playheadStep={playheadStep}
        onDrumStepUpdate={handleDrumStepUpdate}
        synthLoopCount={synthLoopCount}
        onClearSynthLoop={handleClearSynthLoop}
        onClearDrumTrack={handleClearDrumTrack}
      />
    </main>
  );
}
