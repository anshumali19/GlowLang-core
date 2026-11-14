# PostgreSQL

GlowLang talks to PostgreSQL directly. The protocol is built into the
runtime, so there is nothing to install: no libpq, no driver, no DLL. It
works the same in the Microsoft Store version and on Linux and macOS.

```glow
use "postgres"

let db = postgres.connect("postgres://shop:secret@localhost:5432/shop")

db.script("create table if not exists items (id serial primary key, name text not null, price numeric(10,2))")
let pen = db.insert("items", {"name": "pen", "price": "20.50"})
show pen["id"]                                   # 1

for each item in db.query("select name, price from items where price < $1", [100]):
    show item["name"] + " costs " + item["price"]
end

db.close()
```

## Connecting

`postgres.connect` takes a URL, a dict, or nothing (then it reads the
`DATABASE_URL` environment variable):

```glow
let db = postgres.connect("postgres://user:password@host:5432/database?sslmode=require")

let db = postgres.connect({
    "host": "localhost", "port": 5432,
    "user": "shop", "password": "secret", "database": "shop",
    "sslmode": "prefer", "connect_timeout": 10, "timeout": 30
})
```

In a URL, write special characters in the password with `%` codes: a space
is `%20`, `@` is `%40`, `/` is `%2F`.

| Option | Default | Meaning |
|---|---|---|
| `host`, `port` | `localhost`, `5432` | Where the server is |
| `user`, `password` | `postgres`, empty | Who logs in |
| `database` | same as `user` | Which database |
| `sslmode` | `prefer` | `disable`, `prefer` (TLS if the server offers it), `require` (always TLS, any certificate) or `verify-full` (TLS with a trusted certificate for this host name) |
| `connect_timeout` | 10 | Seconds allowed for connecting and logging in |
| `timeout` | none | Seconds to wait for each answer; a slower query raises an error and closes the connection |
| `application_name` | `glowlang` | Shown in the server's `pg_stat_activity` |

**Across the internet, use `sslmode=verify-full`.** It is the only mode that
checks you are talking to the real server. Hosted databases (Supabase, Neon,
AWS RDS, Azure, Google Cloud SQL) have trusted certificates, so
`verify-full` works with them as it is.

Logins: SCRAM-SHA-256 (PostgreSQL's default), MD5, plain passwords and
`trust`. When the server proves it knows your password (SCRAM), GlowLang
checks that proof too.

## Running SQL

Values always go in `$1`, `$2` ... parameters, never into the SQL text:

```glow
db.query("select * from users where email = $1", [email])      # safe
db.query("select * from users where email = '" + email + "'")  # never do this
```

Parameters are sent separately from the SQL, so a value like
`x'); drop table users; --` is just text to the server.

| Method | Returns |
|---|---|
| `db.query(sql, params)` | Every row as a dict: `[{"id": 1, "name": "pen"}, ...]` |
| `db.one(sql, params)` | The first row as a dict, or `none` |
| `db.value(sql, params)` | The first column of the first row, or `none` |
| `db.execute(sql, params)` | How many rows an `insert`, `update` or `delete` changed |
| `db.insert(table, dict)` | Inserts one row and returns it as stored, with generated ids and defaults |
| `db.raw(sql, params)` | `{"columns": [...], "rows": [[...]], "command": "SELECT 2", "affected": 2}`, without building dicts |
| `db.script(sql)` | Runs several statements separated by `;`. Takes no parameters; meant for setup and migrations |
| `db.status()` | `"idle"`, `"transaction"`, `"failed"`, `"broken"` or `"closed"` |
| `db.server_version()` | For example `170002` for PostgreSQL 17.2 |
| `db.close()` | Closes the connection; closing twice is fine |

`postgres.ident(name)` quotes a table or column name, for names you build
SQL from. `db.insert` already does this.

## Types

| PostgreSQL | GlowLang |
|---|---|
| `integer`, `bigint`, `smallint` | int (exact, however large) |
| `real`, `double precision` | float |
| `boolean` | `true` / `false` |
| `text`, `varchar`, `char`, `uuid` | string |
| `numeric` / `decimal` | **string**, so money keeps every digit. `float(x)` converts it |
| `json`, `jsonb` | dicts, lists, numbers ... |
| one-dimensional arrays (`int[]`, `text[]` ...) | list |
| `date`, `timestamp`, `timestamptz`, `time`, `interval` | ISO text, such as `"2026-11-15 10:30:00+05:30"` |
| `bytea` | hex text, such as `"\x00ff"` |
| `NULL` | `none` |

Parameters: `none` is `NULL`. A dict is sent as JSON (for `json` and `jsonb`
columns). A list is sent as a PostgreSQL array, so
`where id = any($1::int[])` with `[1, 2, 3]` works. To store a list in a
`jsonb` column, send `json.stringify(list)`.

## Transactions

```glow
func transfer(tx):
    tx.execute("update accounts set balance = balance - $1 where id = $2", [100, from_id])
    tx.execute("update accounts set balance = balance + $1 where id = $2", [100, to_id])
end

db.transaction(transfer)
```

`transaction` commits when the function returns and rolls back if it raises,
then raises the error again. Calling `transaction` inside another one uses a
savepoint, so a failure inside undoes only the inner part.

## Errors

A failed statement raises an error like:

```
postgres: duplicate key value violates unique constraint "items_name_key" [23505]
DETAIL: Key (name)=(pen) already exists.
```

`postgres.error_code(e)` gives the SQLSTATE code in brackets, for deciding
what to do:

```glow
try:
    db.insert("users", {"email": email})
catch e:
    if postgres.error_code(e) == "23505":
        show "That email is already registered."
    else:
        throw e
    end
end
```

The connection stays usable after an SQL error. If the connection itself is
lost (the server restarted, the network dropped, `timeout` passed), the error
says so, `db.status()` becomes `"broken"`, and you open a new connection.

## Threads

Give each thread its own connection. Two threads using one connection at the
same time is refused with an error instead of mixing up their results. While
a query waits for the server, other GlowLang threads keep running.

## Testing

`tests/integration/test_postgres.glow` runs against a real server:

```bash
PG_TEST_URL="postgres://user:password@localhost:5432/testdb" glowlang tests/integration/test_postgres.glow
```

CI runs it on every pull request against PostgreSQL 17 with TLS on, also with
the interpreter only and under GC stress.

## Not supported yet

- `LISTEN` / `NOTIFY` and `COPY`
- Connection pools (open one connection per thread or per request)
- SCRAM with channel binding (`SCRAM-SHA-256-PLUS` only); plain SCRAM is supported
- Client certificates (`sslcert`, `sslkey`)
