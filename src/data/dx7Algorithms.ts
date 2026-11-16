/**
 * Standard 32 Yamaha DX7 / M-Vave FM-1 6-Operator FM Synthesis Algorithms
 * Each algorithm specifies:
 * - carriers: array of operator IDs that output directly to audio (1-6)
 * - modulators: array of operator IDs that modulate other operators
 * - routes: pairs [modulator, target] describing frequency modulation paths
 * - feedbackOp: operator index that feeds back into itself (default OP6)
 */

export interface AlgorithmDef {
  id: number;
  name: string;
  carriers: number[];
  routes: Array<[number, number]>; // [from, to]
  feedbackOp: number;
  description: string;
}

export const DX7_ALGORITHMS: Record<number, AlgorithmDef> = {
  1: {
    id: 1,
    name: "Alg 1: Stack + 2 Carriers",
    carriers: [1, 3],
    routes: [[6, 6], [6, 5], [5, 4], [4, 3], [2, 1]],
    feedbackOp: 6,
    description: "Classic EP & Bells. Two distinct sound branches (OP1+2 and OP3-6) allowing warm acoustic attack with bell chime."
  },
  2: {
    id: 2,
    name: "Alg 2: 2 Mod Branches -> 2 Carriers",
    carriers: [1, 3],
    routes: [[2, 2], [2, 1], [6, 5], [5, 4], [4, 3]],
    feedbackOp: 2,
    description: "Feedback on OP2 branch. Deep brass and dynamic metallic pluck synthesis."
  },
  3: {
    id: 3,
    name: "Alg 3: 3 Carriers with Sub-branches",
    carriers: [1, 2, 4],
    routes: [[6, 6], [6, 5], [5, 4], [3, 2]],
    feedbackOp: 6,
    description: "Rich polysynth pads and electric organs with multi-layer harmonic presence."
  },
  4: {
    id: 4,
    name: "Alg 4: Dual Feedback Trees -> 2 Carriers",
    carriers: [1, 4],
    routes: [[6, 6], [6, 5], [5, 4], [3, 2], [2, 1]],
    feedbackOp: 6,
    description: "Balanced dual 3-operator stacks. Symmetrical acoustic instruments & pianos."
  },
  5: {
    id: 5,
    name: "Alg 5: 3 Independent 2-Op Pairs",
    carriers: [1, 3, 5],
    routes: [[6, 6], [6, 5], [4, 3], [2, 1]],
    feedbackOp: 6,
    description: "Classic 3-part additive/FM hybrid. Lush synth leads, layered chords, and rich brass."
  },
  6: {
    id: 6,
    name: "Alg 6: 3 Pairs with Feedback on OP5",
    carriers: [1, 3, 5],
    routes: [[5, 5], [6, 5], [4, 3], [2, 1]],
    feedbackOp: 5,
    description: "High harmonic flexibility on carrier 5. Warm analog pad emulation."
  },
  7: {
    id: 7,
    name: "Alg 7: 2 Carriers, Parallel Modulators",
    carriers: [1, 3],
    routes: [[6, 6], [6, 5], [5, 4], [4, 3], [4, 1], [2, 1]],
    feedbackOp: 6,
    description: "Deep cross-modulation. Aggressive basslines, industrial clangs, and complex percussions."
  },
  8: {
    id: 8,
    name: "Alg 8: OP4 modulating Carriers 1 & 2",
    carriers: [1, 2],
    routes: [[6, 6], [6, 5], [5, 4], [4, 1], [4, 2], [3, 2]],
    feedbackOp: 6,
    description: "Stereo-like timbre detuning between dual carriers. Dynamic Rhodes and Wurlitzer EP."
  },
  16: {
    id: 16,
    name: "Alg 16: Triple Modulator into 1 Carrier",
    carriers: [1],
    routes: [[6, 6], [6, 1], [5, 1], [4, 1], [3, 2], [2, 1]],
    feedbackOp: 6,
    description: "Extreme timbral complexity into single voice. Sci-fi sweeps, growling bass, and evolving drones."
  },
  22: {
    id: 22,
    name: "Alg 22: 4 Carriers with Dual Modulators",
    carriers: [1, 2, 3, 5],
    routes: [[6, 6], [6, 5], [4, 3]],
    feedbackOp: 6,
    description: "Chamber strings, warm chorus organs, and lush vintage polysynth textures."
  },
  26: {
    id: 26,
    name: "Alg 26: 5 Carriers with Single Modulator",
    carriers: [1, 2, 3, 4, 5],
    routes: [[6, 6], [6, 5]],
    feedbackOp: 6,
    description: "Additive harmonics with FM brightness top-end. Bright pipe organs and bell towers."
  },
  32: {
    id: 32,
    name: "Alg 32: All 6 Carriers (Additive / Organ)",
    carriers: [1, 2, 3, 4, 5, 6],
    routes: [[6, 6]],
    feedbackOp: 6,
    description: "Pure 6-voice additive harmonic synthesis. Hammond B3 drawbar emulation, clean sines, and microtonal clusters."
  }
};

export function getAlgorithm(id: number): AlgorithmDef {
  return DX7_ALGORITHMS[id] || DX7_ALGORITHMS[1];
}
