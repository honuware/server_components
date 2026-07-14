#pragma once

#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include <gtest/gtest.h>

#include "util/json_value.h"

class JsonWvalueMatcher : public ::testing::MatcherInterface<Json::Value> {
public:
    JsonWvalueMatcher(const Json::Value& jsonValue);


    bool MatchAndExplain(Json::Value res, testing::MatchResultListener* listener) const override;
    void DescribeTo(::std::ostream* os) const override;
    void DescribeNegationTo(::std::ostream* os) const override;

private:
    void StreamExpected(::std::ostream* os) const;

    Json::Value jsonValue_;
};

::testing::Matcher<Json::Value> JsonValueIs(
    const Json::Value& jsonValue);

::testing::Matcher<Json::Value> JsonValueIs(
    std::string_view jsonText);

::testing::Matcher<Json::Value> JsonValueIs(
    const char* jsonText);
