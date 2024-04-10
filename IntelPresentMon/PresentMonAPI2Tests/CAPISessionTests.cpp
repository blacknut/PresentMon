#include "StatusComparison.h"
#include "CppUnitTest.h"
#include "../PresentMonAPI2/PresentMonAPI.h"
#include "../PresentMonAPI2/Internal.h" 
#include <cstring>
#include <vector>
#include <optional>
#include "Utilities.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace PresentMonAPI2Mock
{
	TEST_CLASS(CAPISessionTests)
	{
	private:
		PM_SESSION_HANDLE hSession_;
	public:
		TEST_METHOD_CLEANUP(AfterEachTestMethod)
		{
			pmCloseSession(hSession_);
		}
		TEST_METHOD(OpenAndCloseMockSession)
		{
			char buffer[256]{};

			pmSetMiddlewareAsMock_(true);
			Assert::AreEqual(PM_STATUS_SUCCESS, pmOpenSession(&hSession_));
			Assert::AreEqual(PM_STATUS_SUCCESS, pmMiddlewareSpeak_(hSession_, buffer));
			Assert::AreEqual("mock-middle", buffer);
			Assert::AreEqual(PM_STATUS_SUCCESS, pmCloseSession(hSession_));
			Assert::AreEqual(PM_STATUS_SESSION_NOT_OPEN, pmMiddlewareSpeak_(hSession_, buffer));
		}
		TEST_METHOD(FailUsingClosedSession)
		{
			char buffer[256]{};

			pmSetMiddlewareAsMock_(true);
			Assert::AreEqual(PM_STATUS_SUCCESS, pmOpenSession(&hSession_));
			Assert::AreEqual(PM_STATUS_SUCCESS, pmCloseSession(hSession_));
			Assert::AreEqual(PM_STATUS_SESSION_NOT_OPEN, pmMiddlewareSpeak_(hSession_, buffer));
		}
		TEST_METHOD(OpenAndCloseWithoutLeak)
		{
			pmSetMiddlewareAsMock_(true, true);
			const auto heapBefore = pmCreateHeapCheckpoint_();

			Assert::AreEqual(PM_STATUS_SUCCESS, pmOpenSession(&hSession_));
			Assert::AreEqual(PM_STATUS_SUCCESS, pmCloseSession(hSession_));

			const auto heapAfter = pmCreateHeapCheckpoint_();
			Assert::IsFalse(CrtDiffHasMemoryLeaks(heapBefore, heapAfter));
		}
		TEST_METHOD(OpenWithoutCloseCausesLeak)
		{
			pmSetMiddlewareAsMock_(true, true);
			const auto heapBefore = pmCreateHeapCheckpoint_();

			Assert::AreEqual(PM_STATUS_SUCCESS, pmOpenSession(&hSession_));

			const auto heapAfter = pmCreateHeapCheckpoint_();
			Assert::IsTrue(CrtDiffHasMemoryLeaks(heapBefore, heapAfter));
		}
		TEST_METHOD(LeaksArentDetectedWithoutMockSetting)
		{
			pmSetMiddlewareAsMock_(true, false);
			const auto heapBefore = pmCreateHeapCheckpoint_();

			Assert::AreEqual(PM_STATUS_SUCCESS, pmOpenSession(&hSession_));

			const auto heapAfter = pmCreateHeapCheckpoint_();
			Assert::IsFalse(CrtDiffHasMemoryLeaks(heapBefore, heapAfter));
		}
	};
}

namespace PresentMonAPI2Concrete
{
	TEST_CLASS(CAPISessionTests)
	{
		PM_SESSION_HANDLE hSession_;
	public:
		TEST_METHOD_CLEANUP(AfterEachTestMethod)
		{
			pmCloseSession(hSession_);
		}
		TEST_METHOD(OpenAndCloseSession)
		{
			char buffer[256]{};

			pmSetMiddlewareAsMock_(false);
			Assert::AreEqual(PM_STATUS_SUCCESS, pmOpenSession(&hSession_));
			Assert::AreEqual(PM_STATUS_SUCCESS, pmMiddlewareSpeak_(hSession_, buffer));
			Assert::AreEqual("concrete-middle", buffer);
			Assert::AreEqual(PM_STATUS_SUCCESS, pmCloseSession(hSession_));
		}
	};
}
