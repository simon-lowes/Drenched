import React, { useRef } from 'react';
import { audioEngine, SoundSourceType } from '../engine/audioEngine';
import { Play, Square, Mic, Music, Drum, Sparkles, Volume2, Upload, Activity } from 'lucide-react';

interface AudioSourceRackProps {
  isPlaying: boolean;
  isMicActive: boolean;
  currentSource: SoundSourceType;
  onTogglePlay: () => void;
  onSourceChange: (source: SoundSourceType) => void;
  onCustomAudioUpload: (file: File) => void;
}

export const AudioSourceRack: React.FC<AudioSourceRackProps> = ({
  isPlaying,
  isMicActive,
  currentSource,
  onTogglePlay,
  onSourceChange,
  onCustomAudioUpload,
}) => {
  const fileInputRef = useRef<HTMLInputElement>(null);

  const sources: { id: SoundSourceType; label: string; icon: React.ReactNode; desc: string }[] = [
    { id: 'synth-melody', label: 'Synth Arp', icon: <Sparkles className="w-3.5 h-3.5" />, desc: 'A-minor triangle sequence' },
    { id: 'drum-loop', label: 'Drum Groove', icon: <Drum className="w-3.5 h-3.5" />, desc: 'Dynamic kick, snare, hi-hat' },
    { id: 'guitar-chords', label: 'Pluck Chords', icon: <Music className="w-3.5 h-3.5" />, desc: 'Harmonic progression' },
    { id: 'impulse-click', label: 'Dirac Impulse', icon: <Activity className="w-3.5 h-3.5" />, desc: 'Instantaneous acoustic click' },
    { id: 'pink-noise', label: 'Noise Burst', icon: <Volume2 className="w-3.5 h-3.5" />, desc: 'Full-spectrum pink noise' },
    { id: 'mic', label: 'Microphone', icon: <Mic className="w-3.5 h-3.5" />, desc: 'Live stereo mic input' },
  ];

  return (
    <div className="rounded-lg bg-[#111317] border border-[#232730] p-3.5">
      <div className="flex flex-wrap items-center justify-between gap-3 mb-3">
        <div className="flex items-center gap-2.5">
          <button
            onClick={onTogglePlay}
            className={`flex items-center gap-2 px-4 py-2 rounded-md font-semibold text-xs tracking-wide transition-all shadow-md ${
              isPlaying
                ? 'bg-rose-500/20 text-rose-300 border border-rose-500/40 hover:bg-rose-500/30'
                : 'bg-[#f2a93b] text-[#0e0f11] hover:bg-[#ffb648] hover:shadow-[0_0_12px_rgba(242,169,59,0.3)]'
            }`}
          >
            {isPlaying ? (
              <>
                <Square className="w-3.5 h-3.5 fill-current" />
                <span>STOP</span>
              </>
            ) : (
              <>
                <Play className="w-3.5 h-3.5 fill-current" />
                <span>PLAY DRY SOURCE</span>
              </>
            )}
          </button>

          <span className="text-xs text-[#9a9da3]">
            {isPlaying ? (
              <span className="inline-flex items-center gap-1.5 text-emerald-400 font-mono text-[11px]">
                <span className="w-1.5 h-1.5 rounded-full bg-emerald-400 animate-ping" />
                DSP Active • Processing Block
              </span>
            ) : (
              <span className="font-mono text-[11px] text-[#6e727c]">Audio Idle • Press Play to Audition</span>
            )}
          </span>
        </div>

        {/* Custom Audio Upload */}
        <div>
          <input
            ref={fileInputRef}
            type="file"
            accept="audio/*"
            className="hidden"
            onChange={(e) => {
              const file = e.target.files?.[0];
              if (file) {
                onCustomAudioUpload(file);
              }
            }}
          />
          <button
            onClick={() => fileInputRef.current?.click()}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-[#171a21] border border-[#2a2f3b] text-xs font-mono text-[#c5c8d0] hover:text-[#f2a93b] hover:border-[#f2a93b]/40 transition-colors"
          >
            <Upload className="w-3 h-3" />
            <span>Upload Audio Track</span>
          </button>
        </div>
      </div>

      {/* Audio Sources buttons */}
      <div className="grid grid-cols-2 sm:grid-cols-3 md:grid-cols-6 gap-2">
        {sources.map((src) => {
          const isSelected = currentSource === src.id;
          return (
            <button
              key={src.id}
              onClick={() => onSourceChange(src.id)}
              className={`flex flex-col items-start p-2 rounded-md border text-left transition-all ${
                isSelected
                  ? 'bg-[#1b1f28] border-[#f2a93b] text-[#f2a93b] shadow-[0_0_10px_rgba(242,169,59,0.1)]'
                  : 'bg-[#14161a] border-[#22252e] text-[#9a9da3] hover:text-[#e8e6e1] hover:border-[#2f3442]'
              }`}
            >
              <div className="flex items-center gap-1.5 mb-1 font-semibold text-xs">
                {src.icon}
                <span>{src.label}</span>
              </div>
              <span className="text-[10px] text-[#6e727c] line-clamp-1">{src.desc}</span>
            </button>
          );
        })}
      </div>
    </div>
  );
};
