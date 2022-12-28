/*************************************************************************/
/*  message_pack.h                                                       */
/*************************************************************************/
/*                       This file is part of:                           */
/*                           GODOT ENGINE                                */
/*                      https://godotengine.org                          */
/*************************************************************************/
/* Copyright (c) 2007-2022 Juan Linietsky, Ariel Manzur.                 */
/* Copyright (c) 2014-2022 Godot Engine contributors (cf. AUTHORS.md).   */
/*                                                                       */
/* Permission is hereby granted, free of charge, to any person obtaining */
/* a copy of this software and associated documentation files (the       */
/* "Software"), to deal in the Software without restriction, including   */
/* without limitation the rights to use, copy, modify, merge, publish,   */
/* distribute, sublicense, and/or sell copies of the Software, and to    */
/* permit persons to whom the Software is furnished to do so, subject to */
/* the following conditions:                                             */
/*                                                                       */
/* The above copyright notice and this permission notice shall be        */
/* included in all copies or substantial portions of the Software.       */
/*                                                                       */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,       */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF    */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.*/
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY  */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,  */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE     */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                */
/*************************************************************************/

#ifndef MESSAGE_PACK_H
#define MESSAGE_PACK_H

#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/rb_map.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

#include "mpack/mpack.h"

#if MPACK_EXTENSIONS
struct ExtInfo {
	int type_id;
	String type_name;
	Callable pack_func;
	Callable unpack_func;
};
#endif

class MessagePack : public RefCounted {
	GDCLASS(MessagePack, RefCounted);

	Variant data;
	String err_str;
	int remaining = 0;

	mpack_tree_t tree;
	Callable stream_reader;

#if MPACK_EXTENSIONS
	// TODO: support ext type
	Vector<ExtInfo> ext_list;
#endif

	static Variant _read_recursive(mpack_reader_t &reader, int depth);
	static void _write_recursive(mpack_writer_t &writer, Variant val, int depth);
	Variant _parse_node_recursive(mpack_node_t node, int depth);

	static Error _got_error_or_not(mpack_error_t err, String &_err_str);

protected:
	static void _bind_methods();
	static constexpr size_t MSG_MAX_SIZE = 16 * 1024 * 1024;
	static constexpr size_t NODE_MAX_SIZE = 1024 * 1024;
	static constexpr size_t STR_MAX_SIZE = 1024 * 1024;
	static constexpr size_t BIN_MAX_SIZE = 1024 * 1024;

public:
	static Array unpack(const PackedByteArray &msg_buf);
	static Array pack(const Variant &val);

	Error init_stream_reader(Callable stream_reader,
			size_t msgs_max = MSG_MAX_SIZE,
			size_t nodes_max = NODE_MAX_SIZE);
	Error update_stream();
	Error reset_stream(size_t msgs_max = MSG_MAX_SIZE,
			size_t nodes_max = NODE_MAX_SIZE);
	PackedByteArray _get_stream_data(size_t len);

#if MPACK_EXTENSIONS
	// TODO: support ext type
	Error register_ext_type(int type_id, String &type_name, Callable pack_func, Callable unpack_func);
#endif

	inline Variant get_data() const { return data; }
	inline int get_bytes_remaining() const { return remaining; }
	inline String get_error_message() const { return err_str; }

	MessagePack();
	~MessagePack();
};

#endif // MESSAGE_PACK_H
