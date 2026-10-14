import * as vscode from 'vscode';
import { InspectionResult } from './types';
import { Resolver } from './resolve';

// eslint-disable-next-line @typescript-eslint/no-var-requires
const createLsModule = require('../../ls_wasm.js');

let wasmModule: any = null;

export async function initWasm() {
    if (!wasmModule) {
        wasmModule = await createLsModule();
    }
}

export function astDump(source: string, name: string): string {
    if (!wasmModule) throw new Error("WASM not initialized");
    return wasmModule.ast_dump(source, name);
}

export function inspectJson(source: string, name: string, resolve?: Resolver): InspectionResult {
    if (!wasmModule) throw new Error("WASM not initialized");
    const resultStr = wasmModule.inspect_json(source, name, resolve);
    return JSON.parse(resultStr) as InspectionResult;
}
