# GlowBot

GlowBot writes emails and sends them from your Gmail. It is one GlowLang
file. It never sends anything until it has shown you the whole email and you
have typed `yes`.

```
$ glowlang run examples/glowbot/glowbot.glow send
To (addresses, separated by commas): ana@example.com
Subject: Friday
What should it say? (end with a line holding only a dot)
Can we move our call to 3 pm?
.

---------------- draft ----------------
To:      ana@example.com
Subject: Friday

Can we move our call to 3 pm?
---------------------------------------
Send this email? Type yes to send, anything else cancels: yes
Sent. Gmail message id: 18c2f0a9b3e4d5f6
```

## Commands

| Command | What it does |
|---|---|
| `glowbot login` | Signs in to Google once and saves the permission to send mail |
| `glowbot send` | Asks who the email is for, the subject and the text, shows the email, sends it on `yes` |
| `glowbot send --to a@x.com --subject Hi --body "..."` | The same, with the answers given up front. It still asks before sending |
| `glowbot send --ai` | Takes your rough notes and has a free AI model on your computer write a polished draft. It still shows the draft and asks |
| `glowbot logout` | Deletes the saved sign-in |

`--config PATH` picks the settings file (default `~/.glowbot/config.json`).
`glowbot login --no-browser` prints the sign-in link instead of opening a browser.

Run it with `glowlang run examples/glowbot/glowbot.glow ...`, or build it
into one executable with `glowlang package examples/glowbot/glowbot.glow`.

## Connecting it to your Gmail

You do these steps yourself, once. They are free for personal use.

1. **Create a Google Cloud project.** Go to https://console.cloud.google.com/,
   choose *Select a project → New project*, and name it, for example "GlowBot".
2. **Enable the Gmail API.** In that project, open *APIs & Services → Library*,
   search for "Gmail API" and click *Enable*.
3. **Set up the consent screen.** Open *APIs & Services → OAuth consent screen*
   (it may be called *Google Auth Platform*). Choose *External* and fill in the
   app name and your email. Under *Audience → Test users*, add your own Gmail
   address. The app can stay in *Testing* mode: only the test users you list
   can sign in to it.
4. **Make an OAuth client for a desktop app.** Open *Credentials → Create
   credentials → OAuth client ID*. Choose the application type *Desktop app*.
   Google then shows a client ID and a client secret.
5. **Give them to GlowBot.** Either create `~/.glowbot/config.json`
   (`C:\Users\<you>\.glowbot\config.json` on Windows):

   ```json
   {"client_id": "1234-abc.apps.googleusercontent.com", "client_secret": "GOCSPX-..."}
   ```

   or set the environment variables `GLOWBOT_CLIENT_ID` and `GLOWBOT_CLIENT_SECRET`.
6. **Sign in once:** `glowbot login`. Your browser opens Google's sign-in page.
   Choose your account and allow "Send email on your behalf". Google may warn
   that the app is not verified. That is expected for your own app in Testing
   mode: choose *Continue*. The browser then says "GlowBot is signed in".

From now on `glowbot send` works without signing in again.

> In Testing mode Google ends the sign-in after 7 days, and GlowBot then asks
> you to run `glowbot login` again. To avoid this, set the consent screen's
> publishing status to *In production*. You can do that without Google's
> review, since only you use the app; you will still see the "not verified" warning.

## How it works

- **Sign-in: OAuth 2.0 for installed apps.** `glowbot login` starts a small
  listener on `http://127.0.0.1:8765/`, the redirect address Google allows
  for desktop clients. It sends you to Google with PKCE (S256) and a random
  `state`. When Google sends the browser back with a code, GlowBot checks the
  `state`, ignores any other request, and swaps the code (plus the PKCE
  verifier) for tokens with `lib/oauth.glow`. It asks for `access_type=offline`
  so Google returns a refresh token. Set `"port"` in the config file if 8765
  is taken.
- **Permission: `gmail.send` only.** GlowBot asks for
  `https://www.googleapis.com/auth/gmail.send`. That lets it send mail and
  nothing else: it cannot read, search or delete your email.
- **Sending: the Gmail API.** The email is built as a plain text MIME
  message (UTF-8, with the subject encoded when it is not plain ASCII),
  base64url-encoded, and posted to `users.messages.send`. Gmail sets the From
  address to your account and keeps a copy in *Sent*. Gmail's reply holds the
  message id, which GlowBot prints.
- **The saved sign-in.** Only the refresh token is kept, in `token.json` next
  to the config file (`~/.glowbot/token.json`); on Linux and macOS it is made
  readable only by you. GlowBot never prints it. Each send swaps it for a
  short-lived access token that is held in memory only. `glowbot logout` deletes it;
  to revoke it at Google too, remove GlowBot at
  https://myaccount.google.com/permissions.
- **Why not your password or SMTP?** Google no longer accepts account
  passwords from apps, and an SMTP app password needs 2-step verification
  and gives the full power of your account to anything that holds it, with
  no way to limit it to sending. A Google-issued token for `gmail.send` only
  can be revoked on its own, and GlowBot never sees your password.

## The safety rules

- Nothing is sent until the whole email (recipients, subject, text) has been
  shown and you have typed `yes`. `y`, an empty line or the end of input
  cancel.
- Recipients come only from you. Every address is checked: no line breaks,
  spaces or brackets, so no hidden `Cc:` or `Bcc:` can be slipped in. The
  same goes for the subject.
- With `--ai`, Claude writes only the text. The recipients and the subject
  stay yours, and you still read the draft before saying `yes`.
- If the sign-in has expired or Gmail refuses, GlowBot says so and nothing is sent.

## Drafting help (optional, off by default)

`glowbot send --ai` turns your rough notes into a polished draft. By default
it is **free and private**: it uses [Ollama](https://ollama.com), which runs
an AI model on your own computer, so your notes never leave it.

1. Install Ollama from https://ollama.com (free).
2. Download a model once: `ollama pull llama3.2` (about 2 GB; 8 GB of RAM is enough).
3. Use `glowbot send --ai`. Ollama runs in the background after installing.

`GLOWBOT_AI_MODEL` picks another Ollama model, for example `gemma3` or
`qwen3`. If you set `ANTHROPIC_API_KEY`, GlowBot uses Claude (a paid API)
instead, with `GLOWBOT_AI_MODEL` defaulting to `claude-sonnet-5-5`.

Without `--ai`, nothing leaves your computer except the email itself, which
goes to Gmail.

## Cost

Everything GlowBot needs is free: the Google Cloud project (no billing
account needed), the Gmail API (free for personal use), GlowLang and
Ollama. Only the optional Claude path costs money.

## Tests

`tests/test_glowbot.glow` runs GlowBot against a local GlowWeb server that
plays Google's token endpoint, the Gmail API and the drafting API, and plays
the browser for `glowbot login`. No Google account or network is used. The
config keys `auth_url`, `token_url` and `gmail_url`, and the environment
variable `GLOWBOT_AI_URL`, exist for that.
