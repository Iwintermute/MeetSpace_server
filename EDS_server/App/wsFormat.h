#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <optional>

class StandardWsMessage {
public:
    // Создаём пустое сообщение
    StandardWsMessage() {
        m_json["object"] = "";
        m_json["action"] = "";
        m_json["ctx"] = nlohmann::json::object();
    }

    // Создаём из уже распарсенного json (при получении от клиента)
    explicit StandardWsMessage(const nlohmann::json& raw) : m_json(raw) {
        // Гарантируем наличие обязательных полей
        if (!m_json.contains("object")) m_json["object"] = "";
        if (!m_json.contains("action")) m_json["action"] = "";
        if (!m_json.contains("ctx") || !m_json["ctx"].is_object()) {
            m_json["ctx"] = nlohmann::json::object();
        }
    }

    // Создаём из строки (удобно при получении ws-сообщения)
    explicit StandardWsMessage(const std::string& text) {
        try {
            m_json = nlohmann::json::parse(text);
            // те же гарантии
            if (!m_json.contains("object")) m_json["object"] = "";
            if (!m_json.contains("action")) m_json["action"] = "";
            if (!m_json.contains("ctx") || !m_json["ctx"].is_object()) {
                m_json["ctx"] = nlohmann::json::object();
            }
        }
        catch (...) {
            // в случае ошибки — пустое корректное сообщение
            m_json = nlohmann::json::object();
            m_json["object"] = "";
            m_json["action"] = "";
            m_json["ctx"] = nlohmann::json::object();
        }
    }

    // Получить object
    std::string object() const {
        return m_json.value("object", "");
    }

    // Получить action
    std::string action() const {
        return m_json.value("action", "");
    }

    // Получить ctx целиком
    const nlohmann::json& ctx() const {
        return m_json["ctx"];
    }

    // Удобные геттеры для часто используемых полей в ctx
    template<typename T>
    std::optional<T> get(const std::string& key) const {
        if (m_json["ctx"].contains(key)) {
            try {
                return m_json["ctx"][key].get<T>();
            }
            catch (...) {}
        }
        return std::nullopt;
    }

    // Сериализация обратно в строку (для отправки)
    std::string dump(int indent = -1) const {
        return m_json.dump(indent);
    }

    // Проверка валидности (если нужно отличать сломанные сообщения)
    bool is_valid() const {
        return !object().empty() && !action().empty();
    }

    // Доступ к json целиком, если очень нужно
    const nlohmann::json& json() const {
        return m_json;
    }

    // Мутация (для формирования исходящих сообщений)
    void set_object(const std::string& val) { m_json["object"] = val; }
    void set_action(const std::string& val) { m_json["action"] = val; }
    void set_ctx(const nlohmann::json& ctx) { m_json["ctx"] = ctx; } //Возможность передать ctx как джсон

    // Возможность передать ctx по ключу m_json["ctx"][key] = value;
    template<typename T>
    void set(const std::string& key, const T& value) {
        m_json["ctx"][key] = value;
    }

private:
    nlohmann::json m_json;
};