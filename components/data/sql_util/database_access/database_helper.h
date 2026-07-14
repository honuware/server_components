#pragma once

#include <functional>
#include <iostream>
#include <memory>

#include "sql_util/database_common.h"
#include "transaction.h"

class DatabaseHelperBase;

class DatabaseHelper {
public:
    using DatabaseFunc = std::function<void(Transaction&)>;

    DatabaseHelper(std::shared_ptr<DatabaseHelperBase> base);
    DatabaseHelper(const DatabaseHelper& ref);
    DatabaseHelper& operator=(const DatabaseHelper& ref);
    ~DatabaseHelper();
    // Fetch the database connection object
    pqxx::connection& GetConnection();
    // Get the database name
    const std::string& GetDatabaseName() const;
    // Run the provided code in the context of a transaction
    // in an exception block.
    void RunInTransaction(
        std::string_view description, DatabaseFunc databaseFunc);
    bool IsTest() const;
private:
    std::shared_ptr<DatabaseHelperBase> base_;
};

// Opens a production PostgreSQL connection to `databaseName`. The name is the
// default; the KNOTTYYOGA_DB_NAME env var overrides it when set. The
// application owns the concrete name (see business_logic/app_database_config.h);
// the framework never hard-codes a brand.
DatabaseHelper MakeProductionDatabaseHelper(std::string_view databaseName);

// This creates a connection to the database server that
// does not have an open database to be able to delete and
// create a database.
DatabaseHelper MakeNoDatabaseHelper();

std::string UrlUnescape(std::string_view url);

// These functions escape strings for SQL statements but do not them in quotes.
std::string EscSqlString(
    DatabaseHelper databaseHelper, std::string_view strToEsc);
// These functions escape strings for SQL statements and put them in quotes.
std::string EscQuoteSqlString(
    DatabaseHelper databaseHelper, std::string_view strToEsc);
// These functions convert table and column names to be safe for SQL
// statements. This involves converting them to lower case and putting
// them in quotes if they contain special characters.
std::string EscSqlTableName(
    DatabaseHelper databaseHelper, std::string_view tableName);
std::string EscSqlColumnName(
    DatabaseHelper databaseHelper, std::string_view columnName);
