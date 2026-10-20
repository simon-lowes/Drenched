/**
 * Time-domain (direct form) convolution of one channel with one impulse response.
 *
 * This is the reference every fast engine is null-tested against: it is
 * slow (O(N) per sample) but obviously correct. It also serves as the
 * zero-latency head of the partitioned engine, where N is small.
 *
 * Direct TypeScript port from source/engine/include/drenched/DirectConvolver.h
 * and source/engine/src/DirectConvolver.cpp
 */
export class DirectConvolver {
  private ir: Float32Array = new Float32Array(0);
  private history: Float32Array = new Float32Array(0);
  private writeIndex: number = 0;

  /**
   * Replaces the impulse response and clears the history.
   */
  public setImpulseResponse(impulseResponse: ArrayLike<number>): void {
    this.ir = new Float32Array(impulseResponse);
    this.history = new Float32Array(this.ir.length);
    this.writeIndex = 0;
  }

  /**
   * Clears the input history without changing the impulse response.
   */
  public reset(): void {
    this.history.fill(0);
    this.writeIndex = 0;
  }

  /**
   * Returns the number of taps in the impulse response.
   */
  public length(): number {
    return this.ir.length;
  }

  /**
   * Convolves input into output (they may be the same buffer).
   * output[n] = sum over k of ir[k] * input[n - k].
   */
  public process(
    input: ArrayLike<number>,
    output: Float32Array | number[]
  ): void {
    const len = input.length;
    const taps = this.ir.length;

    if (taps === 0) {
      if (output instanceof Float32Array) {
        output.fill(0);
      } else {
        for (let i = 0; i < len; i++) output[i] = 0;
      }
      return;
    }

    for (let n = 0; n < len; n++) {
      const inSample = input[n];
      this.history[this.writeIndex] = inSample;

      // Walk backwards through history: tap k pairs with the input k samples ago.
      let sum = 0;
      let readIndex = this.writeIndex;

      for (let k = 0; k < taps; k++) {
        sum += this.ir[k] * this.history[readIndex];
        readIndex = (readIndex === 0 ? taps : readIndex) - 1;
      }

      output[n] = sum;
      this.writeIndex = (this.writeIndex + 1) % taps;
    }
  }
}
