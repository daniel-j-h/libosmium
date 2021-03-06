#ifndef OSMIUM_IO_DETAIL_XML_OUTPUT_FORMAT_HPP
#define OSMIUM_IO_DETAIL_XML_OUTPUT_FORMAT_HPP

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

#include <cassert>
#include <chrono>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <future>
#include <iterator>
#include <memory>
#include <ratio>
#include <string>
#include <thread>
#include <utility>

#include <osmium/handler.hpp>
#include <osmium/io/detail/output_format.hpp>
#include <osmium/io/file.hpp>
#include <osmium/io/file_format.hpp>
#include <osmium/io/header.hpp>
#include <osmium/memory/buffer.hpp>
#include <osmium/memory/collection.hpp>
#include <osmium/osm/box.hpp>
#include <osmium/osm/changeset.hpp>
#include <osmium/osm/item_type.hpp>
#include <osmium/osm/location.hpp>
#include <osmium/osm/node.hpp>
#include <osmium/osm/object.hpp>
#include <osmium/osm/relation.hpp>
#include <osmium/osm/tag.hpp>
#include <osmium/osm/timestamp.hpp>
#include <osmium/osm/types.hpp>
#include <osmium/osm/way.hpp>
#include <osmium/thread/pool.hpp>
#include <osmium/visitor.hpp>

namespace osmium {

    namespace io {

        namespace detail {

            struct XMLWriteError {};

            namespace {

                void xml_string(std::string& out, const char* in) {
                    for (; *in != '\0'; ++in) {
                        switch(*in) {
                            case '&':  out += "&amp;";  break;
                            case '\"': out += "&quot;"; break;
                            case '\'': out += "&apos;"; break;
                            case '<':  out += "&lt;";   break;
                            case '>':  out += "&gt;";   break;
                            case '\n': out += "&#xA;";  break;
                            case '\r': out += "&#xD;";  break;
                            case '\t': out += "&#x9;";  break;
                            default:   out += *in;      break;
                        }
                    }
                }

                const size_t tmp_buffer_size = 100;

                template <typename T>
                void oprintf(std::string& out, const char* format, T value) {
                    char buffer[tmp_buffer_size+1];
                    size_t max_size = sizeof(buffer)/sizeof(char);
#ifndef NDEBUG
                    int len =
#endif
#ifndef _MSC_VER
                    snprintf(buffer, max_size, format, value);
#else
                    _snprintf(buffer, max_size, format, value);
#endif
                    assert(len > 0 && static_cast<size_t>(len) < max_size);
                    out += buffer;
                }

            } // anonymous namespace

            class XMLOutputBlock : public osmium::handler::Handler {

                // operation (create, modify, delete) for osc files
                enum class operation {
                    op_none   = 0,
                    op_create = 1,
                    op_modify = 2,
                    op_delete = 3
                }; // enum class operation

                std::shared_ptr<osmium::memory::Buffer> m_input_buffer;

                std::shared_ptr<std::string> m_out;

                operation m_last_op {operation::op_none};

                const bool m_add_metadata;
                const bool m_write_visible_flag;
                const bool m_write_change_ops;

                void write_spaces(int num) {
                    for (; num != 0; --num) {
                        *m_out += ' ';
                    }
                }

                int prefix_spaces() {
                    return m_write_change_ops ? 4 : 2;
                }

                void write_prefix() {
                    write_spaces(prefix_spaces());
                }

                void write_meta(const osmium::OSMObject& object) {
                    oprintf(*m_out, " id=\"%" PRId64 "\"", object.id());

                    if (m_add_metadata) {
                        if (object.version()) {
                            oprintf(*m_out, " version=\"%d\"", object.version());
                        }

                        if (object.timestamp()) {
                            *m_out += " timestamp=\"";
                            *m_out += object.timestamp().to_iso();
                            *m_out += "\"";
                        }

                        if (!object.user_is_anonymous()) {
                            oprintf(*m_out, " uid=\"%d\" user=\"", object.uid());
                            xml_string(*m_out, object.user());
                            *m_out += "\"";
                        }

                        if (object.changeset()) {
                            oprintf(*m_out, " changeset=\"%d\"", object.changeset());
                        }

                        if (m_write_visible_flag) {
                            if (object.visible()) {
                                *m_out += " visible=\"true\"";
                            } else {
                                *m_out += " visible=\"false\"";
                            }
                        }
                    }
                }

                void write_tags(const osmium::TagList& tags, int spaces) {
                    for (const auto& tag : tags) {
                        write_spaces(spaces);
                        *m_out += "  <tag k=\"";
                        xml_string(*m_out, tag.key());
                        *m_out += "\" v=\"";
                        xml_string(*m_out, tag.value());
                        *m_out += "\"/>\n";
                    }
                }

                void write_discussion(const osmium::ChangesetDiscussion& comments) {
                    for (const auto& comment : comments) {
                        oprintf(*m_out, "   <comment uid=\"%d\" user=\"", comment.uid());
                        xml_string(*m_out, comment.user());
                        *m_out += "\" date=\"";
                        *m_out += comment.date().to_iso();
                        *m_out += "\">\n";
                        *m_out += "    <text>";
                        xml_string(*m_out, comment.text());
                        *m_out += "</text>\n   </comment>\n";
                    }
                    *m_out += "  </discussion>\n";
                }

                void open_close_op_tag(const operation op = operation::op_none) {
                    if (op == m_last_op) {
                        return;
                    }

                    switch (m_last_op) {
                        case operation::op_none:
                            break;
                        case operation::op_create:
                            *m_out += "  </create>\n";
                            break;
                        case operation::op_modify:
                            *m_out += "  </modify>\n";
                            break;
                        case operation::op_delete:
                            *m_out += "  </delete>\n";
                            break;
                    }

                    switch (op) {
                        case operation::op_none:
                            break;
                        case operation::op_create:
                            *m_out += "  <create>\n";
                            break;
                        case operation::op_modify:
                            *m_out += "  <modify>\n";
                            break;
                        case operation::op_delete:
                            *m_out += "  <delete>\n";
                            break;
                    }

                    m_last_op = op;
                }

            public:

                explicit XMLOutputBlock(osmium::memory::Buffer&& buffer, bool add_metadata, bool write_visible_flag, bool write_change_ops) :
                    m_input_buffer(std::make_shared<osmium::memory::Buffer>(std::move(buffer))),
                    m_out(std::make_shared<std::string>()),
                    m_add_metadata(add_metadata),
                    m_write_visible_flag(write_visible_flag && !write_change_ops),
                    m_write_change_ops(write_change_ops) {
                }

                XMLOutputBlock(const XMLOutputBlock&) = default;
                XMLOutputBlock& operator=(const XMLOutputBlock&) = default;

                XMLOutputBlock(XMLOutputBlock&&) = default;
                XMLOutputBlock& operator=(XMLOutputBlock&&) = default;

                ~XMLOutputBlock() = default;

                std::string operator()() {
                    osmium::apply(m_input_buffer->cbegin(), m_input_buffer->cend(), *this);

                    if (m_write_change_ops) {
                        open_close_op_tag();
                    }

                    std::string out;
                    std::swap(out, *m_out);
                    return out;
                }

                void node(const osmium::Node& node) {
                    if (m_write_change_ops) {
                        open_close_op_tag(node.visible() ? (node.version() == 1 ? operation::op_create : operation::op_modify) : operation::op_delete);
                    }

                    write_prefix();
                    *m_out += "<node";

                    write_meta(node);

                    if (node.location()) {
                        *m_out += " lat=\"";
                        osmium::util::double2string(std::back_inserter(*m_out), node.location().lat_without_check(), 7);
                        *m_out += "\" lon=\"";
                        osmium::util::double2string(std::back_inserter(*m_out), node.location().lon_without_check(), 7);
                        *m_out += "\"";
                    }

                    if (node.tags().empty()) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    write_tags(node.tags(), prefix_spaces());

                    write_prefix();
                    *m_out += "</node>\n";
                }

                void way(const osmium::Way& way) {
                    if (m_write_change_ops) {
                        open_close_op_tag(way.visible() ? (way.version() == 1 ? operation::op_create : operation::op_modify) : operation::op_delete);
                    }

                    write_prefix();
                    *m_out += "<way";
                    write_meta(way);

                    if (way.tags().empty() && way.nodes().empty()) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    for (const auto& node_ref : way.nodes()) {
                        write_prefix();
                        oprintf(*m_out, "  <nd ref=\"%" PRId64 "\"/>\n", node_ref.ref());
                    }

                    write_tags(way.tags(), prefix_spaces());

                    write_prefix();
                    *m_out += "</way>\n";
                }

                void relation(const osmium::Relation& relation) {
                    if (m_write_change_ops) {
                        open_close_op_tag(relation.visible() ? (relation.version() == 1 ? operation::op_create : operation::op_modify) : operation::op_delete);
                    }

                    write_prefix();
                    *m_out += "<relation";
                    write_meta(relation);

                    if (relation.tags().empty() && relation.members().empty()) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    for (const auto& member : relation.members()) {
                        write_prefix();
                        *m_out += "  <member type=\"";
                        *m_out += item_type_to_name(member.type());
                        oprintf(*m_out, "\" ref=\"%" PRId64 "\" role=\"", member.ref());
                        xml_string(*m_out, member.role());
                        *m_out += "\"/>\n";
                    }

                    write_tags(relation.tags(), prefix_spaces());

                    write_prefix();
                    *m_out += "</relation>\n";
                }

                void changeset(const osmium::Changeset& changeset) {
                    *m_out += " <changeset";

                    oprintf(*m_out, " id=\"%" PRId32 "\"", changeset.id());

                    if (changeset.created_at()) {
                        *m_out += " created_at=\"";
                        *m_out += changeset.created_at().to_iso();
                        *m_out += "\"";
                    }

                    if (changeset.closed_at()) {
                        *m_out += " closed_at=\"";
                        *m_out += changeset.closed_at().to_iso();
                        *m_out += "\" open=\"false\"";
                    } else {
                        *m_out += " open=\"true\"";
                    }

                    if (!changeset.user_is_anonymous()) {
                        *m_out += " user=\"";
                        xml_string(*m_out, changeset.user());
                        oprintf(*m_out, "\" uid=\"%d\"", changeset.uid());
                    }

                    if (changeset.bounds()) {
                        oprintf(*m_out, " min_lat=\"%.7f\"", changeset.bounds().bottom_left().lat_without_check());
                        oprintf(*m_out, " min_lon=\"%.7f\"", changeset.bounds().bottom_left().lon_without_check());
                        oprintf(*m_out, " max_lat=\"%.7f\"", changeset.bounds().top_right().lat_without_check());
                        oprintf(*m_out, " max_lon=\"%.7f\"", changeset.bounds().top_right().lon_without_check());
                    }

                    oprintf(*m_out, " num_changes=\"%" PRId32 "\"", changeset.num_changes());
                    oprintf(*m_out, " comments_count=\"%" PRId32 "\"", changeset.num_comments());

                    // If there are no tags and no comments, we can close the
                    // tag right here and are done.
                    if (changeset.tags().empty() && changeset.num_comments() == 0) {
                        *m_out += "/>\n";
                        return;
                    }

                    *m_out += ">\n";

                    write_tags(changeset.tags(), 0);

                    if (changeset.num_comments() > 0) {
                        *m_out += "  <discussion>\n";
                        write_discussion(changeset.discussion());
                    }

                    *m_out += " </changeset>\n";
                }

            }; // class XMLOutputBlock

            class XMLOutputFormat : public osmium::io::detail::OutputFormat, public osmium::handler::Handler {

                bool m_add_metadata;
                bool m_write_visible_flag;

            public:

                XMLOutputFormat(const osmium::io::File& file, data_queue_type& output_queue) :
                    OutputFormat(file, output_queue),
                    m_add_metadata(file.get("add_metadata") != "false"),
                    m_write_visible_flag(file.has_multiple_object_versions() || m_file.is_true("force_visible_flag")) {
                }

                XMLOutputFormat(const XMLOutputFormat&) = delete;
                XMLOutputFormat& operator=(const XMLOutputFormat&) = delete;

                ~XMLOutputFormat() override final {
                }

                void write_buffer(osmium::memory::Buffer&& buffer) override final {
                    m_output_queue.push(osmium::thread::Pool::instance().submit(XMLOutputBlock{std::move(buffer), m_add_metadata, m_write_visible_flag, m_file.is_true("xml_change_format")}));
                }

                void write_header(const osmium::io::Header& header) override final {
                    std::string out = "<?xml version='1.0' encoding='UTF-8'?>\n";

                    if (m_file.is_true("xml_change_format")) {
                        out += "<osmChange version=\"0.6\" generator=\"";
                        xml_string(out, header.get("generator").c_str());
                        out += "\">\n";
                    } else {
                        out += "<osm version=\"0.6\"";

                        std::string xml_josm_upload = header.get("xml_josm_upload");
                        if (xml_josm_upload == "true" || xml_josm_upload == "false") {
                            out += " upload=\"";
                            out += xml_josm_upload;
                            out += "\"";
                        }
                        out += " generator=\"";
                        xml_string(out, header.get("generator").c_str());
                        out += "\">\n";
                    }

                    for (const auto& box : header.boxes()) {
                        out += "  <bounds";
                        oprintf(out, " minlon=\"%.7f\"", box.bottom_left().lon());
                        oprintf(out, " minlat=\"%.7f\"", box.bottom_left().lat());
                        oprintf(out, " maxlon=\"%.7f\"", box.top_right().lon());
                        oprintf(out, " maxlat=\"%.7f\"/>\n", box.top_right().lat());
                    }

                    std::promise<std::string> promise;
                    m_output_queue.push(promise.get_future());
                    promise.set_value(std::move(out));
                }

                void close() override final {
                    {
                        std::string out;
                        if (m_file.is_true("xml_change_format")) {
                            out += "</osmChange>\n";
                        } else {
                            out += "</osm>\n";
                        }

                        std::promise<std::string> promise;
                        m_output_queue.push(promise.get_future());
                        promise.set_value(std::move(out));
                    }

                    std::promise<std::string> promise;
                    m_output_queue.push(promise.get_future());
                    promise.set_value(std::string());
                }

            }; // class XMLOutputFormat

            namespace {

// we want the register_output_format() function to run, setting the variable
// is only a side-effect, it will never be used
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
                const bool registered_xml_output = osmium::io::detail::OutputFormatFactory::instance().register_output_format(osmium::io::file_format::xml,
                    [](const osmium::io::File& file, data_queue_type& output_queue) {
                        return new osmium::io::detail::XMLOutputFormat(file, output_queue);
                });
#pragma GCC diagnostic pop

            } // anonymous namespace

        } // namespace detail

    } // namespace output

} // namespace osmium

#endif // OSMIUM_IO_DETAIL_XML_OUTPUT_FORMAT_HPP
