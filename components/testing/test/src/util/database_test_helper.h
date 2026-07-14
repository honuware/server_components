#pragma once

#include <string>
#include <string_view>

#include "sql_util/database_common.h"
#include "sql_util/database_access/database_helper.h"
#include "sql_util/schema/database_info.h"

// Utility class for testing SQL databases
class TestDatabaseUtil {
public:
    enum class SchemaMode { Full, Empty };

    TestDatabaseUtil();
    explicit TestDatabaseUtil(SchemaMode mode);
    TestDatabaseUtil(const TestDatabaseUtil&) = delete;
    TestDatabaseUtil& operator=(const TestDatabaseUtil&) = delete;
    ~TestDatabaseUtil() = default;
    DatabaseHelper GetDatabaseHelper();
    DbSchema::DatabaseInfo GetDatabaseInfo() const;
    void RunInTransaction(
        std::string_view description,
        DatabaseHelper::DatabaseFunc databaseFunc);
    int64_t AddPerson(
        Transaction& transaction,
        std::string_view email,
        std::string_view firstName,
        std::string_view lastName,
        std::string_view passwordHash);
    KeyValueTable PersonKeyValueTable(
        std::string_view email,
        std::string_view firstName,
        std::string_view lastName,
        std::string_view passwordHash);
    KeyValueTable PersonKeyValueTable(
        int64_t id,
        std::string_view email,
        std::string_view firstName,
        std::string_view lastName,
        std::string_view passwordHash);
    KeyValueTable PersonKeyValueTable(
        std::string_view id,
        std::string_view email,
        std::string_view firstName,
        std::string_view lastName,
        std::string_view passwordHash);

private:
    DbSchema::DatabaseInfo databaseInfo_;
};
