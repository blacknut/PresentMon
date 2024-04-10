#include "CppUnitTest.h"
#include "../PresentMonAPI2/PresentMonAPI.h"
#include "../PresentMonAPI2/Internal.h"
#include <cstring>
#include <vector>
#include <optional>
#include "Utilities.h"

#include "../PresentMonAPIWrapper/PresentMonAPIWrapper.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace PresentMonAPI2Mock
{
	TEST_CLASS(WrapperRootTests)
	{
	public:
		TEST_METHOD_INITIALIZE(BeforeEachTestMethod)
		{
			pmSetMiddlewareAsMock_(true, true);
			session.emplace();
			data = session->GetIntrospectionRoot();
		}
		TEST_METHOD_CLEANUP(AfterEachTestMethod)
		{
			data.reset();
			session.reset();
		}
		TEST_METHOD(IntrospectRootRange)
		{
			using namespace std::string_literals;

			const std::vector expected{
				"PM_STATUS"s, "PM_METRIC"s, "PM_METRIC_TYPE"s, "PM_DEVICE_VENDOR"s, "PM_PRESENT_MODE"s, "PM_PSU_TYPE"s,
				"PM_UNIT"s, "PM_STAT"s, "PM_DATA_TYPE"s, "PM_GRAPHICS_RUNTIME"s, "PM_DEVICE_TYPE"s, "PM_METRIC_AVAILABILITY"s
			};
			auto e = expected.begin();
			for (auto ev : data->GetEnums()) {
				Assert::AreEqual(*e, ev.GetSymbol());
				e++;
			}
		}
		TEST_METHOD(IntrospectViewRange)
		{
			using namespace std::string_literals;

			const std::vector expected{
				"PM_STATUS_SUCCESS"s,
				"PM_STATUS_SERVICE_ERROR"s,
				"PM_STATUS_INVALID_ETL_FILE"s,
				"PM_STATUS_DATA_LOSS"s,
				"PM_STATUS_NO_DATA"s,
				"PM_STATUS_INVALID_PID"s,
				"PM_STATUS_STREAM_ALREADY_EXISTS"s,
				"PM_STATUS_UNABLE_TO_CREATE_NSM"s,
				"PM_STATUS_INVALID_ADAPTER_ID"s,
				"PM_STATUS_OUT_OF_RANGE"s,
				"PM_STATUS_INSUFFICIENT_BUFFER"s,
				"PM_STATUS_FAILURE"s,
				"PM_STATUS_SESSION_NOT_OPEN"s,
			};
			auto e = expected.begin();
			for (auto kv : data->GetEnums().begin()->GetKeys()) {
				Assert::AreEqual(*e, kv.GetSymbol());
				e++;
			}
		}
		TEST_METHOD(IntrospectMetricToEnumKey)
		{
			using namespace std::string_literals;
			Assert::AreEqual("Presented FPS"s, data->GetMetrics().begin()[1].Introspect().GetName());
		}
		TEST_METHOD(IntrospectMetricUnit)
		{
			using namespace std::string_literals;
			Assert::AreEqual("fps"s, data->GetMetrics().begin()[1].IntrospectUnit().GetShortName());
		}
		TEST_METHOD(IntrospectMetricStats)
		{
			using namespace std::string_literals;
			Assert::AreEqual("Average"s, data->GetMetrics().begin()[1].GetStatInfo().begin()->IntrospectStat()->GetName());
			Assert::AreEqual(7ull, data->GetMetrics().begin()[1].GetStatInfo().size());
		}
		TEST_METHOD(IntrospectMetricStatsWithLookup)
		{
			using namespace std::string_literals;
			Assert::AreEqual("avg"s, data->GetMetrics().begin()[1].GetStatInfo().begin()->IntrospectStat()->GetShortName());
		}
		TEST_METHOD(IntrospectMetricDataTypeEnum)
		{
			using namespace std::string_literals;
			auto metric = data->FindMetric(PM_METRIC_PRESENT_MODE);
			auto type = metric.GetDataTypeInfo();
			Assert::AreEqual("Present Mode"s, metric.Introspect().GetName());
			Assert::AreEqual((int)PM_DATA_TYPE_ENUM, (int)type.GetPolledType());
			Assert::AreEqual((int)PM_DATA_TYPE_ENUM, (int)type.GetFrameType());
			Assert::AreEqual("PM_PRESENT_MODE"s, type.IntrospectEnum().GetSymbol());
		}
		TEST_METHOD(IntrospectMetricDataTypeDivergent)
		{
			using namespace std::string_literals;
			auto metric = data->FindMetric(PM_METRIC_GPU_POWER_LIMITED);
			auto type = metric.GetDataTypeInfo();
			Assert::AreEqual("GPU Power Limited"s, metric.Introspect().GetName());
			Assert::AreEqual((int)PM_DATA_TYPE_DOUBLE, (int)type.GetPolledType());
			Assert::AreEqual((int)PM_DATA_TYPE_BOOL, (int)type.GetFrameType());
		}
		TEST_METHOD(IntrospectMetricDeviceMetricInfo)
		{
			using namespace std::string_literals;
			auto metric = data->FindMetric(PM_METRIC_GPU_FAN_SPEED);
			auto deviceInfos = metric.GetDeviceMetricInfo();
			Assert::AreEqual(2ull, deviceInfos.size());
			{
				auto deviceInfo = deviceInfos.begin()[0];
				Assert::AreEqual((int)PM_METRIC_AVAILABILITY_AVAILABLE, (int)deviceInfo.GetAvailability());
				Assert::AreEqual(1u, deviceInfo.GetArraySize());
			}
			{
				auto deviceInfo = deviceInfos.begin()[1];
				Assert::AreEqual((int)PM_METRIC_AVAILABILITY_AVAILABLE, (int)deviceInfo.GetAvailability());
				Assert::AreEqual(2u, deviceInfo.GetArraySize());
			}
		}
		TEST_METHOD(IntrospectDevices)
		{
			using namespace std::string_literals;
			auto devices = data->GetDevices();
			Assert::AreEqual(3ull, devices.size());
			{
				auto device = devices.begin()[0];
				Assert::AreEqual("Unknown"s, device.IntrospectVendor().GetName());
				Assert::AreEqual("Device-independent"s, device.GetName());
				Assert::AreEqual("Device Independent"s, device.IntrospectType().GetName());
				Assert::AreEqual(0u, device.GetId());
			}
			{
				auto device = devices.begin()[1];
				Assert::AreEqual("Intel"s, device.IntrospectVendor().GetName());
				Assert::AreEqual("Arc 750"s, device.GetName());
				Assert::AreEqual("Graphics Adapter"s, device.IntrospectType().GetName());
				Assert::AreEqual(1u, device.GetId());
			}
			{
				auto device = devices.begin()[2];
				Assert::AreEqual("NVIDIA"s, device.IntrospectVendor().GetName());
				Assert::AreEqual("GeForce RTX 2080 ti"s, device.GetName());
				Assert::AreEqual("Graphics Adapter"s, device.IntrospectType().GetName());
				Assert::AreEqual(2u, device.GetId());
			}
		}
		TEST_METHOD(IntrospectLookupDevice)
		{
			using namespace std::string_literals;
			{
				auto device = data->FindDevice(1);
				Assert::AreEqual("Intel"s, device.IntrospectVendor().GetName());
				Assert::AreEqual("Arc 750"s, device.GetName());
				Assert::AreEqual("Graphics Adapter"s, device.IntrospectType().GetName());
				Assert::AreEqual(1u, device.GetId());
			}
		}
		TEST_METHOD(IntrospectMetricDeviceMetricInfoLookupDevice)
		{
			using namespace std::string_literals;
			auto metric = data->FindMetric(PM_METRIC_GPU_FAN_SPEED);
			auto deviceInfos = metric.GetDeviceMetricInfo();
			Assert::AreEqual(2ull, deviceInfos.size());
			{
				auto deviceInfo = deviceInfos.begin()[1];
				Assert::AreEqual((int)PM_METRIC_AVAILABILITY_AVAILABLE, (int)deviceInfo.GetAvailability());
				Assert::IsTrue(deviceInfo.IsAvailable());
				Assert::AreEqual(2u, deviceInfo.GetArraySize());
				Assert::AreEqual("NVIDIA"s, deviceInfo.GetDevice().IntrospectVendor().GetName());
			}
		}
		TEST_METHOD(IntrospectMetricLookupError)
		{
			using namespace std::string_literals;
			Assert::ExpectException<pmapi::LookupException>([this] {
				data->FindMetric((PM_METRIC)420);
			});
		}
		TEST_METHOD(IntrospectMetricType)
		{
			using namespace std::string_literals;

			Assert::AreEqual("Dynamic and Frame Event Metric"s, data->FindMetric(PM_METRIC_CPU_UTILIZATION).IntrospectType().GetName());
			Assert::AreEqual("Static Metric"s, data->FindMetric(PM_METRIC_APPLICATION).IntrospectType().GetName());
		}
		TEST_METHOD(IntrospectStat)
		{
			using namespace std::string_literals;

			auto shortName = data->FindEnumKey(PM_ENUM_STAT, PM_STAT_AVG).GetShortName();

			Assert::AreEqual("avg"s, shortName);
		}
		TEST_METHOD(IntrospectPreferredUnit)
		{
			using namespace std::string_literals;

			auto metric = data->FindMetric(PM_METRIC_GPU_MEM_SIZE);
			Assert::AreEqual((int)PM_UNIT_BYTES, (int)metric.GetUnit());
			Assert::AreEqual((int)PM_UNIT_GIGABYTES, (int)metric.GetPreferredUnitHint());
		}
		TEST_METHOD(IntrospectUnitUpConversion)
		{
			using namespace std::string_literals;

			Assert::AreEqual(0.000'001, data->FindUnit(PM_UNIT_HERTZ).MakeConversionFactor(PM_UNIT_MEGAHERTZ));
		}
	private:
		std::optional<pmapi::Session> session;
		std::shared_ptr<pmapi::intro::Root> data;
	};
}
