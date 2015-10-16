#ifndef OSMIUM_IO_DETAIL_INPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_INPUT_FORMAT_HPP

/*

This file is part of Osmium (http://osmcode.org/libosmium).

Copyright 2013-2015 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#include <osmium/io/file.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/entity_bits.hpp>
#include <osmium/thread/queue.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            using osmdata_queue_type = osmium::thread::Queue<std::future<osmium::memory::Buffer>>;
            using string_queue_type = osmium::thread::Queue<std::string>;

            /**
             * Virtual base class for all classes decoding OSM files in
             * different formats.
             *
             * Do not use this class or derived classes directly. Use the
             * osmium::io::Reader class instead.
             */
            class InputFormat {

            protected:

                static constexpr size_t max_queue_size = 20; // XXX

                osmdata_queue_type m_output_queue;
                std::promise<osmium::io::Header> m_header_promise;
                std::thread m_thread;
                osmium::io::Header m_header;
                bool m_header_is_initialized;

                InputFormat(const char* queue_name) :
                    m_output_queue(max_queue_size, queue_name),
                    m_header_promise(),
                    m_thread(),
                    m_header(),
                    m_header_is_initialized(false) {
                }

                InputFormat(const InputFormat&) = delete;
                InputFormat(InputFormat&&) = delete;

                InputFormat& operator=(const InputFormat&) = delete;
                InputFormat& operator=(InputFormat&&) = delete;

            public:

                virtual ~InputFormat() {
                    try {
                        close();
                    } catch (...) {
                        // Ignore any exceptions at this point, because
                        // a destructor should not throw.
                    }
                }

                osmium::io::Header header() {
                    if (!m_header_is_initialized) {
                        m_header = m_header_promise.get_future().get();
                    }
                    return m_header;
                }

                /**
                 * Returns the next buffer with OSM data read from the
                 * file. Blocks if data is not available yet.
                 * Returns an empty buffer at end of input.
                 */
                osmium::memory::Buffer read() {
                    std::future<osmium::memory::Buffer> buffer_future;
                    m_output_queue.wait_and_pop(buffer_future);
                    return buffer_future.get();
                }

                void close() {
                    if (m_thread.joinable()) {
                        m_thread.join();
                    }
                }

            }; // class InputFormat

            /**
             * This factory class is used to create objects that decode OSM
             * data written in a specified format.
             *
             * Do not use this class directly. Use the osmium::io::Reader
             * class instead.
             */
            class InputFormatFactory {

            public:

                typedef std::function<
                            osmium::io::detail::InputFormat*(
                                osmium::osm_entity_bits::type read_which_entities,
                                string_queue_type&
                            )
                        > create_input_type;

            private:

                typedef std::map<osmium::io::file_format, create_input_type> map_type;

                map_type m_callbacks;

                InputFormatFactory() :
                    m_callbacks() {
                }

            public:

                static InputFormatFactory& instance() {
                    static InputFormatFactory factory;
                    return factory;
                }

                bool register_input_format(osmium::io::file_format format, create_input_type create_function) {
                    if (! m_callbacks.insert(map_type::value_type(format, create_function)).second) {
                        return false;
                    }
                    return true;
                }

                create_input_type* get_creator_function(const osmium::io::File& file) {
                    auto it = m_callbacks.find(file.format());
                    if (it == m_callbacks.end()) {
                        throw std::runtime_error(
                                std::string("Can not open file '") +
                                file.filename() +
                                "' with type '" +
                                as_string(file.format()) +
                                "'. No support for reading this format in this program.");
                    }
                    return &(it->second);
                }

            }; // class InputFormatFactory

            /**
             * Wrap the buffer into a future and add it to the given queue.
             */
            inline void send_to_queue(osmdata_queue_type& queue, osmium::memory::Buffer&& buffer) {
                std::promise<osmium::memory::Buffer> promise;
                queue.push(promise.get_future());
                promise.set_value(std::move(buffer));
            }

            inline void send_end_of_file(osmdata_queue_type& queue) {
                send_to_queue(queue, osmium::memory::Buffer{});
            }

            inline void send_exception(osmdata_queue_type& queue) {
                std::promise<osmium::memory::Buffer> promise;
                queue.push(promise.get_future());
                promise.set_exception(std::current_exception());
            }

            /**
             * Drain the given queue, ie pop and discard all values.
             */
            inline void drain_queue(string_queue_type& queue) {
                std::string s;
                do {
                    queue.wait_and_pop(s);
                } while (!s.empty());
            }

        } // namespace detail

    } // namespace io

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_INPUT_FORMAT_HPP
