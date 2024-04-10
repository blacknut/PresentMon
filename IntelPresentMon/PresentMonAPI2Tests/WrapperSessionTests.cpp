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
	TEST_CLASS(WrapperSessionTests)
	{
	public:
		TEST_METHOD_INITIALIZE(BeforeEachTestMethod)
		{
			pmSetMiddlewareAsMock_(true, true);
		}
		TEST_METHOD(SessionRountripWithRootNoLeaks)
		{
			const auto heapBefore = pmCreateHeapCheckpoint_();

			{
				pmapi::Session session;
				auto data = session.GetIntrospectionRoot();
			}

			const auto heapAfter = pmCreateHeapCheckpoint_();
			Assert::IsFalse(CrtDiffHasMemoryLeaks(heapBefore, heapAfter));
		}
		TEST_METHOD(IntrospectSessionError)
		{
			using namespace std::string_literals;

			pmapi::Session session1;
			Assert::ExpectException<pmapi::ApiErrorException>([] {
				pmapi::Session session2;
			});
		}
	};
}
