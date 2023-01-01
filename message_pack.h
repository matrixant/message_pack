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

// Limit the depth of recursive functions
#define _RECURSION_MAX_DEPTH 32
// Default maximum message size in bytes: 8MB
#define _MSG_MAX_SIZE (1 << 23)
// Default maximum node size
#define _NODE_MAX_SIZE (1 << 20)
// String length limit: 1MB
#define _STR_MAX_SIZE (1 << 20)
// Binary data size limit in bytes: 1MB
#define _BIN_MAX_SIZE (1 << 20)

class MessagePack : public Object {
	GDCLASS(MessagePack, Object);

	static Variant _read_recursive(mpack_reader_t &p_reader, int p_depth);
	static void _write_recursive(mpack_writer_t &p_writer, Variant p_val, int p_depth);

protected:
	static void _bind_methods();

	Variant data;
	String err_msg;
	mpack_tree_t tree;
	bool started = false;

	Callable stream_reader;

	static Error _got_error_or_not(mpack_error_t p_err, String &r_err_str);
	Variant _parse_node_recursive(mpack_node_t p_node, int p_depth);

	static size_t _read_stream(mpack_tree_t *p_tree, char *r_buffer, size_t p_count);

public:
	static Array decode(const PackedByteArray &p_msg_buf);
	static Array encode(const Variant &p_val);

	Error start_stream(const Callable &r_stream_reader, int p_msgs_max = _MSG_MAX_SIZE);
	Error update_stream();
	Error reset_stream(int p_msgs_max = _MSG_MAX_SIZE);
	virtual PackedByteArray _get_stream_data(int p_len);

	inline Variant get_data() const { return data; }
	inline int get_bytes_remaining() const { return tree.data_length; }
	inline String get_error_message() const { return err_msg; }

	MessagePack();
	~MessagePack();
};

#endif // MESSAGE_PACK_H
