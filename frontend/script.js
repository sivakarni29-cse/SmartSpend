// ========================================
// SMARTSPEND - AUTHENTICATED EXPENSE TRACKER
// CONNECTED TO C BACKEND
// ========================================

const PROD_API_URL = "https://smartspend-pcwl.onrender.com";
const LOCAL_API_URL = "http://localhost:8080";

let API_URL = PROD_API_URL;
let apiResolved = false;

async function resolveApiUrl() {
    if (apiResolved) return API_URL;

    const params = new URLSearchParams(window.location.search);
    const override = params.get("api");

    if (override === "local") {
        API_URL = LOCAL_API_URL;
        apiResolved = true;
        return API_URL;
    }

    if (override === "prod") {
        API_URL = PROD_API_URL;
        apiResolved = true;
        return API_URL;
    }

    const isLocalContext =
        window.location.hostname === "localhost" ||
        window.location.hostname === "127.0.0.1" ||
        window.location.protocol === "file:";

    if (isLocalContext) {
        const controller = new AbortController();
        const timer = setTimeout(() => controller.abort(), 1200);

        try {
            const response = await fetch(`${LOCAL_API_URL}/api/status`, {
                method: "GET",
                signal: controller.signal
            });
            if (response.ok) {
                API_URL = LOCAL_API_URL;
            }
        } catch {
            API_URL = PROD_API_URL;
        } finally {
            clearTimeout(timer);
        }
    }

    apiResolved = true;
    return API_URL;
}

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
const budgetMessage = document.getElementById("budgetMessage");

const expenseForm = document.getElementById("expenseForm");
const budgetForm = document.getElementById("budgetForm");

const transactionList = document.getElementById("transactionList");
const totalExpensesElement = document.getElementById("totalExpenses");
const remainingBalanceElement = document.getElementById("remainingBalance");
const remainingBalanceCard = document.getElementById("remainingBalanceCard");
const remainingBalanceLabel = document.getElementById("remainingBalanceLabel");
const monthlyBudgetElement = document.getElementById("monthlyBudget");

const monthSelector = document.getElementById("monthSelector");
const expenseDateInput = document.getElementById("expenseDate");
const searchExpenseInput = document.getElementById("searchExpense");
const filterCategorySelect = document.getElementById("filterCategory");
const filterScopeSelect = document.getElementById("filterScope");
const exportCsvButton = document.getElementById("exportCsvButton");

// Modals & Toasts
const editExpenseModal = document.getElementById("editExpenseModal");
const editExpenseForm = document.getElementById("editExpenseForm");
const closeEditModalBtn = document.getElementById("closeEditModal");
const cancelEditModalBtn = document.getElementById("cancelEditModal");

const confirmModal = document.getElementById("confirmModal");
const confirmModalTitle = document.getElementById("confirmModalTitle");
const confirmModalMessage = document.getElementById("confirmModalMessage");
const confirmCancelBtn = document.getElementById("confirmCancelBtn");
const confirmOkBtn = document.getElementById("confirmOkBtn");

const toastContainer = document.getElementById("toastContainer");

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
// APPLICATION STATE
// ========================================

let authToken = sessionStorage.getItem("smartSpendToken");
let loggedInUsername = sessionStorage.getItem("smartSpendUsername") || "";

let monthlyBudget = 0;
let expenses = [];

const initialDate = new Date();
let selectedMonth = initialDate.getMonth(); // 0-11
let selectedYear = initialDate.getFullYear();

let confirmResolver = null;

// ========================================
// UTILITY HELPERS
// ========================================

function showToast(message, type = "info") {
    if (!toastContainer) return;

    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    toast.textContent = message;

    toastContainer.appendChild(toast);

    setTimeout(() => {
        toast.remove();
    }, 3200);
}

function setButtonLoading(button, isLoading, loadingText) {
    if (!button) return;

    if (isLoading) {
        if (!button.dataset.originalText) {
            button.dataset.originalText = button.textContent;
        }
        button.disabled = true;
        button.textContent = loadingText;
    } else {
        button.disabled = false;
        if (button.dataset.originalText) {
            button.textContent = button.dataset.originalText;
        }
    }
}

function formatDateInput(year, monthOneBased, day) {
    return `${year}-${String(monthOneBased).padStart(2, "0")}-${String(day).padStart(2, "0")}`;
}

function formatTodayInput() {
    const now = new Date();
    return formatDateInput(now.getFullYear(), now.getMonth() + 1, now.getDate());
}

function parseDateInput(dateString) {
    const match = /^(\d{4})-(\d{2})-(\d{2})$/.exec(dateString || "");
    if (!match) return null;

    const year = Number(match[1]);
    const month = Number(match[2]);
    const day = Number(match[3]);

    const test = new Date(year, month - 1, day);
    if (
        test.getFullYear() !== year ||
        test.getMonth() !== month - 1 ||
        test.getDate() !== day
    ) {
        return null;
    }

    return { year, month, day };
}

function getSelectedPeriodLabel() {
    const d = new Date(selectedYear, selectedMonth, 1);
    return d.toLocaleDateString("en-IN", { month: "long", year: "numeric" });
}

function syncPeriodLabels() {
    const label = getSelectedPeriodLabel();

    const budgetPeriodLabel = document.getElementById("budgetPeriodLabel");
    const expensesPeriodLabel = document.getElementById("expensesPeriodLabel");
    const reportPeriodLabel = document.getElementById("reportPeriodLabel");
    const budgetSectionPeriod = document.getElementById("budgetSectionPeriod");

    if (budgetPeriodLabel) budgetPeriodLabel.textContent = `${label} limit`;
    if (expensesPeriodLabel) expensesPeriodLabel.textContent = `${label} spending`;
    if (reportPeriodLabel) reportPeriodLabel.textContent = `(${label})`;
    if (budgetSectionPeriod) budgetSectionPeriod.textContent = `(${label})`;
}

// ========================================
// CONFIRMATION MODAL
// ========================================

function askConfirm(title, message, okLabel = "Confirm") {
    return new Promise(resolve => {
        confirmResolver = resolve;
        confirmModalTitle.textContent = title;
        confirmModalMessage.textContent = message;
        confirmOkBtn.textContent = okLabel;
        confirmModal.hidden = false;
        confirmOkBtn.focus();
    });
}

function closeConfirmModal(result) {
    confirmModal.hidden = true;
    if (confirmResolver) {
        const resolve = confirmResolver;
        confirmResolver = null;
        resolve(result);
    }
}

confirmCancelBtn.addEventListener("click", () => closeConfirmModal(false));
confirmOkBtn.addEventListener("click", () => closeConfirmModal(true));
confirmModal.addEventListener("click", event => {
    if (event.target === confirmModal) closeConfirmModal(false);
});

// ========================================
// EDIT EXPENSE MODAL
// ========================================

function openEditModal(expense) {
    document.getElementById("editExpenseId").value = expense.id;
    document.getElementById("editExpenseName").value = expense.name;
    document.getElementById("editExpenseAmount").value = expense.amount;
    document.getElementById("editExpenseCategory").value = expense.category;
    document.getElementById("editExpenseDate").value = formatDateInput(
        expense.year,
        expense.month + 1,
        expense.day
    );

    editExpenseModal.hidden = false;
    document.getElementById("editExpenseName").focus();
}

function closeEditModal() {
    editExpenseModal.hidden = true;
}

closeEditModalBtn.addEventListener("click", closeEditModal);
cancelEditModalBtn.addEventListener("click", closeEditModal);
editExpenseModal.addEventListener("click", event => {
    if (event.target === editExpenseModal) closeEditModal();
});

document.addEventListener("keydown", event => {
    if (event.key === "Escape") {
        if (!editExpenseModal.hidden) closeEditModal();
        if (!confirmModal.hidden) closeConfirmModal(false);
    }
});

// ========================================
// API REQUEST HELPER
// ========================================

function clearLocalSession() {
    authToken = null;
    loggedInUsername = "";

    sessionStorage.removeItem("smartSpendToken");
    sessionStorage.removeItem("smartSpendUsername");

    expenses = [];
    monthlyBudget = 0;
}

async function apiRequest(path, options = {}, { handleAuthError = true } = {}) {
    await resolveApiUrl();

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

    if (response.status === 401 && handleAuthError && authToken) {
        clearLocalSession();
        showLoginScreen();
        showToast("Your session expired. Please log in again.", "error");
        const authErr = new Error("Session expired");
        authErr.status = 401;
        throw authErr;
    }

    if (!response.ok) {
        const err = new Error(
            data.error || data.message || `Request failed: ${response.status}`
        );
        err.status = response.status;
        throw err;
    }

    return data;
}

// ========================================
// SHOW LOGIN / REGISTER / DASHBOARD
// ========================================

document.getElementById("showRegister").addEventListener("click", function () {
    loginBox.style.display = "none";
    registerBox.style.display = "block";
    loginMessage.textContent = "";
    registerMessage.textContent = "";
    registerMessage.className = "";
});

document.getElementById("showLogin").addEventListener("click", function () {
    registerBox.style.display = "none";
    loginBox.style.display = "block";
    loginMessage.textContent = "";
    registerMessage.textContent = "";
    loginMessage.className = "";
});

function showDashboard() {
    authSection.style.display = "none";
    dashboardApp.style.display = "flex";
    document.getElementById("loggedInUsername").textContent = loggedInUsername;

    if (monthSelector && !monthSelector.value) {
        monthSelector.value = `${selectedYear}-${String(selectedMonth + 1).padStart(2, "0")}`;
    }

    if (expenseDateInput && !expenseDateInput.value) {
        expenseDateInput.value = formatTodayInput();
    }

    syncPeriodLabels();
}

function showLoginScreen() {
    authSection.style.display = "flex";
    dashboardApp.style.display = "none";

    loginBox.style.display = "block";
    registerBox.style.display = "none";

    loginForm.reset();
    registerForm.reset();

    loginMessage.textContent = "";
    registerMessage.textContent = "";
    loginMessage.className = "";
    registerMessage.className = "";
}

// ========================================
// REGISTER ACCOUNT
// ========================================

registerForm.addEventListener("submit", async function (event) {
    event.preventDefault();

    const username = document.getElementById("registerUsername").value.trim();
    const password = document.getElementById("registerPassword").value;
    const submitBtn = document.getElementById("registerSubmitBtn");

    registerMessage.className = "";

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
        setButtonLoading(submitBtn, true, "Creating Account...");
        registerMessage.textContent = "";

        const data = await apiRequest(
            "/api/register",
            {
                method: "POST",
                body: JSON.stringify({ username, password })
            },
            { handleAuthError: false }
        );

        registerForm.reset();
        registerBox.style.display = "none";
        loginBox.style.display = "block";

        document.getElementById("loginUsername").value = username;
        loginMessage.className = "success";
        loginMessage.textContent =
            data.message || "Account created! Please login.";

        showToast("Account created successfully! Please login.", "success");
    } catch (error) {
        console.error("Registration failed:", error);
        registerMessage.textContent = error.message;
    } finally {
        setButtonLoading(submitBtn, false);
    }
});

// ========================================
// LOGIN ACCOUNT
// ========================================

loginForm.addEventListener("submit", async function (event) {
    event.preventDefault();

    const username = document.getElementById("loginUsername").value.trim();
    const password = document.getElementById("loginPassword").value;
    const submitBtn = document.getElementById("loginSubmitBtn");

    loginMessage.className = "";

    try {
        setButtonLoading(submitBtn, true, "Logging in...");
        loginMessage.textContent = "";

        const data = await apiRequest(
            "/api/login",
            {
                method: "POST",
                body: JSON.stringify({ username, password })
            },
            { handleAuthError: false }
        );

        if (!data.token) {
            throw new Error("Login response did not contain an authentication token.");
        }

        authToken = data.token;
        loggedInUsername = data.username || username;

        sessionStorage.setItem("smartSpendToken", authToken);
        sessionStorage.setItem("smartSpendUsername", loggedInUsername);

        expenses = [];
        monthlyBudget = 0;

        loginForm.reset();
        showDashboard();
        await loadDashboardData();
        showToast(`Welcome back, ${loggedInUsername}!`, "success");
    } catch (error) {
        console.error("Login failed:", error);
        loginMessage.textContent = error.message;
    } finally {
        setButtonLoading(submitBtn, false);
    }
});

// ========================================
// LOGOUT
// ========================================

document.getElementById("logoutButton").addEventListener("click", async function () {
    const confirmed = await askConfirm(
        "Logout",
        "Are you sure you want to log out of SmartSpend?",
        "Logout"
    );

    if (!confirmed) return;

    try {
        await apiRequest(
            "/api/logout",
            { method: "POST", body: JSON.stringify({}) },
            { handleAuthError: false }
        );
    } catch (error) {
        console.error("Logout request failed:", error);
    }

    clearLocalSession();
    displayExpenses();
    showLoginScreen();
    showToast("Logged out successfully.", "info");
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
        date,
        day,
        month: month - 1, // 0-indexed to match JS Date
        year,
        source: "C backend"
    };
}

// ========================================
// LOAD EXPENSES & BUDGET FROM C BACKEND
// ========================================

async function loadBackendExpenses() {
    const data = await apiRequest("/api/expenses", { method: "GET" });

    if (!Array.isArray(data)) {
        throw new Error("Invalid expense data received.");
    }

    expenses = data.map(convertBackendExpense);
    displayExpenses();
}

async function loadBackendBudget() {
    const month = selectedMonth + 1;
    const year = selectedYear;

    const data = await apiRequest(
        `/api/budget?month=${month}&year=${year}`,
        { method: "GET" }
    );

    monthlyBudget = Number(data.budget) || 0;

    const budgetInput = document.getElementById("budgetInput");
    if (budgetInput) {
        budgetInput.value = monthlyBudget > 0 ? monthlyBudget : "";
    }

    displayExpenses();
}

async function loadDashboardData() {
    expenses = [];
    monthlyBudget = 0;
    syncPeriodLabels();
    displayExpenses();

    try {
        await Promise.all([
            loadBackendExpenses(),
            loadBackendBudget()
        ]);
    } catch (error) {
        if (error.status === 401) return;
        console.error("Could not load dashboard data:", error);
        showToast(
            "Could not load account data. Please check that the server is running.",
            "error"
        );
    }
}

// ========================================
// MONTH SELECTOR CHANGE
// ========================================

if (monthSelector) {
    monthSelector.addEventListener("change", async function () {
        const match = /^(\d{4})-(\d{2})$/.exec(this.value);
        if (!match) return;

        selectedYear = Number(match[1]);
        selectedMonth = Number(match[2]) - 1;
        syncPeriodLabels();

        if (budgetMessage) {
            budgetMessage.textContent = "";
            budgetMessage.className = "";
        }

        try {
            await loadBackendBudget();
        } catch (error) {
            if (error.status === 401) return;
            showToast("Could not load budget for selected month.", "error");
        }
    });
}

// ========================================
// FILTER & SORT HELPERS
// ========================================

function getSelectedMonthExpenses() {
    return expenses.filter(expense => {
        return (
            Number(expense.month) === selectedMonth &&
            Number(expense.year) === selectedYear
        );
    });
}

function getFilteredTransactions() {
    const searchQuery = (searchExpenseInput?.value || "").trim().toLowerCase();
    const categoryFilter = filterCategorySelect?.value || "All";
    const scopeFilter = filterScopeSelect?.value || "month";

    const sourceList =
        scopeFilter === "month" ? getSelectedMonthExpenses() : [...expenses];

    return sourceList
        .filter(expense => {
            if (categoryFilter !== "All" && expense.category !== categoryFilter) {
                return false;
            }
            if (searchQuery && !expense.name.toLowerCase().includes(searchQuery)) {
                return false;
            }
            return true;
        })
        .sort((a, b) => {
            if (b.year !== a.year) return b.year - a.year;
            if (b.month !== a.month) return b.month - a.month;
            if (b.day !== a.day) return b.day - a.day;
            return b.id - a.id;
        });
}

if (searchExpenseInput) {
    searchExpenseInput.addEventListener("input", displayExpenses);
}
if (filterCategorySelect) {
    filterCategorySelect.addEventListener("change", displayExpenses);
}
if (filterScopeSelect) {
    filterScopeSelect.addEventListener("change", displayExpenses);
}

// ========================================
// EXPORT CSV
// ========================================

function csvEscape(value) {
    const str = String(value ?? "");
    if (/[",\r\n]/.test(str)) {
        return `"${str.replace(/"/g, '""')}"`;
    }
    return str;
}

if (exportCsvButton) {
    exportCsvButton.addEventListener("click", function () {
        const list = getFilteredTransactions();

        if (list.length === 0) {
            showToast("No transactions to export.", "info");
            return;
        }

        const rows = [
            ["Date", "Expense Name", "Category", "Amount (INR)"]
        ];

        list.forEach(exp => {
            rows.push([
                exp.date,
                exp.name,
                exp.category,
                Number(exp.amount).toFixed(2)
            ]);
        });

        const csvContent = rows
            .map(row => row.map(csvEscape).join(","))
            .join("\r\n");

        const blob = new Blob([csvContent], { type: "text/csv;charset=utf-8;" });
        const url = URL.createObjectURL(blob);

        const scope = filterScopeSelect?.value === "all"
            ? "all-time"
            : `${selectedYear}-${String(selectedMonth + 1).padStart(2, "0")}`;

        const link = document.createElement("a");
        link.href = url;
        link.download = `smartspend-${scope}.csv`;
        document.body.appendChild(link);
        link.click();
        link.remove();
        URL.revokeObjectURL(url);

        showToast(`Exported ${list.length} transaction(s) to CSV.`, "success");
    });
}

// ========================================
// DISPLAY EXPENSES & DASHBOARD SUMMARY
// ========================================

function displayExpenses() {
    transactionList.innerHTML = "";

    const filteredTransactions = getFilteredTransactions();

    if (filteredTransactions.length === 0) {
        const empty = document.createElement("div");
        empty.className = "empty-state";

        const title = document.createElement("p");
        title.textContent = expenses.length === 0
            ? "No transactions yet."
            : "No matching transactions.";

        const sub = document.createElement("span");
        sub.textContent = expenses.length === 0
            ? "Add your first expense above to start tracking."
            : "Try changing the month, search term, or category filter.";

        empty.append(title, sub);
        transactionList.appendChild(empty);
    }

    filteredTransactions.forEach(expense => {
        const item = document.createElement("div");
        item.className = "expense-item";

        const info = document.createElement("div");
        info.className = "expense-info";

        const name = document.createElement("strong");
        name.textContent = expense.name;

        const meta = document.createElement("div");
        meta.className = "expense-meta";

        const badge = document.createElement("span");
        badge.className = `category-badge ${expense.category.toLowerCase()}`;
        badge.textContent = expense.category;

        const dateSpan = document.createElement("span");
        dateSpan.textContent = expense.date;

        meta.append(badge, dateSpan);
        info.append(name, meta);

        const right = document.createElement("div");
        right.className = "expense-right";

        const amount = document.createElement("span");
        amount.className = "expense-amount";
        amount.textContent = `₹${Number(expense.amount).toFixed(2)}`;

        const editButton = document.createElement("button");
        editButton.type = "button";
        editButton.textContent = "Edit";
        editButton.className = "edit-button";
        editButton.addEventListener("click", () => openEditModal(expense));

        const deleteButton = document.createElement("button");
        deleteButton.type = "button";
        deleteButton.textContent = "Delete";
        deleteButton.className = "delete-button";
        deleteButton.addEventListener("click", async () => {
            const confirmed = await askConfirm(
                "Delete Expense",
                `Are you sure you want to delete "${expense.name}" (₹${Number(expense.amount).toFixed(2)})?`,
                "Delete"
            );

            if (!confirmed) return;

            try {
                await apiRequest(
                    `/api/expenses?id=${encodeURIComponent(expense.id)}`,
                    { method: "DELETE" }
                );

                await loadBackendExpenses();
                showToast("Expense deleted successfully!", "success");
            } catch (error) {
                if (error.status === 401) return;
                console.error("Delete failed:", error);
                showToast("Could not delete expense: " + error.message, "error");
            }
        });

        right.append(amount, editButton, deleteButton);
        item.append(info, right);
        transactionList.appendChild(item);
    });

    // SELECTED MONTH TOTALS
    const monthlyExpenses = getSelectedMonthExpenses();

    const monthlyTotal = monthlyExpenses.reduce(
        (total, expense) => total + Number(expense.amount),
        0
    );

    const remaining = monthlyBudget - monthlyTotal;

    totalExpensesElement.textContent = `₹${monthlyTotal.toFixed(2)}`;
    monthlyBudgetElement.textContent = `₹${monthlyBudget.toFixed(2)}`;
    remainingBalanceElement.textContent = `₹${remaining.toFixed(2)}`;

    if (remainingBalanceCard && remainingBalanceLabel) {
        const isOver = monthlyBudget > 0 && remaining < 0;
        remainingBalanceCard.classList.toggle("over-budget", isOver);
        remainingBalanceLabel.textContent = isOver
            ? `Over budget by ₹${Math.abs(remaining).toFixed(2)}`
            : "Available budget";
    }

    displayMonthlyReport();
    displayBudgetProgress();
    displaySpendingInsights();
}

// ========================================
// ADD NEW EXPENSE
// ========================================

expenseForm.addEventListener("submit", async function (event) {
    event.preventDefault();

    const name = document.getElementById("expenseName").value.trim();
    const amount = Number(document.getElementById("expenseAmount").value);
    const category = document.getElementById("expenseCategory").value;
    const parsedDate = parseDateInput(expenseDateInput.value);
    const submitBtn = document.getElementById("addExpenseBtn");

    if (!name || name.includes("|")) {
        showToast("Please enter a valid expense name (cannot contain '|').", "error");
        return;
    }

    if (!Number.isFinite(amount) || amount <= 0 || amount > 10000000) {
        showToast("Please enter a valid expense amount.", "error");
        return;
    }

    if (!categoryIds[category]) {
        showToast("Please select a category.", "error");
        return;
    }

    if (!parsedDate) {
        showToast("Please select a valid date.", "error");
        return;
    }

    const expenseData = {
        name,
        amount,
        category: categoryIds[category],
        day: parsedDate.day,
        month: parsedDate.month,
        year: parsedDate.year
    };

    try {
        setButtonLoading(submitBtn, true, "Saving...");

        await apiRequest("/api/expenses", {
            method: "POST",
            body: JSON.stringify(expenseData)
        });

        await loadBackendExpenses();
        expenseForm.reset();
        expenseDateInput.value = formatTodayInput();

        const addedToDifferentMonth =
            parsedDate.month - 1 !== selectedMonth ||
            parsedDate.year !== selectedYear;

        if (addedToDifferentMonth) {
            showToast("Expense saved! Switch the month selector to view it.", "success");
        } else {
            showToast("Expense saved successfully!", "success");
        }
    } catch (error) {
        if (error.status === 401) return;
        console.error("Expense save failed:", error);
        showToast("Could not save expense: " + error.message, "error");
    } finally {
        setButtonLoading(submitBtn, false);
    }
});

// ========================================
// SUBMIT EDIT EXPENSE MODAL
// ========================================

editExpenseForm.addEventListener("submit", async function (event) {
    event.preventDefault();

    const id = Number(document.getElementById("editExpenseId").value);
    const name = document.getElementById("editExpenseName").value.trim();
    const amount = Number(document.getElementById("editExpenseAmount").value);
    const category = document.getElementById("editExpenseCategory").value;
    const parsedDate = parseDateInput(document.getElementById("editExpenseDate").value);
    const saveBtn = document.getElementById("saveEditBtn");

    if (!name || name.includes("|")) {
        showToast("Expense name is invalid (cannot contain '|').", "error");
        return;
    }

    if (!Number.isFinite(amount) || amount <= 0 || amount > 10000000) {
        showToast("Please enter a valid amount.", "error");
        return;
    }

    if (!categoryIds[category] || !parsedDate) {
        showToast("Please check category and date.", "error");
        return;
    }

    const updatedExpense = {
        id,
        name,
        amount,
        category: categoryIds[category],
        day: parsedDate.day,
        month: parsedDate.month,
        year: parsedDate.year
    };

    try {
        setButtonLoading(saveBtn, true, "Saving...");

        await apiRequest("/api/expenses", {
            method: "PUT",
            body: JSON.stringify(updatedExpense)
        });

        closeEditModal();
        await loadBackendExpenses();
        showToast("Expense updated successfully!", "success");
    } catch (error) {
        if (error.status === 401) return;
        console.error("Update failed:", error);
        showToast("Could not update expense: " + error.message, "error");
    } finally {
        setButtonLoading(saveBtn, false);
    }
});

// ========================================
// MONTHLY BUDGET
// ========================================

budgetForm.addEventListener("submit", async function (event) {
    event.preventDefault();

    const budgetInput = document.getElementById("budgetInput");
    const newBudget = Number(budgetInput.value);
    const saveBtn = document.getElementById("saveBudgetBtn");

    budgetMessage.className = "";

    if (!Number.isFinite(newBudget) || newBudget <= 0 || newBudget > 100000000) {
        budgetMessage.className = "error";
        budgetMessage.textContent = "Please enter a valid monthly budget.";
        return;
    }

    const budgetData = {
        month: selectedMonth + 1,
        year: selectedYear,
        budget: newBudget
    };

    try {
        setButtonLoading(saveBtn, true, "Saving...");

        await apiRequest("/api/budget", {
            method: "POST",
            body: JSON.stringify(budgetData)
        });

        monthlyBudget = newBudget;
        budgetInput.value = monthlyBudget;
        displayExpenses();

        budgetMessage.textContent = `Budget for ${getSelectedPeriodLabel()} saved successfully!`;
        showToast("Monthly budget saved!", "success");
    } catch (error) {
        if (error.status === 401) return;
        console.error("Budget save failed:", error);
        budgetMessage.className = "error";
        budgetMessage.textContent = "Could not save budget: " + error.message;
        showToast("Could not save budget: " + error.message, "error");
    } finally {
        setButtonLoading(saveBtn, false);
    }
});

// ========================================
// BUDGET PROGRESS BAR
// ========================================

function displayBudgetProgress() {
    const percentageElement = document.getElementById("budgetPercentage");
    const progressBar = document.getElementById("budgetProgressBar");
    const progressText = document.getElementById("budgetProgressText");

    if (!percentageElement || !progressBar || !progressText) return;

    const monthlyExpenses = getSelectedMonthExpenses();
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
    const insightTitle = document.getElementById("spendingInsightTitle");
    const insightText = document.getElementById("spendingInsightText");

    if (!insightTitle || !insightText) return;

    const monthlyExpenses = getSelectedMonthExpenses();
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
            (categoryTotals[expense.category] || 0) + Number(expense.amount);
    });

    const categories = Object.entries(categoryTotals);
    let highestCategory = "None";

    if (categories.length > 0) {
        highestCategory = categories.reduce(
            (highest, current) => (current[1] > highest[1] ? current : highest)
        )[0];
    }

    if (monthlyExpenses.length === 0) {
        insightTitle.textContent = "Start Tracking!";
        insightText.textContent =
            `No expenses recorded for ${getSelectedPeriodLabel()} yet. Add expenses to receive monthly insights.`;
    } else if (monthlyBudget <= 0) {
        insightTitle.textContent = "Set Your Budget";
        insightText.textContent =
            `You spent ₹${monthlyTotal.toFixed(2)} in ${getSelectedPeriodLabel()}. Set a monthly budget to track your progress.`;
    } else if (percentage >= 100) {
        insightTitle.textContent = "Budget Limit Reached!";
        insightText.textContent =
            `You have spent ₹${monthlyTotal.toFixed(2)}, which is ₹${Math.abs(remaining).toFixed(2)} over your budget. Your highest spending category is ${highestCategory}.`;
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
// MONTHLY REPORTS & CHART
// ========================================

function displayMonthlyReport() {
    const monthlyExpenses = getSelectedMonthExpenses();

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

    const categoryReport = document.getElementById("categoryReport");
    categoryReport.innerHTML = "";

    const categories = Object.entries(categoryTotals).sort((a, b) => b[1] - a[1]);

    displayExpenseChart(categoryTotals);

    if (categories.length === 0) {
        categoryReport.textContent =
            `No expenses recorded for ${getSelectedPeriodLabel()}.`;
        document.getElementById("topCategory").textContent = "None";
        return;
    }

    categories.forEach(([category, amount]) => {
        const row = document.createElement("div");
        row.className = "category-report-row";

        const label = document.createElement("span");
        label.textContent = category;

        const val = document.createElement("strong");
        val.textContent = `₹${amount.toFixed(2)}`;

        row.append(label, val);
        categoryReport.appendChild(row);
    });

    document.getElementById("topCategory").textContent = categories[0][0];
}

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
// SIDEBAR ACTIVE LINK & DATE DISPLAY
// ========================================

const sidebarLinks = document.querySelectorAll("#sidebarNav a");
sidebarLinks.forEach(link => {
    link.addEventListener("click", function () {
        sidebarLinks.forEach(l => l.classList.remove("active"));
        this.classList.add("active");
    });
});

const todayDate = document.getElementById("todayDate");
if (todayDate) {
    todayDate.textContent = new Date().toLocaleDateString("en-IN", {
        weekday: "long",
        day: "numeric",
        month: "long",
        year: "numeric"
    });
}

// ========================================
// PASSWORD SHOW / HIDE BUTTON
// ========================================

document.querySelectorAll(".toggle-password").forEach(button => {
    const eyeIcon = `
        <svg viewBox="0 0 24 24"
             width="20" height="20"
             fill="none"
             stroke="currentColor"
             stroke-width="1.8"
             stroke-linecap="round"
             stroke-linejoin="round">
            <path d="M2 12s3.5-7 10-7 10 7 10 7-3.5 7-10 7S2 12 2 12z"/>
            <circle cx="12" cy="12" r="3"/>
        </svg>
    `;

    const eyeSlashIcon = `
        <svg viewBox="0 0 24 24"
             width="20" height="20"
             fill="none"
             stroke="currentColor"
             stroke-width="1.8"
             stroke-linecap="round"
             stroke-linejoin="round">
            <path d="M3 3l18 18"/>
            <path d="M10.6 10.6a2 2 0 0 0 2.8 2.8"/>
            <path d="M9.9 5.2A11 11 0 0 1 12 5c6.5 0 10 7 10 7a15 15 0 0 1-3.1 3.9"/>
            <path d="M6.2 6.2C3.5 8 2 12 2 12s3.5 7 10 7c1.2 0 2.3-.2 3.3-.6"/>
        </svg>
    `;

    button.innerHTML = eyeIcon;

    button.addEventListener("click", function () {
        const targetId = this.dataset.target;
        const passwordInput = document.getElementById(targetId);

        if (!passwordInput) return;

        const isHidden = passwordInput.type === "password";
        passwordInput.type = isHidden ? "text" : "password";
        this.innerHTML = isHidden ? eyeSlashIcon : eyeIcon;
        this.setAttribute("aria-label", isHidden ? "Hide password" : "Show password");
        this.setAttribute("aria-pressed", String(isHidden));
    });
});

// ========================================
// APPLICATION START
// ========================================

async function initializeApplication() {
    await resolveApiUrl();

    if (authToken && loggedInUsername) {
        showDashboard();
        await loadDashboardData();
    } else {
        showLoginScreen();
    }
}

initializeApplication();
