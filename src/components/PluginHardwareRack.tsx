import React from 'react';
import { PluginRotaryKnob } from './PluginRotaryKnob';
import { equalPowerMix, decibelsToGain } from '../engine/mix';
import { EngineMode } from '../engine/audioEngine';
import { Sliders, Cpu, Activity, Info } from 'lucide-react';

interface PluginHardwareRackProps {
  mixPercent: number;
  outputDb: number;
  engineMode: EngineMode;
  onMixChange: (val: number) => void;
  onOutputChange: (val: number) => void;
  onEngineModeChange: (mode: EngineMode) => void;
}

export const PluginHardwareRack: React.FC<PluginHardwareRackProps> = ({
  mixPercent,
  outputDb,
  engineMode,
  onMixChange,
  onOutputChange,
  onEngineModeChange,
}) => {
  const gains = equalPowerMix(mixPercent / 100.0);
  const linearOut = decibelsToGain(outputDb);

  return (
    <div className="relative rounded-xl bg-[#0e0f11] border-2 border-[#20232a] shadow-2xl p-6 overflow-hidden">
      {/* 4 Corner Screws for Hardware Plugin Authenticity */}
      <div className="absolute top-3 left-3 w-3 h-3 rounded-full bg-[#1b1d24] border border-[#2e3340] flex items-center justify-center opacity-70">
        <div className="w-1.5 h-0.5 bg-[#474e60] rotate-45" />
      </div>
      <div className="absolute top-3 right-3 w-3 h-3 rounded-full bg-[#1b1d24] border border-[#2e3340] flex items-center justify-center opacity-70">
        <div className="w-1.5 h-0.5 bg-[#474e60] -rotate-12" />
      </div>
      <div className="absolute bottom-3 left-3 w-3 h-3 rounded-full bg-[#1b1d24] border border-[#2e3340] flex items-center justify-center opacity-70">
        <div className="w-1.5 h-0.5 bg-[#474e60] rotate-90" />
      </div>
      <div className="absolute bottom-3 right-3 w-3 h-3 rounded-full bg-[#1b1d24] border border-[#2e3340] flex items-center justify-center opacity-70">
        <div className="w-1.5 h-0.5 bg-[#474e60] rotate-30" />
      </div>

      {/* Top Banner: JUCE Drenched Title & Status LED */}
      <div className="flex flex-col items-center justify-center mb-6 relative">
        <div className="flex items-center gap-2.5">
          <div className="w-2.5 h-2.5 rounded-full bg-[#f2a93b] shadow-[0_0_10px_#f2a93b]" />
          <h1 className="text-2xl sm:text-3xl font-extrabold tracking-[0.25em] text-[#e8e6e1] font-sans">
            DRENCHED
          </h1>
          <div className="w-2.5 h-2.5 rounded-full bg-[#f2a93b] shadow-[0_0_10px_#f2a93b]" />
        </div>
        <span className="text-[10px] font-mono tracking-widest text-[#9a9da3] mt-1 uppercase">
          Precision Convolution & Hybrid Reverb Engine
        </span>
      </div>

      {/* Main Control Panel: Space (Mix) and Output Knobs */}
      <div className="grid grid-cols-1 sm:grid-cols-2 gap-8 my-6 py-4 px-6 rounded-lg bg-[#0a0b0d] border border-[#1b1e26]">
        {/* Space Knob (Mix) */}
        <div className="flex flex-col items-center justify-center">
          <PluginRotaryKnob
            label="Space"
            value={mixPercent}
            min={0.0}
            max={100.0}
            step={0.1}
            unit="%"
            defaultValue={30.0}
            onChange={onMixChange}
            size={124}
          />
          <div className="flex items-center gap-4 text-[10px] font-mono text-[#8a8e99] mt-3">
            <span>Dry: <strong className="text-[#e8e6e1]">{(gains.dry * 100).toFixed(1)}%</strong></span>
            <span>Wet: <strong className="text-[#f2a93b]">{(gains.wet * 100).toFixed(1)}%</strong></span>
          </div>
        </div>

        {/* Output Knob */}
        <div className="flex flex-col items-center justify-center">
          <PluginRotaryKnob
            label="Output"
            value={outputDb}
            min={-24.0}
            max={12.0}
            step={0.1}
            unit="dB"
            defaultValue={0.0}
            onChange={onOutputChange}
            size={124}
          />
          <div className="flex items-center gap-4 text-[10px] font-mono text-[#8a8e99] mt-3">
            <span>Gain: <strong className="text-[#e8e6e1]">{linearOut.toFixed(2)}x</strong></span>
            <span>Trim: <strong className={outputDb > 0 ? 'text-[#f2a93b]' : 'text-[#8a8e99]'}>{outputDb > 0 ? `+${outputDb.toFixed(1)}` : outputDb.toFixed(1)} dB</strong></span>
          </div>
        </div>
      </div>

      {/* DSP Engine Mode Switcher */}
      <div className="flex flex-wrap items-center justify-between gap-3 pt-3 border-t border-[#1b1d24]">
        <div className="flex items-center gap-2 text-xs font-mono text-[#9a9da3]">
          <Cpu className="w-3.5 h-3.5 text-[#f2a93b]" />
          <span>DSP Engine Mode:</span>
        </div>

        <div className="flex items-center gap-1.5 bg-[#14161a] p-1 rounded-lg border border-[#232730]">
          <button
            onClick={() => onEngineModeChange('web-audio-convolver')}
            className={`px-3 py-1 rounded text-xs font-mono transition-all ${
              engineMode === 'web-audio-convolver'
                ? 'bg-[#f2a93b] text-[#0e0f11] font-bold shadow-sm'
                : 'text-[#9a9da3] hover:text-[#e8e6e1]'
            }`}
          >
            Web Audio ConvolverNode
          </button>
          <button
            onClick={() => onEngineModeChange('direct-convolver-dsp')}
            className={`px-3 py-1 rounded text-xs font-mono transition-all ${
              engineMode === 'direct-convolver-dsp'
                ? 'bg-[#f2a93b] text-[#0e0f11] font-bold shadow-sm'
                : 'text-[#9a9da3] hover:text-[#e8e6e1]'
            }`}
          >
            DirectConvolver C++ Time-Domain
          </button>
        </div>
      </div>
    </div>
  );
};
