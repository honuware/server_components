# Secrets Directory

This directory manages configuration secrets for the application.

## A real credential NEVER gets a compiled-in default

**This is the one rule in this directory that is not a style preference.** This
repository is public and runs a public Actions workflow. A live credential
placed in `secret_values.cpp` is disclosed the moment it is pushed, and stays
disclosed in git history after it is removed — rotation is the only thing that
closes it.

**Understand why it happens, or it will happen again.** As the section below
explains, defaults here are loaded *into the database on first run* **and**
*into the test secrets helper automatically*. Putting a real password here
therefore makes every test, on every machine, work with zero configuration.
That convenience is exactly the trap: this is the most convenient place to put
a secret and the one place it must never go. It has happened once already —
`kMailAppPasswordValue` shipped a live Gmail app password until Phase 9.2.

**The test that follows from it.** If a value can sit in a public repo, it is
not a secret: SMTP host, port, auth method, sender address, brand strings and
routing fragments all belong here with real defaults. If it cannot, it gets an
**empty** default and the live value arrives at runtime:

| tier | holds | where |
|---|---|---|
| store of record | real credentials, encrypted at rest | `config_secrets`, read by `secrets_helper.cpp` |
| bootstrap key | the key decrypting the above | `HONUWARE_SECRET_KEY` env var — never in the DB (circular), never in the repo |
| defaults | **non-secrets only** | `secret_values.cpp` / `app_secret_values.cpp` — public repo |

Exactly one bootstrap secret lives in the environment; everything else sits
behind it. A consuming application seeds the real value over the empty default
at seed time — see `create_database.cpp`, which reads
`HONUWARE_MAIL_APP_PASSWORD` and UPDATEs the row.

**An empty default must fail loud, not fail quietly.** Emptiness is only safe
because something checks it: `MakeMailHelper(Transaction&, SecretsHelperPtr)`
throws with an operator-facing message naming the secret and the environment
variable that seeds it, rather than handing an empty password to the SMTP
server and surfacing an opaque auth error at send time. Follow that shape for
any new secret with an empty default, and cover it with a test — see
`mail_helper_test.cpp` (`FrameworkShipsNoMailPasswordDefault`,
`MakeMailHelperFailsLoudWhenPasswordMissing`).

## Adding a New Secret

When adding a new secret, you must update **two files**:

### 1. Add the key in `secret_keys.h`

```cpp
inline constexpr std::string_view kMyNewSecret = "my_new_secret";
```

### 2. Add the default value in `secret_values.cpp`

```cpp
// Add the value constant near related values
inline constexpr std::string_view kMyNewSecretValue = "default value here";

// Add to FillInSecretsStringView function
addSecret(kMyNewSecret, kMyNewSecretValue);
```

## Why Both Files?

- `secret_keys.h` - Defines the key names used throughout the codebase
- `secret_values.cpp` - Provides default values that are loaded:
  - Into the database on first run (production)
  - Into the test secrets helper automatically (tests)

If you only add a key without a default value, the secret will return empty unless explicitly configured in the database, which can cause unexpected behavior.

## Framework key, application default

`config_secrets.name` is **UNIQUE**, so exactly one side may register a default
for a given key — registering in both places makes the seed insert twice and the
transaction fails. When a key is framework surface but its default VALUE is
brand-specific (`kMailSenderName`, and every `site_*` content slot except the two
URL slots whose neutral default really is `""`), declare the key here and leave
`FillInSecretsStringView` alone: the consuming application supplies the value
through its own defaults registration (knottyyoga's
`business_logic/app_secret_values.cpp`). The app-side test
`AppSecretValuesTest.FrameworkAndAppKeySetsDoNotOverlap` enforces the rule.

## File Overview

| File | Purpose |
|------|---------|
| `secret_keys.h` | Key name constants (e.g., `kMailServerName`) |
| `secret_values.h` | Header for `FillInSecretsString` functions |
| `secret_values.cpp` | Default values and population function |
| `secrets_helper.h` | Interface for looking up secrets |
| `secrets_helper.cpp` | Production implementation (reads from database) |
| `secrets_helper_test_util.h/cpp` | Test implementation (in-memory, auto-loads defaults) |
