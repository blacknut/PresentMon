// Copyright (C) 2022 Intel Corporation
// SPDX-License-Identifier: MIT
#pragma once
#include <thread>
#include <vector>
#include <string>
#include <optional>

#include "..\PresentMonUtils\MemBuffer.h"
#include "PresentMon.h"
#include "..\PresentMonUtils\PresentMonNamedPipe.h"
#include "Service.h"
#include "sddl.h"

const size_t MaxBufferSize = 4096;

class NamedPipeServer {
 public:
  NamedPipeServer(Service* srv, PresentMon* pm, std::optional<std::string> pipeName);
  ~NamedPipeServer();
  NamedPipeServer(const NamedPipeServer& t) = delete;
  NamedPipeServer& operator=(const NamedPipeServer& t) = delete;

  DWORD RunServer();

 private:
  enum class PipeStates { CONNECTING, READING, WRITING };

  struct PipeInstanceDeleter {
    void operator()(HANDLE pipeInstance) const {
      if (pipeInstance != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(pipeInstance);
        DisconnectNamedPipe(pipeInstance);
        CloseHandle(pipeInstance);
      }
    }
  };

  class Pipe {
   public:
    Pipe()
        : mRequestBuffer(std::make_unique<MemBuffer>()),
          mResponseBuffer(std::make_unique<MemBuffer>()) {}
    Pipe(const Pipe& t) = delete;
    Pipe& operator=(const Pipe& t) = delete;

    DWORD CreatePipeInstance(LPCTSTR pipe_name, int max_pipes, uint32_t pipe_timeout);

    BYTE mPipeReadBuffer[MaxBufferSize];
    IPMSMResponseHeader mServerErrorResponse = {};
    std::unique_ptr<MemBuffer> mRequestBuffer;
    std::unique_ptr<MemBuffer> mResponseBuffer;
    OVERLAPPED mOverlap = {};
    std::unique_ptr<std::remove_pointer_t<HANDLE>, PipeInstanceDeleter>
        mPipeInstance;
    DWORD mBytesRead = 0;
    PipeStates mCurrentState = PipeStates::CONNECTING;
    BOOL mPendingIO = FALSE;
  };

  DWORD CreateNamedPipesAndEvents();
  DWORD ConnectToNewClient(HANDLE pipe, LPOVERLAPPED overlapped,
                           BOOL& pendingIO);
  void GetPendingOperationResult(DWORD pipeIndex);
  void DisconnectAndReconnect(DWORD pipeIndex);
  void ExecuteCurrentState(DWORD pipeIndex);
  void ReadRequestMessage(DWORD pipeIndex);
  void WriteReplyMessage(DWORD pipeIndex);
  void EvaluateAndRespondToRequestMessage(DWORD pipeIndex);
  DWORD GetNumActiveConnections();

  std::string mPipeName;
  static const int mMaxPipes = 5;
  static const int mMaxEvents = mMaxPipes + 1;
  const UINT32 mPipeTimeout = 5000;

  Service* mService;
  PresentMon* mPm;
  Pipe mPipe[mMaxPipes];
  HANDLE mEvents[mMaxEvents];
};