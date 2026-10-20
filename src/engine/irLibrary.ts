/**
 * Impulse Response library and acoustic room synthesis models for Drenched.
 */

export interface ImpulseResponsePreset {
  id: string;
  name: string;
  category: 'Halls' | 'Stages' | 'Plates' | 'Rooms' | 'Exotic';
  description: string;
  decayTime: number; // in seconds (RT60)
  preDelayMs: number;
  damping: number; // 0 (bright) to 1 (warm / dark)
  diffusion: number; // 0 to 1
  stereoSpread: number; // 0 to 1
  icon: string;
}

export const IR_PRESETS: ImpulseResponsePreset[] = [
  {
    id: 'st-pauls-cathedral',
    name: 'St. Paul’s Cathedral Dome',
    category: 'Halls',
    description: 'Vast limestone cathedral with towering acoustic reflections and massive 4.8s decay tail.',
    decayTime: 4.8,
    preDelayMs: 38,
    damping: 0.35,
    diffusion: 0.95,
    stereoSpread: 0.9,
    icon: 'Church',
  },
  {
    id: 'abbey-scoring-stage',
    name: 'Abbey Scoring Stage One',
    category: 'Stages',
    description: 'Renowned symphonic film scoring stage with rich wooden warm reflections and balanced clarity.',
    decayTime: 2.2,
    preDelayMs: 24,
    damping: 0.45,
    diffusion: 0.88,
    stereoSpread: 0.85,
    icon: 'Music',
  },
  {
    id: 'vintage-emt-plate',
    name: '1976 EMT 140 Gold Plate',
    category: 'Plates',
    description: 'Iconic mechanical plate reverb: instantaneous dense attack, metallic sizzle, and creamy highs.',
    decayTime: 2.9,
    preDelayMs: 4,
    damping: 0.15,
    diffusion: 0.98,
    stereoSpread: 0.75,
    icon: 'Disc',
  },
  {
    id: 'tiled-stone-chamber',
    name: 'Tiled Echo Chamber',
    category: 'Rooms',
    description: 'Reflective ceramic and masonry room with bright flutter echoes and punchy presence.',
    decayTime: 1.4,
    preDelayMs: 12,
    damping: 0.2,
    diffusion: 0.7,
    stereoSpread: 0.65,
    icon: 'Box',
  },
  {
    id: 'vocal-booth-dead',
    name: 'Dry Acoustic Vocal Booth',
    category: 'Rooms',
    description: 'Treated fiberglass studio booth providing tight, early absorption and controlled intimacy.',
    decayTime: 0.35,
    preDelayMs: 2,
    damping: 0.8,
    diffusion: 0.4,
    stereoSpread: 0.4,
    icon: 'Mic',
  },
  {
    id: 'cavern-stalactite',
    name: 'Subterranean Crystal Cave',
    category: 'Exotic',
    description: 'Sprawling natural limestone cavern with resonant low-frequency slaps and sprawling stereo width.',
    decayTime: 6.2,
    preDelayMs: 55,
    damping: 0.55,
    diffusion: 0.8,
    stereoSpread: 1.0,
    icon: 'Mountain',
  },
];

/**
 * Synthesizes an authentic stereo impulse response AudioBuffer
 * using early specular reflections, diffuse late field, frequency-dependent damping,
 * and stereo decorrelation.
 */
export function generateSyntheticIR(
  ctx: AudioContext | OfflineAudioContext,
  preset: ImpulseResponsePreset
): AudioBuffer {
  const sampleRate = ctx.sampleRate;
  const length = Math.max(128, Math.floor(sampleRate * preset.decayTime));
  const buffer = ctx.createBuffer(2, length, sampleRate);
  const left = buffer.getChannelData(0);
  const right = buffer.getChannelData(1);

  const preDelaySamples = Math.floor((preset.preDelayMs / 1000) * sampleRate);
  const decayConstant = 6.91 / (preset.decayTime * sampleRate); // e^(-6.91) ≈ -60 dB (RT60)

  // 1. Early Reflections (Specular taps)
  const earlyTapCount = Math.floor(18 * preset.diffusion);
  for (let i = 0; i < earlyTapCount; i++) {
    const tapDelayL = preDelaySamples + Math.floor(Math.random() * (sampleRate * 0.08));
    const tapDelayR = preDelaySamples + Math.floor(Math.random() * (sampleRate * 0.08));
    const ampL = (Math.random() * 0.8 + 0.2) * (Math.random() > 0.5 ? 1 : -1);
    const ampR = (Math.random() * 0.8 + 0.2) * (Math.random() > 0.5 ? 1 : -1);

    if (tapDelayL < length) left[tapDelayL] += ampL * 0.6;
    if (tapDelayR < length) right[tapDelayR] += ampR * 0.6;
  }

  // 2. Diffuse Late Reverberation with High-Frequency Damping
  let prevSampleL = 0;
  let prevSampleR = 0;
  const dampFactor = Math.min(Math.max(preset.damping, 0.05), 0.95);

  for (let n = preDelaySamples; n < length; n++) {
    const t = n - preDelaySamples;
    const env = Math.exp(-decayConstant * t);

    // Stereo noise with decorrelation
    const commonNoise = (Math.random() * 2 - 1) * (1 - preset.stereoSpread * 0.5);
    const diffNoiseL = (Math.random() * 2 - 1) * preset.stereoSpread;
    const diffNoiseR = (Math.random() * 2 - 1) * preset.stereoSpread;

    const rawL = (commonNoise + diffNoiseL) * env;
    const rawR = (commonNoise + diffNoiseR) * env;

    // Simple one-pole low-pass filter to model frequency absorption over time
    const filteredL = prevSampleL + (rawL - prevSampleL) * (1 - dampFactor * (t / length));
    const filteredR = prevSampleR + (rawR - prevSampleR) * (1 - dampFactor * (t / length));

    prevSampleL = filteredL;
    prevSampleR = filteredR;

    left[n] += filteredL * 0.5;
    right[n] += filteredR * 0.5;
  }

  // Normalize audio buffer to avoid clipping and ensure optimal gain staging
  let maxPeak = 0.0001;
  for (let i = 0; i < length; i++) {
    const absL = Math.abs(left[i]);
    const absR = Math.abs(right[i]);
    if (absL > maxPeak) maxPeak = absL;
    if (absR > maxPeak) maxPeak = absR;
  }

  const normScale = 0.85 / maxPeak;
  for (let i = 0; i < length; i++) {
    left[i] *= normScale;
    right[i] *= normScale;
  }

  return buffer;
}
