import requests
import json

PAT = "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"
USER = "anshumali19"
REPO = "GlowLang"

url = "https://api.github.com/user/repos"
headers = {
    "Authorization": f"token {PAT}",
    "Accept": "application/vnd.github.v3+json"
}
data = {
    "name": REPO,
    "description": "GlowLang - Industrial-grade systems language",
    "private": False
}

r = requests.post(url, headers=headers, data=json.dumps(data))
print(f"Status: {r.status_code}")
print(f"Response: {r.text}")
