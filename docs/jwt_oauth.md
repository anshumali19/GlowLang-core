# Logins: JWT and OAuth

Two libraries for signing people in:

- `jwt` makes and checks JSON Web Tokens, the signed tokens apps use to say
  "this request is from user 42" without a database lookup.
- `oauth` does "Sign in with Google / Microsoft / GitHub" (OAuth 2.0 and
  OpenID Connect), and gets Google service-account tokens for server-to-server
  APIs such as Sheets.

Both are built on signing functions in the runtime (OpenSSL), so nothing
needs installing.

## JWT

```glow
use "jwt"

let secret = os.getenv("JWT_SECRET")          # crypto.random_token(32) makes one
let token = jwt.encode({"sub": "42", "exp": jwt.now() + 3600}, secret)

let claims = jwt.decode(token, secret)          # raises if forged, changed or expired
show claims["sub"]                              # 42
```

### With a key pair

Sign with a private key; anyone with the public key can check, but only
you can sign:

```glow
let keys = crypto.generate_keypair("ES256")     # {"private": PEM, "public": PEM}
let token = jwt.encode({"sub": "42"}, keys["private"], "ES256", {"kid": "key-1"})
let claims = jwt.decode(token, keys["public"], {"algorithms": ["ES256"]})
```

| Algorithm | Key |
|---|---|
| `HS256`, `HS384`, `HS512` | A shared secret of at least 32, 48 or 64 characters |
| `RS256`, `RS384`, `RS512` | RSA, 2048 bits or more, as PEM |
| `ES256`, `ES384` | EC P-256 or P-384, as PEM |

### Checking

`jwt.decode(token, key, options)` checks the signature, then the time
claims, and returns the claims. Options:

| Option | Meaning |
|---|---|
| `algorithms` | The algorithms to accept. Defaults to `["HS256"]` with a secret; required with a PEM key |
| `audience` | `aud` must match (text, or a list where any may match) |
| `issuer` | `iss` must match (text or list) |
| `leeway` | Seconds of clock difference to allow (default 0; 30 to 60 is common) |
| `require` | Claims that must be present, e.g. `["exp", "sub"]` |

`exp` (expiry) and `nbf` (not before) are always checked when present.
`key` may also be a dict of key id → key; the token's `kid` picks one.

Errors start with `jwt:`, for example `jwt: the token has expired` or
`jwt: the signature is invalid`.

### Built-in protections

- `alg: none` (an unsigned token) is always refused, in any letter case.
- The token cannot choose a weaker algorithm: only those in `algorithms`
  are accepted.
- An HMAC algorithm refuses a PEM key as its secret. This blocks the
  "algorithm confusion" attack, where a token signed with HS256 using the
  server's *public* key would otherwise verify.
- RSA keys under 2048 bits are refused, and each key must suit its
  algorithm (an RSA key cannot check an ES256 token).
- Signatures are compared in constant time.

`jwt.header(token)` and `jwt.unverified(token)` read a token without
checking it, for debugging only.

## Sign in with Google (OAuth / OpenID Connect)

```glow
use "oauth"

let google = oauth.provider("google", {
    "client_id": os.getenv("GOOGLE_CLIENT_ID"),
    "client_secret": os.getenv("GOOGLE_CLIENT_SECRET"),
    "redirect_uri": "https://myapp.example/auth/callback"})

# Route /login: send the user to Google.
let login = google.authorize_url()
session["state"] = login["state"]
session["verifier"] = login["verifier"]
session["nonce"] = login["nonce"]
# ... redirect to login["url"]

# Route /auth/callback?code=...&state=...
if query["state"] != session["state"]:
    # refuse: the request did not start here
end
let tokens = google.exchange(query["code"], session["verifier"])
let user = google.verify_id_token(tokens["id_token"], session["nonce"])
show user["sub"]       # the user's stable Google id: use this as your key
show user["email"]
```

| Method | What it does |
|---|---|
| `authorize_url(extra)` | The provider's login URL plus `state`, `verifier` and `nonce` to keep in the session. `extra` adds parameters such as `{"access_type": "offline", "prompt": "consent"}` |
| `exchange(code, verifier)` | Swaps the callback's code for `access_token`, `id_token`, `refresh_token`, `expires_in` |
| `verify_id_token(id_token, nonce)` | Checks the ID token against the provider's published keys and returns the user's claims |
| `refresh(refresh_token)` | New tokens without the user |
| `client_credentials(scope)` | Tokens for your app itself, for server-to-server APIs |

What is checked for you:
- PKCE (S256) on every login, so a stolen code is useless.
- The ID token's signature, using the provider's JWKS keys. These are cached
  for an hour and fetched again when the provider rotates keys, at most
  once a minute.
- The issuer, the audience (your `client_id`), expiry and the nonce.

You check `state` yourself, as in the example, because it lives in your
session.

Providers: `google`, `microsoft` (with a `tenant`, default `common`) and
`github`. GitHub has no ID tokens: use the access token with GitHub's API to
read the user. For any other provider, give `OAuthClient` its URLs:

```glow
let client = OAuthClient({"client_id": "...", "client_secret": "...", "redirect_uri": "...",
    "authorize_url": "https://idp.example/authorize", "token_url": "https://idp.example/token",
    "jwks_url": "https://idp.example/.well-known/jwks.json", "issuer": "https://idp.example",
    "scope": "openid email"})
```

Errors start with `oauth:`. When the provider refuses, its own message is
included, for example `oauth: the provider refused: invalid_grant: Bad code`.

## Google service accounts

For server-to-server Google APIs (Sheets, Drive, Gmail API), download the
service account's JSON key, keep it secret, and:

```glow
let token = oauth.google_service_token(fs.read("service-account.json"),
    ["https://www.googleapis.com/auth/spreadsheets"])
# token["access_token"]; reuse it until token["expires_at"]
```

## Crypto functions

The runtime functions under these libraries, in `crypto`:

| Function | Result |
|---|---|
| `crypto.base64(text, url)` | Base64 text; `url = true` gives base64url without padding |
| `crypto.unbase64(text)` | Decoded text (either alphabet; must decode to UTF-8 text) |
| `crypto.random_token(n)` | `n` secure random bytes as base64url, for secrets, states and ids |
| `crypto.sha256_b64url(text)` | SHA-256 as base64url |
| `crypto.sign(alg, key, data)` | A JWS signature as base64url |
| `crypto.verify(alg, key, data, sig)` | `true` or `false` |
| `crypto.generate_keypair(alg, bits)` | `{"private": PEM, "public": PEM}` for RS* (2048/3072/4096 bits) or ES256/ES384 |
| `crypto.pem_to_jwk(pem)` / `crypto.jwk_to_pem(jwk)` | Convert public keys, e.g. to publish your own JWKS |

## Tested against

- `tests/test_jwt.glow` (85 checks) and `tests/test_oauth.glow` (49 checks,
  against a mock identity provider started inside the test).
- Tokens were cross-checked with PyJWT both ways for every algorithm: Python
  verified GlowLang's tokens and JWKs, and GlowLang verified Python's.
- The PKCE example from RFC 7636 and the well-known jwt.io token match exactly.
