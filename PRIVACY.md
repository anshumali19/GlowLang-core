# GlowLang Privacy Policy

GlowLang (the `glowlang` runtime and the `gdu` package manager) does not
collect, store or send any personal information or usage data. It has no
telemetry, analytics or crash reporting.

Network access happens only when you ask for it:

- `gdu install`, `gdu search` and similar commands download packages and their
  metadata from GitHub (`github.com` and `raw.githubusercontent.com`), subject
  to [GitHub's privacy statement](https://docs.github.com/site-policy/privacy-policies/github-general-privacy-statement).
- Programs you write in GlowLang can make network requests (for example with
  the `http` module); what they send is up to the program.

Files are read and written only where you or your programs direct.

Questions: open an issue at
https://github.com/anshumali19/GlowLang-core/issues.
