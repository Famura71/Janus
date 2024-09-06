#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <Winsock2.h>
#include <string>
#include <ws2tcpip.h>
#include <thread>
#include <mutex>
#include <fstream>
#include <vector>
#include <shlobj.h> 
#include "stdafx.h"
#include <chrono>
#pragma comment(lib, "WS2_32.lib")

using namespace std;

std::mutex mtx;

// Function to escape backslashes in file paths
std::string escapeFilePath(const std::string& filePath) {
    std::string escapedPath;
    for (char ch : filePath) {
        if (ch == '\\') {
            escapedPath += "\\\\"; // Add an extra backslash
        }
        else {
            escapedPath += ch;
        }
    }
    return escapedPath;
}

// Function to send a file to the receiver
void sendFile(SOCKET clientSocket, const std::string& filePath) {
    std::ifstream inFile(filePath, std::ios::binary);
    if (!inFile.is_open()) {
        cout << "Failed to open file: " << filePath << endl;
        return;
    }

    // Extract the file name from the path
    std::string fileName = filePath.substr(filePath.find_last_of("/\\") + 1);

    // Send the file name length and file name
    size_t fileNameSize = fileName.size();
    send(clientSocket, reinterpret_cast<const char*>(&fileNameSize), sizeof(fileNameSize), 0);
    send(clientSocket, fileName.c_str(), fileNameSize, 0);

    // Get file size
    inFile.seekg(0, std::ios::end);
    std::streamsize fileSize = inFile.tellg();
    inFile.seekg(0, std::ios::beg);

    // Send the file size
    send(clientSocket, reinterpret_cast<const char*>(&fileSize), sizeof(fileSize), 0);

    // Send the file data in chunks
    char buffer[1024];
    while (inFile.read(buffer, sizeof(buffer))) {
        send(clientSocket, buffer, sizeof(buffer), 0);
    }
    // Send any remaining bytes
    send(clientSocket, buffer, inFile.gcount(), 0);

    cout << "File sent successfully: " << fileName << endl;
    inFile.close();
}

// Function to generate a unique file path
std::string getUniqueFilePath(const std::string& fileName) {
    PWSTR path = NULL;
    HRESULT result = SHGetKnownFolderPath(FOLDERID_Downloads, 0, NULL, &path);

    if (SUCCEEDED(result)) {
        char downloadsPath[MAX_PATH];
        wcstombs(downloadsPath, path, MAX_PATH);
        CoTaskMemFree(path);

        std::string directory = std::string(downloadsPath) + "\\";
        std::string filePath = directory + fileName;
        std::string newFilePath = filePath;
        int counter = 1;

        while (ifstream(newFilePath).good()) {
            size_t lastDot = filePath.find_last_of('.');
            if (lastDot == std::string::npos) {
                newFilePath = filePath + "_" + std::to_string(counter);
            }
            else {
                newFilePath = filePath.substr(0, lastDot) + "_" + std::to_string(counter) + filePath.substr(lastDot);
            }
            counter++;
        }

        return newFilePath;
    }
    else {
        std::cerr << "Error retrieving Downloads folder path. Saving file with original name in the current directory." << std::endl;
        return fileName;  // Fallback if Downloads folder cannot be found
    }
}

// Function to receive a file from the sender
void receiveFile(SOCKET acceptSocket) {
    // Receive the file name length
    size_t fileNameSize;
    if (recv(acceptSocket, reinterpret_cast<char*>(&fileNameSize), sizeof(fileNameSize), 0) <= 0) {
        std::cerr << "Error receiving file name size." << std::endl;
        return;
    }

    std::cout << "Received file name size: " << fileNameSize << std::endl;

    // Receive the file name
    std::vector<char> fileNameBuffer(fileNameSize);
    if (recv(acceptSocket, fileNameBuffer.data(), fileNameSize, 0) <= 0) {
        std::cerr << "Error receiving file name." << std::endl;
        return;
    }
    std::string fileName(fileNameBuffer.begin(), fileNameBuffer.end());

    // Generate a unique file path
    std::string uniqueFilePath = getUniqueFilePath(fileName);
    std::ofstream outFile(uniqueFilePath, std::ios::binary);
    if (!outFile.is_open()) {
        cout << "Failed to open file: " << uniqueFilePath << endl;
        return;
    }

    // Receive the file size
    std::streamsize fileSize;
    if (recv(acceptSocket, reinterpret_cast<char*>(&fileSize), sizeof(fileSize), 0) <= 0) {
        std::cerr << "Error receiving file size." << std::endl;
        return;
    }

    std::cout << "Received file size: " << fileSize << std::endl;

    // Receive the file data in chunks
    char buffer[1024];
    std::streamsize bytesReceived = 0;

    while (bytesReceived < fileSize) {
        int bytesRead = recv(acceptSocket, buffer, sizeof(buffer), 0);
        if (bytesRead > 0) {
            outFile.write(buffer, bytesRead);
            bytesReceived += bytesRead;
        }
        else if (bytesRead == 0) {
            break;
        }
        else {
            cout << "Receive error: " << WSAGetLastError() << endl;
            break;
        }
    }

    cout << "File received successfully! Saved as: " << uniqueFilePath << endl;
    outFile.close();
}


// Function to handle the server-side operations
// Server Function
void server() {
    WSADATA wsaData;
    int wsaerr;
    WORD wVersionRequested = MAKEWORD(2, 2);
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    if (wsaerr != 0) {
        std::cout << "The Winsock dll not found" << endl;
        return;
    }

    SOCKET ServerSocket = INVALID_SOCKET;
    ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ServerSocket == INVALID_SOCKET) {
        cout << "Error at socket():" << WSAGetLastError() << endl;
        return;
    }

    sockaddr_in service;
    u_short port = 71;
    service.sin_family = AF_INET;
    service.sin_addr.s_addr = INADDR_ANY; // Bind to all available interfaces
    service.sin_port = htons(port);

    if (bind(ServerSocket, (SOCKADDR*)&service, sizeof(service)) == SOCKET_ERROR) {
        cout << "Error at bind():" << WSAGetLastError() << endl;
        closesocket(ServerSocket);
        WSACleanup();
        return;
    }

    if (listen(ServerSocket, 1) == SOCKET_ERROR) {
        cout << "Error at listen():" << WSAGetLastError() << endl;
        closesocket(ServerSocket);
        WSACleanup();
        return;
    }

    SOCKET acceptSocket = accept(ServerSocket, NULL, NULL);
    if (acceptSocket == INVALID_SOCKET) {
        cout << "Error at accept():" << WSAGetLastError() << endl;
        closesocket(ServerSocket);
        WSACleanup();
        return;
    }

    receiveFile(acceptSocket);

    closesocket(ServerSocket);
    WSACleanup();
}

// Client Function
void client() {
    SOCKET clientSocket = INVALID_SOCKET;
    u_short port = 71;
    WSADATA wsaData;
    int wsaerr;
    WORD wVersionRequested = MAKEWORD(2, 2);
    wsaerr = WSAStartup(wVersionRequested, &wsaData);
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in clientService;
    clientService.sin_family = AF_INET;

    string serverIP;
    cout << "Enter server IP address: ";
    cin >> serverIP;
    InetPton(AF_INET, serverIP.c_str(), &clientService.sin_addr.s_addr);

    clientService.sin_port = htons(port);

    if (connect(clientSocket, (SOCKADDR*)&clientService, sizeof(clientService)) == SOCKET_ERROR) {
        cout << "Error at connect(): " << WSAGetLastError() << endl;
        closesocket(clientSocket);
        WSACleanup();
        return;
    }
    else {
        cout << "Connected to server!" << endl;
    }

    // Get file path from user input
    string filepath, Sonne;
    cout << "Enter your file path: " << endl;
    cin >> Sonne;
    getline(cin, filepath);
    cout << filepath << endl;
    // Handle file paths with spaces correctly
    filepath = escapeFilePath(Sonne + filepath);

    sendFile(clientSocket, filepath);
}


int main() {
    cout << "Enter 's' to start server or 'c' to start client: ";
    char choice;
    cin >> choice;

    if (choice == 's') {
        server();
    }
    else if (choice == 'c') {
        client();
    }
    else {
        cout << "Invalid choice." << endl;
    }

    return 0;
}