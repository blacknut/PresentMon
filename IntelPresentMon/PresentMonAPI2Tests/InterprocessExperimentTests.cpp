#include "CppUnitTest.h"
#include <cstring>
#include <vector>
#include <optional>
#include "BoostProcess.h"
#include "../Interprocess/source/ExperimentalInterprocess.h"
#include "../Interprocess/source/SharedMemoryTypes.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace PresentMonAPI2Mock
{
	TEST_CLASS(InterprocessExperimentTests)
	{
	public:
		TEST_METHOD(SingleProcessMakeUnique)
		{
			namespace bip = boost::interprocess;

			struct S
			{
				float f;
			};
			
			bip::managed_windows_shared_memory shm{ bip::create_only, "test-shm-chili", 0x10'0000 };

			auto p = pmon::ipc::ShmMakeUnique<S>(shm.get_segment_manager(), 420.f);

			Assert::AreEqual(420.f, p->f);
		}
		TEST_METHOD(ReadStdout)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, bp::std_out > out, bp::std_in < in);

			std::string output;
			out >> output;

			process.wait();

			Assert::AreEqual("default-output"s, output);
		}
		TEST_METHOD(ReadStdoutWithCLI)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--test-f"s, bp::std_out > out, bp::std_in < in);

			std::string output;
			out >> output;

			process.wait();

			Assert::AreEqual("inter-process-stub"s, output);
		}
		TEST_METHOD(ReadIpcStringMessage)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--basic-message"s, bp::std_out > out, bp::std_in < in);

			// write the code string to server via stdio
			in << "scooby-dooby" << std::endl;

			// wait for goahead from server via stdio
			std::string go;
			out >> go;

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("scooby-dooby-served"s, pClient->Read());

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(ReadIpcPointerMessage)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--basic-message"s, bp::std_out > out, bp::std_in < in);

			// write the code string to server via stdio
			in << "scooby-dooby" << std::endl;

			// wait for goahead from server via stdio
			std::string go;
			out >> go;

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("scooby-dooby-served"s, pClient->ReadWithPointer());

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(ReadIpcUptrMessage)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--basic-message"s, bp::std_out > out, bp::std_in < in);

			// write the code string to server via stdio
			in << "scooby-dooby" << std::endl;

			// wait for goahead from server via stdio
			std::string go;
			out >> go;

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("scooby-dooby-u-served"s, pClient->ReadWithUptr());

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(DestroyUptr)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--destroy-uptr"s, bp::std_out > out, bp::std_in < in);

			// write the code string to server via stdio
			in << "scooby-dooby" << std::endl;

			// wait for goahead from server via stdio
			std::string go;
			out >> go;

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("scooby-dooby-u-served"s, pClient->ReadWithUptr());

			// read free memory
			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before destroy uptr: {}\n", free1).c_str());

			// ack to server that read is complete via stdio, server frees uptr
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// read free memory again, should be more now
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy uptr: {}\n", free2).c_str());
			Assert::IsTrue(free2 > free1);

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(RoundtripUptr)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--make-destroy-uptr"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("dummy-served"s, pClient->ReadWithPointer());

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make uptr: {}\n", free1).c_str());

			// write the code string to server via stdio
			in << "tcooby-doiby" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// read string via shared memory
			Assert::AreEqual("tcooby-doiby-u-served"s, pClient->ReadWithUptr());

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make uptr: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// ack to server that read is complete via stdio, server frees uptr
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// read free memory again, should be more now
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy uptr: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(AllocatorStaticPolyHeapClient)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--basic-message"s, bp::std_out > out, bp::std_in < in);

			// write the code string to server via stdio
			in << "scooby-dooby" << std::endl;

			// wait for goahead from server via stdio
			std::string go;
			out >> go;

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("scooby-dooby-served"s, pClient->ReadWithPointer());

			// do target test of allocator code
			Assert::AreEqual(420, pClient->RoundtripRootInHeap());

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(AllocatorStaticPolySharedServer)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--shared-root-basic"s, bp::std_out > out, bp::std_in < in);

			// write the code string to server via stdio
			in << "scooby-dooby" << std::endl;

			// read result of allocator test
			std::string result;
			out >> result;

			// verify allocator test result
			Assert::AreEqual("69"s, result);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("scooby-dooby-served"s, pClient->ReadWithPointer());

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(RootRoundTrip)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--shared-root-retained"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("dummy-served"s, pClient->ReadWithPointer());

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make root: {}\n", free1).c_str());

			// write the code string to server via stdio
			in << "33" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make root: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared object
			Assert::AreEqual(33, pClient->ReadRoot());
			Assert::AreEqual("very-long-string-forcing-text-allocate-block-33"s, pClient->GetRootRetained().GetString());

			// ack to server that read is complete via stdio, server frees root
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// read free memory again, should be more now
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy root: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(ClientFree)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--client-free"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// read string via shared memory
			Assert::AreEqual("dummy-served"s, pClient->ReadWithPointer());

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make: {}\n", free1).c_str());

			// write the code string to server via stdio
			in << "client-free" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared objects
			Assert::AreEqual("client-free#777"s, pClient->ReadForClientFree());

			// free the object
			pClient->ClientFree();

			// read free memory again, should be restored to original state
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(ClientFreeDirectAccess)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--client-free"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			// write the code string to server via stdio
			in << "client-free" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// access and check shared objects
			Assert::AreEqual(777, pClient->GetRoot().Get());
			Assert::AreEqual("very-long-string-forcing-text-allocate-block-777"s, pClient->GetRoot().GetString());

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(DeepStructureSequence)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--deep"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make: {}\n", free1).c_str());

			// write the code values to server
			in << "1" << std::endl;
			in << "3" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared object
			const auto expected =
				"very-long-string-forcing-text-allocate-block-0| - $$ - "s
				"very-long-string-forcing-text-allocate-block-0|"s
				"very-long-string-forcing-text-allocate-block-1|"s
				"very-long-string-forcing-text-allocate-block-2|"s;
			Assert::AreEqual(expected, pClient->GetDeep().GetString());

			// ack server
			in << "ack" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory again, should be restored to original state
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// ack to server that read is complete via stdio
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(RootClone)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--shared-root-retained"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make root: {}\n", free1).c_str());

			// write the code string to server via stdio
			in << "77" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make root: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared object
			Assert::AreEqual(77, pClient->ReadRoot());
			Assert::AreEqual("very-long-string-forcing-text-allocate-block-77"s, pClient->GetRootRetained().GetString());

			// clone the shared object in local heap
			using Root = pmon::ipc::experimental::Root<std::allocator<void>>;
			auto pRoot = std::make_unique<Root>(pClient->GetRootRetained(), std::allocator<void>{});

			// ack to server that read is complete via stdio, server frees root
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// check cloned object operation
			Assert::AreEqual(77, pRoot->Get());
			Assert::AreEqual("very-long-string-forcing-text-allocate-block-77"s, pRoot->GetString());

			// read free memory again, should be restored
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy root: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// free heap structure
			pRoot.reset();

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(HeapRootCloneRoundtrip)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--clone-heap-to-shm"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make root: {}\n", free1).c_str());

			// write the code string to server via stdio
			in << "67" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make root: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared object
			Assert::AreEqual(67, pClient->ReadRoot());
			Assert::AreEqual("very-long-string-forcing-text-allocate-block-67"s, pClient->GetRootRetained().GetString());

			// clone the shared object in local heap
			using Root = pmon::ipc::experimental::Root<std::allocator<void>>;
			auto pRoot = std::make_unique<Root>(pClient->GetRootRetained(), std::allocator<void>{});

			// ack to server that read is complete via stdio, server frees root
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// check cloned object operation
			Assert::AreEqual(67, pRoot->Get());
			Assert::AreEqual("very-long-string-forcing-text-allocate-block-67"s, pRoot->GetString());

			// read free memory again, should be restored
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy root: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// free heap structure
			pRoot.reset();

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(DeepHeapRootCloneRoundtrip)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--clone-heap-deep-to-shm"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make root: {}\n", free1).c_str());

			// write the code values to server via stdio
			in << "1" << std::endl << "3" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make root: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared object
			const auto expected =
				"very-long-string-forcing-text-allocate-block-0| - $$ - "s
				"very-long-string-forcing-text-allocate-block-0|"s
				"very-long-string-forcing-text-allocate-block-1|"s
				"very-long-string-forcing-text-allocate-block-2|"s;
			Assert::AreEqual(expected, pClient->GetDeep().GetString());

			// clone the shared object in local heap
			using Root2 = pmon::ipc::experimental::Root2<std::allocator<void>>;
			auto pRoot2 = std::make_unique<Root2>(pClient->GetDeep(), std::allocator<void>{});

			// ack to server that read is complete via stdio, server frees root
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// check cloned object operation
			Assert::AreEqual(expected, pRoot2->GetString());

			// read free memory again, should be restored
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy root: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// free heap structure
			pRoot2.reset();

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
		TEST_METHOD(DeepHeapRoot2CloneRoundtrip)
		{
			namespace bp = boost::process;
			using namespace std::string_literals;

			bp::ipstream out; // Stream for reading the process's output
			bp::opstream in;  // Stream for writing to the process's input

			bp::child process("InterprocessMock.exe"s, "--clone-heap-deep-to-shm-2"s, bp::std_out > out, bp::std_in < in);

			// read goahead to connect and check mem
			std::string go;
			out >> go;

			Assert::AreEqual("go"s, go);

			// connect client
			auto pClient = pmon::ipc::experimental::IClient::Make();

			const auto free1 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory before make root: {}\n", free1).c_str());

			// write the code values to server via stdio
			in << "1" << std::endl << "3" << std::endl;

			// wait for goahead signal
			out >> go;
			Assert::AreEqual("go"s, go);

			// read free memory
			const auto free2 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after make root: {}\n", free2).c_str());
			Assert::IsTrue(free1 > free2);

			// access and check shared object
			const auto expected =
				"very-long-string-forcing-text-allocate-block-0| - $$ - "s
				"very-long-string-forcing-text-allocate-block-0|"s
				"very-long-string-forcing-text-allocate-block-1|"s
				"very-long-string-forcing-text-allocate-block-2|"s;
			Assert::AreEqual(expected, pClient->GetDeep2().GetString());

			// clone the shared object in local heap
			using Root3 = pmon::ipc::experimental::Root3<std::allocator<void>>;
			auto pRoot3 = std::make_unique<Root3>(pClient->GetDeep2(), std::allocator<void>{});

			// ack to server that read is complete via stdio, server frees root
			in << "ack" << std::endl;

			// wait for goahead from server via stdio
			out >> go;

			// check cloned object operation
			Assert::AreEqual(expected, pRoot3->GetString());

			// read free memory again, should be restored
			const auto free3 = pClient->GetFreeMemory();
			Logger::WriteMessage(std::format("Free memory after destroy root: {}\n", free3).c_str());
			Assert::IsTrue(free3 > free2);
			Assert::AreEqual(free1, free3);

			// free heap structure
			pRoot3.reset();

			// ack to server that all done, ok to exit
			in << "ack" << std::endl;

			// wait for mock process to exit
			process.wait();
		}
	};
}
