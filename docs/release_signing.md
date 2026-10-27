# Signing Windows releases

Windows blocks or warns about unsigned programs downloaded from the internet
(Smart App Control blocks them outright). The release workflow signs
`glowlang.exe` and `gdu.exe` with **Azure Artifact Signing** (formerly Trusted Signing), which Windows
trusts, once it is set up. Until then releases are built, tested and published
unsigned, with a warning in the workflow log.

## One-time setup (repository owner)

1. In the Azure portal, create an **Artifact Signing account** (formerly Trusted Signing) and complete
   **identity validation** (individual or organization). Validation is done by
   Microsoft and can take a few days.
2. In that account, create a **certificate profile** of type *Public Trust*.
3. Create an **app registration** (service principal) with a client secret, and
   give it the **Artifact Signing Certificate Profile Signer** role (formerly Trusted Signing Certificate Profile Signer) on the
   signing account.
4. In GitHub, **GlowLang-core → Settings → Secrets and variables → Actions**:

   | Kind | Name | Value |
   |---|---|---|
   | Secret | `AZURE_TENANT_ID` | Directory (tenant) ID of the app registration |
   | Secret | `AZURE_CLIENT_ID` | Application (client) ID |
   | Secret | `AZURE_CLIENT_SECRET` | The client secret |
   | Variable | `TRUSTED_SIGNING_ENDPOINT` | The account's endpoint, e.g. `https://eus.codesigning.azure.net/` |
   | Variable | `TRUSTED_SIGNING_ACCOUNT` | Signing account name |
   | Variable | `TRUSTED_SIGNING_PROFILE` | Certificate profile name |

## How it is checked

- **Release workflow:** after signing, both executables must have a `Valid`
  Authenticode signature, and the full test suite then runs on the signed
  binaries.
- **Verify Release workflow:** after a release is published, it downloads each
  archive on a clean machine for every platform, checks it against
  `SHA256SUMS`, checks the Windows signatures, runs the full test suite with
  the downloaded `glowlang`, and runs `scripts/release-smoke.glow` from outside
  the repository (so the shipped `lib/` is what gets used) along with `gdu`.
  It can also be run by hand for any tag (Actions → Verify Release → Run
  workflow).
