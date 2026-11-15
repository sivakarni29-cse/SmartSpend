
#include <stdio.h>
#include <string.h>
#include <time.h>

struct Expense {
    char name[50];
    float amount;
    int category;
    int month;
    int year;
};

struct Expense expenses[100];

int count = 0;

float monthlyBudget = 0;

int currentMonth, currentYear;

char *categories[] = {
    "Food", "Travel", "Education",
    "Shopping", "Other"
};

char *months[] = {
    "", "January", "February", "March",
    "April", "May", "June", "July",
    "August", "September", "October",
    "November", "December"
};

// Function declarations
void saveExpenses();
void loadExpenses();
void saveBudget();
void loadBudget();
void addExpense();
void viewExpenses();
void calculateTotal();
void setBudget();
void viewBudget();

// Add Expense
void addExpense() {
    int category;
    float amount;

    if (count >= 100) {
        printf("\nExpense limit reached!\n");
        return;
    }

    printf("\nEnter expense name (one word): ");

    if (scanf("%49s", expenses[count].name) != 1) {
        printf("Invalid expense name!\n");
        return;
    }

    printf("Enter amount in Rs: ");

    if (scanf("%f", &amount) != 1 || amount <= 0) {
        printf("Invalid amount!\n");
        return;
    }

    printf("\nSelect Category:\n");

    for (int i = 0; i < 5; i++) {
        printf("%d. %s\n", i + 1, categories[i]);
    }

    printf("Enter category: ");

    if (scanf("%d", &category) != 1 ||
        category < 1 || category > 5) {
        printf("Invalid category!\n");
        return;
    }

    expenses[count].amount = amount;
    expenses[count].category = category;
    expenses[count].month = currentMonth;
    expenses[count].year = currentYear;

    count++;

    saveExpenses();

    printf("\nExpense added successfully!\n");
}

// View All Expenses
void viewExpenses() {
    if (count == 0) {
        printf("\nNo expenses recorded.\n");
        return;
    }

    printf("\n----- ALL EXPENSE HISTORY -----\n");

    for (int i = 0; i < count; i++) {
        printf("\n%d. %s\n", i + 1,
               expenses[i].name);

        printf("Amount: Rs. %.2f\n",
               expenses[i].amount);

        printf("Category: %s\n",
               categories[expenses[i].category - 1]);

        printf("Month: %s %d\n",
               months[expenses[i].month],
               expenses[i].year);
    }
}

// Calculate Current Month Total
void calculateTotal() {
    float total = 0;

    for (int i = 0; i < count; i++) {
        if (expenses[i].month == currentMonth &&
            expenses[i].year == currentYear) {
            total += expenses[i].amount;
        }
    }

    printf("\n----- CURRENT MONTH TOTAL -----\n");

    printf("Month: %s %d\n",
           months[currentMonth], currentYear);

    printf("Total Expenses: Rs. %.2f\n", total);
}

// Save Expenses
void saveExpenses() {
    FILE *fp = fopen("expenses.txt", "w");

    if (fp == NULL) {
        printf("Unable to save expenses!\n");
        return;
    }

    for (int i = 0; i < count; i++) {
        fprintf(fp, "%s %.2f %d %d %d\n",
                expenses[i].name,
                expenses[i].amount,
                expenses[i].category,
                expenses[i].month,
                expenses[i].year);
    }

    fclose(fp);
}

// Load Expenses
void loadExpenses() {
    FILE *fp = fopen("expenses.txt", "r");
    char line[200];

    if (fp == NULL) {
        return;
    }

    count = 0;

    while (count < 100 &&
           fgets(line, sizeof(line), fp)) {

        int result;

        result = sscanf(line, "%49s %f %d %d %d",
                        expenses[count].name,
                        &expenses[count].amount,
                        &expenses[count].category,
                        &expenses[count].month,
                        &expenses[count].year);

        // Support old records without date
        if (result == 3) {
            expenses[count].month = currentMonth;
            expenses[count].year = currentYear;
        }

        if ((result == 3 || result == 5) &&
            expenses[count].amount > 0 &&
            expenses[count].category >= 1 &&
            expenses[count].category <= 5 &&
            expenses[count].month >= 1 &&
            expenses[count].month <= 12 &&
            expenses[count].year > 0) {

            count++;
        }
    }

    fclose(fp);
}

// Save Monthly Budget
void saveBudget() {
    FILE *fp = fopen("budgets.txt", "a");

    if (fp == NULL) {
        printf("Unable to save budget!\n");
        return;
    }

    fprintf(fp, "%d %d %.2f\n",
            currentMonth,
            currentYear,
            monthlyBudget);

    fclose(fp);
}

// Load Monthly Budget
void loadBudget() {
    FILE *fp = fopen("budgets.txt", "r");

    int month, year;
    float budget;
    int found = 0;

    int budgetFileExists = (fp != NULL);

    monthlyBudget = 0;

    if (fp != NULL) {

        while (fscanf(fp, "%d %d %f",
                      &month, &year, &budget) == 3) {

            if (month == currentMonth &&
                year == currentYear &&
                budget > 0) {

                monthlyBudget = budget;
                found = 1;
            }
        }

        fclose(fp);
    }

    // Migrate old budget file only once
    if (!found && !budgetFileExists) {

        fp = fopen("budget.txt", "r");

        if (fp != NULL) {

            if (fscanf(fp, "%f", &budget) == 1 &&
                budget > 0) {

                monthlyBudget = budget;
                saveBudget();
            }

            fclose(fp);
        }
    }
}

// Set Monthly Budget
void setBudget() {
    float budget;

    printf("\nEnter your monthly budget in Rs: ");

    if (scanf("%f", &budget) != 1 || budget <= 0) {
        printf("Invalid budget amount!\n");
        return;
    }

    monthlyBudget = budget;

    saveBudget();

    printf("\nMonthly budget set successfully!\n");
    printf("Your Budget: Rs. %.2f\n", monthlyBudget);
}

// View Monthly Budget Status
void viewBudget() {
    float total = 0;

    for (int i = 0; i < count; i++) {
        if (expenses[i].month == currentMonth &&
            expenses[i].year == currentYear) {

            total += expenses[i].amount;
        }
    }

    printf("\n----- MONTHLY BUDGET -----\n");

    printf("Month: %s %d\n",
           months[currentMonth], currentYear);

    if (monthlyBudget <= 0) {
        printf("Please set your monthly budget first.\n");
        printf("Current expenses: Rs. %.2f\n", total);
        return;
    }

    printf("Monthly Budget: Rs. %.2f\n",
           monthlyBudget);

    printf("Total Expenses: Rs. %.2f\n", total);

    if (total > monthlyBudget) {

        printf("\nWarning! Budget exceeded!\n");

        printf("Exceeded Amount: Rs. %.2f\n",
               total - monthlyBudget);

    } else {

        printf("\nRemaining Budget: Rs. %.2f\n",
               monthlyBudget - total);
    }
}

// Main Function
int main() {
    int choice;

    time_t now = time(NULL);

    struct tm *date = localtime(&now);

    if (date == NULL) {
        printf("Unable to get current date!\n");
        return 1;
    }

    currentMonth = date->tm_mon + 1;
    currentYear = date->tm_year + 1900;

    loadExpenses();
    loadBudget();

    do {

        printf("\n===== SMART EXPENSE TRACKER =====\n");

        printf("Date: %s %d\n",
               months[currentMonth], currentYear);

        printf("\n1. Add Expense\n");
        printf("2. View All Expense History\n");
        printf("3. Calculate Current Month Total\n");
        printf("4. Exit\n");
        printf("5. Set Monthly Budget\n");
        printf("6. View Monthly Budget Status\n");

        printf("\nEnter your choice: ");

        if (scanf("%d", &choice) != 1) {
            printf("Invalid input!\n");
            return 1;
        }

        switch (choice) {

            case 1:
                addExpense();
                break;

            case 2:
                viewExpenses();
                break;

            case 3:
                calculateTotal();
                break;

            case 4:
                printf("\nThank you for using Smart Expense Tracker!\n");
                break;

            case 5:
                setBudget();
                break;

            case 6:
                viewBudget();
                break;

            default:
                printf("Invalid choice!\n");
        }

    } while (choice != 4);

    return 0;
}
