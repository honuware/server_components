#include "business_logic/scheduled_jobs/admin_alerts_digest.h"

#include <stdexcept>
#include <string>

#include <gtest/gtest.h>

#include "db_schema/admin_alerts.h"
#include "sql_util/table_helpers/admin_alerts.h"
#include "test/src/util/database_test_helper.h"
#include "util/mail/mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/types.h"

// H13 — the admin-alerts digest (moved from CommunityFinder's EV4 §4.2).
namespace PlatformJobs {
namespace {

class FailingMailHelper : public ::Mail::MailHelper {
public:
    void SendMail(const ::Mail::MailMessage&) override {
        throw std::runtime_error("smtp unavailable");
    }
};

// A site name, sender and recipient, as the seed and the settings page would
// leave them.
Secrets::SecretsHelperPtr ConfiguredSecrets(Transaction& transaction, DatabaseHelper databaseHelper,
                                            std::string_view recipient) {
    Secrets::SecretsHelperPtr secrets = Secrets::MakeSecretsHelper(databaseHelper);
    secrets->AddSecret(transaction, Secrets::kMailSenderName, "Sample Site");
    secrets->AddSecret(transaction, Secrets::kMailSenderAddress, "sender@example.com");
    secrets->AddSecret(transaction, Secrets::kAdminAlertsRecipient, recipient);
    return secrets;
}

std::string LastNotified(Transaction& transaction, Secrets::SecretsHelper& secrets) {
    return secrets.LookupSecret(transaction, Secrets::kAdminAlertsLastNotifiedUs);
}

TEST(AdminAlertsDigestTest, RendersOneLinePerAlertAndTheTotal) {
    KeyValueTableArray alerts = {
        { { std::string(DbSchema::kAdminAlertsCreatedAt), "1000" },
          { std::string(DbSchema::kAdminAlertsDescription), "First problem" } },
        { { std::string(DbSchema::kAdminAlertsCreatedAt), "2000" },
          { std::string(DbSchema::kAdminAlertsDescription), "Second problem" } },
    };
    const std::string body = RenderAdminAlertsDigestBody("Sample Site", alerts);
    EXPECT_EQ(body.rfind("Sample Site admin alerts\r\n", 0), 0u) << body;
    EXPECT_NE(body.find("  [1000] First problem\r\n  [2000] Second problem\r\n"), std::string::npos) << body;
    EXPECT_NE(body.find("Total: 2\r\n"), std::string::npos) << body;
    // mailio wants CRLF everywhere: no bare \n.
    for (size_t at = body.find('\n'); at != std::string::npos; at = body.find('\n', at + 1)) {
        ASSERT_GT(at, 0u);
        EXPECT_EQ(body[at - 1], '\r');
    }
    EXPECT_EQ(NewestAlertCreatedAt(alerts), 2000);
    EXPECT_EQ(NewestAlertCreatedAt({}), 0);
}

TEST(AdminAlertsDigestTest, NoNewAlertsSendsNothing) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("NoNewAlertsSendsNothing", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = ConfiguredSecrets(transaction, db, "admin@example.com");
        ::Mail::Test::TestMailHelperPtr mail = ::Mail::Test::MakeTestMailHelper();

        AdminAlertsDigestResult result = SendAdminAlertsDigest(transaction, db, *secrets, mail);
        EXPECT_EQ(result.alertsCount, 0);
        EXPECT_FALSE(result.mailSent);
        EXPECT_EQ(result.lastNotifiedUs, 0);
        EXPECT_TRUE(mail->GetMessages().empty());
        EXPECT_EQ(LastNotified(transaction, *secrets), "");
    });
}

// New alerts: one mail, then the stamp moves to the newest, so the next run
// sends nothing.
TEST(AdminAlertsDigestTest, NewAlertsAreMailedOnceAndStamped) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("NewAlertsAreMailedOnce", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = ConfiguredSecrets(transaction, db, "admin@example.com");
        TableHelpers::AdminAlerts alerts(db);
        alerts.AddAdminAlert(transaction, "First problem");
        alerts.AddAdminAlert(transaction, "Second problem");
        const int64_t newest = NewestAlertCreatedAt(alerts.GetAdminAlertsSince(transaction, 0));
        ::Mail::Test::TestMailHelperPtr mail = ::Mail::Test::MakeTestMailHelper();

        AdminAlertsDigestResult result = SendAdminAlertsDigest(transaction, db, *secrets, mail);
        EXPECT_EQ(result.alertsCount, 2);
        EXPECT_TRUE(result.mailSent);
        EXPECT_EQ(result.lastNotifiedUs, newest);
        EXPECT_EQ(LastNotified(transaction, *secrets), StringFromInt(newest));
        ASSERT_EQ(mail->GetMessages().size(), 1u);
        const ::Mail::MailMessage& message = mail->GetMessages()[0];
        EXPECT_EQ(message.GetSubject(), "Sample Site admin alerts");
        EXPECT_EQ(message.GetFrom().address, "sender@example.com");
        ASSERT_EQ(message.GetTo().size(), 1u);
        EXPECT_EQ(message.GetTo()[0].address, "admin@example.com");
        EXPECT_NE(message.GetBodyText().find("First problem"), std::string::npos);
        EXPECT_NE(message.GetBodyText().find("Second problem"), std::string::npos);

        AdminAlertsDigestResult again = SendAdminAlertsDigest(transaction, db, *secrets, mail);
        EXPECT_EQ(again.alertsCount, 0);
        EXPECT_FALSE(again.mailSent);
        EXPECT_EQ(again.lastNotifiedUs, newest);
        EXPECT_EQ(mail->GetMessages().size(), 1u);
    });
}

TEST(AdminAlertsDigestTest, AConfiguredSubjectWins) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AConfiguredSubjectWins", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = ConfiguredSecrets(transaction, db, "admin@example.com");
        secrets->AddSecret(transaction, Secrets::kAdminAlertsDigestSubject, "Ops: alerts");
        TableHelpers::AdminAlerts(db).AddAdminAlert(transaction, "A problem");
        ::Mail::Test::TestMailHelperPtr mail = ::Mail::Test::MakeTestMailHelper();

        ASSERT_TRUE(SendAdminAlertsDigest(transaction, db, *secrets, mail).mailSent);
        ASSERT_EQ(mail->GetMessages().size(), 1u);
        EXPECT_EQ(mail->GetMessages()[0].GetSubject(), "Ops: alerts");
    });
}

// No recipient: nothing is sent and the stamp stays, so the alerts are still
// there once someone sets one.
TEST(AdminAlertsDigestTest, AnEmptyRecipientIsANoOp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AnEmptyRecipientIsANoOp", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = ConfiguredSecrets(transaction, db, "");
        TableHelpers::AdminAlerts(db).AddAdminAlert(transaction, "A problem");
        ::Mail::Test::TestMailHelperPtr mail = ::Mail::Test::MakeTestMailHelper();

        AdminAlertsDigestResult result = SendAdminAlertsDigest(transaction, db, *secrets, mail);
        EXPECT_EQ(result.alertsCount, 1);
        EXPECT_FALSE(result.mailSent);
        EXPECT_EQ(result.lastNotifiedUs, 0);
        EXPECT_TRUE(mail->GetMessages().empty());
        EXPECT_EQ(LastNotified(transaction, *secrets), "");

        secrets->AddSecret(transaction, Secrets::kAdminAlertsRecipient, "admin@example.com");
        EXPECT_TRUE(SendAdminAlertsDigest(transaction, db, *secrets, mail).mailSent);
        EXPECT_EQ(mail->GetMessages().size(), 1u);
    });
}

// A failed send throws before the stamp moves, so the next run retries.
TEST(AdminAlertsDigestTest, AFailedSendLeavesTheStamp) {
    TestDatabaseUtil testDb;
    testDb.RunInTransaction("AFailedSendLeavesTheStamp", [&](Transaction& transaction) {
        DatabaseHelper db = testDb.GetDatabaseHelper();
        Secrets::SecretsHelperPtr secrets = ConfiguredSecrets(transaction, db, "admin@example.com");
        TableHelpers::AdminAlerts(db).AddAdminAlert(transaction, "A problem");

        ::Mail::MailHelperPtr failing = std::make_shared<FailingMailHelper>();
        EXPECT_THROW(SendAdminAlertsDigest(transaction, db, *secrets, failing), std::runtime_error);
        EXPECT_THROW(SendAdminAlertsDigest(transaction, db, *secrets, nullptr), std::runtime_error);
        EXPECT_EQ(LastNotified(transaction, *secrets), "");
    });
}

}  // namespace
}  // namespace PlatformJobs
