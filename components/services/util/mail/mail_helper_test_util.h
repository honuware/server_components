#pragma once

#include <vector>

#include "mail_helper.h"

namespace Mail {

bool operator==(const Mail::MailAddress& lhs, const Mail::MailAddress& rhs);
bool operator!=(const Mail::MailAddress& lhs, const Mail::MailAddress& rhs);
bool operator==(const Mail::MailAddressList& lhs, const Mail::MailAddressList& rhs);
bool operator!=(const Mail::MailAddressList& lhs, const Mail::MailAddressList& rhs);
bool operator==(const Mail::MailAttachment& lhs, const Mail::MailAttachment& rhs);
bool operator!=(const Mail::MailAttachment& lhs, const Mail::MailAttachment& rhs);
bool operator==(const Mail::MailMessage& lhs, const Mail::MailMessage& rhs);
bool operator!=(const Mail::MailMessage& lhs, const Mail::MailMessage& rhs);
    
namespace Test {

using MailMessageList = std::vector<MailMessage>;

class TestMailHelper : public MailHelper {
public:
    TestMailHelper() = default;
    TestMailHelper(const TestMailHelper&) = delete;
    TestMailHelper& operator=(const TestMailHelper&) = delete;
    ~TestMailHelper() override = default;

    void SendMail(const MailMessage& message) override;
    const MailMessageList& GetMessages() const;

private:
    MailMessageList mailMessageList_;
};

using TestMailHelperPtr = std::shared_ptr<TestMailHelper>;

TestMailHelperPtr MakeTestMailHelper();

}  // namespace Test {
}  // namespace Mail