#include "CppUnitTest.h"
#include "StatusComparison.h"
#include "BoostProcess.h"
#include "../PresentMonAPI2/Internal.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace EndToEndTests
{
	TEST_CLASS(CAPIToServiceTests)
	{
		std::optional<boost::process::child> oChild;
	public:
		TEST_METHOD_CLEANUP(Cleanup)
		{
			if (oChild) {
				oChild->terminate();
				oChild->wait();
				oChild.reset();
			}
		}
		TEST_METHOD(ConnectDisconnectToServicePipe)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;
			using namespace std::chrono_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			const auto pipeName = R"(\\.\pipe\test-pipe-pmsvc-2)"s;
			const auto introName = "PM_intro_test_nsm_2"s;

			oChild.emplace("PresentMonService.exe"s,
				"--timed-stop"s, "4000"s,
				"--control-pipe"s, pipeName.c_str(),
				"--nsm-prefix"s, "pmon_nsm_utest_"s,
				"--intro-nsm"s, introName,
				bp::std_out > out, bp::std_in < in);

			std::this_thread::sleep_for(10ms);

			PM_SESSION_HANDLE hSession = nullptr;

			{
				const auto sta = pmOpenSession_(&hSession, pipeName.c_str(), introName.c_str());
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta), L"*** Connecting to service via named pipe");
			}
			{
				const auto sta = pmCloseSession(hSession);
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta));
			}
		}
		TEST_METHOD(AcquireIntrospectionData)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;
			using namespace std::chrono_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			const auto pipeName = R"(\\.\pipe\test-pipe-pmsvc-2)"s;
			const auto introName = "PM_intro_test_nsm_2"s;

			oChild.emplace("PresentMonService.exe"s,
				"--timed-stop"s, "4000"s,
				"--control-pipe"s, pipeName,
				"--nsm-prefix"s, "pmon_nsm_utest_"s,
				"--intro-nsm"s, introName,
				bp::std_out > out, bp::std_in < in);

			std::this_thread::sleep_for(10ms);

			PM_SESSION_HANDLE hSession = nullptr;

			{
				const auto sta = pmOpenSession_(&hSession, pipeName.c_str(), introName.c_str());
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta), L"*** Connecting to service via named pipe");
			}

			{
				const PM_INTROSPECTION_ROOT* pRoot = nullptr;
				const auto sta = pmGetIntrospectionRoot(hSession, &pRoot);
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta));

				Assert::AreEqual(PM_STATUS_SUCCESS, pmFreeIntrospectionRoot(pRoot));
			}

			{
				const auto sta = pmCloseSession(hSession);
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta));
			}
		}
		TEST_METHOD(InspectIntrospectionData)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;
			using namespace std::chrono_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			const auto pipeName = R"(\\.\pipe\test-pipe-pmsvc-2)"s;
			const auto introName = "PM_intro_test_nsm_2"s;

			oChild.emplace("PresentMonService.exe"s,
				"--timed-stop"s, "4000"s,
				"--control-pipe"s, pipeName,
				"--nsm-prefix"s, "pmon_nsm_utest_"s,
				"--intro-nsm"s, introName,
				bp::std_out > out, bp::std_in < in);

			std::this_thread::sleep_for(10ms);

			PM_SESSION_HANDLE hSession = nullptr;

			{
				const auto sta = pmOpenSession_(&hSession, pipeName.c_str(), introName.c_str());
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta), L"*** Connecting to service via named pipe");
			}

			{
				const PM_INTROSPECTION_ROOT* pRoot = nullptr;
				const auto sta = pmGetIntrospectionRoot(hSession, &pRoot);
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta));

				Assert::IsNotNull(pRoot);
				Assert::AreEqual(12ull, pRoot->pEnums->size);
				Assert::AreEqual(68ull, pRoot->pMetrics->size);

				// checking 7th enum (unit)
				{
					auto pEnum = static_cast<const PM_INTROSPECTION_ENUM*>(pRoot->pEnums->pData[6]);
					Assert::IsNotNull(pEnum);
					Assert::AreEqual((int)PM_ENUM_UNIT, (int)pEnum->id);
					Assert::AreEqual("PM_UNIT", pEnum->pSymbol->pData);
					Assert::AreEqual("List of all units of measure used for metrics", pEnum->pDescription->pData);
					Assert::AreEqual(15ull, pEnum->pKeys->size);
					// 1st key
					{
						auto pKey = static_cast<const PM_INTROSPECTION_ENUM_KEY*>(pEnum->pKeys->pData[0]);
						Assert::IsNotNull(pKey);
						Assert::IsNotNull(pKey->pSymbol);
						Assert::AreEqual("PM_UNIT_DIMENSIONLESS", pKey->pSymbol->pData);
						Assert::AreEqual("Dimensionless", pKey->pName->pData);
						Assert::AreEqual("", pKey->pShortName->pData);
						Assert::AreEqual((int)PM_ENUM_UNIT, (int)pKey->enumId);
						Assert::AreEqual((int)PM_UNIT_DIMENSIONLESS, pKey->id);
					}
					// 5th key
					{
						auto pKey = static_cast<const PM_INTROSPECTION_ENUM_KEY*>(pEnum->pKeys->pData[5]);
						Assert::IsNotNull(pKey);
						Assert::IsNotNull(pKey->pSymbol);
						Assert::AreEqual("PM_UNIT_PERCENT", pKey->pSymbol->pData);
						Assert::AreEqual("Percent", pKey->pName->pData);
						Assert::AreEqual("%", pKey->pShortName->pData);
						Assert::AreEqual((int)PM_ENUM_UNIT, (int)pKey->enumId);
						Assert::AreEqual((int)PM_UNIT_PERCENT, pKey->id);
					}
				}

				// check device
				{
					auto pDevice = static_cast<const PM_INTROSPECTION_DEVICE*>(pRoot->pDevices->pData[0]);
					Assert::IsNotNull(pDevice);
					Assert::AreEqual(0, (int)pDevice->id);
					Assert::AreEqual((int)PM_DEVICE_TYPE_INDEPENDENT, (int)pDevice->type);
					Assert::AreEqual((int)PM_DEVICE_VENDOR_UNKNOWN, (int)pDevice->vendor);
					Assert::AreEqual("Device-independent", pDevice->pName->pData);
				}

				// check metric 2nd
				{
					auto pMetric = static_cast<const PM_INTROSPECTION_METRIC*>(pRoot->pMetrics->pData[1]);
					Assert::IsNotNull(pMetric);
					Assert::AreEqual((int)PM_METRIC_DISPLAYED_FPS, (int)pMetric->id);
					Assert::AreEqual((int)PM_UNIT_FPS, (int)pMetric->unit);
					Assert::AreEqual((int)PM_DATA_TYPE_DOUBLE, (int)pMetric->pTypeInfo->polledType);
					Assert::AreEqual(7ull, pMetric->pStatInfo->size);
					// check 1st stat
					{
						auto pStatInfo = static_cast<const PM_INTROSPECTION_STAT_INFO*>(pMetric->pStatInfo->pData[0]);
						Assert::AreEqual((int)PM_STAT_AVG, (int)pStatInfo->stat);
					}
					// check device info
					Assert::AreEqual(1ull, pMetric->pDeviceMetricInfo->size);
					{
						auto pInfo = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(pMetric->pDeviceMetricInfo->pData[0]);
						Assert::AreEqual(0u, pInfo->deviceId);
						Assert::AreEqual(1u, pInfo->arraySize);
						Assert::AreEqual((int)PM_METRIC_AVAILABILITY_AVAILABLE, (int)pInfo->availability);
					}
				}

				// check metric gpu array 2 device (fan)
				{
					auto pMetric = static_cast<const PM_INTROSPECTION_METRIC*>(pRoot->pMetrics->pData[22]);
					Assert::IsNotNull(pMetric);
					Assert::AreEqual((int)PM_METRIC_GPU_FAN_SPEED, (int)pMetric->id);
					Assert::AreEqual((int)PM_UNIT_RPM, (int)pMetric->unit);
					Assert::AreEqual((int)PM_DATA_TYPE_DOUBLE, (int)pMetric->pTypeInfo->polledType);
					Assert::AreEqual(7ull, pMetric->pStatInfo->size);
					// check 7th stat
					{
						auto pStatInfo = static_cast<const PM_INTROSPECTION_STAT_INFO*>(pMetric->pStatInfo->pData[6]);
						Assert::AreEqual((int)PM_STAT_MID_POINT, (int)pStatInfo->stat);
					}
				}

				Assert::AreEqual(PM_STATUS_SUCCESS, pmFreeIntrospectionRoot(pRoot));
			}

			{
				const auto sta = pmCloseSession(hSession);
				Assert::AreEqual(int(PM_STATUS_SUCCESS), int(sta));
			}
		}
	};
}