# GlowLang 1.13.0

## New

- **Thirteen standard modules, written in GlowLang.** Each has a section in
  the [language reference](language_reference.md#18-standard-library), a
  test, and a benchmark against Python in
  [benchmarks/libs](../benchmarks/libs/README.md).

  | Module | What it does |
  |---|---|
  | `template` | Mustache-style templates; values are HTML-escaped |
  | `markdown` | Markdown to HTML, safe for text from users |
  | `schema` | Checks data against a JSON Schema and lists every problem |
  | `url` | Parses, builds, joins and encodes URLs and query strings |
  | `csv` | Now reads quoted cells, line breaks inside cells and `\r\n` files, and quotes when writing |
  | `toml`, `yaml` | Read configuration files |
  | `dotenv` | Loads settings from a `.env` file |
  | `log` | Log lines with levels and fields, as text or JSON |
  | `lru` | An in-memory cache with a size limit and expiry |
  | `collections` | A heap (priority queue), a deque, and counting |
  | `semver` | Semantic versions and npm-style ranges such as `^1.2.0` |
  | `diff` | Line differences, printed as `diff -u` prints them |

  ```glow
  import "yaml"
  import "schema"
  import "template"

  let config = yaml.parse("title: Notes\nlimit: 3\n")
  let rules = {"type": "object", "required": ["title"], "properties": {"limit": {"type": "integer", "minimum": 1}}}
  show schema.validate(config, rules)    # []
  show template.render("<h1>{{title}}</h1>", config)    # <h1>Notes</h1>
  ```

- **GlowWeb:**
  - keeps a connection open for the client's next request (HTTP keep-alive);
  - reads cookies (`req.cookies()`) and sets them (`res.set_cookie`, `HttpOnly` and `SameSite=Lax` by default);
  - has `patch` routes;
  - answers `405 Method Not Allowed`, with an `Allow` header, when a path exists but not for that method.
- `http.read(client, seconds)` gives `none` when nothing arrives in time.
- **New string methods:** `is_digit`, `is_alpha`, `is_alnum`, `is_space`.

## Faster

- **Keep-alive.** Clients that reuse their connection get 21,988 requests a
  second from 8 clients, where a new connection per request gave 9,324.
  Node.js gives 36,937 and Python's `http.server` 3,535 on the same test
  ([#155](https://github.com/anshumali19/GlowLang-core/pull/155)).
- **Regular expressions** compile each pattern once and reuse it: `re.find`
  in a loop is 52% faster, and modules that use patterns gain 15% to 134%.
- Speed of the new modules against Python, on Linux under WSL 2:
  - `yaml` parses 6.6× as fast as PyYAML's Python parser;
  - `schema` checks 2× to 3.6× as fast as `jsonschema`;
  - `diff` is 7× as fast as `difflib`;
  - `log` writes 1.3× as fast as `logging`.

  The others are slower. `toml`, `url` and `semver` are 1.3× to 5.4×
  slower than Python modules that are also written in Python. `csv`, `lru`,
  and the deque and counting in `collections` are about 2× slower than
  modules written in C; the heap is within 15% of `heapq`. All the figures
  are in
  [benchmarks/libs](../benchmarks/libs/README.md).
- The CPU benchmarks are unchanged from 1.12.0: 1.4× to 6.7× faster than
  CPython ([benchmarks/RESULTS-linux.md](../benchmarks/RESULTS-linux.md)).
  The loop row takes 8 or 9 ms from run to run, which moves its ratio
  between 6.7× and 7.9×.
