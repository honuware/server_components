#include "database_test_helper.h"

#include <string>
#include <string_view>

#include "db_schema/people.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "global_database_test_support.h"

// The composed schema comes from the harness (which the app-side test main
// injected via MakeDatabaseInfo), so this utility — like the rest of
// honuware_testing — never calls the app's schema composition root itself.
//
// Clone() is essential: the harness hands back a DatabaseInfo that shares its
// pImpl, and many tests add per-test tables (e.g. "test_people") to the schema
// they get from GetDatabaseInfo(). Without a deep copy those AddTable calls
// would mutate the single shared global schema and the second test to add the
// table would throw "table already exists". Each TestDatabaseUtil therefore
// owns an independent deep copy.
TestDatabaseUtil::TestDatabaseUtil()
    : databaseInfo_(GlobalDatabaseTestSupport::GetInstance().GetDatabaseInfo().Clone()) {
}

TestDatabaseUtil::TestDatabaseUtil(SchemaMode mode)
    : databaseInfo_(mode == SchemaMode::Full
          ? GlobalDatabaseTestSupport::GetInstance().GetDatabaseInfo().Clone()
          : DbSchema::DatabaseInfo(kTestDatabaseName)) {
}

DbSchema::DatabaseInfo TestDatabaseUtil::GetDatabaseInfo() const {
    return databaseInfo_;
}

DatabaseHelper TestDatabaseUtil::GetDatabaseHelper() {
    return GlobalDatabaseTestSupport::GetInstance().GetDatabaseHelper();
}

void TestDatabaseUtil::RunInTransaction(
    std::string_view description,
    DatabaseHelper::DatabaseFunc databaseFunc) {
    GetDatabaseHelper().RunInTransaction(description, databaseFunc);
}

int64_t TestDatabaseUtil::AddPerson(
    Transaction& transaction,
    std::string_view email,
    std::string_view firstName,
    std::string_view lastName,
    std::string_view passwordHash) {
    return DbCrud::AddRowToTableFetchInt64PrimaryKey(
        transaction,
        GetDatabaseHelper(),
        DbSchema::kPeopleTable,
       {
            DbPair(DbSchema::kPeopleEmail, email),
            DbPair(DbSchema::kPeopleFirstName, firstName),
            DbPair(DbSchema::kPeopleLastName, lastName),
            DbPair(DbSchema::kPeoplePasswordHash, passwordHash)
        });
}

KeyValueTable TestDatabaseUtil::PersonKeyValueTable(
    std::string_view email,
    std::string_view firstName,
    std::string_view lastName,
    std::string_view passwordHash) {
    return KeyValueTable{
        {std::string(DbSchema::kPeopleEmail), std::string(email)},
        {std::string(DbSchema::kPeopleFirstName), std::string(firstName)},
        {std::string(DbSchema::kPeopleLastName), std::string(lastName)},
        {std::string(DbSchema::kPeoplePasswordHash), std::string(passwordHash)}
    };
}

KeyValueTable TestDatabaseUtil::PersonKeyValueTable(
    int64_t id,
    std::string_view email,
    std::string_view firstName,
    std::string_view lastName,
    std::string_view passwordHash) {
    return PersonKeyValueTable(
        std::to_string(id), email, firstName, lastName, passwordHash);
}

KeyValueTable TestDatabaseUtil::PersonKeyValueTable(
    std::string_view id,
    std::string_view email,
    std::string_view firstName,
    std::string_view lastName,
    std::string_view passwordHash) {
    return KeyValueTable{
        {std::string(DbSchema::kPeopleId), std::string(id)},
        {std::string(DbSchema::kPeopleEmail), std::string(email)},
        {std::string(DbSchema::kPeopleFirstName), std::string(firstName)},
        {std::string(DbSchema::kPeopleLastName), std::string(lastName)},
        {std::string(DbSchema::kPeoplePasswordHash), std::string(passwordHash)}
    };
}


