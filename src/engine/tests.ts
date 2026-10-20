import { equalPowerMix, decibelsToGain } from './mix';
import { DirectConvolver } from './directConvolver';

export interface TestCaseResult {
  name: string;
  suite: 'Mix' | 'DirectConvolver';
  passed: boolean;
  assertions: number;
  message: string;
  durationMs: number;
}

// Textbook full convolution, the ground truth for engine test
function naiveConvolve(x: number[], h: number[]): number[] {
  const y = new Array(x.length).fill(0);
  for (let n = 0; n < x.length; ++n) {
    for (let k = 0; k < h.length && k <= n; ++k) {
      y[n] += h[k] * x[n - k];
    }
  }
  return y;
}

// PRNG for deterministic test vectors
function pseudoNoise(length: number, seed: number): number[] {
  let s = seed;
  const result: number[] = new Array(length);
  for (let i = 0; i < length; i++) {
    // Simple LCG
    s = (s * 1664525 + 1013904223) % 4294967296;
    result[i] = (s / 4294967296) * 2.0 - 1.0;
  }
  return result;
}

function withinAbs(actual: number, expected: number, tolerance: number): boolean {
  return Math.abs(actual - expected) <= tolerance;
}

export function runAllCatch2Tests(): {
  results: TestCaseResult[];
  totalPassed: number;
  totalFailed: number;
} {
  const results: TestCaseResult[] = [];

  // TEST 1: Equal-power mix keeps total power constant
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;
    for (let amount = 0.0; amount <= 1.0; amount += 0.05) {
      const g = equalPowerMix(amount);
      assertions++;
      const power = g.dry * g.dry + g.wet * g.wet;
      if (!withinAbs(power, 1.0, 1.0e-5)) {
        passed = false;
        break;
      }
    }
    results.push({
      name: 'Equal-power mix keeps total power constant',
      suite: 'Mix',
      passed,
      assertions,
      message: passed ? 'All 21 mix steps preserve dry² + wet² ≈ 1.0 within 1e-5 tolerance' : 'Power dipped outside tolerance',
      durationMs: performance.now() - start,
    });
  }

  // TEST 2: Equal-power mix endpoints and clamping
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    const t1 = equalPowerMix(0.0);
    assertions += 2;
    if (!withinAbs(t1.dry, 1.0, 1.0e-6) || !withinAbs(t1.wet, 0.0, 1.0e-6)) passed = false;

    const t2 = equalPowerMix(1.0);
    assertions++;
    if (!withinAbs(t2.wet, 1.0, 1.0e-6)) passed = false;

    const t3 = equalPowerMix(-3.0);
    assertions++;
    if (!withinAbs(t3.dry, 1.0, 1.0e-6)) passed = false;

    const t4 = equalPowerMix(7.0);
    assertions++;
    if (!withinAbs(t4.wet, 1.0, 1.0e-6)) passed = false;

    results.push({
      name: 'Equal-power mix endpoints and clamping',
      suite: 'Mix',
      passed,
      assertions,
      message: passed ? 'Verified mix=0 dry=1 wet=0, mix=1 wet=1, clamp(-3) dry=1, clamp(7) wet=1' : 'Endpoint clamping mismatch',
      durationMs: performance.now() - start,
    });
  }

  // TEST 3: Decibels convert to linear gain
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    assertions++;
    if (!withinAbs(decibelsToGain(0.0), 1.0, 1.0e-6)) passed = false;

    assertions++;
    if (!withinAbs(decibelsToGain(-6.0206), 0.5, 1.0e-4)) passed = false;

    assertions++;
    if (decibelsToGain(-120.0) !== 0.0) passed = false;

    results.push({
      name: 'Decibels convert to linear gain',
      suite: 'Mix',
      passed,
      assertions,
      message: passed ? '0dB -> 1.0, -6.0206dB -> 0.5, -120dB -> 0.0 (silence threshold)' : 'Gain math error',
      durationMs: performance.now() - start,
    });
  }

  // TEST 4: An impulse returns the impulse response
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    const ir = [0.5, -0.25, 0.125, 1.0];
    const conv = new DirectConvolver();
    conv.setImpulseResponse(ir);

    const input = new Float32Array([1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0]);
    const output = new Float32Array(8);
    conv.process(input, output);

    for (let i = 0; i < ir.length; ++i) {
      assertions++;
      if (!withinAbs(output[i], ir[i], 1.0e-7)) {
        passed = false;
      }
    }
    for (let i = ir.length; i < output.length; ++i) {
      assertions++;
      if (output[i] !== 0.0) {
        passed = false;
      }
    }

    results.push({
      name: 'An impulse returns the impulse response',
      suite: 'DirectConvolver',
      passed,
      assertions,
      message: passed ? 'Dirac delta [1, 0, 0...] through convolver accurately yielded IR taps' : 'Impulse response did not match',
      durationMs: performance.now() - start,
    });
  }

  // TEST 5: Block-wise processing matches one-shot convolution
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    const ir = pseudoNoise(97, 1);
    const input = pseudoNoise(1000, 2);
    const expected = naiveConvolve(input, ir);

    const conv = new DirectConvolver();
    conv.setImpulseResponse(ir);

    const output = new Float32Array(input.length);
    let pos = 0;
    const blockSizes = [1, 13, 64, 7, 256, 659];
    for (const block of blockSizes) {
      const sliceIn = input.slice(pos, pos + block);
      const sliceOut = new Float32Array(block);
      conv.process(sliceIn, sliceOut);
      output.set(sliceOut, pos);
      pos += block;
    }

    assertions++;
    if (pos !== input.length) passed = false;

    for (let i = 0; i < output.length; ++i) {
      assertions++;
      if (!withinAbs(output[i], expected[i], 1.0e-4)) {
        passed = false;
        break;
      }
    }

    results.push({
      name: 'Block-wise processing matches one-shot convolution',
      suite: 'DirectConvolver',
      passed,
      assertions,
      message: passed ? 'Arbitrary irregular block sizes (1, 13, 64, 7, 256, 659) preserved circular history' : 'Block convolution history mismatch',
      durationMs: performance.now() - start,
    });
  }

  // TEST 6: Processing in place is supported
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    const ir = pseudoNoise(16, 3);
    const buffer = new Float32Array(pseudoNoise(128, 4));
    const inputCopy = Array.from(buffer);
    const expected = naiveConvolve(inputCopy, ir);

    const conv = new DirectConvolver();
    conv.setImpulseResponse(ir);
    conv.process(buffer, buffer);

    for (let i = 0; i < buffer.length; ++i) {
      assertions++;
      if (!withinAbs(buffer[i], expected[i], 1.0e-5)) {
        passed = false;
        break;
      }
    }

    results.push({
      name: 'Processing in place is supported',
      suite: 'DirectConvolver',
      passed,
      assertions,
      message: passed ? 'In-place convolution (buffer == output) executed with zero corruption' : 'In-place buffer corrupted samples',
      durationMs: performance.now() - start,
    });
  }

  // TEST 7: Reset clears the history
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    const conv = new DirectConvolver();
    conv.setImpulseResponse([1.0, 1.0, 1.0]);

    const ones = new Float32Array([1.0, 1.0, 1.0]);
    const out = new Float32Array(3);
    conv.process(ones, out);
    conv.reset();

    const silence = new Float32Array([0.0, 0.0, 0.0]);
    conv.process(silence, out);

    for (let i = 0; i < out.length; i++) {
      assertions++;
      if (out[i] !== 0.0) {
        passed = false;
      }
    }

    results.push({
      name: 'Reset clears the history',
      suite: 'DirectConvolver',
      passed,
      assertions,
      message: passed ? 'History buffer cleanly wiped to zero upon reset()' : 'Residual memory remained after reset',
      durationMs: performance.now() - start,
    });
  }

  // TEST 8: An empty impulse response outputs silence
  {
    const start = performance.now();
    let passed = true;
    let assertions = 0;

    const conv = new DirectConvolver();
    const input = new Float32Array([1.0, 1.0, 1.0, 1.0]);
    const output = new Float32Array([9.0, 9.0, 9.0, 9.0]);
    conv.process(input, output);

    for (let i = 0; i < output.length; i++) {
      assertions++;
      if (output[i] !== 0.0) {
        passed = false;
      }
    }

    results.push({
      name: 'An empty impulse response outputs silence',
      suite: 'DirectConvolver',
      passed,
      assertions,
      message: passed ? 'Zero taps gracefully flushed output to silence' : 'Non-zero output on empty IR',
      durationMs: performance.now() - start,
    });
  }

  const totalPassed = results.filter((r) => r.passed).length;
  const totalFailed = results.length - totalPassed;

  return { results, totalPassed, totalFailed };
}
