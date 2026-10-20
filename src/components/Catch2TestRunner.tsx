import React, { useState, useEffect } from 'react';
import { runAllCatch2Tests, TestCaseResult } from '../engine/tests';
import { CheckCircle2, XCircle, RotateCcw, Cpu, Terminal, ShieldCheck } from 'lucide-react';

export const Catch2TestRunner: React.FC = () => {
  const [testResults, setTestResults] = useState<{
    results: TestCaseResult[];
    totalPassed: number;
    totalFailed: number;
  } | null>(null);
  const [isRunning, setIsRunning] = useState(false);

  const executeTests = () => {
    setIsRunning(true);
    setTimeout(() => {
      const outcome = runAllCatch2Tests();
      setTestResults(outcome);
      setIsRunning(false);
    }, 80);
  };

  useEffect(() => {
    executeTests();
  }, []);

  return (
    <div className="rounded-lg bg-[#111317] border border-[#232730] p-4">
      <div className="flex flex-wrap items-center justify-between gap-3 mb-3 pb-3 border-b border-[#1f222a]">
        <div>
          <div className="flex items-center gap-2">
            <Terminal className="w-4 h-4 text-[#f2a93b]" />
            <h3 className="text-sm font-bold tracking-tight text-[#e8e6e1]">
              C++ DSP Catch2 Engine Test Suite
            </h3>
            <span className="px-2 py-0.5 rounded text-[10px] font-mono font-bold bg-[#1d212b] text-[#9a9da3] border border-[#2a2f3d]">
              tests/DirectConvolverTests.cpp & tests/MixTests.cpp
            </span>
          </div>
          <p className="text-xs text-[#848894] mt-0.5">
            Automated mathematical verification of equal-power gains, direct time-domain convolution, and block circular buffers.
          </p>
        </div>

        <div className="flex items-center gap-3">
          {testResults && (
            <div className="flex items-center gap-2 font-mono text-xs">
              <span className="flex items-center gap-1 text-emerald-400 bg-emerald-950/40 px-2 py-1 rounded border border-emerald-800/40">
                <CheckCircle2 className="w-3.5 h-3.5" />
                {testResults.totalPassed} Passed
              </span>
              {testResults.totalFailed > 0 && (
                <span className="flex items-center gap-1 text-rose-400 bg-rose-950/40 px-2 py-1 rounded border border-rose-800/40">
                  <XCircle className="w-3.5 h-3.5" />
                  {testResults.totalFailed} Failed
                </span>
              )}
            </div>
          )}

          <button
            onClick={executeTests}
            disabled={isRunning}
            className="flex items-center gap-1.5 px-3 py-1.5 rounded bg-[#1c202a] border border-[#2e3444] hover:border-[#f2a93b]/50 text-xs font-mono text-[#e8e6e1] hover:text-[#f2a93b] transition-colors disabled:opacity-50"
          >
            <RotateCcw className={`w-3 h-3 ${isRunning ? 'animate-spin text-[#f2a93b]' : ''}`} />
            <span>Re-run Unit Tests</span>
          </button>
        </div>
      </div>

      {/* Test List */}
      <div className="space-y-2">
        {testResults?.results.map((test, index) => (
          <div
            key={index}
            className={`p-2.5 rounded-md border flex flex-col sm:flex-row sm:items-center justify-between gap-2 transition-all ${
              test.passed
                ? 'bg-[#13151a] border-[#1e222a] hover:border-[#2d323e]'
                : 'bg-rose-950/20 border-rose-900/40'
            }`}
          >
            <div className="flex items-start sm:items-center gap-2.5">
              {test.passed ? (
                <CheckCircle2 className="w-4 h-4 text-emerald-400 shrink-0 mt-0.5 sm:mt-0" />
              ) : (
                <XCircle className="w-4 h-4 text-rose-400 shrink-0 mt-0.5 sm:mt-0" />
              )}
              <div>
                <div className="flex items-center gap-2 flex-wrap">
                  <span className="font-semibold text-xs text-[#e8e6e1]">
                    {test.name}
                  </span>
                  <span className="text-[10px] font-mono px-1.5 py-0.2 rounded bg-[#1b1e26] text-[#9a9da3]">
                    [{test.suite.toLowerCase()}]
                  </span>
                </div>
                <div className="text-[11px] text-[#787c88] font-mono mt-0.5">
                  {test.message}
                </div>
              </div>
            </div>

            <div className="flex items-center gap-3 shrink-0 text-[10px] font-mono text-[#8c909c] sm:self-center pl-6 sm:pl-0">
              <span>{test.assertions} assertions</span>
              <span className="text-[#f2a93b]">{test.durationMs.toFixed(2)} ms</span>
            </div>
          </div>
        ))}
      </div>
    </div>
  );
};
