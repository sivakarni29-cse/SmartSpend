
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
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
#endif

#define PORT 8080
#define REQUEST_SIZE 16384
#define DATA_SIZE 32768

// ========================================
// SEND HTTP RESPONSE
// ========================================

void sendResponse(SOCKET client, const char *body)
{
    char response[DATA_SIZE + 1024];

    int length = snprintf(
        response,
        sizeof(response),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Connection: close\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "%s",
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
// JSON STRING ESCAPING
// ========================================

void jsonEscape(const char *source, char *destination, size_t size)
{
    size_t j = 0;

    for (size_t i = 0; source[i] != '\0' && j + 2 < size; i++)
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

// ========================================
// READ JSON STRING
// ========================================

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

    return (*p == '"');
}

// ========================================
// READ JSON NUMBER
// ========================================

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

// ========================================
// READ EXPENSES FROM FILE
// ========================================

void getExpenses(char *json)
{
    FILE *file = fopen("expenses.txt", "r");

    if (!file)
    {
        strcpy(json, "[]");
        return;
    }

    char line[512];
    char name[100];
    char escapedName[300];

    float amount;
    int category, day, month, year;

    int offset = 0;
    int first = 1;

    offset += snprintf(json, DATA_SIZE, "[");

    while (fgets(line, sizeof(line), file))
    {
        name[0] = '\0';

        int fields = sscanf(
            line,
            "%99[^|]|%f|%d|%d|%d|%d",
            name,
            &amount,
            &category,
            &day,
            &month,
            &year
        );

        if (fields != 6)
        {
            fields = sscanf(
                line,
                "%99s %f %d %d %d",
                name,
                &amount,
                &category,
                &month,
                &year
            );

            if (fields == 5)
            {
                day = 1;
            }
            else
            {
                fields = sscanf(
                    line,
                    "%99s %f %d",
                    name,
                    &amount,
                    &category
                );

                if (fields != 3)
                    continue;

                day = 1;
                month = 1;
                year = 2026;
            }
        }

        if (amount <= 0 ||
            category < 1 || category > 5 ||
            day < 1 || day > 31 ||
            month < 1 || month > 12)
        {
            continue;
        }

        jsonEscape(name, escapedName, sizeof(escapedName));

        if (offset >= DATA_SIZE - 500)
            break;

        offset += snprintf(
            json + offset,
            DATA_SIZE - offset,
            "%s{\"name\":\"%s\","
            "\"amount\":%.2f,"
            "\"category\":%d,"
            "\"day\":%d,"
            "\"month\":%d,"
            "\"year\":%d}",
            first ? "" : ",",
            escapedName,
            amount,
            category,
            day,
            month,
            year
        );

        first = 0;
    }

    snprintf(
        json + offset,
        DATA_SIZE - offset,
        "]"
    );

    fclose(file);
}

// ========================================
// ADD EXPENSE TO FILE
// ========================================

void addExpense(const char *body, char *json)
{
    char name[100];

    double amountValue;
    double categoryValue;
    double dayValue;
    double monthValue;
    double yearValue;

    if (!getJsonString(body, "name", name, sizeof(name)) ||
        !getJsonNumber(body, "amount", &amountValue) ||
        !getJsonNumber(body, "category", &categoryValue) ||
        !getJsonNumber(body, "day", &dayValue) ||
        !getJsonNumber(body, "month", &monthValue) ||
        !getJsonNumber(body, "year", &yearValue))
    {
        strcpy(json, "{\"error\":\"Invalid expense data\"}");
        return;
    }

    int category = (int)categoryValue;
    int day = (int)dayValue;
    int month = (int)monthValue;
    int year = (int)yearValue;

    if (strlen(name) == 0 ||
        strchr(name, '|') ||
        strchr(name, '\n') ||
        strchr(name, '\r') ||
        amountValue <= 0 ||
        amountValue > 1000000000 ||
        categoryValue != category ||
        dayValue != day ||
        monthValue != month ||
        yearValue != year ||
        category < 1 || category > 5 ||
        day < 1 || day > 31 ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2100)
    {
        strcpy(json, "{\"error\":\"Expense details are invalid\"}");
        return;
    }

    FILE *file = fopen("expenses.txt", "a");

    if (!file)
    {
        strcpy(json, "{\"error\":\"Could not open expenses.txt\"}");
        return;
    }

    fprintf(
        file,
        "%s|%.2f|%d|%d|%d|%d\n",
        name,
        amountValue,
        category,
        day,
        month,
        year
    );

    fclose(file);

    strcpy(json, "{\"message\":\"Expense saved successfully\"}");

    printf("New expense saved: %s - %.2f\n", name, amountValue);
}

// ========================================
// GET MONTHLY BUDGET
// ========================================

void getBudget(const char *request, char *json)
{
    int month = 0;
    int year = 0;

    const char *query = strchr(request, '?');

    if (query)
    {
        sscanf(
            query,
            "?month=%d&year=%d",
            &month,
            &year
        );
    }

    if (month < 1 || month > 12 || year < 2000 || year > 2100)
    {
        strcpy(json, "{\"budget\":0}");
        return;
    }

    FILE *file = fopen("budgets.txt", "r");

    if (!file)
    {
        strcpy(json, "{\"budget\":0}");
        return;
    }

    int fileMonth, fileYear;
    float budget;
    float latestBudget = 0;

    while (fscanf(
        file,
        "%d %d %f",
        &fileMonth,
        &fileYear,
        &budget
    ) == 3)
    {
        if (fileMonth == month && fileYear == year)
            latestBudget = budget;
    }

    fclose(file);

    snprintf(
        json,
        DATA_SIZE,
        "{\"budget\":%.2f}",
        latestBudget
    );
}

// ========================================
// SAVE MONTHLY BUDGET
// ========================================

void saveBudget(const char *body, char *json)
{
    double monthValue, yearValue, budgetValue;

    if (!getJsonNumber(body, "month", &monthValue) ||
        !getJsonNumber(body, "year", &yearValue) ||
        !getJsonNumber(body, "budget", &budgetValue))
    {
        strcpy(json, "{\"error\":\"Invalid budget data\"}");
        return;
    }

    int month = (int)monthValue;
    int year = (int)yearValue;

    if (monthValue != month ||
        yearValue != year ||
        month < 1 || month > 12 ||
        year < 2000 || year > 2100 ||
        budgetValue <= 0 ||
        budgetValue > 1000000000)
    {
        strcpy(json, "{\"error\":\"Budget details are invalid\"}");
        return;
    }

    FILE *file = fopen("budgets.txt", "a");

    if (!file)
    {
        strcpy(json, "{\"error\":\"Could not open budgets.txt\"}");
        return;
    }

    fprintf(
        file,
        "%d %d %.2f\n",
        month,
        year,
        budgetValue
    );

    fclose(file);

    strcpy(json, "{\"message\":\"Budget saved successfully\"}");

    printf("Budget saved: %d/%d - %.2f\n", month, year, budgetValue);
}

// ========================================
// READ COMPLETE HTTP REQUEST
// ========================================

int receiveRequest(SOCKET client, char *request, size_t capacity)
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

                char *lengthHeader = strstr(request, "Content-Length:");

                if (lengthHeader && lengthHeader < headerEnd)
                {
                    contentLength = atoi(lengthHeader + 15);
                }

                if (contentLength < 0 ||
                    contentLength > (int)capacity - headerLength - 1)
                {
                    return 0;
                }
            }
        }

        if (headerLength != -1 &&
            received >= (size_t)(headerLength + contentLength))
        {
            return 1;
        }
    }

    return 0;
}

// ========================================
// MAIN SERVER
// ========================================

int main()
{
#ifdef _WIN32
    WSADATA wsa;

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        printf("Winsock initialization failed!\n");
        return 1;
    }
#endif

    SOCKET serverSocket;
    SOCKET clientSocket;

    struct sockaddr_in serverAddress;

    char request[REQUEST_SIZE];
    char json[DATA_SIZE];

    int port = PORT;
    char *portEnvironment = getenv("PORT");

    if (portEnvironment != NULL)
    {
        int requestedPort = atoi(portEnvironment);

        if (requestedPort > 0 && requestedPort <= 65535)
            port = requestedPort;
    }

    printf("Starting SmartSpend C Server...\n");

    serverSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (serverSocket == INVALID_SOCKET)
    {
        printf("Socket creation failed!\n");

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }

    int reuse = 1;

    setsockopt(
        serverSocket,
        SOL_SOCKET,
        SO_REUSEADDR,
        (const char *)&reuse,
        sizeof(reuse)
    );

    memset(&serverAddress, 0, sizeof(serverAddress));

    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons((unsigned short)port);
    serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(
        serverSocket,
        (struct sockaddr *)&serverAddress,
        sizeof(serverAddress)
    ) == SOCKET_ERROR)
    {
        printf("Port %d is unavailable!\n", port);
        closesocket(serverSocket);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }

    if (listen(serverSocket, 5) == SOCKET_ERROR)
    {
        printf("Server listening failed!\n");
        closesocket(serverSocket);

#ifdef _WIN32
        WSACleanup();
#endif

        return 1;
    }

    printf("\nSmartSpend C Server is running!\n");
    printf("Listening on port: %d\n", port);
    printf("Press Ctrl + C to stop.\n\n");

    while (1)
    {
        clientSocket = accept(serverSocket, NULL, NULL);

        if (clientSocket == INVALID_SOCKET)
            continue;

        memset(request, 0, sizeof(request));
        memset(json, 0, sizeof(json));

        if (receiveRequest(clientSocket, request, sizeof(request)))
        {
            printf("Request: %.100s\n", request);

            char *body = strstr(request, "\r\n\r\n");

            if (body)
                body += 4;
            else
                body = "";

            if (strncmp(request, "OPTIONS ", 8) == 0)
            {
                sendResponse(clientSocket, "{}");
            }
            else if (strncmp(request, "GET /api/status", 15) == 0)
            {
                sendResponse(
                    clientSocket,
                    "{\"status\":\"SmartSpend C Server is running\"}"
                );
            }
            else if (strncmp(request, "GET /api/expenses", 17) == 0)
            {
                getExpenses(json);
                sendResponse(clientSocket, json);

                printf("Expenses loaded.\n");
            }
            else if (strncmp(request, "POST /api/expenses", 18) == 0)
            {
                addExpense(body, json);
                sendResponse(clientSocket, json);
            }
            else if (strncmp(request, "GET /api/budget", 15) == 0)
            {
                getBudget(request, json);
                sendResponse(clientSocket, json);
            }
            else if (strncmp(request, "POST /api/budget", 16) == 0)
            {
                saveBudget(body, json);
                sendResponse(clientSocket, json);
            }
            else
            {
                sendResponse(
                    clientSocket,
                    "{\"error\":\"Endpoint not available\"}"
                );
            }
        }
        else
        {
            sendResponse(
                clientSocket,
                "{\"error\":\"Invalid or incomplete request\"}"
            );
        }

        closesocket(clientSocket);
    }

    closesocket(serverSocket);

#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
