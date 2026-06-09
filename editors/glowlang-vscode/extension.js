const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');
const path = require('path');
const { exec } = require('child_process');

let client;
let outputChannel;
let statusBarItem;

function activate(context) {
    outputChannel = vscode.window.createOutputChannel('GlowLang');
    
    // Status bar
    statusBarItem = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 100);
    statusBarItem.text = '$(flame) GlowLang';
    statusBarItem.tooltip = 'GlowLang is active';
    statusBarItem.command = 'glowlang.run';
    statusBarItem.show();
    context.subscriptions.push(statusBarItem);

    // LSP Client
    const config = vscode.workspace.getConfiguration('glowlang');
    if (config.get('enableLsp', true)) {
        startLspClient(context, config);
    }

    // Register Commands
    context.subscriptions.push(
        vscode.commands.registerCommand('glowlang.run', () => runCurrentFile(false)),
        vscode.commands.registerCommand('glowlang.runWithArgs', () => runWithArguments()),
        vscode.commands.registerCommand('glowlang.debug', () => runCurrentFile(true)),
        vscode.commands.registerCommand('glowlang.profile', () => profileCurrentFile()),
        vscode.commands.registerCommand('glowlang.openProfiler', () => openHtmlTool('profiler', 'GlowLang Profiler')),
        vscode.commands.registerCommand('glowlang.openStackVisualizer', () => openHtmlTool('stack_visualizer', 'GlowLang Stack Visualizer')),
        vscode.commands.registerCommand('glowlang.openPlayground', () => openHtmlTool('playground', 'GlowLang Playground')),
        vscode.commands.registerCommand('glowlang.packageInit', () => runGlowCommand('--init')),
        vscode.commands.registerCommand('glowlang.packageInstall', () => runGlowCommand('--install')),
        vscode.commands.registerCommand('glowlang.restartLsp', () => restartLsp(context))
    );

    outputChannel.appendLine('[GlowLang] Extension activated');
}

function startLspClient(context, config) {
    let serverPath = config.get('path');
    if (!serverPath || serverPath === 'glowlang') {
        serverPath = path.join(context.extensionPath, '../../bin/glowlang');
    }

    const serverOptions = {
        run:   { command: serverPath, args: ['--lsp'], transport: TransportKind.stdio },
        debug: { command: serverPath, args: ['--lsp', '--debug'], transport: TransportKind.stdio }
    };

    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'glow' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.glow')
        },
        outputChannel: outputChannel,
        traceOutputChannel: outputChannel
    };

    client = new LanguageClient(
        'glowlangLsp',
        'GlowLang LSP Server',
        serverOptions,
        clientOptions
    );

    client.start().then(() => {
        outputChannel.appendLine('[GlowLang] LSP server connected');
        statusBarItem.text = '$(flame) GlowLang LSP ✓';
    }).catch(err => {
        outputChannel.appendLine(`[GlowLang] LSP failed to start: ${err.message}`);
        statusBarItem.text = '$(flame) GlowLang (no LSP)';
    });
}

function getGlowPath() {
    const config = vscode.workspace.getConfiguration('glowlang');
    let p = config.get('path', 'glowlang');
    if (p === 'glowlang') {
        // Try workspace-relative binary
        const ws = vscode.workspace.workspaceFolders;
        if (ws && ws.length > 0) {
            const localBin = path.join(ws[0].uri.fsPath, 'bin', 'glowlang');
            const fs = require('fs');
            if (fs.existsSync(localBin)) return localBin;
        }
    }
    return p;
}

function runCurrentFile(debug) {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'glow') {
        vscode.window.showWarningMessage('No active .glow file to run');
        return;
    }

    editor.document.save().then(() => {
        const filePath = editor.document.fileName;
        const glowPath = getGlowPath();
        const args = debug ? `--debug "${filePath}"` : `"${filePath}"`;
        
        const terminal = vscode.window.createTerminal({
            name: debug ? 'GlowLang Debug' : 'GlowLang Run',
            iconPath: new vscode.ThemeIcon('flame')
        });
        terminal.sendText(`${glowPath} ${args}`);
        terminal.show();
    });
}

function runWithArguments() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'glow') {
        vscode.window.showWarningMessage('No active .glow file');
        return;
    }

    vscode.window.showInputBox({
        prompt: 'Enter arguments for the GlowLang script',
        placeHolder: '--flag1 value1 --flag2 value2'
    }).then(args => {
        if (args === undefined) return;
        editor.document.save().then(() => {
            const filePath = editor.document.fileName;
            const glowPath = getGlowPath();
            const terminal = vscode.window.createTerminal({ name: 'GlowLang Run' });
            terminal.sendText(`${glowPath} "${filePath}" ${args}`);
            terminal.show();
        });
    });
}

function profileCurrentFile() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'glow') {
        vscode.window.showWarningMessage('No active .glow file to profile');
        return;
    }

    editor.document.save().then(() => {
        const filePath = editor.document.fileName;
        const glowPath = getGlowPath();
        
        const terminal = vscode.window.createTerminal({ name: 'GlowLang Profiler' });
        terminal.sendText(`${glowPath} --profile "${filePath}"`);
        terminal.show();
        
        vscode.window.showInformationMessage(
            'Profiling complete. Open profiler UI?', 'Open Profiler'
        ).then(choice => {
            if (choice === 'Open Profiler') {
                vscode.commands.executeCommand('glowlang.openProfiler');
            }
        });
    });
}

function openHtmlTool(toolName, title) {
    const ws = vscode.workspace.workspaceFolders;
    if (!ws || ws.length === 0) {
        vscode.window.showErrorMessage('No workspace folder open');
        return;
    }

    const toolsDir = path.join(ws[0].uri.fsPath, 'tools');
    let htmlFile;
    
    if (toolName === 'profiler') {
        htmlFile = path.join(toolsDir, 'profiler', 'profiler.html');
    } else if (toolName === 'playground') {
        htmlFile = path.join(toolsDir, 'playground', 'playground.html');
    } else {
        htmlFile = path.join(toolsDir, 'stack_visualizer', 'visualizer.html');
    }

    const fs = require('fs');
    if (!fs.existsSync(htmlFile)) {
        vscode.window.showErrorMessage(`Tool not found: ${htmlFile}`);
        return;
    }

    const panel = vscode.window.createWebviewPanel(
        `glowlang.${toolName}`,
        title,
        vscode.ViewColumn.One,
        {
            enableScripts: true,
            retainContextWhenHidden: true
        }
    );

    panel.webview.html = fs.readFileSync(htmlFile, 'utf-8');
}

function runGlowCommand(command) {
    const ws = vscode.workspace.workspaceFolders;
    if (!ws || ws.length === 0) {
        vscode.window.showErrorMessage('No workspace folder open');
        return;
    }

    const glowPath = getGlowPath();
    const terminal = vscode.window.createTerminal({ name: 'GlowLang Package Manager' });
    terminal.sendText(`cd "${ws[0].uri.fsPath}" && ${glowPath} ${command}`);
    terminal.show();
}

async function restartLsp(context) {
    if (client) {
        await client.stop();
        outputChannel.appendLine('[GlowLang] LSP server stopped');
    }
    const config = vscode.workspace.getConfiguration('glowlang');
    startLspClient(context, config);
}

function deactivate() {
    if (statusBarItem) statusBarItem.dispose();
    if (outputChannel) outputChannel.dispose();
    if (!client) return undefined;
    return client.stop();
}

module.exports = {
    activate,
    deactivate
};
