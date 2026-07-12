import * as vscode from 'vscode';
import { InspectionResult } from './types';

const cache = new Map<string, InspectionResult>();

export function setCached(uri: string, result: InspectionResult) {
    cache.set(uri, result);
}

export function getCached(uri: string): InspectionResult | undefined {
    return cache.get(uri);
}

export function deleteCached(uri: string) {
    cache.delete(uri);
}
