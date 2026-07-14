#pragma once

#include <cstdint>
#include <cmath>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <variant>
#include <vector>

#include <crow/json.h> // crow::json::rvalue, crow::json::wvalue, crow::json::load

namespace Json {

struct Value; // fwd

using JsonArray = std::vector<Value>;
using JsonObject = std::map<std::string, Value>; // deterministic ordering

namespace detail {

// ---- helpers / traits (C++17-friendly)

template <class...>
using void_t = void;

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <class T>
struct is_string_like : std::false_type {};

template <>
struct is_string_like<std::string> : std::true_type {};
template <>
struct is_string_like<std::string_view> : std::true_type {};
template <>
struct is_string_like<const char*> : std::true_type {};
template <>
struct is_string_like<char*> : std::true_type {};

template <class T>
constexpr bool is_string_like_v = is_string_like<remove_cvref_t<T>>::value;

// Detect begin/end
template <class T, class = void>
struct is_iterable : std::false_type {};
template <class T>
struct is_iterable<T, void_t<decltype(std::begin(std::declval<T&>())),
    decltype(std::end(std::declval<T&>()))>> : std::true_type {};
template <class T>
constexpr bool is_iterable_v = is_iterable<T>::value;

// Detect map-like with string key
template <class M, class = void>
struct is_map_string_key : std::false_type {};

template <class M>
struct is_map_string_key<
    M,
    void_t<typename remove_cvref_t<M>::value_type,
    typename remove_cvref_t<M>::key_type,
    typename remove_cvref_t<M>::mapped_type>> {
    using Key = typename remove_cvref_t<M>::key_type;
    static constexpr bool value = std::is_same<remove_cvref_t<Key>, std::string>::value;
};

template <class M>
constexpr bool is_map_string_key_v = is_map_string_key<M>::value;

template <class C>
using iter_ref_t = decltype(*std::begin(std::declval<C&>()));

template <class C>
using elem_t = remove_cvref_t<iter_ref_t<C>>;

// ---- JSON dumping helpers

inline void append_escaped_json_string(std::string& out, std::string_view s) {
    out.push_back('"');
    for (unsigned char uc : s) {
        switch (uc) {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (uc < 0x20) {
                // control character -> \u00XX
                static const char* hex = "0123456789ABCDEF";
                out += "\\u00";
                out.push_back(hex[(uc >> 4) & 0xF]);
                out.push_back(hex[uc & 0xF]);
            }
            else {
                out.push_back(static_cast<char>(uc));
            }
            break;
        }
    }
    out.push_back('"');
}

inline void append_json_number(std::string& out, double d) {
    // JSON doesn't allow NaN/Infinity. Emit "null" to keep output valid JSON.
    if (!std::isfinite(d)) {
        out += "null";
        return;
    }
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << std::setprecision(std::numeric_limits<double>::max_digits10) << d;
    out += oss.str();
}

// ---- Crow -> Json::Value conversion

inline Value FromCrow(const crow::json::rvalue& r); // defined after Value

inline Value FromCrow(const crow::json::wvalue& w);

} // namespace detail

struct Value {
    using Storage = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        JsonArray,
        JsonObject>;

    Storage storage;

    // ---- special members
    Value() = default;
    Value(const Value&) = default;
    Value(Value&&) noexcept = default;
    Value& operator=(const Value&) = default;
    Value& operator=(Value&&) noexcept = default;
    ~Value() = default;

    // ---- scalar constructors (kept implicit for ergonomics)
    Value(std::nullptr_t) : storage(nullptr) {}
    Value(bool b) : storage(b) {}
    Value(int64_t i) : storage(i) {}
    Value(int i) : storage(static_cast<int64_t>(i)) {}
#ifndef _MSC_VER
    // On Linux/GCC, long long is distinct from int64_t (long).
    Value(long long i) : storage(static_cast<int64_t>(i)) {}
#endif
    Value(double d) : storage(d) {}
    Value(const char* s) : storage(std::string(s ? s : "")) {}
    Value(std::string s) : storage(std::move(s)) {}

    // Optional #3: make string_view explicit
    explicit Value(std::string_view sv) : storage(std::string(sv)) {}

    Value(const JsonArray& a) : storage(a) {}
    Value(JsonArray&& a) : storage(std::move(a)) {}
    Value(const JsonObject& o) : storage(o) {}
    Value(JsonObject&& o) : storage(std::move(o)) {}

    // ---- Crow constructors (explicit)
    explicit Value(const crow::json::rvalue& r) : storage(detail::FromCrow(r).storage) {}
    explicit Value(const crow::json::wvalue& w) : storage(detail::FromCrow(w).storage) {}

    // ---- Named factories (explicit conversion boundary; bypasses ctors)
    static Value FromRValue(const crow::json::rvalue& r) {
        Value v;
        v.storage = detail::FromCrow(r).storage;
        return v;
    }

    static Value FromWValue(const crow::json::wvalue& w) {
        Value v;
        v.storage = detail::FromCrow(w).storage;
        return v;
    }

    static Value FromText(std::string_view jsonText) {
        auto r = crow::json::load(jsonText.data(), jsonText.size());
        Value v;
        v.storage = detail::FromCrow(r).storage;
        return v;
    }

    // ---- templated array-container constructor (explicit)
    // Accepts any iterable container whose elements are constructible as Json::Value.
    // Excludes string-like types and map<string,*> types to avoid ambiguity.
    template <class Container,
        std::enable_if_t<
        detail::is_iterable_v<Container> &&
        !detail::is_string_like_v<Container> &&
        !detail::is_map_string_key_v<Container>&&
        std::is_constructible<Value, detail::elem_t<Container>>::value,
        int> = 0>
    explicit Value(const Container& c) : storage(JsonArray{}) {
        auto& a = std::get<JsonArray>(storage);
        for (const auto& e : c) a.emplace_back(Value(e));
    }

    // ---- iterator pair constructor (explicit)
    template <class It,
        class Elem = detail::remove_cvref_t<decltype(*std::declval<It&>())>,
        std::enable_if_t<std::is_constructible<Value, Elem>::value, int> = 0>
    explicit Value(It begin, It end) : storage(JsonArray{}) {
        auto& a = std::get<JsonArray>(storage);
        for (auto it = begin; it != end; ++it) a.emplace_back(Value(*it));
    }

    // ---- templated map-like constructor (explicit)
    // Accepts associative containers with key_type == std::string and mapped_type constructible as Value.
    template <class Map,
        std::enable_if_t<
        detail::is_map_string_key_v<Map>&&
        std::is_constructible<Value, typename detail::remove_cvref_t<Map>::mapped_type>::value,
        int> = 0>
    explicit Value(const Map& m) : storage(JsonObject{}) {
        auto& o = std::get<JsonObject>(storage);
        for (const auto& kv : m) o.emplace(kv.first, Value(kv.second));
    }

    // ---- scalar assignment
    Value& operator=(std::nullptr_t) { storage = nullptr; return *this; }
    Value& operator=(bool b) { storage = b; return *this; }
    Value& operator=(int64_t i) { storage = i; return *this; }
    Value& operator=(int i) { storage = static_cast<int64_t>(i); return *this; }
    Value& operator=(double d) { storage = d; return *this; }
    Value& operator=(const char* s) { storage = std::string(s ? s : ""); return *this; }
    Value& operator=(std::string s) { storage = std::move(s); return *this; }

    // Optional #3: explicit ctor doesn't prevent assignment; provide assignment from string_view too.
    Value& operator=(std::string_view sv) { storage = std::string(sv); return *this; }

    Value& operator=(const JsonArray& a) { storage = a; return *this; }
    Value& operator=(JsonArray&& a) { storage = std::move(a); return *this; }
    Value& operator=(const JsonObject& o) { storage = o; return *this; }
    Value& operator=(JsonObject&& o) { storage = std::move(o); return *this; }

    // ---- Crow assignment (conversion)
    Value& operator=(const crow::json::rvalue& r) { storage = detail::FromCrow(r).storage; return *this; }
    Value& operator=(const crow::json::wvalue& w) { storage = detail::FromCrow(w).storage; return *this; }

    // ---- templated array-container assignment
    template <class Container,
        std::enable_if_t<
        detail::is_iterable_v<Container> &&
        !detail::is_string_like_v<Container> &&
        !detail::is_map_string_key_v<Container>&&
        std::is_constructible<Value, detail::elem_t<Container>>::value,
        int> = 0>
    Value& operator=(const Container& c) {
        JsonArray a;
        for (const auto& e : c) a.emplace_back(Value(e));
        storage = std::move(a);
        return *this;
    }

    // ---- templated map-like assignment
    template <class Map,
        std::enable_if_t<
        detail::is_map_string_key_v<Map>&&
        std::is_constructible<Value, typename detail::remove_cvref_t<Map>::mapped_type>::value,
        int> = 0>
    Value& operator=(const Map& m) {
        JsonObject o;
        for (const auto& kv : m) o.emplace(kv.first, Value(kv.second));
        storage = std::move(o);
        return *this;
    }

    // ---- queries / accessors
    bool IsNull() const { return std::holds_alternative<std::nullptr_t>(storage); }
    bool IsArray() const { return std::holds_alternative<JsonArray>(storage); }
    bool HasChildren() const { return std::holds_alternative<JsonObject>(storage); }

    bool operator==(const Value& other) const {
        return storage == other.storage;
    }
    bool operator!=(const Value& other) const {
        return storage != other.storage;
    }

    Value& operator[](std::string_view key) {
        MakeObject();
        return std::get<JsonObject>(storage)[std::string(key)];
    }

    const Value& operator[](std::string_view key) const {
        const auto& obj = std::get<JsonObject>(storage);
        auto it = obj.find(std::string(key));
        if (it == obj.end()) {
            throw std::out_of_range("Json::Value: key not found in object");
        }
        return it->second;
    }

    Value& operator[](size_t index) {
        MakeArray();
        return std::get<JsonArray>(storage).at(index);
    }
    const Value& operator[](size_t index) const {
        return std::get<JsonArray>(storage).at(index);
    }

    bool HasChild(std::string_view key, Value** outValue = nullptr) {
        if (!HasChildren()) return false;
        auto& obj = std::get<JsonObject>(storage);
        auto it = obj.find(std::string(key));
        if (it == obj.end()) return false;
        if (outValue) *outValue = &it->second;
        return true;
    }

    bool HasChild(std::string_view key, const Value** outValue) const {
        if (!HasChildren()) return false;
        const auto& obj = std::get<JsonObject>(storage);
        auto it = obj.find(std::string(key));
        if (it == obj.end()) return false;
        if (outValue) *outValue = &it->second;
        return true;
    }

    template <class Type>
    bool Is() const {
        using T = std::decay_t<Type>;
        return std::holds_alternative<T>(storage);
    }

    // Throws std::bad_variant_access on mismatch (std::get semantics)
    template <class Type>
    Type& Get() {
        using T = std::decay_t<Type>;
        return std::get<T>(storage);
    }

    template <class Type>
    const Type& Get() const {
        using T = std::decay_t<Type>;
        return std::get<T>(storage);
    }

    // ---- non-throwing access
    template <class Type>
    std::decay_t<Type>* TryGet() {
        using T = std::decay_t<Type>;
        return std::get_if<T>(&storage);
    }

    template <class Type>
    const std::decay_t<Type>* TryGet() const {
        using T = std::decay_t<Type>;
        return std::get_if<T>(&storage);
    }

    JsonObject* TryGetChildren() { return std::get_if<JsonObject>(&storage); }
    const JsonObject* TryGetChildren() const { return std::get_if<JsonObject>(&storage); }

    JsonArray* TryGetArray() { return std::get_if<JsonArray>(&storage); }
    const JsonArray* TryGetArray() const { return std::get_if<JsonArray>(&storage); }

    // ---- runtime type info of the currently held alternative
    const std::type_info& GetType() const {
        return std::visit([](const auto& x) -> const std::type_info& { return typeid(x); }, storage);
    }

    // ---- object / array access (throws if wrong alternative)
    JsonObject& GetChildren() { MakeObject(); return std::get<JsonObject>(storage); }
    const JsonObject& GetChildren() const { return std::get<JsonObject>(storage); }

    JsonArray& GetArray() { MakeArray(); return std::get<JsonArray>(storage); }
    const JsonArray& GetArray() const { return std::get<JsonArray>(storage); }

    // ---- Visit helpers (thin wrappers over std::visit)
    template <class Visitor>
    decltype(auto) Visit(Visitor&& vis) {
        return std::visit(std::forward<Visitor>(vis), storage);
    }

    template <class Visitor>
    decltype(auto) Visit(Visitor&& vis) const {
        return std::visit(std::forward<Visitor>(vis), storage);
    }

    // ---- Crow export (const; returns by value)
    crow::json::rvalue GetCrowRValue() const {
        const std::string s = ToString();
        return crow::json::load(s.c_str(), s.size());
    }

    crow::json::wvalue GetCrowWValue() const {
        return crow::json::wvalue(GetCrowRValue());
    }

    // ---- JSON serialization
    // Returns valid JSON for all types (strings quoted/escaped, arrays/objects serialized).
    // For double NaN/Inf, outputs "null" to keep JSON valid.
    std::string ToString() const {
        std::string out;
        out.reserve(64);
        AppendJson(out);
        return out;
    }

    // Like ToString(), but if the value is a string, returns it unquoted/unescaped.
    std::string ToSimpleString() const {
        if (std::holds_alternative<std::string>(storage)) {
            return std::get<std::string>(storage);
        }
        return ToString();
    }

private:
    void MakeArray() {
        if (!std::holds_alternative<JsonArray>(storage)) {
            storage = JsonArray{}; // overwrite current value
        }
    }
    void MakeObject() {
        if (!std::holds_alternative<JsonObject>(storage)) {
            storage = JsonObject{}; // overwrite current value
        }
    }   
    void AppendJson(std::string& out) const {
        std::visit(
            [&](const auto& x) {
                using T = std::decay_t<decltype(x)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    out += "null";
                }
                else if constexpr (std::is_same_v<T, bool>) {
                    out += (x ? "true" : "false");
                }
                else if constexpr (std::is_same_v<T, int64_t>) {
                    out += std::to_string(x);
                }
                else if constexpr (std::is_same_v<T, double>) {
                    detail::append_json_number(out, x);
                }
                else if constexpr (std::is_same_v<T, std::string>) {
                    detail::append_escaped_json_string(out, x);
                }
                else if constexpr (std::is_same_v<T, JsonArray>) {
                    out.push_back('[');
                    bool first = true;
                    for (const auto& elem : x) {
                        if (!first) out.push_back(',');
                        first = false;
                        elem.AppendJson(out);
                    }
                    out.push_back(']');
                }
                else if constexpr (std::is_same_v<T, JsonObject>) {
                    out.push_back('{');
                    bool first = true;
                    for (const auto& kv : x) {
                        if (!first) out.push_back(',');
                        first = false;
                        detail::append_escaped_json_string(out, kv.first);
                        out.push_back(':');
                        kv.second.AppendJson(out);
                    }
                    out.push_back('}');
                }
                else {
                    static_assert(sizeof(T) == 0, "Unhandled Json::Value alternative");
                }
            },
            storage);
    }
};

// Builds a pair of label to JSON values to put in an initializer list.
inline std::pair<std::string, Value> JsonPair(
    std::string_view label, std::string_view value) {
    return std::make_pair<std::string, Value>(
        std::string(label), std::string(value));
}
inline std::pair<std::string, Value> JsonPairValue(
    std::string_view label, Value&& value) {
    return std::make_pair<std::string, Value>(
        std::string(label), std::move(value));
}

// ---- detail::FromCrow implementation (after Value is complete)
namespace detail {

inline Value FromCrow(const crow::json::wvalue& w) {
    // wvalue doesn't provide typed getters publicly; round-trip through JSON text.
    const std::string dumped = w.dump();
    auto r = crow::json::load(dumped.c_str(), dumped.size());
    return FromCrow(r);
}

inline Value FromCrow(const crow::json::rvalue& r) {
    using crow::json::type;

    switch (r.t()) {
    case type::Null:
        return Value(nullptr);

    case type::True:
        return Value(true);

    case type::False:
        return Value(false);

    case type::String:
        return Value(std::string(r.s()));

    case type::Number: {
        // Prefer integer when Crow says it's an integer; otherwise store double.
        // If it's an unsigned integer too large for int64_t, fall back to double.
        const auto nt = r.nt();
        if (nt == crow::json::num_type::Signed_integer) {
            return Value(static_cast<int64_t>(r.i()));
        }
        if (nt == crow::json::num_type::Unsigned_integer) {
            const std::uint64_t u = r.u();
            if (u <= static_cast<std::uint64_t>(std::numeric_limits<int64_t>::max())) {
                return Value(static_cast<int64_t>(u));
            }
            return Value(static_cast<double>(u));
        }
        return Value(r.d());
    }

    case type::List: {
        JsonArray a;
        a.reserve(r.size());
        for (std::size_t i = 0; i < r.size(); ++i) {
            a.emplace_back(FromCrow(r[i]));
        }
        return Value(std::move(a));
    }

    case type::Object: {
        JsonObject o;
        for (const auto& k : r.keys()) {
            o.emplace(k, FromCrow(r[k]));
        }
        return Value(std::move(o));
    }

    default:
        // Unknown / future Crow types -> keep output valid JSON
        return Value(nullptr);
    }
}

} // namespace detail

} // namespace Json
