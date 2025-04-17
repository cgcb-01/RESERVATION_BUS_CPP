#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <iomanip>
using namespace std;

mutex mtx;

const string USER_FILE = "users.txt";
const string DRIVER_FILE = "drivers.txt";
const string TRIPS_FILE = "trips.txt";
const string BOOKING_FILE = "bookings.txt";
const string BUS_FILE="buses.txt";

// --- UTILITY ---

// Read from a file
vector<vector<string>> readFile(const string &filename) {
    vector<vector<string>> data;
    ifstream file(filename);
    string line;
    while (getline(file, line)) {
        stringstream ss(line);
        vector<string> row;
        string cell;
        while (getline(ss, cell, ','))
            row.push_back(cell);
        data.push_back(row);
    }
    return data;
}

// Escape special characters in CSV
string escapeCSV(const string& field) {
    string escaped = field;
    bool needsQuotes = escaped.find(',') != string::npos ||
                       escaped.find('"') != string::npos ||
                       escaped.find('\n') != string::npos;

    if (needsQuotes) {
        size_t pos = 0;
        while ((pos = escaped.find('"', pos)) != string::npos) {
            escaped.insert(pos, "\""); // Escape quotes by doubling them
            pos += 2;
        }
        escaped = "\"" + escaped + "\"";
    }

    return escaped;
}


//FOR FINDING UPCOMING TRIPS
int timeToMinutes(const string& timeStr) {
    stringstream ss(timeStr);
    int hours, minutes;
    char colon;
    ss >> hours >> colon >> minutes;
    return hours * 60 + minutes;
}
bool isTimeDifferenceSafe(const string& existingTime, const string& newTime) {
    int existingMinutes = timeToMinutes(existingTime);
    int newMinutes = timeToMinutes(newTime);

    return abs(existingMinutes - newMinutes) >= 60;
}

//Updating a file

void updateFile(const string& filename, const vector<vector<string>>& data) {

    ofstream outFile(filename);
    if (!outFile) {
        cerr << "❌ Could not open file: " << filename << endl;
        return;
    }

    for (const auto& row : data) {
        for (size_t i = 0; i < row.size(); ++i) {
            outFile << escapeCSV(row[i]);
            if (i < row.size() - 1) outFile << ",";
        }
        outFile << "\n";
    }

    outFile.close();
}

// Append row to CSV file
void writeFile(const string& filename, const vector<string>& row) {
    ofstream file(filename, ios::app);
    if (!file) {
        cerr << "❌ Could not open file: " << filename << endl;
        return;
    }

    for (size_t i = 0; i < row.size(); ++i) {
        file << escapeCSV(row[i]);
        if (i < row.size() - 1) file << ",";
    }
    file << "\n";
}

// Send message to client
void sendPrompt(int sock, const string &msg) {
    send(sock, msg.c_str(), msg.length(), 0);
}

// Receive input from client
string receiveInput(int sock) {
    char buffer[4096] = {0};
    recv(sock, buffer, sizeof(buffer), 0);
    return string(buffer);
}

//UPDATING SEAT FILE BY LOCKING
mutex seatLockMutex;

bool bookSeat(int sock, const string& tripId, const string& seatChoice, const string& aadhar, const string& name)
{
    string seatKey = tripId + "_" + seatChoice;

    unique_lock<std::mutex> seatLock(seatLockMutex);

    string seatFile = "seat" + tripId + ".txt";
    auto seatData = readFile(seatFile);

    bool seatFoundAndBooked = false;
    for (auto &seat : seatData) {
        if (seat.size() >= 3 && seat[0] == seatChoice && seat[1] == "0") {
            seat[1] = "1";
            seatFoundAndBooked = true;
            break;
        }
    }

    if (!seatFoundAndBooked) {
        sendPrompt(sock, "❌ Seat " + seatChoice + " is either already booked or invalid.\n");
        return false;
    }

    // Update the seat data after booking
    updateFile(seatFile, seatData);

    sendPrompt(sock, "✅ Seat " + seatChoice + " booked successfully.\n");
    return true;
}


// Aadhar validation
bool isValidAadhar(const string& aadhar) {
    return aadhar.length() == 12 && all_of(aadhar.begin(), aadhar.end(), ::isdigit);
}
bool isAadharExist(const string& aadhar) {
    auto users = readFile(USER_FILE);
    for (const auto& row : users)
        if (row.size() > 0 && row[1] == aadhar)
            return true;
    return false;
}//d

// License validation
bool isValidLicense(const string& license) {
    return (license.length() == 16);
}

bool isLicenseExist(const string& license) {
    auto users = readFile(DRIVER_FILE);
    for (const auto& row : users)
        if (row.size() > 0 && row[1] == license)
            return true;
    return false;
}//d

//CURRENT TIME CHECKING

bool isTimeAfterNow(const string& timeStr) {
    int tripHour, tripMin;
    char colon;
    stringstream ss(timeStr);
    if (!(ss >> tripHour >> colon >> tripMin) || colon != ':') {
        return false; // Invalid time
    }

    time_t now = time(0);
    tm *ltm = localtime(&now);
    int currMinutes = ltm->tm_hour * 60 + ltm->tm_min;
    int tripMinutes = tripHour * 60 + tripMin;

    return tripMinutes > currMinutes;
}//d

//SEAT MATRIX PRINTING
void seatMatrix(const string& tripId, int rows, int cols, int sock) {
    string seatFile = "seat" + tripId + ".txt";
    auto seatData = readFile(seatFile);  // seatData is expected to be a vector of vectors: {{seatNo, status, price}, ...}

    stringstream response;
    response << "🪑 Seat Matrix for Trip " << tripId << "\n\n";

    int totalSeats = rows * cols;
    int seatIndex = 0;

    for (int i = 0; i < totalSeats; ++i) {
        if (seatIndex < seatData.size() && seatData[seatIndex].size() >= 3) {
            string seatNo = seatData[seatIndex][0];
            string status = seatData[seatIndex][1] == "1" ? "X" : "✓";
            string price = seatData[seatIndex][2];

            stringstream seatStr;
            seatStr << "[" << status << " ₹" << price << " " << seatNo << "]";
            response << left << setw(18) << seatStr.str();

            ++seatIndex;
        } else {
            // For missing seat data, show as unavailable
            response << left << setw(18) << "[X ₹-- --]";
        }

        if ((i + 1) % cols == 0)
            response << "\n";
    }

    response << "\n(✓ = Available, X = Booked)\n";
    sendPrompt(sock, response.str());
}//d


// --- CLASS DECLARATIONS ---

class User {
public:
    void registerUser(int sock);
    string login(int sock);
};

//reservation handler
class ReservationHandler {
private:
    string uid;
public:
    ReservationHandler(string userID) : uid(userID) {}

    void viewTickets(int sock) ;
    vector<vector<string>> viewTrips(int sock);
    void reserve(int sock);
};

//driver class
class Driver {
public:
    void registerDriver(int sock);
    string loginDriver(int sock);
};

//------BUS_TRIP_HANDLER-------------

class bus_trip_handler {
    string aadhar;
public:
    bus_trip_handler(string a) : aadhar(a) {};//constructor

    void registerBus(int sock);
    void insertTrip(int sock);
    void createSeatFile(const string & tripId,int row,int col);

private:
    string generateTripID() {
        static int tripCount = 1;
        auto trips = readFile(TRIPS_FILE);
        tripCount += trips.size();
        stringstream ss;
        ss << "T" << setfill('0') << setw(3) << tripCount;
        return ss.str();
    }
};//d

// --------- Create Seat File Function----------
void bus_trip_handler::createSeatFile(const string &tripId, int rows, int cols)
{
   for (int r = 0; r < rows; r++) {
    for (int c = 0; c < cols; ++c) {
        string seatNum = "R" + to_string(r + 1) + "C" + to_string(c + 1);
        string status = "0";
        string price = to_string((c == 0 || c == cols - 1) ? 150 : 100); // Window seats cost more
        vector<string> row = {seatNum, status, price};
        writeFile("seat" + tripId + ".txt", row);
    }
}
}//d

// --- USER METHODS ---

//------------USER REGISTER--------------------------
void User::registerUser(int sock) {
    sendPrompt(sock, "Enter your Name:");
    string name = receiveInput(sock);

    int age = 0;
    while (true) {
        sendPrompt(sock, "Enter your Age:");
        string ageinput = receiveInput(sock);
        try {
            age = stoi(ageinput);
            if (age < 1)
                sendPrompt(sock, "Children below 1 year of age are not eligible for safety concerns.\n");
            else if (age > 150)
                sendPrompt(sock, "❌ Enter realistic age data.\n");
            else
                break;
        } catch (invalid_argument &e) {
            sendPrompt(sock, "❌ Invalid input. Please enter a number for age.\n");
        }
    }

    string aadhar;
    while (true) {
        sendPrompt(sock, "Enter your Unique Aadhar number:");
        aadhar = receiveInput(sock);
        if (!isValidAadhar(aadhar)) {
            sendPrompt(sock, "❌ Invalid Aadhar number. It must be 12 digits.\n");
            continue;
        }
        if (isAadharExist(aadhar)) {
            sendPrompt(sock, "This Aadhar number is already registered.\nIs it a Typo error? (y/n): ");
            string ans = receiveInput(sock);
            if (ans == "y" || ans == "Y") {
                sendPrompt(sock, "No worries, Re-enter again.\n");
                continue;
            } else {
                sendPrompt(sock, "You're already registered. Please login instead.\n");
                return;
            }
        } else {
            break;
        }
    }

    sendPrompt(sock, "Enter a Strong Password:");
    string password = receiveInput(sock);

    mtx.lock();
    writeFile(USER_FILE, {aadhar, name, to_string(age), password});
    mtx.unlock();
    sendPrompt(sock, "✅ Registration Successful!\n");
}//d

//----------USER LOGIN----------------
string User::login(int sock) {
    sendPrompt(sock, "Enter Your Aadhar Number:");
    string aadhar = receiveInput(sock);
    sendPrompt(sock, "Enter Your Password:");
    string password = receiveInput(sock);

    auto users = readFile(USER_FILE);
    for (auto &row : users) {
        if (row.size() < 4) continue;
        if (row[0] == aadhar && row[3] == password) {
            sendPrompt(sock, "✅ Login Successful!\n");
            return aadhar;
        }
    }

    sendPrompt(sock, "❌ Invalid Aadhar number or Password.\n");
    return "";
}//d

//------DRIVER CLASS-----------------

void Driver::registerDriver(int sock) {

    //Asking for name
    sendPrompt(sock, "Enter your Name:");
    string name = receiveInput(sock);

    //asking for age
    int age = 0;
    while (true) {
        sendPrompt(sock, "Enter your Age:");
        string ageinput = receiveInput(sock);
        try {
            age = stoi(ageinput);
            if (age < 25|| age>60)
                sendPrompt(sock, "Not Eligible to register here as a driver.\n");
            else
                break;
        } catch (invalid_argument &e) {
            sendPrompt(sock, "❌ Invalid input. Please enter a number for age.\n");
        }
    }

    //Asking for Aadhar no
    string aadhar;
    while (true) {
        sendPrompt(sock, "Enter your Unique Aadhar number:");
        aadhar = receiveInput(sock);
        if (!isValidAadhar(aadhar)) {
            sendPrompt(sock, "❌ Invalid Aadhar number. It must be 12 digits.\n");
            continue;
        }
        if (isAadharExist(aadhar)) {
            sendPrompt(sock, "This Aadhar number is already registered.\nIs it a Typo error? (y/n): ");
            string ans = receiveInput(sock);
            if (ans == "y" || ans == "Y") {
                sendPrompt(sock, "No worries, Re-enter again.\n");
                continue;
            } else {
                sendPrompt(sock, "You're already registered. Please login instead.\n");
                return;
            }
        } else {
            break;
        }
    }

    //Asking for license
     string license;
    while (true) {
        sendPrompt(sock, "Enter your Unique License number:");
        license = receiveInput(sock);
        if (!isValidLicense(license)) {
            sendPrompt(sock, "❌ Invalid License number.\n");
            continue;
        }
        if (isLicenseExist(license)) {
            sendPrompt(sock, "This License number is already registered.\nIs it a Typo error? (y/n): ");
            string ans = receiveInput(sock);
            if (ans == "y" || ans == "Y") {
                sendPrompt(sock, "No worries, Re-enter again.\n");
                continue;
            } else {
                sendPrompt(sock, "You're already registered. Please login instead.\n");
                return;
            }
        } else {
            break;
        }
    }

    sendPrompt(sock, "Enter a Strong Password:");
    string password = receiveInput(sock);

    mtx.lock();
    writeFile(DRIVER_FILE, {aadhar,license,name,to_string(age),password});
    mtx.unlock();
    sendPrompt(sock, "✅ Registration Successful!\n");
}//d

//--------------LOGIN DRIVER-----------------
string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == string::npos || end == string::npos) return "";
    return s.substr(start, end - start + 1);
}
string Driver::loginDriver(int sock) {
    sendPrompt(sock, "Enter Your Aadhar Number:");
    string aadhar = trim(receiveInput(sock));

    sendPrompt(sock, "Enter Your Password:");
    string password = trim(receiveInput(sock));

    auto users = readFile(DRIVER_FILE);

    for (auto &row : users) {
        if (row.size() < 5) continue;

        string storedAadhar = trim(row[0]);
        string storedPassword = trim(row[4]);

        if (storedAadhar == aadhar && storedPassword == password) {
            sendPrompt(sock, "✅ Login Successful!\n");
            return storedAadhar;
        }
    }

    sendPrompt(sock, "❌ Invalid Aadhar number or Password.\n");
    return "";
}

//-------------Insert a Trip(bus_trip_handler)------------------
void bus_trip_handler::insertTrip(int sock) {
    sendPrompt(sock, "Enter Bus Number: ");
    string busNo = receiveInput(sock);

    sendPrompt(sock, "Enter Source: ");
    string source = receiveInput(sock);

    sendPrompt(sock, "Enter Destination: ");
    string destination = receiveInput(sock);

    sendPrompt(sock, "Enter Start Time (HH:MM): ");
    string startTime = receiveInput(sock);

    auto buses = readFile("buses.txt");
    int rows = -1, cols = -1;
    for (auto &bus : buses) {
        if (bus.size() >=3 && bus[0] == busNo) {
            rows = stoi(bus[2]);
            cols = stoi(bus[3]);
            break;
        }
    }

    if (rows <= 0 || cols <= 0) {
        sendPrompt(sock, "❌ Bus not found or invalid seat dimensions.\n");
        return;
    }

    auto trips = readFile("trips.txt");
    for (auto &trip : trips) {
        if (trip.size() == 6 && trip[1] == busNo) {
            string existingTime = trip[4];

            if (!isTimeDifferenceSafe(existingTime, startTime)) {
                sendPrompt(sock, "❌ A trip with this bus is already scheduled around the same time (within 60 minutes).\n");
                return;
            }
        }
    }
    string tripID = generateTripID();

    mtx.lock();
    writeFile(TRIPS_FILE, {tripID,busNo,source,destination,startTime,aadhar});
    mtx.unlock();
   //creating seat file (rows and cols are int)
    createSeatFile(tripID, rows, cols);

    sendPrompt(sock, "✅ A Trip is being inserted successfully with Trip ID: " + tripID + "\n");
}//d

//-----------Register a bus-------------
void bus_trip_handler::registerBus(int sock)
{
    sendPrompt(sock, "Enter Bus Number (unique ID): ");
    string busNo = receiveInput(sock);

    // Check if busNo already exists
    auto buses = readFile("buses.txt");
    for (auto &b : buses) {
        if (!b.empty() && b[0] == busNo) {
            sendPrompt(sock, "❌ Bus Number already registered.\n");
            return;
        }
    }

    // Get rows
    sendPrompt(sock, "Enter number of seat rows: ");
    string rowStr = receiveInput(sock);

    // Get columns
    sendPrompt(sock, "Enter number of seat columns: ");
    string colStr = receiveInput(sock);

    // Validate inputs
    try {
        int rows = stoi(rowStr);
        int cols = stoi(colStr);

        if (rows <= 0 || cols <= 0) {
            sendPrompt(sock, "❌ Invalid row or column count for a bus\n");
            return;
        }

        mtx.lock();
        writeFile("buses.txt", {busNo,aadhar, rowStr, colStr});
        mtx.unlock();

        sendPrompt(sock, "✅ Bus registered successfully.\n");
    } catch (...) {
        sendPrompt(sock, "❌ Invalid input. Please enter numeric values.\n");
    }
}//d

//-----------RESERVATION HANDLER-----------------

//-------------VIEW TICKETS----------
void ReservationHandler::viewTickets(int sock) {
    auto bookings = readFile(BOOKING_FILE);
    string response = "🧾 Your Bookings:\n";
    int count = 0;

    for (auto &b : bookings) {
        if (b.size() >= 5 && b[4] == uid) {
            response += to_string(++count) + ". Trip ID: " + b[0] +",Bus No:"+ b[1]+", Seat No: " + b[2] + "\n";
        }
    }

    if (count == 0) {
        response = "❌ No bookings found under your ID.\n";
    }

    sendPrompt(sock, response);
}

//-----------VIEW TRIPS--------------
vector<vector<string>> ReservationHandler::viewTrips(int sock) {
    auto trips = readFile(TRIPS_FILE);
    vector<vector<string>> upcomingTrips;

    // Format: TripID, BusNo, Source, Destination, StartTime, DriverName
    for (auto &trip : trips) {
        if (trip.size() == 6 && isTimeAfterNow(trip[4])) {
            upcomingTrips.push_back(trip);
        }
    }

    if (upcomingTrips.empty()) {
        sendPrompt(sock, "❌ No upcoming trips are available.\n");
    }

    return upcomingTrips;
}//d


//------------RESERVE TICKET------------------

void ReservationHandler::reserve(int sock) {
    vector<vector<string>> available = viewTrips(sock);
    if (available.empty()) {
        sendPrompt(sock, "No upcoming Buses available\n");
        return;
    }

    string tripOptions = "🚌 Upcoming Trips:\n";
    for (size_t i = 0; i < available.size(); ++i) {
        auto &t = available[i];
        tripOptions += to_string(i + 1) + ". Trip ID: " + t[0] +
                       ", Bus No: " + t[1] +
                       ", From: " + t[2] +
                       ", To: " + t[3] +
                       ", Start Time: " + t[4] + "\n";
    }
    sendPrompt(sock, tripOptions);

    vector<string> tripIds;
    for (auto &trip : available) {
        tripIds.push_back(trip[0]);
    }

    while (true) {
        sendPrompt(sock, "\nEnter a Trip ID to book the trip or 'e' to exit:\n");
        string tripId = receiveInput(sock);
        if (tripId == "e") {
            sendPrompt(sock, "Thanks, exiting! Visit again.\n");
            return;
        }

        auto it = find(tripIds.begin(), tripIds.end(), tripId);
        if (it == tripIds.end()) {
            sendPrompt(sock, "❌ Invalid Trip ID. Please try again.\n");
            continue;
        }

        string busNo = "", source = "", destination = "", startTime = "";
        for (const auto &trip : available) {
            if (trip.size() == 6 && trip[0] == tripId) {
                busNo = trip[1];
                source = trip[2];
                destination = trip[3];
                startTime = trip[4];
                break;
            }
        }

        int rows = 0, cols = 0;
        auto buses = readFile(BUS_FILE);
        for (const auto &b : buses) {
            if (b.size() == 4 && b[0] == busNo) {
                rows = stoi(b[2]);
                cols = stoi(b[3]);
                break;
            }
        }
      seatMatrix(tripId,rows,cols,sock);
        auto seatData = readFile("seat" + tripId + ".txt");
        sendPrompt(sock, "Choose seat numbers one by one\nType:\n- 'c' to change trip\n- 'e' to exit\n");

        while (true) {
            string seatChoice = receiveInput(sock);
            if (seatChoice == "c") break;
            if (seatChoice == "e") return;

            bool found = false;
            for (auto &seat : seatData) {
                if (seat.size() >= 3 && seat[0] == seatChoice && seat[1] == "0") {
                    found = true;
                    string price="You have to pay:Rs"+seat[2];
                    sendPrompt(sock, "Enter Passenger Name: ");
                    string name = receiveInput(sock);
                    sendPrompt(sock, "Enter Aadhar Number: ");
                    string aadhar = receiveInput(sock);

                    bool registered = false;
                    auto users = readFile(USER_FILE);
                    for (auto &user : users) {
                        if (user.size() >= 2 && user[0] == aadhar && user[1] == name) {
                            registered = true;
                            break;
                        }
                    }

                    if (!registered) {
                        sendPrompt(sock, "❌ Passenger not registered. Cannot book.\n");
                        break;
                    }
                    sendPrompt(sock,price);
                    
                    if (bookSeat(sock, tripId, seatChoice, aadhar, name))
                    {
                    vector<string> booking = {tripId, busNo, seatChoice, aadhar, name};
                    writeFile(BOOKING_FILE, booking);

                    sendPrompt(sock, "✅ Seat " + seatChoice + " booked successfully.\n");
                    break;
                    }

                }
            }

            if (!found) {
                sendPrompt(sock, "❌ Invalid Input or already booked seat number.\nPlease try another seat.\n");
            }
        }
    }
}


// --- CLIENT HANDLER ---

//----------DRIVER CLIENT--------------
void driver_client(int sock) {

    Driver driver;
    while (true) {
        sendPrompt(sock, "\n---------- MAIN MENU ----------\n1. Register\n2. Login\n3. Exit\nChoose: ");
        string choice = receiveInput(sock);

        if (choice == "3") break;

        if (choice == "1") {
            driver.registerDriver(sock);
            sendPrompt(sock, "Please login to continue...\n");
        }

        string uid = driver.loginDriver(sock);
        if (uid.empty()) continue;

        bus_trip_handler handlerbus(uid);
        while (true) {
            sendPrompt(sock, "\n---------- DASHBOARD ----------\n1. Register a bus \n2. Insert a trip\n3. Logout\nChoose: ");
            string action =receiveInput(sock);
            if (action == "1")
                handlerbus.registerBus(sock);
            else if (action == "2")
                handlerbus.insertTrip(sock);
            else
                break;
        }
    }
    close(sock);
}//d

//----------USER CLIENT----------------
void handle_client(int sock) {
    sendPrompt(sock, "🚍 Welcome to the Bus Reservation System 🚍\n");
    sendPrompt(sock,"\n--------------Mention Your Requirements:---------------\n1(& default).USER\n2.DRIVER\n Enter Your Choice:");
    string c=receiveInput(sock);
    if(c=="2")
        driver_client(sock);
    User user;
    while (true) {
        sendPrompt(sock, "\n---------- MAIN MENU ----------\n1. Register\n2. Login\n3. Exit\nChoose: ");
        string choice = receiveInput(sock);

        if (choice == "3") break;

        if (choice == "1") {
            user.registerUser(sock);
            sendPrompt(sock, "Please login to continue...\n");
        }

        string uid = user.login(sock);
        if (uid.empty()) continue;

        ReservationHandler handler(uid);
        while (true) {
            sendPrompt(sock, "\n---------- DASHBOARD ----------\n1. View Ticket\n2. Reserve Ticket\n3. Logout\nChoose: ");
            string action = receiveInput(sock);
            if (action == "1")
                handler.viewTickets(sock);
            else if (action == "2")
                handler.reserve(sock);
            else if(action == "3")
                break;
            else
            {
                sendPrompt(sock,"Oops! you mistyped. Try again");
                continue;
            }
        }
    }
    close(sock);
}

// --- MAIN ---

int main() {
    int server_fd, new_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(8050);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 3);

    cout << "✅ Server is running on port 8050..." << endl;

    while (true) {
        new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        thread(handle_client, new_sock).detach(); // Handle each client in a new thread
    }

    return 0;
}
