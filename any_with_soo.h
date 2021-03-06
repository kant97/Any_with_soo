#ifndef ANYWITHS00_H
#define ANYWITHS00_H

#include <iostream>
#include <typeinfo>
#include <type_traits>

namespace mylib {

    struct bad_any_cast : public std::bad_cast {
        const std::string message;

        bad_any_cast(const std::string &message) : message(message) {}

        virtual const char *what() const noexcept {
            return message.c_str();
        }
    };
    struct any {

        // construct/copy/destruct
        any() noexcept {
            ptr = nullptr;
        }

        any(const any &rhs) {
            if (!rhs.empty()) ptr = rhs.ptr->make_copy(this->memalloc);
            
        }

        any(any &&rhs) noexcept {
            if (!rhs.empty()) ptr = rhs.ptr->move_obj(this->memalloc);
        }

        template<typename ValueType>
        any(const ValueType &rhs) {
            ptr = memalloc.allocate(rhs);
        }

        template<typename ValueType, typename = typename std::enable_if<!std::is_same<typename std::decay<ValueType>::type, any>::value>::type >
        any(ValueType &&rhs) noexcept {
            ptr =  memalloc.allocate(std::forward<ValueType>(rhs));
        }

        any &operator=(const any &rhs) {
            if (rhs.empty()) {
                clear();
                return *this;
            }
            any tmp = rhs;
            clear();
            if (tmp.empty()) return *this;
            ptr = tmp.ptr->make_copy(this->memalloc);
            return *this;
        }

        any &operator=(any &&rhs) noexcept {
            clear();
            *this = rhs;
            rhs.clear();
            return *this;
        }

        template<typename ValueType>
        any &operator=(const ValueType &rhs) {
            clear();
            ptr = memalloc.allocate( rhs);
            return *this;
        }

        template<typename ValueType, typename = typename std::enable_if<!std::is_same<typename std::decay<ValueType>::type, any>::value>::type>
        any &operator=(ValueType &&rhs) noexcept {
            clear();
            ptr = memalloc.allocate(std::forward<ValueType>(rhs));
            return *this;
        }

        ~any() {
            clear();
        }

        // modifiers
        any &swap(any &rhs) noexcept {
            if (this->memalloc.tag == allocator::Tag_t::EMPTY) {
                *this = rhs;
                rhs.clear();
            } else if (rhs.memalloc.tag == allocator::Tag_t::EMPTY) {
                rhs = *this;
                this->clear();
            } else if (this->memalloc.tag == allocator::Tag_t::BIG && rhs.memalloc.tag == allocator::Tag_t::BIG) {
                std::swap(this->ptr, rhs.ptr);
            } else if (this->memalloc.tag == allocator::Tag_t::SMALL && rhs.memalloc.tag == allocator::Tag_t::SMALL) {
                any tmp = rhs;
                rhs = *this;
                *this = tmp;
            } else if (this->memalloc.tag == allocator::Tag_t::BIG && rhs.memalloc.tag == allocator::Tag_t::SMALL) {
                god_of_war *tmp_ptr = this->ptr;
                this->ptr = nullptr;
                this->ptr = rhs.ptr->make_copy(this->memalloc);
                rhs.memalloc.free(rhs.ptr);
                rhs.ptr = tmp_ptr;
                this->memalloc.tag = allocator::Tag_t::SMALL;
                rhs.memalloc.tag = allocator::Tag_t::BIG;
            } else if (this->memalloc.tag == allocator::Tag_t::SMALL && rhs.memalloc.tag == allocator::Tag_t::BIG) {
                god_of_war *tmp_ptr = rhs.ptr;
                rhs.ptr = nullptr;
                rhs.ptr = this->ptr->make_copy(rhs.memalloc);
                this->memalloc.free(this->ptr);
                this->ptr = tmp_ptr;
                this->memalloc.tag = allocator::Tag_t::BIG;
                rhs.memalloc.tag = allocator::Tag_t::SMALL;
            }
            return *this;
        }

        // queries
        bool empty() const noexcept {
            return memalloc.tag == allocator::Tag_t::EMPTY;
        }

        const std::type_info &type() const noexcept {
            if (empty()) return typeid(void);
            return ptr->get_type_info();
        }

        void clear() noexcept {
            memalloc.free(ptr);
            ptr = nullptr;
        }

        template<typename T>
        friend T any_cast(any &);

        template<typename T>
        friend T any_cast(any &&);

        template<typename T>
        friend T any_cast(const any &);

        template<typename ValueType>
        friend const ValueType *any_cast(const any *);

        template<typename ValueType>
        friend ValueType *any_cast(any *);

    private:

        template<typename T>
        using rem_ref_t = typename std::remove_reference<T>::type;

        struct allocator;

        struct god_of_war {
            virtual ~god_of_war() {}

            virtual const std::type_info &get_type_info() const noexcept = 0;

            virtual god_of_war *make_copy(allocator &alloc) const = 0;

            virtual god_of_war *move_obj(allocator &alloc) const = 0;
        };

        template<typename T>
        struct war : public god_of_war {
            war(T &&obj) : obj(std::move(obj)) {}

            war(const T &obj) : obj(obj) {}

            war() {}

            const std::type_info &get_type_info() const noexcept {
                return typeid(obj);
            }

            god_of_war *make_copy(allocator &alloc) const {
                return alloc.allocate(obj);
            }

            god_of_war *move_obj(allocator &alloc) const {
            	return alloc.allocate(std::move(obj));
            }

            T obj;
        };

        class allocator {
        public:
            const static int N = 3;

            static void * var_for_type;

            using stack_alloc_storage = typename std::aligned_storage<
                    N * sizeof(decltype(var_for_type)), std::alignment_of<decltype(var_for_type)>::value>::type;


            template <typename T>
            using need_allocation = typename std::integral_constant<bool, !((sizeof(T) <= sizeof(stack_alloc_storage)) &&
                (std::alignment_of<T>::value <=
                std::alignment_of<stack_alloc_storage>::value) &&
                (std::is_nothrow_copy_constructible<T>::value))>;

            stack_alloc_storage buf[1];

            enum Tag_t {EMPTY, SMALL, BIG} tag = EMPTY;

            template<typename T>
            god_of_war *allocate(T &&obj) {
                if (!need_allocation<war<rem_ref_t<T> >>::value) {
                    tag = SMALL;
                    return new(buf) war< typename std::decay<T>::type >(std::forward<T>(obj));
                } else {
                    tag = BIG;
                    return new war< typename std::decay<T>::type >(std::forward<T>(obj));
                }
            }

            void free(god_of_war *ptr) {
                if (tag == SMALL) {
                    ptr->~god_of_war();
                } else if (tag == BIG) delete ptr;
                tag = EMPTY;
            }
        } memalloc;

        god_of_war *ptr;
    };

    void swap(any &a, any &b) noexcept {
        a.swap(b);
    }

    template<typename T>
    T any_cast(any &a) {
        any::war<T> *casted = dynamic_cast<any::war<T> *>(a.ptr);
        if (casted == 0) {
            throw mylib::bad_any_cast("bad any cast");
        }
        return casted->obj;
    }

    template<typename T>
    T any_cast(any &&a) {
        return any_cast<T>(a);
    }

    template<typename T>
    T any_cast(const any &a) {
        mylib::any tmp;
        tmp = a;
        T ans = any_cast<T>(tmp);
        return ans;
    }

    template<typename T>
    T *any_cast(any *a) {
        return a && a->type() == typeid(T)
               ? &dynamic_cast<any::war<typename std::remove_cv<T>::type> *>(a->ptr)->obj
               : nullptr;
    }

    template<typename T>
    const T *any_cast(const any *a) {
        return any_cast<T>(const_cast<any *>(a));
    }
}

#endif