#pragma once

#include <stdexcept>

#include "sql_util/database_common.h"
#include "database_helper.h"

class DatabaseHelperBase {
public:
    virtual ~DatabaseHelperBase() = default;
    // Fetch the database connection object
    virtual pqxx::connection& GetConnection() = 0;
    // Replace a dropped connection with a fresh one. Only the production helper
    // can; the default refuses loudly rather than pretend a test or bootstrap
    // connection was renewed.
    virtual void ResetConnection() {
        throw std::logic_error("This DatabaseHelper cannot reset its connection");
    }
    // Get the database name
    virtual const std::string& GetDatabaseName() const = 0;
    // Run the provided code in the context of a transaction
    // in an exception block.
    virtual void RunInTransaction(
        std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) = 0;
    virtual bool IsTest() const = 0;
};
