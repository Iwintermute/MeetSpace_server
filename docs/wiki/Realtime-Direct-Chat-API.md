# Realtime Direct Chat API
## Назначение
Документ описывает realtime-action `direct_chat.search_users`, который используется для поиска пользователя по email перед началом direct-чата.
## Endpoint
- `object`: `direct_chat`
- `agent`: `messaging`
- `action`: `search_users`
## Формат запроса
Action вызывается через существующий websocket dispatch-контракт:
```json
{
  "object": "direct_chat",
  "agent": "messaging",
  "action": "search_users",
  "ctx": {
    "query": "oz.chat.b@example.com",
    "limit": 10
  }
}
```
### Поля `ctx`
- `query` — основное поле поисковой строки (email или его часть).
- `limit` — максимальное число записей в выдаче.
### Совместимость входных полей
Для обратной совместимости также поддерживаются поля:
- `emailQuery`
- `email`
Если `query` пустой, используется первое непустое из `emailQuery` и `email`.
## Поведение поиска
- поиск регистронезависимый (`lower(...)`);
- query нормализуется через `trim`;
- пустой query возвращает пустой список пользователей;
- источник данных:
  - `app.user_profiles.email` (основной),
  - `auth.users.email` (fallback);
- текущий пользователь (requester) исключается из выдачи;
- дубликаты по `userId` удаляются;
- сортировка приоритизирует:
  1. точное совпадение email,
  2. совпадение по префиксу,
  3. остальные частичные совпадения;
- `limit` ограничивается в диапазоне `[1..100]` (если не передан, используется значение по умолчанию из команды).
## Формат успешного ответа
В `dispatch_result` возвращается `ok: true`, а полезная нагрузка имеет вид:
```json
{
  "query": "oz.chat.b@example.com",
  "users": [
    {
      "userId": "f9a25664-c27d-475c-9a74-105975f77161",
      "email": "oz.chat.b@example.com",
      "displayName": "oz.chat.b"
    }
  ]
}
```
### Поля пользователя
- `userId` — UUID пользователя.
- `email` — нормализованный email.
- `displayName` — display name из `app.user_profiles` (может быть `null`).
