import React, { useRef, useState } from 'react';
import { IR_PRESETS, ImpulseResponsePreset } from '../engine/irLibrary';
import { Church, Music, Disc, Box, Mic, Mountain, FolderUp, Check, Layers } from 'lucide-react';

interface PresetBrowserProps {
  currentPreset: ImpulseResponsePreset | null;
  customIrName: string | null;
  onSelectPreset: (preset: ImpulseResponsePreset) => void;
  onUploadCustomIR: (file: File) => void;
}

export const PresetBrowser: React.FC<PresetBrowserProps> = ({
  currentPreset,
  customIrName,
  onSelectPreset,
  onUploadCustomIR,
}) => {
  const [activeCategory, setActiveCategory] = useState<string>('All');
  const [isDraggingOver, setIsDraggingOver] = useState(false);
  const fileInputRef = useRef<HTMLInputElement>(null);

  const categories = ['All', 'Halls', 'Stages', 'Plates', 'Rooms', 'Exotic'];

  const filteredPresets = activeCategory === 'All'
    ? IR_PRESETS
    : IR_PRESETS.filter((p) => p.category === activeCategory);

  const getIcon = (name: string) => {
    switch (name) {
      case 'Church': return <Church className="w-4 h-4" />;
      case 'Music': return <Music className="w-4 h-4" />;
      case 'Disc': return <Disc className="w-4 h-4" />;
      case 'Box': return <Box className="w-4 h-4" />;
      case 'Mic': return <Mic className="w-4 h-4" />;
      case 'Mountain': return <Mountain className="w-4 h-4" />;
      default: return <Layers className="w-4 h-4" />;
    }
  };

  const handleDrop = (e: React.DragEvent) => {
    e.preventDefault();
    setIsDraggingOver(false);
    const file = e.dataTransfer.files?.[0];
    if (file) {
      onUploadCustomIR(file);
    }
  };

  return (
    <div className="rounded-lg bg-[#111317] border border-[#232730] p-3.5">
      <div className="flex flex-wrap items-center justify-between gap-2 mb-3">
        <div className="flex items-center gap-2">
          <span className="text-xs font-semibold tracking-wider uppercase text-[#e8e6e1]">
            Acoustic Spaces & Impulse Responses
          </span>
          <span className="text-[11px] font-mono text-[#9a9da3]">
            ({IR_PRESETS.length} Authentic Acoustic Environments)
          </span>
        </div>

        {/* Categories */}
        <div className="flex items-center gap-1 overflow-x-auto pb-0.5">
          {categories.map((cat) => (
            <button
              key={cat}
              onClick={() => setActiveCategory(cat)}
              className={`px-2.5 py-1 rounded text-xs font-medium transition-colors ${
                activeCategory === cat
                  ? 'bg-[#f2a93b] text-[#0e0f11] font-bold'
                  : 'bg-[#181b22] text-[#9a9da3] hover:text-[#e8e6e1]'
              }`}
            >
              {cat}
            </button>
          ))}
        </div>
      </div>

      {/* Preset Cards Grid */}
      <div className="grid grid-cols-1 sm:grid-cols-2 lg:grid-cols-3 gap-2.5">
        {filteredPresets.map((preset) => {
          const isSelected = !customIrName && currentPreset?.id === preset.id;
          return (
            <button
              key={preset.id}
              onClick={() => onSelectPreset(preset)}
              className={`group flex flex-col justify-between p-3 rounded-lg border text-left transition-all ${
                isSelected
                  ? 'bg-[#191d26] border-[#f2a93b] ring-1 ring-[#f2a93b]/40 shadow-[0_0_15px_rgba(242,169,59,0.12)]'
                  : 'bg-[#14161a] border-[#22252e] hover:border-[#353a47] hover:bg-[#16181f]'
              }`}
            >
              <div>
                <div className="flex items-center justify-between gap-1 mb-1.5">
                  <div className="flex items-center gap-2">
                    <span className={`p-1.5 rounded ${isSelected ? 'bg-[#f2a93b]/20 text-[#f2a93b]' : 'bg-[#1d2028] text-[#9a9da3]'}`}>
                      {getIcon(preset.icon)}
                    </span>
                    <span className="font-semibold text-xs text-[#e8e6e1] group-hover:text-[#f2a93b] transition-colors">
                      {preset.name}
                    </span>
                  </div>
                  {isSelected && (
                    <span className="flex items-center gap-0.5 text-[10px] font-mono font-bold text-[#f2a93b] bg-[#f2a93b]/10 px-1.5 py-0.5 rounded">
                      <Check className="w-3 h-3" /> ACTIVE
                    </span>
                  )}
                </div>

                <p className="text-[11px] text-[#868a94] line-clamp-2 leading-relaxed mb-2.5">
                  {preset.description}
                </p>
              </div>

              <div className="flex items-center justify-between pt-2 border-t border-[#1d2028] text-[10px] font-mono text-[#9a9da3]">
                <span>Decay: <strong className="text-[#f2a93b]">{preset.decayTime}s</strong></span>
                <span>Pre-delay: <strong>{preset.preDelayMs}ms</strong></span>
                <span>Spread: <strong>{(preset.stereoSpread * 100).toFixed(0)}%</strong></span>
              </div>
            </button>
          );
        })}

        {/* Custom IR Drop / Upload Card */}
        <div
          onDragOver={(e) => {
            e.preventDefault();
            setIsDraggingOver(true);
          }}
          onDragLeave={() => setIsDraggingOver(false)}
          onDrop={handleDrop}
          onClick={() => fileInputRef.current?.click()}
          className={`flex flex-col items-center justify-center p-3 rounded-lg border-2 border-dashed cursor-pointer text-center transition-all ${
            customIrName
              ? 'bg-[#191d26] border-[#f2a93b] text-[#f2a93b]'
              : isDraggingOver
              ? 'bg-[#1f2430] border-[#f2a93b] text-[#f2a93b]'
              : 'bg-[#121418] border-[#252833] text-[#9a9da3] hover:border-[#3d4354] hover:text-[#e8e6e1]'
          }`}
        >
          <input
            ref={fileInputRef}
            type="file"
            accept=".wav,.aif,.aiff,audio/*"
            className="hidden"
            onChange={(e) => {
              const file = e.target.files?.[0];
              if (file) {
                onUploadCustomIR(file);
              }
            }}
          />
          <FolderUp className="w-5 h-5 mb-1.5 text-[#f2a93b]" />
          <span className="font-semibold text-xs text-[#e8e6e1]">
            {customIrName ? customIrName : 'Drop Custom IR File'}
          </span>
          <span className="text-[10px] text-[#6e727c] mt-0.5">
            {customIrName ? 'Custom Impulse Response Active' : 'WAV / AIFF / FLAC True Stereo IR'}
          </span>
        </div>
      </div>
    </div>
  );
};
