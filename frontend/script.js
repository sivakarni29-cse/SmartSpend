
// ========================================
// SMARTSPEND - AUTHENTICATED EXPENSE TRACKER
// CONNECTED TO C BACKEND
// ========================================

// LOCAL TESTING ADDRESS
const API_URL = "http://127.0.0.1:8080";

// ========================================
// GET HTML ELEMENTS
// ========================================

const authSection = document.getElementById("authSection");
const dashboardApp = document.getElementById("dashboardApp");

const loginForm = document.getElementById("loginForm");
const registerForm = document.getElementById("registerForm");

const loginBox = document.getElementById("loginBox");
const registerBox = document.getElementById("registerBox");

const loginMessage = document.getElementById("loginMessage");
const registerMessage = document.getElementById("registerMessage");

const expenseForm = document.getElementById("expenseForm");
const budgetForm = document.getElementById("budgetForm");

const transactionList = document.getElementById("transactionList");

const totalExpensesElement = document.getElementById("totalExpenses");
const remainingBalanceElement = document.getElementById("remainingBalance");
const monthlyBudgetElement = document.getElementById("monthlyBudget");

const categoryIds = {
    Food: 1,
    Travel: 2,
    Education: 3,
    Shopping: 4,
    Other: 5
};

const categoryNames = {
    1: "Food",
    2: "Travel",
    3: "Education",
    4: "Shopping",
    5: "Other"
};

// ========================================
// ACCOUNT SESSION
// ========================================

// Token is kept only for this browser tab session.
let authToken = sessionStorage.getItem("smartSpendToken");
let loggedInUsername = sessionStorage.getItem("smartSpendUsername") || "";

let monthlyBudget = 0;
let expenses = [];


// ========================================
// API REQUEST HELPER
// ========================================

async function apiRequest(path, options = {}) {

    const headers = {
        ...(options.headers || {})
    };

    if (authToken) {
        headers.Authorization = `Bearer ${authToken}`;
    }

    if (options.body && !headers["Content-Type"]) {
        headers["Content-Type"] = "application/json";
    }

    const response = await fetch(`${API_URL}${path}`, {
        ...options,
        headers
    });

    const responseText = await response.text();

    let data = {};

    if (responseText) {
        try {
            data = JSON.parse(responseText);
        } catch {
            data = { message: responseText };
        }
    }

    if (!response.ok) {
        throw new Error(
            data.error || data.message || `Request failed: ${response.status}`
        );
    }

    return data;
}


// ========================================
// SHOW LOGIN / REGISTER
// ========================================

document.getElementById("showRegister").addEventListener("click", function() {

    loginBox.style.display = "none";
    registerBox.style.display = "block";

    loginMessage.textContent = "";
    registerMessage.textContent = "";

});


document.getElementById("showLogin").addEventListener("click", function() {

    registerBox.style.display = "none";
    loginBox.style.display = "block";

    loginMessage.textContent = "";
    registerMessage.textContent = "";

});


// ========================================
// SHOW DASHBOARD
// ========================================

function showDashboard() {

    authSection.style.display = "none";
    dashboardApp.style.display = "flex";

    document.getElementById("loggedInUsername").textContent =
        loggedInUsername;

}


// ========================================
// SHOW LOGIN SCREEN
// ========================================

function showLoginScreen() {

    authSection.style.display = "flex";
    dashboardApp.style.display = "none";

    loginBox.style.display = "block";
    registerBox.style.display = "none";

    loginForm.reset();
    registerForm.reset();

    loginMessage.textContent = "";
    registerMessage.textContent = "";

}


// ========================================
// REGISTER ACCOUNT
// ========================================

registerForm.addEventListener("submit", async function(event) {

    event.preventDefault();

    const username =
        document.getElementById("registerUsername").value.trim();

    const password =
        document.getElementById("registerPassword").value;

    if (!/^[a-zA-Z0-9_]{3,32}$/.test(username)) {

        registerMessage.textContent =
            "Username must contain 3–32 letters, numbers or underscores.";

        return;
    }

    if (password.length < 8 || password.length > 128) {

        registerMessage.textContent =
            "Password must contain 8–128 characters.";

        return;
    }

    try {

        const data = await apiRequest("/api/register", {
            method: "POST",
            body: JSON.stringify({
                username: username,
                password: password
            })
        });

        registerMessage.textContent =
            data.message || "Account created successfully!";

        registerForm.reset();

        alert("Account created! Please login.");

        registerBox.style.display = "none";
        loginBox.style.display = "block";

        document.getElementById("loginUsername").value = username;

    } catch (error) {

        console.error("Registration failed:", error);

        registerMessage.textContent = error.message;

    }

});


// ========================================
// LOGIN ACCOUNT
// ========================================

loginForm.addEventListener("submit", async function(event) {

    event.preventDefault();

    const username =
        document.getElementById("loginUsername").value.trim();

    const password =
        document.getElementById("loginPassword").value;

    try {

        loginMessage.textContent = "Logging in...";

        const data = await apiRequest("/api/login", {
            method: "POST",
            body: JSON.stringify({
                username: username,
                password: password
            })
        });

        if (!data.token) {
            throw new Error("Login response did not contain an authentication token.");
        }

        authToken = data.token;
        loggedInUsername = username;

        sessionStorage.setItem("smartSpendToken", authToken);
        sessionStorage.setItem("smartSpendUsername", loggedInUsername);

        expenses = [];
        monthlyBudget = 0;

        loginForm.reset();

        showDashboard();

        await loadDashboardData();

    } catch (error) {

        console.error("Login failed:", error);

        loginMessage.textContent = error.message;

    }

});


// ========================================
// LOGOUT
// ========================================

document.getElementById("logoutButton").addEventListener("click", async function() {

    if (!confirm("Are you sure you want to logout?")) {
        return;
    }

    try {

        await apiRequest("/api/logout", {
            method: "POST",
            body: JSON.stringify({})
        });

    } catch (error) {

        console.error("Logout request failed:", error);

    }

    authToken = null;
    loggedInUsername = "";

    sessionStorage.removeItem("smartSpendToken");
    sessionStorage.removeItem("smartSpendUsername");

    expenses = [];
    monthlyBudget = 0;

    displayExpenses();

    showLoginScreen();

});


// ========================================
// CONVERT C DATA INTO FRONTEND FORMAT
// ========================================

function convertBackendExpense(expense) {

    const day = Number(expense.day) || 1;
    const month = Number(expense.month) || 1;
    const year = Number(expense.year);

    const date =
        `${String(day).padStart(2, "0")}/${String(month).padStart(2, "0")}/${year}`;

    return {
        id: Number(expense.id),
        name: expense.name,
        amount: Number(expense.amount),
        category: categoryNames[Number(expense.category)] || "Other",
        date: date,
        day: day,
        month: month - 1,
        year: year,
        source: "C backend"
    };

}


// ========================================
// LOAD EXPENSES FROM C BACKEND
// ========================================

async function loadBackendExpenses() {

    const data = await apiRequest("/api/expenses", {
        method: "GET"
    });

    if (!Array.isArray(data)) {
        throw new Error("Invalid expense data received.");
    }

    expenses = data.map(convertBackendExpense);

    displayExpenses();

}


// ========================================
// LOAD MONTHLY BUDGET
// ========================================

async function loadBackendBudget() {

    const today = new Date();

    const month = today.getMonth() + 1;
    const year = today.getFullYear();

    const data = await apiRequest(
        `/api/budget?month=${month}&year=${year}`,
        { method: "GET" }
    );

    monthlyBudget = Number(data.budget) || 0;

    document.getElementById("budgetInput").value =
        monthlyBudget > 0 ? monthlyBudget : "";

    displayExpenses();

}


// ========================================
// LOAD DASHBOARD DATA
// ========================================

async function loadDashboardData() {

    expenses = [];
    monthlyBudget = 0;

    displayExpenses();

    try {

        await Promise.all([
            loadBackendExpenses(),
            loadBackendBudget()
        ]);

        console.log("Account data loaded successfully.");

    } catch (error) {

        console.error("Could not load dashboard data:", error);

        alert(
            "Could not load your account data. " +
            "Please check that the C server is running and try again."
        );

    }

}


// ========================================
// GET CURRENT MONTH EXPENSES
// ========================================

function getCurrentMonthExpenses() {

    const today = new Date();

    return expenses.filter(expense => {

        return (
            Number(expense.month) === today.getMonth() &&
            Number(expense.year) === today.getFullYear()
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

    expenses.forEach(expense => {

        const item = document.createElement("div");
        item.className = "expense-item";

        const name = document.createElement("strong");
        name.textContent = expense.name;

        const details = document.createElement("p");
        details.textContent = `${expense.category} • ${expense.date}`;

        const amount = document.createElement("span");
        amount.textContent = `₹${Number(expense.amount).toFixed(2)}`;

        // EDIT BUTTON
        const editButton = document.createElement("button");

        editButton.textContent = "Edit";
        editButton.className = "edit-button";

        editButton.addEventListener("click", async function() {

            const newName = prompt("Enter expense name:", expense.name);

            if (newName === null) return;

            const newAmount = prompt("Enter amount:", expense.amount);

            if (newAmount === null) return;

            const newCategory = prompt(
                "Enter category: Food, Travel, Education, Shopping or Other",
                expense.category
            );

            if (newCategory === null) return;

            const cleanName = newName.trim();
            const cleanCategory = newCategory.trim();

            const amountValue = Number(newAmount);

            if (
                !cleanName ||
                !Number.isFinite(amountValue) ||
                amountValue <= 0 ||
                !categoryIds[cleanCategory]
            ) {

                alert("Please enter valid expense details and category.");
                return;

            }

            const updatedExpense = {
                id: expense.id,
                name: cleanName,
                amount: amountValue,
                category: categoryIds[cleanCategory],
                day: expense.day,
                month: expense.month + 1,
                year: expense.year
            };

            try {

                await apiRequest("/api/expenses", {
                    method: "PUT",
                    body: JSON.stringify(updatedExpense)
                });

                await loadBackendExpenses();

                alert("Expense updated successfully!");

            } catch (error) {

                console.error("Update failed:", error);
                alert("Could not update expense: " + error.message);

            }

        });


        // DELETE BUTTON
        const deleteButton = document.createElement("button");

        deleteButton.textContent = "Delete";
        deleteButton.className = "delete-button";

        deleteButton.addEventListener("click", async function() {

            if (!confirm(`Delete "${expense.name}"?`)) {
                return;
            }

            try {

                await apiRequest(
                    `/api/expenses?id=${encodeURIComponent(expense.id)}`,
                    { method: "DELETE" }
                );

                await loadBackendExpenses();

                alert("Expense deleted successfully!");

            } catch (error) {

                console.error("Delete failed:", error);
                alert("Could not delete expense: " + error.message);

            }

        });

        item.append(name, details, amount, editButton, deleteButton);

        transactionList.appendChild(item);

    });


    // CURRENT MONTH TOTAL
    const monthlyExpenses = getCurrentMonthExpenses();

    const monthlyTotal = monthlyExpenses.reduce(
        (total, expense) => total + Number(expense.amount),
        0
    );


    // UPDATE DASHBOARD
    totalExpensesElement.textContent =
        `₹${monthlyTotal.toFixed(2)}`;

    monthlyBudgetElement.textContent =
        `₹${monthlyBudget.toFixed(2)}`;

    remainingBalanceElement.textContent =
        `₹${(monthlyBudget - monthlyTotal).toFixed(2)}`;


    displayMonthlyReport();
    displayBudgetProgress();
    displaySpendingInsights();

}


// ========================================
// ADD NEW EXPENSE
// ========================================

expenseForm.addEventListener("submit", async function(event) {

    event.preventDefault();

    const name =
        document.getElementById("expenseName").value.trim();

    const amount =
        Number(document.getElementById("expenseAmount").value);

    const category =
        document.getElementById("expenseCategory").value;

    if (
        !name ||
        !Number.isFinite(amount) ||
        amount <= 0 ||
        !categoryIds[category]
    ) {

        alert("Please enter valid expense details.");
        return;

    }

    const today = new Date();

    const expenseData = {
        name: name,
        amount: amount,
        category: categoryIds[category],
        day: today.getDate(),
        month: today.getMonth() + 1,
        year: today.getFullYear()
    };

    try {

        await apiRequest("/api/expenses", {
            method: "POST",
            body: JSON.stringify(expenseData)
        });

        await loadBackendExpenses();

        expenseForm.reset();

        alert("Expense saved successfully!");

    } catch (error) {

        console.error("Expense save failed:", error);

        alert("Could not save expense: " + error.message);

    }

});


// ========================================
// MONTHLY BUDGET
// ========================================

budgetForm.addEventListener("submit", async function(event) {

    event.preventDefault();

    const newBudget =
        Number(document.getElementById("budgetInput").value);

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

        await apiRequest("/api/budget", {
            method: "POST",
            body: JSON.stringify(budgetData)
        });

        monthlyBudget = newBudget;

        displayExpenses();

        document.getElementById("budgetMessage").textContent =
            "Monthly budget saved successfully!";

        budgetForm.reset();

    } catch (error) {

        console.error("Budget save failed:", error);

        document.getElementById("budgetMessage").textContent =
            "Could not save budget: " + error.message;

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

    const percentage =
        monthlyBudget > 0 ? (monthlyTotal / monthlyBudget) * 100 : 0;

    percentageElement.textContent = `${Math.round(percentage)}%`;

    progressBar.style.width = `${Math.min(percentage, 100)}%`;

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

    const percentage =
        monthlyBudget > 0 ? (monthlyTotal / monthlyBudget) * 100 : 0;

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

    if (monthlyExpenses.length === 0) {

        insightTitle.textContent = "Start Tracking!";

        insightText.textContent =
            "Add your expenses to receive useful monthly spending insights.";

    } else if (monthlyBudget <= 0) {

        insightTitle.textContent = "Set Your Budget";

        insightText.textContent =
            "You have recorded expenses. Set a monthly budget to track your spending.";

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

    displayExpenseChart(categoryTotals);

    if (categories.length === 0) {

        categoryReport.textContent =
            "No expenses recorded this month.";

        document.getElementById("topCategory").textContent =
            "None";

        return;

    }

    categories.forEach(([category, amount]) => {

        const row = document.createElement("p");

        row.style.padding = "12px 0";
        row.style.borderBottom = "1px solid #eeeaf7";

        row.textContent =
            `${category}: ₹${amount.toFixed(2)}`;

        categoryReport.appendChild(row);

    });

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
        categoryAmount.textContent = `₹${amount.toFixed(2)}`;

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
// DATE DISPLAY
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


// ========================================
// APPLICATION START
// ========================================

async function initializeApplication() {

    if (authToken && loggedInUsername) {

        showDashboard();

        await loadDashboardData();

    } else {

        showLoginScreen();

    }

}

initializeApplication();
