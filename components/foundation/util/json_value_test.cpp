
#include "json_value.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <string_view>
#include <typeinfo>
#include <vector>

namespace Json {
namespace {

TEST(JsonValueTest, ConstructorDefaultBasic)
{
	Value v;
	EXPECT_TRUE(v.IsNull());
	EXPECT_TRUE(v.Is<std::nullptr_t>());
}

TEST(JsonValueTest, ConstructorNullptrBasic)
{
	Value v(nullptr);
	EXPECT_TRUE(v.IsNull());
	EXPECT_TRUE(v.Is<std::nullptr_t>());
}

TEST(JsonValueTest, ConstructorBoolBasic)
{
	Value v(true);
	EXPECT_FALSE(v.IsNull());
	EXPECT_TRUE(v.Is<bool>());
	EXPECT_EQ(v.Get<bool>(), true);
}

TEST(JsonValueTest, ConstructorIntBasic)
{
	Value v(42);
	EXPECT_TRUE(v.Is<int64_t>());
	EXPECT_EQ(v.Get<int64_t>(), 42);
}

TEST(JsonValueTest, ConstructorInt64Basic)
{
	Value v(static_cast<int64_t>(1234567890123LL));
	EXPECT_TRUE(v.Is<int64_t>());
	EXPECT_EQ(v.Get<int64_t>(), 1234567890123LL);
}

TEST(JsonValueTest, ConstructorDoubleBasic)
{
	Value v(3.25);
	EXPECT_TRUE(v.Is<double>());
	EXPECT_DOUBLE_EQ(v.Get<double>(), 3.25);
}

TEST(JsonValueTest, ConstructorCStringBasic)
{
	Value v("hello");
	EXPECT_TRUE(v.Is<std::string>());
	EXPECT_EQ(v.Get<std::string>(), "hello");
}

TEST(JsonValueTest, ConstructorStringBasic)
{
	Value v(std::string("world"));
	EXPECT_TRUE(v.Is<std::string>());
	EXPECT_EQ(v.Get<std::string>(), "world");
}

TEST(JsonValueTest, ConstructorStringViewBasic)
{
	std::string_view sv = "sv";
	Value v(sv);
	EXPECT_TRUE(v.Is<std::string>());
	EXPECT_EQ(v.Get<std::string>(), "sv");
}

TEST(JsonValueTest, ConstructorJsonArrayBasic)
{
	JsonArray a{ Value(1), Value("two"), Value(false) };
	Value v(a);
	EXPECT_TRUE(v.IsArray());
	ASSERT_TRUE(v.TryGetArray());
	EXPECT_EQ(v.GetArray().size(), 3u);
	EXPECT_EQ(v.GetArray()[0].Get<int64_t>(), 1);
	EXPECT_EQ(v.GetArray()[1].Get<std::string>(), "two");
	EXPECT_EQ(v.GetArray()[2].Get<bool>(), false);
}

TEST(JsonValueTest, ConstructorJsonArrayEmpty)
{
	JsonArray a;
	Value v(a);
	EXPECT_TRUE(v.IsArray());
	ASSERT_TRUE(v.TryGetArray());
	EXPECT_TRUE(v.GetArray().empty());
	EXPECT_EQ(v.ToString(), "[]");
}

TEST(JsonValueTest, ConstructorJsonObjectBasic)
{
	JsonObject o;
	o["a"] = 1;
	o["b"] = "two";
	Value v(o);
	EXPECT_TRUE(v.HasChildren());
	ASSERT_TRUE(v.TryGetChildren());
	EXPECT_EQ(v.GetChildren().size(), 2u);
	EXPECT_EQ(v["a"].Get<int64_t>(), 1);
	EXPECT_EQ(v["b"].Get<std::string>(), "two");
}

TEST(JsonValueTest, ConstructorJsonObjectEmpty)
{
	JsonObject o;
	Value v(o);
	EXPECT_TRUE(v.HasChildren());
	ASSERT_TRUE(v.TryGetChildren());
	EXPECT_TRUE(v.GetChildren().empty());
	EXPECT_EQ(v.ToString(), "{}");
}

TEST(JsonValueTest, ConstructorIterableContainerBasic)
{
	std::vector<int> ints{ 1, 2, 3 };
	Value v(ints);
	EXPECT_TRUE(v.IsArray());
	ASSERT_TRUE(v.TryGetArray());
	EXPECT_EQ(v.GetArray().size(), 3u);
	EXPECT_EQ(v[0].Get<int64_t>(), 1);
	EXPECT_EQ(v[2].Get<int64_t>(), 3);
}

TEST(JsonValueTest, ConstructorIteratorPairBasic)
{
	std::vector<std::string> ss{ "a", "b" };
	Value v(ss.begin(), ss.end());
	EXPECT_TRUE(v.IsArray());
	ASSERT_TRUE(v.TryGetArray());
	EXPECT_EQ(v.GetArray().size(), 2u);
	EXPECT_EQ(v[0].Get<std::string>(), "a");
	EXPECT_EQ(v[1].Get<std::string>(), "b");
}

TEST(JsonValueTest, ConstructorMapLikeBasic)
{
	std::map<std::string, int> m{ {"x", 1}, {"y", 2} };
	Value v(m);
	EXPECT_TRUE(v.HasChildren());
	ASSERT_TRUE(v.TryGetChildren());
	EXPECT_EQ(v["x"].Get<int64_t>(), 1);
	EXPECT_EQ(v["y"].Get<int64_t>(), 2);
}

TEST(JsonValueTest, ConstructorCrowRValueBasic)
{
	auto r = crow::json::load("{\"n\":5,\"s\":\"x\"}");
	Value v(r);
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["n"].Get<int64_t>(), 5);
	EXPECT_EQ(v["s"].Get<std::string>(), "x");
}

TEST(JsonValueTest, ConstructorCrowWValueBasic)
{
	crow::json::wvalue w;
	w["a"] = 1;
	w["b"] = "two";
	Value v(w);
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["a"].Get<int64_t>(), 1);
	EXPECT_EQ(v["b"].Get<std::string>(), "two");
}

TEST(JsonValueTest, FactoryFromRValueBasic)
{
	auto r = crow::json::load("[1,true,\"s\"]");
	Value v = Value::FromRValue(r);
	EXPECT_TRUE(v.IsArray());
	EXPECT_EQ(v.GetArray().size(), 3u);
	EXPECT_EQ(v[0].Get<int64_t>(), 1);
	EXPECT_EQ(v[1].Get<bool>(), true);
	EXPECT_EQ(v[2].Get<std::string>(), "s");
}

TEST(JsonValueTest, FactoryFromWValueBasic)
{
	crow::json::wvalue w;
	w[0] = 7;
	w[1] = "x";
	Value v = Value::FromWValue(w);
	EXPECT_TRUE(v.IsArray());
	EXPECT_EQ(v.GetArray().size(), 2u);
	EXPECT_EQ(v[0].Get<int64_t>(), 7);
	EXPECT_EQ(v[1].Get<std::string>(), "x");
}

TEST(JsonValueTest, FromTextBasic)
{
	Value v = Value::FromText("{\"n\":5,\"arr\":[true,\"x\"]}");
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["n"].Get<int64_t>(), 5);
	ASSERT_TRUE(v["arr"].IsArray());
	EXPECT_EQ(v["arr"].GetArray().size(), 2u);
	EXPECT_EQ(v["arr"][0].Get<bool>(), true);
	EXPECT_EQ(v["arr"][1].Get<std::string>(), "x");
}

TEST(JsonValueTest, AssignmentNullptrBasic)
{
	Value v(true);
	v = nullptr;
	EXPECT_TRUE(v.IsNull());
}

TEST(JsonValueTest, AssignmentBoolBasic)
{
	Value v;
	v = false;
	EXPECT_TRUE(v.Is<bool>());
	EXPECT_EQ(v.Get<bool>(), false);
}

TEST(JsonValueTest, AssignmentIntBasic)
{
	Value v;
	v = 9;
	EXPECT_TRUE(v.Is<int64_t>());
	EXPECT_EQ(v.Get<int64_t>(), 9);
}

TEST(JsonValueTest, AssignmentInt64Basic)
{
	Value v;
	v = static_cast<int64_t>(-10);
	EXPECT_TRUE(v.Is<int64_t>());
	EXPECT_EQ(v.Get<int64_t>(), -10);
}

TEST(JsonValueTest, AssignmentDoubleBasic)
{
	Value v;
	v = 2.5;
	EXPECT_TRUE(v.Is<double>());
	EXPECT_DOUBLE_EQ(v.Get<double>(), 2.5);
}

TEST(JsonValueTest, AssignmentCStringBasic)
{
	Value v;
	v = "abc";
	EXPECT_TRUE(v.Is<std::string>());
	EXPECT_EQ(v.Get<std::string>(), "abc");
}

TEST(JsonValueTest, AssignmentStringBasic)
{
	Value v;
	v = std::string("xyz");
	EXPECT_TRUE(v.Is<std::string>());
	EXPECT_EQ(v.Get<std::string>(), "xyz");
}

TEST(JsonValueTest, AssignmentStringViewBasic)
{
	Value v;
	std::string_view sv = "sv2";
	v = sv;
	EXPECT_TRUE(v.Is<std::string>());
	EXPECT_EQ(v.Get<std::string>(), "sv2");
}

TEST(JsonValueTest, AssignmentJsonArrayBasic)
{
	Value v;
	JsonArray a{ Value(1), Value(2) };
	v = a;
	EXPECT_TRUE(v.IsArray());
	EXPECT_EQ(v.GetArray().size(), 2u);
}

TEST(JsonValueTest, AssignmentJsonObjectBasic)
{
	Value v;
	JsonObject o;
	o["k"] = 1;
	v = o;
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["k"].Get<int64_t>(), 1);
}

TEST(JsonValueTest, AssignmentCrowRValueBasic)
{
	Value v;
	auto r = crow::json::load("{\"x\":1}");
	v = r;
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["x"].Get<int64_t>(), 1);
}

TEST(JsonValueTest, AssignmentCrowWValueBasic)
{
	Value v;
	crow::json::wvalue w;
	w["x"] = "y";
	v = w;
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["x"].Get<std::string>(), "y");
}

TEST(JsonValueTest, AssignmentIterableContainerBasic)
{
	Value v;
	std::vector<int> ints{ 4, 5 };
	v = ints;
	EXPECT_TRUE(v.IsArray());
	EXPECT_EQ(v.GetArray().size(), 2u);
	EXPECT_EQ(v[1].Get<int64_t>(), 5);
}

TEST(JsonValueTest, AssignmentMapLikeBasic)
{
	Value v;
	std::map<std::string, std::string> m{ {"a", "b"} };
	v = m;
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v["a"].Get<std::string>(), "b");
}

TEST(JsonValueTest, IsNullBasic)
{
	EXPECT_TRUE(Value(nullptr).IsNull());
	EXPECT_FALSE(Value(false).IsNull());
}

TEST(JsonValueTest, IsArrayBasic)
{
	EXPECT_TRUE(Value(JsonArray{}).IsArray());
	EXPECT_FALSE(Value(JsonObject{}).IsArray());
}

TEST(JsonValueTest, HasChildrenBasic)
{
	EXPECT_TRUE(Value(JsonObject{}).HasChildren());
	EXPECT_FALSE(Value(JsonArray{}).HasChildren());
}

TEST(JsonValueTest, OperatorEqualsBasic)
{
	// Every supported type: equality on same value
	EXPECT_TRUE(Value() == Value());
	EXPECT_TRUE(Value(nullptr) == Value(nullptr));
	EXPECT_TRUE(Value(true) == Value(true));
	EXPECT_TRUE(Value(false) == Value(false));
	EXPECT_TRUE(Value(42) == Value(42));
	EXPECT_TRUE(Value(static_cast<int64_t>(42)) == Value(static_cast<int64_t>(42)));
	EXPECT_TRUE(Value(3.25) == Value(3.25));
	EXPECT_TRUE(Value("hello") == Value("hello"));
	EXPECT_TRUE(Value(std::string("world")) == Value(std::string("world")));
	EXPECT_TRUE(Value(std::string_view("sv")) == Value(std::string_view("sv")));

	EXPECT_TRUE(Value(JsonArray{}) == Value(JsonArray{}));
	EXPECT_TRUE(Value(JsonArray{ Value(1), Value("two"), Value(false) }) ==
		Value(JsonArray{ Value(1), Value("two"), Value(false) }));

	EXPECT_TRUE(Value(JsonObject{}) == Value(JsonObject{}));
	EXPECT_TRUE(Value(JsonObject{ {"a", Value(1)}, {"b", Value("two")} }) ==
		Value(JsonObject{ {"a", Value(1)}, {"b", Value("two")} }));

	// Every supported type: mismatch should be false for operator==
	EXPECT_FALSE(Value(nullptr) == Value(true));
	EXPECT_FALSE(Value(true) == Value(false));
	EXPECT_FALSE(Value(42) == Value(43));
	EXPECT_FALSE(Value(static_cast<int64_t>(42)) == Value(static_cast<int64_t>(43)));
	EXPECT_FALSE(Value(3.25) == Value(3.5));
	EXPECT_FALSE(Value("hello") == Value("hellO"));
	EXPECT_FALSE(Value(std::string("world")) == Value(std::string("World")));
	EXPECT_FALSE(Value(std::string_view("sv")) == Value(std::string_view("sv2")));

	EXPECT_FALSE(Value(JsonArray{ Value(1) }) == Value(JsonArray{ Value(2) }));
	EXPECT_FALSE(Value(JsonArray{ Value(1), Value(2) }) == Value(JsonArray{ Value(1) }));
	EXPECT_FALSE(Value(JsonObject{ {"a", Value(1)} }) == Value(JsonObject{ {"a", Value(2)} }));
	EXPECT_FALSE(Value(JsonObject{ {"a", Value(1)} }) == Value(JsonObject{ {"b", Value(1)} }));

	// Mixed-type comparison should be false (variant alternative differs)
	EXPECT_FALSE(Value(1) == Value(1.0));
}

TEST(JsonValueTest, OperatorNotEqualsBasic)
{
	// Every supported type: mismatch should be true for operator!=
	EXPECT_TRUE(Value() != Value(true));
	EXPECT_TRUE(Value(nullptr) != Value(false));
	EXPECT_TRUE(Value(true) != Value(false));
	EXPECT_TRUE(Value(42) != Value(43));
	EXPECT_TRUE(Value(static_cast<int64_t>(42)) != Value(static_cast<int64_t>(43)));
	EXPECT_TRUE(Value(3.25) != Value(3.5));
	EXPECT_TRUE(Value("hello") != Value("hellO"));
	EXPECT_TRUE(Value(std::string("world")) != Value(std::string("World")));
	EXPECT_TRUE(Value(std::string_view("sv")) != Value(std::string_view("sv2")));
	EXPECT_TRUE(Value(JsonArray{ Value(1) }) != Value(JsonArray{ Value(2) }));
	EXPECT_TRUE(Value(JsonArray{ Value(1), Value(2) }) != Value(JsonArray{ Value(1) }));
	EXPECT_TRUE(Value(JsonObject{ {"a", Value(1)} }) != Value(JsonObject{ {"a", Value(2)} }));
	EXPECT_TRUE(Value(JsonObject{ {"a", Value(1)} }) != Value(JsonObject{ {"b", Value(1)} }));

	// Every supported type: equality should be false for operator!=
	EXPECT_FALSE(Value() != Value());
	EXPECT_FALSE(Value(nullptr) != Value(nullptr));
	EXPECT_FALSE(Value(true) != Value(true));
	EXPECT_FALSE(Value(false) != Value(false));
	EXPECT_FALSE(Value(42) != Value(42));
	EXPECT_FALSE(Value(static_cast<int64_t>(42)) != Value(static_cast<int64_t>(42)));
	EXPECT_FALSE(Value(3.25) != Value(3.25));
	EXPECT_FALSE(Value("hello") != Value("hello"));
	EXPECT_FALSE(Value(std::string("world")) != Value(std::string("world")));
	EXPECT_FALSE(Value(std::string_view("sv")) != Value(std::string_view("sv")));
	EXPECT_FALSE(Value(JsonArray{}) != Value(JsonArray{}));
	EXPECT_FALSE(Value(JsonArray{ Value(1), Value("two"), Value(false) }) !=
		Value(JsonArray{ Value(1), Value("two"), Value(false) }));
	EXPECT_FALSE(Value(JsonObject{}) != Value(JsonObject{}));
	EXPECT_FALSE(Value(JsonObject{ {"a", Value(1)}, {"b", Value("two")} }) !=
		Value(JsonObject{ {"a", Value(1)}, {"b", Value("two")} }));

	// Mixed-type comparison should be true (variant alternative differs)
	EXPECT_TRUE(Value(1) != Value(1.0));
}

TEST(JsonValueTest, StringKeyIndexOperatorBasic)
{
	Value v(JsonObject{});

	// Non-const operator[] inserts if missing
	EXPECT_TRUE(v.HasChildren());
	EXPECT_EQ(v.GetChildren().size(), 0u);

	Value& a = v["a"];
	EXPECT_TRUE(a.IsNull());
	EXPECT_EQ(v.GetChildren().size(), 1u);
	EXPECT_TRUE(v.GetChildren().find("a") != v.GetChildren().end());

	// Mutate through returned reference
	a = 7;
	EXPECT_TRUE(v["a"].Is<int64_t>());
	EXPECT_EQ(v["a"].Get<int64_t>(), 7);

	// Existing key returns same element (no growth)
	Value& a2 = v[std::string_view("a")];
	EXPECT_EQ(&a2, &v["a"]);
	EXPECT_EQ(v.GetChildren().size(), 1u);
}

TEST(JsonValueTest, StringKeyIndexOperatorConstBasic)
{
	const Value v(JsonObject{ {"x", Value(1)}, {"y", Value("two")} });

	// Const operator[] returns existing item
	EXPECT_EQ(v["x"].Get<int64_t>(), 1);
	EXPECT_EQ(v["y"].Get<std::string>(), "two");

	// Missing key throws
	EXPECT_THROW((void)v["missing"], std::out_of_range);
}

TEST(JsonValueTest, IntIndexOperatorBasic)
{
	Value v(JsonArray{ Value(1), Value("two"), Value(false) });

	// Non-const index returns mutable reference
	EXPECT_EQ(v[0].Get<int64_t>(), 1);
	v[0] = 5;
	EXPECT_EQ(v[0].Get<int64_t>(), 5);

	// Bounds checked
	EXPECT_THROW((void)v[3], std::out_of_range);
}

TEST(JsonValueTest, IntIndexOperatorConstBasic)
{
	const Value v(JsonArray{ Value(1), Value("two") });
	EXPECT_EQ(v[0].Get<int64_t>(), 1);
	EXPECT_EQ(v[1].Get<std::string>(), "two");
	EXPECT_THROW((void)v[2], std::out_of_range);
}

TEST(JsonValueTest, HasChildBasic)
{
	// Non-object should return false and not set out param
	Value nonObj(Value(5));
	Value* out = reinterpret_cast<Value*>(0x1);
	EXPECT_FALSE(nonObj.HasChild("a", &out));
	EXPECT_EQ(out, reinterpret_cast<Value*>(0x1));

	Value v(JsonObject{ {"a", Value(1)}, {"b", Value("two")} });

	Value* outA = nullptr;
	EXPECT_TRUE(v.HasChild("a", &outA));
	ASSERT_NE(outA, nullptr);
	EXPECT_EQ(outA->Get<int64_t>(), 1);

	// Pointer is to actual stored element (mutating updates object)
	*outA = 9;
	EXPECT_EQ(v["a"].Get<int64_t>(), 9);

	// Missing key: false and out not modified
	Value* outMissing = reinterpret_cast<Value*>(0x1);
	EXPECT_FALSE(v.HasChild("missing", &outMissing));
	EXPECT_EQ(outMissing, reinterpret_cast<Value*>(0x1));

	// If outValue == nullptr, still returns correct presence
	EXPECT_TRUE(v.HasChild("b", nullptr));
}

TEST(JsonValueTest, HasChildConstBasic)
{
	const Value nonObj(Value(5));
	const Value* out = reinterpret_cast<const Value*>(0x1);
	EXPECT_FALSE(nonObj.HasChild("a", &out));
	EXPECT_EQ(out, reinterpret_cast<const Value*>(0x1));

	const Value v(JsonObject{ {"a", Value(1)}, {"b", Value("two")} });
	const Value* outA = nullptr;
	EXPECT_TRUE(v.HasChild("a", &outA));
	ASSERT_NE(outA, nullptr);
	EXPECT_EQ(outA->Get<int64_t>(), 1);

	const Value* outMissing = reinterpret_cast<const Value*>(0x1);
	EXPECT_FALSE(v.HasChild("missing", &outMissing));
	EXPECT_EQ(outMissing, reinterpret_cast<const Value*>(0x1));

	EXPECT_TRUE(v.HasChild("b", nullptr));
}


TEST(JsonValueTest, IsTemplateBasic)
{
	Value v(1);
	EXPECT_TRUE(v.Is<int64_t>());
	EXPECT_FALSE(v.Is<std::string>());
}

TEST(JsonValueTest, GetTemplateBasic)
{
	Value v(std::string("x"));
	EXPECT_EQ(v.Get<std::string>(), "x");
}

TEST(JsonValueTest, TryGetTemplateBasic)
{
	Value v(1);
	EXPECT_NE(v.TryGet<int64_t>(), nullptr);
	EXPECT_EQ(v.TryGet<std::string>(), nullptr);
}

TEST(JsonValueTest, TryGetChildrenBasic)
{
	Value v(JsonObject{});
	EXPECT_NE(v.TryGetChildren(), nullptr);
	Value v2(JsonArray{});
	EXPECT_EQ(v2.TryGetChildren(), nullptr);
}

TEST(JsonValueTest, TryGetArrayBasic)
{
	Value v(JsonArray{});
	EXPECT_NE(v.TryGetArray(), nullptr);
	Value v2(JsonObject{});
	EXPECT_EQ(v2.TryGetArray(), nullptr);
}

TEST(JsonValueTest, GetTypeBasic)
{
	Value v(1);
	EXPECT_EQ(&v.GetType(), &typeid(int64_t));
	Value v2(std::string("x"));
	EXPECT_EQ(&v2.GetType(), &typeid(std::string));
}

TEST(JsonValueTest, GetChildrenBasic)
{
	JsonObject o;
	o["a"] = 1;
	Value v(o);
	EXPECT_EQ(v["a"].Get<int64_t>(), 1);
}

TEST(JsonValueTest, GetArrayBasic)
{
	JsonArray a{ Value(1), Value(2) };
	Value v(a);
	EXPECT_EQ(v.GetArray().size(), 2u);
}

TEST(JsonValueTest, VisitBasic)
{
	Value v(5);
	int seen = 0;
	v.Visit([&](auto& x) {
		using T = std::decay_t<decltype(x)>;
		if constexpr (std::is_same_v<T, int64_t>) seen = static_cast<int>(x);
		});
	EXPECT_EQ(seen, 5);
}

TEST(JsonValueTest, VisitConstBasic)
{
	const Value v(std::string("abc"));
	std::string out;
	v.Visit([&](const auto& x) {
		using T = std::decay_t<decltype(x)>;
		if constexpr (std::is_same_v<T, std::string>) out = x;
		});
	EXPECT_EQ(out, "abc");
}

TEST(JsonValueTest, GetCrowRValueBasic)
{
	Value v(JsonObject{ {"a", Value(1)} });
	crow::json::rvalue r = v.GetCrowRValue();
	EXPECT_EQ(r.t(), crow::json::type::Object);
	EXPECT_EQ(static_cast<int>(r["a"].i()), 1);
}

TEST(JsonValueTest, GetWValueBasic)
{
	Value v(JsonArray{ Value(1), Value("x") });
	crow::json::wvalue w = v.GetCrowWValue();
	auto r = crow::json::load(w.dump());
	ASSERT_EQ(r.t(), crow::json::type::List);
	ASSERT_EQ(r.size(), 2u);
	EXPECT_EQ(static_cast<int>(r[0].i()), 1);
	EXPECT_EQ(static_cast<std::string>(r[1].s()), "x");
}

TEST(JsonValueTest, ToStringNullBasic)
{
	EXPECT_EQ(Value(nullptr).ToString(), "null");
}

TEST(JsonValueTest, ToStringBoolBasic)
{
	EXPECT_EQ(Value(true).ToString(), "true");
	EXPECT_EQ(Value(false).ToString(), "false");
}

TEST(JsonValueTest, ToStringIntBasic)
{
	EXPECT_EQ(Value(12).ToString(), "12");
}

TEST(JsonValueTest, ToStringDoubleBasic)
{
	EXPECT_EQ(Value(1.5).ToString(), "1.5");
}

TEST(JsonValueTest, ToStringDoubleNonFiniteBasic)
{
	EXPECT_EQ(Value(std::numeric_limits<double>::infinity()).ToString(), "null");
	EXPECT_EQ(Value(-std::numeric_limits<double>::infinity()).ToString(), "null");
	EXPECT_EQ(Value(std::numeric_limits<double>::quiet_NaN()).ToString(), "null");
}

TEST(JsonValueTest, ToStringStringBasic)
{
	EXPECT_EQ(Value("hi").ToString(), "\"hi\"");
}

TEST(JsonValueTest, ToStringStringEscapesBasic)
{
	Value v("a\n\"b\\c");
	EXPECT_EQ(v.ToString(), "\"a\\n\\\"b\\\\c\"");
}

TEST(JsonValueTest, ToStringArrayBasic)
{
	Value v(JsonArray{ Value(1), Value(true), Value("x") });
	EXPECT_EQ(v.ToString(), "[1,true,\"x\"]");
}

TEST(JsonValueTest, ToStringArrayEmpty)
{
	Value v(JsonArray{});
	EXPECT_EQ(v.ToString(), "[]");
}

TEST(JsonValueTest, ToStringObjectBasic)
{
	JsonObject o;
	o["a"] = 1;
	o["b"] = "x";
	Value v(o);
	EXPECT_EQ(v.ToString(), "{\"a\":1,\"b\":\"x\"}");
}

TEST(JsonValueTest, ToStringObjectEmpty)
{
	Value v(JsonObject{});
	EXPECT_EQ(v.ToString(), "{}");
}

TEST(JsonValueTest, ToStringNestedBasic)
{
	JsonObject root;
	root["arr"] = JsonArray{ Value(1), Value(JsonObject{ {"k", Value("v")} }) };
	root["null"] = Value(nullptr);
	Value v(root);
	EXPECT_EQ(v.ToString(), "{\"arr\":[1,{\"k\":\"v\"}],\"null\":null}");
}

TEST(JsonValueTest, ToStringSimpleBasic)
{
	EXPECT_EQ(Value("hi").ToSimpleString(), "hi");
	EXPECT_EQ(Value(12).ToSimpleString(), "12");
	EXPECT_EQ(Value(1.5).ToSimpleString(), "1.5");
	EXPECT_EQ(Value(nullptr).ToSimpleString(), "null");
}

} // namespace
} // namespace Json

