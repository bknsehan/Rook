const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const path = require('path');
const fs = require('fs');

let client = null;

function findLspBinary() {
    const config = vscode.workspace.getConfiguration('rook');
    const customPath = config.get('lsp.path');
    if (customPath && customPath !== 'rook-lsp') {
        if (fs.existsSync(customPath)) return customPath;
    }

    const envRookHome = process.env.ROOK_HOME;
    if (envRookHome) {
        const p = path.join(envRookHome, 'bin', process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp');
        if (fs.existsSync(p)) return p;
    }

    const home = process.env.HOME || process.env.USERPROFILE;
    if (home) {
        const candidates = [
            path.join(home, 'bin', process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp'),
            path.join(home, '.local', 'bin', process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp'),
            path.join(home, 'bin', 'Rook', 'bin', process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp'),
            path.join(home, '.cargo', 'bin', process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp'),
        ];
        for (const p of candidates) {
            if (fs.existsSync(p)) return p;
        }
    }

    // Try workspace build dir
    const workspaceFolders = vscode.workspace.workspaceFolders;
    if (workspaceFolders && workspaceFolders.length > 0) {
        const wsPath = workspaceFolders[0].uri.fsPath;
        const buildBin = path.join(wsPath, 'build', process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp');
        if (fs.existsSync(buildBin)) return buildBin;
    }

    return process.platform === 'win32' ? 'rook-lsp.exe' : 'rook-lsp';
}

function activate(context) {
    const serverCommand = findLspBinary();
    const outputChannel = vscode.window.createOutputChannel('Rook Language Server');

    // Warn if binary doesn't exist on disk (PATH fallback won't show up via existsSync)
    if (!fs.existsSync(serverCommand) && !serverCommand.endsWith('rook-lsp') && !serverCommand.endsWith('rook-lsp.exe')) {
        vscode.window.showWarningMessage(
            `Rook LSP: binary not found at '${serverCommand}'. ` +
            `Set 'rook.lsp.path' in settings or install rook-lsp to ~/bin/rook-lsp.`
        );
    }

    outputChannel.appendLine(`[Rook LSP] Starting server: ${serverCommand}`);

    const serverOptions = {
        command: serverCommand,
        args: [],
        options: { env: { ...process.env } }
    };

    const clientOptions = {
        documentSelector: [
            { scheme: 'file', language: 'rook' },
            { scheme: 'file', pattern: '**/*.rook' },
            { scheme: 'file', pattern: '**/*.rk' }
        ],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.{rook,rk}')
        },
        outputChannel
    };

    client = new LanguageClient(
        'rook-lsp',
        'Rook Language Server',
        serverOptions,
        clientOptions
    );

    client.start().catch(err => {
        outputChannel.appendLine(`[Rook LSP] Failed to start: ${err}`);
        vscode.window.showErrorMessage(`Rook LSP failed to start: ${err.message || err}`);
    });
}

function deactivate() {
    if (!client) {
        return undefined;
    }
    return client.stop();
}

module.exports = {
    activate,
    deactivate
};
