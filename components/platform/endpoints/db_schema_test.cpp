#include "db_schema.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "endpoints/endpoint_test_helper.h"
#include "web_app.h"
#include "sql_util/table_helpers/allowed_tables.h"
#include "sql_util/database_common.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/database_test_helper.h"
#include "util/json_value.h"

#include "sql_util/table_helpers/admin_table_friendly_names.h"
#include "sql_util/table_helpers/admin_column_friendly_names.h"
#include "sql_util/table_helpers/admin_column_data_info.h"
#include "sql_util/table_helpers/admin_table_display_template.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Endpoints {
    namespace
    {
        constexpr std::string_view kTestDatabaseSchema = R"JSON(
{
  "display_templates": {},
  "fk_picker_preload_threshold": 50,
  "nested_tables": [],
  "root_tables": ["people","orders"],
  "tables": [
    {
      "columns": [
        {
          "column_friendly_name": "Person ID",
          "column_name": "person_id",
          "default_value": "default1a",
          "hidden": "f",
          "hint": "hint1a",
          "html_input_type": "htmlInputType1a",
          "label": "label1a",
          "max_length": "5",
          "nullable": "f",
          "place_holder": "placeHolder1a",
          "primary_key": "t",
          "readonly": "f",
          "regex": "regex1a",
          "required": "t",
          "rows": "3",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "Email",
          "column_name": "email",
          "default_value": "default1b",
          "hidden": "f",
          "hint": "hint1b",
          "html_input_type": "htmlInputType1b",
          "label": "label1b",
          "max_length": "7",
          "nullable": "f",
          "place_holder": "placeHolder1b",
          "primary_key": "f",
          "readonly": "f",
          "regex": "regex1b",
          "required": "t",
          "rows": "5",
          "type": "VARCHAR",
          "unique": "t"
        }
      ],
      "description": "description1",
      "foreign_keys": [],
      "primary_key": "person_id",
      "table_friendly_name": "People",
      "table_name": "people"
    },
    {
      "columns": [
        {
          "column_friendly_name": "Order ID",
          "column_name": "order_id",
          "default_value": "default2a",
          "hidden": "f",
          "hint": "hint2a",
          "html_input_type": "htmlInputType2a",
          "label": "label2a",
          "max_length": "6",
          "nullable": "f",
          "place_holder": "placeHolder2a",
          "primary_key": "t",
          "readonly": "f",
          "regex": "regex2a",
          "required": "f",
          "rows": "4",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "My person",
          "column_name": "parent_person_id",
          "default_value": "default2",
          "hidden": "f",
          "hint": "hint2b",
          "html_input_type": "htmlInputType2b",
          "label": "label2b",
          "max_length": "8",
          "nullable": "f",
          "place_holder": "placeHolder2b",
          "primary_key": "f",
          "readonly": "f",
          "regex": "regex2b",
          "required": "f",
          "rows": "6",
          "type": "INT",
          "unique": "f"
        }
      ],
      "description": "description2",
      "foreign_keys": [
        {
          "column_name": "parent_person_id",
          "parent_column_name": "person_id",
          "parent_table_name": "people"
        }
      ],
      "primary_key": "order_id",
      "table_friendly_name": "Orders",
      "table_name": "orders"
    }
  ]
}
)JSON";

        TEST(DbSchemaTest, GetDbSchemaBasic) {
            TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
            testDatabaseUtil.RunInTransaction("GetDbSchemaBasic", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, "people");
                endpointTestHelper.AddAllowedTable(transaction, "orders");

                endpointTestHelper.AddAdminTopLevelTable(transaction, "people");
                endpointTestHelper.AddAdminTopLevelTable(transaction, "orders");

                constexpr std::string_view kPeople = "people";
                databaseInfo.AddTable(kPeople);
                databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);

                constexpr std::string_view kOrders = "orders";
                databaseInfo.AddTable(kOrders);
                databaseInfo.AddColumnPrimaryKey(kOrders, "order_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnForeignKeyRef(
                    kPeople,
                    "person_id",
                    kOrders,
                    "parent_person_id");

                // Add friendly names and data info
                TableHelpers::AdminTableFriendlyNames adminTableFriendlyNames(databaseHelper);
                adminTableFriendlyNames.AddAdminTableFriendlyName(
                    transaction, kPeople, "People", "description1");
                adminTableFriendlyNames.AddAdminTableFriendlyName(
                    transaction, kOrders, "Orders", "description2");

                TableHelpers::AdminColumnFriendlyNames adminColumnFriendlyNames(databaseHelper);
                adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                    transaction, kPeople, "person_id", "Person ID");
                adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                    transaction, kPeople, "email", "Email");
                adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                    transaction, kOrders, "order_id", "Order ID");
                adminColumnFriendlyNames.AddAdminColumnFriendlyName(
                    transaction, kOrders, "parent_person_id", "My person");

                TableHelpers::AdminColumnDataInfo adminColumnDataInfo(databaseHelper);
                adminColumnDataInfo.AddAdminColumnDataInfo(
                    transaction, kPeople, "person_id", "label1a", "hint1a", "placeHolder1a", "regex1a", "htmlInputType1a", "true", "5", "default1a", "3");
                adminColumnDataInfo.AddAdminColumnDataInfo(
                    transaction, kPeople, "email", "label1b", "hint1b", "placeHolder1b", "regex1b", "htmlInputType1b", "true", "7", "default1b", "5");
                adminColumnDataInfo.AddAdminColumnDataInfo(
                    transaction, kOrders, "order_id", "label2a", "hint2a", "placeHolder2a", "regex2a", "htmlInputType2a", "false", "6", "default2a", "4");
                adminColumnDataInfo.AddAdminColumnDataInfo(
                    transaction, kOrders, "parent_person_id", "label2b", "hint2b", "placeHolder2b", "regex2b", "htmlInputType2b", "false", "8", "default2", "6");

                crow::request req;
                req.url = "/api/get_db_schema";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                EXPECT_THAT(
					Json::Value::FromText(resp.body),
					JsonValueIs(Json::Value::FromText(kTestDatabaseSchema)));
                });
        }

        constexpr std::string_view kTestDatabaseSchemaWithNested = R"JSON(
{
  "display_templates": {},
  "fk_picker_preload_threshold": 50,
  "nested_tables": ["orders"],
  "root_tables": ["people"],
  "tables": [
    {
      "columns": [
        {
          "column_friendly_name": "",
          "column_name": "person_id",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "",
          "label": "",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "t",
          "readonly": "f",
          "regex": "",
          "required": "f",
          "rows": "1",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "",
          "column_name": "email",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "",
          "label": "",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "f",
          "readonly": "f",
          "regex": "",
          "required": "f",
          "rows": "1",
          "type": "VARCHAR",
          "unique": "t"
        }
      ],
      "description": "",
      "foreign_keys": [],
      "primary_key": "person_id",
      "table_friendly_name": "people",
      "table_name": "people"
    },
    {
      "columns": [
        {
          "column_friendly_name": "",
          "column_name": "order_id",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "",
          "label": "",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "t",
          "readonly": "f",
          "regex": "",
          "required": "f",
          "rows": "1",
          "type": "SERIAL",
          "unique": "f"
        },
        {
          "column_friendly_name": "",
          "column_name": "parent_person_id",
          "default_value": "",
          "hidden": "f",
          "hint": "",
          "html_input_type": "",
          "label": "",
          "max_length": "",
          "nullable": "f",
          "place_holder": "",
          "primary_key": "f",
          "readonly": "f",
          "regex": "",
          "required": "f",
          "rows": "1",
          "type": "INT",
          "unique": "f"
        }
      ],
      "description": "",
      "foreign_keys": [
        {
          "column_name": "parent_person_id",
          "parent_column_name": "person_id",
          "parent_table_name": "people"
        }
      ],
      "primary_key": "order_id",
      "table_friendly_name": "orders",
      "table_name": "orders"
    }
  ]
}
)JSON";

        TEST(DbSchemaTest, GetDbSchemaWithNestedTables) {
            TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
            testDatabaseUtil.RunInTransaction("GetDbSchemaWithNestedTables", [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, "people");
                endpointTestHelper.AddAllowedTable(transaction, "orders");

                endpointTestHelper.AddAdminTopLevelTable(transaction, "people");
                endpointTestHelper.AddAdminNestedTable(transaction, "orders");

                constexpr std::string_view kPeople = "people";
                databaseInfo.AddTable(kPeople);
                databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);

                constexpr std::string_view kOrders = "orders";
                databaseInfo.AddTable(kOrders);
                databaseInfo.AddColumnPrimaryKey(kOrders, "order_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnForeignKeyRef(
                    kPeople,
                    "person_id",
                    kOrders,
                    "parent_person_id");

                crow::request req;
                req.url = "/api/get_db_schema";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                EXPECT_THAT(
                    Json::Value::FromText(resp.body),
                    JsonValueIs(Json::Value::FromText(kTestDatabaseSchemaWithNested)));
                });
        }

        TEST(DbSchemaTest, GetDbSchemaWithDisplayTemplates) {
            TestDatabaseUtil testDatabaseUtil(TestDatabaseUtil::SchemaMode::Empty);
            testDatabaseUtil.RunInTransaction("GetDbSchemaWithDisplayTemplates",
                [&](Transaction& transaction) {
                DatabaseHelper databaseHelper = testDatabaseUtil.GetDatabaseHelper();
                EndpointTestHelper endpointTestHelper(transaction, testDatabaseUtil);
                auto databaseInfo = testDatabaseUtil.GetDatabaseInfo();

                endpointTestHelper.AddAllowedTable(transaction, "people");

                endpointTestHelper.AddAdminTopLevelTable(transaction, "people");

                constexpr std::string_view kPeople = "people";
                databaseInfo.AddTable(kPeople);
                databaseInfo.AddColumnPrimaryKey(kPeople, "person_id", DB_TYPE_SERIAL);
                databaseInfo.AddColumnUnique(kPeople, "email", DB_TYPE_STRING);

                // Add a display template
                TableHelpers::AdminTableDisplayTemplate displayTemplateHelper(databaseHelper);
                displayTemplateHelper.SetDisplayTemplate(
                    transaction, "people", "{first_name} {last_name}");

                // Set a custom threshold
                auto secrets = endpointTestHelper.GetSecretsHelper();
                secrets->AddSecret(transaction,
                    Secrets::kFkPickerPreloadThreshold, "75");

                crow::request req;
                req.url = "/api/get_db_schema";
                crow::response resp;
                endpointTestHelper.GetWebApp().GetApp().handle_full(req, resp);

                ASSERT_EQ(resp.code, 200);
                Json::Value body = Json::Value::FromText(resp.body);

                // Verify display_templates
                const auto& templates = body["display_templates"].GetChildren();
                EXPECT_EQ(templates.at("people").Get<std::string>(),
                    "{first_name} {last_name}");

                // Verify fk_picker_preload_threshold
                EXPECT_EQ(body["fk_picker_preload_threshold"].Get<int64_t>(), 75);
            });
        }

    } // namespace
}  // namespace Endpoints
