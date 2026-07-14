#pragma once

#include <functional>
#include <string_view>
#include <string>

namespace Secrets {
namespace Values {

void FillInSecretsStringView(std::function<void(std::string_view, std::string_view)> addSecret);

void FillInSecretsString(std::function<void(const std::string&, const std::string&)> addSecret);

}
}  // namespace Secrets