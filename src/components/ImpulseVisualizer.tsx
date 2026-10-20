import React, { useRef, useEffect } from 'react';
import { audioEngine } from '../engine/audioEngine';

interface ImpulseVisualizerProps {
  irBuffer: AudioBuffer | null;
  rt60DecayTime: number;
}

export const ImpulseVisualizer: React.FC<ImpulseVisualizerProps> = ({ irBuffer, rt60DecayTime }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const animFrameRef = useRef<number | null>(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const fftData = new Uint8Array(256);

    const render = () => {
      const width = canvas.width;
      const height = canvas.height;

      ctx.clearRect(0, 0, width, height);

      // Background grid
      ctx.strokeStyle = '#181b22';
      ctx.lineWidth = 1;
      for (let x = 0; x < width; x += 40) {
        ctx.beginPath();
        ctx.moveTo(x, 0);
        ctx.lineTo(x, height);
        ctx.stroke();
      }
      for (let y = 0; y < height; y += 25) {
        ctx.beginPath();
        ctx.moveTo(0, y);
        ctx.lineTo(width, y);
        ctx.stroke();
      }

      // Draw IR Waveform if buffer exists
      if (irBuffer) {
        const left = irBuffer.getChannelData(0);
        const step = Math.ceil(left.length / width);
        const midY = height / 2;

        // Waveform fill
        ctx.beginPath();
        ctx.moveTo(0, midY);

        for (let x = 0; x < width; x++) {
          const sampleIdx = Math.floor(x * step);
          let min = 1.0;
          let max = -1.0;

          for (let j = 0; j < step && sampleIdx + j < left.length; j++) {
            const val = left[sampleIdx + j];
            if (val < min) min = val;
            if (val > max) max = val;
          }

          if (min > max) {
            min = 0;
            max = 0;
          }

          const yTop = midY - max * (midY * 0.85);
          ctx.lineTo(x, yTop);
        }

        ctx.strokeStyle = 'rgba(242, 169, 59, 0.4)';
        ctx.lineWidth = 1.2;
        ctx.stroke();

        // Waveform bottom
        ctx.beginPath();
        ctx.moveTo(0, midY);
        for (let x = 0; x < width; x++) {
          const sampleIdx = Math.floor(x * step);
          let min = 1.0;
          let max = -1.0;

          for (let j = 0; j < step && sampleIdx + j < left.length; j++) {
            const val = left[sampleIdx + j];
            if (val < min) min = val;
            if (val > max) max = val;
          }

          if (min > max) {
            min = 0;
            max = 0;
          }

          const yBottom = midY - min * (midY * 0.85);
          ctx.lineTo(x, yBottom);
        }
        ctx.strokeStyle = 'rgba(242, 169, 59, 0.4)';
        ctx.stroke();

        // Analytical exponential decay curve
        ctx.beginPath();
        ctx.strokeStyle = 'rgba(232, 230, 225, 0.3)';
        ctx.setLineDash([3, 3]);
        for (let x = 0; x < width; x++) {
          const progress = x / width;
          const decayEnv = Math.exp(-4.5 * progress); // visual curve
          const y = midY - decayEnv * (midY * 0.85);
          if (x === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
        }
        ctx.stroke();
        ctx.setLineDash([]);
      }

      // Draw real-time output FFT spectrum overlay
      audioEngine.getOutputFrequencyData(fftData);
      let isAudioPlaying = false;
      for (let i = 0; i < 30; i++) {
        if (fftData[i] > 10) {
          isAudioPlaying = true;
          break;
        }
      }

      if (isAudioPlaying) {
        ctx.beginPath();
        const barWidth = width / fftData.length;
        ctx.moveTo(0, height);

        for (let i = 0; i < fftData.length; i++) {
          const v = fftData[i] / 255.0;
          const y = height - v * (height * 0.7);
          ctx.lineTo(i * barWidth, y);
        }

        ctx.lineTo(width, height);
        ctx.closePath();

        const grad = ctx.createLinearGradient(0, 0, 0, height);
        grad.addColorStop(0, 'rgba(242, 169, 59, 0.25)');
        grad.addColorStop(1, 'rgba(242, 169, 59, 0.02)');
        ctx.fillStyle = grad;
        ctx.fill();

        ctx.strokeStyle = '#f2a93b';
        ctx.lineWidth = 1.5;
        ctx.stroke();
      }

      animFrameRef.current = requestAnimationFrame(render);
    };

    render();

    return () => {
      if (animFrameRef.current) cancelAnimationFrame(animFrameRef.current);
    };
  }, [irBuffer]);

  return (
    <div className="relative w-full rounded-lg bg-[#0a0b0d] border border-[#20232a] p-3 overflow-hidden">
      <div className="flex items-center justify-between mb-2">
        <div className="flex items-center gap-2">
          <div className="w-2 h-2 rounded-full bg-[#f2a93b] animate-pulse" />
          <span className="text-[11px] font-mono uppercase tracking-wider text-[#9a9da3]">
            Impulse Response & Spectral Decay
          </span>
        </div>
        <div className="flex items-center gap-3 text-[11px] font-mono text-[#9a9da3]">
          <span>RT60: <strong className="text-[#f2a93b]">{rt60DecayTime.toFixed(2)}s</strong></span>
          {irBuffer && (
            <span>Taps: <strong className="text-[#e8e6e1]">{irBuffer.length.toLocaleString()}</strong></span>
          )}
          {irBuffer && (
            <span>Rate: <strong className="text-[#e8e6e1]">{irBuffer.sampleRate / 1000}kHz</strong></span>
          )}
        </div>
      </div>

      <canvas
        ref={canvasRef}
        width={720}
        height={130}
        className="w-full h-[120px] rounded block"
      />

      <div className="flex justify-between items-center text-[10px] font-mono text-[#6e727c] mt-1.5 px-1">
        <span>0 ms (Dirac Direct Arrival)</span>
        <span>Early Reflections</span>
        <span>Diffuse Reverberant Tail (RT60)</span>
      </div>
    </div>
  );
};
