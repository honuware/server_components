#include "sql_util/database_access/database_util.h"

#include <filesystem>
#include <boost/algorithm/string.hpp>

#include "util/file_util.h"

namespace DbUtil {

    std::string GenerateSelectStatementFromTableAndInfo(
        std::string_view tableName,
        const DbMeta::DatabaseColumnInfoArray& databaseColumnInfoArray) {
        std::string ret = "SELECT ";

        StringArray columnNames = StringArrayFromDatabaseColumnInfoArray(
            databaseColumnInfoArray);
        ret += StringArrayToCommaSeparatedString(columnNames);
        ret += " FROM ";
        ret += tableName;

        return ret;
    }

    void ClearDatabase(Transaction& transactionNoDatabase, std::string_view databaseName) {
        // Databases can't be dropped inside a transaction.
        std::string dropDatabase = "DROP DATABASE IF EXISTS " + std::string(databaseName);
        transactionNoDatabase.RunSqlStatement(dropDatabase);
    }

    void MakeDatabase(Transaction& transactionNoDatabase, std::string_view databaseName) {
        // Databases can't be created inside a transaction.
        std::string dropDatabase = "CREATE DATABASE " + std::string(databaseName);
        transactionNoDatabase.RunSqlStatement(dropDatabase);
    }

    void RemoveAndCreateDatabase(
        Transaction& transactionNoDatabase,
        std::string_view databaseName) {
        ClearDatabase(transactionNoDatabase, databaseName);
        MakeDatabase(transactionNoDatabase, databaseName);
    }

    void RunSqlFile(Transaction& transaction, std::string_view basePath, std::string_view filePath) {
        std::filesystem::path pathToFile(basePath);
        pathToFile += filePath;
        std::string sql = ReadFile(pathToFile.string());
        std::string transactionDescription = "Running SQL: " + pathToFile.string();
        transaction.RunSqlStatement(sql);
    }

}  // namespace DbUtil