#pragma once
#include <type_traits>
#include <utility>

namespace pmon::util
{
    template <typename T> struct MemberPointerInfo;

    // deconstruct member pointer into the object type the pointer works with and the member type
    template <typename S, typename M>
    struct MemberPointerInfo<M S::*> {
        using StructType = S;
        using MemberType = M;
    };

    // get the type of the elements in any iterable type (typically containers)
    template<typename T>
    using ContainerElementType = std::remove_reference_t<decltype(*std::begin(std::declval<T&>()))>;

    // get the size of any type, even void (defined as 0 for void)
    template<typename T>
    constexpr std::size_t VoidableSizeof() {
        if constexpr (std::is_void_v<T>) {
            return 0;
        }
        else {
            return sizeof(T);
        }
    }
}