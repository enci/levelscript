import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';

// The native debugger (`levelscript-debugger`) is a separate program shipped by
// the installer / built from this repo. The extension finds it once, stores the
// location in the `levelscript.debuggerPath` setting, and launches it on the
// active .ls file.

const SETTING = 'levelscript.debuggerPath';
const EXE = process.platform === 'win32' ? 'levelscript-debugger.exe' : 'levelscript-debugger';

function isFile(p: string): boolean {
    try { return fs.statSync(p).isFile(); } catch { return false; }
}

function configured(): string {
    return vscode.workspace.getConfiguration().get<string>(SETTING, '').trim();
}

function fromPath(): string | undefined {
    for (const dir of (process.env.PATH ?? '').split(path.delimiter)) {
        if (!dir) continue;
        const candidate = path.join(dir, EXE);
        if (isFile(candidate)) return candidate;
    }
    return undefined;
}

// Where the installer / typical setups put it, then dev build trees.
function fromKnownLocations(): string | undefined {
    const dirs: string[] = [];
    if (process.platform === 'win32') {
        const local = process.env.LOCALAPPDATA;
        if (local) dirs.push(path.join(local, 'Programs', 'LevelScript', 'bin'));
    } else {
        dirs.push(path.join(os.homedir(), '.local', 'bin'), '/usr/local/bin', '/opt/homebrew/bin');
    }
    // Dev builds: <repo>/cmake-build-*/app[/<config>] and <repo>/build/app[/<config>],
    // for the repo this extension lives in and for open workspace folders.
    const roots = [path.resolve(__dirname, '..', '..', '..'),
                   ...(vscode.workspace.workspaceFolders ?? []).map(f => f.uri.fsPath)];
    for (const root of roots) {
        let entries: string[] = [];
        try { entries = fs.readdirSync(root); } catch { continue; }
        for (const e of entries) {
            if (e !== 'build' && !e.startsWith('cmake-build-')) continue;
            for (const sub of ['app', path.join('app', 'Debug'), path.join('app', 'Release')])
                dirs.push(path.join(root, e, sub));
        }
    }
    for (const dir of dirs) {
        const candidate = path.join(dir, EXE);
        if (isFile(candidate)) return candidate;
    }
    return undefined;
}

async function remember(exe: string) {
    await vscode.workspace.getConfiguration().update(SETTING, exe, vscode.ConfigurationTarget.Global);
}

async function browse(): Promise<string | undefined> {
    const picked = await vscode.window.showOpenDialog({
        canSelectMany: false,
        openLabel: 'Select levelscript-debugger',
        title: 'Locate the LevelScript debugger executable',
        filters: process.platform === 'win32' ? { Executable: ['exe'] } : undefined,
    });
    const exe = picked?.[0]?.fsPath;
    if (exe) await remember(exe);
    return exe;
}

// The stored path if it still exists; otherwise search the system and store
// the result; otherwise ask the user to point at it.
async function resolveDebugger(forcePrompt = false): Promise<string | undefined> {
    if (!forcePrompt) {
        const stored = configured();
        if (stored && isFile(stored)) return stored;
        const found = fromPath() ?? fromKnownLocations();
        if (found) {
            await remember(found);
            return found;
        }
        const choice = await vscode.window.showWarningMessage(
            'LevelScript debugger (levelscript-debugger) was not found on this system.',
            'Locate…', 'Cancel');
        if (choice !== 'Locate…') return undefined;
    }
    return browse();
}

export function registerDebugger(ctx: vscode.ExtensionContext) {
    async function launch(doc: vscode.TextDocument) {
        // The debugger reads the file from disk.
        if (doc.isDirty && !(await doc.save())) return;
        const exe = await resolveDebugger();
        if (!exe) return;
        const child = cp.spawn(exe, [doc.uri.fsPath], {
            cwd: path.dirname(doc.uri.fsPath),
            detached: true,
            stdio: 'ignore',
        });
        child.on('error', async err => {
            const choice = await vscode.window.showErrorMessage(
                `Could not start the LevelScript debugger (${exe}): ${err.message}`, 'Locate…');
            if (choice === 'Locate…' && await resolveDebugger(true)) launch(doc);
        });
        child.unref();
    }

    ctx.subscriptions.push(
        vscode.commands.registerCommand('levelscript.run', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'levelscript') {
                vscode.window.showWarningMessage('Open a .ls file to debug it.');
                return;
            }
            return launch(editor.document);
        }),
        vscode.commands.registerCommand('levelscript.locateDebugger', async () => {
            const exe = await resolveDebugger(true);
            if (exe) vscode.window.showInformationMessage(`LevelScript debugger: ${exe}`);
        }),
    );
}
