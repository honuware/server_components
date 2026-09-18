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

    // The username this helper would present to SMTP AUTH for a message from
    // `from`. Exposed so the wiring from config_secrets to the transport can be
    // asserted without a live server; the base answer is the historical one
    // (the sender address), which is what a test double also reports.
    virtual std::string SmtpUsernameFor(const MailAddress& from) const {
        return from.address;
    }

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

// Which name to log in with. Gmail authenticates the mailbox, so the sender
// address IS the username and `mail_smtp_username` stays empty; Amazon SES
// issues an IAM-derived AKIA... username that is nothing like an address, so
// there the configured value wins. An empty configured value therefore means
// "the way it always worked", and an existing database without the row keeps
// behaving exactly as before.
std::string ResolveSmtpUsername(
    std::string_view configuredUsername, std::string_view fromAddress);

MailHelperPtr MakeMailHelper(
    const std::string_view server,
    unsigned int port,
    const std::string_view password,
    MailAuthMethod authMethod);

// As above, with an explicit SMTP AUTH username; empty falls back to the
// sender address (ResolveSmtpUsername).
MailHelperPtr MakeMailHelper(
    const std::string_view server,
    unsigned int port,
    const std::string_view smtpUsername,
    const std::string_view password,
    MailAuthMethod authMethod);

MailHelperPtr MakeMailHelper(Transaction& transaction, Secrets::SecretsHelperPtr secretsHelper);

} // namespace Mail