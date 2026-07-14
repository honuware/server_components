#include "database_helper.h"
#include <curl/curl.h>

#include "database_helper_base.h"
#include "database_helper_init.h"
#include "transaction_impl.h"
#include "util/logging.h"

namespace {

    class DatabaseHelperProd : public DatabaseHelperBase {
    public:
        DatabaseHelperProd(const DatabaseHelperInit& init);
        DatabaseHelperProd(const DatabaseHelperProd& ref) = delete;
        DatabaseHelperProd& operator=(const DatabaseHelperProd& ref) = delete;
        ~DatabaseHelperProd();

        // Fetch the database connection object
        pqxx::connection& GetConnection() override;

        // Get the database name
        const std::string& GetDatabaseName() const override;

        // Run the provided code in the context of a transaction
        // in an exception block.
        void RunInTransaction(
            std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) override;

        bool IsTest() const override { return false; }

    private:
        std::shared_ptr<pqxx::connection> connection_;
        std::string databaseName_;
    };

    DatabaseHelperProd::DatabaseHelperProd(const DatabaseHelperInit& init)
        : databaseName_(init.dbname) {
        connection_ = std::make_shared<pqxx::connection>(
            init.GetConnectionString()
        );
    }

    DatabaseHelperProd::~DatabaseHelperProd() {
    }

    pqxx::connection& DatabaseHelperProd::GetConnection() {
        return *connection_;
    }

    const std::string& DatabaseHelperProd::GetDatabaseName() const {
        return databaseName_;
    }

    void DatabaseHelperProd::RunInTransaction(
        std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) {
        std::string transactionDescription =
            "Running transaction: " + std::string(description);
        pqxx::work trans(*connection_, transactionDescription);
        TransactionImpl transaction(trans);
        databaseFunc(transaction);
        trans.commit();
    }

    class DatabaseHelperNoDatabase : public DatabaseHelperBase {
    public:
        DatabaseHelperNoDatabase();
        DatabaseHelperNoDatabase(const DatabaseHelperNoDatabase& ref) = delete;
        DatabaseHelperNoDatabase& operator=(const DatabaseHelperNoDatabase& ref) = delete;
        ~DatabaseHelperNoDatabase() = default;

        // Fetch the database connection object
        pqxx::connection& GetConnection() override;

        // Get the database name
        const std::string& GetDatabaseName() const override;

        // Run the provided code in the context of a transaction
        // in an exception block.
        void RunInTransaction(
            std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) override;

        bool IsTest() const override { return true; }

    private:
        std::shared_ptr<pqxx::connection> connection_;
    };

    DatabaseHelperNoDatabase::DatabaseHelperNoDatabase() {
        // No database is selected: pass an empty default and clear any value
        // the KNOTTYYOGA_DB_NAME env var might have supplied, so the
        // connection string omits dbname entirely.
        DatabaseHelperInit init("");
        init.dbname.clear();
        connection_ = std::make_shared<pqxx::connection>(
            init.GetConnectionString()
        );
    }

    pqxx::connection& DatabaseHelperNoDatabase::GetConnection() {
        return *connection_;
    }

    const std::string& DatabaseHelperNoDatabase::GetDatabaseName() const {
        static std::string emptyString;
        return emptyString;
    }

    void DatabaseHelperNoDatabase::RunInTransaction(
        std::string_view description, DatabaseHelper::DatabaseFunc databaseFunc) {
        std::string transactionDescription =
            "Running transaction: " + std::string(description);
        pqxx::nontransaction trans(GetConnection(), "Clear test database");
        TransactionImpl transaction(trans);
        databaseFunc(transaction);
    }

    std::shared_ptr<DatabaseHelperBase> MakeNoDatabaseHelperTest() {
        auto databaseHelperTest = std::make_unique<DatabaseHelperNoDatabase>();
        return databaseHelperTest;
    }

}  // namespace {

DatabaseHelper::DatabaseHelper(std::shared_ptr<DatabaseHelperBase> base) : base_(base) {}

DatabaseHelper::DatabaseHelper(const DatabaseHelper& ref) : base_(ref.base_) {}

DatabaseHelper& DatabaseHelper::operator=(const DatabaseHelper& ref) {
    if (this != &ref) {
        base_ = ref.base_;
    }
    return *this;
}

DatabaseHelper::~DatabaseHelper() {}

pqxx::connection& DatabaseHelper::GetConnection() {
    return base_->GetConnection();
}

const std::string& DatabaseHelper::GetDatabaseName() const {
    return base_->GetDatabaseName();
}

void DatabaseHelper::RunInTransaction(
    std::string_view description, DatabaseFunc databaseFunc) {
    base_->RunInTransaction(description, databaseFunc);
}

std::string UrlUnescape(std::string_view url) {
    std::string ret;
    CURL *curl = curl_easy_init();
    if(curl) {
        char* unescape = curl_easy_unescape(curl, url.data(), url.size(), nullptr);
        ret = unescape;
        curl_free(unescape);
        curl_easy_cleanup(curl);
    }
    return ret;
}

std::string EscSqlString(DatabaseHelper databaseHelper, std::string_view strToEsc) {
    pqxx::nontransaction fakeTrans(databaseHelper.GetConnection());
    return fakeTrans.esc(strToEsc);
}

std::string EscQuoteSqlString(
    DatabaseHelper databaseHelper, std::string_view strToEsc) {
    pqxx::nontransaction fakeTrans(databaseHelper.GetConnection());
    return fakeTrans.quote(strToEsc);
}

bool IsSimpleName(std::string_view name) {
    if(name.empty()) {
        return false;
    }
    char firstChar = name[0];
    if(!std::isalpha(firstChar) && firstChar != '_') {
        return false;
    }
    for (char c : name) {
        if(!std::isalnum(c) && c != '_') {
            return false;
        }
    }
    return true;
}

std::string EscSqlTableName(DatabaseHelper databaseHelper, std::string_view tableName) {
    std::string loweredTableName = StringToLower(tableName);
    if(IsSimpleName(loweredTableName)) {
        return loweredTableName;
    }
    pqxx::nontransaction fakeTrans(databaseHelper.GetConnection());
    return fakeTrans.quote_name(loweredTableName);
}

std::string EscSqlColumnName(DatabaseHelper databaseHelper, std::string_view columnName) {
    std::string loweredColumnName = StringToLower(columnName);
    if(IsSimpleName(loweredColumnName)) {
        return loweredColumnName;
    }
    pqxx::nontransaction fakeTrans(databaseHelper.GetConnection());
    return fakeTrans.quote_name(loweredColumnName);
}

DatabaseHelper MakeProductionDatabaseHelper(std::string_view databaseName) {
    DatabaseHelperInit init(databaseName);
    init.LogStartupInfo();
    auto databaseHelperProd = std::make_shared<DatabaseHelperProd>(init);
    DatabaseHelper helper(databaseHelperProd);
    return helper;
}

DatabaseHelper MakeNoDatabaseHelper() {
    DatabaseHelper helper(MakeNoDatabaseHelperTest());
    return helper;
}
