import React, { useRef, useState, useCallback, useEffect } from 'react';

interface PluginRotaryKnobProps {
  label: string;
  value: number;
  min: number;
  max: number;
  step?: number;
  unit: string;
  defaultValue: number;
  onChange: (val: number) => void;
  size?: number;
}

export const PluginRotaryKnob: React.FC<PluginRotaryKnobProps> = ({
  label,
  value,
  min,
  max,
  step = 0.1,
  unit,
  defaultValue,
  onChange,
  size = 110,
}) => {
  const knobRef = useRef<HTMLDivElement>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [isEditing, setIsEditing] = useState(false);
  const [editString, setEditString] = useState(value.toFixed(1));
  const dragStartY = useRef(0);
  const startVal = useRef(value);

  // Map value to angle (-135 deg to +135 deg, 270 deg total range)
  const norm = Math.min(Math.max((value - min) / (max - min), 0), 1);
  const angle = -135 + norm * 270;

  const handleMouseDown = (e: React.MouseEvent) => {
    if (isEditing) return;
    setIsDragging(true);
    dragStartY.current = e.clientY;
    startVal.current = value;
    window.addEventListener('mousemove', handleMouseMove);
    window.addEventListener('mouseup', handleMouseUp);
  };

  const handleMouseMove = useCallback(
    (e: MouseEvent) => {
      const deltaY = dragStartY.current - e.clientY;
      const sensitivity = (max - min) / 200; // 200px drag for full sweep
      let newVal = startVal.current + deltaY * sensitivity;
      newVal = Math.min(Math.max(newVal, min), max);
      if (step) {
        newVal = Math.round(newVal / step) * step;
      }
      onChange(parseFloat(newVal.toFixed(1)));
    },
    [min, max, step, onChange]
  );

  const handleMouseUp = useCallback(() => {
    setIsDragging(false);
    window.removeEventListener('mousemove', handleMouseMove);
    window.removeEventListener('mouseup', handleMouseUp);
  }, [handleMouseMove]);

  const handleWheel = (e: React.WheelEvent) => {
    e.preventDefault();
    const dir = e.deltaY < 0 ? 1 : -1;
    let newVal = value + dir * step * 2;
    newVal = Math.min(Math.max(newVal, min), max);
    onChange(parseFloat(newVal.toFixed(1)));
  };

  const handleDoubleClick = () => {
    onChange(defaultValue);
  };

  // SVGs for the circular arc
  const radius = size * 0.42;
  const center = size / 2;
  const arcStrokeWidth = 4.5;
  const circumference = 2 * Math.PI * radius;
  // 270 deg out of 360 deg
  const maxArcLength = circumference * (270 / 360);
  const currentArcLength = maxArcLength * norm;

  return (
    <div className="flex flex-col items-center select-none group">
      {/* Label */}
      <span className="text-xs font-semibold tracking-wider uppercase text-[#9a9da3] mb-2 group-hover:text-[#e8e6e1] transition-colors">
        {label}
      </span>

      {/* Rotary Dial */}
      <div
        ref={knobRef}
        onMouseDown={handleMouseDown}
        onWheel={handleWheel}
        onDoubleClick={handleDoubleClick}
        className="relative cursor-ns-resize touch-none"
        style={{ width: size, height: size }}
        title={`Drag up/down, scroll, or double-click to reset to ${defaultValue}${unit}`}
      >
        <svg
          className="absolute inset-0 -rotate-[225deg] transform origin-center"
          width={size}
          height={size}
          viewBox={`0 0 ${size} ${size}`}
        >
          {/* Background track */}
          <circle
            cx={center}
            cy={center}
            r={radius}
            fill="none"
            stroke="#1c1e24"
            strokeWidth={arcStrokeWidth}
            strokeDasharray={`${maxArcLength} ${circumference}`}
            strokeLinecap="round"
          />
          {/* Active value track */}
          <circle
            cx={center}
            cy={center}
            r={radius}
            fill="none"
            stroke="#f2a93b"
            strokeWidth={arcStrokeWidth}
            strokeDasharray={`${currentArcLength} ${circumference}`}
            strokeLinecap="round"
            className="transition-[stroke-dasharray] duration-75"
          />
        </svg>

        {/* Knob Body */}
        <div
          className={`absolute rounded-full border border-[#2b303b] shadow-2xl transition-all duration-100 flex items-center justify-center ${
            isDragging ? 'ring-2 ring-[#f2a93b]/50 shadow-[#f2a93b]/10' : ''
          }`}
          style={{
            top: size * 0.13,
            left: size * 0.13,
            width: size * 0.74,
            height: size * 0.74,
            background: 'radial-gradient(circle at 35% 35%, #252830 0%, #111216 100%)',
          }}
        >
          {/* Metallic brushed ring accent */}
          <div className="absolute inset-1.5 rounded-full border border-[#363b47]/50" />

          {/* Notch pointer */}
          <div
            className="w-full h-full relative"
            style={{
              transform: `rotate(${angle}deg)`,
              transformOrigin: 'center center',
            }}
          >
            <div
              className="absolute left-1/2 -translate-x-1/2 bg-[#f2a93b] rounded-full shadow-[0_0_8px_#f2a93b]"
              style={{
                top: 4,
                width: 3.5,
                height: 12,
              }}
            />
          </div>
        </div>
      </div>

      {/* Value Input / Readout */}
      <div className="mt-2.5">
        {isEditing ? (
          <input
            type="number"
            step={step}
            min={min}
            max={max}
            autoFocus
            value={editString}
            onChange={(e) => setEditString(e.target.value)}
            onBlur={() => {
              setIsEditing(false);
              const num = parseFloat(editString);
              if (!isNaN(num)) {
                onChange(Math.min(Math.max(num, min), max));
              }
            }}
            onKeyDown={(e) => {
              if (e.key === 'Enter') {
                setIsEditing(false);
                const num = parseFloat(editString);
                if (!isNaN(num)) {
                  onChange(Math.min(Math.max(num, min), max));
                }
              } else if (e.key === 'Escape') {
                setIsEditing(false);
              }
            }}
            className="w-20 px-1 py-0.5 text-center bg-[#17191e] border border-[#f2a93b] text-[#e8e6e1] text-xs font-mono rounded"
          />
        ) : (
          <button
            onClick={() => {
              setEditString(value.toFixed(1));
              setIsEditing(true);
            }}
            className="px-2.5 py-0.5 rounded bg-[#14161a] border border-[#232730] text-[#e8e6e1] hover:border-[#f2a93b]/50 text-xs font-mono font-medium tracking-tight transition-colors"
          >
            {value.toFixed(1)} {unit}
          </button>
        )}
      </div>
    </div>
  );
};
