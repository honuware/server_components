#include "mail_helper.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "mail_helper_test_util.h"
#include "util/secrets/secret_keys.h"
#include "util/secrets/secrets_helper_test_util.h"

namespace Mail {
namespace {

TEST(MailHelperTest, SendMessage) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    MailAddress from{ "Knotty Yoga", "knottyyogaandspa@gmail.com" };
    MailAddress to{ "Mason Bendixen", "masonbendixen@hotmail.com" };
    MailMessage message(from, to);
    message.SetSubject("Test Subject");
    message.SetBodyText("This is a test email body.");
    message.SetBodyHtml("<h1>This is a test email body.</h1><p>The body!</p>");
    MailHelperPtr mailHelper = MakeMailHelper(
        secrets->LookupSecretTest(Secrets::kMailServerName),
        atoi(std::string(secrets->LookupSecretTest(Secrets::kMailServerPort)).c_str()), 
        secrets->LookupSecretTest(Secrets::kMailAppPassword),
        ParseMailAuthMethod(secrets->LookupSecretTest(Secrets::kMailServerMethod)));
    EXPECT_NO_THROW(mailHelper->SendMail(message));
}

TEST(MailHelperTest, SendMessageInvalidMethod) {
    auto secrets = Secrets::Test::MakeTestSecretsHelper();
    MailAddress from{ "Knotty Yoga", "knottyyogaandspa@gmail.com" };
    MailAddress to{ "Mason Bendixen", "masonbendixen@hotmail.com" };
    MailMessage message(from, to);
    message.SetSubject("Test Subject");
    message.SetBodyText("This is a test email body.");
    message.SetBodyHtml("<h1>This is a test email body.</h1><p>The body!</p>");
    MailHelperPtr mailHelper = MakeMailHelper(
        secrets->LookupSecretTest(Secrets::kMailServerName),
        atoi(std::string(secrets->LookupSecretTest(Secrets::kMailServerPort)).c_str()),
        secrets->LookupSecretTest(Secrets::kMailAppPassword),
        static_cast<MailAuthMethod>(-1));
    try {
        mailHelper->SendMail(message);
        FAIL() << "Expected std::invalid_argument";
    } catch(const std::invalid_argument& e) {
        EXPECT_STREQ("ConvertAuthMethod - Unsupported MailAuthMethod.", e.what());
    }
    catch (...) {
        FAIL() << "Expected std::invalid_argument";
    }
}

TEST(MailHelperTest, ParseMailAuthMethodBasic) {
    ASSERT_EQ(ParseMailAuthMethod("login"), MailAuthMethod::MAIL_AUTH_METHOD_LOGIN);
    ASSERT_EQ(ParseMailAuthMethod("tls"), MailAuthMethod::MAIL_AUTH_METHOD_TLS);
}

TEST(MailHelperTest, ParseMailAuthMethodInvalid) {
    try {
        ASSERT_EQ(ParseMailAuthMethod("invalid"), MailAuthMethod::MAIL_AUTH_METHOD_LOGIN);
        FAIL() << "Expected std::invalid_argument";
    }
    catch (const std::invalid_argument& e) {
        EXPECT_STREQ("ParseMailAuthMethod - Unsupported MailAuthMethod string.", e.what());
    }
    catch (...) {
        FAIL() << "Expected std::invalid_argument";
    }
}

TEST(MailHelperTest, TestMailHelperSend) {
    Mail::Test::TestMailHelperPtr testMailHelper = Mail::Test::MakeTestMailHelper();
    MailAddress from{ "Knotty Yoga", "knottyyogaandspa@gmail.com" };
    MailAddress to{ "Mason Bendixen", "masonbendixen@hotmail.com" };
    MailMessage message(from, to);
    message.SetSubject("Test Subject");
    message.SetBodyText("This is a test email body.");
    message.SetBodyHtml("<h1>This is a test email body.</h1><p>The body!</p>");
    MailMessage message2(message);
    message2.SetSubject("Test Subject 2");
    message2.SetBodyText("This is a second test email body.");
    message2.SetBodyHtml("<h1>This is a second test email body.</h1><p>The body!</p>");
    testMailHelper->SendMail(message);
    ASSERT_EQ(testMailHelper->GetMessages().size(), 1);
    ASSERT_TRUE(testMailHelper->GetMessages()[0] == message);
    ASSERT_EQ(testMailHelper->GetMessages()[0], message);
    testMailHelper->SendMail(message2);
    ASSERT_EQ(testMailHelper->GetMessages().size(), 2);
    ASSERT_EQ(testMailHelper->GetMessages()[0], message);
    ASSERT_EQ(testMailHelper->GetMessages()[1], message2);
}

TEST(MailHelperTest, AddAttachmentBasic) {
    MailAddress from{ "Sender", "sender@test.com" };
    MailAddress to{ "Recipient", "recipient@test.com" };
    MailMessage message(from, to);

    EXPECT_EQ(message.GetAttachments().size(), 0u);

    message.AddAttachment("booking.ics", "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n", "calendar");

    ASSERT_EQ(message.GetAttachments().size(), 1u);
    EXPECT_EQ(message.GetAttachments()[0].filename, "booking.ics");
    EXPECT_EQ(message.GetAttachments()[0].content, "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");
    EXPECT_EQ(message.GetAttachments()[0].contentType, "calendar");
}

TEST(MailHelperTest, AddMultipleAttachments) {
    MailAddress from{ "Sender", "sender@test.com" };
    MailAddress to{ "Recipient", "recipient@test.com" };
    MailMessage message(from, to);

    message.AddAttachment("file1.ics", "content1", "calendar");
    message.AddAttachment("file2.txt", "content2", "plain");

    ASSERT_EQ(message.GetAttachments().size(), 2u);
    EXPECT_EQ(message.GetAttachments()[0].filename, "file1.ics");
    EXPECT_EQ(message.GetAttachments()[1].filename, "file2.txt");
    EXPECT_EQ(message.GetAttachments()[0].content, "content1");
    EXPECT_EQ(message.GetAttachments()[1].content, "content2");
}

TEST(MailHelperTest, CopyMessagePreservesAttachments) {
    MailAddress from{ "Sender", "sender@test.com" };
    MailAddress to{ "Recipient", "recipient@test.com" };
    MailMessage message(from, to);
    message.SetSubject("Test");
    message.AddAttachment("booking.ics", "ical-content", "calendar");

    MailMessage copy(message);

    ASSERT_EQ(copy.GetAttachments().size(), 1u);
    EXPECT_EQ(copy.GetAttachments()[0].filename, "booking.ics");
    EXPECT_EQ(copy.GetAttachments()[0].content, "ical-content");
}

TEST(MailHelperTest, MessageEqualityIncludesAttachments) {
    MailAddress from{ "Sender", "sender@test.com" };
    MailAddress to{ "Recipient", "recipient@test.com" };

    MailMessage msg1(from, to);
    msg1.SetSubject("Test");
    msg1.AddAttachment("booking.ics", "content", "calendar");

    MailMessage msg2(from, to);
    msg2.SetSubject("Test");
    msg2.AddAttachment("booking.ics", "content", "calendar");

    EXPECT_EQ(msg1, msg2);

    // Different attachment content should make them unequal
    MailMessage msg3(from, to);
    msg3.SetSubject("Test");
    msg3.AddAttachment("booking.ics", "different", "calendar");

    EXPECT_NE(msg1, msg3);
}

TEST(MailHelperTest, MessageEqualityNoAttachments) {
    MailAddress from{ "Sender", "sender@test.com" };
    MailAddress to{ "Recipient", "recipient@test.com" };

    MailMessage msg1(from, to);
    msg1.SetSubject("Test");

    MailMessage msg2(from, to);
    msg2.SetSubject("Test");

    EXPECT_EQ(msg1, msg2);

    // Adding attachment makes them unequal
    msg2.AddAttachment("file.ics", "data", "calendar");
    EXPECT_NE(msg1, msg2);
}

TEST(MailHelperTest, TestMailHelperCapturesAttachments) {
    Mail::Test::TestMailHelperPtr testMailHelper = Mail::Test::MakeTestMailHelper();
    MailAddress from{ "Sender", "sender@test.com" };
    MailAddress to{ "Recipient", "recipient@test.com" };
    MailMessage message(from, to);
    message.SetSubject("Booking Confirmed");
    message.SetBodyHtml("<p>Your booking</p>");
    message.AddAttachment("booking.ics", "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n", "calendar");

    testMailHelper->SendMail(message);

    ASSERT_EQ(testMailHelper->GetMessages().size(), 1u);
    const auto& sent = testMailHelper->GetMessages()[0];
    ASSERT_EQ(sent.GetAttachments().size(), 1u);
    EXPECT_EQ(sent.GetAttachments()[0].filename, "booking.ics");
    EXPECT_EQ(sent.GetAttachments()[0].content, "BEGIN:VCALENDAR\r\nEND:VCALENDAR\r\n");
    EXPECT_EQ(sent.GetAttachments()[0].contentType, "calendar");
    EXPECT_EQ(sent, message);
}

} // namespace {
} // namespace Mail
