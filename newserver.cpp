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
#include <arpa/inet.h>
#include <ctime>
#include <math.h>
#include <unordered_map>
#include <fcntl.h> 
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/tcp.h>
#include <regex>
#include <openssl/sha.h>

#define BROADCAST_PORT 9000
#define TCP_PORT 8050

using namespace std;

mutex mtx;

const string USER_FILE = "users.txt";
const string DRIVER_FILE = "drivers.txt";
const string TRIPS_FILE = "trips.txt";
const string BOOKING_FILE = "bookings.txt";
const string BUS_FILE = "buses.txt";

// --- UTILITY ---

// Read from a file
vector<vector<string>> readFile(const string &filename)
{
    vector<vector<string>> data;
    ifstream file(filename);
    string line;
    while (getline(file, line))
    {
        stringstream ss(line);
        vector<string> row;
        string cell;
        while (getline(ss, cell, ','))
            row.push_back(cell);
        data.push_back(row);
    }
    return data;
}


void hash_password(const char *password, char *output)
{
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)password, strlen(password), hash);
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++)
        sprintf(output + (i * 2), "%02x", hash[i]);
    output[SHA256_DIGEST_LENGTH * 2] = '\0';
}
string extractTime(const string &dateTime) {
    // Example: "Thu May 15 06:45:00 2025" --> "06:45"
    size_t timeStart = dateTime.find(':') - 2;
    return dateTime.substr(timeStart, 5); // extracts "06:45"
}
string extractDateDDMMYYYY(const string &dateTime) {
    // input: "Thu May 15 06:45:00 2025"
    // output: "15/05/2025"
    istringstream iss(dateTime);
    string dayOfWeek, monthStr, dayStr, timeStr, yearStr;
    iss >> dayOfWeek >> monthStr >> dayStr >> timeStr >> yearStr;

    // Convert month name to number
    map<string, string> monthMap = {
        {"Jan", "01"}, {"Feb", "02"}, {"Mar", "03"}, {"Apr", "04"},
        {"May", "05"}, {"Jun", "06"}, {"Jul", "07"}, {"Aug", "08"},
        {"Sep", "09"}, {"Oct", "10"}, {"Nov", "11"}, {"Dec", "12"}
    };

    string month = monthMap[monthStr];

    if (dayStr.length() == 1) dayStr = "0" + dayStr;

    return dayStr + "/" + month + "/" + yearStr;
}



// Escape special characters in CSV
string escapeCSV(const string &field)
{
    string escaped = field;
    bool needsQuotes = escaped.find(',') != string::npos ||
                       escaped.find('"') != string::npos ||
                       escaped.find('\n') != string::npos;

    if (needsQuotes)
    {
        size_t pos = 0;
        while ((pos = escaped.find('"', pos)) != string::npos)
        {
            escaped.insert(pos, "\""); // Escape quotes by doubling them
            pos += 2;
        }
        escaped = "\"" + escaped + "\"";
    }

    return escaped;
}

// FOR FINDING UPCOMING TRIPS ++ DYNAMIC PRICING
int timeToMinutes(const string &timeStr)
{
    stringstream ss(timeStr);
    int hours, minutes;
    char colon;
    ss >> hours >> colon >> minutes;
    return hours * 60 + minutes;
}
bool isTimeDifferenceSafe(const string &existingTime, const string &newTime)
{
    int existingMinutes = timeToMinutes(existingTime);
    int newMinutes = timeToMinutes(newTime);

    return abs(existingMinutes - newMinutes) >= 60;
}

// Updating a file

void updateFile(const string &filename, const vector<vector<string>> &data)
{

    ofstream outFile(filename);
    if (!outFile)
    {
        cerr << "❌ Could not open file: " << filename << endl;
        return;
    }

    for (const auto &row : data)
    {
        for (size_t i = 0; i < row.size(); ++i)
        {
            outFile << escapeCSV(row[i]);
            if (i < row.size() - 1)
                outFile << ",";
        }
        outFile << "\n";
    }

    outFile.close();
}

void writeFile(const string &filename, const vector<string> &row) {
    // Check if row is completely empty (i.e., all fields are empty)
    bool isBlank = true;
    for (const auto &field : row) {
        if (!field.empty()) {
            isBlank = false;
            break;
        }
    }

    if (row.empty() || isBlank) {
        // Don't write empty or all-blank lines
        return;
    }

    // First write using ofstream (for convenient formatting)
    {
        ofstream file(filename, ios::app);
        if (!file) {
            cerr << "❌ Could not open file: " << filename << endl;
            return;
        }

        for (size_t i = 0; i < row.size(); ++i) {
            file << escapeCSV(row[i]);
            if (i < row.size() - 1)
                file << ",";
        }
        file << "\n";

        // Flush C++ buffers
        file.flush();
    }  // ofstream closes here

    // Then sync using file descriptor (POSIX)
    int fd = open(filename.c_str(), O_WRONLY | O_APPEND);
    if (fd != -1) {
        fsync(fd);  // Force sync to disk
        close(fd);
    }
}


// ---------- Communication Functions ----------
void sendPrompt(int sock, const string &msg)
{
    // 1. Clear any pending data in the socket buffer
    char temp_buf[256];
    while (recv(sock, temp_buf, sizeof(temp_buf), MSG_DONTWAIT) > 0) {}

    // 2. Send the message
    send(sock, msg.c_str(), msg.length(), 0);

    // 3. Force TCP buffer flush
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    // 4. Debug trace
    cout << "[SEND] " << msg << endl;

    // 5. Brief delay to stabilize client receive
    usleep(10000); // 10ms
}

void sendMessage(int sock, const string &message)
{
    send(sock, message.c_str(), message.size(), 0);
    cout << "[SEND] " << message << endl;
}

string receiveInput(int sock)
{
    int flag = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

    char buffer[8192];
    string input;
    int blankInputCount = 0;

    while (true) {
        memset(buffer, 0, sizeof(buffer));

        // Clear any unread junk in socket buffer
        fd_set set;
        struct timeval timeout = {0, 1000}; // 1ms
        FD_ZERO(&set);
        FD_SET(sock, &set);
        while (select(sock + 1, &set, NULL, NULL, &timeout) > 0) {
            recv(sock, buffer, sizeof(buffer) - 1, MSG_DONTWAIT);
        }

        int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);

        if (bytesReceived == 0) {
            cout << "[RECV] Client closed the connection.\n";
            close(sock);
            pthread_exit(NULL);
        }
        else if (bytesReceived < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);
                continue;
            }
            perror("[RECV] Error receiving data");
            continue;
        }

        buffer[bytesReceived] = '\0';
        input = string(buffer);

        // Trim whitespace
        size_t start = input.find_first_not_of(" \t\r\n");

        /*
        if (start == string::npos) {
            blankInputCount++;
            if (blankInputCount >= 2) {
                // Allow 2nd blank input to break sync (consume junk)
                cout << "[RECV] ⚠️  Second blank input detected. Consuming it to resync...\n";
                blankInputCount = 0;
                continue; // skip this input
            } else {
                sendPrompt(sock, "⚠️  Empty input. Please enter again:\nPROMPT@> ");
                continue;
            }
        }
        */

        // If input was only spaces/newlines
        if (start == string::npos) {
            continue; // silently skip if you don't want to handle it anymore
        }

        size_t end = input.find_last_not_of(" \t\r\n");
        input = input.substr(start, end - start + 1);

        if (input == "A client got disconnected") {
            cout << "⚠️  Client disconnected using Ctrl+C.\n";
            close(sock);
            pthread_exit(NULL);
        }

        cout << "[RECV] " << input << endl;
        return input;
    }
}



// Ignoring Case 
bool equalsIgnoreCase(const string a, const string b) {
    string lowerA = a;
    string lowerB = b;
    transform(lowerA.begin(), lowerA.end(), lowerA.begin(), ::tolower);
    transform(lowerB.begin(), lowerB.end(), lowerB.begin(), ::tolower);
    return lowerA == lowerB;
}   


// UPDATING SEAT FILE BY LOCKING
mutex seatLockMutex;

bool bookSeat(int sock, const string &tripId, const string &seatChoice, const string &aadhar, const string &name)
{
    string seatKey = tripId + "_" + seatChoice;

    unique_lock<mutex> seatLock(seatLockMutex);

    string seatFile = "seat" + tripId + ".txt";
    auto seatData = readFile(seatFile);

    bool seatFoundAndBooked = false;
    for (auto &seat : seatData)
    {
        if (seat.size() >= 3 && seat[0] == seatChoice && seat[1] == "0")
        {
            seat[1] = "1";
            seatFoundAndBooked = true;
            break;
        }
    }

    if (!seatFoundAndBooked)
    {
        sendPrompt(sock, "❌ Seat " + seatChoice + " is either already booked or invalid.\n");
        return false;
    }

    // Update the seat data after booking
    updateFile(seatFile, seatData);
    return true;
}

// Aadhar validation
bool isValidAadhar(const string &aadhar)
{
    return aadhar.length() == 12 && all_of(aadhar.begin(), aadhar.end(), ::isdigit);
}
bool isAadharExist(const string &aadhar)
{
    auto users = readFile(USER_FILE);
    for (const auto &row : users)
        if (row.size() > 0 && row[0] == aadhar)
            return true;
    return false;
} // d

// License validation
bool isValidLicense(const string &license)
{
    return (license.length() == 16);
}

bool isLicenseExist(const string &license)
{
    auto users = readFile(DRIVER_FILE);
    for (const auto &row : users)
        if (row.size() > 0 && row[1] == license)
            return true;
    return false;
} // d

// CURRENT TIME CHECKING

// bool isTimeAfterNow(const string &timeStr)
// {
//     int tripHour, tripMin;
//     char colon;
//     stringstream ss(timeStr);
//     if (!(ss >> tripHour >> colon >> tripMin) || colon != ':')
//     {
//         return false; // Invalid time
//     }

//     time_t now = time(0);
//     tm *ltm = localtime(&now);
//     int currMinutes = ltm->tm_hour * 60 + ltm->tm_min;
//     int tripMinutes = tripHour * 60 + tripMin;

//     return tripMinutes > currMinutes;
// } // d

bool isDateTimeAfterNow(const string &dateTimeStr)
{
    tm trip_tm = {};
    if (strptime(dateTimeStr.c_str(), "%a %b %d %H:%M:%S %Y", &trip_tm) == nullptr)
    {
        return false; // Invalid datetime format
    }

    time_t trip_time = mktime(&trip_tm);
    time_t now_time = time(nullptr);

    return difftime(trip_time, now_time) > 0;
}
// int getMinutesFromDateTime(const string &dateTimeStr)
time_t getTimeFromDateTime(const string &dateTimeStr)
{
    tm trip_tm = {};
    if (strptime(dateTimeStr.c_str(), "%a %b %d %H:%M:%S %Y", &trip_tm) == nullptr)
    {
        return -1; // Error
    }
    // return trip_tm.tm_hour * 60 + trip_tm.tm_min;
    return mktime(&trip_tm); // full datetime in seconds since epoch
}

//Printing the SEAT MATRIX 

void seatMatrix(const string &tripId, int rows, int cols, int sock) {
    string seatFile = "seat" + tripId + ".txt";
    auto seatData = readFile(seatFile); // Load seat data

    stringstream response;
    response << "SEAT CHART FOR THE TRIP " << tripId << "\n";

    // GATE and driver header
   stringstream ss;
ss << "\n╔═══════════════════════════════╗";
ss << "\n║         BUS SEAT STATUS       ║";
ss << "\n╠═══════╦═══════════════════════╣";
ss << "\n║ G     ║    DRIVER SEAT        ║";
ss << "\n║       ╠═══════════════════════╣";
ss << "\n║ A     ║ Seat Position: Optimal║";
ss << "\n║ T     ║ Temperature:  22°C    ║";
ss << "\n║ E     ║ Occupancy:    Detected║";
ss << "\n╚═══════╩═══════════════════════╝";
response << ss.str() << "\n\n";

    int leftCols = cols / 2;
    int rightCols = cols - leftCols;
    int totalSeats = rows * cols;
    int seatIndex = 0;

    for (int row = 0; row < rows; ++row) {
        stringstream iconLine;
        stringstream numberLine;

        iconLine << "|";
        numberLine << "|";

        // Left side
        for (int i = 0; i < leftCols; ++i) {
            if (seatIndex < seatData.size() && seatData[seatIndex].size() >= 2) {
                string status = seatData[seatIndex][1];
                string seatIcon = (status == "0") ? "💺" : "❌";
                iconLine << setw(2) << seatIcon << " ";
                numberLine << setw(2) << setfill('0') << seatData[seatIndex][0] << " ";
            } else {
                iconLine << setw(3) << " ";
                numberLine << setw(3) << " ";
            }
            ++seatIndex;
        }

        // Middle aisle spacing
        int aisleSpacing = 31 - (leftCols + rightCols) * 3 - 2; // 2 for '|'
        iconLine << string(aisleSpacing, ' ');
        numberLine << string(aisleSpacing, ' ');

        // Right side
        for (int i = 0; i < rightCols; ++i) {
            if (seatIndex < seatData.size() && seatData[seatIndex].size() >= 2) {
                string status = seatData[seatIndex][1];
                string seatIcon = (status == "0") ? "💺" : "❌";
                iconLine << setw(2) << seatIcon << " ";
                numberLine << setw(2) << setfill('0') << seatData[seatIndex][0] << " ";
            } else {
                iconLine << setw(3) << " ";
                numberLine << setw(3) << " ";
            }
            ++seatIndex;
        }

        iconLine << " |";
        numberLine << " |";

        response << iconLine.str() << "\n";
        response << numberLine.str() << "\n";
    }
    response << "\n|===============================|";
    response << "\n 💺 = Available, ❌ = Booked\n";

    // Collect price details
    string windowPrice = "N/A";
    string middlePrice = "N/A";
    string backWindowPrice = "N/A";
    string backMiddlePrice = "N/A";

    if (!seatData.empty()) {
        // Window seat (first seat)
        if (seatData[0].size() >= 3) {
            windowPrice = seatData[0][2];
        }

        // Middle seat (if cols > 2)
        if (cols > 2) {
            if (seatData.size() > 1 && seatData[1].size() >= 3) {
                middlePrice = seatData[1][2];
            } else {
                middlePrice = "N/A";
            }
        }

        // Back window seat
        int backRowIndex = (rows - 1) * cols;
        if (backRowIndex < seatData.size() && seatData[backRowIndex].size() >= 3) {
            backWindowPrice = seatData[backRowIndex][2];
        }

        // Back middle seat (if cols > 2)
        if (cols > 2) {
            int backMiddleIndex = backRowIndex + 1;
            if (backMiddleIndex < seatData.size() && seatData[backMiddleIndex].size() >= 3) {
                backMiddlePrice = seatData[backMiddleIndex][2];
            } else {
                backMiddlePrice = "N/A";
            }
        }
    }

    // Build the price details response
    response << "\n IMPORTANT PRICE DETAILS\n";
    if (cols > 2) {
        response << "1. LOWER PRICE FOR MIDDLE SEATS: " << middlePrice << "\n";
    } else {
        response << "1. LOWER PRICE FOR MIDDLE SEATS: Not applicable (no middle seats in 2-column layout)\n";
    }

    response << "2. MORE LOWER PRICES FOR BACK SEATS -> BACK WINDOW: " << backWindowPrice;
    if (cols > 2) {
        response << ", BACK MIDDLE: " << backMiddlePrice;
    }
    response << "\n";

    response << "3. PRICE HIKE FOR WINDOW SEATS: " << windowPrice << "\n";

    sendMessage(sock, response.str());
}

// --- CLASS DECLARATIONS ---

class User
{
public:
    void registerUser(int sock);
    string login(int sock);
};

// reservation handler
class ReservationHandler
{
private:
    string uid;

public:
    ReservationHandler(string userID) : uid(userID) {}

    void viewTickets(int sock);
    vector<vector<string>> viewTrips(int sock);
    void reserve(int sock);
};

// driver class
class Driver
{
public:
    void registerDriver(int sock);
    string loginDriver(int sock);
};

//------BUS_TRIP_HANDLER-------------

class bus_trip_handler
{
    string aadhar;

public:
    bus_trip_handler(string a) : aadhar(a) {}; // constructor

    void registerBus(int sock);
    void insertTrip(int sock);
    void createSeatFile(const string &tripId, int row, int col,float dist);

private:
    string generateTripID()
    {       
    auto trips = readFile(TRIPS_FILE);
    int maxID = 0;

    std::regex idPattern(R"(T(\d+))");

    for (const auto& trip : trips) {
        if (trip.empty()) continue;

        std::smatch match;
        if (std::regex_match(trip[0], match, idPattern)) {
            int idNum = std::stoi(match[1]);
            if (idNum > maxID) {
                maxID = idNum;
            }
        }
    }

    stringstream ss;
    ss << "T" << setfill('0') << setw(3) << (maxID + 1);
    return ss.str();
}
}; // d

// --------- Create Seat File Function----------
void bus_trip_handler::createSeatFile(const string &tripId, int rows, int cols, float dist)
{
    int k = 1;
    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; ++c)
        {
            string seatNum = to_string(k);
            string status = "0";

            int baseprice;

            // Check for back row seats first
            if ((r == rows - 1) && (c == 0 || c == cols - 1))
            {
                baseprice = 105; // Back window seats
            }
            else if (r == rows - 1)
            {
                baseprice = 100; // Back middle seats
            }
            else if (c == 0 || c == cols - 1)
            {
                baseprice = 150; // Window seats (not back row)
            }
            else
            {
                baseprice = 120; // Middle seats (not back row)
            }

            // Calculate price based on distance
            int multiplier = ceil(dist / 80.0);
            int price = baseprice * multiplier;

            vector<string> row = {seatNum, status, to_string(price)};
            writeFile("seat" + tripId + ".txt", row);
            k++;
        }
    }
}

// --- USER METHODS ---

//------------USER REGISTER--------------------------
void User::registerUser(int sock)
{
    sendPrompt(sock, "Enter your Name:PROMPT@");
    string name = receiveInput(sock);

    int age = 0;
    while (true)
    {
        sendPrompt(sock, "Enter your Age:PROMPT@");
        string ageinput = receiveInput(sock);
        try
        {
            age = stoi(ageinput);
            if (age < 1)
                sendMessage(sock, "Children below 1 year of age are not eligible for safety concerns.\n");
            else if (age > 150)
                sendMessage(sock, "❌ Enter realistic age data.\n");
            else
                break;
        }
        catch (invalid_argument &e)
        {
            sendMessage(sock, "❌ Invalid input. Please enter a number for age.\n");
        }
    }

    string aadhar;
    while (true)
    {
        sendPrompt(sock, "Enter your Unique Aadhar number:PROMPT@");
        aadhar = receiveInput(sock);
        if (!isValidAadhar(aadhar))
        {
            sendMessage(sock, "❌ Invalid Aadhar number. It must be 12 digits.\n");
            continue;
        }
        if (isAadharExist(aadhar))
        {
            sendPrompt(sock, "This Aadhar number is already registered.\nIs it a Typo error? (y/n): PROMPT@");
            string ans = receiveInput(sock);
            if (ans == "y" || ans == "Y")
            {
                sendMessage(sock, "No worries, Re-enter again.\n");
                continue;
            }
            else
            {
                sendMessage(sock, "You're already registered. Please login instead.\n");
                return;
            }
        }
        else
        {
            break;
        }
    }

    sendPrompt(sock, "Enter a Strong Password:PROMPT@");
    string password = receiveInput(sock);

     // Hash the password
    char hash[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_password(password.c_str(), hash);

    mtx.lock();
    // writeFile(USER_FILE, {aadhar, name, to_string(age), password});
    writeFile(USER_FILE, {aadhar, name, to_string(age), string(hash)});
    mtx.unlock();
    sendMessage(sock, "✅ Registration Successful!\n");
} // d

//----------USER LOGIN----------------
string User::login(int sock)
{
    sendPrompt(sock, "Enter Your Aadhar Number:PROMPT@");
    string aadhar = receiveInput(sock);
    sendPrompt(sock, "Enter Your Password:PROMPT@");
    string password = receiveInput(sock);

    // Hash the entered password
    char hashedPassword[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_password(password.c_str(), hashedPassword);

    auto users = readFile(USER_FILE);
    for (auto &row : users)
    {
        if (row.size() < 4)
            continue;
        // if (row[0] == aadhar && row[3] == password)
        if (row[0] == aadhar && row[3] == hashedPassword)
        {
            sendMessage(sock, "✅ Login Successful!\n");
            return aadhar;
        }
    }

    sendMessage(sock, "❌ Invalid Aadhar number or Password.\n");
    return "";
} // d

//------DRIVER CLASS-----------------

void Driver::registerDriver(int sock)
{

    // Asking for name
    sendPrompt(sock, "Enter your Name:PROMPT@");
    string name = receiveInput(sock);

    // asking for age
    int age = 0;
    while (true)
    {
        sendPrompt(sock, "Enter your Age:PROMPT@");
        string ageinput = receiveInput(sock);
        try
        {
            age = stoi(ageinput);
            if (age < 25 || age > 60)
                sendMessage(sock, "Not Eligible to register here as a driver.\n");
            else
                break;
        }
        catch (invalid_argument &e)
        {
            sendMessage(sock, "❌ Invalid input. Please enter a number for age.\n");
        }
    }

    // Asking for Aadhar no
    string aadhar;
    while (true)
    {
        sendPrompt(sock, "Enter your Unique Aadhar number:PROMPT@");
        aadhar = receiveInput(sock);
        if (!isValidAadhar(aadhar))
        {
            sendMessage(sock, "❌ Invalid Aadhar number. It must be 12 digits.\n");
            continue;
        }
        if (isAadharExist(aadhar))
        {
            sendPrompt(sock, "This Aadhar number is already registered.\nIs it a Typo error? (y/n): PROMPT@");
            string ans = receiveInput(sock);
            if (ans == "y" || ans == "Y")
            {
                sendMessage(sock, "No worries, Re-enter again.\n");
                continue;
            }
            else
            {
                sendMessage(sock, "You're already registered. Please login instead.\n");
                return;
            }
        }
        else
        {
            break;
        }
    }

    // Asking for license
    string license;
    while (true)
    {
        sendPrompt(sock, "Enter your Unique License number:PROMPT@");
        license = receiveInput(sock);
        if (!isValidLicense(license))
        {
            sendMessage(sock, "❌ Invalid License number.\n");
            continue;
        }
        if (isLicenseExist(license))
        {
            sendPrompt(sock, "This License number is already registered.\nIs it a Typo error? (y/n): PROMPT@");
            string ans = receiveInput(sock);
            if (ans == "y" || ans == "Y")
            {
                sendMessage(sock, "No worries, Re-enter again.\n");
                continue;
            }
            else
            {
                sendMessage(sock, "You're already registered. Please login instead.\n");
                return;
            }
        }
        else
        {
            break;
        }
    }

    sendPrompt(sock, "Enter a Strong Password:PROMPT@");
    string password = receiveInput(sock);

    // Hash the password
    char hash[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_password(password.c_str(), hash);

    mtx.lock();
    writeFile(DRIVER_FILE, {aadhar, license, name, to_string(age), string(hash)});
    // writeFile(DRIVER_FILE, {aadhar, license, name, to_string(age), password});
    mtx.unlock();
    sendMessage(sock, "✅ Registration Successful!\n");
} // d

//--------------LOGIN DRIVER-----------------
string trim(const string &s)
{
    size_t start = s.find_first_not_of(" \t\n\r");
    size_t end = s.find_last_not_of(" \t\n\r");
    if (start == string::npos || end == string::npos)
        return "";
    return s.substr(start, end - start + 1);
}
string Driver::loginDriver(int sock)
{
    sendPrompt(sock, "Enter Your Aadhar Number:PROMPT@");
    string aadhar = trim(receiveInput(sock));

    sendPrompt(sock, "Enter Your Password:PROMPT@");
    string password = trim(receiveInput(sock));

    // Hash the entered password
    char hashedPassword[SHA256_DIGEST_LENGTH * 2 + 1];
    hash_password(password.c_str(), hashedPassword);

    auto users = readFile(DRIVER_FILE);

    for (auto &row : users)
    {
        if (row.size() < 5)
            continue;

        string storedAadhar = trim(row[0]);
        string storedPassword = trim(row[4]);

        if (storedAadhar == aadhar && storedPassword == hashedPassword)
        // if (storedAadhar == aadhar && storedPassword == password)
        {
            sendMessage(sock, "✅ Login Successful!\n");
            return storedAadhar;
        }
    }

    sendMessage(sock, "❌ Invalid Aadhar number or Password.\n");
    return "";
}

//-------------Insert a Trip(bus_trip_handler)------------------
// Function to split string by delimiter
vector<string> split(const string &s, char delimiter)
{
    vector<string> tokens;
    string token;
    istringstream tokenStream(s);
    while (getline(tokenStream, token, delimiter))
    {
        tokens.push_back(token);
    }
    return tokens;
}

// Function to validate date and compare
bool validateAndCompareDate(const string &departDate, time_t &timestamp, const string &startTime)
{
    vector<string> parts = split(departDate, '/');
    vector<string> timeParts = split(startTime, ':');

    // Check for proper format
    if (parts.size() != 3)
        return false;

    int day, month, year, hh, mm;
    try
    {
        day = stoi(parts[0]);
        month = stoi(parts[1]);
        year = stoi(parts[2]);
        hh = stoi(timeParts[0]);
        mm = stoi(timeParts[1]);
    }
    catch (...)
    {
        return false; // Non-numeric input
    }

    // Basic range checks
    if (month < 1 || month > 12 || day < 1 || day > 31 || year < 1900)
        return false;

    // Set up struct tm
    struct tm datetime = {};
    datetime.tm_year = year - 1900;
    datetime.tm_mon = month - 1;
    datetime.tm_mday = day;
    datetime.tm_hour = hh;
    datetime.tm_min = mm;
    datetime.tm_sec = 0;
    datetime.tm_isdst = -1;

    // Convert to time_t
    timestamp = mktime(&datetime);
    cout << ctime(&timestamp);
    if (timestamp == -1)
        return false; // Invalid timestamp

    // Compare with current time
    time_t now = time(NULL);
    if (difftime(timestamp, now) < 0)
    {
        // Date is in the past
        return false;
    }

    // Valid and in future
    return true;
}

//-----------INSERT TRIPS----------------
void bus_trip_handler::insertTrip(int sock)
{
    sendPrompt(sock, "Enter Bus Number: PROMPT@");
    string busNo = receiveInput(sock);

    sendPrompt(sock, "Enter Source: PROMPT@");
    string source = receiveInput(sock);

    sendPrompt(sock, "Enter Destination: PROMPT@");
    string destination = receiveInput(sock);

    sendPrompt(sock, "Enter Departure Date (DD/MM/YYYY): PROMPT@");
    string departDate = receiveInput(sock);

    sendPrompt(sock, "Enter Start Time (HH:MM): PROMPT@");
    string startTime = receiveInput(sock);

    sendPrompt(sock,"Enter the Total Distance being covered in KM:PROMPT@");
    string distance = receiveInput(sock);
    float dist;
    try
        {
            dist = stof(distance);
        }
        catch (invalid_argument &e)
        {
            sendMessage(sock, "❌ Invalid input. Please enter a number\n");
        }

    auto buses = readFile("buses.txt");
    int rows = -1, cols = -1;
    for (auto &bus : buses)
    {
        if (bus.size() >= 3 && bus[0] == busNo)
        {
            rows = stoi(bus[2]);
            cols = stoi(bus[3]);
            break;
        }
    }
    time_t timestamp;
    string departure = "";
    if (validateAndCompareDate(departDate, timestamp, startTime) == false)
    {
        sendMessage(sock, "❌ Departure date or time invalid.\n");
        return;
    }
    else
    {

        departure = ctime(&timestamp);
        if (!departure.empty() && departure.back() == '\n')
        {
            departure.pop_back();
        }
    }

    if (rows <= 0 || cols <= 0)
    {
        sendMessage(sock, "❌ Bus not found or invalid seat dimensions.\n");
        return;
    }

    auto trips = readFile("trips.txt");
    for (auto &trip : trips)
    {
        if (trip.size() == 7 && trip[1] == busNo)
        {
            string existingDateTime = trip[6];
        string existingDate = extractDateDDMMYYYY(existingDateTime);

        if (existingDate == departDate)
        {
            string existingTime = extractTime(existingDateTime);  // "HH:MM"
            
            if (!isTimeDifferenceSafe(existingTime, startTime))
            {
                sendPrompt(sock, "❌ A trip with this bus is already scheduled on the same date within 60 minutes.\n");
                return;
            }
        }
        }
    }

    string tripID = generateTripID();

    mtx.lock();
    writeFile(TRIPS_FILE, {tripID, busNo, source, destination, distance, aadhar, departure});
    mtx.unlock();
    // creating seat file (rows and cols are int)
    createSeatFile(tripID, rows, cols,dist);

    sendMessage(sock, "✅ A Trip is being inserted successfully with Trip ID " + tripID + "\n");
} // d

//-----------Register a bus-------------
void bus_trip_handler::registerBus(int sock)
{
    sendPrompt(sock, "Enter Bus Number (unique ID): PROMPT@");
    string busNo = receiveInput(sock);

    // Check if busNo already exists
    auto buses = readFile("buses.txt");
    for (auto &b : buses)
    {
        if (!b.empty() && b[0] == busNo)
        {
            sendPrompt(sock, "❌ Bus Number already registered.\n");
            return;
        }
    }

    // Get rows
    sendPrompt(sock, "Enter number of seat rows: PROMPT@");
    string rowStr = receiveInput(sock);

    // Get columns
    sendPrompt(sock, "Enter number of seat columns: PROMPT@");
    string colStr = receiveInput(sock);

    // Validate inputs
    try
    {
        int rows = stoi(rowStr);
        int cols = stoi(colStr);

        if (rows <= 0 || cols <= 0)
        {
            sendMessage(sock, "❌ Invalid row or column count for a bus\n");
            return;
        }

        mtx.lock();
        writeFile("buses.txt", {busNo, aadhar, rowStr, colStr});
        mtx.unlock();

        sendMessage(sock, "✅ Bus registered successfully.\n");
    }
    catch (...)
    {
        sendPrompt(sock, "❌ Invalid input. Please enter numeric values.\n");
    }
} // d

//-----------RESERVATION HANDLER-----------------

//-------------VIEW TICKETS----------
void ReservationHandler::viewTickets(int sock)
{
    // auto bookings = readFile("bookings.txt");
    // string response = "🧾 Your Bookings:\n";
    // int count = 0;

    // for (auto &b : bookings)
    // {
    //     if (b.size() >= 5 && b[3] == uid)
    //     {
    //         response += to_string(++count) + ". Trip ID: " + b[0] + ",Bus No:" + b[1] + ", Seat No: " + b[2] + "\n";
    //     }
    // }

    // if (count == 0)
    // {
    //     response = "❌ No bookings found under your ID.\n";
    // }

    // sendPrompt(sock, response);
    auto bookings = readFile("bookings.txt");
    auto trips = readFile("trips.txt");

    string response = "🎫 Your Booked Tickets:\n\n";
    int count = 0;

    for (auto &b : bookings)
    {
        if (b.size() >= 7 && b[3] == uid)
        {
            string tripID = b[0];
            string busNo = b[1];
            string seatNo = b[2];
            string maskedAadhar = string(b[3].length() - 4, 'X') + b[3].substr(b[3].length() - 4);
            string username = b[4];
            string price = b[5];
            string bookingTime = b[6];

            // Find the corresponding trip from trips.txt
            vector<string> tripInfo;
            for (auto &t : trips)
            {
                if (t.size() >= 7 && t[0] == tripID)
                {
                    tripInfo = t;
                    break;
                }
            }

            if (tripInfo.empty())
                continue; // Skip if no matching trip found

            string source = tripInfo[2];
            string destination = tripInfo[3];
            string departure = tripInfo[6];

            response += "-----------------------------------------\n";
            response += "🚌 Ticket #" + to_string(++count) + "\n";
            response += "Trip ID       : " + tripID + "\n";
            response += "Bus Number    : " + busNo + "\n";
            response += "Seat Number   : " + seatNo + "\n";
            response += "Name          : " + username + "\n";
            response += "Aadhar Number : " + maskedAadhar + "\n";
            response += "Ticket Price  : ₹" + price + "\n";
            response += "Booking Time  : " + bookingTime + "\n";
            response += "-----------------------------------------\n";
            response += "Route         : " + source + " ➡ " + destination + "\n";
            response += "Departure Date: " + departure + "\n";
            response += "=========================================\n\n";
        }
    }

    if (count == 0)
    {
        response = "❌ No bookings found under your ID.\n";
    }

    sendMessage(sock, response);
}

//-----------VIEW TRIPS--------------
vector<vector<string>> ReservationHandler::viewTrips(int sock)
{
    auto trips = readFile(TRIPS_FILE);
    vector<vector<string>> upcomingTrips;

    // Get current time
    auto now = chrono::system_clock::now();
    time_t now_time = chrono::system_clock::to_time_t(now);
    tm now_tm = *localtime(&now_time);
    int currentMinutes = now_tm.tm_hour * 60 + now_tm.tm_min;

    // Format: TripID, BusNo, Source, Destination, StartTime, DriverName
    for (auto &trip : trips)
    {
        // if (trip.size() == 6 && isTimeAfterNow(trip[4]))
        // {
        //     // Calculate time difference
        //     int tripMinutes = timeToMinutes(trip[4]);
        //     int timeDiff = tripMinutes - currentMinutes;

        //     // Add pricing indicator if less than 1 hour remains
        //     if (timeDiff <= 60 && timeDiff > 0)
        //     {
        //         trip.push_back("⚠️ Less than 1 hour left! Price Decreased - Book Now!");
        //     }
        //     upcomingTrips.push_back(trip);
        // }
        if (trip.size() != 7)
            continue;

        string dateTimeStr = trip[6];

        if (isDateTimeAfterNow(dateTimeStr))
        {
            time_t tripTime = getTimeFromDateTime(dateTimeStr);
            // int tripMinutes = getMinutesFromDateTime(dateTimeStr);
            // int timeDiff = tripMinutes - currentMinutes;
            // cout << timeDiff << endl;
            // if (timeDiff <= 60 && timeDiff > 0)
            time_t nowTime = time(nullptr);
            int timeDiffMinutes = difftime(tripTime, nowTime) / 60;

            cout << "Time diff (mins): " << timeDiffMinutes << endl;

            if (timeDiffMinutes <= 60 && timeDiffMinutes > 0)
            {
                
                trip[6] += " [ DISCOUNT-⚠️ Less than 1 hour left! Price Decreased - Hurry!]";
            }

            upcomingTrips.push_back(trip);
        }
    }

    if (upcomingTrips.empty())
    {
        sendMessage(sock, "❌ No upcoming trips are available.\n");
    }

    return upcomingTrips;
} // d


//------------RESERVE TICKET------------------
bool validate(string aadhar, string name)
{
 bool registered = false;
     auto users = readFile(USER_FILE);
         for (auto &user : users)
          {
           if (user.size() >= 2 && user[0] == aadhar && equalsIgnoreCase(user[1],name))
                {
                  registered = true;
                  break;
                 }
          }
    return registered;
}

void ReservationHandler::reserve(int sock) {

    while (true) {
        vector<vector<string>> available = viewTrips(sock);
        if (available.empty()) {
            sendMessage(sock, "No upcoming buses available\n");
            return;
        }

        // Build trip list with pricing info
        string tripOptions = "\n====================================\n"
                            "         🚌 Upcoming Trips          \n"
                            "------------------------------------\n\n";
        unordered_map<string, bool> hasDiscount;
        vector<string> tripIds;
        
        for (size_t i = 0; i < available.size(); ++i) {
            auto& t = available[i];
            string priceNote = "";
            bool discount = false;
            if (t.size() == 7 && t[6].find("DISCOUNT") != string::npos) 
            discount = true;
            
            tripOptions += to_string(i+1) + ". " + t[0] + " | " + t[1] + " | " + t[2] 
                        + " → " + t[3] + " | " + t[6] +"\n";
            tripIds.push_back(t[0]);
            hasDiscount[t[0]] = discount;
        }
        sendMessage(sock, tripOptions);

        // Trip selection
        string currentTripId, busNo;
        int rows = 0, cols = 0;
        
        while (true) {
            try {
                sendPrompt(sock, "\nEnter Trip ID (r to refresh trips /e to exit from here): PROMPT@");
                string input = receiveInput(sock);

                if (input == "e") {
                    sendMessage(sock, "Exiting...\n");
                    return;
                }
                if (input == "r") break;

                auto it = find(tripIds.begin(), tripIds.end(), input);
                if (it == tripIds.end()) {
                    sendMessage(sock, "❌ Invalid Trip ID\n");
                    continue;
                }

                currentTripId = input;
                // Validate trip time and get details
                for (auto& trip : available) {
                    if (trip[0] == currentTripId) {
                        busNo = trip[1];
                        string datetimeStr = trip[6];
                        
                        // Time validation
                        tm tripTm = {};
                        istringstream ss(datetimeStr);
                        ss >> get_time(&tripTm, "%a %b %d %H:%M:%S %Y");
                        time_t tripTime = mktime(&tripTm);
                        
                        if (difftime(tripTime, time(nullptr)) <= 0) {
                            sendMessage(sock, "⚠️ Trip has departed!\n");
                            currentTripId.clear();
                            break;
                        }
                        
                        // Get bus layout
                        auto buses = readFile(BUS_FILE);
                        for (auto& b : buses) {
                            if (b.size() >= 4 && b[0] == busNo) {
                                rows = stoi(b[2]);
                                cols = stoi(b[3]);
                                break;
                            }
                        }
                        break;
                    }
                }
                if (!currentTripId.empty()) break;
            }
            catch (const invalid_argument&) {
                sendMessage(sock, "❌ Input cannot be blank!\n");
            }
        }

        if (currentTripId.empty()) continue;

        // Seat booking
        bool returnToTrips = false;
        while (!returnToTrips) {
            seatMatrix(currentTripId, rows, cols, sock);

            try {
                sendPrompt(sock, "\nChoose seat (c to change trip /e to exit /seat#): PROMPT@");
                string seatChoice = receiveInput(sock);

                if (seatChoice == "c") { returnToTrips = true; break; }
                if (seatChoice == "e") return;
                if (seatChoice == "v") continue;

                // Validate seat
                auto seatData = readFile("seat" + currentTripId + ".txt");
                bool validSeat = false;
                float basePrice = 0.0f;
                
                for (auto& seat : seatData) {
                    if (seat.size() >= 3 && seat[0] == seatChoice && seat[1] == "0") {
                        validSeat = true;
                        basePrice = stof(seat[2]);
                        break;
                    }
                }

                if (!validSeat) {
                    sendMessage(sock, "❌ Invalid/occupied seat\n");
                    continue;
                }

                // Passenger details
                string name, aadhar;
              while(true)
              {
                        sendPrompt(sock, "Passenger Name:PROMPT@");
                        name = receiveInput(sock);                     
              
                        sendPrompt(sock, "Aadhar Number:PROMPT@");
                        aadhar = receiveInput(sock);
                bool registered=validate(aadhar,name);
                if(registered)break;
                else
                sendMessage(sock,"\n ❌ ENTER A REGISTERED USER\n");
              }
                // Price calculation
                bool applyDiscount = hasDiscount[currentTripId];
                float finalPrice = applyDiscount ? basePrice * 0.9f : basePrice;
                
                stringstream priceMsg;
                priceMsg << "💰 Final Price: Rs" << fixed << setprecision(2) << finalPrice;
                if (applyDiscount) {
                    priceMsg << " (10% discount applied!)";
                }

                // Confirmation
                string confirm;
                while (true) {
                    sendPrompt(sock, priceMsg.str() + "\nConfirm (y/n): PROMPT@");
                    confirm = receiveInput(sock);
                    if (confirm == "y" || confirm == "n") break;
                    sendMessage(sock, "❌ Invalid choice!\n");
                }

                if (confirm == "y") {
                    time_t timestamp = time(nullptr);
                    if (bookSeat(sock, currentTripId, seatChoice, aadhar, name)) {
                        char timeBuf[80];
                        strftime(timeBuf, sizeof(timeBuf), "%c", localtime(&timestamp));
                        
                        vector<string> booking = {
                            currentTripId, busNo, seatChoice, 
                            aadhar, name, to_string(finalPrice), timeBuf
                        };
                        writeFile(BOOKING_FILE, booking);
                        
                        sendMessage(sock, "✅ Seat is being Booked Successfully! " + string(timeBuf) + "\n");
                        
                        // Post-booking action
                        string another;
                        while (true) {
                            sendPrompt(sock, "Book another seat? (y/n): PROMPT@");
                            another = receiveInput(sock);
                            if (another == "y" || another == "n") break;
                            sendMessage(sock, "❌ Invalid input!\n");
                        }

                        if (another == "n") {
                            returnToTrips = true;
                            sendMessage(sock, "Returning to trip selection...\n");
                        }
                    }
                }
            }
            catch (const invalid_argument&) {
                sendMessage(sock, "❌ Input cannot be blank!\n");
            }
        }
    }
}



 
//DRIVER CLIENT--------------
void driver_client(int sock)
{

    Driver driver;
    while (true)
    {
        sendPrompt(sock, "\n---------- MAIN MENU ----------\n1. Register\n2. Login\n3. Exit\nChoose: PROMPT@");
        string choice = receiveInput(sock);

        if (choice == "3")
            break;

        if (choice == "1")
        {
            driver.registerDriver(sock);
            sendMessage(sock, "Please login to continue...\n");
        }

        string uid = driver.loginDriver(sock);
        if (uid.empty())
            continue;

        bus_trip_handler handlerbus(uid);
        while (true)
        {
            sendPrompt(sock, "\n---------- DASHBOARD ----------\n1. Register a bus \n2. Insert a trip\n3. Logout\nChoose: PROMPT@");
            string action = receiveInput(sock);
            if (action == "1")
                handlerbus.registerBus(sock);
            else if (action == "2")
                handlerbus.insertTrip(sock);
            else
                break;
        }
    }
    close(sock);
} // d

//----------USER CLIENT----------------
void handle_client(int sock)
{
stringstream ss;
ss << "\n╔══════════════════════════════════════════════════════════╗";
ss << "\n║                  BUS RESERVATION SYSTEM                  ║";
ss << "\n╠══════════════════════════════════════════════════════════╣";
ss << "\n║                    ARE YOU HERE AS A ?                   ║";
ss << "\n║                                                          ║";
ss << "\n║              1. PASSENGER   2. BUS DRIVERS               ║";
ss << "\n╚══════════════════════════════════════════════════════════╝";
ss << "\n  ENTER YOUR CHOICE :  PROMPT@";

    sendPrompt(sock,ss.str());
    string c = receiveInput(sock); 
    if (c == "2")
        driver_client(sock);
    User user;
    while (true)
    {
        sendPrompt(sock, "\n---------- MAIN MENU ----------\n1. Register\n2. Login\n3. Exit\nChoose: PROMPT@");
        string choice = receiveInput(sock);

        if (choice == "3")
            break;

        if (choice == "1")
        {
            user.registerUser(sock);
            sendMessage(sock, "Please login to continue...\n");
        }

        string uid = user.login(sock);
        if (uid.empty())
            continue;

        ReservationHandler handler(uid);
        while (true)
        {
            sendPrompt(sock, "\n---------- DASHBOARD ----------\n1. View Ticket\n2. Reserve Ticket\n3. Logout\nChoose: PROMPT@");
            string action = receiveInput(sock);
            if (action == "1")
                handler.viewTickets(sock);
            else if (action == "2")
                handler.reserve(sock);
            else if (action == "3")
                break;
            else
            {
                sendMessage(sock, "Oops! you mistyped. Try again");
                continue;
            }
        }
    }
    close(sock);
}

// --- MAIN ---

// ---------- UDP Broadcast Function ----------
void broadcastServerIP()
{
    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    int broadcast = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    sockaddr_in broadcast_addr{};
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(BROADCAST_PORT);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    string message = "SERVER:" + to_string(TCP_PORT);

    while (true)
    {
        sendto(udp_socket, message.c_str(), message.length(), 0,
               (sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
        cout << "[Broadcasting] " << message << "\n";
        sleep(10);
    }
}

// ---------- Main Function ----------
int main()
{
    // Start UDP broadcasting
    thread broadcaster(broadcastServerIP);
    broadcaster.detach();

    // Setup TCP server
    int server_fd, new_sock;
    struct sockaddr_in address;
    int addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    // Add these socket options
int flag = 1;
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
setsockopt(server_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(TCP_PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 3);

    cout << "✅ Server is running on port " << TCP_PORT << " and broadcasting..." << endl;

    // Accept multiple clients
    while (true)
    {
        new_sock = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
         // Set socket options for the client connection
    int flag = 1;
    setsockopt(new_sock, SOL_SOCKET, SO_KEEPALIVE, &flag, sizeof(flag));
    setsockopt(new_sock, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        thread(handle_client, new_sock).detach();
    }

    return 0;
}
