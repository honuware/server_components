#include "json_value_matcher.h"

#include <string>

JsonWvalueMatcher::JsonWvalueMatcher(const Json::Value& jsonValue)
: jsonValue_(jsonValue) {
}

bool JsonWvalueMatcher::MatchAndExplain(
    Json::Value res, testing::MatchResultListener* listener) const {
    if (jsonValue_.ToString() == res.ToString()) {
        return true;
    }
    *listener << "Value:\n" << res.ToString() << "\nDoes not match:\n" << jsonValue_.ToString();
    return false;
}

void JsonWvalueMatcher::DescribeTo(::std::ostream* os) const {
    *os << "Result set should match the values provided.\n";
    StreamExpected(os);
}

void JsonWvalueMatcher::DescribeNegationTo(::std::ostream* os) const {
    *os << "Result set should not match the values provided.\n";
    StreamExpected(os);
}

void JsonWvalueMatcher::StreamExpected(::std::ostream* os) const {
    *os << jsonValue_.ToString();
}

::testing::Matcher<Json::Value> JsonValueIs(
    const Json::Value& jsonValue) {
    return ::testing::MakeMatcher(new JsonWvalueMatcher(jsonValue));
}

::testing::Matcher<Json::Value> JsonValueIs(
    std::string_view jsonText) {
    return ::testing::MakeMatcher(new JsonWvalueMatcher(Json::Value::FromText(jsonText)));
}

::testing::Matcher<Json::Value> JsonValueIs(
    const char* jsonText) {
    return JsonValueIs(std::string_view(jsonText));
}