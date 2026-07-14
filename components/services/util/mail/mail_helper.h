#pragma once

#include <string>
#include <vector>
#include <string>
#include <string_view>
#include <memory>

namespace Secrets {

    class SecretsHelper;
    using SecretsHelperPtr = std::shared_ptr<SecretsHelper>;

}  // namespace Secrets {

class Transaction;

namespace Mail {

struct MailAddress {
    std::string name;
    std::string address;
};

using MailAddressList = std::vector<MailAddress>;

struct MailAttachment {
    std::string filename;     // e.g. "booking.ics"
    std::string content;      // Raw file content
    std::string contentType;  // e.g. "text/calendar"
};

class MailMessage {
public:
    MailMessage() = delete;
    MailMessage(const MailAddress& from, const MailAddressList& to);
    MailMessage(const MailAddress& from, const MailAddress& to);
    MailMessage(const MailMessage&) = default;
    MailMessage& operator=(const MailMessage&) = default;
    ~MailMessage() = default;

    void SetSubject(const std::string_view subject) { subject_ = subject; }
    void SetBodyText(const std::string_view bodyText) { bodyText_ = bodyText; }
    void SetBodyHtml(const std::string_view bodyHtml) { bodyHtml_ = bodyHtml; }
    void AddAttachment(const std::string& filename, const std::string& content,
                       const std::string& contentType);

    const MailAddress& GetFrom() const { return from_; }
    const MailAddressList& GetTo() const { return to_; }
    const std::string& GetSubject() const { return subject_; }
    const std::string& GetBodyText() const { return bodyText_; }
    const std::string& GetBodyHtml() const { return bodyHtml_; }
    const std::vector<MailAttachment>& GetAttachments() const { return attachments_; }

private:
    MailAddress from_;
    MailAddressList to_;
    std::string subject_;
    std::string bodyText_;
    std::string bodyHtml_;
    std::vector<MailAttachment> attachments_;
};

class MailHelper {
public:
    virtual ~MailHelper() = default;

    virtual void SendMail(const MailMessage& message) = 0;

protected:
    MailHelper() = default;
    MailHelper(const MailHelper&) = default;
    MailHelper& operator=(const MailHelper&) = default;
};

using MailHelperPtr = std::shared_ptr<MailHelper>;

enum MailAuthMethod {
    MAIL_AUTH_METHOD_LOGIN,
    MAIL_AUTH_METHOD_TLS
};

MailAuthMethod ParseMailAuthMethod(const std::string_view methodStr);

MailHelperPtr MakeMailHelper(
    const std::string_view server,
    unsigned int port, 
    const std::string_view password,
    MailAuthMethod authMethod);

MailHelperPtr MakeMailHelper(Transaction& transaction, Secrets::SecretsHelperPtr secretsHelper);

} // namespace Mail