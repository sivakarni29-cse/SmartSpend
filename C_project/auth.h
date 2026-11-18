#ifndef AUTH_H
#define AUTH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#define MAX_USERNAME 32
#define MAX_PASSWORD 128
#define SALT_SIZE 16
#define HASH_SIZE 32
#define TOKEN_SIZE 32
#define MAX_SESSIONS 256
#define HASH_ITERATIONS 600000
#define SESSION_TTL_SECONDS (7 * 24 * 60 * 60)

#define USERS_FILE "users.txt"
#define SESSIONS_FILE "sessions.txt"

typedef struct
{
    char username[MAX_USERNAME + 1];
    char token[65];
    time_t expiresAt;
    int active;
} UserSession;

static UserSession sessions[MAX_SESSIONS];
static int sessionsLoaded = 0;

/* Atomically replace targetPath with tempPath */
static int atomicReplaceFile(const char *tempPath, const char *targetPath)
{
#ifdef _WIN32
    if (MoveFileExA(tempPath, targetPath, MOVEFILE_REPLACE_EXISTING))
        return 1;
    remove(tempPath);
    return 0;
#else
    if (rename(tempPath, targetPath) == 0)
        return 1;
    remove(tempPath);
    return 0;
#endif
}

/* Check username format (3-32 letters, numbers, underscores) */
static int validUsername(const char *username)
{
    if (!username)
        return 0;

    size_t length = strlen(username);

    if (length < 3 || length > MAX_USERNAME)
        return 0;

    for (size_t i = 0; i < length; i++)
    {
        if (!isalnum((unsigned char)username[i]) &&
            username[i] != '_')
            return 0;
    }

    return 1;
}

/* Convert bytes into lowercase hexadecimal text */
static void toHex(
    const unsigned char *input,
    int length,
    char *output
)
{
    static const char digits[] = "0123456789abcdef";

    for (int i = 0; i < length; i++)
    {
        output[i * 2] = digits[input[i] >> 4];
        output[i * 2 + 1] = digits[input[i] & 15];
    }

    output[length * 2] = '\0';
}

/* Convert hexadecimal text into bytes */
static int fromHex(
    const char *input,
    unsigned char *output,
    int length
)
{
    for (int i = 0; i < length; i++)
    {
        unsigned int value;

        if (!isxdigit((unsigned char)input[i * 2]) ||
            !isxdigit((unsigned char)input[i * 2 + 1]) ||
            sscanf(input + i * 2, "%2x", &value) != 1)
            return 0;

        output[i] = (unsigned char)value;
    }

    return 1;
}

/* Generate secure PBKDF2-HMAC-SHA256 password hash */
static int createPasswordHash(
    const char *password,
    const unsigned char *salt,
    unsigned char *hash
)
{
    return PKCS5_PBKDF2_HMAC(
        password,
        (int)strlen(password),
        salt,
        SALT_SIZE,
        HASH_ITERATIONS,
        EVP_sha256(),
        HASH_SIZE,
        hash
    );
}

/* Save active unexpired sessions to disk */
static void saveSessions(void)
{
    const char *tempFile = "sessions.txt.tmp";
    FILE *file = fopen(tempFile, "w");

    if (!file)
        return;

    time_t now = time(NULL);

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (sessions[i].active && sessions[i].expiresAt > now)
        {
            fprintf(
                file,
                "%s|%s|%lld\n",
                sessions[i].token,
                sessions[i].username,
                (long long)sessions[i].expiresAt
            );
        }
    }

    fclose(file);
    atomicReplaceFile(tempFile, SESSIONS_FILE);
}

/* Remove expired sessions from memory */
static int cleanupExpiredSessions(void)
{
    time_t now = time(NULL);
    int removed = 0;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (sessions[i].active && sessions[i].expiresAt <= now)
        {
            OPENSSL_cleanse(&sessions[i], sizeof(sessions[i]));
            removed++;
        }
    }

    return removed;
}

/* Load persisted sessions from disk (survives server restarts) */
static void loadSessions(void)
{
    if (sessionsLoaded)
        return;

    sessionsLoaded = 1;
    memset(sessions, 0, sizeof(sessions));

    FILE *file = fopen(SESSIONS_FILE, "r");

    if (!file)
        return;

    char line[256];
    char token[65];
    char username[MAX_USERNAME + 1];
    long long expiresValue = 0;
    time_t now = time(NULL);
    int slot = 0;

    while (slot < MAX_SESSIONS && fgets(line, sizeof(line), file))
    {
        if (sscanf(
                line,
                "%64[^|]|%32[^|]|%lld",
                token,
                username,
                &expiresValue
            ) == 3)
        {
            if (strlen(token) == 64 &&
                validUsername(username) &&
                (time_t)expiresValue > now)
            {
                snprintf(sessions[slot].token, sizeof(sessions[slot].token), "%s", token);
                snprintf(sessions[slot].username, sizeof(sessions[slot].username), "%s", username);
                sessions[slot].expiresAt = (time_t)expiresValue;
                sessions[slot].active = 1;
                slot++;
            }
        }
    }

    fclose(file);
}

/* Check whether username already exists */
static int userExists(const char *username)
{
    if (!validUsername(username))
        return 0;

    FILE *file = fopen(USERS_FILE, "r");

    if (!file)
        return 0;

    char line[256];
    char storedUsername[MAX_USERNAME + 1];

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(line, "%32[^|]", storedUsername) == 1)
        {
            if (strcmp(storedUsername, username) == 0)
            {
                fclose(file);
                return 1;
            }
        }
    }

    fclose(file);
    return 0;
}

/* Register a new account */
static int registerUser(
    const char *username,
    const char *password,
    char *message,
    size_t messageSize
)
{
    if (!validUsername(username))
    {
        snprintf(message, messageSize,
                 "Username must be 3-32 letters, numbers or underscores");
        return 0;
    }

    if (!password ||
        strlen(password) < 8 ||
        strlen(password) > MAX_PASSWORD)
    {
        snprintf(message, messageSize,
                 "Password must contain 8-128 characters");
        return 0;
    }

    if (userExists(username))
    {
        snprintf(message, messageSize,
                 "Username already exists");
        return 0;
    }

    unsigned char salt[SALT_SIZE];
    unsigned char hash[HASH_SIZE];

    char saltHex[SALT_SIZE * 2 + 1];
    char hashHex[HASH_SIZE * 2 + 1];

    if (RAND_bytes(salt, sizeof(salt)) != 1 ||
        !createPasswordHash(password, salt, hash))
    {
        OPENSSL_cleanse(salt, sizeof(salt));
        OPENSSL_cleanse(hash, sizeof(hash));
        snprintf(message, messageSize,
                 "Password protection failed");
        return 0;
    }

    toHex(salt, SALT_SIZE, saltHex);
    toHex(hash, HASH_SIZE, hashHex);

    OPENSSL_cleanse(salt, sizeof(salt));
    OPENSSL_cleanse(hash, sizeof(hash));

    FILE *file = fopen(USERS_FILE, "a");

    if (!file)
    {
        snprintf(message, messageSize,
                 "Could not open users file");
        return 0;
    }

    fprintf(file, "%s|%s|%s\n",
            username, saltHex, hashHex);

    fclose(file);

    snprintf(message, messageSize,
             "Account created successfully");

    return 1;
}

/* Verify password and create persistent login session */
static int loginUser(
    const char *username,
    const char *password,
    char *tokenOutput,
    size_t tokenSize,
    char *message,
    size_t messageSize
)
{
    loadSessions();

    if (!validUsername(username) ||
        !password ||
        strlen(password) > MAX_PASSWORD)
    {
        snprintf(message, messageSize,
                 "Invalid username or password");
        return 0;
    }

    FILE *file = fopen(USERS_FILE, "r");

    if (!file)
    {
        snprintf(message, messageSize,
                 "Invalid username or password");
        return 0;
    }

    char line[256];
    char storedUsername[MAX_USERNAME + 1];
    char saltHex[SALT_SIZE * 2 + 1];
    char hashHex[HASH_SIZE * 2 + 1];

    unsigned char salt[SALT_SIZE];
    unsigned char storedHash[HASH_SIZE];
    unsigned char enteredHash[HASH_SIZE];

    int found = 0;

    while (fgets(line, sizeof(line), file))
    {
        if (sscanf(
                line,
                "%32[^|]|%32[^|]|%64s",
                storedUsername,
                saltHex,
                hashHex
            ) == 3 &&
            strcmp(storedUsername, username) == 0)
        {
            if (strlen(saltHex) != SALT_SIZE * 2 ||
                strlen(hashHex) != HASH_SIZE * 2 ||
                !fromHex(saltHex, salt, SALT_SIZE) ||
                !fromHex(hashHex, storedHash, HASH_SIZE))
                break;

            if (createPasswordHash(password, salt, enteredHash) &&
                CRYPTO_memcmp(
                    storedHash,
                    enteredHash,
                    HASH_SIZE
                ) == 0)
            {
                found = 1;
            }

            OPENSSL_cleanse(salt, sizeof(salt));
            OPENSSL_cleanse(storedHash, sizeof(storedHash));
            OPENSSL_cleanse(enteredHash, sizeof(enteredHash));
            break;
        }
    }

    fclose(file);

    if (!found)
    {
        snprintf(message, messageSize,
                 "Invalid username or password");
        return 0;
    }

    cleanupExpiredSessions();

    int slot = -1;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!sessions[i].active)
        {
            slot = i;
            break;
        }
    }

    /* If full, evict the session closest to expiring */
    if (slot == -1)
    {
        slot = 0;
        for (int i = 1; i < MAX_SESSIONS; i++)
        {
            if (sessions[i].expiresAt < sessions[slot].expiresAt)
                slot = i;
        }
    }

    unsigned char randomToken[TOKEN_SIZE];

    if (tokenSize < 65 ||
        RAND_bytes(randomToken, sizeof(randomToken)) != 1)
    {
        snprintf(message, messageSize,
                 "Session creation failed");
        return 0;
    }

    toHex(randomToken, TOKEN_SIZE, tokenOutput);
    OPENSSL_cleanse(randomToken, sizeof(randomToken));

    snprintf(sessions[slot].username, sizeof(sessions[slot].username), "%s", username);
    snprintf(sessions[slot].token, sizeof(sessions[slot].token), "%s", tokenOutput);
    sessions[slot].expiresAt = time(NULL) + SESSION_TTL_SECONDS;
    sessions[slot].active = 1;

    saveSessions();

    snprintf(message, messageSize,
             "Login successful");

    return 1;
}

/* Find account using session token */
static int getUserFromToken(
    const char *token,
    char *usernameOutput,
    size_t outputSize
)
{
    loadSessions();

    if (!token || strlen(token) != 64)
        return 0;

    time_t now = time(NULL);

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (sessions[i].active &&
            CRYPTO_memcmp(
                sessions[i].token,
                token,
                65
            ) == 0)
        {
            if (sessions[i].expiresAt <= now)
            {
                OPENSSL_cleanse(&sessions[i], sizeof(sessions[i]));
                saveSessions();
                return 0;
            }

            snprintf(
                usernameOutput,
                outputSize,
                "%s",
                sessions[i].username
            );

            return 1;
        }
    }

    return 0;
}

/* Logout and persist session removal */
static int logoutUser(const char *token)
{
    loadSessions();

    if (!token || strlen(token) != 64)
        return 0;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (sessions[i].active &&
            CRYPTO_memcmp(
                sessions[i].token,
                token,
                65
            ) == 0)
        {
            OPENSSL_cleanse(&sessions[i], sizeof(sessions[i]));
            saveSessions();
            return 1;
        }
    }

    return 0;
}

#endif
