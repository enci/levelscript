import * as vscode from 'vscode';
import { InspectionResult, RunState } from './types';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const createLsModule = require('../../ls_wasm.js');

let wasmModule: any = null;

export async function initWasm() {
    if (!wasmModule) {
        wasmModule = await createLsModule();
    }
}

export function inspectJson(source: string, name: string): InspectionResult {
    if (!wasmModule) throw new Error("WASM not initialized");
    const resultStr = wasmModule.inspect_json(source, name);
    return JSON.parse(resultStr) as InspectionResult;
}

// Compile + start a progressive run; returns a session id, or -1 on a
// compile error (call runLastError() to get the diagnostics text).
export function runBegin(source: string, name: string, seed: number): number {
    if (!wasmModule) throw new Error("WASM not initialized");
    return wasmModule.run_begin(source, name, seed >>> 0);
}

export function runLastError(): string {
    if (!wasmModule) throw new Error("WASM not initialized");
    return wasmModule.run_last_error();
}

export function runState(id: number): RunState {
    if (!wasmModule) throw new Error("WASM not initialized");
    return JSON.parse(wasmModule.run_state(id)) as RunState;
}

export function runStep(id: number): RunState {
    if (!wasmModule) throw new Error("WASM not initialized");
    return JSON.parse(wasmModule.run_step(id)) as RunState;
}

// Advance to the next statement boundary (one or more applications).
export function runNextStatement(id: number): RunState {
    if (!wasmModule) throw new Error("WASM not initialized");
    return JSON.parse(wasmModule.run_next_statement(id)) as RunState;
}

export function runFinish(id: number): RunState {
    if (!wasmModule) throw new Error("WASM not initialized");
    return JSON.parse(wasmModule.run_finish(id)) as RunState;
}

export function runEnd(id: number): void {
    if (!wasmModule) return;
    wasmModule.run_end(id);
}
