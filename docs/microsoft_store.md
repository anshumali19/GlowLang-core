# Publishing GlowLang on the Microsoft Store

Windows blocks or warns about unsigned programs downloaded from the internet,
and Smart App Control blocks them outright. Apps published on the Microsoft
Store as MSIX packages are signed by Microsoft, so Windows trusts them, at no
cost. Registration is free for individual developers.

The release workflow builds the Store package on every release; publishing it
takes a free Partner Center account and the one-time steps below.

## What the package contains

`scripts/build-msix.ps1` builds `GlowLang-x64.msix` from
`packaging/msix/AppxManifest.xml.in`. It holds `glowlang.exe`, `gdu.exe`, the
standard library (`lib/`), LICENSE and NOTICE. Once installed:

- `glowlang` and `gdu` work in every terminal (Command Prompt, PowerShell,
  Windows Terminal), without editing PATH;
- the Start menu gets a **GlowLang** entry that opens the interactive REPL,
  and a **gdu (GlowLang packages)** entry. The Store does not accept a
  program without a Start entry unless Microsoft grants a waiver.

## One-time setup

1. **Create a developer account (free).** Go to
   [storedeveloper.microsoft.com](https://storedeveloper.microsoft.com), choose
   **Individual**, sign in with your Microsoft account and verify your
   identity with a government ID and a selfie.
2. **Reserve the name.** In [Partner Center](https://partner.microsoft.com/dashboard),
   open **Apps and games → New product → MSIX or PWA app** and reserve
   `GlowLang` (or another name if it is taken).
3. **Copy the package identity.** In the new app, open
   **Product management → Product identity** and copy three values.
4. **Add them to GitHub.** In **GlowLang-core → Settings → Secrets and
   variables → Actions → Variables**, create:

   | Variable | Value from Product identity |
   |---|---|
   | `MSIX_IDENTITY_NAME` | Package/Identity/Name |
   | `MSIX_PUBLISHER` | Package/Identity/Publisher (starts with `CN=`) |
   | `MSIX_PUBLISHER_DISPLAY_NAME` | Package/Properties/PublisherDisplayName |

   Until these are set, the workflow still builds the package with a
   placeholder identity (with a warning), but the Store rejects that package.

## Each release

1. **Build.** Push a version tag (for example `git tag v1.3.3` then
   `git push origin v1.3.3`). The **GlowLang Release** workflow builds and
   tests the package. Each Store version must be higher than the last.
2. **Download.** Open that workflow run on GitHub (Actions → GlowLang
   Release) and download the **store-msix-x64** artifact. It is a zip that
   contains `GlowLang-x64.msix`. Artifacts are kept for 90 days.
3. **Submit.** In Partner Center, open the app and select **Start your
   submission** (later releases: **Update**). Fill in:
   - **Pricing and availability:** Free.
   - **Properties:** category *Developer tools*. Privacy policy URL:
     `https://github.com/anshumali19/GlowLang-core/blob/main/PRIVACY.md`.
   - **Age ratings:** answer the questionnaire (no user-generated content
     sharing, no purchases).
   - **Packages:** upload `GlowLang-x64.msix`.
   - **Store listings:** description, and at least one screenshot (for
     example a terminal running a GlowLang program), 1366×768 or larger.
   - **Submission options → restricted capabilities:** the package uses
     `runFullTrust`. Explanation to enter: *GlowLang is a programming
     language runtime and command-line tool. It runs as a normal desktop
     process so that it can run the user's programs, read and write the
     files they choose, and be started from any terminal.*
4. **Wait for certification.** Microsoft reviews the submission, usually in
   up to three business days, and emails the result. Once it passes, GlowLang
   appears in the Store, signed by Microsoft.

## Notes

- **Unsigned on purpose.** The `.msix` from the workflow is unsigned because
  the Store signs it. It cannot be installed by double-clicking; use the Store.
- **x64 only.** Windows on ARM can run it through emulation.
- **Other downloads keep working.** The `.zip`, `.tar.gz` and `gdu`
  downloads on GlowLang-packages are unchanged.
