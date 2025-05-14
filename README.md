# BUS RESERVATION SYSTEM
# Overview
This is a comprehensive Bus Reservation System server implemented in C++ that handles both user and driver functionalities through TCP socket communication. The system supports:

User registration and authentication

Driver registration and authentication

Bus registration by drivers

Trip scheduling by drivers

Seat reservation with dynamic pricing

Real-time seat availability visualization

UDP broadcasting for server discovery

---

# Features
### User Features
- ```Registration & Login```: Secure user registration with Aadhar validation

- ```Trip Viewing```: View all upcoming trips with real-time availability

- ```Seat Reservation```: Interactive seat map with visual indicators

- ```Dynamic Pricing```: Automatic discounts for last-minute bookings

- ```Ticket Management```: View all booked tickets

### Driver Features
- ```Driver Registration```: Special registration with license validation

- ```Bus Management```: Register new buses with seat configurations

- ```Trip Scheduling```: Create new trips with time conflict checking

- ```Seat Configuration```: Automatic seat file generation for each trip

# Technical Features
Multi-threaded: Handles multiple clients simultaneously

File-based Database: CSV files for data persistence

UDP Broadcast: Automatic server discovery

Mutex Protection: Thread-safe file operations

Interactive CLI: Rich terminal interface for clients

---

# System Architecture
```
Bus Reservation System
├── Server (this)
│   ├── TCP Listener (Port 8050)
│   ├── UDP Broadcaster (Port 9000)
│   ├── Data Storage (CSV files)
│   └── Multi-threaded Client Handler
└── Clients
    ├── User Clients
    └── Driver Clients
```
--- 

# Data Files
The system maintains several CSV files for data persistence:

```users.txt``` - Stores user information (Aadhar, name, age, password)

```drivers.txt``` - Stores driver information (Aadhar, license, name, age, password)

```buses.txt```- Stores bus information (Bus number, driver Aadhar, rows, cols)

```trips.txt``` - Stores trip information (Trip ID, bus number, source, destination, time, driver)

```bookings.txt``` - Stores booking information (Trip ID, bus number, seat, user Aadhar, name, price)

```seat{TRIP_ID}.txt``` - Per-trip seat availability files

---

# Installation & Setup
## Prerequisites
Linux environment

GCC/G++ compiler

Basic build tools (make)

# Compilation

```
g++ -pthread newserver.cpp -o server
```
# Execution
```
./server
```
---

# Usage
Starting the Server
Compile the server code

Run the executable

The server will:

Start broadcasting its availability on port 9000

Listen for TCP connections on port 8050

Create necessary data files if they don't exist

# Client Interaction
Clients can connect to the server using the broadcasted port information. The server provides an interactive menu system for all operations.
# Compilation

```
g++ -pthread cimine.cpp -o client
```
# Execution
```
./client
```

---

# Code Structure
## Main Components
### File Operations

readFile() - Reads CSV files into 2D string vectors

writeFile() - Appends data to CSV files

updateFile() - Overwrites entire CSV files

### User Management

Registration with Aadhar validation

Login authentication

### Driver Management

Registration with license validation

Login authentication

### Bus & Trip Management

Bus registration with seat configuration

Trip scheduling with time conflict checking

Automatic seat file generation

### Reservation System

Interactive seat maps

Seat locking mechanism

Dynamic pricing based on booking time

### Network Communication

TCP server for client connections

UDP broadcasting for server discovery

Thread-per-client model

---

# Security Considerations
Input Validation: All user inputs are strictly validated

Aadhar Verification: 12-digit numeric validation

License Verification: 16-character validation

Password Protection: Stored in plaintext (for educational purposes)

Mutex Protection: All file operations are thread-safe

---

# Limitations
Data Persistence: Uses simple CSV files (not suitable for production)

Security: Passwords stored in plaintext

Scalability: Thread-per-client model may not scale to thousands of clients

Error Handling: Basic error handling (could be more robust)

---

# Future Enhancements
Database integration (SQLite/MySQL)

Password encryption

JSON protocol for client-server communication

Admin interface

Trip cancellation functionality

Enhanced reporting

