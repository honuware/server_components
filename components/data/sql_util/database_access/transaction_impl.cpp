#include "transaction_impl.h"

TransactionImpl::TransactionImpl(pqxx::transaction_base& trans)
    : trans_(trans) {}

DataResults TransactionImpl::RunSqlStatementReturningDataResultsHelper(
    std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) {
    StringArray columnNames;
    StringTable stringTable;
    pqxx::result res = trans_.exec_params(
        std::string(sqlStatement), execParamsHelper.GetParams());
    columnNames.reserve(res.columns());
    for (pqxx::result::size_type i = 0; i < res.columns(); ++i) {
        columnNames.emplace_back(res.column_name(i));
    }
    for (const pqxx::row& row : res) {
        StringArray rowData;
        for (const pqxx::field& field : row) {
            if (!field.is_null()) {
                rowData.push_back(field.as <std::string>());
            }
            else {
                rowData.push_back(std::string());
            }
        }
        stringTable.push_back(rowData);
    }
    return MakeDataResults(columnNames, stringTable);
}

KeyValueTableArray TransactionImpl::RunSqlStatementReturningKeyValueTableArrayHelper(
    std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) {
    KeyValueTableArray tableArray;
    pqxx::result res = trans_.exec_params(
        std::string(sqlStatement), execParamsHelper.GetParams());
    for (const pqxx::row& row : res) {
        KeyValueTable keyValueTable;
        for (pqxx::result::size_type i = 0; i < res.columns(); ++i) {
            const pqxx::field& field = row[i];
            if (!field.is_null()) {
                keyValueTable[field.name()] = field.as<std::string>();
            }
            else {
                keyValueTable[field.name()] = std::string();
            }
        }
        tableArray.emplace_back(std::move(keyValueTable));
    }
    return tableArray;
}

StringTable TransactionImpl::RunSqlStatementReturningStringTableHelper(
    std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) {
    StringTable table;
    pqxx::result res = trans_.exec_params(
        std::string(sqlStatement), execParamsHelper.GetParams());
    for (const pqxx::row& row : res) {
        StringArray rowData;
        for (const pqxx::field& field : row) {
            if (!field.is_null()) {
                rowData.push_back(field.as <std::string>());
            }
            else {
                rowData.push_back(std::string());
            }
        }
        table.push_back(rowData);
    }
    return table;

}

KeyValueTable TransactionImpl::RunSqlStatementReturningOneRowHelper(
    std::string_view sqlStatement,
    const ExecParamsHelper& execParamsHelper) {
    KeyValueTable keyValueTable;
    pqxx::result res = trans_.exec_params(std::string(sqlStatement), execParamsHelper.GetParams());
    if (res.size() > 0) {
        pqxx::row row = res[0];
        for (pqxx::result::size_type i = 0; i < res.columns(); ++i) {
            const pqxx::field& field = row[i];
            if (!field.is_null()) {
                keyValueTable[field.name()] = field.as<std::string>();
            }
            else {
                keyValueTable[field.name()] = std::string();
            }
        }
    }
    return keyValueTable;
}

std::string TransactionImpl::RunSqlStatementReturningOneValueHelper(
    std::string_view sqlStatement,
    const ExecParamsHelper& execParamsHelper) {
    std::string value;
    pqxx::row result = trans_.exec_params1(
        std::string(sqlStatement), execParamsHelper.GetParams());
    value = result[0].as<std::string>();
    return value;
}

void TransactionImpl::RunSqlStatementHelper(
    std::string_view sqlStatement,
    const ExecParamsHelper& execParamsHelper) {
    trans_.exec_params0(
        std::string(sqlStatement), execParamsHelper.GetParams());
}


