#pragma once

#include "sql_util/database_common.h"

class ExecParamsHelper {
public:
    ExecParamsHelper() = default;
    std::string AddParam(std::string_view param) {
        params_.push_back(std::string(param));
        return "$" + std::to_string(count_++);
    }
    const pqxx::params GetParams() const {
        pqxx::params params;
        params.reserve(params_.size());
        params.append_multi(params_);
        return params;
    }
    const StringArray& GetParamStringArray() const {
        return params_;
    }

private:
    int count_ = 1;
    StringArray params_;
};

class Transaction {
public:
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    virtual ~Transaction() = default;

    template <class... Args>
    ExecParamsHelper BuildExecParams(Args&&... args) {
        static_assert((std::is_convertible_v<Args, std::string_view> && ...),
            "All arguments must be convertible to std::string_view");

        ExecParamsHelper helper;
        (helper.AddParam(std::forward<Args>(args)), ...);
        return helper;
    }

    virtual DataResults RunSqlStatementReturningDataResultsHelper(
        std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) = 0;

    template <class... Args>
    DataResults RunSqlStatementReturningDataResults(
        std::string_view sqlStatement,
        Args&&... args) {
        ExecParamsHelper helper = BuildExecParams(std::forward<Args>(args)...);
        return RunSqlStatementReturningDataResultsHelper(sqlStatement, helper);
    }

    virtual KeyValueTableArray RunSqlStatementReturningKeyValueTableArrayHelper(
        std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) = 0;

    template <class... Args>
    KeyValueTableArray RunSqlStatementReturningKeyValueTableArray(
        std::string_view sqlStatement,
        Args&&... args) {
        ExecParamsHelper helper = BuildExecParams(std::forward<Args>(args)...);
        return RunSqlStatementReturningKeyValueTableArrayHelper(sqlStatement, helper);
    }

    virtual StringTable RunSqlStatementReturningStringTableHelper(
        std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) = 0;

    template <class... Args>
    StringTable RunSqlStatementReturningStringTable(
        std::string_view sqlStatement,
        Args&&... args) {
        ExecParamsHelper helper = BuildExecParams(std::forward<Args>(args)...);
        return RunSqlStatementReturningStringTableHelper(sqlStatement, helper);
    }


    virtual KeyValueTable RunSqlStatementReturningOneRowHelper(
        std::string_view sqlStatement,
        const ExecParamsHelper& execParamsHelper) = 0;

    template <class... Args>
    KeyValueTable RunSqlStatementReturningOneRow(
        std::string_view sqlStatement,
        Args&&... args) {
        ExecParamsHelper helper = BuildExecParams(std::forward<Args>(args)...);
        return RunSqlStatementReturningOneRowHelper(
            sqlStatement, helper);
    }

    virtual std::string RunSqlStatementReturningOneValueHelper(
        std::string_view sqlStatement,
        const ExecParamsHelper& execParamsHelper) = 0;

    template <class... Args>
    std::string RunSqlStatementReturningOneValue(
        std::string_view sqlStatement,
        Args&&... args) {
        ExecParamsHelper helper = BuildExecParams(std::forward<Args>(args)...);
        return RunSqlStatementReturningOneValueHelper(
            sqlStatement, helper);
    }

    virtual void RunSqlStatementHelper(
        std::string_view sqlStatement,
        const ExecParamsHelper& execParamsHelper) = 0;

    template <class... Args>
    void RunSqlStatement(
        std::string_view sqlStatement,
        Args&&... args) {
        ExecParamsHelper helper = BuildExecParams(std::forward<Args>(args)...);
        RunSqlStatementHelper(sqlStatement, helper);
    }

protected:
    Transaction() = default;
};
