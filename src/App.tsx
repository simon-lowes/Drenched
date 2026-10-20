import React, { useState, useEffect } from 'react';
import { audioEngine, AudioEngineState } from './engine/audioEngine';
import { ImpulseResponsePreset } from './engine/irLibrary';
import { PluginHardwareRack } from './components/PluginHardwareRack';
import { ImpulseVisualizer } from './components/ImpulseVisualizer';
import { AudioSourceRack } from './components/AudioSourceRack';
import { PresetBrowser } from './components/PresetBrowser';
import { Catch2TestRunner } from './components/Catch2TestRunner';
import { EngineArchitectureView } from './components/EngineArchitectureView';
import { Sliders, Terminal, Code2, Sparkles, Volume2, AudioLines } from 'lucide-react';

export const App: React.FC = () => {
  const [engineState, setEngineState] = useState<AudioEngineState>(audioEngine.getState());
  const [activeTab, setActiveTab] = useState<'plugin' | 'tests' | 'architecture'>('plugin');

  useEffect(() => {
    const unsubscribe = audioEngine.subscribe((st) => {
      setEngineState({ ...st });
    });
    return unsubscribe;
  }, []);

  const handleMixChange = (val: number) => {
    audioEngine.setMix(val);
  };

  const handleOutputChange = (val: number) => {
    audioEngine.setOutput(val);
  };

  const handleEngineModeChange = (mode: 'web-audio-convolver' | 'direct-convolver-dsp') => {
    audioEngine.setEngineMode(mode);
  };

  const handleSelectPreset = (preset: ImpulseResponsePreset) => {
    audioEngine.loadPreset(preset);
  };

  const handleUploadCustomIR = (file: File) => {
    audioEngine.loadCustomIR(file);
  };

  const handleSourceChange = (src: any) => {
    audioEngine.setSource(src);
  };

  const handleCustomAudioUpload = (file: File) => {
    audioEngine.loadCustomAudioSource(file);
  };

  const handleTogglePlay = () => {
    audioEngine.togglePlay();
  };

  return (
    <div className="min-h-screen bg-[#0a0b0d] text-[#e8e6e1] flex flex-col">
      {/* Top Navbar */}
      <header className="border-b border-[#1b1e25] bg-[#0e1014]/90 backdrop-blur-md sticky top-0 z-50 px-4 sm:px-6 py-3">
        <div className="max-w-6xl mx-auto flex flex-wrap items-center justify-between gap-3">
          <div className="flex items-center gap-3">
            <div className="w-8 h-8 rounded-lg bg-gradient-to-br from-[#f2a93b] to-[#c7821e] flex items-center justify-center text-[#0e0f11] font-black text-base shadow-[0_0_15px_rgba(242,169,59,0.3)]">
              D
            </div>
            <div>
              <div className="flex items-center gap-2">
                <h1 className="font-bold text-sm tracking-wide text-[#e8e6e1]">
                  Drenched Reverb
                </h1>
                <span className="text-[10px] font-mono px-1.5 py-0.5 rounded bg-[#1e222b] text-[#f2a93b] border border-[#2b303d]">
                  v0.1.0-web
                </span>
              </div>
              <p className="text-[11px] text-[#868a94] hidden sm:block">
                Premium Convolution & Hybrid Reverb Engine
              </p>
            </div>
          </div>

          {/* Navigation Tabs */}
          <div className="flex items-center gap-1.5 bg-[#14161d] p-1 rounded-lg border border-[#222631]">
            <button
              onClick={() => setActiveTab('plugin')}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-medium transition-all ${
                activeTab === 'plugin'
                  ? 'bg-[#f2a93b] text-[#0e0f11] font-bold shadow-sm'
                  : 'text-[#9a9da3] hover:text-[#e8e6e1]'
              }`}
            >
              <Sliders className="w-3.5 h-3.5" />
              <span>Plugin Rack</span>
            </button>
            <button
              onClick={() => setActiveTab('tests')}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-medium transition-all ${
                activeTab === 'tests'
                  ? 'bg-[#f2a93b] text-[#0e0f11] font-bold shadow-sm'
                  : 'text-[#9a9da3] hover:text-[#e8e6e1]'
              }`}
            >
              <Terminal className="w-3.5 h-3.5" />
              <span>Catch2 Tests</span>
            </button>
            <button
              onClick={() => setActiveTab('architecture')}
              className={`flex items-center gap-1.5 px-3 py-1.5 rounded-md text-xs font-medium transition-all ${
                activeTab === 'architecture'
                  ? 'bg-[#f2a93b] text-[#0e0f11] font-bold shadow-sm'
                  : 'text-[#9a9da3] hover:text-[#e8e6e1]'
              }`}
            >
              <Code2 className="w-3.5 h-3.5" />
              <span>DSP Architecture</span>
            </button>
          </div>

          {/* Audio Engine Status Badge */}
          <div className="hidden lg:flex items-center gap-3 text-xs font-mono text-[#8a8e99]">
            <div className="flex items-center gap-1.5">
              <span className={`w-2 h-2 rounded-full ${engineState.isPlaying ? 'bg-emerald-400 animate-pulse' : 'bg-[#474c58]'}`} />
              <span>{engineState.sampleRate / 1000} kHz</span>
            </div>
            <span>•</span>
            <span>CPU ~{engineState.cpuLoadEstimate}%</span>
          </div>
        </div>
      </header>

      {/* Main Content Area */}
      <main className="flex-1 max-w-6xl w-full mx-auto p-4 sm:p-6 space-y-6">
        {activeTab === 'plugin' && (
          <div className="space-y-6">
            {/* JUCE Plugin Hardware Rack */}
            <PluginHardwareRack
              mixPercent={engineState.mixPercent}
              outputDb={engineState.outputDb}
              engineMode={engineState.engineMode}
              onMixChange={handleMixChange}
              onOutputChange={handleOutputChange}
              onEngineModeChange={handleEngineModeChange}
            />

            {/* Impulse Response Visualizer & Decay Spectrogram */}
            <ImpulseVisualizer
              irBuffer={audioEngine.getCurrentIRBuffer()}
              rt60DecayTime={engineState.currentPreset?.decayTime || 2.0}
            />

            {/* Dry Audio Auditioning Rack (Play, Instrument Sources, Mic, Custom File) */}
            <AudioSourceRack
              isPlaying={engineState.isPlaying}
              isMicActive={engineState.isMicActive}
              currentSource={engineState.currentSource}
              onTogglePlay={handleTogglePlay}
              onSourceChange={handleSourceChange}
              onCustomAudioUpload={handleCustomAudioUpload}
            />

            {/* Acoustic Space / Impulse Response Preset Browser */}
            <PresetBrowser
              currentPreset={engineState.currentPreset}
              customIrName={engineState.customIrName}
              onSelectPreset={handleSelectPreset}
              onUploadCustomIR={handleUploadCustomIR}
            />
          </div>
        )}

        {activeTab === 'tests' && (
          <div className="space-y-6">
            <Catch2TestRunner />
          </div>
        )}

        {activeTab === 'architecture' && (
          <div className="space-y-6">
            <EngineArchitectureView />
          </div>
        )}
      </main>

      {/* Footer */}
      <footer className="border-t border-[#1b1e25] py-4 px-6 text-center text-xs text-[#6e727c]">
        <div className="max-w-6xl mx-auto flex flex-col sm:flex-row items-center justify-between gap-2 font-mono text-[11px]">
          <span>Drenched Reverb • Ported from C++ JUCE / Catch2 Engine</span>
          <span>Equal-Power DSP • Direct-Form Time-Domain Convolver</span>
        </div>
      </footer>
    </div>
  );
};
