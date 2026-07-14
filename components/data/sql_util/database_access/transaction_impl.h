#pragma

#include "transaction.h"

class TransactionImpl : public Transaction {
public:
    TransactionImpl(pqxx::transaction_base& trans);
    TransactionImpl(const TransactionImpl&) = delete;
    TransactionImpl& operator=(const TransactionImpl&) = delete;
    ~TransactionImpl() = default;

    DataResults RunSqlStatementReturningDataResultsHelper(
        std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) override;

    KeyValueTableArray RunSqlStatementReturningKeyValueTableArrayHelper(
        std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) override;

    StringTable RunSqlStatementReturningStringTableHelper(
        std::string_view sqlStatement, const ExecParamsHelper& execParamsHelper) override;

    KeyValueTable RunSqlStatementReturningOneRowHelper(
        std::string_view sqlStatement,
        const ExecParamsHelper& execParamsHelper) override;

    std::string RunSqlStatementReturningOneValueHelper(
        std::string_view sqlStatement,
        const ExecParamsHelper& execParamsHelper) override;

    void RunSqlStatementHelper(
        std::string_view sqlStatement,
        const ExecParamsHelper& execParamsHelper) override;

private:
    pqxx::transaction_base& trans_;
};

