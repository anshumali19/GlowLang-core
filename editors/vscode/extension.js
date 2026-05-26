const vscode = require('vscode');
const { LanguageClient } = require('vscode-languageclient/node');
const path = require('path');

let client;

function activate(context) {
    let serverPath = vscode.workspace.getConfiguration('glowlang').get('path');
    if (!serverPath) {
        serverPath = path.join(context.extensionPath, '../../bin/glowlang');
    }

    const serverOptions = {
        run: { command: serverPath, args: ['--lsp'] },
        debug: { command: serverPath, args: ['--lsp'] }
    };
    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'glow' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.glow')
        }
    };

    client = new LanguageClient(
        'glowlangLsp',
        'GlowLang LSP Server',
        serverOptions,
        clientOptions
    );

    client.start();
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
