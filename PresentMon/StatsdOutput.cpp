#include <WinSock2.h>

#include "PresentMon.hpp"


static SOCKET      mSocket = 0;
static SOCKADDR_IN mStatsdConnection;

static void CreateOutputStatsd()
{
    auto const& args = GetCommandLineArgs();

    if (args.mStatsdPort != 0) {
        WSADATA wsaData;
        auto version = MAKEWORD(2, 2);
        if (0 == WSAStartup(version, &wsaData)) {
            mSocket = socket(AF_INET, SOCK_DGRAM, 0);
            if (0 != mSocket)
            {
                mStatsdConnection.sin_port = htons((unsigned short)(args.mStatsdPort));
                mStatsdConnection.sin_family = AF_INET;
                mStatsdConnection.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

            }
        }
    }

    return;
}

void UpdateStatsdGauge(const char* gauge, const char* app, float value)
{
    char stat[1024];
    int iBytesToSend = 0;
    int iBytesSent = 0;

    // init
    if (mSocket == 0) {
        CreateOutputStatsd();
    }
    // still null, not requested or terrible error
    if (mSocket == 0) {
        return;
    }

    iBytesToSend = snprintf(stat, 1024, "%s,app=%s:%.2f|g", gauge, app, value);
    iBytesSent = 0;
    iBytesSent = sendto(mSocket, stat, iBytesToSend, 0, (struct sockaddr*)&mStatsdConnection, sizeof mStatsdConnection);
    if (iBytesSent != iBytesToSend) {}
}

void FinalizeStatsd()
{
    if (0 != mSocket) {
        closesocket(mSocket);
        mSocket = 0;
    }
}