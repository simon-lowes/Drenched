import { equalPowerMix, decibelsToGain } from './mix';
import { DirectConvolver } from './directConvolver';
import { ImpulseResponsePreset, IR_PRESETS, generateSyntheticIR } from './irLibrary';

export type EngineMode = 'web-audio-convolver' | 'direct-convolver-dsp';
export type SoundSourceType = 'drum-loop' | 'synth-melody' | 'guitar-chords' | 'impulse-click' | 'pink-noise' | 'mic' | 'custom-audio';

export interface AudioEngineState {
  isPlaying: boolean;
  isMicActive: boolean;
  mixPercent: number; // 0 to 100 (ParamID: "mix", named "Space")
  outputDb: number; // -24 to +12 (ParamID: "output", named "Output")
  currentPreset: ImpulseResponsePreset | null;
  customIrName: string | null;
  currentSource: SoundSourceType;
  engineMode: EngineMode;
  sampleRate: number;
  cpuLoadEstimate: number;
}

export class DrenchedAudioEngine {
  private ctx: AudioContext | null = null;
  private isInitialized = false;

  // Nodes
  private sourceNode: AudioNode | null = null;
  private micStream: MediaStream | null = null;
  private micSourceNode: MediaStreamAudioSourceNode | null = null;
  private dryGainNode: GainNode | null = null;
  private wetGainNode: GainNode | null = null;
  private outputGainNode: GainNode | null = null;
  private convolverNode: ConvolverNode | null = null;
  private scriptProcessorNode: ScriptProcessorNode | null = null;
  private directConvolverL: DirectConvolver = new DirectConvolver();
  private directConvolverR: DirectConvolver = new DirectConvolver();

  // Analysers
  private inputAnalyser: AnalyserNode | null = null;
  private wetAnalyser: AnalyserNode | null = null;
  private outputAnalyser: AnalyserNode | null = null;

  // Sound generator timers
  private synthInterval: number | null = null;
  private customAudioBuffer: AudioBuffer | null = null;
  private currentIrBuffer: AudioBuffer | null = null;

  // Parameter values
  private mixPercent = 30.0; // Default from PluginProcessor: 30%
  private outputDb = 0.0; // Default from PluginProcessor: 0.0 dB
  private engineMode: EngineMode = 'web-audio-convolver';
  private currentPreset: ImpulseResponsePreset = IR_PRESETS[0];
  private customIrName: string | null = null;
  private currentSource: SoundSourceType = 'synth-melody';
  private isPlaying = false;
  private onStateChange: ((state: AudioEngineState) => void) | null = null;

  constructor() {}

  public subscribe(cb: (state: AudioEngineState) => void): () => void {
    this.onStateChange = cb;
    this.notifyState();
    return () => {
      this.onStateChange = null;
    };
  }

  private notifyState() {
    if (this.onStateChange) {
      this.onStateChange(this.getState());
    }
  }

  public getState(): AudioEngineState {
    return {
      isPlaying: this.isPlaying,
      isMicActive: !!this.micStream,
      mixPercent: this.mixPercent,
      outputDb: this.outputDb,
      currentPreset: this.currentPreset,
      customIrName: this.customIrName,
      currentSource: this.currentSource,
      engineMode: this.engineMode,
      sampleRate: this.ctx ? this.ctx.sampleRate : 48000,
      cpuLoadEstimate: this.engineMode === 'direct-convolver-dsp' ? 14.5 : 2.1,
    };
  }

  public async initAudio(): Promise<void> {
    if (this.isInitialized && this.ctx) {
      if (this.ctx.state === 'suspended') {
        await this.ctx.resume();
      }
      return;
    }

    const AudioContextClass = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
    this.ctx = new AudioContextClass();
    if (this.ctx.state === 'suspended') {
      await this.ctx.resume();
    }

    // Build the DSP Graph
    this.dryGainNode = this.ctx.createGain();
    this.wetGainNode = this.ctx.createGain();
    this.outputGainNode = this.ctx.createGain();

    this.convolverNode = this.ctx.createConvolver();
    this.convolverNode.normalize = false;

    this.inputAnalyser = this.ctx.createAnalyser();
    this.inputAnalyser.fftSize = 512;
    this.wetAnalyser = this.ctx.createAnalyser();
    this.wetAnalyser.fftSize = 512;
    this.outputAnalyser = this.ctx.createAnalyser();
    this.outputAnalyser.fftSize = 1024;

    // Load initial preset
    await this.loadPreset(this.currentPreset);

    // Apply gains
    this.applyGains();

    this.isInitialized = true;
    this.notifyState();
  }

  public setMix(percent: number): void {
    this.mixPercent = Math.min(Math.max(percent, 0.0), 100.0);
    this.applyGains();
    this.notifyState();
  }

  public setOutput(db: number): void {
    this.outputDb = Math.min(Math.max(db, -24.0), 12.0);
    this.applyGains();
    this.notifyState();
  }

  private applyGains(): void {
    if (!this.ctx) return;
    const now = this.ctx.currentTime;
    const rampTime = 0.05; // 50ms smoothing, matching PluginProcessor.cpp smoothingSeconds = 0.05

    // Equal-power crossfade between dry and wet
    const gains = equalPowerMix(this.mixPercent / 100.0);
    const outLinear = decibelsToGain(this.outputDb);

    if (this.dryGainNode) {
      this.dryGainNode.gain.cancelScheduledValues(now);
      this.dryGainNode.gain.setTargetAtTime(gains.dry, now, rampTime);
    }

    if (this.wetGainNode) {
      this.wetGainNode.gain.cancelScheduledValues(now);
      this.wetGainNode.gain.setTargetAtTime(gains.wet, now, rampTime);
    }

    if (this.outputGainNode) {
      this.outputGainNode.gain.cancelScheduledValues(now);
      this.outputGainNode.gain.setTargetAtTime(outLinear, now, rampTime);
    }
  }

  public async setEngineMode(mode: EngineMode): Promise<void> {
    this.engineMode = mode;
    if (this.isPlaying) {
      // Re-route processing path
      this.connectProcessingChain();
    }
    this.notifyState();
  }

  public async loadPreset(preset: ImpulseResponsePreset): Promise<void> {
    await this.initAudio();
    if (!this.ctx) return;

    this.currentPreset = preset;
    this.customIrName = null;

    const irBuffer = generateSyntheticIR(this.ctx, preset);
    this.currentIrBuffer = irBuffer;
    this.applyImpulseResponseBuffer(irBuffer);
    this.notifyState();
  }

  public async loadCustomIR(file: File): Promise<void> {
    await this.initAudio();
    if (!this.ctx) return;

    try {
      const arrayBuffer = await file.arrayBuffer();
      const decodedBuffer = await this.ctx.decodeAudioData(arrayBuffer);
      this.currentIrBuffer = decodedBuffer;
      this.customIrName = file.name;
      this.applyImpulseResponseBuffer(decodedBuffer);
      this.notifyState();
    } catch (e) {
      console.error('Failed to decode IR audio file', e);
      throw e;
    }
  }

  public getCurrentIRBuffer(): AudioBuffer | null {
    return this.currentIrBuffer;
  }

  private applyImpulseResponseBuffer(buffer: AudioBuffer): void {
    if (!this.convolverNode) return;
    this.convolverNode.buffer = buffer;

    // Also prime the DirectConvolver time-domain engine for direct C++ DSP mode
    // (Truncated to first ~2048 taps for responsive real-time JS direct-form execution)
    const taps = Math.min(buffer.length, 2048);
    const leftChannel = buffer.getChannelData(0).subarray(0, taps);
    const rightChannel = buffer.numberOfChannels > 1 ? buffer.getChannelData(1).subarray(0, taps) : leftChannel;

    this.directConvolverL.setImpulseResponse(leftChannel);
    this.directConvolverR.setImpulseResponse(rightChannel);
  }

  public async setSource(source: SoundSourceType): Promise<void> {
    this.currentSource = source;
    if (this.isPlaying) {
      this.stopPlayback();
      await this.startPlayback();
    }
    this.notifyState();
  }

  public async loadCustomAudioSource(file: File): Promise<void> {
    await this.initAudio();
    if (!this.ctx) return;
    const arrayBuffer = await file.arrayBuffer();
    this.customAudioBuffer = await this.ctx.decodeAudioData(arrayBuffer);
    this.currentSource = 'custom-audio';
    if (this.isPlaying) {
      this.stopPlayback();
      await this.startPlayback();
    }
    this.notifyState();
  }

  public async togglePlay(): Promise<void> {
    await this.initAudio();
    if (this.isPlaying) {
      this.stopPlayback();
    } else {
      await this.startPlayback();
    }
  }

  private connectProcessingChain(): void {
    if (!this.ctx || !this.dryGainNode || !this.wetGainNode || !this.outputGainNode || !this.outputAnalyser || !this.inputAnalyser || !this.wetAnalyser) return;

    // Disconnect old wiring
    try {
      if (this.convolverNode) this.convolverNode.disconnect();
      if (this.scriptProcessorNode) this.scriptProcessorNode.disconnect();
      this.dryGainNode.disconnect();
      this.wetGainNode.disconnect();
    } catch {}

    // Input branch to Dry Gain
    this.dryGainNode.connect(this.outputGainNode);

    if (this.engineMode === 'web-audio-convolver') {
      // Wet path via Web Audio ConvolverNode
      if (this.convolverNode) {
        this.convolverNode.connect(this.wetGainNode);
      }
    } else {
      // Wet path via DirectConvolver DSP processor
      const bufferSize = 512;
      this.scriptProcessorNode = this.ctx.createScriptProcessor(bufferSize, 2, 2);
      this.scriptProcessorNode.onaudioprocess = (e) => {
        const inL = e.inputBuffer.getChannelData(0);
        const inR = e.inputBuffer.numberOfChannels > 1 ? e.inputBuffer.getChannelData(1) : inL;
        const outL = e.outputBuffer.getChannelData(0);
        const outR = e.outputBuffer.getChannelData(1);

        // Process directly using the exact C++ ported DirectConvolver engine
        this.directConvolverL.process(inL, outL);
        this.directConvolverR.process(inR, outR);
      };
      this.scriptProcessorNode.connect(this.wetGainNode);
    }

    this.wetGainNode.connect(this.wetAnalyser);
    this.wetGainNode.connect(this.outputGainNode);

    // Master Output
    this.outputGainNode.connect(this.outputAnalyser);
    this.outputAnalyser.connect(this.ctx.destination);
  }

  private async startPlayback(): Promise<void> {
    await this.initAudio();
    if (!this.ctx) return;

    this.connectProcessingChain();

    if (this.currentSource === 'mic') {
      try {
        this.micStream = await navigator.mediaDevices.getUserMedia({ audio: true });
        this.micSourceNode = this.ctx.createMediaStreamSource(this.micStream);
        this.routeAudioSource(this.micSourceNode);
        this.isPlaying = true;
        this.notifyState();
      } catch (err) {
        console.warn('Microphone permission denied', err);
        this.currentSource = 'synth-melody';
        this.startInternalSynthPlayback();
      }
      return;
    }

    if (this.currentSource === 'custom-audio' && this.customAudioBuffer) {
      this.playBufferLoop(this.customAudioBuffer);
      return;
    }

    if (this.currentSource === 'impulse-click') {
      this.playPeriodicImpulses();
      return;
    }

    if (this.currentSource === 'pink-noise') {
      this.playNoiseSource();
      return;
    }

    if (this.currentSource === 'drum-loop') {
      this.playProceduralDrums();
      return;
    }

    if (this.currentSource === 'guitar-chords') {
      this.playProceduralChords();
      return;
    }

    // Default: synth-melody
    this.startInternalSynthPlayback();
  }

  public stopPlayback(): void {
    if (this.synthInterval) {
      clearInterval(this.synthInterval);
      this.synthInterval = null;
    }

    if (this.sourceNode) {
      try {
        (this.sourceNode as AudioBufferSourceNode).stop();
      } catch {}
      this.sourceNode.disconnect();
      this.sourceNode = null;
    }

    if (this.micStream) {
      this.micStream.getTracks().forEach((t) => t.stop());
      this.micStream = null;
    }

    if (this.micSourceNode) {
      this.micSourceNode.disconnect();
      this.micSourceNode = null;
    }

    this.isPlaying = false;
    this.notifyState();
  }

  private routeAudioSource(node: AudioNode): void {
    if (!this.inputAnalyser || !this.dryGainNode) return;
    node.connect(this.inputAnalyser);
    node.connect(this.dryGainNode);

    if (this.engineMode === 'web-audio-convolver' && this.convolverNode) {
      node.connect(this.convolverNode);
    } else if (this.engineMode === 'direct-convolver-dsp' && this.scriptProcessorNode) {
      node.connect(this.scriptProcessorNode);
    }
  }

  // --- Procedural Musical Sound Generators for Instant Dry Testing ---

  private startInternalSynthPlayback(): void {
    if (!this.ctx) return;
    this.isPlaying = true;
    this.notifyState();

    const notes = [220, 261.63, 329.63, 392, 440, 523.25, 659.25, 523.25]; // A minor arpeggio
    let step = 0;

    const playNote = () => {
      if (!this.ctx || !this.isPlaying) return;
      const freq = notes[step % notes.length];
      step++;

      const osc = this.ctx.createOscillator();
      const env = this.ctx.createGain();

      osc.type = 'triangle';
      osc.frequency.setValueAtTime(freq, this.ctx.currentTime);

      env.gain.setValueAtTime(0, this.ctx.currentTime);
      env.gain.linearRampToValueAtTime(0.25, this.ctx.currentTime + 0.02);
      env.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.35);

      osc.connect(env);
      this.routeAudioSource(env);

      osc.start();
      osc.stop(this.ctx.currentTime + 0.36);
    };

    playNote();
    this.synthInterval = window.setInterval(playNote, 240);
  }

  private playProceduralDrums(): void {
    if (!this.ctx) return;
    this.isPlaying = true;
    this.notifyState();

    let step = 0;
    const playBeat = () => {
      if (!this.ctx || !this.isPlaying) return;
      const beat = step % 8;
      step++;

      const t = this.ctx.currentTime;

      // Kick on 0 and 4
      if (beat === 0 || beat === 4) {
        const osc = this.ctx.createOscillator();
        const env = this.ctx.createGain();
        osc.frequency.setValueAtTime(140, t);
        osc.frequency.exponentialRampToValueAtTime(38, t + 0.12);
        env.gain.setValueAtTime(0.6, t);
        env.gain.exponentialRampToValueAtTime(0.001, t + 0.25);
        osc.connect(env);
        this.routeAudioSource(env);
        osc.start(t);
        osc.stop(t + 0.25);
      }

      // Snare on 2 and 6
      if (beat === 2 || beat === 6) {
        const bufferSize = this.ctx.sampleRate * 0.15;
        const noiseBuffer = this.ctx.createBuffer(1, bufferSize, this.ctx.sampleRate);
        const output = noiseBuffer.getChannelData(0);
        for (let i = 0; i < bufferSize; i++) output[i] = Math.random() * 2 - 1;

        const whiteNoise = this.ctx.createBufferSource();
        whiteNoise.buffer = noiseBuffer;
        const filter = this.ctx.createBiquadFilter();
        filter.type = 'highpass';
        filter.frequency.value = 1000;

        const env = this.ctx.createGain();
        env.gain.setValueAtTime(0.4, t);
        env.gain.exponentialRampToValueAtTime(0.001, t + 0.15);

        whiteNoise.connect(filter);
        filter.connect(env);
        this.routeAudioSource(env);
        whiteNoise.start(t);
        whiteNoise.stop(t + 0.15);
      }

      // Hi-hat on every 8th
      {
        const osc = this.ctx.createOscillator();
        const env = this.ctx.createGain();
        osc.type = 'square';
        osc.frequency.value = 7500;
        env.gain.setValueAtTime(beat % 2 === 0 ? 0.08 : 0.04, t);
        env.gain.exponentialRampToValueAtTime(0.0001, t + 0.05);
        osc.connect(env);
        this.routeAudioSource(env);
        osc.start(t);
        osc.stop(t + 0.05);
      }
    };

    playBeat();
    this.synthInterval = window.setInterval(playBeat, 160);
  }

  private playProceduralChords(): void {
    if (!this.ctx) return;
    this.isPlaying = true;
    this.notifyState();

    const chords = [
      [220, 261.63, 329.63], // Am
      [174.61, 220, 261.63], // F
      [130.81, 164.81, 196.0], // C
      [196.0, 246.94, 293.66], // G
    ];
    let chordIdx = 0;

    const playChord = () => {
      if (!this.ctx || !this.isPlaying) return;
      const frequencies = chords[chordIdx % chords.length];
      chordIdx++;
      const t = this.ctx.currentTime;

      frequencies.forEach((freq, idx) => {
        if (!this.ctx) return;
        const osc = this.ctx.createOscillator();
        const env = this.ctx.createGain();
        osc.type = 'sawtooth';
        osc.frequency.setValueAtTime(freq, t);

        const filter = this.ctx.createBiquadFilter();
        filter.type = 'lowpass';
        filter.frequency.setValueAtTime(2400, t);
        filter.frequency.exponentialRampToValueAtTime(600, t + 1.2);

        env.gain.setValueAtTime(0, t);
        env.gain.linearRampToValueAtTime(0.12, t + 0.04 + idx * 0.03);
        env.gain.exponentialRampToValueAtTime(0.001, t + 1.4);

        osc.connect(filter);
        filter.connect(env);
        this.routeAudioSource(env);

        osc.start(t + idx * 0.03);
        osc.stop(t + 1.5);
      });
    };

    playChord();
    this.synthInterval = window.setInterval(playChord, 1600);
  }

  private playPeriodicImpulses(): void {
    if (!this.ctx) return;
    this.isPlaying = true;
    this.notifyState();

    const playClick = () => {
      if (!this.ctx || !this.isPlaying) return;
      // Single impulse / click to hear raw acoustic room response (Dirac Delta)
      const buffer = this.ctx.createBuffer(1, 12, this.ctx.sampleRate);
      const data = buffer.getChannelData(0);
      data[0] = 1.0;
      data[1] = -0.5;

      const source = this.ctx.createBufferSource();
      source.buffer = buffer;
      const gain = this.ctx.createGain();
      gain.gain.value = 0.8;
      source.connect(gain);
      this.routeAudioSource(gain);
      source.start();
    };

    playClick();
    this.synthInterval = window.setInterval(playClick, 1400);
  }

  private playNoiseSource(): void {
    if (!this.ctx) return;
    this.isPlaying = true;
    this.notifyState();

    // 1-second burst of pinkish noise
    const playBurst = () => {
      if (!this.ctx || !this.isPlaying) return;
      const len = Math.floor(this.ctx.sampleRate * 0.12);
      const buffer = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
      const data = buffer.getChannelData(0);
      let b0 = 0, b1 = 0, b2 = 0;
      for (let i = 0; i < len; i++) {
        const white = Math.random() * 2 - 1;
        b0 = 0.99886 * b0 + white * 0.0555179;
        b1 = 0.99332 * b1 + white * 0.0750759;
        b2 = 0.96900 * b2 + white * 0.1538520;
        data[i] = (b0 + b1 + b2 + white * 0.5362) * 0.15;
      }

      const source = this.ctx.createBufferSource();
      source.buffer = buffer;
      this.routeAudioSource(source);
      source.start();
    };

    playBurst();
    this.synthInterval = window.setInterval(playBurst, 1200);
  }

  private playBufferLoop(buffer: AudioBuffer): void {
    if (!this.ctx) return;
    this.isPlaying = true;
    this.notifyState();

    const source = this.ctx.createBufferSource();
    source.buffer = buffer;
    source.loop = true;
    this.sourceNode = source;
    this.routeAudioSource(source);
    source.start();
  }

  // --- Analyser Accessors for Visualizers ---

  public getInputFrequencyData(array: Uint8Array): void {
    if (this.inputAnalyser) {
      this.inputAnalyser.getByteFrequencyData(array as any);
    }
  }

  public getOutputFrequencyData(array: Uint8Array): void {
    if (this.outputAnalyser) {
      this.outputAnalyser.getByteFrequencyData(array as any);
    }
  }

  public getOutputTimeData(array: Uint8Array): void {
    if (this.outputAnalyser) {
      this.outputAnalyser.getByteTimeDomainData(array as any);
    }
  }
}

export const audioEngine = new DrenchedAudioEngine();
