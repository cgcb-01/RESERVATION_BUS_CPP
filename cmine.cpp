#include <iostream>
#include <string>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <cstring>
#include <signal.h>
using namespace std;

int sock = -1;

// Signal handler for Ctrl+C
void handle_sigint(int sig) {
    cout << "\nCaught Ctrl+C, Disconnecting from the server\n";
    if (sock != -1) {
        const char* msg = "A client got disconnected\n";
        send(sock, msg, strlen(msg), 0);
        close(sock);
    }
    exit(0);
}

int main() {
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        cerr << "❌ Socket creation failed\n";
        return 1;
    }

    signal(SIGINT, handle_sigint);

    struct sockaddr_in server;
    server.sin_family = AF_INET;
    server.sin_port = htons(8050);
    server.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (connect(sock, (struct sockaddr*)&server, sizeof(server)) < 0) {
        cerr << "❌ Connection failed\n";
        return 1;
    }

    cout << "✅ Connected to the server!\n";

    char buffer[8192];

while (true) {
    memset(buffer, 0, sizeof(buffer));

    // Receive message from server
    int bytesReceived = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytesReceived <= 0) {
        cout << "🔌 Server disconnected.\n";
        break;
    }

    buffer[bytesReceived] = '\0';
    string message(buffer);

    // ✅ Always print the server message
    cout << message;

    // ✅ Check for the explicit prompt marker
   if (message.rfind("PROMPT@") != string::npos &&
    message.length() >= 7 &&
    message.substr(message.length() - 7) == "PROMPT@") {

    string input;
    int blankCount = 0;

    while (true) {
        cout << "> ";
        getline(cin, input);

        // Trim the input
        size_t start = input.find_first_not_of(" \t\r\n");
        if (start == string::npos) {
            blankCount++;
            if (blankCount == 1) {
                cout << "⚠️  Empty input. Please enter again:\n";
                continue; // prompt again
            } else {
                // Send a space to keep sync and inform server it's junk
                input = " ";
                break;
            }
        } else {
            break; // valid input
        }
    }

    input += '\n';

    if (send(sock, input.c_str(), input.length(), 0) == -1) {
        cerr << "❌ Failed to send input\n";
        break;
        }
     }
}
    close(sock);
    return 0;
}

