// === clip_sender.cpp ===
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

using namespace std::chrono_literals;

const int DEFAULT_PORT = 50000;
const int BUFFER_SIZE = 64 * 1024; // 64KB
const int ACK_TIMEOUT_MS = 700; // milliseconds
const int MAX_RETRIES = 5;

// Read Unicode text (UTF-16) from clipboard and return as UTF-8 string.
std::string GetClipboardTextUtf8() {
    if (!OpenClipboard(NULL)) return std::string();
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData == NULL) { CloseClipboard(); return std::string(); }
    wchar_t* pszText = static_cast<wchar_t*>(GlobalLock(hData));
    if (pszText == NULL) { CloseClipboard(); return std::string(); }
    // convert UTF-16 to UTF-8
    int utf8len = WideCharToMultiByte(CP_UTF8, 0, pszText, -1, NULL, 0, NULL, NULL);
    std::string result;
    if (utf8len > 0) {
        result.resize(utf8len - 1);
        WideCharToMultiByte(CP_UTF8, 0, pszText, -1, &result[0], utf8len, NULL, NULL);
    }
    GlobalUnlock(hData);
    CloseClipboard();
    return result;
}

// Simple packet format: <seq>:<utf8-text>
// seq is decimal sequence number (monotonic)

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: clip_sender <target_ip|broadcast> [port]\n";
        return 1;
    }

    std::string target = argv[1];
    int port = (argc >= 3) ? atoi(argv[2]) : DEFAULT_PORT;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    // Enable broadcast if requested
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(port);
    if (target == "broadcast") {
        BOOL bcast = TRUE;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&bcast, sizeof(bcast));
        dest.sin_addr.s_addr = INADDR_BROADCAST;
        std::cout << "Using broadcast to port " << port << "\n";
    } else {
        dest.sin_addr.s_addr = inet_addr(target.c_str());
        std::cout << "Sending to " << target << ":" << port << "\n";
    }

    std::string lastSeen;
    uint64_t seq = 1;

    std::cout << "Polling clipboard every 600ms. Press Ctrl+C to exit.\n";
    while (true) {
        std::string current = GetClipboardTextUtf8();
        if (!current.empty() && current != lastSeen) {
            // prepare packet
            std::string packet = std::to_string(seq) + ":" + current;
            int retries = 0;
            bool acked = false;

            sockaddr_in fromAddr{};
            int fromLen = sizeof(fromAddr);
            char ackBuf[128];

            while (retries < MAX_RETRIES && !acked) {
                int sent = sendto(sock, packet.c_str(), (int)packet.size(), 0,
                                  (sockaddr*)&dest, sizeof(dest));
                if (sent == SOCKET_ERROR) {
                    std::cerr << "sendto failed\n";
                    break;
                }
                // set timeout for recv
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock, &fds);
                timeval tv{};
                tv.tv_sec = ACK_TIMEOUT_MS / 1000;
                tv.tv_usec = (ACK_TIMEOUT_MS % 1000) * 1000;
                int sel = select(0, &fds, NULL, NULL, &tv);
                if (sel > 0 && FD_ISSET(sock, &fds)) {
                    int r = recvfrom(sock, ackBuf, sizeof(ackBuf)-1, 0,
                                     (sockaddr*)&fromAddr, &fromLen);
                    if (r > 0) {
                        ackBuf[r] = '\0';
                        // Expect ACK format: ACK:<seq>
                        std::string ack(ackBuf);
                        std::string prefix = "ACK:" + std::to_string(seq);
                        if (ack.find(prefix) == 0) {
                            std::cout << "ACK received for seq " << seq << "\n";
                            acked = true;
                            break;
                        }
                    }
                } else {
                    // timeout
                    retries++;
                    std::cout << "No ACK, retry " << retries << "\n";
                }
            }

            if (!acked) {
                std::cout << "Failed to deliver seq " << seq << " after retries.\n";
            } else {
                lastSeen = current;
                seq++;
            }
        }

        Sleep(600); // Sleep for 600 milliseconds (Windows API)
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}



