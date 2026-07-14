# Mail Directory

This directory is the SMTP mail-sending service for the application. It is part
of the **services** layer (alongside `util/secrets`).

## Layer position (foundation → data → services)

`util/mail` sits in the **services** layer, which is **above** the **data**
layer (`sql_util`) and **below** the **platform**/business-logic layers. See the
"Component layer order" subsection of the root `CLAUDE.md` for the full stack and
the audited include edges.

**`util/mail` may include `sql_util/database_access` — this is conformant, not a
violation.** `MakeMailHelper(Transaction&, SecretsHelperPtr)` reads the SMTP
server / port / credentials out of the `config_secrets` table through
`SecretsHelper`, so `mail_helper.cpp` includes
`sql_util/database_access/transaction.h`. Because services sit one layer *above*
data, that is a downward dependency. Do **not** try to remove the `Transaction`
parameter to "clean up" the include — the dependency is by design and is what
lets the mailer resolve its configuration at call time.

Allowed includes from this directory:
- `util/*` (foundation + the sibling `util/secrets` service)
- `sql_util/database_access/*` (data layer — `Transaction`)
- standard library

NOT allowed (would be upward back-edges):
- `business_logic/*`
- `endpoints/*`

When the code is extracted into components, this directory becomes part of
`honuware_services` (with `util/secrets`), depending on `honuware_data`
(`sql_util`) and `honuware_foundation` (`util` core).

## File Overview

| File | Purpose |
|------|---------|
| `tenant_branding.h` | `Mail::TenantBranding` struct (studio name, sender, website) substituted into framework mail bodies so they carry no hardcoded brand |
| `mail_helper.h` | `MailAddress` / `MailMessage` value types and the `MailHelper` interface |
| `mail_helper.cpp` | mailio-backed SMTP send; `MakeMailHelper(Transaction&, SecretsHelperPtr)` reads SMTP config from `config_secrets` |
| `mail_helper_test_util.h/cpp` | `TestMailHelper` — in-memory capture of sent messages for tests |

## Framework mail bodies must stay brand-free

Framework mail builders (e.g. `business_logic/auth/person_verify_mail.cpp`,
`quick_account_welcome_mail.cpp`) must not hardcode a studio name — they take a
`Mail::TenantBranding` and substitute `{studio_name}` via `FormatString`. App-side
mail builders (`payment/`, `scheduling/`) keep their own literals for now and are
only parameterized in the tenancy project's Phase 4.5. Always wrap generated
bodies with `NormalizeCrLf()` (see the root CLAUDE.md "Email Body Generation").
