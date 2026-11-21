# Tools you can ship as one file

Five small, real command-line tools written in GlowLang. Run any of them
with `glowlang run`, or turn it into a single executable that runs on a
machine without GlowLang:

```bash
glowlang package examples/tools/logsum.glow
./logsum /var/log/app.log
```

`glowlang package` puts GlowLang, the tool and every library it imports
into that one file ([Shipping a script as one file](../../docs/shell_scripting.md#shipping-a-script-as-one-file)).
Every tool has `--help`.

| Tool | What it does | Built with |
|------|--------------|------------|
| [`logsum`](logsum.glow) | Summarizes log files: lines per level, the most common errors (numbers ignored), errors per hour | `re`, `cli` tables |
| [`csvclean`](csvclean.glow) | Tidies a CSV: trims values, snake_case headers, drops empty and repeated rows, fixes row widths, and reports what it changed | a quoted-field CSV reader in 60 lines |
| [`backup`](backup.glow) | Keeps a copy of a folder up to date, copying only new and changed files; `--delete`, `--dry-run` | `fs.walk`, `fs.stat` |
| [`apicheck`](apicheck.glow) | Checks URLs from a list: status, text in the body, a path in a JSON answer; exit code 1 on any failure, for cron and CI | `http.request`, `json` |
| [`renamer`](renamer.glow) | Renames many files with a regular expression; shows the plan first and refuses clashes | `re.replace`, `fs.move` |

## Examples

```
$ logsum app.log --top 3
10 lines
+-------+-------+
| level | lines |
+-------+-------+
| FATAL | 1     |
| ERROR | 4     |
| WARN  | 1     |
| INFO  | 2     |
| DEBUG | 1     |
+-------+-------+
most common errors:
+-------+-----------------------------------+
| count | error                             |
+-------+-----------------------------------+
| 3     | timeout after #s calling payments |
| 1     | user # not found                  |
| 1     | disk full on /var                 |
+-------+-----------------------------------+
errors per hour:
2026-11-21 09:00  ############################## 2
2026-11-21 10:00  ############################## 2
2026-11-21 11:00  ############### 1
```

```
$ csvclean people.csv --dedupe -o clean.csv
csvclean: 7 rows in, 5 out; headers: first_name, last_name, email_address, city
  trimmed: 2
  empty: 1
  padded: 1
  cut: 1
  duplicates: 1
```

```
$ backup ~/Documents /mnt/usb/Documents --delete
changed  a.txt
removed  docs/old.md
copied 1 files (14 B), 1 already up to date, 1 removed
```

```
$ apicheck checks.txt
+--------+----+--------------------------------------+--------------------------+
| result | ms | url                                  | detail                   |
+--------+----+--------------------------------------+--------------------------+
| ok     | 3  | https://api.example.com/health       | status 200               |
| FAIL   | 4  | https://api.example.com/v1/user      | JSON has no data.email   |
+--------+----+--------------------------------------+--------------------------+
1 of 2 checks passed
```

```
$ renamer "IMG_(\d+)" "trip-\1" ~/Pictures/trip --apply
IMG_0001.jpg  ->  trip-0001.jpg
IMG_0002.jpg  ->  trip-0002.jpg
2 files renamed
```
