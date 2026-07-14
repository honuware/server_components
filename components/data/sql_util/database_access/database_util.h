#pragma once

#include "database_helper.h"
#include "sql_util/database_access/database_metadata.h"

namespace DbUtil {

    // Generate select statement from table name and DatabaseColumnInfoArray
    std::string GenerateSelectStatementFromTableAndInfo(
        std::string_view tableName,
        const DbMeta::DatabaseColumnInfoArray& databaseColumnInfoArray);

    // Deletes the given database
    void ClearDatabase(
        Transaction& transactionNoDatabase, std::string_view databaseName);

    // Creates the given database
    void MakeDatabase(
        Transaction& transactionNoDatabase, std::string_view databaseName);

    // Deletes the given database if it exists and then recreates it
    void RemoveAndCreateDatabase(
        Transaction& transactionNoDatabase, 
        std::string_view databaseName);

    // Appends the filePath to basePath, loads the SQL text from the file,
    // and executes it
    void RunSqlFile(
        Transaction& transaction,
        std::string_view basePath,
        std::string_view filePath);

}  // namespace DbUtil