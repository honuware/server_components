#include "sql_util/database_access/database_metadata.h"

#include <list>
#include <boost/algorithm/string.hpp>

namespace DbMeta {

    bool DatabaseColumnInfo::operator==(const DatabaseColumnInfo& rhs) const {
        return columnName == rhs.columnName
            && ordinalPosition == rhs.ordinalPosition
            && dataType == rhs.dataType
            && numericPrecision == rhs.numericPrecision
            && udtName == rhs.udtName && primaryKey == rhs.primaryKey
            && unique == rhs.unique;
    }

    StringArray ListDatabases(Transaction& transaction) {
        KeyValueTableArray
            databasesKeyValueTableArray =
                transaction.RunSqlStatementReturningKeyValueTableArray(
                    "SELECT datname as name from pg_database");
        return StringArrayFromKeyValueTableArrayColumn(databasesKeyValueTableArray);
    }

    StringArray ListTables(Transaction& transaction) {
        KeyValueTableArray
            tablesKeyValueTableArray =
                transaction.RunSqlStatementReturningKeyValueTableArray(
                    "select tablename as name from pg_tables "
                    "where schemaname != 'pg_catalog'");
        return StringArrayFromKeyValueTableArrayColumn(tablesKeyValueTableArray);
    }

    StringArray ListViews(Transaction& transaction) {
        KeyValueTableArray
            viewsKeyValueTableArray =
            transaction.RunSqlStatementReturningKeyValueTableArray(
                "select viewname as name from pg_views where schemaname != 'pg_catalog'");
        return StringArrayFromKeyValueTableArrayColumn(viewsKeyValueTableArray);
    }

    DatabaseTypes DatabaseTypesFromDatabaseColumnInfo(
        const DatabaseColumnInfo& databaseColumnInfo) {
        // Primary lookup keyed on `data_type`. For built-in Postgres types
        // this is what `format_type(atttypid, atttypmod)` returns (e.g.
        // "integer", "bigint"). information_schema reports user-defined types
        // — like the citext extension — with `data_type = 'USER-DEFINED'`
        // and the actual type name in `udt_name`.
        static const std::map<std::string, DatabaseTypes> kPrimaryLookup = {
            {"BOOL", DB_TYPE_BOOL},
            {"BOOLEAN", DB_TYPE_BOOL},
            {"BYTEA", DB_TYPE_BYTES},
            {"CHARACTER VARYING", DB_TYPE_STRING},
            {"VARCHAR", DB_TYPE_STRING},
            {"DATE", DB_TYPE_DATE},
            {"DOUBLE PRECISION", DB_TYPE_DOUBLE},
            {"FLOAT8", DB_TYPE_DOUBLE},
            {"INTEGER", DB_TYPE_INT},
            {"INT", DB_TYPE_INT},
            {"INT4", DB_TYPE_INT},
            {"JSON", DB_TYPE_JSON},
            {"SERIAL", DB_TYPE_SERIAL},
            {"SERIAL4", DB_TYPE_SERIAL},
            {"TEXT", DB_TYPE_STRING},
            {"TIME WITHOUT TIME ZONE", DB_TYPE_TIME},
            {"TIME WITH TIME ZONE", DB_TYPE_TIME},
            {"TIMTZ", DB_TYPE_TIME},
            {"UUID", DB_TYPE_UUID},
            {"NULL", DB_TYPE_NULL},
            {"TIMESTAMP WITHOUT TIME ZONE", DB_TYPE_TIMESTAMP},
            {"TIMESTAMP WITH TIME ZONE", DB_TYPE_TIMESTAMPTZ},
            {"TIMESTAMPTZ", DB_TYPE_TIMESTAMPTZ},
            {"BIGINT", DB_TYPE_BIGINT},
            {"INT8", DB_TYPE_BIGINT},
            {"BIGSERIAL", DB_TYPE_BIGSERIAL},
            {"SERIAL8", DB_TYPE_BIGSERIAL},
            // Some Postgres versions / pg_attribute paths emit the raw
            // user-defined type name in data_type rather than
            // 'USER-DEFINED'. List them here too so both paths resolve.
            {"CITEXT", DB_TYPE_CITEXT}
        };

        // Fallback lookup keyed on `udt_name`, used when `data_type` is the
        // generic `USER-DEFINED` marker. Adding the next user-defined type
        // (hstore, ltree, custom enums, …) means one new line here.
        static const std::map<std::string, DatabaseTypes> kUserDefinedLookup = {
            {"CITEXT", DB_TYPE_CITEXT}
        };

        std::string typeString = databaseColumnInfo.dataType;
        boost::algorithm::to_upper(typeString);

        auto iter = kPrimaryLookup.find(typeString);
        if (iter != kPrimaryLookup.end()) {
            return iter->second;
        }

        if (typeString == "USER-DEFINED") {
            std::string udtName = databaseColumnInfo.udtName;
            boost::algorithm::to_upper(udtName);
            auto udtIter = kUserDefinedLookup.find(udtName);
            if (udtIter != kUserDefinedLookup.end()) {
                return udtIter->second;
            }
            std::stringstream ss;
            ss << "DatabaseTypesFromDatabaseColumnInfo - unsupported "
                << "user-defined type: " << databaseColumnInfo.udtName;
            throw std::invalid_argument(ss.str());
        }

        std::stringstream ss;
        ss << "DatabaseTypesFromDatabaseColumnInfo - invalid database type: "
            << databaseColumnInfo.dataType;
        throw std::invalid_argument(ss.str());
    }

    constexpr std::string_view kListColumnsSql = R"SQL(
SELECT c.column_name, c.ordinal_position, attr.data_type,
    CASE WHEN c.numeric_precision IS NULL THEN 0
        ELSE c.numeric_precision END AS numeric_precision,
    c.udt_name, (pk.constraint_type IS NOT NULL AND pk.constraint_type = 'PRIMARY KEY') AS primary_key,
    (pk.constraint_type IS NOT NULL AND pk.constraint_type = 'UNIQUE') AS is_unique
FROM information_schema.columns AS c
  LEFT JOIN (
    SELECT kcu.table_name AS table_name, kcu.column_name AS column_name,
        kcu.table_schema AS table_schema, tc.constraint_type AS constraint_type
    FROM information_schema.key_column_usage AS kcu
      JOIN information_schema.table_constraints AS tc
        ON kcu.table_name = tc.table_name
          AND kcu.table_schema = tc.table_schema
          AND kcu.constraint_name = tc.constraint_name
          AND (tc.constraint_type = 'PRIMARY KEY'
               OR (tc.constraint_type = 'UNIQUE'
                   AND (SELECT COUNT(*) FROM information_schema.key_column_usage kcu2
                        WHERE kcu2.constraint_name = tc.constraint_name
                        AND kcu2.table_schema = tc.table_schema) = 1))
  ) AS pk
    ON c.table_name = pk.table_name AND c.table_schema = pk.table_schema
        AND c.column_name = pk.column_name
  LEFT JOIN (
    SELECT a.attrelid::regclass::text AS table_name, a.attname AS column_name
         , CASE WHEN a.atttypid = ANY ('{int,int8,int2}'::regtype[])
              AND EXISTS (
                 SELECT FROM pg_attrdef ad
                 WHERE  ad.adrelid = a.attrelid
                 AND    ad.adnum   = a.attnum
                 AND    pg_get_expr(ad.adbin, ad.adrelid)
                      = 'nextval('''
                     || (pg_get_serial_sequence (a.attrelid::regclass::text
                                              , a.attname))::regclass
                     || '''::regclass)'
                 )
            THEN CASE a.atttypid
                    WHEN 'int'::regtype  THEN 'serial'
                    WHEN 'int8'::regtype THEN 'bigserial'
                    WHEN 'int2'::regtype THEN 'smallserial'
                 END
            ELSE format_type(a.atttypid, a.atttypmod)
            END AS data_type
    FROM   pg_attribute  a
    WHERE  a.attrelid = ('' || $1)::regclass
    AND    a.attnum > 0
    AND    NOT a.attisdropped
    ORDER  BY a.attnum
  ) AS attr
    ON c.table_name = attr.table_name AND c.column_name = attr.column_name
WHERE c.table_name = $1
ORDER BY ordinal_position
)SQL";
// Note that the last nested select is because SERIAL isn't really a type
// so it gets converted to INT4. This chunk of code looks up a referenced
// sequence and converts it back to SERIAL. More details at:
// https://dba.stackexchange.com/questions/90555/postgresql-select-primary-key-as-serial-or-bigserial/90567#90567

    DatabaseColumnInfoArray ListColumns(
        Transaction& transaction, std::string_view tableName) {
        DatabaseColumnInfoArray databaseColumnInfoArray;
        KeyValueTableArray keyValueTableArray = transaction.RunSqlStatementReturningKeyValueTableArray(
            kListColumnsSql, tableName);
        for (const auto& keyValueTable : keyValueTableArray) {
            databaseColumnInfoArray.push_back({
                keyValueTable.at("column_name"),
                std::stoi(keyValueTable.at("ordinal_position")),
                keyValueTable.at("data_type"),
                std::stoi(keyValueTable.at("numeric_precision")),
                keyValueTable.at("udt_name"),
                StringToBool(keyValueTable.at("primary_key")),
                StringToBool(keyValueTable.at("is_unique"))
                });
        }
        return databaseColumnInfoArray;
    }

    std::string GetPrimaryKey(
        Transaction& transaction, std::string_view tableName) {
        std::string ret;
        DatabaseColumnInfoArray databaseColumnInfoArray
            = ListColumns(transaction, tableName);
        for (const auto& databaseColumnInfo : databaseColumnInfoArray) {
            if (databaseColumnInfo.primaryKey) {
                ret = databaseColumnInfo.columnName;
                break;
            }
        }
        return ret;
    }

    constexpr std::string_view kListChildTablesSql = R"SQL(
SELECT kcu.table_name AS child_table_name
FROM information_schema.key_column_usage AS kcu
  JOIN information_schema.table_constraints AS tc
    ON kcu.constraint_schema = tc.constraint_schema
      AND kcu.constraint_name = tc.constraint_name
    JOIN information_schema.constraint_column_usage AS ccu
      ON tc.constraint_schema = ccu.constraint_schema
        AND tc.constraint_name = ccu.constraint_name
WHERE tc.constraint_type = 'FOREIGN KEY' AND ccu.table_name = $1
)SQL";

    StringArray ListChildTables(
        Transaction& transaction, std::string_view parentTableName) {
        KeyValueTableArray keyValueTableArray = 
            transaction.RunSqlStatementReturningKeyValueTableArray(
                kListChildTablesSql, parentTableName);
        return StringArrayFromKeyValueTableArrayColumn(keyValueTableArray);
    }

    // Note that table_constraints has which constraints are of type FOREIGN KEY.
    // key_column_usage has the columns and the constraint name that can link
    // back to table_constraints. key_column_usage is the child table.
    // table_constraints has the parent table.
    constexpr std::string_view kListParentTablesSql = R"SQL(
SELECT ccu.table_name AS parent_table_name
FROM information_schema.key_column_usage AS kcu
  JOIN information_schema.table_constraints AS tc
    ON kcu.constraint_schema = tc.constraint_schema
      AND kcu.constraint_name = tc.constraint_name
    JOIN information_schema.constraint_column_usage AS ccu
      ON tc.constraint_schema = ccu.constraint_schema
        AND tc.constraint_name = ccu.constraint_name
WHERE tc.constraint_type = 'FOREIGN KEY' AND kcu.table_name = $1
)SQL";

    StringArray ListParentTables(
        Transaction& transaction, std::string_view childTableName) {
        KeyValueTableArray keyValueTableArray =
            transaction.RunSqlStatementReturningKeyValueTableArray(
                kListParentTablesSql, childTableName);
        return StringArrayFromKeyValueTableArrayColumn(keyValueTableArray);
    }

    // Every child table (the referencing side of any FOREIGN KEY) in one query.
    // A root table is simply one that never appears here. This lets
    // ListRootTables collect all children with a SINGLE information_schema join
    // instead of one per table. Those views are expensive (unindexed catalog
    // scans), so on a fully-populated schema (hundreds of tables) the previous
    // per-table loop cost seconds; this is two queries total regardless of size.
    constexpr std::string_view kListAllChildTablesSql = R"SQL(
SELECT DISTINCT kcu.table_name AS child_table_name
FROM information_schema.key_column_usage AS kcu
  JOIN information_schema.table_constraints AS tc
    ON kcu.constraint_schema = tc.constraint_schema
      AND kcu.constraint_name = tc.constraint_name
WHERE tc.constraint_type = 'FOREIGN KEY'
)SQL";

    StringArray ListRootTables(Transaction& transaction) {
        StringArray rootTables;
        StringSet childTables;
        // One query for the complete set of child tables...
        AddStringArrayToSet(
            childTables,
            StringArrayFromKeyValueTableArrayColumn(
                transaction.RunSqlStatementReturningKeyValueTableArray(
                    kListAllChildTablesSql)));
        // ...then every table that is not a child is a root.
        for (const std::string& table : ListTables(transaction)) {
            if (!SetContains(childTables, table)) {
                rootTables.push_back(table);
            }
        }
        return rootTables;
    }

    constexpr std::string_view kGetParentChildForeignKeyColumnsSql = R"SQL(
SELECT ccu.column_name AS parent_column_name, kcu.column_name AS child_column_name
FROM information_schema.key_column_usage AS kcu
  JOIN information_schema.table_constraints AS tc
    ON kcu.constraint_schema = tc.constraint_schema
      AND kcu.constraint_name = tc.constraint_name
    JOIN information_schema.constraint_column_usage AS ccu
      ON tc.constraint_schema = ccu.constraint_schema
        AND tc.constraint_name = ccu.constraint_name
WHERE tc.constraint_type = 'FOREIGN KEY'
  AND ccu.table_name = $1
  AND kcu.table_name = $2
)SQL";

    void GetParentChildForeignKeyColumns(
        Transaction& transaction,
        std::string_view parentTableName, std::string_view childTableName,
        std::string& parentColumnName, std::string& childColumnName) {
        KeyValueTableArray keyValueTableArray =
            transaction.RunSqlStatementReturningKeyValueTableArray(
                kGetParentChildForeignKeyColumnsSql, parentTableName, childTableName);
        if (keyValueTableArray.empty()) {
            throw std::runtime_error(
                "GetParentChildForeignKeyColumns - no foreign key relationship found between "
                "parent table " + std::string(parentTableName) +
                " and child table " + std::string(childTableName));
        }
        parentColumnName = keyValueTableArray[0].at("parent_column_name");
        childColumnName = keyValueTableArray[0].at("child_column_name");
    }

    StringArray StringArrayFromDatabaseColumnInfoArray(
        const DatabaseColumnInfoArray& databaseColumnInfoArray) {
        StringArray ret;

        for (const auto& databaseColumnInfo : databaseColumnInfoArray) {
            ret.push_back(databaseColumnInfo.columnName);
        }

        return ret;
    }

    constexpr std::string_view kIsForeignKeyNullableSql = R"SQL(
SELECT is_nullable
FROM information_schema.columns
WHERE table_name = $1 AND column_name = $2 AND table_schema = 'public'
)SQL";

    bool IsForeignKeyNullable(
        Transaction& transaction,
        std::string_view tableName,
        std::string_view columnName) {
        KeyValueTableArray keyValueTableArray =
            transaction.RunSqlStatementReturningKeyValueTableArray(
                kIsForeignKeyNullableSql, tableName, columnName);
        if (keyValueTableArray.empty()) {
            std::stringstream ss;
            ss << "IsForeignKeyNullable - column not found: "
                << tableName << "." << columnName;
            throw std::invalid_argument(ss.str());
        }
        return keyValueTableArray[0].at("is_nullable") == "YES";
    }

    constexpr std::string_view kListTableUniqueConstraintsSql = R"SQL(
SELECT
    tc.constraint_name,
    kcu.column_name,
    kcu.ordinal_position
FROM information_schema.table_constraints AS tc
JOIN information_schema.key_column_usage AS kcu
    ON tc.constraint_name = kcu.constraint_name
    AND tc.table_schema = kcu.table_schema
    AND tc.table_name = kcu.table_name
WHERE tc.constraint_type = 'UNIQUE'
    AND tc.table_name = $1
    AND tc.table_schema = 'public'
ORDER BY tc.constraint_name, kcu.ordinal_position
)SQL";

    DbSchema::TableUniqueValuesArray ListTableUniqueValuesArrayForTable(
        Transaction& transaction,
        std::string_view tableName) {
        DbSchema::TableUniqueValuesArray result;

        KeyValueTableArray keyValueTableArray =
            transaction.RunSqlStatementReturningKeyValueTableArray(
                kListTableUniqueConstraintsSql, tableName);

        // Group by constraint_name
        std::map<std::string, StringArray> constraintColumns;
        for (const auto& row : keyValueTableArray) {
            std::string constraintName = row.at("constraint_name");
            std::string columnName = row.at("column_name");
            constraintColumns[constraintName].push_back(columnName);
        }

        // Filter out single-column constraints (those are handled by ColumnInfo.unique_)
        for (const auto& [name, columns] : constraintColumns) {
            if (columns.size() > 1) {
                DbSchema::TableUniqueValues tableUniqueValues;
                tableUniqueValues.name = name;
                tableUniqueValues.columns = columns;
                result.push_back(tableUniqueValues);
            }
        }

        return result;
    }

    namespace {

        struct TableInfo {
            std::string tableName;
            StringArray parents;
        };
        using TableInfoList = std::list<TableInfo>;

        struct ForeignKeyRef {
            std::string parentTable;
            std::string parentColumn;
        };
        using ForeignKeyTable = std::map<std::string, ForeignKeyRef>;

        ForeignKeyTable MakeForeignKeyTable(
            Transaction& transaction,
            const TableInfo& tableInfo) {
            ForeignKeyTable foreignKeyTable;
            for (const std::string& parentTable : tableInfo.parents) {
                std::string childColumn, parentColumn;
                DbMeta::GetParentChildForeignKeyColumns(
                    transaction,
                    parentTable, 
                    tableInfo.tableName, 
                    parentColumn, 
                    childColumn);
                foreignKeyTable.insert(
                    std::make_pair(
                        childColumn, 
                        ForeignKeyRef{ parentTable, parentColumn }));
            }
            return foreignKeyTable;
        }

        void AddTableToDatabaseInfo(
            Transaction& transaction,
            const TableInfo& tableInfo,
            DbSchema::DatabaseInfo& databaseInfo,
            StringSet& tablesAdded) {
            DatabaseColumnInfoArray databaseColumnInfoArray = ListColumns(
                transaction, tableInfo.tableName);
            databaseInfo.AddTable(tableInfo.tableName);
            tablesAdded.insert(tableInfo.tableName);

            ForeignKeyTable foreignKeyTable = MakeForeignKeyTable(
                transaction, tableInfo);

            for (const DatabaseColumnInfo& databaseColumnInfo
                : databaseColumnInfoArray) {
                auto iter = foreignKeyTable.find(
                    databaseColumnInfo.columnName);
                if (iter != foreignKeyTable.end()) {
                    bool isNullable = IsForeignKeyNullable(
                        transaction,
                        tableInfo.tableName,
                        databaseColumnInfo.columnName);
                    if (isNullable) {
                        databaseInfo.AddColumnForeignKeyRefNullable(
                            iter->second.parentTable,
                            iter->second.parentColumn,
                            tableInfo.tableName, // Child table name
                            iter->first); // Child column
                    } else {
                        databaseInfo.AddColumnForeignKeyRef(
                            iter->second.parentTable,
                            iter->second.parentColumn,
                            tableInfo.tableName, // Child table name
                            iter->first); // Child column
                    }
                }
                else if (databaseColumnInfo.primaryKey) {
                    databaseInfo.AddColumnPrimaryKey(
                        tableInfo.tableName, 
                        databaseColumnInfo.columnName, 
                        DatabaseTypesFromDatabaseColumnInfo(
                            databaseColumnInfo));
                }
                else if (databaseColumnInfo.unique) {
                    databaseInfo.AddColumnUnique(
                        tableInfo.tableName, 
                        databaseColumnInfo.columnName, 
                        DatabaseTypesFromDatabaseColumnInfo(
                            databaseColumnInfo));
                }
                else {
                    databaseInfo.AddColumnSimple(
                        tableInfo.tableName,
                        databaseColumnInfo.columnName,
                        DatabaseTypesFromDatabaseColumnInfo(
                            databaseColumnInfo));
                }
            }

            // Add multi-column unique constraints
            DbSchema::TableUniqueValuesArray tableUniqueValuesArray =
                ListTableUniqueValuesArrayForTable(transaction, tableInfo.tableName);
            for (const auto& tableUniqueValues : tableUniqueValuesArray) {
                databaseInfo.AddUniqueConstraintHelper(
                    tableInfo.tableName, tableUniqueValues);
            }
        }

        StringArray FilterBasedOnAllowedTables(
            const StringArray& tables, const StringArray& allowedTables) {
            StringArray ret;
            StringSet stringSet = StringSetFromStringArray(allowedTables);
            for (const std::string& table : tables) {
                if (SetContains(stringSet, table)) {
                    ret.push_back(table);
                }
            }
            return ret;
        }

        TableInfoList BuildChildTableInfoList(
            Transaction& transaction,
            const StringSet& rootTables,
            const StringArray& allowedTables) {
            TableInfoList tableInfoList;

            StringArray allTables = FilterBasedOnAllowedTables(
                ListTables(transaction), allowedTables);
            for (const std::string& table : allTables) {
                if (!SetContains(rootTables, table)) {
                    tableInfoList.push_back({ 
                        table, 
                        ListParentTables(transaction, table) });
                }
            }

            return tableInfoList;
        }

        bool AreAllParentsAdded(
            const StringSet& tablesAdded,
            const TableInfo& tableInfo) {
            for (const std::string& parent : tableInfo.parents) {
                if (!SetContains(tablesAdded, parent)) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    DbSchema::DatabaseInfo DatabaseInfoFromDatabase(
        Transaction& transaction,
        std::string_view databaseName,
        const StringArray& allowedTables) {
        DbSchema::DatabaseInfo databaseInfo(databaseName);
        StringSet tablesAdded;

        // Add all the root tables to start
        for (const std::string& rootTable : 
            FilterBasedOnAllowedTables(
                ListRootTables(transaction), allowedTables)) {
            AddTableToDatabaseInfo(
                transaction, TableInfo{ rootTable, {} }, databaseInfo, tablesAdded);
        }

        // Build child info list
        TableInfoList tableInfoList = BuildChildTableInfoList(
            transaction, tablesAdded, allowedTables);

        // Add things as their parent dependencies are handled
        while (!tableInfoList.empty()) {
            for (auto iter = tableInfoList.begin();
                iter != tableInfoList.end();
                ++iter) {
                if (AreAllParentsAdded(tablesAdded, *iter)) {
                    AddTableToDatabaseInfo(
                        transaction,
                        *iter, 
                        databaseInfo, 
                        tablesAdded);
                    tableInfoList.erase(iter);
                    break;
                }
            }
        }

        return databaseInfo;
    }

}  // namespace DbMeta