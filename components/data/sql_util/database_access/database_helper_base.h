#pragma once

#include "sql_util/database_common.h"
#include "database_helper.h"

class DatabaseHelperBase {
public:
    virtual ~DatabaseHelperBase() = default;
    // Fetch the database connection object
    virtual pqxx::connection& GetConnection() = 0;
    // Get the database name
    virtual const std::string& GetDatabaseName() const = 0;
    // Run the provided code in the context of a transaction
    // in an exception block.
    virtual void RunInTransaction(
        std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) = 0;
    virtual bool IsTest() const = 0;
};
