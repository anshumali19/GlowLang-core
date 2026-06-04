#!/bin/bash
# migrate_to_antigravity.sh
# A script to migrate Gemini CLI state to Antigravity CLI (agy)

set -e

GEMINI_DIR="$HOME/.gemini"
ANTIGRAVITY_DIR="$HOME/.gemini/antigravity-cli"
CONFIG_DIR="$HOME/.gemini/config"
WORKSPACE_DIR=$(pwd)

echo "Starting migration from Gemini CLI to Antigravity CLI..."

# 1. Prepare target directories
mkdir -p "$ANTIGRAVITY_DIR"
mkdir -p "$CONFIG_DIR"
mkdir -p "$ANTIGRAVITY_DIR/memory"

# 2. Migrate Global Settings (Safe Merge using Python)
if [ -f "$GEMINI_DIR/settings.json" ]; then
    echo "Migrating and merging global settings..."
    python3 -c "
import json
import os

legacy_path = os.path.expanduser('~/.gemini/settings.json')
new_path = os.path.expanduser('~/.gemini/antigravity-cli/settings.json')

legacy_settings = {}
new_settings = {}

if os.path.exists(legacy_path):
    try:
        with open(legacy_path, 'r') as f:
            legacy_settings = json.load(f)
    except Exception as e:
        print(f'Error reading legacy settings: {e}')

if os.path.exists(new_path):
    try:
        with open(new_path, 'r') as f:
            new_settings = json.load(f)
    except Exception as e:
        print(f'Error reading new settings: {e}')

# Merge them: priority to new settings but keep all keys
merged = {**legacy_settings, **new_settings}

try:
    with open(new_path, 'w') as f:
        json.dump(merged, f, indent=2)
    print('Settings successfully merged.')
except Exception as e:
    print(f'Error writing merged settings: {e}')
"
fi

# 3. Migrate Project-Specific Memory
echo "Searching for project memory in $GEMINI_DIR/tmp..."
if [ -d "$GEMINI_DIR/tmp" ]; then
    for project_dir in "$GEMINI_DIR/tmp"/*; do
        if [ -d "$project_dir/memory" ]; then
            project_id=$(basename "$project_dir")
            echo "Found memory for project: $project_id"
            mkdir -p "$ANTIGRAVITY_DIR/memory/$project_id"
            cp -r "$project_dir/memory/"* "$ANTIGRAVITY_DIR/memory/$project_id/"
        fi
    done
fi

# 4. Migrate Workspace Agent Skills
if [ -d "$WORKSPACE_DIR/.gemini/skills" ]; then
    echo "Migrating workspace Agent Skills to .agents/skills..."
    mkdir -p "$WORKSPACE_DIR/.agents/skills"
    cp -r "$WORKSPACE_DIR/.gemini/skills/"* "$WORKSPACE_DIR/.agents/skills/"
fi

if [ -d "$GEMINI_DIR/skills" ]; then
    echo "Migrating global Agent Skills to $CONFIG_DIR/skills..."
    mkdir -p "$CONFIG_DIR/skills"
    cp -r "$GEMINI_DIR/skills/"* "$CONFIG_DIR/skills/"
fi

# 5. Extract MCP Configuration if present
if [ -f "$GEMINI_DIR/settings.json" ]; then
    echo "Extracting MCP configuration to mcp_config.json..."
    python3 -c "
import json
import os

legacy_path = os.path.expanduser('~/.gemini/settings.json')
mcp_path = os.path.expanduser('~/.gemini/config/mcp_config.json')

if os.path.exists(legacy_path):
    try:
        with open(legacy_path, 'r') as f:
            settings = json.load(f)
        
        # If 'mcp' config exists in settings, extract it
        mcp_config = settings.get('mcp', {})
        if mcp_config:
            with open(mcp_path, 'w') as f:
                json.dump(mcp_config, f, indent=2)
            print('MCP configuration successfully extracted to config/mcp_config.json')
        else:
            print('No MCP configuration found in legacy settings.')
    except Exception as e:
         print(f'Error extracting MCP configuration: {e}')
"
fi

# Write .migrated flag
touch "$CONFIG_DIR/.migrated"

echo ""
echo "Migration steps completed!"
echo "Next steps:"
echo "1. Run 'agy' to initialize or resume the new environment."
echo "2. Run 'agy plugin import gemini' to import your extensions (if any)."
echo "3. Verify your configuration files in ~/.gemini/config/"
