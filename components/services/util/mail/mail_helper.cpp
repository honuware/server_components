#include "mail_helper.h"

#include <list>
#include <sstream>

// mailio includes
#include <mailio/message.hpp>
#include <mailio/smtp.hpp>

#include "util/secrets/secrets_helper.h"
#include "util/secrets/secret_keys.h"

#include "sql_util/database_access/transaction.h"

namespace Mail {
   
MailMessage::MailMessage(const MailAddress& from, const MailAddressList& to)
    : from_(from), to_(to) {
}

MailMessage::MailMessage(const MailAddress& from, const MailAddress& to)
    : from_(from), to_{ to } {
}

void MailMessage::AddAttachment(const std::string& filename,
                                const std::string& content,
                                const std::string& contentType) {
    attachments_.push_back({filename, content, contentType});
}

namespace {

class MailHelperImpl : public MailHelper {
public:
    MailHelperImpl() = delete;
    MailHelperImpl(
        const std::string_view server,
        unsigned int port, 
        const std::string_view password,
        MailAuthMethod authMethod);
    MailHelperImpl(const MailHelperImpl&) = default;
    MailHelperImpl& operator=(const MailHelperImpl&) = default;
    ~MailHelperImpl() override = default;

    void SendMail(const MailMessage& message) override;

private:
    std::string server_;
    unsigned int port_ = 0;
    std::string password_;
    MailAuthMethod authMethod_; 
};

MailHelperImpl::MailHelperImpl(
    const std::string_view server,
    unsigned int port, 
    const std::string_view password,
    MailAuthMethod authMethod)
    : server_(server), port_(port), password_(password), authMethod_(authMethod) {
}

mailio::smtps::auth_method_t ConvertAuthMethod(MailAuthMethod method) {
    switch (method) {
        case MailAuthMethod::MAIL_AUTH_METHOD_LOGIN:
            return mailio::smtps::auth_method_t::LOGIN;
        case MailAuthMethod::MAIL_AUTH_METHOD_TLS:
            return mailio::smtps::auth_method_t::START_TLS;
        default:
            throw std::invalid_argument("ConvertAuthMethod - Unsupported MailAuthMethod.");
    }
}

void MailHelperImpl::SendMail(const MailMessage& message) {
    try {
        mailio::message msg;

        // Set From
        msg.from(mailio::mail_address(message.GetFrom().name, message.GetFrom().address));

        // Set To
        for (const auto& to : message.GetTo()) {
            msg.add_recipient(mailio::mail_address(to.name, to.address));
        }

        // Set Subject
        msg.subject(message.GetSubject());

        // Set Body (prefer HTML if set, otherwise text)
        if (!message.GetBodyHtml().empty()) {
            msg.content_type(mailio::message::media_type_t::TEXT, "html", "utf-8");
            msg.content(message.GetBodyHtml());
        }
        else {
            msg.content_type(mailio::message::media_type_t::TEXT, "plain", "utf-8");
            msg.content(message.GetBodyText());
        }

        // Add attachments
        const auto& attachments = message.GetAttachments();
        if (!attachments.empty()) {
            std::list<std::tuple<std::istream&, mailio::string_t, mailio::message::content_type_t>> attList;
            std::vector<std::istringstream> streams;
            streams.reserve(attachments.size());

            for (const auto& att : attachments) {
                streams.emplace_back(att.content);
                mailio::message::content_type_t ct(
                    mailio::message::media_type_t::TEXT, att.contentType.empty() ? "calendar" : att.contentType);
                attList.emplace_back(streams.back(), att.filename, ct);
            }
            msg.attach(attList);
        }

        // Send using SMTP
        mailio::smtps conn(server_, port_);

        conn.authenticate(message.GetFrom().address, password_, ConvertAuthMethod(authMethod_));

        conn.submit(msg);
    }
    catch (const std::exception& ex) {
        throw;
    }
}

}  // namespace {

MailAuthMethod ParseMailAuthMethod(const std::string_view methodStr) {
    if (methodStr == "login") {
        return MailAuthMethod::MAIL_AUTH_METHOD_LOGIN;
    }
    else if (methodStr == "tls") {
        return MailAuthMethod::MAIL_AUTH_METHOD_TLS;
    }
    else {
        throw std::invalid_argument("ParseMailAuthMethod - Unsupported MailAuthMethod string.");
     }
}

MailHelperPtr MakeMailHelper(
    const std::string_view server,
    unsigned int port,
    const std::string_view password,
    MailAuthMethod authMethod) {
    return std::make_shared<MailHelperImpl>(server, port, password, authMethod);
}

MailHelperPtr MakeMailHelper(Transaction& transaction, Secrets::SecretsHelperPtr secretsHelper) {
    std::string server = secretsHelper->LookupSecret(transaction, Secrets::kMailServerName);
    std::string portStr = secretsHelper->LookupSecret(transaction, Secrets::kMailServerPort);
    unsigned int port = static_cast<unsigned int>(std::stoul(portStr));
    std::string password = secretsHelper->LookupSecret(transaction, Secrets::kMailAppPassword);
    std::string methodStr = secretsHelper->LookupSecret(transaction, Secrets::kMailServerMethod);
    MailAuthMethod authMethod = ParseMailAuthMethod(methodStr);
    return MakeMailHelper(server, port, password, authMethod);
}

}  // namespace Mail


