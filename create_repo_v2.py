import requests
import json

PAT = "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"
USER = "anshumali19"
REPO = "GlowLang"

# Try to create repository for the authenticated user
url = "https://api.github.com/user/repos"
headers = {
    "Authorization": f"token {PAT}",
    "Accept": "application/vnd.github.v3+json"
}
data = {
    "name": REPO,
    "description": "GlowLang - High-performance, natural-language systems programming language",
    "private": False,
    "has_issues": True,
    "has_projects": True,
    "has_wiki": True
}

r = requests.post(url, headers=headers, json=data)
print(f"Status: {r.status_code}")
if r.status_code != 201:
    print(f"Error: {r.text}")
else:
    print(f"Successfully created repository: {REPO}")
