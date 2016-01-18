#ifndef OSMIUM_MEMORY_BUFFER_HPP
#define OSMIUM_MEMORY_BUFFER_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2016 Jochen Topf <jochen@topf.org> and others (see README).

Boost Software License - Version 1.0 - August 17th, 2003

Permission is hereby granted, free of charge, to any person or organization
obtaining a copy of the software and accompanying documentation covered by
this license (the "Software") to use, reproduce, display, distribute,
execute, and transmit the Software, and to prepare derivative works of the
Software, and to permit third-parties to whom the Software is furnished to
do so, all subject to the following:

The copyright notices in the Software and this entire statement, including
the above license grant, this restriction and the following disclaimer,
must be included in all copies of the Software, in whole or in part, and
all derivative works of the Software, unless such copies or derivative
works are solely in the form of machine-executable object code generated by
a source language processor.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.

*/

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

#include <osmium/memory/item.hpp>
#include <osmium/memory/item_iterator.hpp>
#include <osmium/osm/entity.hpp>
#include <osmium/util/compatibility.hpp>

namespace osmium {

    /**
     * Exception thrown by the osmium::memory::Buffer class when somebody tries
     * to write data into a buffer and it doesn't fit. Buffers with internal
     * memory management will not throw this exception, but increase their size.
     */
    struct buffer_is_full : public std::runtime_error {

        buffer_is_full() :
            std::runtime_error("Osmium buffer is full") {
        }

    }; // struct buffer_is_full

    /**
     * @brief Memory management of items in buffers and iterators over this data.
     */
    namespace memory {

        /**
         * A memory area for storing OSM objects and other items. Each item stored
         * has a type and a length. See the Item class for details.
         *
         * Data can be added to a buffer piece by piece using reserve_space() and
         * add_item(). After all data that together forms an item is added, it must
         * be committed using the commit() call. Usually this is done through the
         * Builder class and its derived classes.
         *
         * You can iterate over all items in a buffer using the iterators returned
         * by begin(), end(), cbegin(), and cend().
         *
         * Buffers exist in two flavours, those with external memory management and
         * those with internal memory management. If you already have some memory
         * with data in it (for instance read from disk), you create a Buffer with
         * external memory management. It is your job then to free the memory once
         * the buffer isn't used any more. If you don't have memory already, you can
         * create a Buffer object and have it manage the memory internally. It will
         * dynamically allocate memory and free it again after use.
         *
         * By default, if a buffer gets full it will throw a buffer_is_full exception.
         * You can use the set_full_callback() method to set a callback functor
         * which will be called instead of throwing an exception. The full
         * callback functionality is deprecated and will be removed in the
         * future. See the documentation for set_full_callback() for alternatives.
         */
        class Buffer {

        public:

            // This is needed so we can call std::back_inserter() on a Buffer.
            using value_type = Item;

            enum class auto_grow : bool {
                yes = true,
                no  = false
            }; // enum class auto_grow

        private:

            std::vector<unsigned char> m_memory;
            unsigned char* m_data;
            size_t m_capacity;
            size_t m_written;
            size_t m_committed;
            auto_grow m_auto_grow {auto_grow::no};
            std::function<void(Buffer&)> m_full;

        public:

            /**
             * The constructor without any parameters creates an invalid,
             * buffer, ie an empty hull of a buffer that has no actual memory
             * associated with it. It can be used to signify end-of-data.
             *
             * Most methods of the Buffer class will not work with an invalid
             * buffer.
             */
            Buffer() noexcept :
                m_memory(),
                m_data(nullptr),
                m_capacity(0),
                m_written(0),
                m_committed(0) {
            }

            /**
             * Constructs a valid externally memory-managed buffer using the
             * given memory and size.
             *
             * @param data A pointer to some already initialized data.
             * @param size The size of the initialized data.
             *
             * @throws std::invalid_argument if the size isn't a multiple of
             *         the alignment.
             */
            explicit Buffer(unsigned char* data, size_t size) :
                m_memory(),
                m_data(data),
                m_capacity(size),
                m_written(size),
                m_committed(size) {
                if (size % align_bytes != 0) {
                    throw std::invalid_argument("buffer size needs to be multiple of alignment");
                }
            }

            /**
             * Constructs a valid externally memory-managed buffer with the
             * given capacity that already contains 'committed' bytes of data.
             *
             * @param data A pointer to some (possibly initialized) data.
             * @param capacity The size of the memory for this buffer.
             * @param committed The size of the initialized data. If this is 0, the buffer startes out empty.
             *
             * @throws std::invalid_argument if the capacity or committed isn't
             *         a multiple of the alignment.
             */
            explicit Buffer(unsigned char* data, size_t capacity, size_t committed) :
                m_memory(),
                m_data(data),
                m_capacity(capacity),
                m_written(committed),
                m_committed(committed) {
                if (capacity % align_bytes != 0) {
                    throw std::invalid_argument("buffer capacity needs to be multiple of alignment");
                }
                if (committed % align_bytes != 0) {
                    throw std::invalid_argument("buffer parameter 'committed' needs to be multiple of alignment");
                }
            }

            /**
             * Constructs a valid internally memory-managed buffer with the
             * given capacity.
             * Will internally get dynamic memory of the required size.
             * The dynamic memory will be automatically freed when the Buffer
             * is destroyed.
             *
             * @param capacity The (initial) size of the memory for this buffer.
             * @param auto_grow Should this buffer automatically grow when it
             *        becomes to small?
             *
             * @throws std::invalid_argument if the capacity isn't a multiple
             *         of the alignment.
             */
            explicit Buffer(size_t capacity, auto_grow auto_grow = auto_grow::yes) :
                m_memory(capacity),
                m_data(m_memory.data()),
                m_capacity(capacity),
                m_written(0),
                m_committed(0),
                m_auto_grow(auto_grow) {
                if (capacity % align_bytes != 0) {
                    throw std::invalid_argument("buffer capacity needs to be multiple of alignment");
                }
            }

            // buffers can not be copied
            Buffer(const Buffer&) = delete;
            Buffer& operator=(const Buffer&) = delete;

            // buffers can be moved
            Buffer(Buffer&&) = default;
            Buffer& operator=(Buffer&&) = default;

            ~Buffer() = default;

            /**
             * Return a pointer to data inside the buffer.
             *
             * @pre The buffer must be valid.
             */
            unsigned char* data() const noexcept {
                assert(m_data);
                return m_data;
            }

            /**
             * Returns the capacity of the buffer, ie how many bytes it can
             * contain. Always returns 0 on invalid buffers.
             */
            size_t capacity() const noexcept {
                return m_capacity;
            }

            /**
             * Returns the number of bytes already filled in this buffer.
             * Always returns 0 on invalid buffers.
             */
            size_t committed() const noexcept {
                return m_committed;
            }

            /**
             * Returns the number of bytes currently filled in this buffer that
             * are not yet committed.
             * Always returns 0 on invalid buffers.
             */
            size_t written() const noexcept {
                return m_written;
            }

            /**
             * This tests if the current state of the buffer is aligned
             * properly. Can be used for asserts.
             *
             * @pre The buffer must be valid.
             */
            bool is_aligned() const noexcept {
                assert(m_data);
                return (m_written % align_bytes == 0) && (m_committed % align_bytes == 0);
            }

            /**
             * Set functor to be called whenever the buffer is full
             * instead of throwing buffer_is_full.
             *
             * The behaviour is undefined if you call this on an invalid
             * buffer.
             *
             * @pre The buffer must be valid.
             *
             * @deprecated
             * Callback functionality will be removed in the future. Either
             * detect the buffer_is_full exception or use a buffer with
             * auto_grow::yes. If you want to avoid growing buffers, check
             * that the used size of the buffer (committed()) is small enough
             * compared to the capacity (for instance small than 90% of the
             * capacity) before adding anything to the Buffer. If the buffer
             * is initialized with auto_grow::yes, it will still grow in the
             * rare case that a very large object will be added taking more
             * than the difference between committed() and capacity().
             */
            OSMIUM_DEPRECATED void set_full_callback(std::function<void(Buffer&)> full) {
                assert(m_data);
                m_full = full;
            }

            /**
             * Grow capacity of this buffer to the given size.
             * This works only with internally memory-managed buffers.
             * If the given size is not larger than the current capacity,
             * nothing is done.
             * Already written but not committed data is discarded.
             *
             * @pre The buffer must be valid.
             *
             * @param size New capacity.
             *
             * @throws std::logic_error if the buffer doesn't use internal
             *         memory management.
             * @throws std::invalid_argument if the size isn't a multiple
             *         of the alignment.
             */
            void grow(size_t size) {
                assert(m_data);
                if (m_memory.empty()) {
                    throw std::logic_error("Can't grow Buffer if it doesn't use internal memory management.");
                }
                if (m_capacity < size) {
                    if (size % align_bytes != 0) {
                        throw std::invalid_argument("buffer capacity needs to be multiple of alignment");
                    }
                    m_memory.resize(size);
                    m_data = m_memory.data();
                    m_capacity = size;
                }
            }

            /**
             * Mark currently written bytes in the buffer as committed.
             *
             * @pre The buffer must be valid and aligned properly (as indicated
             *      by is_aligned().
             *
             * @returns Number of committed bytes before this commit. Can be
             *          used as an offset into the buffer to get to the
             *          object being committed by this call.
             */
            size_t commit() {
                assert(m_data);
                assert(is_aligned());

                const size_t offset = m_committed;
                m_committed = m_written;
                return offset;
            }

            /**
             * Roll back changes in buffer to last committed state.
             *
             * @pre The buffer must be valid.
             */
            void rollback() {
                assert(m_data);
                m_written = m_committed;
            }

            /**
             * Clear the buffer.
             *
             * No-op on an invalid buffer.
             *
             * @returns Number of bytes in the buffer before it was cleared.
             */
            size_t clear() {
                const size_t committed = m_committed;
                m_written = 0;
                m_committed = 0;
                return committed;
            }

            /**
             * Get the data in the buffer at the given offset.
             *
             * @pre The buffer must be valid.
             *
             * @tparam T Type we want to the data to be interpreted as.
             *
             * @returns Reference of given type pointing to the data in the
             *          buffer.
             */
            template <typename T>
            T& get(const size_t offset) const {
                assert(m_data);
                return *reinterpret_cast<T*>(&m_data[offset]);
            }

            /**
             * Reserve space of given size in buffer and return pointer to it.
             * This is the only way of adding data to the buffer. You reserve
             * the space and then fill it.
             *
             * Note that you have to eventually call commit() to actually
             * commit this data.
             *
             * If there isn't enough space in the buffer, one of three things
             * can happen:
             *
             * * If you have set a callback with set_full_callback(), it is
             *   called. After the call returns, you must have either grown
             *   the buffer or cleared it by calling buffer.clear(). (Usage
             *   of the full callback is deprecated and this functionality
             *   will be removed in the future. See the documentation for
             *   set_full_callback() for alternatives.
             * * If no callback is defined and this buffer uses internal
             *   memory management, the buffers capacity is grown, so that
             *   the new data will fit.
             * * Else the buffer_is_full exception is thrown.
             *
             * @pre The buffer must be valid.
             *
             * @param size Number of bytes to reserve.
             *
             * @returns Pointer to reserved space. Note that this pointer is
             *          only guaranteed to be valid until the next call to
             *          reserve_space().
             *
             * @throws osmium::buffer_is_full if the buffer is full there is
             *         no callback defined and the buffer isn't auto-growing.
             */
            unsigned char* reserve_space(const size_t size) {
                assert(m_data);
                // try to flush the buffer empty first.
                if (m_written + size > m_capacity && m_full) {
                    m_full(*this);
                }
                // if there's still not enough space, then try growing the buffer.
                if (m_written + size > m_capacity) {
                    if (!m_memory.empty() && (m_auto_grow == auto_grow::yes)) {
                        // double buffer size until there is enough space
                        size_t new_capacity = m_capacity * 2;
                        while (m_written + size > new_capacity) {
                            new_capacity *= 2;
                        }
                        grow(new_capacity);
                    } else {
                        throw osmium::buffer_is_full();
                    }
                }
                unsigned char* data = &m_data[m_written];
                m_written += size;
                return data;
            }

            /**
             * Add an item to the buffer. The size of the item is stored inside
             * the item, so we know how much memory to copy.
             *
             * Note that you have to eventually call commit() to actually
             * commit this data.
             *
             * @pre The buffer must be valid.
             *
             * @tparam T Class of the item to be copied.
             *
             * @param item Reference to the item to be copied.
             *
             * @returns Reference to newly copied data in the buffer.
             */
            template <typename T>
            T& add_item(const T& item) {
                assert(m_data);
                unsigned char* target = reserve_space(item.padded_size());
                std::copy_n(reinterpret_cast<const unsigned char*>(&item), item.padded_size(), target);
                return *reinterpret_cast<T*>(target);
            }

            /**
             * Add committed contents of the given buffer to this buffer.
             *
             * @pre The buffer must be valid.
             *
             * Note that you have to eventually call commit() to actually
             * commit this data.
             *
             * @param buffer The source of the copy. Must be valid.
             */
            void add_buffer(const Buffer& buffer) {
                assert(m_data && buffer);
                unsigned char* target = reserve_space(buffer.committed());
                std::copy_n(buffer.data(), buffer.committed(), target);
            }

            /**
             * Add an item to the buffer. This function is provided so that
             * you can use std::back_inserter.
             *
             * @pre The buffer must be valid.
             *
             * @param item The item to be added.
             */
            void push_back(const osmium::memory::Item& item) {
                assert(m_data);
                add_item(item);
                commit();
            }

            /**
             * An iterator that can be used to iterate over all items of
             * type T in a buffer.
             */
            template <typename T>
            using t_iterator = osmium::memory::ItemIterator<T>;

            /**
             * A const iterator that can be used to iterate over all items of
             * type T in a buffer.
             */
            template <typename T>
            using t_const_iterator = osmium::memory::ItemIterator<const T>;

            /**
             * An iterator that can be used to iterate over all OSMEntity
             * objects in a buffer.
             */
            using iterator = t_iterator<osmium::OSMEntity>;

            /**
             * A const iterator that can be used to iterate over all OSMEntity
             * objects in a buffer.
             */
            using const_iterator = t_const_iterator<osmium::OSMEntity>;

            /**
             * Get iterator for iterating over all items of type T in the
             * buffer.
             *
             * @pre The buffer must be valid.
             *
             * @returns Iterator to first item of type T in the buffer.
             */
            template <typename T>
            t_iterator<T> begin() {
                assert(m_data);
                return t_iterator<T>(m_data, m_data + m_committed);
            }

            /**
             * Get iterator for iterating over all objects of class OSMEntity
             * in the buffer.
             *
             * @pre The buffer must be valid.
             *
             * @returns Iterator to first OSMEntity in the buffer.
             */
            iterator begin() {
                assert(m_data);
                return iterator(m_data, m_data + m_committed);
            }

            /**
             * Get iterator for iterating over all items of type T in the
             * buffer.
             *
             * @pre The buffer must be valid.
             *
             * @returns Iterator to first item of type T after given offset
             *          in the buffer.
             */
            template <typename T>
            t_iterator<T> get_iterator(size_t offset) {
                assert(m_data);
                return t_iterator<T>(m_data + offset, m_data + m_committed);
            }

            /**
             * Get iterator for iterating over all objects of class OSMEntity
             * in the buffer.
             *
             * @pre The buffer must be valid.
             *
             * @returns Iterator to first OSMEntity after given offset in the
             *          buffer.
             */
            iterator get_iterator(size_t offset) {
                assert(m_data);
                return iterator(m_data + offset, m_data + m_committed);
            }

            /**
             * Get iterator for iterating over all items of type T in the
             * buffer.
             *
             * @pre The buffer must be valid.
             *
             * @returns End iterator.
             */
            template <typename T>
            t_iterator<T> end() {
                assert(m_data);
                return t_iterator<T>(m_data + m_committed, m_data + m_committed);
            }

            /**
             * Get iterator for iterating over all objects of class OSMEntity
             * in the buffer.
             *
             * @pre The buffer must be valid.
             *
             * @returns End iterator.
             */
            iterator end() {
                assert(m_data);
                return iterator(m_data + m_committed, m_data + m_committed);
            }

            template <typename T>
            t_const_iterator<T> cbegin() const {
                assert(m_data);
                return t_const_iterator<T>(m_data, m_data + m_committed);
            }

            const_iterator cbegin() const {
                assert(m_data);
                return const_iterator(m_data, m_data + m_committed);
            }

            template <typename T>
            t_const_iterator<T> get_iterator(size_t offset) const {
                assert(m_data);
                return t_const_iterator<T>(m_data + offset, m_data + m_committed);
            }

            const_iterator get_iterator(size_t offset) const {
                assert(m_data);
                return const_iterator(m_data + offset, m_data + m_committed);
            }

            template <typename T>
            t_const_iterator<T> cend() const {
                assert(m_data);
                return t_const_iterator<T>(m_data + m_committed, m_data + m_committed);
            }

            const_iterator cend() const {
                assert(m_data);
                return const_iterator(m_data + m_committed, m_data + m_committed);
            }

            template <typename T>
            t_const_iterator<T> begin() const {
                return cbegin<T>();
            }

            const_iterator begin() const {
                return cbegin();
            }

            template <typename T>
            t_const_iterator<T> end() const {
                return cend<T>();
            }

            const_iterator end() const {
                return cend();
            }

            /**
             * In a bool context any valid buffer is true.
             */
            explicit operator bool() const noexcept {
                return m_data != nullptr;
            }

            void swap(Buffer& other) {
                using std::swap;

                swap(m_memory, other.m_memory);
                swap(m_data, other.m_data);
                swap(m_capacity, other.m_capacity);
                swap(m_written, other.m_written);
                swap(m_committed, other.m_committed);
                swap(m_auto_grow, other.m_auto_grow);
                swap(m_full, other.m_full);
            }

            /**
             * Purge removed items from the buffer. This is done by moving all
             * non-removed items forward in the buffer overwriting removed
             * items and then correcting the m_written and m_committed numbers.
             *
             * Note that calling this function invalidates all iterators on
             * this buffer and all offsets in this buffer.
             *
             * For every non-removed item that moves its position, the function
             * 'moving_in_buffer' is called on the given callback object with
             * the old and new offsets in the buffer where the object used to
             * be and is now, respectively. This call can be used to update any
             * indexes.
             *
             * @pre The buffer must be valid.
             */
            template <typename TCallbackClass>
            void purge_removed(TCallbackClass* callback) {
                assert(m_data);
                if (begin() == end()) {
                    return;
                }

                iterator it_write = begin();

                iterator next;
                for (iterator it_read = begin(); it_read != end(); it_read = next) {
                    next = std::next(it_read);
                    if (!it_read->removed()) {
                        if (it_read != it_write) {
                            assert(it_read.data() >= data());
                            assert(it_write.data() >= data());
                            size_t old_offset = static_cast<size_t>(it_read.data() - data());
                            size_t new_offset = static_cast<size_t>(it_write.data() - data());
                            callback->moving_in_buffer(old_offset, new_offset);
                            std::memmove(it_write.data(), it_read.data(), it_read->padded_size());
                        }
                        it_write.advance_once();
                    }
                }

                assert(it_write.data() >= data());
                m_written = static_cast<size_t>(it_write.data() - data());
                m_committed = m_written;
            }

        }; // class Buffer

        inline void swap(Buffer& lhs, Buffer& rhs) {
            lhs.swap(rhs);
        }

        /**
         * Compare two buffers for equality.
         *
         * Buffers are equal if they are both invalid or if they are both
         * valid and have the same data pointer, capacity and committed
         * data.
         */
        inline bool operator==(const Buffer& lhs, const Buffer& rhs) noexcept {
            if (!lhs || !rhs) {
                return !lhs && !rhs;
            }
            return lhs.data() == rhs.data() && lhs.capacity() == rhs.capacity() && lhs.committed() == rhs.committed();
        }

        inline bool operator!=(const Buffer& lhs, const Buffer& rhs) noexcept {
            return ! (lhs == rhs);
        }

    } // namespace memory

} // namespace osmium

#endif // OSMIUM_MEMORY_BUFFER_HPP
