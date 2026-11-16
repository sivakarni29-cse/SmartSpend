
#include <stdio.h>
#include <stdlib.h>
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
    #include <sys/types.h>
    #include <sys/socket.h>
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
#define DATA_SIZE 32768

// ========================================
// HTTP RESPONSE
// ========================================

void sendResponse(
    SOCKET client,
    int status,
    const char *statusText,
    const char *body
)
{
    char response[DATA_SIZE + 2048];

    int length = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type, Authorization\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n\r\n%s",
        status,
        statusText,
        strlen(body),
        body
    );

    if (length <= 0 || length >= (int)sizeof(response))
        return;

    int sent = 0;

    while (sent < length)
    {
        int result = send(
            client,
            response + sent,
            length - sent,
            0
        );

        if (result <= 0)
            break;

        sent += result;
    }
}

// ========================================
// JSON HELPERS
// ========================================

void jsonEscape(
    const char *source,
    char *destination,
    size_t size
)
{
    size_t j = 0;

    for (size_t i = 0; source[i] && j + 2 < size; i++)
    {
        unsigned char c = (unsigned char)source[i];

        if (c == '"' || c == '\\')
        {
            destination[j++] = '\\';
            destination[j++] = c;
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
            destination[j++] = c;
        }
    }

    destination[j] = '\0';
}

int getJsonString(
    const char *json,
    const char *key,
    char *output,
    size_t outputSize
)
{
    char search[100];

    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);

    if (!p)
        return 0;

    p = strchr(p, ':');

    if (!p)
        return 0;

    p++;

    while (isspace((unsigned char)*p))
        p++;

    if (*p != '"')
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
            else if (*p == 't')
                output[i++] = '\t';
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

int getJsonNumber(
    const char *json,
    const char *key,
    double *value
)
{
    char search[100];

    snprintf(search, sizeof(search), "\"%s\"", key);

    const char *p = strstr(json, search);

    if (!p)
        return 0;

    p = strchr(p, ':');

    if (!p)
        return 0;

    p++;

    while (isspace((unsigned char)*p))
        p++;

    char *end;

    double number = strtod(p, &end);

    if (p == end)
        return 0;

    *value = number;

    return 1;
}

int getQueryNumber(
    const char *path,
    const char *key,
    int *value
)
{
    char search[80];

    snprintf(search, sizeof(search), "%s=", key);

    const char *p = strchr(path, '?');

    if (!p)
        return 0;

    p = strstr(p, search);

    if (!p)
        return 0;

    p += strlen(search);

    char *end;
    long number = strtol(p, &end, 10);

    if (p == end || number < 0 || number > 2147483647)
        return 0;

    *value = (int)number;

    return 1;
}

// ========================================
// AUTHENTICATION
// ========================================

int getToken(
    const char *request,
    char *token,
    size_t tokenSize
)
{
    const char *header = strstr(request, "\r\nAuthorization:");

    if (!header)
        header = strstr(request, "\r\nauthorization:");

    if (!header)
        return 0;

    header += 16;

    while (*header == ' ')
        header++;

    if (strncmp(header, "Bearer ", 7) != 0)
        return 0;

    header += 7;

    size_t i = 0;

    while (*header &&
           *header != '\r' &&
           *header != '\n' &&
           i + 1 < tokenSize)
    {
        token[i++] = *header++;
    }

    token[i] = '\0';

    return i == 64;
}

int authenticateRequest(
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

int receiveRequest(
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

        received += result;
        request[received] = '\0';

        if (headerLength == -1)
        {
            char *headerEnd = strstr(request, "\r\n\r\n");

            if (headerEnd)
            {
                headerLength = (int)(headerEnd - request) + 4;

                char *lengthHeader = strstr(
                    request,
                    "Content-Length:"
                );

                if (lengthHeader && lengthHeader < headerEnd)
                    contentLength = atoi(lengthHeader + 15);

                if (contentLength < 0 ||
                    contentLength > (int)capacity - headerLength - 1)
                    return 0;
            }
        }

        if (headerLength != -1 &&
            received >= (size_t)(headerLength + contentLength))
            return 1;
    }

    return 0;
}

// ========================================
// CREATE DATA FOLDERS
// ========================================

void createDataFolders(void)
{
    MAKE_DIR("users");
    MAKE_DIR("user_expenses");
    MAKE_DIR("user_budgets");
}

// ========================================
// USER REGISTRATION
// ========================================

void handleRegister(const char *body, char *json)
{
    char username[MAX_USERNAME + 1];
    char password[MAX_PASSWORD + 1];
    char message[200];

    if (!getJsonString(body, "username",
                       username, sizeof(username)) ||
        !getJsonString(body, "password",
                       password, sizeof(password)))
    {
        strcpy(json, "{\"error\":\"Username and password required\"}");
        return;
    }

    if (registerUser(
        username,
        password,
        message,
        sizeof(message)))
    {
        snprintf(json, DATA_SIZE,
                 "{\"message\":\"%s\"}", message);
    }
    else
    {
        snprintf(json, DATA_SIZE,
                 "{\"error\":\"%s\"}", message);
    }
}

// ========================================
// USER LOGIN
// ========================================

void handleLogin(const char *body, char *json)
{
    char username[MAX_USERNAME + 1];
    char password[MAX_PASSWORD + 1];
    char token[65];
    char message[200];

    if (!getJsonString(body, "username",
                       username, sizeof(username)) ||
        !getJsonString(body, "password",
                       password, sizeof(password)))
    {
        strcpy(json, "{\"error\":\"Username and password required\"}");
        return;
    }

    if (loginUser(
        username,
        password,
        token,
        sizeof(token),
        message,
        sizeof(message)))
    {
        snprintf(json, DATA_SIZE,
                 "{\"message\":\"%s\",\"token\":\"%s\",\"username\":\"%s\"}",
                 message, token, username);
    }
    else
    {
        snprintf(json, DATA_SIZE,
                 "{\"error\":\"%s\"}", message);
    }
}

// ========================================
// USER LOGOUT
// ========================================

void handleLogout(const char *request, char *json)
{
    char token[65];

    if (!getToken(request, token, sizeof(token)))
    {
        strcpy(json, "{\"error\":\"Login required\"}");
        return;
    }

    if (logoutUser(token))
        strcpy(json, "{\"message\":\"Logout successful\"}");
    else
        strcpy(json, "{\"error\":\"Invalid session\"}");
}

// ========================================
// USER FILE PATHS
// ========================================

int getExpenseFilePath(
    const char *username,
    char *path,
    size_t pathSize
)
{
    if (!validUsername(username))
        return 0;

    int length = snprintf(
        path,
        pathSize,
        "user_expenses/%s.txt",
        username
    );

    return length > 0 && (size_t)length < pathSize;
}

int getBudgetFilePath(
    const char *username,
    char *path,
    size_t pathSize
)
{
    if (!validUsername(username))
        return 0;

    int length = snprintf(
        path,
        pathSize,
        "user_budgets/%s.txt",
        username
    );

    return length > 0 && (size_t)length < pathSize;
}

// ========================================
// GET USER EXPENSES
// ========================================

void getUserExpenses(
    const char *username,
    char *json
)
{
    char path[128];

    if (!getExpenseFilePath(username, path, sizeof(path)))
    {
        strcpy(json, "[]");
        return;
    }

    FILE *file = fopen(path, "r");

    if (!file)
    {
        strcpy(json, "[]");
        return;
    }

    char line[512];
    char name[100];
    char escapedName[300];

    int id, category, day, month, year;
    float amount;

    int offset = 0;
    int first = 1;

    offset += snprintf(json, DATA_SIZE, "[");

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(
            line,
            "%d|%99[^|]|%f|%d|%d|%d|%d",
            &id, name, &amount, &category,
            &day, &month, &year
        ) != 7)
            continue;

        if (id <= 0 || amount <= 0 ||
            category < 1 || category > 5 ||
            day < 1 || day > 31 ||
            month < 1 || month > 12 ||
            year < 2000 || year > 2100)
            continue;

        jsonEscape(name, escapedName, sizeof(escapedName));

        if (offset >= DATA_SIZE - 400)
            break;

        offset += snprintf(
            json + offset,
            DATA_SIZE - offset,
            "%s{\"id\":%d,\"name\":\"%s\","
            "\"amount\":%.2f,\"category\":%d,"
            "\"day\":%d,\"month\":%d,\"year\":%d}",
            first ? "" : ",",
            id, escapedName, amount,
            category, day, month, year
        );

        first = 0;
    }

    snprintf(json + offset, DATA_SIZE - offset, "]");

    fclose(file);
}

// ========================================
// CATEGORY READER
// ========================================

int readCategory(const char *body, int *category)
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

    const char *names[] = {
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

// ========================================
// READ EXPENSE DATE
// ========================================

void getExpenseDate(
    const char *body,
    int *day,
    int *month,
    int *year
)
{
    time_t now = time(NULL);
    struct tm *current = localtime(&now);

    *day = current->tm_mday;
    *month = current->tm_mon + 1;
    *year = current->tm_year + 1900;

    double value;

    if (getJsonNumber(body, "day", &value))
        *day = (int)value;

    if (getJsonNumber(body, "month", &value))
        *month = (int)value;

    if (getJsonNumber(body, "year", &value))
        *year = (int)value;
}

// ========================================
// ADD EXPENSE
// ========================================

int addUserExpense(
    const char *username,
    const char *body,
    char *json
)
{
    char path[128];
    char name[100];

    double amount;
    int category;
    int day, month, year;

    if (!getExpenseFilePath(username, path, sizeof(path)))
    {
        strcpy(json, "{\"error\":\"Invalid account\"}");
        return 0;
    }

    if (!getJsonString(body, "name", name, sizeof(name)) ||
        !getJsonNumber(body, "amount", &amount) ||
        !readCategory(body, &category))
    {
        strcpy(json, "{\"error\":\"Invalid expense details\"}");
        return 0;
    }

    getExpenseDate(body, &day, &month, &year);

    if (name[0] == '\0' ||
        strchr(name, '|') || strchr(name, '\n') ||
        strchr(name, '\r') ||
        amount <= 0 || amount > 10000000 ||
        day < 1 || day > 31 ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2100)
    {
        strcpy(json, "{\"error\":\"Invalid expense values\"}");
        return 0;
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
        strcpy(json, "{\"error\":\"Could not save expense\"}");
        return 0;
    }

    fprintf(file, "%d|%s|%.2f|%d|%d|%d|%d\n",
            nextId, name, amount, category,
            day, month, year);

    fclose(file);

    snprintf(json, DATA_SIZE,
             "{\"message\":\"Expense added\",\"id\":%d}",
             nextId);

    return 1;
}

// ========================================
// EDIT EXPENSE
// ========================================

int editUserExpense(
    const char *username,
    const char *body,
    char *json
)
{
    char path[128];
    char tempPath[160];
    char name[100];

    double idValue, amount;
    int category, day, month, year;

    if (!getExpenseFilePath(username, path, sizeof(path)))
        return 0;

    if (!getJsonNumber(body, "id", &idValue) ||
        !getJsonString(body, "name", name, sizeof(name)) ||
        !getJsonNumber(body, "amount", &amount) ||
        !readCategory(body, &category))
    {
        strcpy(json, "{\"error\":\"Invalid expense details\"}");
        return 0;
    }

    int id = (int)idValue;

    getExpenseDate(body, &day, &month, &year);

    if (id <= 0 || idValue != id ||
        name[0] == '\0' || strchr(name, '|') ||
        amount <= 0 || amount > 10000000 ||
        category < 1 || category > 5 ||
        day < 1 || day > 31 ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2100)
    {
        strcpy(json, "{\"error\":\"Invalid expense values\"}");
        return 0;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE *input = fopen(path, "r");

    if (!input)
    {
        strcpy(json, "{\"error\":\"Expense not found\"}");
        return 0;
    }

    FILE *output = fopen(tempPath, "w");

    if (!output)
    {
        fclose(input);
        strcpy(json, "{\"error\":\"Could not update expense\"}");
        return 0;
    }

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), input))
    {
        int oldId, oldCategory, oldDay, oldMonth, oldYear;
        char oldName[100];
        float oldAmount;

        if (sscanf(line, "%d|%99[^|]|%f|%d|%d|%d|%d",
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
        strcpy(json, "{\"error\":\"Expense not found\"}");
        return 0;
    }

    if (remove(path) != 0 || rename(tempPath, path) != 0)
    {
        strcpy(json, "{\"error\":\"Could not finish update\"}");
        return 0;
    }

    strcpy(json, "{\"message\":\"Expense updated successfully\"}");
    return 1;
}

// ========================================
// DELETE EXPENSE
// ========================================

int deleteUserExpense(
    const char *username,
    int id,
    char *json
)
{
    char path[128];
    char tempPath[160];

    if (id <= 0 ||
        !getExpenseFilePath(username, path, sizeof(path)))
    {
        strcpy(json, "{\"error\":\"Invalid expense ID\"}");
        return 0;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE *input = fopen(path, "r");

    if (!input)
    {
        strcpy(json, "{\"error\":\"Expense not found\"}");
        return 0;
    }

    FILE *output = fopen(tempPath, "w");

    if (!output)
    {
        fclose(input);
        strcpy(json, "{\"error\":\"Could not delete expense\"}");
        return 0;
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
        strcpy(json, "{\"error\":\"Expense not found\"}");
        return 0;
    }

    if (remove(path) != 0 || rename(tempPath, path) != 0)
    {
        strcpy(json, "{\"error\":\"Could not finish deletion\"}");
        return 0;
    }

    strcpy(json, "{\"message\":\"Expense deleted successfully\"}");
    return 1;
}

// ========================================
// GET MONTHLY BUDGET
// ========================================

void getUserBudget(
    const char *username,
    int month,
    int year,
    char *json
)
{
    char path[128];

    if (!getBudgetFilePath(username, path, sizeof(path)))
    {
        strcpy(json, "{\"budget\":0}");
        return;
    }

    FILE *file = fopen(path, "r");

    if (!file)
    {
        strcpy(json, "{\"budget\":0}");
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

    snprintf(json, DATA_SIZE,
             "{\"budget\":%.2f}", result);
}

// ========================================
// SAVE MONTHLY BUDGET
// ========================================

int saveUserBudget(
    const char *username,
    const char *body,
    char *json
)
{
    char path[128];
    char tempPath[160];

    double budget;
    double monthValue, yearValue;

    if (!getBudgetFilePath(username, path, sizeof(path)))
        return 0;

    if (!getJsonNumber(body, "budget", &budget) &&
        !getJsonNumber(body, "monthlyBudget", &budget))
    {
        strcpy(json, "{\"error\":\"Budget value required\"}");
        return 0;
    }

    time_t now = time(NULL);
    struct tm *current = localtime(&now);

    int month = current->tm_mon + 1;
    int year = current->tm_year + 1900;

    if (getJsonNumber(body, "month", &monthValue))
        month = (int)monthValue;

    if (getJsonNumber(body, "year", &yearValue))
        year = (int)yearValue;

    if (budget < 0 || budget > 100000000 ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2100)
    {
        strcpy(json, "{\"error\":\"Invalid budget values\"}");
        return 0;
    }

    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);

    FILE *input = fopen(path, "r");
    FILE *output = fopen(tempPath, "w");

    if (!output)
    {
        if (input)
            fclose(input);

        strcpy(json, "{\"error\":\"Could not save budget\"}");
        return 0;
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

    if (remove(path) != 0 && errno != ENOENT)
    {
        remove(tempPath);
        strcpy(json, "{\"error\":\"Could not replace budget file\"}");
        return 0;
    }

    if (rename(tempPath, path) != 0)
    {
        strcpy(json, "{\"error\":\"Could not finish saving budget\"}");
        return 0;
    }

    strcpy(json, "{\"message\":\"Budget saved successfully\"}");
    return 1;
}

// ========================================
// ROUTE API REQUESTS
// ========================================

void handleRequest(
    const char *request,
    SOCKET client
)
{
    char method[16] = "";
    char path[256] = "";
    char username[MAX_USERNAME + 1] = "";
    char json[DATA_SIZE] = "{\"error\":\"Unknown request\"}";

    const char *body = strstr(request, "\r\n\r\n");

    if (body)
        body += 4;
    else
        body = "";

    sscanf(request, "%15s %255s", method, path);

    if (strcmp(method, "OPTIONS") == 0)
    {
        sendResponse(client, 204, "No Content", "");
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/status") == 0)
    {
        strcpy(json, "{\"status\":\"SmartSpend C backend connected\"}");
        sendResponse(client, 200, "OK", json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/register") == 0)
    {
        handleRegister(body, json);

        int success = strstr(json, "\"error\"") == NULL;

        sendResponse(client,
                     success ? 200 : 400,
                     success ? "OK" : "Bad Request",
                     json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/login") == 0)
    {
        handleLogin(body, json);

        int success = strstr(json, "\"token\"") != NULL;

        sendResponse(client,
                     success ? 200 : 401,
                     success ? "OK" : "Unauthorized",
                     json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/logout") == 0)
    {
        handleLogout(request, json);

        int success = strstr(json, "successful") != NULL;

        sendResponse(client,
                     success ? 200 : 401,
                     success ? "OK" : "Unauthorized",
                     json);
        return;
    }

    if (!authenticateRequest(request, username))
    {
        strcpy(json, "{\"error\":\"Login required\"}");
        sendResponse(client, 401, "Unauthorized", json);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        strcmp(path, "/api/expenses") == 0)
    {
        getUserExpenses(username, json);
        sendResponse(client, 200, "OK", json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/expenses") == 0)
    {
        int success = addUserExpense(username, body, json);

        sendResponse(client,
                     success ? 201 : 400,
                     success ? "Created" : "Bad Request",
                     json);
        return;
    }

    if (strcmp(method, "PUT") == 0 &&
        strcmp(path, "/api/expenses") == 0)
    {
        int success = editUserExpense(username, body, json);

        sendResponse(client,
                     success ? 200 : 400,
                     success ? "OK" : "Bad Request",
                     json);
        return;
    }

    if (strcmp(method, "DELETE") == 0 &&
        strncmp(path, "/api/expenses?", 14) == 0)
    {
        int id = 0;

        if (!getQueryNumber(path, "id", &id))
        {
            strcpy(json, "{\"error\":\"Expense ID required\"}");
            sendResponse(client, 400, "Bad Request", json);
            return;
        }

        int success = deleteUserExpense(username, id, json);

        sendResponse(client,
                     success ? 200 : 404,
                     success ? "OK" : "Not Found",
                     json);
        return;
    }

    if (strcmp(method, "GET") == 0 &&
        strncmp(path, "/api/budget", 11) == 0)
    {
        int month = 0;
        int year = 0;

        time_t now = time(NULL);
        struct tm *current = localtime(&now);

        month = current->tm_mon + 1;
        year = current->tm_year + 1900;

        getQueryNumber(path, "month", &month);
        getQueryNumber(path, "year", &year);

        getUserBudget(username, month, year, json);

        sendResponse(client, 200, "OK", json);
        return;
    }

    if (strcmp(method, "POST") == 0 &&
        strcmp(path, "/api/budget") == 0)
    {
        int success = saveUserBudget(username, body, json);

        sendResponse(client,
                     success ? 200 : 400,
                     success ? "OK" : "Bad Request",
                     json);
        return;
    }

    strcpy(json, "{\"error\":\"API endpoint not found\"}");
    sendResponse(client, 404, "Not Found", json);
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
#endif

    createDataFolders();

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

    if (listen(server, 10) == SOCKET_ERROR)
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
    printf("Authentication enabled.\n");
    printf("Waiting for requests...\n\n");

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
