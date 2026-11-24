# __NAME__

A command-line tool written in [GlowLang](https://github.com/anshumali19/GlowLang-core).
It counts the lines, words and characters of files and lists the most
common words; change `words.glow` and `__NAME__.glow` to make it do your job.

```bash
glowlang run __NAME__.glow README.md --top 5
glowlang test                          # the tests in test_words.glow
glowlang package __NAME__.glow         # one executable: ./__NAME__ README.md
```

`glowlang package` puts GlowLang, the tool and the libraries it uses into a
single file that runs on a machine without GlowLang. Add `--target
linux-x64` (or `macos-arm64`, `macos-x64`, `windows-x64`) to build it for
another system.
