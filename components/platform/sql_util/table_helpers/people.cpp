#include "people.h"

#include "db_schema/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "sql_util/database_access/database_util.h"
#include "util/date_time_util.h"

namespace TableHelpers {

People::People(DatabaseHelper databaseHelper)
    : databaseHelper_(databaseHelper) {}

int64_t People::AddPerson(
    Transaction& transaction,
    std::string_view email,
    std::string_view firstName,
    std::string_view lastName,
    std::string_view passwordHash) {
    KeyValueTable kv = {
        { std::string(DbSchema::kPeopleEmail), std::string(email) },
        { std::string(DbSchema::kPeopleFirstName), std::string(firstName) },
        { std::string(DbSchema::kPeopleLastName), std::string(lastName) },
        { std::string(DbSchema::kPeoplePasswordHash), std::string(passwordHash) }
    };
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        kv);
}

KeyValueTableArray People::GetPeople(Transaction& transaction) const {
    return DbCrud::GetTableRows(
        transaction, databaseHelper_, DbSchema::kPeopleTable);
}

KeyValueTable People::GetPersonById(
    Transaction& transaction, int64_t personId) const {
    return DbCrud::GetRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleId,
        StringFromInt(personId));
}

KeyValueTable People::LookupPersonByEmail(
    Transaction& transaction, std::string_view email) const {
    return DbCrud::GetRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleEmail,
        email);
}

std::string People::GetCreatedAt(
    Transaction& transaction,
    int64_t personId) const {
    return transaction.RunSqlStatementReturningOneValue(
        "SELECT to_char(to_timestamp("
        + std::string(DbSchema::kPeopleCreatedAt)
        + " / 1000000.0), 'FMMonth FMDD, YYYY' ) AS friendly FROM "
        + std::string(DbSchema::kPeopleTable) + " WHERE "
        + std::string(DbSchema::kPeopleId) + " = $1",
        StringFromInt(personId));
}


void People::VerifyPersonEmail(
    Transaction& transaction, std::string_view email) {
    KeyValueTable person = LookupPersonByEmail(transaction, email);
    if (person.empty()) {
        throw std::runtime_error("People::VerifyPersonEmail - Person not found");
    }
    if(!person.at(std::string(DbSchema::kPeopleEmailVerifiedAt)).empty()) {
        throw std::runtime_error("People::VerifyPersonEmail - EmailVerifiedAt already set");
    }

    // Mark email as verified and (re)set the email to the given value.
    KeyValueTable kv = {
        {std::string(DbSchema::kPeopleUpdatedAt), std::string(DbSchema::kDatabaseInfoDefaultNow)},
        {std::string(DbSchema::kPeopleEmailVerifiedAt), std::string(DbSchema::kDatabaseInfoDefaultNow)}
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleEmail,
        email,
        kv,
        { std::string(DbSchema::kDatabaseInfoDefaultNow) });
}

void People::UpdatePerson(
    Transaction& transaction,
    int64_t personId,
    const KeyValueTable& keyValueTable) {
    StringSet allowedFields = {
        std::string(DbSchema::kPeopleEmail),
        std::string(DbSchema::kPeopleFirstName),
        std::string(DbSchema::kPeopleLastName)
    };
    for (const auto& [key, value] : keyValueTable) {
        if (!SetContains(allowedFields, key)) {
            throw std::runtime_error("People::UpdatePerson - key not allowed");
        }
    }
    auto keyValueTableCopy = keyValueTable;
    keyValueTableCopy[std::string(DbSchema::kPeopleUpdatedAt)] = DbSchema::kDatabaseInfoDefaultNow;
    // Update arbitrary fields for a person by id.
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleId,
        StringFromInt(personId),
        keyValueTableCopy,
        { std::string(DbSchema::kDatabaseInfoDefaultNow) });
}

void People::UpdatePassword(
    Transaction& transaction,
    int64_t personId,
    std::string_view passwordHash) {
    KeyValueTable keyValueTable = {
        {std::string(DbSchema::kPeoplePasswordHash), std::string(passwordHash)},
        {std::string(DbSchema::kPeopleUpdatedAt), std::string(DbSchema::kDatabaseInfoDefaultNow)}
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleId,
        StringFromInt(personId),
        keyValueTable,
        { std::string(DbSchema::kDatabaseInfoDefaultNow) });
}

void People::SetMustChangePassword(
    Transaction& transaction,
    int64_t personId,
    bool mustChangePassword) {
    KeyValueTable keyValueTable = {
        {std::string(DbSchema::kPeopleMustChangePassword),
         mustChangePassword ? "true" : "false"},
        {std::string(DbSchema::kPeopleUpdatedAt),
         std::string(DbSchema::kDatabaseInfoDefaultNow)}
    };
    DbCrud::UpdateRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleId,
        StringFromInt(personId),
        keyValueTable,
        { std::string(DbSchema::kDatabaseInfoDefaultNow), "true", "false" });
}

void People::DeletePerson(Transaction& transaction, int64_t personId) {
    DbCrud::DeleteRow(
        transaction,
        databaseHelper_,
        DbSchema::kPeopleTable,
        DbSchema::kPeopleId,
        StringFromInt(personId));
}

} // namespace TableHelpers

