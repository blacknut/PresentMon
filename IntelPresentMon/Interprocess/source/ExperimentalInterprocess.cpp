#include "ExperimentalInterprocess.h"
#include <memory>

namespace pmon::ipc::experimental
{
	class Server : public IServer
	{
	public:
		Server(std::string code)
		{
			// construct string in shm
			auto allocator = shm.get_allocator<char>();
			auto pMessage = shm.construct<ShmString>(MessageStringName)(allocator);
			*pMessage = (code + "-served").c_str();
			// construct ptr to string in shm
			shm.construct<bip::offset_ptr<ShmString>>(MessagePtrName)(pMessage);
		}
		void FreeUptrToMessage() override
		{
			pupMessage.reset();
		}
		void MakeUptrToMessage(std::string code) override
		{
			// construct named uptr in shm to anonymous string in shm, and own shm uptr via uptr in heap
			pupMessage = Uptr<Uptr<ShmString>>{ shm.construct<Uptr<ShmString>>(MessageUptrName)(
				shm.construct<ShmString>(bip::anonymous_instance)(shm.get_segment_manager()),
				UptrDeleter<ShmString>{ shm.get_segment_manager() }
			), UptrDeleter<Uptr<ShmString>>{ shm.get_segment_manager() } };
			**pupMessage = (code + "-u-served").c_str();
		}
		int RoundtripRootInShared() override
		{			
			auto allocator = shm.get_allocator<std::string>();
			const auto pRoot = shm.construct<Root<decltype(allocator)>>(bip::anonymous_instance)(69, std::move(allocator));
			const auto ret = pRoot->Get();
			shm.destroy_ptr(pRoot);
			return ret;
		}
		void MakeRoot(int x) override
		{
			pRoot = { 
				shm.construct<Root<ShmAllocator<void>>>(RootPtrName)(x, shm.get_allocator<void>()),
				UptrDeleter<Root<ShmAllocator<void>>>{ shm.get_segment_manager() }
			};
		}
		void MakeRootCloneHeap(int x) override
		{
			using A = std::allocator<void>;
			// why MakeUnique isn't working here?
			auto pRootHeap = std::make_unique<Root<A>>(x, A{});
			pRoot = {
				shm.construct<Root<ShmAllocator<void>>>(RootPtrName)(*pRootHeap, shm.get_allocator<void>()),
				UptrDeleter<Root<ShmAllocator<void>>>{ shm.get_segment_manager() }
			};
		}
		void FreeRoot() override
		{
			pRoot.reset();
		}
		void CreateForClientFree(int x, std::string s) override
		{
			auto allo = shm.get_allocator<void>();
			shm.construct<Root<ShmAllocator<void>>>(ClientFreeRoot)(x, allo);
			**shm.construct<Uptr<ShmString>>(ClientFreeUptrString)(
				shm.construct<ShmString>(bip::anonymous_instance)(allo),
				UptrDeleter<ShmString>{ shm.get_segment_manager() }
			) = s.c_str();
		}
		void MakeDeep(int n1, int n2) override
		{
			shm.construct<Root2<ShmAllocator<void>>>(DeepRoot)(n1, n2, shm.get_allocator<void>());
		}
		void MakeDeepCloneHeap(int n1, int n2) override
		{
			using A = std::allocator<void>;
			// why MakeUnique isn't working here?
			auto pRoot2Heap = std::make_unique<Root2<A>>(n1, n2, A{});
			auto s = pRoot2Heap->GetString();
			shm.construct<Root2<ShmAllocator<void>>>(DeepRoot)(*pRoot2Heap, shm.get_allocator<void>());
		}
		void FreeDeep() override
		{
			shm.destroy<Root2<ShmAllocator<void>>>(DeepRoot);
		}
		void MakeDeepCloneHeap2(int n1, int n2) override
		{
			using A = std::allocator<void>;
			// why MakeUnique isn't working here?
			auto pRoot3Heap = std::make_unique<Root3<A>>(n1, n2, A{});
			auto s = pRoot3Heap->GetString();
			shm.construct<Root3<ShmAllocator<void>>>(DeepRoot2)(*pRoot3Heap, shm.get_allocator<void>());
		}
		void FreeDeep2() override
		{
			shm.destroy<Root3<ShmAllocator<void>>>(DeepRoot2);
		}
	private:
		ShmSegment shm{ bip::create_only, SharedMemoryName, 0x10'0000 };
		Uptr<Uptr<ShmString>> pupMessage{ nullptr, UptrDeleter<Uptr<ShmString>>{ nullptr } };
		Uptr<Root<ShmAllocator<void>>> pRoot{ nullptr, UptrDeleter<Root<ShmAllocator<void>>>{ nullptr } };
	};

	std::unique_ptr<IServer> IServer::Make(std::string code)
	{
		return std::make_unique<Server>(std::move(code));
	}

	class Client : public IClient
	{
	public:
		Client()
		{
			pMessage = shm.find<ShmString>(IServer::MessageStringName).first;
			ppMessage = shm.find<bip::offset_ptr<ShmString>>(IServer::MessagePtrName).first;
		}
		std::string Read() const override
		{
			return pMessage->c_str();
		}
		std::string ReadWithPointer() const override
		{
			return ppMessage->get()->c_str();
		}
		std::string ReadWithUptr() override
		{
			auto pup = shm.find<Uptr<ShmString>>(IServer::MessageUptrName).first;
			return pup->get()->c_str();
		}
		size_t GetFreeMemory() const override
		{
			return shm.get_free_memory();
		}
		int RoundtripRootInHeap() const override
		{
			const Root r{ 420, std::allocator<char>{} };
			return r.Get();
		}
		int ReadRoot() override
		{
			auto pair = shm.find<Root<ShmAllocator<void>>>(IServer::RootPtrName);
			auto ptr = pair.first;
			return ptr->Get();
		}
		std::string ReadForClientFree() override
		{
			std::string s = shm.find<Uptr<ShmString>>(IServer::ClientFreeUptrString).first->get()->c_str();
			return s + "#" + std::to_string(shm.find<Root<ShmAllocator<void>>>(IServer::ClientFreeRoot).first->Get());
		}
		void ClientFree() override
		{
			shm.destroy<Root<ShmAllocator<void>>>(IServer::ClientFreeRoot);
			shm.destroy<Uptr<ShmString>>(IServer::ClientFreeUptrString);
		}
		Root<ShmAllocator<void>>& GetRoot() override
		{
			return *shm.find<Root<ShmAllocator<void>>>(IServer::ClientFreeRoot).first;
		}
		Root<ShmAllocator<void>>& GetRootRetained() override
		{
			return *shm.find<Root<ShmAllocator<void>>>(IServer::RootPtrName).first;
		}
		Root2<ShmAllocator<void>>& GetDeep()
		{
			return *shm.find<Root2<ShmAllocator<void>>>(IServer::DeepRoot).first;
		}
		Root3<ShmAllocator<void>>& GetDeep2()
		{
			return *shm.find<Root3<ShmAllocator<void>>>(IServer::DeepRoot2).first;
		}
	private:
		bip::managed_windows_shared_memory shm{ bip::open_only, IServer::SharedMemoryName };
		const ShmString* pMessage = nullptr;
		const bip::offset_ptr<ShmString>* ppMessage = nullptr;
	};

	std::unique_ptr<IClient> IClient::Make()
	{
		return std::make_unique<Client>();
	}


	const char* f()
	{
		return "inter-process-stub";
	}
}