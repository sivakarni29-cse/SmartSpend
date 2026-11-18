#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <direct.h>
    #define MAKE_DIR(path) _mkdir(path)
#else
    #include <signal.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>

    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
    #define closesocket close
    #define MAKE_DIR(path) mkdir(path, 0700)
#endif

#include "auth.h"

#define REQUEST_SIZE 16384
#define SOCKET_TIMEOUT_SEC 5

// ========================================
// DYNAMIC STRING BUFFER
// ========================================

typedef struct
{
    char *data;
    size_t length;
    size_t capacity;
} StringBuffer;

static int sbInit(StringBuffer *sb, size_t initialCapacity)
{
    if (initialCapacity < 256)
        initialCapacity = 256;

    sb->data = (char *)malloc(initialCapacity);
    if (!sb->data)
    {
        sb->length = 0;
        sb->capacity = 0;
        return 0;
    }

    sb->data[0] = '\0';
    sb->length = 0;
    sb->capacity = initialCapacity;
    return 1;
}

static void sbFree(StringBuffer *sb)
{
    free(sb->data);
    sb->data = NULL;
    sb->length = 0;
    sb->capacity = 0;
}

static int sbEnsure(StringBuffer *sb, size_t extra)
{
    if (sb->length + extra + 1 <= sb->capacity)
        return 1;

    size_t newCapacity = sb->capacity ? sb->capacity * 2 : 256;
    while (newCapacity < sb->length + extra + 1)
        newCapacity *= 2;

    char *resized = (char *)realloc(sb->data, newCapacity);
    if (!resized)
        return 0;

    sb->data = resized;
    sb->capacity = newCapacity;
    return 1;
}

static int sbAppend(StringBuffer *sb, const char *text)
{
    size_t len = strlen(text);
    if (!sbEnsure(sb, len))
        return 0;

    memcpy(sb->data + sb->length, text, len + 1);
    sb->length += len;
    return 1;
}

static int sbAppendf(StringBuffer *sb, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);

    if (needed < 0)
        return 0;

    if (!sbEnsure(sb, (size_t)needed))
        return 0;

    va_start(args, fmt);
    vsnprintf(sb->data + sb->length, sb->capacity - sb->length, fmt, args);
    va_end(args);

    sb->length += (size_t)needed;
    return 1;
}

// ========================================
// HTTP RESPONSE
// ========================================

static int sendAll(SOCKET client, const char *buffer, size_t length)
{
    size_t sent = 0;

    while (sent < length)
    {
        int result = send(
            client,
            buffer + sent,
            (int)(length - sent),
            0
        );

        if (result <= 0)
            return 0;

        sent += (size_t)result;
    }

    return 1;
}

static void sendResponse(
    SOCKET client,
    int status,
    const char *statusText,
    const char *body
)
{
    if (!body)
        body = "";

    size_t bodyLength = strlen(body);
    char headers[1024];

    int headerLength = snprintf(
        headers,
        sizeof(headers),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "X-Content-Type-Options: nosniff\r\n"
        "Cache-Control: no-store\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n\r\n",
        status,
        statusText,
        bodyLength
    );

    if (headerLength <= 0 || headerLength >= (int)sizeof(headers))
        return;

    if (!sendAll(client, headers, (size_t)headerLength))
        return;

    if (bodyLength > 0)
        sendAll(client, body, bodyLength);
}

// ========================================
// JSON ESCAPING & STRUCTURAL PARSING
// ========================================

static void jsonEscape(
    const char *source,
    char *destination,
    size_t size
)
{
    if (!source || size == 0)
        return;

    size_t j = 0;

    for (size_t i = 0; source[i] && j + 2 < size; i++)
    {
        unsigned char c = (unsigned char)source[i];

        if (c == '"' || c == '\\')
        {
            destination[j++] = '\\';
            destination[j++] = (char)c;
        }
        else if (c == '\n')
        {
            destination[j++] = '\\';
            destination[j++] = 'n';
        }
        else if (c == '\r')
        {
            destination[j++] = '\\';
            destination[j++] = 'r';
        }
        else if (c == '\t')
        {
            destination[j++] = '\\';
            destination[j++] = 't';
        }
        else if (c >= 32)
        {
            destination[j++] = (char)c;
        }
    }

    destination[j] = '\0';
}

static void setJsonMessage(
    StringBuffer *sb,
    const char *field,
    const char *text
)
{
    char escaped[512];
    jsonEscape(text, escaped, sizeof(escaped));
    sb->length = 0;
    sb->data[0] = '\0';
    sbAppendf(sb, "{\"%s\":\"%s\"}", field, escaped);
}

static const char *skipWs(const char *p)
{
    while (*p && isspace((unsigned char)*p))
        p++;
    return p;
}

static const char *skipJsonString(const char *p)
{
    if (*p != '"')
        return NULL;

    p++;
    while (*p)
    {
        if (*p == '\\' && p[1])
        {
            p += 2;
        }
        else if (*p == '"')
        {
            return p + 1;
        }
        else
        {
            p++;
        }
    }

    return NULL;
}

static const char *skipJsonValue(const char *p);

static const char *skipJsonContainer(const char *p, char openCh, char closeCh)
{
    if (*p != openCh)
        return NULL;

    p = skipWs(p + 1);
    if (*p == closeCh)
        return p + 1;

    while (*p)
    {
        if (openCh == '{')
        {
            p = skipJsonString(p);
            if (!p)
                return NULL;
            p = skipWs(p);
            if (*p != ':')
                return NULL;
            p = skipWs(p + 1);
        }

        p = skipJsonValue(p);
        if (!p)
            return NULL;

        p = skipWs(p);
        if (*p == ',')
        {
            p = skipWs(p + 1);
        }
        else if (*p == closeCh)
        {
            return p + 1;
        }
        else
        {
            return NULL;
        }
    }

    return NULL;
}

static const char *skipJsonValue(const char *p)
{
    p = skipWs(p);
    if (!*p)
        return NULL;

    if (*p == '"')
        return skipJsonString(p);
    if (*p == '{')
        return skipJsonContainer(p, '{', '}');
    if (*p == '[')
        return skipJsonContainer(p, '[', ']');

    while (*p && *p != ',' && *p != '}' && *p != ']' && !isspace((unsigned char)*p))
        p++;

    return p;
}

/* Locate the value pointer for a top-level JSON object key */
static const char *findJsonValue(const char *json, const char *key)
{
    if (!json || !key)
        return NULL;

    const char *p = skipWs(json);
    if (*p != '{')
        return NULL;

    p = skipWs(p + 1);
    if (*p == '}')
        return NULL;

    size_t keyLen = strlen(key);

    while (*p)
    {
        if (*p != '"')
            return NULL;

        const char *keyStart = p + 1;
        const char *afterKey = skipJsonString(p);
        if (!afterKey)
            return NULL;

        size_t parsedLen = (size_t)((afterKey - 1) - keyStart);

        p = skipWs(afterKey);
        if (*p != ':')
            return NULL;

        p = skipWs(p + 1);

        if (parsedLen == keyLen && strncmp(keyStart, key, keyLen) == 0)
            return p;

        p = skipJsonValue(p);
        if (!p)
            return NULL;

        p = skipWs(p);
        if (*p == ',')
        {
            p = skipWs(p + 1);
        }
        else
        {
            break;
        }
    }

    return NULL;
}

static int getJsonString(
    const char *json,
    const char *key,
    char *output,
    size_t outputSize
)
{
    const char *p = findJsonValue(json, key);

    if (!p || *p != '"' || outputSize == 0)
        return 0;

    p++;
    size_t i = 0;

    while (*p && *p != '"' && i + 1 < outputSize)
    {
        if (*p == '\\' && p[1])
        {
            p++;

            if (*p == 'n')
                output[i++] = '\n';
            else if (*p == 'r')
                output[i++] = '\r';
            else if (*p == 't')
                output[i++] = '\t';
            else if (*p == 'b')
                output[i++] = '\b';
            else if (*p == 'f')
                output[i++] = '\f';
            else
                output[i++] = *p;

            p++;
        }
        else
        {
            output[i++] = *p++;
        }
    }

    output[i] = '\0';

    return *p == '"';
}

static int isFiniteDouble(double value)
{
    return (value == value) && ((value - value) == 0.0);
}

static int getJsonNumber(
    const char *json,
    const char *key,
    double *value
)
{
    const char *p = findJsonValue(json, key);

    if (!p || *p == '"')
        return 0;

    char *end = NULL;
    double number = strtod(p, &end);

    if (p == end || !isFiniteDouble(number))
        return 0;

    const char *after = skipWs(end);
    if (*after != ',' && *after != '}' && *after != '\0')
        return 0;

    *value = number;
    return 1;
}

/* Parse exact query parameter (e.g. ?id=5 or &month=11) */
static int getQueryNumber(
    const char *path,
    const char *key,
    int *value
)
{
    const char *query = strchr(path, '?');
    if (!query)
        return 0;

    size_t keyLen = strlen(key);
    const char *p = query + 1;

    while (*p)
    {
        if (strncmp(p, key, keyLen) == 0 && p[keyLen] == '=')
        {
            const char *valStart = p + keyLen + 1;
            char *end = NULL;
            long number = strtol(valStart, &end, 10);

            if (valStart == end ||
                (*end != '\0' && *end != '&') ||
                number < 0 || number > 2147483647L)
                return 0;

            *value = (int)number;
            return 1;
        }

        const char *next = strchr(p, '&');
        if (!next)
            break;
        p = next + 1;
    }

    return 0;
}

// ========================================
// CASE-INSENSITIVE HTTP HEADER LOOKUP
// ========================================

static const char *findHeaderValue(
    const char *request,
    const char *headerEnd,
    const char *headerName
)
{
    size_t nameLen = strlen(headerName);
    const char *line = strstr(request, "\r\n");

    while (line && line < headerEnd)
    {
        line += 2;
        if (line >= headerEnd)
            break;

        size_t i = 0;
        while (i < nameLen &&
               line + i < headerEnd &&
               tolower((unsigned char)line[i]) ==
               tolower((unsigned char)headerName[i]))
        {
            i++;
        }

        if (i == nameLen && line + i < headerEnd && line[i] == ':')
        {
            const char *val = line + i + 1;
            while (val < headerEnd && (*val == ' ' || *val == '\t'))
                val++;
            return val;
        }

        line = strstr(line, "\r\n");
    }

    return NULL;
}

// ========================================
// AUTHENTICATION HELPERS
// ========================================

static int getToken(
    const char *request,
    char *token,
    size_t tokenSize
)
{
    const char *headerEnd = strstr(request, "\r\n\r\n");
    if (!headerEnd)
        headerEnd = request + strlen(request);

    const char *header = findHeaderValue(request, headerEnd, "Authorization");
    if (!header)
        return 0;

    if (strncmp(header, "Bearer ", 7) != 0 &&
        strncmp(header, "bearer ", 7) != 0)
        return 0;

    header += 7;

    while (*header == ' ')
        header++;

    size_t i = 0;

    while (*header &&
           *header != '\r' &&
           *header != '\n' &&
           *header != ' ' &&
           i + 1 < tokenSize)
    {
        token[i++] = *header++;
    }

    token[i] = '\0';

    return i == 64;
}

static int authenticateRequest(
    const char *request,
    char *username
)
{
    char token[65];

    if (!getToken(request, token, sizeof(token)))
        return 0;

    return getUserFromToken(
        token,
        username,
        MAX_USERNAME + 1
    );
}

// ========================================
// RECEIVE HTTP REQUEST
// ========================================

static void configureClientTimeouts(SOCKET client)
{
#ifdef _WIN32
    DWORD timeoutMs = SOCKET_TIMEOUT_SEC * 1000;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char *)&timeoutMs, sizeof(timeoutMs));
#else
    struct timeval tv;
    tv.tv_sec = SOCKET_TIMEOUT_SEC;
    tv.tv_usec = 0;
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

static int receiveRequest(
    SOCKET client,
    char *request,
    size_t capacity
)
{
    size_t received = 0;
    int headerLength = -1;
    int contentLength = 0;

    request[0] = '\0';

    while (received < capacity - 1)
    {
        int result = recv(
            client,
            request + received,
            (int)(capacity - received - 1),
            0
        );

        if (result <= 0)
            return 0;

        received += (size_t)result;
        request[received] = '\0';

        if (headerLength == -1)
        {
            char *headerEnd = strstr(request, "\r\n\r\n");

            if (headerEnd)
            {
                headerLength = (int)(headerEnd - request) + 4;

                const char *lengthVal = findHeaderValue(
                    request,
                    headerEnd,
                    "Content-Length"
                );

                if (lengthVal)
                {
                    char *end = NULL;
                    long parsed = strtol(lengthVal, &end, 10);
                    if (end == lengthVal || parsed < 0 ||
                        parsed > (long)(capacity - (size_t)headerLength - 1))
                        return 0;
                    contentLength = (int)parsed;
                }
            }
        }

        if (headerLength != -1 &&
            received >= (size_t)(headerLength + contentLength))
            return 1;
    }

    return 0;
}

// ========================================
// DATE & FOLDER HELPERS
// ========================================

static void createDataFolders(void)
{
    MAKE_DIR("user_expenses");
    MAKE_DIR("user_budgets");
}

static int isLeapYear(int year)
{
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

static int isValidDate(int day, int month, int year)
{
    if (year < 2000 || year > 2100 || month < 1 || month > 12 || day < 1)
        return 0;

    static const int daysInMonth[] = {
        0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    int maxDay = daysInMonth[month];
    if (month == 2 && isLeapYear(year))
        maxDay = 29;

    return day <= maxDay;
}

static int isValidExpenseName(const char *name)
{
    if (!name || name[0] == '\0')
        return 0;

    if (strchr(name, '|') || strchr(name, '\n') || strchr(name, '\r'))
        return 0;

    return 1;
}

// ========================================
// AUTH HANDLERS
// ========================================

static int handleRegister(const char *body, StringBuffer *json)
{
    char username[MAX_USERNAME + 1];
    char password[MAX_PASSWORD + 1];
    char message[200];

    if (!getJsonString(body, "username", username, sizeof(username)) ||
        !getJsonString(body, "password", password, sizeof(password)))
    {
        setJsonMessage(json, "error", "Username and password required");
        return 400;
    }

    if (registerUser(username, password, message, sizeof(message)))
    {
        setJsonMessage(json, "message", message);
        return 201;
    }

    setJsonMessage(json, "error", message);
    return (strstr(message, "already exists") != NULL) ? 409 : 400;
}

static int handleLogin(const char *body, StringBuffer *json)
{
    char username[MAX_USERNAME + 1];
    char password[MAX_PASSWORD + 1];
    char token[65];
    char message[200];

    if (!getJsonString(body, "username", username, sizeof(username)) ||
        !getJsonString(body, "password", password, sizeof(password)))
    {
        setJsonMessage(json, "error", "Username and password required");
        return 400;
    }

    if (loginUser(username, password, token, sizeof(token), message, sizeof(message)))
    {
        char escapedMsg[256];
        char escapedUser[128];
        jsonEscape(message, escapedMsg, sizeof(escapedMsg));
        jsonEscape(username, escapedUser, sizeof(escapedUser));

        sbAppendf(
            json,
            "{\"message\":\"%s\",\"token\":\"%s\",\"username\":\"%s\"}",
            escapedMsg,
            token,
            escapedUser
        );
        return 200;
    }

    setJsonMessage(json, "error", message);
    return 401;
}

static int handleLogout(const char *request, StringBuffer *json)
{
    char token[65];

    if (!getToken(request, token, sizeof(token)))
    {
        setJsonMessage(json, "error", "Login required");
        return 401;
    }

    if (logoutUser(token))
    {
        setJsonMessage(json, "message", "Logout successful");
        return 200;
    }

    setJsonMessage(json, "error", "Invalid session");
    return 401;
}

// ========================================
// USER FILE PATHS
// ========================================

static int getExpenseFilePath(
    const char *username,
    char *path,
    size_t pathSize
)
{
    if (!validUsername(username))
        return 0;

    int length = snprintf(path, pathSize, "user_expenses/%s.txt", username);
    return length > 0 && (size_t)length < pathSize;
}

static int getBudgetFilePath(
    const char *username,
    char *path,
    size_t pathSize
)
{
    if (!validUsername(username))
        return 0;

    int length = snprintf(path, pathSize, "user_budgets/%s.txt", username);
    return length > 0 && (size_t)length < pathSize;
}

// ========================================
// GET USER EXPENSES (DYNAMIC BUFFER + OPTIONAL MONTH/YEAR FILTER)
// ========================================

static void getUserExpenses(
    const char *username,
    int filterMonth,
    int filterYear,
    StringBuffer *json
)
{
    char path[128];

    if (!getExpenseFilePath(username, path, sizeof(path)))
    {
        sbAppend(json, "[]");
        return;
    }

    FILE *file = fopen(path, "r");

    if (!file)
    {
        sbAppend(json, "[]");
        return;
    }

    char line[512];
    char name[100];
    char escapedName[300];

    int id, category, day, month, year;
    double amount;
    int first = 1;

    sbAppend(json, "[");

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(
                line,
                "%d|%99[^|]|%lf|%d|%d|%d|%d",
                &id, name, &amount, &category,
                &day, &month, &year
            ) != 7)
            continue;

        if (id <= 0 || amount <= 0 ||
            category < 1 || category > 5 ||
            !isValidDate(day, month, year))
            continue;

        if (filterMonth > 0 && month != filterMonth)
            continue;

        if (filterYear > 0 && year != filterYear)
            continue;

        jsonEscape(name, escapedName, sizeof(escapedName));

        if (!sbAppendf(
                json,
                "%s{\"id\":%d,\"name\":\"%s\","
                "\"amount\":%.2f,\"category\":%d,"
                "\"day\":%d,\"month\":%d,\"year\":%d}",
                first ? "" : ",",
                id, escapedName, amount,
                category, day, month, year
            ))
            break;

        first = 0;
    }

    sbAppend(json, "]");
    fclose(file);
}

// ========================================
// CATEGORY & DATE READERS
// ========================================

static int readCategory(const char *body, int *category)
{
    double number;

    if (getJsonNumber(body, "category", &number))
    {
        if (number < 1 || number > 5 || number != (int)number)
            return 0;

        *category = (int)number;
        return 1;
    }

    char text[40];

    if (!getJsonString(body, "category", text, sizeof(text)))
        return 0;

    static const char *names[] = {
        "Food", "Travel", "Education", "Shopping", "Other"
    };

    for (int i = 0; i < 5; i++)
    {
        if (strcmp(text, names[i]) == 0)
        {
            *category = i + 1;
            return 1;
        }
    }

    return 0;
}

static int getExpenseDate(
    const char *body,
    int *day,
    int *month,
    int *year
)
{
    time_t now = time(NULL);
    struct tm *current = localtime(&now);

    if (!current)
        return 0;

    *day = current->tm_mday;
    *month = current->tm_mon + 1;
    *year = current->tm_year + 1900;

    double value;

    if (findJsonValue(body, "day") != NULL)
    {
        if (!getJsonNumber(body, "day", &value) || value != (int)value)
            return 0;
        *day = (int)value;
    }

    if (findJsonValue(body, "month") != NULL)
    {
        if (!getJsonNumber(body, "month", &value) || value != (int)value)
            return 0;
        *month = (int)value;
    }

    if (findJsonValue(body, "year") != NULL)
    {
        if (!getJsonNumber(body, "year", &value) || value != (int)value)
            return 0;
        *year = (int)value;
    }

    return isValidDate(*day, *month, *year);
}

// ========================================
// ADD EXPENSE
// ========================================

static int addUserExpense(
    const char *username,
    const char *body,
    StringBuffer *json
)
{
    char path[128];
    char name[100];

    double amount;
    int category;
    int day, month, year;

    if (!getExpenseFilePath(username, path, sizeof(path)))
    {
        setJsonMessage(json, "error", "Invalid account");
        return 400;
    }

    if (!getJsonString(body, "name", name, sizeof(name)) ||
        !getJsonNumber(body, "amount", &amount) ||
        !readCategory(body, &category))
    {
        setJsonMessage(json, "error", "Invalid expense details");
        return 400;
    }

    if (!getExpenseDate(body, &day, &month, &year) ||
        !isValidExpenseName(name) ||
        amount <= 0 || amount > 10000000)
    {
        setJsonMessage(json, "error", "Invalid expense values or date");
        return 400;
    }

    int nextId = 1;
    FILE *file = fopen(path, "r");

    if (file)
    {
        char line[512];
        int existingId;

        while (fgets(line, sizeof(line), file))
        {
            if (sscanf(line, "%d|", &existingId) == 1 &&
                existingId >= nextId)
                nextId = existingId + 1;
        }

        fclose(file);
    }

    file = fopen(path, "a");

    if (!file)
    {
        setJsonMessage(json, "error", "Could not save expense");
        return 500;
    }

    fprintf(file, "%d|%s|%.2f|%d|%d|%d|%d\n",
            nextId, name, amount, category,
            day, month, year);

    fclose(file);

    sbAppendf(json, "{\"message\":\"Expense added\",\"id\":%d}", nextId);
    return 201;
}

// ========================================
// EDIT EXPENSE
// ========================================

static int editUserExpense(
    const char *username,
    const char *body,
    StringBuffer *json
)
{
    char path[128];
    char tempPath[160];
    char name[100];

    double idValue, amount;
    int category, day, month, year;

    if (!getExpenseFilePath(username, path, sizeof(path)))
    {
        setJsonMessage(json, "error", "Invalid account");
        return 400;
    }

    if (!getJsonNumber(body, "id", &idValue) ||
        !getJsonString(body, "name", name, sizeof(name)) ||
        !getJsonNumber(body, "amount", &amount) ||
        !readCategory(body, &category))
    {
        setJsonMessage(json, "error", "Invalid expense details");
        return 400;
    }

    int id = (int)idValue;

    if (id <= 0 || idValue != id ||
        !getExpenseDate(body, &day, &month, &year) ||
        !isValidExpenseName(name) ||
        amount <= 0 || amount > 10000000 ||
        category < 1 || category > 5)
    {
        setJsonMessage(json, "error", "Invalid expense values or date");
        return 400;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE *input = fopen(path, "r");

    if (!input)
    {
        setJsonMessage(json, "error", "Expense not found");
        return 404;
    }

    FILE *output = fopen(tempPath, "w");

    if (!output)
    {
        fclose(input);
        setJsonMessage(json, "error", "Could not update expense");
        return 500;
    }

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), input))
    {
        int oldId, oldCategory, oldDay, oldMonth, oldYear;
        char oldName[100];
        double oldAmount;

        if (sscanf(line, "%d|%99[^|]|%lf|%d|%d|%d|%d",
                   &oldId, oldName, &oldAmount,
                   &oldCategory, &oldDay, &oldMonth,
                   &oldYear) != 7)
            continue;

        if (oldId == id)
        {
            fprintf(output, "%d|%s|%.2f|%d|%d|%d|%d\n",
                    id, name, amount, category,
                    day, month, year);
            found = 1;
        }
        else
        {
            fputs(line, output);
        }
    }

    fclose(input);
    fclose(output);

    if (!found)
    {
        remove(tempPath);
        setJsonMessage(json, "error", "Expense not found");
        return 404;
    }

    if (!atomicReplaceFile(tempPath, path))
    {
        setJsonMessage(json, "error", "Could not finish update");
        return 500;
    }

    setJsonMessage(json, "message", "Expense updated successfully");
    return 200;
}

// ========================================
// DELETE EXPENSE
// ========================================

static int deleteUserExpense(
    const char *username,
    int id,
    StringBuffer *json
)
{
    char path[128];
    char tempPath[160];

    if (id <= 0 ||
        !getExpenseFilePath(username, path, sizeof(path)))
    {
        setJsonMessage(json, "error", "Invalid expense ID");
        return 400;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE *input = fopen(path, "r");

    if (!input)
    {
        setJsonMessage(json, "error", "Expense not found");
        return 404;
    }

    FILE *output = fopen(tempPath, "w");

    if (!output)
    {
        fclose(input);
        setJsonMessage(json, "error", "Could not delete expense");
        return 500;
    }

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), input))
    {
        int existingId;

        if (sscanf(line, "%d|", &existingId) == 1 &&
            existingId == id)
        {
            found = 1;
            continue;
        }

        fputs(line, output);
    }

    fclose(input);
    fclose(output);

    if (!found)
    {
        remove(tempPath);
        setJsonMessage(json, "error", "Expense not found");
        return 404;
    }

    if (!atomicReplaceFile(tempPath, path))
    {
        setJsonMessage(json, "error", "Could not finish deletion");
        return 500;
    }

    setJsonMessage(json, "message", "Expense deleted successfully");
    return 200;
}

// ========================================
// GET MONTHLY BUDGET
// ========================================

static void getUserBudget(
    const char *username,
    int month,
    int year,
    StringBuffer *json
)
{
    char path[128];

    if (month < 1 || month > 12 || year < 2000 || year > 2100 ||
        !getBudgetFilePath(username, path, sizeof(path)))
    {
        sbAppend(json, "{\"budget\":0}");
        return;
    }

    FILE *file = fopen(path, "r");

    if (!file)
    {
        sbAppend(json, "{\"budget\":0}");
        return;
    }

    int storedMonth, storedYear;
    double budget;
    double result = 0;

    while (fscanf(file, "%d|%d|%lf",
                  &storedMonth, &storedYear, &budget) == 3)
    {
        if (storedMonth == month && storedYear == year)
            result = budget;
    }

    fclose(file);

    sbAppendf(json, "{\"budget\":%.2f,\"month\":%d,\"year\":%d}", result, month, year);
}

// ========================================
// SAVE MONTHLY BUDGET
// ========================================

static int saveUserBudget(
    const char *username,
    const char *body,
    StringBuffer *json
)
{
    char path[128];
    char tempPath[160];

    double budget;
    double monthValue, yearValue;

    if (!getBudgetFilePath(username, path, sizeof(path)))
    {
        setJsonMessage(json, "error", "Invalid account");
        return 400;
    }

    if (!getJsonNumber(body, "budget", &budget) &&
        !getJsonNumber(body, "monthlyBudget", &budget))
    {
        setJsonMessage(json, "error", "Budget value required");
        return 400;
    }

    time_t now = time(NULL);
    struct tm *current = localtime(&now);
    if (!current)
    {
        setJsonMessage(json, "error", "Server date unavailable");
        return 500;
    }

    int month = current->tm_mon + 1;
    int year = current->tm_year + 1900;

    if (findJsonValue(body, "month") != NULL)
    {
        if (!getJsonNumber(body, "month", &monthValue) || monthValue != (int)monthValue)
        {
            setJsonMessage(json, "error", "Invalid budget month");
            return 400;
        }
        month = (int)monthValue;
    }

    if (findJsonValue(body, "year") != NULL)
    {
        if (!getJsonNumber(body, "year", &yearValue) || yearValue != (int)yearValue)
        {
            setJsonMessage(json, "error", "Invalid budget year");
            return 400;
        }
        year = (int)yearValue;
    }

    if (budget < 0 || budget > 100000000 ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2100)
    {
        setJsonMessage(json, "error", "Invalid budget values");
        return 400;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE *input = fopen(path, "r");
    FILE *output = fopen(tempPath, "w");

    if (!output)
    {
        if (input)
            fclose(input);

        setJsonMessage(json, "error", "Could not save budget");
        return 500;
    }

    int storedMonth, storedYear;
    double storedBudget;

    if (input)
    {
        while (fscanf(input, "%d|%d|%lf",
                      &storedMonth, &storedYear,
                      &storedBudget) == 3)
        {
            if (storedMonth == month && storedYear == year)
                continue;

            fprintf(output, "%d|%d|%.2f\n",
                    storedMonth, storedYear, storedBudget);
        }

        fclose(input);
    }

    fprintf(output, "%d|%d|%.2f\n", month, year, budget);
    fclose(output);

    if (!atomicReplaceFile(tempPath, path))
    {
        setJsonMessage(json, "error", "Could not finish saving budget");
        return 500;
    }

    setJsonMessage(json, "message", "Budget saved successfully");
    return 200;
}

// ========================================
// STATUS TEXT HELPER
// ========================================

static const char *httpStatusText(int code)
{
    switch (code)
    {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 409: return "Conflict";
        case 500: return "Internal Server Error";
        default:  return "OK";
    }
}

// ========================================
// ROUTE API REQUESTS
// ========================================

static void handleRequest(
    const char *request,
    SOCKET client
)
{
    char method[16] = "";
    char path[256] = "";
    char username[MAX_USERNAME + 1] = "";

    StringBuffer json;
    if (!sbInit(&json, 4096))
    {
        sendResponse(client, 500, "Internal Server Error", "{\"error\":\"Out of memory\"}");
        return;
    }

    const char *body = strstr(request, "\r\n\r\n");
    body = body ? body + 4 : "";

    sscanf(request, "%15s %255s", method, path);

    if (strcmp(method, "OPTIONS") == 0)
    {
        sendResponse(client, 204, "No Content", "");
        sbFree(&json);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/status") == 0)
    {
        sendResponse(client, 200, "OK", "{\"status\":\"SmartSpend C backend connected\"}");
        sbFree(&json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/register") == 0)
    {
        int code = handleRegister(body, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/login") == 0)
    {
        int code = handleLogin(body, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/logout") == 0)
    {
        int code = handleLogout(request, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    if (!authenticateRequest(request, username))
    {
        sendResponse(client, 401, "Unauthorized", "{\"error\":\"Login required\"}");
        sbFree(&json);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/me") == 0)
    {
        char escapedUser[128];
        jsonEscape(username, escapedUser, sizeof(escapedUser));
        sbAppendf(&json, "{\"username\":\"%s\"}", escapedUser);
        sendResponse(client, 200, "OK", json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/api/expenses") == 0 ||
         strncmp(path, "/api/expenses?", 14) == 0))
    {
        int filterMonth = 0;
        int filterYear = 0;
        getQueryNumber(path, "month", &filterMonth);
        getQueryNumber(path, "year", &filterYear);

        getUserExpenses(username, filterMonth, filterYear, &json);
        sendResponse(client, 200, "OK", json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/expenses") == 0)
    {
        int code = addUserExpense(username, body, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "PUT") == 0 &&
        strcmp(path, "/api/expenses") == 0)
    {
        int code = editUserExpense(username, body, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "DELETE") == 0 &&
        strncmp(path, "/api/expenses?", 14) == 0)
    {
        int id = 0;

        if (!getQueryNumber(path, "id", &id))
        {
            sendResponse(client, 400, "Bad Request", "{\"error\":\"Expense ID required\"}");
            sbFree(&json);
            return;
        }

        int code = deleteUserExpense(username, id, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        (strcmp(path, "/api/budget") == 0 ||
         strncmp(path, "/api/budget?", 12) == 0))
    {
        time_t now = time(NULL);
        struct tm *current = localtime(&now);

        int month = current ? (current->tm_mon + 1) : 1;
        int year = current ? (current->tm_year + 1900) : 2026;

        getQueryNumber(path, "month", &month);
        getQueryNumber(path, "year", &year);

        getUserBudget(username, month, year, &json);
        sendResponse(client, 200, "OK", json.data);
        sbFree(&json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/budget") == 0)
    {
        int code = saveUserBudget(username, body, &json);
        sendResponse(client, code, httpStatusText(code), json.data);
        sbFree(&json);
        return;
    }

    sendResponse(client, 404, "Not Found", "{\"error\":\"API endpoint not found\"}");
    sbFree(&json);
}

// ========================================
// MAIN SERVER
// ========================================

int main(void)
{
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Winsock startup failed.\n");
        return 1;
    }
#else
    signal(SIGPIPE, SIG_IGN);
#endif

    createDataFolders();
    loadSessions();

    int port = 8080;
    char *environmentPort = getenv("PORT");

    if (environmentPort)
    {
        int configuredPort = atoi(environmentPort);
        if (configuredPort > 0 && configuredPort <= 65535)
            port = configuredPort;
    }

    SOCKET server = socket(AF_INET, SOCK_STREAM, 0);

    if (server == INVALID_SOCKET)
    {
        printf("Could not create server socket.\n");
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    int reuse = 1;

    setsockopt(
        server,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&reuse,
        sizeof(reuse)
    );

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons((unsigned short)port);

    if (bind(
            server,
            (struct sockaddr *)&address,
            sizeof(address)
        ) == SOCKET_ERROR)
    {
        printf("Could not bind server port %d.\n", port);
        closesocket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (listen(server, 16) == SOCKET_ERROR)
    {
        printf("Could not start listening.\n");
        closesocket(server);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    printf("\nSmartSpend C Server is running!\n");
    printf("Port: %d\n", port);
    printf("Persistent authentication enabled.\n");
    printf("Waiting for requests...\n\n");
    fflush(stdout);

    while (1)
    {
        struct sockaddr_in clientAddress;

#ifdef _WIN32
        int addressLength = sizeof(clientAddress);
#else
        socklen_t addressLength = sizeof(clientAddress);
#endif

        SOCKET client = accept(
            server,
            (struct sockaddr *)&clientAddress,
            &addressLength
        );

        if (client == INVALID_SOCKET)
            continue;

        configureClientTimeouts(client);

        char request[REQUEST_SIZE];

        if (receiveRequest(client, request, sizeof(request)))
        {
            handleRequest(request, client);
        }

        closesocket(client);
    }

    closesocket(server);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
