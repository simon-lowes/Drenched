/**
 * Gains for an equal-power crossfade between a dry and a wet signal.
 * Direct TypeScript port from source/engine/include/drenched/Mix.h
 */
export interface MixGains {
  dry: number;
  wet: number;
}

/**
 * Returns equal-power gains for a mix amount in [0, 1], where 0 is fully dry
 * and 1 is fully wet. Out-of-range values are clamped. Total power stays
 * constant across the sweep, so the perceived level doesn't dip at 50%.
 */
export function equalPowerMix(amount: number): MixGains {
  const clamped = Math.min(Math.max(amount, 0.0), 1.0);
  const angle = clamped * Math.PI * 0.5;
  return {
    dry: Math.cos(angle),
    wet: Math.sin(angle),
  };
}

/**
 * Converts decibels to linear gain. Anything at or below -120 dB is silence.
 */
export function decibelsToGain(decibels: number): number {
  return decibels <= -120.0 ? 0.0 : Math.pow(10.0, decibels / 20.0);
}

/**
 * Converts linear gain to decibels. Returns -120 dB for zero or near-zero values.
 */
export function gainToDecibels(gain: number): number {
  if (gain <= 0.000001) return -120.0;
  return 20.0 * Math.log10(gain);
}
