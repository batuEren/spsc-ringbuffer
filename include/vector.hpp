#pragma once

#include <memory>
#include <type_traits>
#include <stdexcept>
#include <utility>

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
		, size_{0}
	{
		if (capacity_ < 1) {
			throw std::invalid_argument("capacity must be at least 1");
		}
		memBlock_ = allocator_traits::allocate(allocator_, capacity_);
	}

	~Vector() {
		delete_elems(memBlock_, size_);
		allocator_traits::deallocate(allocator_, memBlock_, capacity_);
	}

	Vector(const Vector& other) // copy cons
		: size_(0),
		capacity_(other.capacity()),
		memBlock_(allocator_traits::allocate(allocator_, other.capacity()))
		{
		for (const value_type& e : other) {
			push_back(e);
		}
	}

	Vector& operator=(const Vector& other) { // copy assignment constr
		if (this == &other) return *this; // self guard

		delete_elems(memBlock_, size_);
		allocator_traits::deallocate(allocator_, memBlock_, capacity_);
		memBlock_ = allocator_traits::allocate(allocator_, other.capacity());

		size_ = 0;
		capacity_ = other.capacity_;

		for (const value_type& e : other) {
			push_back(e);
		}
		return *this;
	}


	Vector(Vector&& other) noexcept // move constr
		: allocator_(std::move(other.allocator_))
		, capacity_(other.capacity_)
		, size_(other.size_)
		, memBlock_(other.memBlock_) 
	{
		other.memBlock_ = nullptr;
		other.size_ = 0;
		other.capacity_ = 0;
	}

	Vector& operator=(Vector&& other) 
	{ // move assignment constr
		if (this == &other) return *this;

		delete_elems(memBlock_, size_);
		allocator_traits::deallocate(allocator_, memBlock_, capacity_);

		allocator_ = other.allocator_;
		capacity_ = other.capacity_;
		size_ = other.size_;
		memBlock_ = other.memBlock_;

		other.memBlock_ = nullptr;
		other.capacity_ = 0;
		other.size_ = 0;

		return *this;
	}

	T* begin() const { return memBlock_; }
	T* end()   const { return memBlock_ + size_; }

	bool empty() const noexcept {
		return size_ == 0;
	}

	size_type capacity() const {
		return capacity_;
	}

	size_type size() const {
		return size_;
	}

	T& front() {
		if (size_ > 0) return *(memBlock_);
		
		throw std::runtime_error("no elements in vector");
	}

	T& back() {
		if (size_ > 0) return *(memBlock_ + size_ - 1);

		throw std::runtime_error("no elements in vector");
	}

	const T& front() const {
		if (size_ > 0) return *(memBlock_);

		throw std::runtime_error("no elements in vector");
	}

	const T& back() const {
		if (size_ > 0) return *(memBlock_ + size_ - 1);

		throw std::runtime_error("no elements in vector");
	}

	value_type& operator[] (size_type index) {
		//if (index >= size_) throw std::out_of_range("index out of bounds");
		//no bounds checking for speed
		return *(memBlock_ + index);
	}

	void push_back(const T& item) {
		if (size_ >= capacity_) {
			//alloc new memory
			T* newMemBlock = allocator_traits::allocate(allocator_, capacity_ * 2);
			//move everything
			for (size_type i{ 0 }; i < size_; i++) {
				allocator_traits::construct(allocator_, newMemBlock + i, *(memBlock_ + i));
			}
			delete_elems(memBlock_, size_);
			allocator_traits::deallocate(allocator_, memBlock_, capacity_);

			capacity_ *= 2;

			memBlock_ = newMemBlock;
		}

		allocator_traits::construct(allocator_, memBlock_ + size_, item);
		size_++;
	}

	// add emplace back

	void pop_back() {
		if (size_ == 0) return; // check effect on performance
		size_--;
		allocator_traits::destroy(allocator_, memBlock_ + size_);
		//possible shrink
	}
		
private:
	void delete_elems(T* ptr, size_type size) {
		for (size_type i{ 0 }; i < size; i++) {
			allocator_traits::destroy(allocator_, ptr + i);
		}
	}


	Alloc allocator_;
	T* memBlock_;
	size_type capacity_;
	size_type size_;
};