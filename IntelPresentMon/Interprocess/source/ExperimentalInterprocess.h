#pragma once
#include <string>
#include <memory>
#include <boost/interprocess/managed_windows_shared_memory.hpp>
#include <boost/interprocess/containers/string.hpp>
#include <boost/interprocess/containers/vector.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/interprocess/smart_ptr/unique_ptr.hpp>
#include <boost/swap.hpp>

namespace pmon::ipc::experimental
{
	namespace bip = boost::interprocess;
	using bip::ipcdetail::to_raw_pointer;


	using ShmSegment = bip::managed_windows_shared_memory;
	using ShmSegmentManager = ShmSegment::segment_manager;
	template<typename T>
	using ShmAllocator = ShmSegmentManager::allocator<T>::type;
	using ShmString = bip::basic_string<char, std::char_traits<char>, ShmAllocator<char>>;
	template<typename T>
	using UptrDeleter = bip::deleter<T, ShmSegmentManager>;
	template<typename T>
	using Uptr = bip::unique_ptr<T, UptrDeleter<T>>;
	template<class A>
	using AllocString = bip::basic_string<char, std::char_traits<char>, A>;
	template<class T, class A>
	using AllocVector = bip::vector<T, A>;


	// <A> is allocator already adapted to handling the type T its meant to handle
	template<class A>
	struct AllocatorDeleter
	{
		using T = typename A::value_type;
		using pointer = std::allocator_traits<A>::pointer;
		AllocatorDeleter(A allocator_) : allocator{ std::move(allocator_) } {}
		AllocatorDeleter(const AllocatorDeleter& rhs) noexcept
			:
			allocator{ rhs.allocator }
		{}
		AllocatorDeleter& operator=(AllocatorDeleter&& rhs) noexcept
		{
			// TODO: debug check for allocator compatibility (same memory segment)
			boost::swap(allocator, rhs.allocator);
			return *this;
		}
		void operator()(pointer ptr)
		{
			std::allocator_traits<A>::destroy(allocator, to_raw_pointer(ptr));
			allocator.deallocate(to_raw_pointer(ptr), 1u);
		}
	private:
		A allocator;
	};

	template<template <class> typename T, class A>
	using UptrT = bip::unique_ptr<T<A>, AllocatorDeleter<typename T<A>::Allocator>>;

	template<template <class> typename T, class A, typename...P>
	auto MakeUnique(A allocator_in, P&&...args)
	{
		// allocator type for creating the object to be managed by the uptr
		using Allocator = typename T<A>::Allocator;
		// deleter type for the uptr (derived from the allocator of the managed object)
		using Deleter = typename UptrT<T, A>::deleter_type;
		// create allocator for managed object
		Allocator allocator(std::move(allocator_in));
		// allocate memory for managed object (uninitialized)
		auto ptr = allocator.allocate(1);
		// construct object in allocated memory
		std::allocator_traits<Allocator>::construct(allocator, to_raw_pointer(ptr), std::forward<P>(args)..., allocator);
		// construct uptr and deleter
		return UptrT<T, A>(to_raw_pointer(ptr), Deleter(allocator));
	}

	template<class A>
	class Branch
	{
		template<typename A2> friend class Branch;
	public:
		// allocator type for this instance, based on the allocator type used to template
		using Allocator = std::allocator_traits<A>::template rebind_alloc<Branch>;
		Branch(int x_, A allocator) : x{ x_ }, str{ allocator }
		{
			str = "very-long-string-forcing-text-allocate-block-";
			str.append(std::to_string(x).c_str());
		}
		template<class A2>
		Branch(const Branch<A2>& other, A allocator)
			:
			Branch{ other.x, std::move(allocator) }
		{}
		int Get() const { return x; }
		std::string GetString() const { return str.c_str(); }
	private:
		using CharAllocator = std::allocator_traits<A>::template rebind_alloc<char>;
		int x;
		AllocString<CharAllocator> str;
	};

	template<class A>
	class Root
	{
		template<typename A2>
		friend class Root;
	public:
		Root(int x, A allocator)
			:
			pBranch{ MakeUnique<Branch>(allocator, x) }
		{}
		template<class A2>
		Root(const Root<A2>& other, A allocator)
			:
			pBranch{ MakeUnique<Branch>(allocator, *other.pBranch) }
		{}
		int Get() const { return pBranch->Get(); }
		std::string GetString() const { return pBranch->GetString(); }
	private:
		UptrT<Branch, A> pBranch;
	};

	template<class A>
	class Leaf2
	{
		template<typename A2>
		friend class Leaf2;
	public:
		// allocator type for this instance, based on the allocator type used to template
		using Allocator = std::allocator_traits<A>::template rebind_alloc<Leaf2>;
		Leaf2(int x, A allocator) : str{ allocator }
		{
			str = "very-long-string-forcing-text-allocate-block-";
			str.append(std::to_string(x).c_str());
		}
		template<class A2>
		Leaf2(const Leaf2<A2>& other, A allocator)
			:
			str{ other.str.begin(), other.str.end(), allocator }
		{}
		std::string GetString() const { return str.c_str(); }
	private:
		using CharAllocator = std::allocator_traits<A>::template rebind_alloc<char>;
		AllocString<CharAllocator> str;
	};

	template<class A>
	class Branch2
	{
		template<typename A2>
		friend class Branch2;
	public:
		// allocator type for this instance, based on the allocator type used to template
		using Allocator = std::allocator_traits<A>::template rebind_alloc<Branch2>;
		Branch2(int n, A allocator) : leafPtrs{ allocator }
		{
			leafPtrs.reserve(n);
			for (int i = 0; i < n; i++) {
				leafPtrs.push_back(MakeUnique<Leaf2, A>(allocator, i));
			}
		}
		template<class A2>
		Branch2(const Branch2<A2>& other, A allocator)
			:
			leafPtrs{ allocator }
		{
			leafPtrs.reserve(other.leafPtrs.size());
			for (auto& pLeaf : other.leafPtrs) {
				leafPtrs.push_back(MakeUnique<Leaf2>(allocator, *pLeaf));
			}
		}
		std::string GetString() const
		{
			std::string s;
			for (auto& l : leafPtrs) {
				s += l->GetString() + "|";
			}
			return s;
		}
	private:
		using LeafPtrAllocator = std::allocator_traits<A>::template rebind_alloc<UptrT<Leaf2, A>>;
		AllocVector<UptrT<Leaf2, A>, LeafPtrAllocator> leafPtrs;
	};

	template<class A>
	class Root2
	{
		template<typename A2>
		friend class Root2;
	public:
		Root2(int n1, int n2, A allocator)
			:
			pBranch1{ MakeUnique<Branch2>(allocator, n1) },
			pBranch2{ MakeUnique<Branch2>(allocator, n2) }
		{}
		template<class A2>
		Root2(const Root2<A2>&other, A allocator)
			:
			pBranch1{ MakeUnique<Branch2>(allocator, *other.pBranch1) },
			pBranch2{ MakeUnique<Branch2>(allocator, *other.pBranch2) }
		{}
		std::string GetString() const
		{
			return pBranch1->GetString() + " - $$ - " + pBranch2->GetString();
		}

	private:
		UptrT<Branch2, A> pBranch1;
		UptrT<Branch2, A> pBranch2;
	};

	template<class A>
	class Branch3
	{
		template<typename A2>
		friend class Branch3;
	public:
		// allocator type for this instance, based on the allocator type used to template
		using Allocator = std::allocator_traits<A>::template rebind_alloc<Branch3>;
		Branch3(int n, A allocator) : leafs{ allocator }
		{
			leafs.reserve(n);
			for (int i = 0; i < n; i++) {
				leafs.emplace_back(i, allocator);
			}
		}
		template<class A2>
		Branch3(const Branch3<A2>& other, A allocator) : leafs{ allocator }
		{
			leafs.reserve(other.leafs.size());
			for (auto& l : other.leafs) {
				leafs.emplace_back(l, allocator);
			}
		}
		std::string GetString() const
		{
			std::string s;
			for (auto& l : leafs) {
				s += l.GetString() + "|";
			}
			return s;
		}
	private:
		using LeafAllocator = std::allocator_traits<A>::template rebind_alloc<Leaf2<A>>;
		AllocVector<Leaf2<A>, LeafAllocator> leafs;
	};

	template<class A>
	class Root3
	{
		template<typename A2>
		friend class Root3;
	public:
		Root3(int n1, int n2, A allocator)
			:
			pBranch1{ MakeUnique<Branch3>(allocator, n1) },
			pBranch2{ MakeUnique<Branch3>(allocator, n2) }
		{}
		template<class A2>
		Root3(const Root3<A2>& other, A allocator)
			:
			pBranch1{ MakeUnique<Branch3>(allocator, *other.pBranch1) },
			pBranch2{ MakeUnique<Branch3>(allocator, *other.pBranch2) }
		{}
		std::string GetString() const
		{
			return pBranch1->GetString() + " - $$ - " + pBranch2->GetString();
		}

	private:
		UptrT<Branch3, A> pBranch1;
		UptrT<Branch3, A> pBranch2;
	};

	class IServer
	{
	public:
		virtual ~IServer() = default;
		static constexpr const char* SharedMemoryName = "MySharedMemory-42069";
		static constexpr const char* MessageStringName = "message-string-777";
		static constexpr const char* MessagePtrName = "message-ptr-787";
		static constexpr const char* MessageUptrName = "message-uptr-57";
		static constexpr const char* RootPtrName = "root-ptr-157";
		static constexpr const char* ClientFreeUptrString = "client-free-string-11";
		static constexpr const char* ClientFreeRoot = "client-free-root-22";
		static constexpr const char* DeepRoot = "deep-root-13";
		static constexpr const char* DeepRoot2 = "deep-root-343";
		virtual void MakeUptrToMessage(std::string code) = 0;
		virtual void FreeUptrToMessage() = 0;
		virtual void MakeRoot(int x) = 0;
		virtual void MakeRootCloneHeap(int x) = 0;
		virtual void FreeRoot() = 0;
		virtual int RoundtripRootInShared() = 0;
		virtual void CreateForClientFree(int x, std::string s) = 0;
		virtual void MakeDeep(int n1, int n2) = 0;
		virtual void FreeDeep() = 0;
		virtual void MakeDeepCloneHeap(int n1, int n2) = 0;
		virtual void MakeDeepCloneHeap2(int n1, int n2) = 0;
		virtual void FreeDeep2() = 0;
		static std::unique_ptr<IServer> Make(std::string code);
	};

	class IClient
	{
	public:
		virtual ~IClient() = default;
		virtual size_t GetFreeMemory() const = 0;
		virtual std::string Read() const = 0;
		virtual std::string ReadWithPointer() const = 0;
		virtual std::string ReadWithUptr() = 0;
		virtual int ReadRoot() = 0;
		virtual int RoundtripRootInHeap() const = 0;
		virtual std::string ReadForClientFree() = 0;
		virtual void ClientFree() = 0;
		virtual Root<ShmAllocator<void>>& GetRoot() = 0;
		virtual Root<ShmAllocator<void>>& GetRootRetained() = 0;
		virtual Root2<ShmAllocator<void>>& GetDeep() = 0;
		virtual Root3<ShmAllocator<void>>& GetDeep2() = 0;
		static std::unique_ptr<IClient> Make();
	};

	const char* f();
}