
#include <stdio.h>
#include <string.h>
#include "auth.h"

int main()
{
    char message[200];
    char token[65];
    char username[33];

    const char *testUsername = "demo_user";
    const char *testPassword = "DemoPass123!";

    printf("SMARTSPEND AUTHENTICATION TEST\n\n");

    if (!userExists(testUsername))
    {
        if (registerUser(
            testUsername,
            testPassword,
            message,
            sizeof(message)))
        {
            printf("Registration: %s\n", message);
        }
        else
        {
            printf("Registration failed: %s\n", message);
            return 1;
        }
    }
    else
    {
        printf("Test account already exists.\n");
    }

    if (!loginUser(
        testUsername,
        testPassword,
        token,
        sizeof(token),
        message,
        sizeof(message)))
    {
        printf("Login failed: %s\n", message);
        return 1;
    }

    printf("Login: %s\n", message);

    if (!getUserFromToken(token, username, sizeof(username)))
    {
        printf("Session verification failed.\n");
        return 1;
    }

    printf("Session belongs to: %s\n", username);

    if (logoutUser(token))
        printf("Logout: Successful\n");
    else
        printf("Logout failed.\n");

    if (!getUserFromToken(token, username, sizeof(username)))
        printf("Session invalidated successfully.\n");

    return 0;
}
