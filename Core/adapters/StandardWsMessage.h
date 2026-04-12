#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>

class StandardWsMessage {
public:
    StandardWsMessage() {
        payload_["object"] = "";
        payload_["action"] = "";
        payload_["ctx"] = nlohmann::json::object();
    }

    explicit StandardWsMessage(const nlohmann::json& raw)
        : payload_(raw) {
        ensureShape();
    }

    explicit StandardWsMessage(const std::string& text) {
        const auto parsed = nlohmann::json::parse(text, nullptr, false);
        if (!parsed.is_discarded()) {
            payload_ = parsed;
        } else {
            payload_ = nlohmann::json::object();
        }
        ensureShape();
    }

    std::string object() const {
        return payload_.value("object", "");
    }

    std::string action() const {
        return payload_.value("action", "");
    }

    const nlohmann::json& ctx() const {
        return payload_["ctx"];
    }

    template<typename T>
    std::optional<T> get(const std::string& key) const {
        if (payload_["ctx"].contains(key)) {
            try {
                return payload_["ctx"][key].get<T>();
            } catch (...) {
            }
        }
        return std::nullopt;
    }

    std::string dump(int indent = -1) const {
        return payload_.dump(indent);
    }

    bool is_valid() const {
        return !object().empty() && !action().empty();
    }

    const nlohmann::json& json() const {
        return payload_;
    }

    void set_object(const std::string& value) {
        payload_["object"] = value;
    }

    void set_action(const std::string& value) {
        payload_["action"] = value;
    }

    void set_ctx(const nlohmann::json& context) {
        payload_["ctx"] = context;
    }

    template<typename T>
    void set(const std::string& key, const T& value) {
        payload_["ctx"][key] = value;
    }

private:
    void ensureShape() {
        if (!payload_.contains("object")) {
            payload_["object"] = "";
        }
        if (!payload_.contains("action")) {
            payload_["action"] = "";
        }
        if (!payload_.contains("ctx") || !payload_["ctx"].is_object()) {
            payload_["ctx"] = nlohmann::json::object();
        }
    }

private:
    nlohmann::json payload_ = nlohmann::json::object();
};
