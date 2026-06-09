#!/usr/bin/env bash
set -e

echo -e "\033[36m[GlowLang Installer]\033[0m Fetching source and compiling optimized release for POSIX..."

# Remove old installation if it exists
rm -rf ~/.glowlang

# Clone the latest codebase directly from your GitHub
git clone https://github.com/anshumali19/GlowLang.git ~/.glowlang
cd ~/.glowlang

# Build the complete toolchain (GlowLang VM + GDU)
echo -e "\033[36m[GlowLang Installer]\033[0m Compiling GlowLang VM and GDU Package Manager..."
make clean > /dev/null
make > /dev/null

echo -e "\033[32mInstallation complete!\033[0m"
echo "Configuring PATH..."

# Add to bashrc
if ! grep -q "GlowLang" ~/.bashrc; then
    echo -e '\n# GlowLang Toolchain' >> ~/.bashrc
    echo 'export PATH="$HOME/.glowlang/bin:$PATH"' >> ~/.bashrc
fi

# Add to zshrc if it exists
if [ -f ~/.zshrc ]; then
    if ! grep -q "GlowLang" ~/.zshrc; then
        echo -e '\n# GlowLang Toolchain' >> ~/.zshrc
        echo 'export PATH="$HOME/.glowlang/bin:$PATH"' >> ~/.zshrc
    fi
fi

echo -e "\033[32mSuccessfully added to PATH in ~/.bashrc and ~/.zshrc!\033[0m"
echo -e "Please run \033[33msource ~/.bashrc\033[0m or restart your terminal to start using \`glowlang\` and \`gdu\`."
