#pragma once

#include <string_view>
#include <vector>

#include "sql_util/database_access/database_helper.h"
#include "sql_util/database_common.h"

namespace TableHelpers {

class People
{
public:
    People(DatabaseHelper databaseHelper);
    People(const People&) = default;
    People& operator=(const People&) = default;
    ~People() = default;

    int64_t AddPerson(
        Transaction& transaction,
        std::string_view email,
        std::string_view firstName,
        std::string_view lastName,
        std::string_view passwordHash);
    KeyValueTableArray GetPeople(Transaction& transaction) const;
    KeyValueTable GetPersonById(Transaction& transaction, int64_t personId) const;
    KeyValueTable LookupPersonByEmail(
        Transaction& transaction,
        std::string_view email) const;
    std::string GetCreatedAt(
        Transaction& transaction,
        int64_t personId) const;
    void VerifyPersonEmail(
        Transaction& transaction, std::string_view email);
    void UpdatePerson(
        Transaction& transaction,
        int64_t personId,
        const KeyValueTable& keyValueTable);
    void UpdatePassword(
        Transaction& transaction,
        int64_t personId,
        std::string_view passwordHash);
    // Dedicated setter (like UpdatePassword): must_change_password is an
    // auth flag, deliberately NOT in UpdatePerson's profile-field allowlist.
    void SetMustChangePassword(
        Transaction& transaction,
        int64_t personId,
        bool mustChangePassword);
    void DeletePerson(Transaction& transaction, int64_t personId);

private:
    DatabaseHelper databaseHelper_;
};

} // namespace TableHelpers

