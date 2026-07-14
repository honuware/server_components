#include "sql_util/database_access/database_crud_helpers.h"

#include "database_crud_helpers_priv.h"
#include "sql_util/database_access/database_util.h"
#include "sql_util/database_access/database_metadata.h"

namespace {

    using DatabaseColumnInfoTable =
        std::map<std::string, DbMeta::DatabaseColumnInfo>;

    std::string GenerateInsertParams(
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords,
        ExecParamsHelper& execParamsHelper) {
        StringArray params;
        for (const auto& [key, value] : table) {
            if(SetContains(allowedSqlKeywords, value)) {
                params.push_back(value);
            }
            else {
                params.push_back(execParamsHelper.AddParam(value));
            }
        }
        return StringArrayToCommaSeparatedString(params);
    }

    std::string GenerateInsertColumnNames(
        DatabaseHelper databaseHelper,
        const DatabaseColumnInfoTable& databaseColumnInfoTable,
        const KeyValueTable& table) {
        StringArray columns;
        for (const auto& [key, value] : table) {

            const DbMeta::DatabaseColumnInfo& databaseColumnInfo =
                databaseColumnInfoTable.find(key)->second;
            columns.push_back(EscSqlColumnName(
                databaseHelper, databaseColumnInfo.columnName));
        }
        return StringArrayToCommaSeparatedString(columns);
    }

    DatabaseColumnInfoTable GenerateDatabaseColumnInfoTable(
        const DbMeta::DatabaseColumnInfoArray& databaseColumnInfoArray) {
        DatabaseColumnInfoTable databaseColumnInfoTable;
        for (const DbMeta::DatabaseColumnInfo& databaseColumnInfo
            : databaseColumnInfoArray) {
            databaseColumnInfoTable[databaseColumnInfo.columnName]
                = databaseColumnInfo;
        }
        return databaseColumnInfoTable;
    }

    void CheckKeysInTable(
        const KeyValueTable& table,
        const DatabaseColumnInfoTable& databaseColumnInfoTable) {
        for (const auto& [key, value] : table) {
            if (databaseColumnInfoTable.find(key)
                == databaseColumnInfoTable.end()) {
                std::stringstream ss;
                ss << "CheckKeysInTable key(" << key
                    << ") not found in table.";
                throw std::invalid_argument(ss.str());
            }
        }
    }

    DbMeta::DatabaseColumnInfo LookupDatabaseColumnInfoByColumnName(
        const DbMeta::DatabaseColumnInfoArray& databaseColumnInfoArray,
        std::string_view columnName) {
        DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo;
        for (const DbMeta::DatabaseColumnInfo& databaseColumnInfo
            : databaseColumnInfoArray) {
            if (columnName == databaseColumnInfo.columnName) {
                lookupValueDatabaseColumnInfo = databaseColumnInfo;
                break;
            }
        }

        if (lookupValueDatabaseColumnInfo.columnName.empty()) {
            throw std::invalid_argument(
                std::string(
                    "LookupDatabaseColumnInfoByColumnName"
                    " - column name not found: ")
                + std::string(columnName));
        }
        return lookupValueDatabaseColumnInfo;
    }

    StringArray GetColumnNames(
        DatabaseHelper databaseHelper,
        const DbMeta::DatabaseColumnInfoArray& databaseColumnInfoArray) {
        StringArray columnNames;
        for (const DbMeta::DatabaseColumnInfo& databaseColumnInfo
            : databaseColumnInfoArray) {
            columnNames.push_back(
                EscSqlColumnName(
                    databaseHelper, databaseColumnInfo.columnName));
        }
        return columnNames;
    }

    std::string WhereClauseFromKeyValueTable(
        DatabaseHelper databaseHelper,
        const KeyValueTable& lookupValues,
        ExecParamsHelper& execParamsHelper) {
        std::string whereClause;
        bool first = true;
        for (const auto& [key, value] : lookupValues) {
            if (!first) {
                whereClause += " AND ";
            }
            else {
                first = false;
            }
            whereClause += EscSqlColumnName(databaseHelper, key);
            whereClause += " = ";
            whereClause += execParamsHelper.AddParam(value);
        }
        return whereClause;
    }

    void ExtractTotalCount(KeyValueTableArray& results, int64_t* totalCount) {
        if (!results.empty()) {
            if (totalCount != nullptr) {
                *totalCount = std::stoll(results[0]["_total_count"]);
            }
            for (auto& row : results) {
                row.erase("_total_count");
            }
        } else if (totalCount != nullptr) {
            *totalCount = 0;
        }
    }

} // namespace

namespace DbCrud {

    namespace PrivateSql {

        std::string GenerateAddRowToTableSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& table,
            const StringSet& allowedSqlKeywords,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            DatabaseColumnInfoTable databaseColumnInfoTable
                = GenerateDatabaseColumnInfoTable(databaseColumnInfoArray);
            CheckKeysInTable(table, databaseColumnInfoTable);

            // Generate code like:
            //  INSERT INTO orders(parent_person_id, item, amount)
            //      VALUES (person_id_arg, item_arg, amount_arg)

            std::string sql = "INSERT INTO ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " (";
            sql += GenerateInsertColumnNames(
                databaseHelper, databaseColumnInfoTable, table);
            sql += ") VALUES (";
            sql += GenerateInsertParams(
                table, allowedSqlKeywords, execParamsHelper);
            sql += ")";

            return sql;
        }

        std::string GenerateAddRowToTableFetchIntPrimaryKeySql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& table,
            const StringSet& allowedSqlKeywords,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray = 
                DbMeta::ListColumns(transaction, tableName);
            DatabaseColumnInfoTable databaseColumnInfoTable
                = GenerateDatabaseColumnInfoTable(
                    databaseColumnInfoArray);
            CheckKeysInTable(table, databaseColumnInfoTable);

            // Generate code like:
            //  INSERT INTO orders(parent_person_id, item, amount)
            //      VALUES(person_id_arg, item_arg, amount_arg)
            //      RETURNING order_id INTO id_arg;

            std::string sql = "INSERT INTO ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " (";
            sql += GenerateInsertColumnNames(
                databaseHelper, databaseColumnInfoTable, table);
            sql += ") VALUES (";
            sql += GenerateInsertParams(
                table, allowedSqlKeywords, execParamsHelper);
            sql += ") RETURNING ";
            sql += DbMeta::GetPrimaryKey(transaction, tableName);

            return sql;
        }

        std::string GenerateLookupRowByValueSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view lookupValue) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray = 
                DbMeta::ListColumns(transaction, tableName);
            DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo
                = LookupDatabaseColumnInfoByColumnName(
                    databaseColumnInfoArray, columnName);
            StringArray columnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            // Generate code like:
            // SELECT person_id, parent_person_id, item, amount
            // FROM people
            // WHERE person_id = 1

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(columnNames);
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " WHERE ";
            sql += EscSqlColumnName(databaseHelper, columnName);
            sql += " = $1";

            return sql;
        }

        std::string GenerateLookupRowByValuesSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            KeyValueTable lookupValues,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            StringArray columnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            // Generate code like:
            // SELECT person_id, parent_person_id, item, amount
            // FROM people
            // WHERE person_id = 1

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(columnNames);
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " WHERE ";
            sql += WhereClauseFromKeyValueTable(
                databaseHelper, lookupValues, execParamsHelper);

            return sql;
        }

        std::string GenerateGetRowsByColumnSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            bool asc,
            int pageSize,
            int page) {
            StringArray saColumnNames;
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo
                = LookupDatabaseColumnInfoByColumnName(
                    databaseColumnInfoArray, columnName);
            saColumnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            // Generate code like:
            // SELECT person_id, parent_person_id, item, amount
            // FROM people
            // ORDER BY person_id ASC
            // LIMIT {pageSize} OFFSET {page * page}

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(saColumnNames);
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " ORDER BY ";
            sql += EscSqlColumnName(databaseHelper, columnName);
            sql += asc ? " ASC" : " DESC";
            sql += " LIMIT $1 OFFSET $2";

            return sql;
        }

        std::string GenerateGetRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view columnValue,
            StringArray& saColumnNames) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo
                = LookupDatabaseColumnInfoByColumnName(
                    databaseColumnInfoArray, columnName);
            saColumnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            // Generate code like:
            // SELECT person_id, parent_person_id, item, amount
            // FROM people
            // WHERE person_id = person_id_arg

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(saColumnNames);
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " WHERE ";
            sql += EscSqlColumnName(databaseHelper, columnName);
            sql += " = $1";

            return sql;
        }

        std::string GenerateUpdateRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view columnValue,
            const KeyValueTable& table,
            const StringSet& allowedSqlKeywords,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            DatabaseColumnInfoTable databaseColumnInfoTable
                = GenerateDatabaseColumnInfoTable(
                    databaseColumnInfoArray);
            CheckKeysInTable(table, databaseColumnInfoTable);
            DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo
                = LookupDatabaseColumnInfoByColumnName(
                    databaseColumnInfoArray, columnName);
            StringArray columnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);
            std::string param = execParamsHelper.AddParam(columnValue);

            // Generate code like:
            // UPDATE people SET (parent_person_id, item, amount)
            // = (person_id_arg, item_arg, amount_arg)
            // WHERE person_id = person_id_arg

            std::string sql = "UPDATE ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " SET (";
            sql += GenerateInsertColumnNames(
                databaseHelper, databaseColumnInfoTable, table);
            sql += ") = ROW(";
            sql += GenerateInsertParams(
                table, allowedSqlKeywords, execParamsHelper);
            sql += ") WHERE ";
            sql += EscSqlColumnName(databaseHelper, columnName);
            sql += " = ";
            sql += param;

            return sql;
        }

        std::string GenerateDeleteRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view columnName,
            std::string_view columnValue) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo
                = LookupDatabaseColumnInfoByColumnName(
                    databaseColumnInfoArray, columnName);

            // Generate code like:
            // DELETE FROM people WHERE person_id = id_arg

            std::string sql = "DELETE FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " WHERE ";
            sql += EscSqlColumnName(databaseHelper, columnName);
            sql += " = $1";

            return sql;
        }

        std::string GenerateDeleteRowSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& keyValueTable,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);

            // Generate code like:
            // DELETE FROM people WHERE person_id = id_arg

            std::string sql = "DELETE FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " WHERE";
            bool first = true;
            for (const auto& [columnName, value] : keyValueTable) {
                DbMeta::DatabaseColumnInfo lookupValueDatabaseColumnInfo
                    = LookupDatabaseColumnInfoByColumnName(
                        databaseColumnInfoArray, columnName);
                std::string param = execParamsHelper.AddParam(value);
                if (first) {
                    first = false;
                }
                else {
                    sql += " AND";
                }
                sql += " ";
                sql += EscSqlColumnName(databaseHelper, columnName);
                sql += " = ";
                sql += param;
            }

            return sql;
        }

        std::string GenerateGetRowsByValuesWithOrderBySql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            const KeyValueTable& lookupValues,
            std::string_view orderByColumn,
            bool asc,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            LookupDatabaseColumnInfoByColumnName(
                databaseColumnInfoArray, orderByColumn);
            StringArray saColumnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            if (lookupValues.empty()) {
                throw std::invalid_argument(
                    "GetRowsByValuesWithOrderBy - lookupValues must not be empty.");
            }

            DatabaseColumnInfoTable databaseColumnInfoTable
                = GenerateDatabaseColumnInfoTable(databaseColumnInfoArray);
            CheckKeysInTable(lookupValues, databaseColumnInfoTable);

            // Generate code like:
            // SELECT col1, col2, ...
            // FROM tableName
            // WHERE col1 = $1 AND col2 = $2
            // ORDER BY sortCol ASC

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(saColumnNames);
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);
            sql += " WHERE ";
            sql += WhereClauseFromKeyValueTable(
                databaseHelper, lookupValues, execParamsHelper);
            sql += " ORDER BY ";
            sql += EscSqlColumnName(databaseHelper, orderByColumn);
            sql += asc ? " ASC" : " DESC";

            return sql;
        }

        std::string GenerateGetRowsByValuesSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view sortColumnName,
            const KeyValueTable& lookupValues,
            bool asc,
            int pageSize,
            int page,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            LookupDatabaseColumnInfoByColumnName(
                databaseColumnInfoArray, sortColumnName);
            StringArray saColumnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            // Generate code like:
            // SELECT col1, col2, ..., COUNT(*) OVER() AS _total_count
            // FROM people
            // [WHERE person_id = $1 AND ...]
            // ORDER BY first_name ASC
            // LIMIT NULLIF($N, 0) OFFSET $M

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(saColumnNames);
            sql += ", COUNT(*) OVER() AS _total_count";
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);

            if (!lookupValues.empty()) {
                DatabaseColumnInfoTable databaseColumnInfoTable
                    = GenerateDatabaseColumnInfoTable(databaseColumnInfoArray);
                CheckKeysInTable(lookupValues, databaseColumnInfoTable);

                sql += " WHERE ";
                sql += WhereClauseFromKeyValueTable(
                    databaseHelper, lookupValues, execParamsHelper);
            }

            sql += " ORDER BY ";
            sql += EscSqlColumnName(databaseHelper, sortColumnName);
            sql += asc ? " ASC" : " DESC";
            sql += " LIMIT NULLIF(";
            sql += execParamsHelper.AddParam(std::to_string(pageSize));
            sql += ", 0) OFFSET ";
            sql += execParamsHelper.AddParam(std::to_string(page * pageSize));

            return sql;
        }

        std::string GenerateSearchRowsByILikeSql(
            Transaction& transaction,
            DatabaseHelper databaseHelper,
            std::string_view tableName,
            std::string_view sortColumnName,
            const StringArray& searchColumns,
            std::string_view searchText,
            bool asc,
            int pageSize,
            int page,
            ExecParamsHelper& execParamsHelper) {
            DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
                DbMeta::ListColumns(transaction, tableName);
            LookupDatabaseColumnInfoByColumnName(
                databaseColumnInfoArray, sortColumnName);
            StringArray saColumnNames = GetColumnNames(
                databaseHelper, databaseColumnInfoArray);

            // Validate search columns exist
            DatabaseColumnInfoTable databaseColumnInfoTable
                = GenerateDatabaseColumnInfoTable(databaseColumnInfoArray);
            for (const auto& col : searchColumns) {
                if (databaseColumnInfoTable.find(col) == databaseColumnInfoTable.end()) {
                    throw std::invalid_argument(
                        "Search column '" + col + "' not found in table.");
                }
            }

            // SELECT col1, col2, ..., COUNT(*) OVER() AS _total_count
            // FROM tableName
            // WHERE (col1::text ILIKE $1 OR col2::text ILIKE $1)
            // ORDER BY sortCol ASC
            // LIMIT NULLIF($2, 0) OFFSET $3

            std::string sql = "SELECT ";
            sql += StringArrayToCommaSeparatedString(saColumnNames);
            sql += ", COUNT(*) OVER() AS _total_count";
            sql += " FROM ";
            sql += EscSqlTableName(databaseHelper, tableName);

            if (!searchColumns.empty()) {
                std::string pattern = "%" + std::string(searchText) + "%";
                std::string searchParam = execParamsHelper.AddParam(pattern);
                sql += " WHERE (";
                for (size_t i = 0; i < searchColumns.size(); ++i) {
                    if (i > 0) sql += " OR ";
                    sql += EscSqlColumnName(databaseHelper, searchColumns[i]);
                    sql += "::text ILIKE ";
                    sql += searchParam;
                }
                sql += ")";
            }

            sql += " ORDER BY ";
            sql += EscSqlColumnName(databaseHelper, sortColumnName);
            sql += asc ? " ASC" : " DESC";
            sql += " LIMIT NULLIF(";
            sql += execParamsHelper.AddParam(std::to_string(pageSize));
            sql += ", 0) OFFSET ";
            sql += execParamsHelper.AddParam(std::to_string(page * pageSize));

            return sql;
        }

    }  // namespace PrivateSql

    void AddRowToTable(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords /*= StringSet()*/) {
        ExecParamsHelper execParamsHelper;
        std::string sqlInsert = PrivateSql::GenerateAddRowToTableSql(
            transaction, databaseHelper, tableName, table, allowedSqlKeywords, execParamsHelper);
        transaction.RunSqlStatementHelper(sqlInsert, execParamsHelper);
    }

    // Adds row to table with provided primary key
    int AddRowToTableFetchIntPrimaryKey(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords /*= StringSet()*/) {
        int id;
        ExecParamsHelper execParamsHelper;
        std::string sqlInsert =
            PrivateSql::GenerateAddRowToTableFetchIntPrimaryKeySql(
                transaction, databaseHelper, tableName, table, allowedSqlKeywords, execParamsHelper);
        std::string idParam = transaction.RunSqlStatementReturningOneValueHelper(
            sqlInsert, execParamsHelper);
        if(idParam.empty()) {
            throw std::runtime_error(
                "AddRowToTableFetchIntPrimaryKey - Most likely duplicate email.");
        }
        return stoi(idParam);
    }

    // Adds row to table with provided primary key as int64_t
    int64_t AddRowToTableFetchInt64PrimaryKey(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords /*= StringSet()*/) {
        ExecParamsHelper execParamsHelper;
        std::string sqlInsert =
            PrivateSql::GenerateAddRowToTableFetchIntPrimaryKeySql(
                transaction, databaseHelper, tableName, table, allowedSqlKeywords, execParamsHelper);
        std::string idParam = transaction.RunSqlStatementReturningOneValueHelper(
            sqlInsert, execParamsHelper);
        if(idParam.empty()) {
            throw std::runtime_error(
                "AddRowToTableFetchInt64PrimaryKey - Most likely duplicate email.");
        }
        return std::stoll(idParam);
    }

    KeyValueTable LookupRowByValue(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view lookupValue) {
        std::string sqlLookup = PrivateSql::GenerateLookupRowByValueSql(
            transaction, databaseHelper, tableName, columnName, lookupValue);
        ExecParamsHelper execParamsHelper;
        execParamsHelper.AddParam(lookupValue);
        return transaction.RunSqlStatementReturningOneRowHelper(
            sqlLookup, execParamsHelper);
    }

    KeyValueTable LookupRowByValues(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        KeyValueTable lookupValues) {
        ExecParamsHelper execParamsHelper;
        std::string sqlLookup = PrivateSql::GenerateLookupRowByValuesSql(
            transaction, databaseHelper, tableName, lookupValues, execParamsHelper);
        return transaction.RunSqlStatementReturningOneRowHelper(
            sqlLookup, execParamsHelper);
    }

    KeyValueTableArray GetTableRows(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName) {
        DbMeta::DatabaseColumnInfoArray databaseColumnInfoArray =
            DbMeta::ListColumns(transaction, tableName);
        StringArray columnNames;
        for (const DbMeta::DatabaseColumnInfo& databaseColumnInfo
            : databaseColumnInfoArray) {
            columnNames.push_back(databaseColumnInfo.columnName);
        }
        std::string sql = DbUtil::GenerateSelectStatementFromTableAndInfo(
            tableName, databaseColumnInfoArray);
        return transaction.RunSqlStatementReturningKeyValueTableArray(sql);
    }

    KeyValueTableArray GetRowsByColumn(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        bool asc,
        int pageSize,
        int page) {
        std::string sqlRowsByColumn = PrivateSql::GenerateGetRowsByColumnSql(
            transaction, databaseHelper, tableName, columnName, asc, pageSize, page);
        ExecParamsHelper execParamsHelper;
        execParamsHelper.AddParam(std::to_string(pageSize));
        execParamsHelper.AddParam(std::to_string(page * pageSize));
        return transaction.RunSqlStatementReturningKeyValueTableArrayHelper(
            sqlRowsByColumn, execParamsHelper);
    }

    KeyValueTableArray GetRowsByValues(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view sortColumnName,
        const KeyValueTable& lookupValues,
        bool asc,
        int pageSize,
        int page,
        int64_t* totalCount /*= nullptr*/) {
        ExecParamsHelper execParamsHelper;
        std::string sql = PrivateSql::GenerateGetRowsByValuesSql(
            transaction, databaseHelper, tableName, sortColumnName,
            lookupValues, asc, pageSize, page, execParamsHelper);
        KeyValueTableArray results =
            transaction.RunSqlStatementReturningKeyValueTableArrayHelper(
                sql, execParamsHelper);
        ExtractTotalCount(results, totalCount);
        return results;
    }

    KeyValueTableArray GetRowsByValuesWithOrderBy(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& lookupValues,
        std::string_view orderByColumn,
        bool asc) {
        ExecParamsHelper execParamsHelper;
        std::string sql = PrivateSql::GenerateGetRowsByValuesWithOrderBySql(
            transaction, databaseHelper, tableName,
            lookupValues, orderByColumn, asc, execParamsHelper);
        return transaction.RunSqlStatementReturningKeyValueTableArrayHelper(
            sql, execParamsHelper);
    }

    int64_t GetTableRowCount(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName) {
        std::string sql = "SELECT COUNT(*) FROM ";
        sql += EscSqlTableName(databaseHelper, tableName);
        std::string countStr = transaction.RunSqlStatementReturningOneValue(sql);
        return std::stoll(countStr);
    }

    KeyValueTable GetRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view columnValue) {
        StringArray columnNames;
        std::string sqlGetRow = PrivateSql::GenerateGetRowSql(
            transaction, databaseHelper, tableName, columnName, columnValue, columnNames);
        ExecParamsHelper execParamsHelper;
        execParamsHelper.AddParam(columnValue);
        return transaction.RunSqlStatementReturningOneRowHelper(
            sqlGetRow, execParamsHelper);
    }

    void UpdateRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view columnValue,
        const KeyValueTable& table,
        const StringSet& allowedSqlKeywords /*= StringSet()*/) {
        ExecParamsHelper execParamsHelper;
        std::string sqlUpdate = PrivateSql::GenerateUpdateRowSql(
            transaction, databaseHelper, tableName, columnName, columnValue, table, allowedSqlKeywords, execParamsHelper);
        return transaction.RunSqlStatementHelper(sqlUpdate, execParamsHelper);
    }

    void DeleteRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view columnName,
        std::string_view columnValue) {
        std::string sqlDelete = PrivateSql::GenerateDeleteRowSql(
            transaction, databaseHelper, tableName, columnName, columnValue);
        ExecParamsHelper execParamsHelper;
        execParamsHelper.AddParam(columnValue);
        transaction.RunSqlStatementHelper(sqlDelete, execParamsHelper);
    }

    void DeleteRow(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        const KeyValueTable& keyValueTable) {
        ExecParamsHelper execParamsHelper;
        std::string sqlDelete = PrivateSql::GenerateDeleteRowSql(
            transaction, databaseHelper, tableName, keyValueTable, execParamsHelper);
        transaction.RunSqlStatementHelper(sqlDelete, execParamsHelper);
    }

    KeyValueTableArray SearchRowsByILike(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view sortColumnName,
        const StringArray& searchColumns,
        std::string_view searchText,
        bool asc,
        int pageSize,
        int page,
        int64_t* totalCount /*= nullptr*/) {
        ExecParamsHelper execParamsHelper;
        std::string sql = PrivateSql::GenerateSearchRowsByILikeSql(
            transaction, databaseHelper, tableName, sortColumnName,
            searchColumns, searchText, asc, pageSize, page, execParamsHelper);
        KeyValueTableArray results =
            transaction.RunSqlStatementReturningKeyValueTableArrayHelper(
                sql, execParamsHelper);
        ExtractTotalCount(results, totalCount);
        return results;
    }

    StringArray ParseTemplateColumns(std::string_view displayTemplate) {
        StringArray columns;
        size_t pos = 0;
        while (pos < displayTemplate.size()) {
            size_t start = displayTemplate.find('{', pos);
            if (start == std::string_view::npos) break;
            size_t end = displayTemplate.find('}', start + 1);
            if (end == std::string_view::npos) break;
            columns.push_back(std::string(
                displayTemplate.substr(start + 1, end - start - 1)));
            pos = end + 1;
        }
        return columns;
    }

    KeyValueTable ResolveDisplayValues(
        const KeyValueTableArray& rows,
        std::string_view primaryKeyColumn,
        std::string_view displayTemplate) {
        KeyValueTable result;
        std::string pkCol(primaryKeyColumn);
        for (const KeyValueTable& row : rows) {
            auto it = row.find(pkCol);
            if (it == row.end()) continue;
            const std::string& pkValue = it->second;
            if (displayTemplate.empty()) {
                result[pkValue] = pkValue;
            } else {
                result[pkValue] = FormatString(displayTemplate, row);
            }
        }
        return result;
    }

    KeyValueTableArray LookupRowsByPrimaryKeyValues(
        Transaction& transaction,
        DatabaseHelper databaseHelper,
        std::string_view tableName,
        std::string_view primaryKeyColumn,
        const StringArray& values) {
        if (values.empty()) {
            return {};
        }
        std::string quotedTable = EscSqlTableName(
            databaseHelper, tableName);
        std::string quotedColumn = EscSqlColumnName(
            databaseHelper, primaryKeyColumn);
        ExecParamsHelper execParamsHelper;
        StringArray paramPlaceholders;
        for (const auto& value : values) {
            paramPlaceholders.push_back(execParamsHelper.AddParam(value));
        }
        std::string sql = "SELECT * FROM " + quotedTable
            + " WHERE " + quotedColumn + " IN ("
            + StringArrayToCommaSeparatedString(paramPlaceholders)
            + ")";
        return transaction.RunSqlStatementReturningKeyValueTableArrayHelper(
            sql, execParamsHelper);
    }

} //  namespace DbCrud
