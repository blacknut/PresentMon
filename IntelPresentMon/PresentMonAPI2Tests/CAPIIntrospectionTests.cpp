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
	TEST_CLASS(CAPIIntrospectionTests)
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
		TEST_METHOD(FreeIntrospectionTree)
		{
			const auto heapBefore = pmCreateHeapCheckpoint_();

			const PM_INTROSPECTION_ROOT* pRoot{};
			Assert::AreEqual(PM_STATUS_SUCCESS, pmGetIntrospectionRoot(hSession_, &pRoot));
			Assert::IsNotNull(pRoot);

			Assert::AreEqual(PM_STATUS_SUCCESS, pmFreeIntrospectionRoot(pRoot));

			const auto heapAfter = pmCreateHeapCheckpoint_();
			Assert::IsFalse(CrtDiffHasMemoryLeaks(heapBefore, heapAfter));
		}
		TEST_METHOD(LeakIntrospectionTree)
		{
			const auto heapBefore = pmCreateHeapCheckpoint_();

			const PM_INTROSPECTION_ROOT* pRoot{};
			Assert::AreEqual(PM_STATUS_SUCCESS, pmGetIntrospectionRoot(hSession_, &pRoot));
			Assert::IsNotNull(pRoot);

			// normally we would free the linked structure here via its root
			// Assert::AreEqual(PM_STATUS_SUCCESS, pmFreeIntrospectionRoot(pRoot));

			const auto heapAfter = pmCreateHeapCheckpoint_();
			Assert::IsTrue(CrtDiffHasMemoryLeaks(heapBefore, heapAfter));
		}
		TEST_METHOD(Introspect)
		{
			// introspection query
			const PM_INTROSPECTION_ROOT* pRoot{};
			Assert::AreEqual(PM_STATUS_SUCCESS, pmGetIntrospectionRoot(hSession_, &pRoot));
			Assert::IsNotNull(pRoot);
			Assert::AreEqual(12ull, pRoot->pEnums->size);
			Assert::AreEqual(69ull, pRoot->pMetrics->size);
			Assert::AreEqual(3ull, pRoot->pDevices->size);

			// checking 7th enum (unit)
			{
				auto pEnum = static_cast<const PM_INTROSPECTION_ENUM*>(pRoot->pEnums->pData[6]);
				Assert::IsNotNull(pEnum);
				Assert::AreEqual((int)PM_ENUM_UNIT, (int)pEnum->id);
				Assert::AreEqual("PM_UNIT", pEnum->pSymbol->pData);
				Assert::AreEqual("List of all units of measure used for metrics", pEnum->pDescription->pData);
				Assert::AreEqual(31ull, pEnum->pKeys->size);
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
					auto pKey = static_cast<const PM_INTROSPECTION_ENUM_KEY*>(pEnum->pKeys->pData[3]);
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
				auto pMetric = static_cast<const PM_INTROSPECTION_METRIC*>(pRoot->pMetrics->pData[0]);
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
				// check device infos
				Assert::AreEqual(2ull, pMetric->pDeviceMetricInfo->size);
				{
					auto pInfo = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(pMetric->pDeviceMetricInfo->pData[0]);
					Assert::AreEqual(1u, pInfo->deviceId);
					Assert::AreEqual(1u, pInfo->arraySize);
					Assert::AreEqual((int)PM_METRIC_AVAILABILITY_AVAILABLE, (int)pInfo->availability);
				}
				{
					auto pInfo = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(pMetric->pDeviceMetricInfo->pData[1]);
					Assert::AreEqual(2u, pInfo->deviceId);
					Assert::AreEqual(2u, pInfo->arraySize);
					Assert::AreEqual((int)PM_METRIC_AVAILABILITY_AVAILABLE, (int)pInfo->availability);
				}
			}

			Assert::AreEqual(PM_STATUS_SUCCESS, pmFreeIntrospectionRoot(pRoot));
		}
	};
}
