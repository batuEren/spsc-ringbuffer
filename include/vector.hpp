#pragma once

#include <memory>
#include <type_traits>
#include <stdexcept>

template<typename T, typename Alloc = std::allocator<T>>
class Vector {
public:
	using value_type = T;
	using allocator_type = Alloc;
	using allocator_traits = std::allocator_traits<allocator_type>;
	using size_type = typename allocator_traits::size_type;

	explicit Vector(size_type capacity = 4,
		allocator_type const& alloc = allocator_type{})
		: allocator_{ alloc }
		, capacity_{ capacity }
		, memBlock_ { allocator_traits::allocate(allocator_, capacity_) }
		, size_{0}
	{
		if (capacity_ < 1) {
			throw std::invalid_argument("capacity must be at least 1");
		}
	}

	~Vector() {
		for (size_type i{ 0 }; i < size_; i++) {
			allocator_traits::destroy(allocator_, memBlock_ + i);
		}

		allocator_traits::deallocate(allocator_, memBlock_, capacity_);
	}

	bool empty() const noexcept {
		return size_ == 0;
	}

	size_type capacity() {
		return capacity_;
	}

	size_type size() {
		return size_;
	}

	T& front() {
		if (size_ > 0) return *(memBlock_);
		
		throw std::exception("no elements in vector");
	}

	T& back() {
		if (size_ > 0) return *(memBlock_ + size_ - 1);

		throw std::exception("no elements in vector");
	}

	value_type& operator[] (size_type index) {
		//if (index >= size_) throw std::out_of_range("index out of bounds");
		//no bounds checking for speed
		return *(memBlock_ + index);
	}

	void push_back(const T& item) noexcept {
		allocator_traits::construct(allocator_, memBlock_ + size_, item);
		size_++;
		//add growth
	}

	// add emplace back

	void pop_back() {
		if (size_ == 0) return; // check effect on performance
		size_--;
		allocator_traits::destroy(allocator_, memBlock_ + size_);
		//possible shrink
	}
		
private:
	Alloc allocator_;
	T* memBlock_;
	size_type capacity_;
	size_type size_;
};