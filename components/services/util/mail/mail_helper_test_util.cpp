#include "mail_helper_test_util.h"

namespace Mail {

bool operator==(const Mail::MailAddress& lhs, const Mail::MailAddress& rhs) {
    return lhs.name == rhs.name && lhs.address == rhs.address;
}

bool operator!=(const Mail::MailAddress& lhs, const Mail::MailAddress& rhs) {
    return !(lhs == rhs);
}

bool operator==(const Mail::MailAddressList& lhs, const Mail::MailAddressList& rhs) {
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (size_t i = 0; i < lhs.size(); ++i) {
        if (!(lhs[i] == rhs[i])) {
            return false;
        }
    }
    return true;
}

bool operator!=(const Mail::MailAddressList& lhs, const Mail::MailAddressList& rhs) {
    return !(lhs == rhs);
}

bool operator==(const Mail::MailAttachment& lhs, const Mail::MailAttachment& rhs) {
    return lhs.filename == rhs.filename &&
        lhs.content == rhs.content &&
        lhs.contentType == rhs.contentType;
}

bool operator!=(const Mail::MailAttachment& lhs, const Mail::MailAttachment& rhs) {
    return !(lhs == rhs);
}

bool operator==(const Mail::MailMessage& lhs, const Mail::MailMessage& rhs) {
    return lhs.GetFrom() == rhs.GetFrom() &&
        lhs.GetTo() == rhs.GetTo() &&
        lhs.GetSubject() == rhs.GetSubject() &&
        lhs.GetBodyText() == rhs.GetBodyText() &&
        lhs.GetBodyHtml() == rhs.GetBodyHtml() &&
        lhs.GetAttachments() == rhs.GetAttachments();
}

bool operator!=(const Mail::MailMessage& lhs, const Mail::MailMessage& rhs) {
    return !(lhs == rhs);
}
    
namespace Test {

void TestMailHelper::SendMail(const MailMessage& message) {
    mailMessageList_.push_back(message);
}

const MailMessageList& TestMailHelper::GetMessages() const {
    return mailMessageList_;
}

TestMailHelperPtr MakeTestMailHelper() {
    return std::make_shared<TestMailHelper>();
}

}  // namespace Test {
}  // namespace Mail