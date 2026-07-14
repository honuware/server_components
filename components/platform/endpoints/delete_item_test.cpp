#include "delete_item.h"

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
#include "util/error_codes.h"
#include "util/json_value.h"

namespace Endpoints {
    namespace
    {
        using Json::JsonPair;
        using Json::JsonPairValue;
        using JsonTestUtil::JsonPerson;

        TEST(DeleteItemTest, DeleteItemBasic) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemBasic", [&](Transaction& transaction) {
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

				req.body = Json::Value(std::move(addItem2)).ToString();
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                // Get actual IDs from inserted rows
                auto rows = DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable);
                ASSERT_EQ(rows.size(), 2u);
                std::string id1, id2;
                for (const auto& row : rows) {
                    if (row.at(std::string(DbSchema::kPeopleEmail)) == "email1") {
                        id1 = row.at(std::string(DbSchema::kPeopleId));
                    } else {
                        id2 = row.at(std::string(DbSchema::kPeopleId));
                    }
                }
                ASSERT_FALSE(id1.empty());
                ASSERT_FALSE(id2.empty());

                req.body = {};
                req.method = crow::HTTPMethod::GET;
                req.url = "/api/delete_item/people/id/" + id2;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {
                        {
                            {std::string(DbSchema::kPeopleEmail), "email1"},
                            {std::string(DbSchema::kPeopleFirstName), "first1"},
                            {std::string(DbSchema::kPeopleLastName), "last1"},
                            {std::string(DbSchema::kPeoplePasswordHash), "hash1"}
                         }
                    },
                    DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));

                req.body = {};
                req.method = crow::HTTPMethod::GET;
                req.url = "/api/delete_item/people/id/" + id1;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);

                ASSERT_TRUE(CompareKeyValueTableArraysMinusColumns(
                    {},
                    DbCrud::GetTableRows(transaction, databaseHelper, DbSchema::kPeopleTable),
                    DbSchema::kPeopleId, DbSchema::kPeopleCreatedAt, DbSchema::kPeopleUpdatedAt, DbSchema::kPeopleEmailVerifiedAt, DbSchema::kPeopleMustChangePassword, DbSchema::kPeopleFailedLoginAttempts, DbSchema::kPeopleLockedUntil));
                });
        }

        TEST(DeleteItemTest, DeleteItemTableNotAllowed) {
            TestDatabaseUtil testDatabaseUtil;
            testDatabaseUtil.RunInTransaction("DeleteItemTableNotAllowed", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);

                crow::request req;
                req.url = "/api/delete_item/people/id/1";
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
