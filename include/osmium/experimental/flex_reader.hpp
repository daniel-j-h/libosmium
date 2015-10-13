#ifndef OSMIUM_EXPERIMENTAL_FLEX_READER_HPP
#define OSMIUM_EXPERIMENTAL_FLEX_READER_HPP

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

#include <string>
#include <vector>

#include <osmium/area/assembler.hpp>
#include <osmium/area/multipolygon_collector.hpp>
#include <osmium/handler/node_locations_for_ways.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/header.hpp>
#include <osmium/io/reader.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/osm/entity_bits.hpp>
#include <osmium/visitor.hpp>

namespace osmium {

    /**
     * @brief Experimental code that is not "officially" supported.
     */
    namespace experimental {

        template <class TLocationHandler>
        class FlexReader {

            bool m_with_areas;
            osmium::osm_entity_bits::type m_entities;

            TLocationHandler& m_location_handler;

            osmium::io::Reader m_reader;
            osmium::area::Assembler::config_type m_assembler_config;
            osmium::area::MultipolygonCollector<osmium::area::Assembler> m_collector;

        public:

            explicit FlexReader(const osmium::io::File& file, TLocationHandler& location_handler, osmium::osm_entity_bits::type entities = osmium::osm_entity_bits::nwr) :
                m_with_areas((entities & osmium::osm_entity_bits::area) != 0),
                m_entities((entities & ~osmium::osm_entity_bits::area) | (m_with_areas ? osmium::osm_entity_bits::node | osmium::osm_entity_bits::way : osmium::osm_entity_bits::nothing)),
                m_location_handler(location_handler),
                m_reader(file, m_entities),
                m_assembler_config(),
                m_collector(m_assembler_config)
            {
                m_location_handler.ignore_errors();
                if (m_with_areas) {
                    osmium::io::Reader reader(file, osmium::osm_entity_bits::relation);
                    m_collector.read_relations(reader);
                    reader.close();
                }
            }

            explicit FlexReader(const std::string& filename, TLocationHandler& location_handler, osmium::osm_entity_bits::type entities = osmium::osm_entity_bits::nwr) :
                FlexReader(osmium::io::File(filename), location_handler, entities) {
            }

            explicit FlexReader(const char* filename, TLocationHandler& location_handler, osmium::osm_entity_bits::type entities = osmium::osm_entity_bits::nwr) :
                FlexReader(osmium::io::File(filename), location_handler, entities) {
            }

            osmium::memory::Buffer read() {
                osmium::memory::Buffer buffer = m_reader.read();

                if (buffer) {
                    if (m_with_areas) {
                        std::vector<osmium::memory::Buffer> area_buffers;
                        osmium::apply(buffer, m_location_handler, m_collector.handler([&area_buffers](osmium::memory::Buffer&& area_buffer) {
                            area_buffers.push_back(std::move(area_buffer));
                        }));
                        for (const osmium::memory::Buffer& b : area_buffers) {
                            buffer.add_buffer(b);
                            buffer.commit();
                        }
                    } else if (m_entities & (osmium::osm_entity_bits::node | osmium::osm_entity_bits::way)) {
                        osmium::apply(buffer, m_location_handler);
                    }
                }

                return buffer;
            }

            osmium::io::Header header() {
                return m_reader.header();
            }

            void close() {
                return m_reader.close();
            }

            bool eof() const {
                return m_reader.eof();
            }

            const osmium::area::MultipolygonCollector<osmium::area::Assembler>& collector() const {
                return m_collector;
            }

        }; // class FlexReader

    } // namespace experimental

} // namespace osmium

#endif // OSMIUM_EXPERIMENTAL_FLEX_READER_HPP
