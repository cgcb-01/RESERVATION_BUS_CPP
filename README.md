# 🚍 Bus Reservation System Management in C++

> A **feature-rich, command-line-based Bus Reservation System** built in **C++**, utilizing key **Operating System concepts** such as **socket programming**, **multi-threading**, and robust **file handling** for persistent storage. The system supports a dual-interface for **passengers and drivers**, enabling actions like user/driver registration, secure login via Aadhar/License, trip creation and management, dynamic seat layout handling (including window seat pricing), and a full-fledged ticket booking workflow with real-time seat availability checks and validations. Designed with a modular architecture and intuitive terminal-based UI using ANSI formatting, it ensures both functionality and usability for scalable offline simulations.

![License: MIT](https://img.shields.io/badge/License-MIT-green?style=flat-square)
![Language: C++](https://img.shields.io/badge/Language-C++-blue?style=flat-square&logo=c%2B%2B)
![OS: Linux](https://img.shields.io/badge/OS-Linux-yellow?style=flat-square&logo=linux)
![Security: OpenSSL](https://img.shields.io/badge/Security-OpenSSL-lightgrey?style=flat-square&logo=openssl)
![UI: ANSI](https://img.shields.io/badge/UI-ANSI_Terminal-orange?style=flat-square)


## 📑 Table of Contents

- [Bus Reservation System Management in C++](#bus-reservation-system-management-in-c)
 - [Features](#Features)
 - [Getting Started](#getting-started)
    - [Prerequisites](#prerequisites)
    - [Installation](#installation)
    - [Compilation](#compilation)
    - [Running the Application](#running-the-application)
    - [Admin Credentials](#admin-credentials)
  - [File Structure](#file-structure)
  - [Classes and Methods](#classes-and-methods)
    - [User](#user)
    - [Driver](#driver)
    - [Reservation_Handler](#reservation-handler)
    - [bus_trip_handler](#bus_trip_handler)
  - [Utility Functions](#utility-functions)
  - [How to Use](#how-to-use)

---

## ✨ Features

- **Bus Management**: Insert(for drivers), view, and register trips.
- **Reservation System**: Book, view, and cancel reservations.
- **File Handling**: Persist bus and reservation data using file handling techniques.
- **User Interface**: Command-line based user interface for interacting with the system.
  
---

## 🚀 Getting Started

To get started with the development or usage of this project, follow the instructions below:

### 🧰 Prerequisites

- A C++ compiler (such as g++, clang++)
- Terminal or command line
### 📥 Installation

1. Clone this repository to your local machine:

    ```bash
    git clone https://github.com/cgcb-01/RESERVATION_BUS_CPP
    ```

2. Navigate into the cloned repository directory:

    ```bash
    cd RESERVATION_BUS_CPP

    ```
3. Install the libssl-dev
   
   ```bash
   sudo apt update
   sudo apt install libssl-dev
   ```

### ⚙️ Compilation

To compile the project, you can use the following command in the terminal:
SERVER:

```bash
g++ newserver.cpp -o s -lcrpto
```
CLIENT:

```bash
g++ cmine.cpp -o c
```

### ▶️ Running the Application

After successfully compiling the project, you can run the application using the command:

SERVER:
```
./s
```
CLIENT:
```
./c
```

## 📁 File Structure

The project directory typically contains the following files:

- `cmine.cpp`: The main entry point of the application.
- `newserver.cpp`: The server client implementation code.
- `users.txt`: The details of the users stored here after successful registration.
- `drivers.txt`: A file containing all the details regarding successfully registered drivers.
- `buses.txt`: Information regarding buses present. 
- `trips.txt`: The trips present in our project.
- `bookings.txt`: A file contains all the confirmed bookings.
- `seatT00X.txt`: The seat matrix of the trips existing in our project.
- `screenshots/`: The folder containing all the screenshots of the project.

### 🏷️ Classes and Methods 

- **👤 User**
  - `registerUser(sock)`: Registers a new user using Aadhar and name.
  - `login(sock)`: Logs in a user by validating credentials.

- **🧑‍✈️ Driver**
  - `registerDriver(sock)`:  Registers a driver with license ID and name.
  - `loginDriver(sock)`: Authenticates a driver based on ID and name.

- **🎫 Reservation_Handler**
  - `viewTickets(sock)`:  Displays tickets booked by the user.
  - `viewTrips(sock)`: Lists upcoming trips (excludes expired ones).
  - `reserve(sock)`: Full flow to select a trip and book a seat.

- **🚌 bus_trip_handler**
  - `registerBus(sock)`:Adds a new bus with seat layout.
  - `insertTrip(sock)`: Assigns a trip with time, date, source, and destination.
  - `createSeatFile(tripId, rows, cols)`: Generates seat layout file for the trip.

### 🧩 Utility Functions

- File I/O: `readFile()`, `updateFile()`, `writeFile()`, `escapeCSV()`
- Security: `hash_password()`
- Time: `timeToMinutes()`, `isTimeDifferenceSafe()`, `isDateTimeAfterNow()`, `getTimeFromDateTime()`
- Seat Booking: `bookSeat()`, `seatMatrix()`
- Communication: `sendPrompt()`, `receiveInput()`
- Validation: `isValidAadhar()`, `isAadharExist()`, `isValidLicense()`, `isLicenseExist()`

## How to Use

1. **Add a Bus**: Select the option to add a bus and enter the required details.
2. **View Buses**: Select the option to view all available buses.
3. **Book a Ticket**: Select the option to book a ticket and enter the required details.
4. **View Reservations**: Select the option to view all reservations.

Enjoy building and extending this terminal-based system! Feel free to contribute or raise issues on the GitHub repo.

---

## LICENSE
This project is Licensed under MIT LICENSE.
