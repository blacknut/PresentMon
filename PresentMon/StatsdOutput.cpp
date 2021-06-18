#include <WinSock2.h>

#include "PresentMon.hpp"

static OutputStatsd CreateOutputStatsd()
{
    auto const& args = GetCommandLineArgs();

    OutputStatsd outputStatsd = {};

    if (args.mOutputStatsdPort != 0) {
        WSADATA wsaData;
        auto version = MAKEWORD(2, 2);
        WSAStartup(version, &wsaData);
        outputStatsd.mSocket = socket(AF_INET, SOCK_DGRAM, 0);
        if (outputStatsd.mSocket > 0)
        {
            outputStatsd.mStatsdConnection.sin_port = htons((unsigned short)(args.mOutputStatsdPort));
            outputStatsd.mStatsdConnection.sin_family = AF_INET;
            outputStatsd.mStatsdConnection.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        }
        else
        {
            outputStatsd.mSocket = 0;
        }
    }

    return outputStatsd;
}

OutputStatsd GetOutputStatsd(ProcessInfo* processInfo)
{
    auto const& args = GetCommandLineArgs();

    // TODO: If fopen_s() fails to open mFile, we'll just keep trying here
    // every time PresentMon wants to output to the file. We should detect the
    // failure and generate an error instead.

    if( (args.mOutputStatsdPort != 0) && (processInfo->mOutputStatsd.mSocket == 0) )
    {
        processInfo->mOutputStatsd = CreateOutputStatsd();
    }

    return processInfo->mOutputStatsd;
}

void UpdateStatsd(ProcessInfo* processInfo, SwapChainData const& chain, PresentEvent const& p)
{
    char stat[1024];
    int iBytesToSend = 0;
    int iBytesSent = 0;

    auto const& args = GetCommandLineArgs();

    // Don't output dropped frames (if requested).
    auto presented = p.FinalState == PresentResult::Presented;
    if (args.mExcludeDropped && !presented) {
        return;
    }

    // Early return if not outputing to CSV.
    auto statsdSocket = GetOutputStatsd(processInfo).mSocket;
    if (statsdSocket == 0) {
        return;
    }
    auto statsdConnection = GetOutputStatsd(processInfo).mStatsdConnection;

    iBytesToSend = snprintf(stat, 1024, "game_fps:1|c");
    iBytesSent = sendto(statsdSocket, stat, iBytesToSend, 0, (struct sockaddr*)&statsdConnection, sizeof statsdConnection);
    if (iBytesSent != iBytesToSend)
    {
    }

    // Look up the last present event in the swapchain's history.  We need at
    // least two presents to compute frame statistics.
    if (chain.mPresentHistoryCount == 0) {
        return;
    }

    auto lastPresented = chain.mPresentHistory[(chain.mNextPresentIndex - 1) % SwapChainData::PRESENT_HISTORY_MAX_COUNT].get();

    // Compute frame statistics.
    double msBetweenPresents = 1000.0 * QpcDeltaToSeconds(p.QpcTime - lastPresented->QpcTime);
    double msUntilRenderComplete = 0.0;
    double msUntilDisplayed = 0.0;
    double msBetweenDisplayChange = 0.0;

    if (args.mVerbosity > Verbosity::Simple) {
        if (p.ReadyTime > 0) {
            msUntilRenderComplete = 1000.0 * QpcDeltaToSeconds(p.ReadyTime - p.QpcTime);
        }
        if (presented) {
            msUntilDisplayed = 1000.0 * QpcDeltaToSeconds(p.ScreenTime - p.QpcTime);

            if (chain.mLastDisplayedPresentIndex > 0) {
                auto lastDisplayed = chain.mPresentHistory[chain.mLastDisplayedPresentIndex % SwapChainData::PRESENT_HISTORY_MAX_COUNT].get();
                msBetweenDisplayChange = 1000.0 * QpcDeltaToSeconds(p.ScreenTime - lastDisplayed->ScreenTime);
            }
        }
    }

    // Output in StatsD format
    iBytesToSend = snprintf( stat, 1024, "game_present_duration:%d|g", (int)msBetweenPresents);
    iBytesSent = sendto(statsdSocket, stat, iBytesToSend, 0, (struct sockaddr*)&statsdConnection, sizeof statsdConnection );
    if (iBytesSent != iBytesToSend)
    {
    }
}

