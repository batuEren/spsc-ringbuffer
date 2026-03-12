#pragma once

#include <memory>
#include <type_traits>
#include <stdexcept>

template<typename T, typename Alloc = std::allocator<T>>
class Vector{
public:
	using value_type = T;
	using allocator_type = Alloc;
	using allocator_traits = std::allocator_traits<allocator_type>;
	using size_type = typename allocator_traits::size_type;

	explicit Vector(size_type capacity = 4,
		allocator_type const& alloc = allocator_type{})
		: allocator_type{ alloc }
		, capacity_{ capacity }
		, memBlock_{ allocator_traits::allocate(*this, capacity_) }
		, size_(0)
	{
		if (capacity_ < 1) {
			throw std::invalid_argument("capacity must be at least 1");
		}
	}

	~Vector() {
		//to do
	}

	bool empty() const noexcept {
		return size_ == 0;
	}

	size_type capacity() {
		return capacity_;
	}

	bool push_back() {

	}

	bool pop_back() {

	}


		
private:
	compressed_pair<Alloc, T*> memory;
	const size_type capacity_;
	const size_type size_;
	T* memBlock_;
};