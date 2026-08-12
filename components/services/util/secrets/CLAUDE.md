# Secrets Directory

This directory manages configuration secrets for the application.

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
