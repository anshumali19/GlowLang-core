#!/bin/bash
PAT="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"
USER="anshumali19"
REPO_NAME="GlowLang"

# Create the repo via API
curl -s -u "$USER:$PAT" https://api.github.com/user/repos -d "{\"name\":\"$REPO_NAME\", \"description\":\"GlowLang 4.0 - High-performance, natural-language systems programming language\", \"private\":false}" > /dev/null

git branch -M main
git remote remove origin 2>/dev/null
git remote add origin "https://$USER:$PAT@github.com/$USER/$REPO_NAME.git"
git push -u origin main
