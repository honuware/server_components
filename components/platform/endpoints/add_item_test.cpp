#include "add_item.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "db_schema/people.h"
#include "sql_util/table_helpers/allowed_tables.h"
#include "sql_util/database_common.h"
#include "sql_util/json/database_rest_helper.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_test_util.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/test_helper.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using Json::JsonPairValue;
        using JsonTestUtil::JsonPerson;

        TEST(AddItemTest, AddItemBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);
				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::Value person2 = JsonPerson(
                    "email2", "first2", "last2", "hash2");

				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem1["value"] = std::move(person1);
				Json::JsonObject addItem2;
				addItem2["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem2["value"] = std::move(person2);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        { {"email", "email1"}, {"first_name", "first1"}, {"last_name", "last1"}, {"password_hash", "hash1"} }
                    },
                    DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
				req.body = Json::Value(std::move(addItem2)).ToString();
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        { {"email", "email1"}, {"first_name", "first1"}, {"last_name", "last1"}, {"password_hash", "hash1"} },
                        { {"email", "email2"}, {"first_name", "first2"}, {"last_name", "last2"}, {"password_hash", "hash2"} }
                    },
                    DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        TEST(AddItemTest, AddItemNoMessageBody) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemNoMessageBody", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kBadRequest);
                EXPECT_EQ(body["detail"].Get<std::string>(), "No message body.");
                });
        }

        TEST(AddItemTest, AddItemNoTableName) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemNoTableName", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::JsonObject addItem1;
				addItem1["value"] = std::move(person1);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "table_name not found.");
                });
        }

        TEST(AddItemTest, AddItemNoValue) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemNoValue", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "value not found.");
                });
        }

        TEST(AddItemTest, AddItemTableNotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem1["value"] = std::move(person1);

                crow::request req;
                req.url = "/api/add_item";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), ErrorCodes::kValidationError);
                EXPECT_EQ(body["detail"].Get<std::string>(), "Table(people) is not an allowed table.");
                });
        }

    } // namespace 
}  // namespace Endpoints
