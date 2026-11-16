
# SmartSpend – Smart Expense Tracker

A web-based expense tracking application developed using C, HTML, CSS, and JavaScript. SmartSpend helps users manage their daily expenses, track budgets, and monitor their spending in one place.

## 🚀 Live Demo

[Open SmartSpend](https://smartspend-1-0lp4.onrender.com)

## 📌 About the Project

SmartSpend is designed to make personal expense management simple and convenient. Users can create an account, log in securely, add and manage expenses, and set budgets.

The application uses a C-based backend connected to a responsive web frontend.

## ✨ Features

- User Registration and Login
- Password Show/Hide Option
- Add New Expenses
- Edit Existing Expenses
- Delete Expenses
- Set and Manage Budgets
- Separate Expense Records for Each User
- Data Persistence During Normal Server Operation
- Responsive Web Interface

## 🛠️ Technologies Used

| Technology | Purpose |
|---|---|
| C | Backend development |
| HTML | Webpage structure |
| CSS | Styling and design |
| JavaScript | Frontend functionality |
| OpenSSL | Password hashing and security |
| Git & GitHub | Version control |
| Render | Application deployment |

## 📂 Project Structure

```text
SmartExpenseTracker_Web/
│
├── C_project/
│   ├── server_new.c
│   ├── auth.h
│   ├── expense.c
│   └── users/
│
├── frontend/
│   ├── index.html
│   ├── style.css
│   └── script.js
│
├── Dockerfile
├── .gitignore
└── README.md
```

## ⚙️ How to Run the Project Locally

### 1. Clone the Repository

```bash
git clone https://github.com/sivakarni29-cse/SmartSpend.git
```

### 2. Open the Project Folder

```bash
cd SmartSpend
```

### 3. Compile the C Backend

Open the C_project folder and compile:

```bash
gcc server_new.c -o server_new.exe -lws2_32 -lssl -lcrypto
```

### 4. Start the Server

```bash
.\server_new.exe
```

### 5. Open the Frontend

Open `frontend/index.html` using a local web server, such as VS Code Live Server.

For local testing, configure the frontend API URL to point to your local C server.

## 🔮 Future Enhancements

- Improved mobile dashboard design
- Expense category-wise charts
- Monthly expense reports
- Expense search and filtering
- Downloadable expense reports
- Improved data storage

## 👩‍💻 Developer

**Sivakarni S**

First-Year Computer Science and Engineering Student

## 🔗 GitHub Repository

[SmartSpend – GitHub](https://github.com/sivakarni29-cse/SmartSpend)

---

⭐ If you find this project interesting, feel free to explore the repository!
