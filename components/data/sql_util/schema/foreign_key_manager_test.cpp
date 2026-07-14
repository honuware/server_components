#include "foreign_key_manager.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "sql_util/database_common.h"
#include "util/file_util.h"
#include "test/src/util/database_test_helper.h"
#include "test/src/util/json_value_matcher.h"
#include "test/src/util/table_matcher.h"

namespace DbSchema {
    namespace
    {
        using ::testing::Contains;
        using ::testing::ElementsAreArray;
        using ::testing::UnorderedElementsAre;
        using ::testing::Eq;


        TEST(ForeignKeyManagerTest, AddForeignKeyReferenceIsRootTableBasic)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            ASSERT_TRUE(manager.IsRootTable("parentTable1"));
            ASSERT_FALSE(manager.IsRootTable("childTable1"));
        }

        TEST(ForeignKeyManagerTest, AddForeignKeyReferenceAlreadyPresent)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            ASSERT_TRUE(manager.IsRootTable("parentTable1"));
            ASSERT_FALSE(manager.IsRootTable("childTable1"));
        }

        TEST(ForeignKeyManagerTest, AddForeignKeyReferenceAlreadyPresentDifferent)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            try {
                manager.AddForeignKeyReference(
                    { "parentTable2", "parentColumn2" },
                    { "childTable1", "childColumn1" });
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "ForeignKeyManagerImpl::AddForeignKeyReference child"
                    "(childTable1, childColumn1) is already present for "
                    "(parentTable1, parentColumn1) and trying to add "
                    "(parentTable2, parentColumn2)");
            }
        }

        TEST(ForeignKeyManagerTest, GetChildReferencesByPair)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable2", "childColumn2" });
            manager.AddForeignKeyReference(
                { "parentTable2", "parentColumn2" },
                { "childTable3", "childColumn3" });
            EXPECT_THAT(
                manager.GetChildReferences(TableColumnPair{ "parentTable1", "parentColumn1" }),
                UnorderedElementsAre(
                    TableColumnPair({ "childTable1", "childColumn1" }),
                    TableColumnPair({ "childTable2", "childColumn2" })));
            EXPECT_THAT(
                manager.GetChildReferences(TableColumnPair{ "parentTable2", "parentColumn2" }),
                UnorderedElementsAre(
                    TableColumnPair({ "childTable3", "childColumn3" })));
        }

        TEST(ForeignKeyManagerTest, GetChildReferencesByTable)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn2" },
                { "childTable2", "childColumn2" });
            manager.AddForeignKeyReference(
                { "parentTable2", "parentColumn2" },
                { "childTable3", "childColumn3" });
            EXPECT_THAT(
                manager.GetChildReferences("parentTable1"),
                UnorderedElementsAre(
                    TableColumnPair({ "childTable1", "childColumn1" }),
                    TableColumnPair({ "childTable2", "childColumn2" })));
            EXPECT_THAT(
                manager.GetChildReferences("parentTable2"),
                UnorderedElementsAre(
                    TableColumnPair({ "childTable3", "childColumn3" })));
        }

        TEST(ForeignKeyManagerTest, GetForeignReferenceFieldsBasic)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            manager.AddForeignKeyReference(
                { "parentTable2", "parentColumn2" },
                { "childTable1", "childColumn2" });
            manager.AddForeignKeyReference(
                { "parentTable3", "parentColumn3" },
                { "childTable3", "childColumn3" });
            EXPECT_THAT(
                manager.GetForeignReferenceFields("childTable1"),
                UnorderedElementsAre(
                    TableColumnPair({ "parentTable1", "parentColumn1" }),
                    TableColumnPair({ "parentTable2", "parentColumn2" })));
            EXPECT_THAT(
                manager.GetForeignReferenceFields("childTable3"),
                UnorderedElementsAre(
                    TableColumnPair({ "parentTable3", "parentColumn3" })));
        }

        TEST(ForeignKeyManagerTest, HasForeignReferenceBasic)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn2" },
                { "childTable2", "childColumn2" });
            manager.AddForeignKeyReference(
                { "parentTable2", "parentColumn2" },
                { "childTable3", "childColumn3" });
            ASSERT_TRUE(manager.HasForeignReference({ "childTable1", "childColumn1" }));
            ASSERT_TRUE(manager.HasForeignReference({ "childTable3", "childColumn3" }));
            ASSERT_FALSE(manager.HasForeignReference({ "parentTable1", "parentColumn1" }));
        }

        TEST(ForeignKeyManagerTest, GetForeignReferenceOfFieldBasic)
        {
            ForeignKeyManager manager;
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });
            manager.AddForeignKeyReference(
                { "parentTable1", "parentColumn2" },
                { "childTable2", "childColumn2" });
            manager.AddForeignKeyReference(
                { "parentTable2", "parentColumn2" },
                { "childTable3", "childColumn3" });
            EXPECT_EQ(
                manager.GetForeignReferenceOfField({ "childTable1", "childColumn1" }),
                TableColumnPair({ "parentTable1", "parentColumn1" }));
            EXPECT_EQ(
                manager.GetForeignReferenceOfField({ "childTable3", "childColumn3" }),
                TableColumnPair({ "parentTable2", "parentColumn2" }));
        }

        TEST(ForeignKeyManagerTest, GetForeignReferenceOfFieldNotPresent)
        {
            ForeignKeyManager manager;
            try {
                manager.GetForeignReferenceOfField({ "childTable1", "childColumn1" });
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "ForeignKeyManagerImpl::GetForeignReferenceOfField unable to find"
                    " foreign key reference for (childTable1, childColumn1)");
            }
        }

        TEST(ForeignKeyManagerTest, AddForeignKeyReferenceNullable)
        {
            ForeignKeyManager manager;
            TableColumnPair parent{ "parentTable", "id" };
            TableColumnPair child{ "childTable", "parent_id" };

            manager.AddForeignKeyReference(parent, child, true);

            EXPECT_TRUE(manager.HasForeignReference(child));
            EXPECT_TRUE(manager.IsNullable(parent, child));
        }

        TEST(ForeignKeyManagerTest, AddForeignKeyReferenceNotNullable)
        {
            ForeignKeyManager manager;
            TableColumnPair parent{ "parentTable", "id" };
            TableColumnPair child{ "childTable", "parent_id" };

            manager.AddForeignKeyReference(parent, child, false);

            EXPECT_TRUE(manager.HasForeignReference(child));
            EXPECT_FALSE(manager.IsNullable(parent, child));
        }

        TEST(ForeignKeyManagerTest, AddForeignKeyReferenceDefaultNotNullable)
        {
            ForeignKeyManager manager;
            TableColumnPair parent{ "parentTable", "id" };
            TableColumnPair child{ "childTable", "parent_id" };

            manager.AddForeignKeyReference(parent, child);  // No nullable param

            EXPECT_FALSE(manager.IsNullable(parent, child));
        }

        TEST(ForeignKeyManagerTest, CloneIsIndependentDeepCopy)
        {
            ForeignKeyManager original;
            original.AddForeignKeyReference(
                { "parentTable1", "parentColumn1" },
                { "childTable1", "childColumn1" });

            ForeignKeyManager clone = original.Clone();

            // The clone starts with the same references...
            EXPECT_TRUE(clone.HasForeignReference({ "childTable1", "childColumn1" }));

            // ...but adding a reference to the clone does not affect the original.
            clone.AddForeignKeyReference(
                { "parentTable2", "parentColumn2" },
                { "childTable2", "childColumn2" });
            EXPECT_TRUE(clone.HasForeignReference({ "childTable2", "childColumn2" }));
            EXPECT_FALSE(original.HasForeignReference({ "childTable2", "childColumn2" }));

            // ...and adding a reference to the original does not affect the clone.
            original.AddForeignKeyReference(
                { "parentTable3", "parentColumn3" },
                { "childTable3", "childColumn3" });
            EXPECT_TRUE(original.HasForeignReference({ "childTable3", "childColumn3" }));
            EXPECT_FALSE(clone.HasForeignReference({ "childTable3", "childColumn3" }));
        }

        TEST(ForeignKeyManagerTest, IsNullableNotPresent)
        {
            ForeignKeyManager manager;
            try {
                manager.IsNullable(
                    { "parentTable", "id" },
                    { "childTable", "parent_id" });
                FAIL() << "Expected exception was not thrown";
            }
            catch (std::invalid_argument& e) {
                ASSERT_EQ(
                    std::string(e.what()),
                    "ForeignKeyManagerImpl::IsNullable unable to find"
                    " foreign key reference for parent (parentTable, id)"
                    " child (childTable, parent_id)");
            }
        }

    } // namespace {
} //  namespace DbSchema {
