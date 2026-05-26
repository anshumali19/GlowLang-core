#!/bin/bash
PAT="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"
USER="anshumali19"
REPO_URL="https://github.com/anshumali19/GlowLang.git"

echo "Setting up remote and pushing to $REPO_URL..."

git branch -M main
git remote remove origin 2>/dev/null
# Use the NEW PAT for authentication in the URL
git remote add origin "https://$USER:$PAT@github.com/anshumali19/GlowLang.git"

git push -u origin main --force
