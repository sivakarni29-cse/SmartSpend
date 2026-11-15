// ========================================
// SMARTSPEND - EXPENSE TRACKER
// CONNECTED TO C BACKEND
// ========================================

const API_URL = "http://127.0.0.1:8080";

// GET HTML ELEMENTS
const expenseForm = document.getElementById("expenseForm");
const transactionList = document.getElementById("transactionList");

const totalExpensesElement = document.getElementById("totalExpenses");
const remainingBalanceElement = document.getElementById("remainingBalance");
const monthlyBudgetElement = document.getElementById("monthlyBudget");

const budgetForm = document.getElementById("budgetForm");


// ========================================
// LOAD SAVED BROWSER DATA
// ========================================

let monthlyBudget = Number(localStorage.getItem("monthlyBudget")) || 5000;

// Keep existing browser expenses separately.
let localExpenses = JSON.parse(localStorage.getItem("expenses")) || [];

// Expenses received from the C backend.
let backendExpenses = [];

// Combined expenses displayed on the dashboard.
let expenses = [...localExpenses];


// ========================================
// CONVERT C DATA INTO FRONTEND FORMAT
// ========================================

function convertBackendExpense(expense) {

    const categories = {
        1: "Food",
        2: "Travel",
        3: "Education",
        4: "Shopping",
        5: "Other"
    };

    const day = Number(expense.day) || 1;
    const month = Number(expense.month);
    const year = Number(expense.year);

    const date =
        `${String(day).padStart(2, "0")}/${String(month).padStart(2, "0")}/${year}`;

    return {
        name: expense.name,
        amount: Number(expense.amount),
        category: categories[Number(expense.category)] || "Other",
        date: date,
        day: day,

        // JavaScript months start at 0.
        // C backend months start at 1.
        month: month - 1,
        year: year,

        source: "C backend"
    };
}


// ========================================
// LOAD EXPENSES FROM C BACKEND
// ========================================

async function loadBackendExpenses() {

    try {

        const response = await fetch(`${API_URL}/api/expenses`);

        if (!response.ok) {
            throw new Error("Could not load expenses from C server.");
        }

        const data = await response.json();

        if (!Array.isArray(data)) {
            throw new Error("Invalid expense data received.");
        }

        backendExpenses = data.map(convertBackendExpense);

        // Combine backend and browser expenses.
        expenses = [...backendExpenses, ...localExpenses];

        displayExpenses();

        console.log("C backend connected successfully!");
        console.log("Backend expenses:", backendExpenses);

    } catch (error) {

        console.error("C backend connection failed:", error);

        // Existing browser expenses remain available.
        expenses = [...localExpenses];

        displayExpenses();

        console.warn(
            "Showing browser expenses because the C backend is unavailable."
        );

    }

}


// ========================================
// LOAD MONTHLY BUDGET FROM C BACKEND
// ========================================

async function loadBackendBudget() {

    const today = new Date();
    const month = today.getMonth() + 1;
    const year = today.getFullYear();

    try {

        const response = await fetch(
            `${API_URL}/api/budget?month=${month}&year=${year}`
        );

        if (!response.ok) {
            throw new Error("Could not load budget from C server.");
        }

        const data = await response.json();
        const savedBudget = Number(data.budget);

        if (Number.isFinite(savedBudget) && savedBudget > 0) {

            monthlyBudget = savedBudget;

            displayExpenses();

            console.log("Budget loaded from C backend:", monthlyBudget);

        }

    } catch (error) {

        console.error("Could not load C backend budget:", error);

        console.warn("Keeping the existing browser budget.");

    }

}


// ========================================
// SAVE LOCAL EXPENSES
// ========================================

function saveLocalExpenses() {

    localStorage.setItem(
        "expenses",
        JSON.stringify(localExpenses)
    );

}


// ========================================
// GET CURRENT MONTH EXPENSES
// ========================================

function getCurrentMonthExpenses() {

    const today = new Date();

    return expenses.filter(expense => {

        if (expense.month !== undefined && expense.year !== undefined) {

            return (
                Number(expense.month) === today.getMonth() &&
                Number(expense.year) === today.getFullYear()
            );

        }

        if (!expense.date) return false;

        const parts = expense.date.split("/");

        if (parts.length !== 3) return false;

        return (
            Number(parts[1]) - 1 === today.getMonth() &&
            Number(parts[2]) === today.getFullYear()
        );

    });

}


// ========================================
// DISPLAY EXPENSES
// ========================================

function displayExpenses() {

    transactionList.innerHTML = "";

    if (expenses.length === 0) {

        transactionList.innerHTML = `
            <p>No transactions yet.</p>
            <span>Your expenses will appear here.</span>
        `;

    }

    expenses.forEach((expense, index) => {

        const item = document.createElement("div");
        item.className = "expense-item";

        const name = document.createElement("strong");
        name.textContent = expense.name;

        const details = document.createElement("p");

        details.textContent =
            `${expense.category} • ${expense.date}`;

        const amount = document.createElement("span");

        amount.textContent =
            `₹${Number(expense.amount).toFixed(2)}`;

        const deleteButton = document.createElement("button");

        // Backend expenses cannot be deleted yet.
        if (expense.source === "C backend") {

            deleteButton.textContent = "C File";
            deleteButton.className = "delete-button";
            deleteButton.disabled = true;
            deleteButton.title = "Saved in expenses.txt";

        } else {

            deleteButton.textContent = "Delete";
            deleteButton.className = "delete-button";

            deleteButton.addEventListener("click", function() {

                if (confirm(`Delete "${expense.name}"?`)) {

                    const localIndex = localExpenses.indexOf(expense);

                    if (localIndex !== -1) {

                        localExpenses.splice(localIndex, 1);

                        saveLocalExpenses();

                        expenses = [
                            ...backendExpenses,
                            ...localExpenses
                        ];

                        displayExpenses();

                    }

                }

            });

        }

        item.append(name, details, amount, deleteButton);

        transactionList.appendChild(item);

    });


    // ========================================
    // CURRENT MONTH TOTAL
    // ========================================

    const monthlyExpenses = getCurrentMonthExpenses();

    const monthlyTotal = monthlyExpenses.reduce(
        (total, expense) => total + Number(expense.amount),
        0
    );


    // ========================================
    // UPDATE DASHBOARD
    // ========================================

    totalExpensesElement.textContent =
        `₹${monthlyTotal.toFixed(2)}`;

    monthlyBudgetElement.textContent =
        `₹${monthlyBudget.toFixed(2)}`;

    remainingBalanceElement.textContent =
        `₹${(monthlyBudget - monthlyTotal).toFixed(2)}`;


    // UPDATE OTHER SECTIONS
    displayMonthlyReport();
    displayBudgetProgress();
    displaySpendingInsights();

}


// ========================================
// ADD NEW EXPENSE
// ========================================

expenseForm.addEventListener("submit", async function(event) {

    event.preventDefault();

    const name = document.getElementById("expenseName").value.trim();

    const amount = Number(
        document.getElementById("expenseAmount").value
    );

    const category = document.getElementById("expenseCategory").value;

    const categoryIds = {
        "Food": 1,
        "Travel": 2,
        "Education": 3,
        "Shopping": 4,
        "Other": 5
    };

    if (!name || !Number.isFinite(amount) || amount <= 0 ||
        !categoryIds[category]) {

        alert("Please enter valid expense details.");

        return;

    }

    const today = new Date();

    const backendExpense = {
        name: name,
        amount: amount,
        category: categoryIds[category],
        day: today.getDate(),
        month: today.getMonth() + 1,
        year: today.getFullYear()
    };

    try {

        const response = await fetch(`${API_URL}/api/expenses`, {

            method: "POST",

            headers: {
                "Content-Type": "application/json"
            },

            body: JSON.stringify(backendExpense)

        });

        if (!response.ok) {
            throw new Error("The C server could not save this expense.");
        }

        // Refresh from C so the backend remains the source of truth.
        await loadBackendExpenses();

        expenseForm.reset();

        alert("Expense saved successfully in the C backend!");

    } catch (error) {

        console.error("Expense save failed:", error);

        alert(
            "Could not save the expense to the C backend. " +
            "Your existing expenses have been kept. Please check that the C server is running."
        );

    }

});


// ========================================
// MONTHLY BUDGET
// ========================================

budgetForm.addEventListener("submit", async function(event) {

    event.preventDefault();

    const newBudget = Number(
        document.getElementById("budgetInput").value
    );

    if (!Number.isFinite(newBudget) || newBudget <= 0) {

        alert("Please enter a valid budget.");

        return;

    }

    const today = new Date();

    const budgetData = {
        month: today.getMonth() + 1,
        year: today.getFullYear(),
        budget: newBudget
    };

    try {

        const response = await fetch(`${API_URL}/api/budget`, {

            method: "POST",

            headers: {
                "Content-Type": "application/json"
            },

            body: JSON.stringify(budgetData)

        });

        if (!response.ok) {
            throw new Error("The C server could not save the budget.");
        }

        monthlyBudget = newBudget;

        localStorage.setItem("monthlyBudget", monthlyBudget);

        displayExpenses();

        document.getElementById("budgetMessage").textContent =
            "Monthly budget saved successfully in the C backend!";

        budgetForm.reset();

    } catch (error) {

        console.error("Budget save failed:", error);

        document.getElementById("budgetMessage").textContent =
            "Could not save budget to C. Please check that the server is running.";

    }

});


// ========================================
// BUDGET PROGRESS BAR
// ========================================

function displayBudgetProgress() {

    const percentageElement =
        document.getElementById("budgetPercentage");

    const progressBar =
        document.getElementById("budgetProgressBar");

    const progressText =
        document.getElementById("budgetProgressText");


    if (!percentageElement || !progressBar || !progressText) return;


    const monthlyExpenses = getCurrentMonthExpenses();

    const monthlyTotal = monthlyExpenses.reduce(
        (total, expense) => total + Number(expense.amount),
        0
    );


    const percentage = (monthlyTotal / monthlyBudget) * 100;


    percentageElement.textContent =
        `${Math.round(percentage)}%`;


    progressBar.style.width =
        `${Math.min(percentage, 100)}%`;


    if (percentage >= 100) {

        progressBar.style.background = "#EF6B78";

    } else if (percentage >= 75) {

        progressBar.style.background = "#F5A623";

    } else {

        progressBar.style.background =
            "linear-gradient(90deg, #A78BFA, #7C5AC8)";

    }


    progressText.textContent =
        `₹${monthlyTotal.toFixed(2)} spent out of ₹${monthlyBudget.toFixed(2)}`;

}


// ========================================
// SMART SPENDING INSIGHTS
// ========================================

function displaySpendingInsights() {

    const insightTitle =
        document.getElementById("spendingInsightTitle");

    const insightText =
        document.getElementById("spendingInsightText");


    if (!insightTitle || !insightText) return;


    const monthlyExpenses = getCurrentMonthExpenses();

    const monthlyTotal = monthlyExpenses.reduce(
        (total, expense) => total + Number(expense.amount),
        0
    );


    const remaining = monthlyBudget - monthlyTotal;

    const percentage = (monthlyTotal / monthlyBudget) * 100;


    // FIND HIGHEST SPENDING CATEGORY
    const categoryTotals = {};

    monthlyExpenses.forEach(expense => {

        categoryTotals[expense.category] =
            (categoryTotals[expense.category] || 0) +
            Number(expense.amount);

    });


    const categories = Object.entries(categoryTotals);

    let highestCategory = "None";

    if (categories.length > 0) {

        highestCategory = categories.reduce(
            (highest, current) =>
                current[1] > highest[1] ? current : highest
        )[0];

    }


    // DISPLAY INSIGHTS
    if (monthlyExpenses.length === 0) {

        insightTitle.textContent = "Start Tracking!";

        insightText.textContent =
            "Add your expenses to receive useful monthly spending insights.";

    } else if (percentage >= 100) {

        insightTitle.textContent = "Budget Limit Reached!";

        insightText.textContent =
            `You have spent ₹${monthlyTotal.toFixed(2)}. You are over your budget by ₹${Math.abs(remaining).toFixed(2)}. Your highest spending category is ${highestCategory}.`;

    } else if (percentage >= 75) {

        insightTitle.textContent = "Watch Your Spending!";

        insightText.textContent =
            `You have used ${Math.round(percentage)}% of your budget. Only ₹${remaining.toFixed(2)} remains. Your highest spending category is ${highestCategory}.`;

    } else {

        insightTitle.textContent = "Good Budget Progress!";

        insightText.textContent =
            `You have ₹${remaining.toFixed(2)} remaining. Your highest spending category is ${highestCategory}. Keep tracking your expenses!`;

    }

}


// ========================================
// MONTHLY REPORTS
// ========================================

function displayMonthlyReport() {

    const monthlyExpenses = getCurrentMonthExpenses();

    let monthlyTotal = 0;

    const categoryTotals = {};


    monthlyExpenses.forEach(expense => {

        const amount = Number(expense.amount);

        monthlyTotal += amount;

        categoryTotals[expense.category] =
            (categoryTotals[expense.category] || 0) + amount;

    });


    document.getElementById("reportTotal").textContent =
        `₹${monthlyTotal.toFixed(2)}`;


    const categoryReport =
        document.getElementById("categoryReport");

    categoryReport.innerHTML = "";


    const categories = Object.entries(categoryTotals);


    // UPDATE GRAPH
    displayExpenseChart(categoryTotals);


    if (categories.length === 0) {

        categoryReport.textContent =
            "No expenses recorded this month.";

        document.getElementById("topCategory").textContent =
            "None";

        return;

    }


    // CATEGORY-WISE TOTALS
    categories.forEach(([category, amount]) => {

        const row = document.createElement("p");

        row.style.padding = "12px 0";

        row.style.borderBottom = "1px solid #eeeaf7";

        row.textContent =
            `${category}: ₹${amount.toFixed(2)}`;

        categoryReport.appendChild(row);

    });


    // HIGHEST CATEGORY
    const highestCategory = categories.reduce(
        (highest, current) =>
            current[1] > highest[1] ? current : highest
    );


    document.getElementById("topCategory").textContent =
        highestCategory[0];

}


// ========================================
// MONTHLY SPENDING GRAPH
// ========================================

function displayExpenseChart(categoryTotals) {

    const chart = document.getElementById("expenseChart");

    if (!chart) return;

    chart.innerHTML = "";


    const categories = [

        { name: "Food", className: "food" },
        { name: "Travel", className: "travel" },
        { name: "Education", className: "education" },
        { name: "Shopping", className: "shopping" },
        { name: "Other", className: "other" }

    ];


    const values = categories.map(
        category => Number(categoryTotals[category.name]) || 0
    );


    const maxAmount = Math.max(...values);


    if (maxAmount === 0) {

        chart.textContent = "No spending data available yet.";

        return;

    }


    categories.forEach((category, index) => {

        const amount = values[index];

        const percentage = (amount / maxAmount) * 100;


        const row = document.createElement("div");

        row.className = "chart-row";


        const label = document.createElement("div");

        label.className = "chart-label";


        const categoryName = document.createElement("span");

        categoryName.textContent = category.name;


        const categoryAmount = document.createElement("span");

        categoryAmount.textContent =
            `₹${amount.toFixed(2)}`;


        label.append(categoryName, categoryAmount);


        const track = document.createElement("div");

        track.className = "chart-track";


        const bar = document.createElement("div");

        bar.className = `chart-bar ${category.className}`;

        bar.style.width = `${percentage}%`;


        track.appendChild(bar);

        row.append(label, track);

        chart.appendChild(row);

    });

}


// ========================================
// INITIALIZE APPLICATION
// ========================================

const todayDate = document.getElementById("todayDate");

if (todayDate) {

    todayDate.textContent = new Date().toLocaleDateString(
        "en-IN",
        {
            weekday: "long",
            day: "numeric",
            month: "long",
            year: "numeric"
        }
    );

}


// LOAD DASHBOARD
// First show existing browser expenses.
displayExpenses();

// Then load expenses and monthly budget from C backend.
loadBackendExpenses();
loadBackendBudget();