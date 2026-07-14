#include "add_item_fetch_primary_key.h"

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
#include "test/src/util/database_test_helper.h"
#include "test/src/util/table_matcher.h"
#include "sql_util/database_access/database_crud_helpers.h"
#include "util/json_value.h"


namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using Json::JsonPairValue;
        using JsonTestUtil::JsonPerson;

        TEST(AddItemFetchPrimaryKeyTest, AddItemFetchPrimaryKeyBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemFetchPrimaryKeyBasic", [&](Transaction& transaction) {
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
                req.url = "/api/add_item_fetch_primary_key";
                req.method = crow::HTTPMethod::POST;
                req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value primaryKeyBody1 = Json::Value::FromText(resp.body);
                ASSERT_TRUE(primaryKeyBody1.HasChildren());
                std::string id1 = primaryKeyBody1[DbSchema::kPeopleId].Get<std::string>();
                ASSERT_FALSE(id1.empty());

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        {
                            { std::string(DbSchema::kPeopleEmail), "email1" },
                            { std::string(DbSchema::kPeopleFirstName), "first1" },
                            { std::string(DbSchema::kPeopleLastName), "last1" },
                            { std::string(DbSchema::kPeoplePasswordHash), "hash1" }
                        }
                    },
                    DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

                req.body = Json::Value(std::move(addItem2)).ToString();
                resp.clear();
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value primaryKeyBody2 = Json::Value::FromText(resp.body);
                ASSERT_TRUE(primaryKeyBody2.HasChildren());
                std::string id2 = primaryKeyBody2[DbSchema::kPeopleId].Get<std::string>();
                ASSERT_FALSE(id2.empty());
                ASSERT_NE(id1, id2);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        {
                            { std::string(DbSchema::kPeopleEmail), "email1" },
                            { std::string(DbSchema::kPeopleFirstName), "first1" },
                            { std::string(DbSchema::kPeopleLastName), "last1" },
                            { std::string(DbSchema::kPeoplePasswordHash), "hash1" }
                        },
                        {
                            { std::string(DbSchema::kPeopleEmail), "email2" },
                            { std::string(DbSchema::kPeopleFirstName), "first2" },
                            { std::string(DbSchema::kPeopleLastName), "last2" },
                            { std::string(DbSchema::kPeoplePasswordHash), "hash2" }
                        }
                    },
                    DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });         
        }

        TEST(AddItemFetchPrimaryKeyTest, AddItemFetchPrimaryKeyNoMessageBody) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemFetchPrimaryKeyNoMessageBody", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item_fetch_primary_key";
                req.method = crow::HTTPMethod::POST;
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                // Phase 7.3 of the security review: response is the
                // RFC-7807 BadRequest shape, not a raw text body.
                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), "bad_request");
                EXPECT_EQ(body["detail"].Get<std::string>(), "No message body.");
                });
        }

        TEST(AddItemFetchPrimaryKeyTest, AddItemFetchPrimaryKeyNoTableName) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemFetchPrimaryKeyNoTableName", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::JsonObject addItem1;
				addItem1["value"] = std::move(person1);

                crow::request req;
                req.url = "/api/add_item_fetch_primary_key";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                // Phase 7.3: input-validation errors are routed to
                // ValidationError (400) — same shape as other
                // missing-field errors in the codebase.
                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), "validation_error");
                EXPECT_EQ(body["detail"].Get<std::string>(), "table_name not found.");
                });
        }

        TEST(AddItemFetchPrimaryKeyTest, AddItemFetchPrimaryKeyNoValue) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemFetchPrimaryKeyNoValue", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                endpointTestHelper.AddAllowedTable(transaction, DbSchema::kPeopleTable);

				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);

                crow::request req;
                req.url = "/api/add_item_fetch_primary_key";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                // Phase 7.3: input-validation routes to 400 ValidationError.
                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), "validation_error");
                EXPECT_EQ(body["detail"].Get<std::string>(), "value not found.");
                });
        }

        TEST(AddItemFetchPrimaryKeyTest, AddItemFetchPrimaryKeyTableNotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("AddItemFetchPrimaryKeyTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                // Note: Not adding people table to allowed tables
				Json::Value person1 = JsonPerson(
                    "email1", "first1", "last1", "hash1");
				Json::JsonObject addItem1;
				addItem1["table_name"] = std::string(DbSchema::kPeopleTable);
				addItem1["value"] = std::move(person1);

                crow::request req;
                req.url = "/api/add_item_fetch_primary_key";
                req.method = crow::HTTPMethod::POST;
				req.body = Json::Value(std::move(addItem1)).ToString();
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                // Phase 7.3: not-allowed-table is a ValidationError (400).
                ASSERT_EQ(resp.code, 400);
                Json::Value body = Json::Value::FromText(resp.body);
                EXPECT_EQ(body["type"].Get<std::string>(), "validation_error");
                EXPECT_EQ(body["detail"].Get<std::string>(),
                    "Table(people) is not an allowed table.");
                });
        }

    } // namespace 
}  // namespace Endpoints
