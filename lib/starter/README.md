# __NAME__

A web API written in [GlowLang](https://github.com/anshumali19/GlowLang-core):
sign-up, login with tokens, and notes stored in SQLite. Everything is in
`app.glow`; nothing else needs installing.

```bash
glowlang run app.glow          # serves on http://localhost:8000
glowlang watch app.glow        # the same, restarted each time you save a .glow file
glowlang run test_app.glow     # starts it, calls every endpoint, stops it (or: glowlang test)
```

| Method | Path | Send | Answer |
|---|---|---|---|
| GET | `/health` | | `{"ok": true}` |
| POST | `/signup` | `{"email", "password"}` (8+ characters) | 201 `{"id"}` |
| POST | `/login` | `{"email", "password"}` | `{"token"}`, valid for a day |
| GET | `/notes` | token | your notes |
| POST | `/notes` | token, `{"text"}` | 201 the new note |
| DELETE | `/notes/:id` | token | `{"deleted"}` |

Send the token as the header `Authorization: Bearer <token>`:

```bash
curl -X POST localhost:8000/signup -d '{"email":"ada@example.com","password":"correct horse"}'
curl -X POST localhost:8000/login  -d '{"email":"ada@example.com","password":"correct horse"}'
curl localhost:8000/notes -H "Authorization: Bearer <token>"
```

Passwords are stored as salted PBKDF2 hashes (`hash.password`), never as
text, and every SQL value is a bound parameter.

## Settings

`glowlang new` wrote them to `.env`, with a secret made for this project.
The app reads that file at start; a variable that is already set in the
environment wins over it. `.env` is in `.gitignore`: keep it out of git.

| Variable | Default | |
|---|---|---|
| `PORT` | `8000` | |
| `DB_PATH` | `app.db` | the SQLite file |
| `APP_SECRET` | made once and kept in `secret.txt` | signs login tokens; set it in production and keep it private |

## Deploying

```bash
glowlang package app.glow      # one file: app (app.exe on Windows)
```

Copy that file to the server and run it with the settings above. It needs
no GlowLang install. Put it behind a reverse proxy such as Caddy or nginx for
HTTPS.
