# Service clients: Razorpay, WhatsApp, SMS and Google Sheets

Ready-made clients for services that small businesses and apps in India use
every day. Each one is a GlowLang library built on `http.request` (HTTPS
with certificate checks), so there is nothing extra to install.

| Library | For | Sign-in |
|---|---|---|
| `use "razorpay"` | Orders, payments, refunds, payment links; checking Checkout and webhook signatures | Key id + key secret |
| `use "whatsapp"` | Sending text, template, image and document messages with the WhatsApp Cloud API; reading webhooks | Access token + phone number id |
| `use "sms"` | Sending SMS with Twilio; checking Twilio's webhook signatures | Account SID + auth token (or an API key) |
| `use "sheets"` | Reading and writing Google Sheets | A service account key, or an access token |
| `use "rest"` | The shared helpers, for calling any other JSON API | — |

Common to all of them:

- **Keep secrets out of your code.** Read them with `os.getenv(...)`.
- **Errors** raise text that starts with the library's name and carries the
  service's own message and code, e.g.
  `razorpay: BAD_REQUEST_ERROR: The amount must be atleast INR 1.00 (field: amount)`.
  The whole error the service sent is kept in `client.last_error` (a dict,
  with the HTTP status under `"status"`), and is `none` after a call that worked.
- **Options** (the last argument when creating a client): `"timeout"` in
  seconds (default 30) and `"base_url"` (to go through a proxy, or to test
  against a mock server).
- **`client.request(...)`** reaches any endpoint the library has no
  function for yet.
- **Signature checks** compare in constant time and simply return `false`
  for anything missing or malformed, so they never raise in a webhook handler.

## Razorpay

```glow
use "razorpay"
let rp = razorpay.client(os.getenv("RAZORPAY_KEY_ID"), os.getenv("RAZORPAY_KEY_SECRET"))

# 1. Create an order (amounts in paise: 49900 = Rs 499.00)
let order = rp.create_order(49900, "INR", {"receipt": "order-1042", "notes": {"customer": "Asha"}})
# give order["id"] and your key id to Razorpay Checkout on your page

# 2. Checkout returns razorpay_payment_id, razorpay_order_id, razorpay_signature.
#    Check the signature against the order id YOU stored, not the browser's:
if rp.verify_payment(order["id"], payment_id, signature):
    show "paid"
end
```

| Function | Does |
|---|---|
| `rp.create_order(amount, currency = "INR", extra)` | `extra`: `receipt`, `notes`, `partial_payment`, `first_payment_min_amount` |
| `rp.order(id)`, `rp.order_payments(id)` | An order; the list of its payments |
| `rp.payment(id)`, `rp.payments(filters)` | A payment; a list (`from`, `to`, `count` up to 100, `skip`) |
| `rp.capture(payment_id, amount, currency = "INR")` | Captures an authorized payment |
| `rp.refund(payment_id, amount = none, extra)` | Full refund when `amount` is `none`; `extra`: `speed`, `notes`, `receipt` |
| `rp.refunds(payment_id)` | The refunds of a payment |
| `rp.create_payment_link(link)` | `link`: `amount`, `description`, `reference_id`, `customer`, `notify`, `expire_by`, `callback_url`, `callback_method`: `"get"`, ... Send the reply's `short_url` to the customer |
| `rp.payment_link(id)`, `rp.cancel_payment_link(id)` | |
| `rp.verify_payment(order_id, payment_id, signature)` | After Checkout |
| `rp.verify_payment_link(link_id, reference_id, status, payment_id, signature)` | After a payment link's `callback_url` redirect |
| `rp.verify_subscription(payment_id, subscription_id, signature)` | After a subscription's first payment |
| `razorpay.verify_webhook(raw_body, signature, webhook_secret)` | `X-Razorpay-Signature`; use the webhook secret, not the key secret |
| `rp.request(method, path, body, query)` | Any other endpoint, e.g. `rp.request("GET", "/customers")` |

Amounts must be whole numbers: `499.5` is refused before anything is sent.
For webhooks, verify the raw body exactly as it arrived; parsing and
re-encoding it changes the bytes and the signature no longer matches.
Razorpay may deliver events more than once and out of order, so use the
`x-razorpay-event-id` header to ignore repeats.

## WhatsApp (Cloud API)

```glow
use "whatsapp"
let wa = whatsapp.client(os.getenv("WHATSAPP_TOKEN"), os.getenv("WHATSAPP_PHONE_NUMBER_ID"))

wa.send_text("919876543210", "Your order 1042 has shipped!")
wa.send_template("919876543210", "order_update", "en",
    [{"type": "body", "parameters": [{"type": "text", "text": "1042"}]}])
wa.send_document("919876543210", "https://shop.example/invoice-1042.pdf", "invoice-1042.pdf", "Your invoice")
```

| Function | Does |
|---|---|
| `wa.send_text(number, text, preview_url = false)` | Returns the message id (`wamid...`) |
| `wa.send_template(number, name, language = "en_US", components)` | An approved template |
| `wa.send_image(number, url_or_media_id, caption)` | |
| `wa.send_document(number, url_or_media_id, filename, caption)` | |
| `wa.mark_read(message_id)` | Blue ticks on a message you received |
| `wa.send(number, kind, content)` | Any other message type, e.g. `wa.send(n, "location", {...})` |
| `whatsapp.verify_subscription(query, verify_token)` | Webhook setup (GET): returns the challenge to answer, or `none` (answer 403) |
| `whatsapp.verify_signature(raw_body, header, app_secret)` | `X-Hub-Signature-256` (`sha256=...`) |
| `whatsapp.parse_webhook(body)` | `{"messages": [...], "statuses": [...]}` |

A parsed message has `from`, `name`, `id`, `timestamp` (a number), `type`,
`text` (for text messages), `phone_number_id` and `raw` (the original
object, for images, buttons and the rest). A status has `id`, `status`
(`sent`, `delivered`, `read`, `failed`), `recipient`, `timestamp`, `errors`
and `raw`.

Numbers are in international format; `+`, spaces, brackets and dashes are
removed (`"+91 98765-43210"` is sent as `919876543210`). Outside the 24 hours
after a customer last wrote to you, WhatsApp only delivers approved
templates. The Graph API version defaults to `v23.0`; pass
`{"version": "v24.0"}` (or whichever is current) to use another.

A webhook with the `web` library (put it behind HTTPS, e.g. nginx or Caddy;
Meta only calls https URLs):

```glow
import "lib/web.glow"
use "whatsapp"
let wa = whatsapp.client(os.getenv("WHATSAPP_TOKEN"), os.getenv("WHATSAPP_PHONE_NUMBER_ID"))

func verify(req, res):
    let challenge = whatsapp.verify_subscription(req.query_params, os.getenv("WA_VERIFY_TOKEN"))
    when challenge == none:
        res.status(403).text("forbidden")
    otherwise:
        res.text(challenge)
    end
end

func incoming(req, res):
    when not whatsapp.verify_signature(req.body, req.get_header("X-Hub-Signature-256"), os.getenv("WA_APP_SECRET")):
        res.status(401).text("bad signature")
        return none
    end
    for each m in whatsapp.parse_webhook(req.body)["messages"]:
        when m["type"] == "text":
            wa.send_text(m["from"], "Thanks " + str(m["name"]) + ", we got: " + m["text"])
        end
    end
    res.text("ok")
end

let app = Server(8080)
app.get("/webhook", verify)
app.post("/webhook", incoming)
app.start()
```

## SMS (Twilio)

```glow
use "sms"
let tw = sms.twilio(os.getenv("TWILIO_ACCOUNT_SID"), os.getenv("TWILIO_AUTH_TOKEN"), "+15005550006")
let msg = tw.send("+919876543210", "Your code is 482913")
show msg["sid"], msg["status"]            # SM..., queued
show tw.message(msg["sid"])["status"]     # later: delivered, undelivered, failed
```

| Function | Does |
|---|---|
| `sms.twilio(account_sid, auth_token, sender, options)` | `sender`: a Twilio number, or a Messaging Service SID (`MG...`). Options also take `api_key` and `api_secret` |
| `tw.send(number, text, extra)` | `number` in E.164 (`+91...`). `extra`: any Twilio parameter, e.g. `StatusCallback`, `MediaUrl` (a list), `From` |
| `tw.message(sid)` | A message with its current `status`, `error_code`, `price` |
| `sms.twilio_signature_ok(url, fields, header, auth_token)` | Checks `X-Twilio-Signature` on status callbacks and incoming SMS |

For `twilio_signature_ok`, `url` must be exactly the URL configured in
Twilio (scheme, host, port, path and query), and `fields` the POST form
fields as a dict. The check follows Twilio's documented algorithm and
reproduces its worked example.

Sending SMS to Indian numbers requires DLT registration of the sender and
template, whichever provider you use. MSG91 is not included yet: its API
reference could not be checked against official documentation, and a client
that cannot be verified is not shipped.

## Google Sheets

```glow
use "sheets"
let gs = sheets.service_account(fs.read("service-account.json"))
let id = "1BxiMVs0XRA5nFMdKvBdBZjgmUUqptlbs74OgvE2upms"     # from the sheet's URL

for each row in gs.read(id, "Orders!A2:D"):
    show row
end
gs.append(id, "Orders!A:D", [["1043", "Meera", 120, "paid"]])
gs.write(id, "Summary!B1", [["Total", "=SUM(Orders!C:C)"]])
show gs.records(id, "Orders")        # [{"id": "1041", "name": "Ravi", ...}, ...]
```

Setting up: in the Google Cloud console, enable the Google Sheets API,
create a service account, add a JSON key and download it. Then share the
spreadsheet with the account's `client_email`, as you would with a person.

| Function | Does |
|---|---|
| `sheets.service_account(key, options)` | `key`: the JSON key as text or a dict. Signs in when needed and reuses the token until a minute before it expires |
| `sheets.with_token(access_token, options)` | An access token you already have (e.g. from an OAuth login) |
| `gs.read(id, range, formatted = true)` | A list of rows; `[]` for an empty range. `formatted = false` gives numbers as numbers |
| `gs.read_many(id, ranges)` | The rows of several ranges in one call |
| `gs.records(id, range)` | The rows under a header row, as dicts |
| `gs.write(id, range, rows, raw = false)` | Overwrites cells from the range's top-left corner |
| `gs.append(id, range, rows, raw = false)` | Adds rows after the table (inserted, never overwriting) |
| `gs.clear(id, range)` | Empties the cells (formatting stays) |
| `gs.info(id)` | `{"id", "title", "url", "sheets": [{"title", "id"}]}` |
| `gs.create(title)` | A new spreadsheet owned by the service account |

Values are written as if typed (`USER_ENTERED`: formulas run, dates are
recognised); `raw = true` stores them exactly. Google leaves out empty rows
and cells at the end of a range, so a row can be shorter than the range;
`records` fills those cells with `""`. Rows must be lists of cells:
`[["a", 1]]`, not `["a", 1]`.

## Calling other APIs: `rest`

```glow
use "rest"
let r = rest_call("github", "GET", "https://api.github.com/repos/anshumali19/GlowLang-core" + rest_query({"per_page": 5}),
    {"Accept": "application/vnd.github+json"}, none, 30)
show r["status"], r["data"]["stargazers_count"]
```

`rest_call(service, method, url, headers, body, timeout)` returns
`{"status", "headers", "body", "data"}` (`data` is the parsed JSON, or
`none`) and raises only when no request could be made. Also
`rest_enc(text)` (percent-encoding), `rest_query(dict)`, `rest_form(dict)`
(lists repeat the name), `rest_basic(user, password)` and
`rest_header(response, name)` (any case).

## Tested against

- `tests/test_razorpay.glow` (46 checks), `tests/test_whatsapp.glow` (40),
  `tests/test_sms.glow` (29) and `tests/test_sheets.glow` (38), each against
  a mock of the service started inside the test. The mocks check every
  request the way the service documents it (method, path, query, headers,
  authentication, body) and answer with the service's documented replies
  and errors. The Sheets mock verifies the service account's signed JWT.
- Signatures were checked against values computed independently with
  Python's `hmac` module, against Twilio's documented worked example, and
  against the Razorpay SDK's signing code.
- None of the clients has been run against the live services in CI (that
  needs real accounts). The request formats follow the official
  documentation as of November 2026.
