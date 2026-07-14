#pragma once

#include "transaction.h"
#include "sql_util/schema/database_info.h"

namespace DbMeta {

    // List all the databases
    StringArray ListDatabases(Transaction& transaction);

    // List all the non system tables
    StringArray ListTables(Transaction& transaction);

    // List all system views
    StringArray ListViews(Transaction& transaction);

    struct DatabaseColumnInfo {
        std::string columnName;
        int ordinalPosition;
        std::string dataType;
        int numericPrecision;
        std::string udtName;
        bool primaryKey;
        bool unique;

        bool operator==(const DatabaseColumnInfo& rhs) const;
    };
    using DatabaseColumnInfoArray = std::vector<DatabaseColumnInfo>;

    // Convert DatabaseColumnInfo to DatabaseTypes
    DatabaseTypes DatabaseTypesFromDatabaseColumnInfo(
        const DatabaseColumnInfo& databaseColumnInfo);

    // List all of the columns for a specific table
    DatabaseColumnInfoArray ListColumns(
        Transaction& transaction, std::string_view tableName);

    // Get primary key for table
    std::string GetPrimaryKey(
        Transaction& transaction, std::string_view tableName);

    // List all tables that reference this table via a foreign key
    StringArray ListChildTables(
        Transaction& transaction, std::string_view parentTableName);

    // List all parent tables referenced by the given child as a foriegn key
    StringArray ListParentTables(
        Transaction& transaction, std::string_view childTableName);

    // Returns list of root tables
    StringArray ListRootTables(Transaction& transaction);

    // Given a parent / child table pair, return the names of the
    // foreign key columns in the parent and child.
    void GetParentChildForeignKeyColumns(
        Transaction& transaction,
        std::string_view parentTableName, std::string_view childTableName,
        std::string& parentColumnName, std::string& childColumnName);

    // Returns true if the foreign key column in the child table is nullable
    bool IsForeignKeyNullable(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName);

    // Take an array of DatabaseColumnInfo and return a StringArray
    StringArray StringArrayFromDatabaseColumnInfoArray(
        const DatabaseColumnInfoArray& databaseColumnInfoArray);

    // Constructs DatabaseInfo from the metadata of the database
    DbSchema::DatabaseInfo DatabaseInfoFromDatabase(
        Transaction& transaction,
        std::string_view databaseName,
        const StringArray& allowedTables);

    // List all multi-column unique constraints for a table
    DbSchema::TableUniqueValuesArray ListTableUniqueValuesArrayForTable(
        Transaction& transaction,
        std::string_view tableName);

}  // namespace DbMeta