#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <memory>
#include <stdexcept>


template<typename T, typename Alloc = std::allocator<T>>
class SpscTripleBuffer : private Alloc {
public:
	using value_type = T;
	using allocator_type = Alloc;
	using allocator_traits = std::allocator_traits<Alloc>;
	using size_type = typename allocator_traits::size_type

	static_assert(std::is_trivially_copyable_v<T>,
			"SpscTripleBuffer requires trivially copyable T");

	explicit SpscTripleBuffer() { // constructor

	}

	~SpscTripleBuffer() { // destructor

	}

};