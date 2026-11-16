
#ifndef AUTH_H
#define AUTH_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#define MAX_USERNAME 32
#define MAX_PASSWORD 128
#define SALT_SIZE 16
#define HASH_SIZE 32
#define TOKEN_SIZE 32
#define MAX_SESSIONS 100
#define HASH_ITERATIONS 600000

#define USERS_FILE "users.txt"

typedef struct
{
    char username[MAX_USERNAME + 1];
    char token[65];
    int active;
} UserSession;

static UserSession sessions[MAX_SESSIONS];

/* Check username format */
static int validUsername(const char *username)
{
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

/* Convert bytes into hexadecimal text */
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

        if (sscanf(input + i * 2, "%2x", &value) != 1)
            return 0;

        output[i] = (unsigned char)value;
    }

    return 1;
}

/* Generate secure password hash */
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

/* Check whether username already exists */
static int userExists(const char *username)
{
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
        snprintf(message, messageSize,
                 "Password protection failed");
        return 0;
    }

    toHex(salt, SALT_SIZE, saltHex);
    toHex(hash, HASH_SIZE, hashHex);

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

/* Verify password and create login session */
static int loginUser(
    const char *username,
    const char *password,
    char *tokenOutput,
    size_t tokenSize,
    char *message,
    size_t messageSize
)
{
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
                 "Account not found");
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

    int slot = -1;

    for (int i = 0; i < MAX_SESSIONS; i++)
    {
        if (!sessions[i].active)
        {
            slot = i;
            break;
        }
    }

    if (slot == -1)
    {
        snprintf(message, messageSize,
                 "Maximum sessions reached");
        return 0;
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

    strcpy(sessions[slot].username, username);
    strcpy(sessions[slot].token, tokenOutput);
    sessions[slot].active = 1;

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

/* Logout */
static int logoutUser(const char *token)
{
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
            memset(&sessions[i], 0, sizeof(sessions[i]));
            return 1;
        }
    }

    return 0;
}

#endif
