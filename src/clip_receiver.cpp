// === clip_receiver.cpp ===
#include <winsock2.h>
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

const int DEFAULT_PORT_R = 50000;
const int BUFFER_SIZE_R = 64 * 1024;

// Set clipboard text from a UTF-8 string by converting to UTF-16 and using CF_UNICODETEXT
bool SetClipboardUtf8Text(const std::string& utf8) {
    int wideLen = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, NULL, 0);
    if (wideLen == 0) return false;
    HGLOBAL hGlob = GlobalAlloc(GMEM_MOVEABLE, wideLen * sizeof(wchar_t));
    if (!hGlob) return false;
    wchar_t* pwsz = (wchar_t*)GlobalLock(hGlob);
    if (!pwsz) { GlobalFree(hGlob); return false; }
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, pwsz, wideLen);
    GlobalUnlock(hGlob);

    if (!OpenClipboard(NULL)) { GlobalFree(hGlob); return false; }
    EmptyClipboard();
    SetClipboardData(CF_UNICODETEXT, hGlob); // system owns the memory now
    CloseClipboard();
    return true;
}

int main(int argc, char** argv) {
    int port = (argc >= 2) ? atoi(argv[1]) : DEFAULT_PORT_R;

    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2), &wsa);

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        std::cerr << "Bind failed\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Receiver listening on port " << port << "\n";

    std::vector<char> buf(BUFFER_SIZE_R);
    sockaddr_in from{};
    int fromLen = sizeof(from);

    while (true) {
        int r = recvfrom(sock, buf.data(), (int)buf.size()-1, 0, (sockaddr*)&from, &fromLen);
        if (r == SOCKET_ERROR) continue;
        buf[r] = '\0';
        std::string packet(buf.data(), r);
        // parse <seq>:<utf8-text>
        size_t colon = packet.find(':');
        if (colon == std::string::npos) continue;
        std::string seqstr = packet.substr(0, colon);
        std::string text = packet.substr(colon + 1);

        std::cout << "Received seq " << seqstr << " from " << inet_ntoa(from.sin_addr) << ":" << ntohs(from.sin_port) << "\n";
        bool ok = SetClipboardUtf8Text(text);
        if (ok) std::cout << "Clipboard updated.\n";
        else std::cout << "Failed to update clipboard.\n";

        // send ACK: "ACK:<seq>" back to sender
        std::string ack = "ACK:" + seqstr;
        sendto(sock, ack.c_str(), (int)ack.size(), 0, (sockaddr*)&from, fromLen);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}