#include "CppUnitTest.h"
#include "StatusComparison.h"
#include "../PresentMonAPI2/PresentMonAPI.h"
#include "../PresentMonAPI2/Internal.h"
#include <cstring>
#include <vector>
#include <optional>
#include "Utilities.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace PresentMonAPI2Mock
{
	TEST_CLASS(CAPIStaticQueryTests)
	{
	private:
		PM_SESSION_HANDLE hSession_ = nullptr;
	public:
		TEST_METHOD_INITIALIZE(BeforeEachTestMethod)
		{
			pmSetMiddlewareAsMock_(true, true);
			pmOpenSession(&hSession_);
		}
		TEST_METHOD_CLEANUP(AfterEachTestMethod)
		{
			pmCloseSession(hSession_);
		}
		TEST_METHOD(PollStaticMetricString)
		{
			PM_QUERY_ELEMENT element{ .metric = PM_METRIC_APPLICATION, .deviceId = 0, .arrayIndex = 0 };
			auto pBlob = std::make_unique<uint8_t[]>(260);
			Assert::AreEqual(PM_STATUS_SUCCESS, pmPollStaticQuery(hSession_, &element, 4004, pBlob.get()));
			Assert::AreEqual("dota2.exe", (const char*)pBlob.get());
		}
		TEST_METHOD(FailToPollDynamicMetricAsStatic)
		{
			PM_QUERY_ELEMENT element{ .metric = PM_METRIC_CPU_UTILIZATION, .deviceId = 0, .arrayIndex = 0 };
			auto pBlob = std::make_unique<uint8_t>(8);
			Assert::AreEqual(PM_STATUS_FAILURE, pmPollStaticQuery(hSession_, &element, 4004, pBlob.get()));
		}
	};
}
