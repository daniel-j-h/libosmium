#ifndef OSMIUM_OSM_RELATION_HPP
#define OSMIUM_OSM_RELATION_HPP

/*

This file is part of Osmium (http://osmcode.org/osmium).

Copyright 2013 Jochen Topf <jochen@topf.org> and others (see README).

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

#include <osmium/osm/object.hpp>

namespace osmium {

    class RelationMember : private osmium::memory::detail::ItemHelper {

        object_id_type m_ref;
        item_type m_type;
        uint32_t m_flags;

        RelationMember(const RelationMember&) = delete;
        RelationMember(RelationMember&&) = delete;

        RelationMember& operator=(const RelationMember&) = delete;
        RelationMember& operator=(RelationMember&&) = delete;

        char* role_position() {
            return self() + sizeof(RelationMember);
        }

        const char* role_position() const {
            return self() + sizeof(RelationMember);
        }

        char* endpos() {
            char* current = self() + sizeof(RelationMember);
            return current + sizeof(size_t) + osmium::memory::padded_length(*reinterpret_cast<size_t*>(current));
        }

        const char* endpos() const {
            const char* current = self() + sizeof(RelationMember);
            return current + sizeof(size_t) + osmium::memory::padded_length(*reinterpret_cast<const size_t*>(current));
        }

        template <class TMember>
        friend class osmium::memory::CollectionIterator;

        char* next() {
            if (full_member()) {
                return endpos() + reinterpret_cast<osmium::memory::Item*>(endpos())->size();
            } else {
                return endpos();
            }
        }

        const char* next() const {
            if (full_member()) {
                return endpos() + reinterpret_cast<const osmium::memory::Item*>(endpos())->size();
            } else {
                return endpos();
            }
        }

    public:

        static constexpr item_type collection_type = item_type::relation_member_list;

        RelationMember(const object_id_type ref=0, const item_type type=item_type(), const bool full=false) :
            m_ref(ref),
            m_type(type),
            m_flags(full ? 1 : 0) {
        }

        object_id_type ref() const {
            return m_ref;
        }

        unsigned_object_id_type positive_ref() const {
            return std::abs(m_ref);
        }

        item_type type() const {
            return m_type;
        }

        bool full_member() const {
            return m_flags == 1;
        }

        const char* role() const {
            return role_position() + sizeof(size_t);
        }

        Object& get_object() {
            return *reinterpret_cast<Object*>(endpos());
        }

        const Object& get_object() const {
            return *reinterpret_cast<const Object*>(endpos());
        }

    }; // class RelationMember

    class RelationMemberList : public osmium::memory::Collection<RelationMember> {

    public:

        static constexpr osmium::item_type itemtype = osmium::item_type::relation_member_list;

        RelationMemberList() :
            osmium::memory::Collection<RelationMember>() {
        }

    }; // class RelationMemberList


    class Relation : public Object {

        friend class osmium::memory::ObjectBuilder<osmium::Relation>;

        Relation() :
            Object(sizeof(Relation), osmium::item_type::relation) {
        }

    public:

        static constexpr osmium::item_type itemtype = osmium::item_type::relation;

        RelationMemberList& members() {
            return subitem_of_type<RelationMemberList>();
        }

        const RelationMemberList& members() const {
            return subitem_of_type<const RelationMemberList>();
        }

    }; // class Relation

    static_assert(sizeof(Relation) % osmium::memory::align_bytes == 0, "Class osmium::Relation has wrong size to be aligned properly!");

} // namespace osmium

#endif // OSMIUM_OSM_RELATION_HPP
