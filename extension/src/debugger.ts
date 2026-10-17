import * as vscode from 'vscode';
import * as cp from 'child_process';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';

// The native debugger (`levelscript-debugger`) is a separate program shipped by
// the installer / built from this repo. The extension finds it once, stores the
// location in the `levelscript.debuggerPath` setting, and launches it on the
// active .lvs file.

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

// `levelscript-debugger --version` prints "levelscript-debugger X.Y.Z".
function versionOf(exe: string): string | undefined {
    try {
        const out = cp.execFileSync(exe, ['--version'], { timeout: 5000, encoding: 'utf8' });
        return /(\d+\.\d+\.\d+)/.exec(out)?.[1];
    } catch { return undefined; }
}

export function registerDebugger(ctx: vscode.ExtensionContext) {
    // The debugger's own output (load errors, crashes) lands here; it has no
    // window to show them in when the script fails before one opens.
    const output = vscode.window.createOutputChannel('LevelScript');
    const ours: string = ctx.extension.packageJSON.version;
    const warnedFor = new Set<string>();

    async function launch(doc: vscode.TextDocument) {
        // The debugger reads the file from disk.
        if (doc.isDirty && !(await doc.save())) return;
        const exe = await resolveDebugger();
        if (!exe) return;

        const version = versionOf(exe);
        output.appendLine(`[${new Date().toLocaleTimeString()}] ${exe} (${version ?? 'unknown version'}) ${doc.uri.fsPath}`);
        if (version && version !== ours && !warnedFor.has(exe)) {
            warnedFor.add(exe);
            vscode.window.showWarningMessage(
                `LevelScript debugger is ${version} but the extension is ${ours} (${exe}).`, 'Locate…')
                .then(async choice => { if (choice === 'Locate…') await resolveDebugger(true); });
        }

        const child = cp.spawn(exe, [doc.uri.fsPath], {
            cwd: path.dirname(doc.uri.fsPath),
            detached: true,
            stdio: ['ignore', 'pipe', 'pipe'],
        });
        child.stdout?.on('data', d => output.append(d.toString()));
        child.stderr?.on('data', d => output.append(d.toString()));
        let failedToStart = false;   // 'exit' may follow 'error'; report once
        child.on('error', async err => {
            failedToStart = true;
            output.appendLine(`could not start: ${err.message}`);
            const choice = await vscode.window.showErrorMessage(
                `Could not start the LevelScript debugger (${exe}): ${err.message}`, 'Locate…');
            if (choice === 'Locate…' && await resolveDebugger(true)) launch(doc);
        });
        child.on('exit', async (code, signal) => {
            if (code === 0 || failedToStart) return;
            output.appendLine(`debugger exited with ${signal ?? `code ${code}`}`);
            const choice = await vscode.window.showErrorMessage(
                `LevelScript debugger exited with ${signal ?? `code ${code}`}.`, 'Show Output');
            if (choice === 'Show Output') output.show(true);
        });
        child.unref();
    }

    ctx.subscriptions.push(
        output,
        vscode.commands.registerCommand('levelscript.run', () => {
            const editor = vscode.window.activeTextEditor;
            if (!editor || editor.document.languageId !== 'levelscript') {
                vscode.window.showWarningMessage('Open a .lvs file to debug it.');
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
