#include "endpoints/admin_notify_admin_alerts.h"

#include <string>

#include <gtest/gtest.h>

#include "business_logic/auth/server_config.h"
#include "endpoints/scheduled_job_test_support.h"
#include "sql_util/table_helpers/admin_alerts.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"
#include "util/types.h"

// H13 — POST /api/admin/notify_admin_alerts. The digest's rules are tested in
// admin_alerts_digest_test; this is the endpoint around it.
namespace Endpoints {
namespace {

using namespace ScheduledJobTest;

TEST(AdminNotifyAdminAlertsTest, MailsNewAlertsOnceAndARepeatSendsNothing) {
    Auth::ServerConfig::Shutdown();
    Auth::ServerConfig::InitializeTestMode();
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("MailsNewAlertsOnce", [&](Transaction& transaction) {
        EndpointTestHelper helper(transaction, testDb);
        auto secrets = helper.GetSecretsHelper();
        secrets->AddSecret(transaction, Secrets::kAdminAlertsRecipient, "admin@example.com");
        TableHelpers::AdminAlerts(testDb.GetDatabaseHelper()).AddAdminAlert(transaction, "A sample problem");
        SignInAsScheduler(transaction, testDb, helper);
        auto mail = helper.GetMailHelper();
        const size_t mailBefore = mail->GetMessages().size();

        crow::response first = RunJob(helper, "notify_admin_alerts");
        ASSERT_EQ(first.code, 200) << first.body;
        Json::Value body = Json::Value::FromText(first.body);
        EXPECT_EQ(body["alerts_count"].Get<int64_t>(), 1);
        EXPECT_TRUE(body["mail_sent"].Get<bool>());
        const int64_t stamp = body["last_notified_us"].Get<int64_t>();
        EXPECT_GT(stamp, 0);
        EXPECT_EQ(secrets->LookupSecret(transaction, Secrets::kAdminAlertsLastNotifiedUs), StringFromInt(stamp));
        ASSERT_EQ(mail->GetMessages().size(), mailBefore + 1);
        const ::Mail::MailMessage& message = mail->GetMessages().back();
        ASSERT_EQ(message.GetTo().size(), 1u);
        EXPECT_EQ(message.GetTo()[0].address, "admin@example.com");
        EXPECT_NE(message.GetBodyText().find("A sample problem"), std::string::npos);

        crow::response again = RunJob(helper, "notify_admin_alerts");
        ASSERT_EQ(again.code, 200) << again.body;
        body = Json::Value::FromText(again.body);
        EXPECT_EQ(body["alerts_count"].Get<int64_t>(), 0);
        EXPECT_FALSE(body["mail_sent"].Get<bool>());
        EXPECT_EQ(body["last_notified_us"].Get<int64_t>(), stamp);
        EXPECT_EQ(mail->GetMessages().size(), mailBefore + 1);
    });
    Auth::ServerConfig::Shutdown();
}

}  // namespace
}  // namespace Endpoints
