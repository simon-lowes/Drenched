import React from 'react';
import { Cpu, Zap, Code, ShieldCheck, Box, Sliders } from 'lucide-react';

export const EngineArchitectureView: React.FC = () => {
  return (
    <div className="rounded-lg bg-[#111317] border border-[#232730] p-4 text-xs">
      <div className="flex items-center gap-2 mb-3 pb-2 border-b border-[#1f222a]">
        <Code className="w-4 h-4 text-[#f2a93b]" />
        <h3 className="font-bold text-sm text-[#e8e6e1]">
          Drenched DSP Engine Architecture & C++ Technical Specifications
        </h3>
      </div>

      <div className="grid grid-cols-1 md:grid-cols-3 gap-3 mb-4">
        {/* Real-time Safety */}
        <div className="p-3 rounded-lg bg-[#15171d] border border-[#22252e]">
          <div className="flex items-center gap-2 text-[#f2a93b] font-semibold mb-1">
            <Zap className="w-3.5 h-3.5" />
            <span>Real-Time Safety</span>
          </div>
          <p className="text-[#848894] leading-relaxed">
            The C++ core (<code>source/engine/</code>) guarantees zero heap allocations, zero mutex locks, and no file I/O on the audio rendering thread. Audio processing runs in constant O(1) buffer space.
          </p>
        </div>

        {/* DirectConvolver Zero-Latency Head */}
        <div className="p-3 rounded-lg bg-[#15171d] border border-[#22252e]">
          <div className="flex items-center gap-2 text-[#f2a93b] font-semibold mb-1">
            <Cpu className="w-3.5 h-3.5" />
            <span>DirectConvolver</span>
          </div>
          <p className="text-[#848894] leading-relaxed">
            Time-domain direct convolution reference: circular history buffer of length N with backward index walk (<code>sum += ir[k] * history[readIndex]</code>). Serves as the ground truth reference and zero-latency head.
          </p>
        </div>

        {/* Equal-Power Crossfade */}
        <div className="p-3 rounded-lg bg-[#15171d] border border-[#22252e]">
          <div className="flex items-center gap-2 text-[#f2a93b] font-semibold mb-1">
            <Sliders className="w-3.5 h-3.5" />
            <span>Equal-Power Law</span>
          </div>
          <p className="text-[#848894] leading-relaxed">
            Preserves <code>dry² + wet² = 1.0</code> using trigonometric mapping <code>dry = cos(amount · π / 2)</code> and <code>wet = sin(amount · π / 2)</code> with 50ms smoothing to eliminate clicks or perceived level dips at 50%.
          </p>
        </div>
      </div>

      {/* DSP Pipeline Diagram */}
      <div className="p-3 rounded-lg bg-[#0e1014] border border-[#1d2027] font-mono text-[11px] overflow-x-auto text-[#9a9da3]">
        <div className="text-[#f2a93b] font-semibold mb-2">// Signal Flow Pipeline</div>
        <div className="flex items-center gap-2 whitespace-nowrap">
          <span className="px-2 py-1 rounded bg-[#181b22] text-[#e8e6e1] border border-[#262b37]">Stereo Input</span>
          <span>─┬─►</span>
          <span className="px-2 py-1 rounded bg-[#181b22] text-[#e8e6e1] border border-[#262b37]">Dry Gain: cos(Space · π / 2)</span>
          <span>───────┐</span>
        </div>
        <div className="flex items-center gap-2 whitespace-nowrap mt-1.5">
          <span className="opacity-0 px-2 py-1">Stereo Input</span>
          <span> └─►</span>
          <span className="px-2 py-1 rounded bg-[#201c15] text-[#f2a93b] border border-[#f2a93b]/30">Convolution (Direct / ConvolverNode)</span>
          <span>─►</span>
          <span className="px-2 py-1 rounded bg-[#181b22] text-[#e8e6e1] border border-[#262b37]">Wet Gain: sin(Space · π / 2)</span>
          <span>─►</span>
          <span className="px-2 py-1 rounded bg-[#181b22] text-[#e8e6e1] border border-[#262b37]">Summing Bus</span>
          <span>─►</span>
          <span className="px-2 py-1 rounded bg-[#201c15] text-[#f2a93b] border border-[#f2a93b]/30">Output Gain: 10^(dB / 20)</span>
          <span>─►</span>
          <span className="px-2 py-1 rounded bg-[#181b22] text-emerald-400 border border-emerald-800/40">Master Out</span>
        </div>
      </div>
    </div>
  );
};
