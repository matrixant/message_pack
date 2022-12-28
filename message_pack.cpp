/*************************************************************************/
/*  message_pack.cpp                                                     */
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

#include "message_pack.h"
#include "core/os/memory.h"

Variant MessagePack::_read_recursive(mpack_reader_t &reader, int depth) {
	// critical check!
	if (depth >= MPACK_NODE_MAX_DEPTH_WITHOUT_MALLOC) {
		mpack_reader_flag_error(&reader, mpack_error_too_big);
		ERR_FAIL_COND_V_MSG(depth >= MPACK_NODE_MAX_DEPTH_WITHOUT_MALLOC,
				Variant(), "Parse recursive too deep.");
	}

	mpack_tag_t tag = mpack_read_tag(&reader);
	if (mpack_reader_error(&reader) != mpack_ok) {
		return Variant();
	}

	switch (mpack_tag_type(&tag)) {
		case mpack_type_nil:
			return Variant();
		case mpack_type_bool:
			return mpack_tag_bool_value(&tag);
			break;
		case mpack_type_int:
			return mpack_tag_int_value(&tag);
			break;
		case mpack_type_uint:
			return mpack_tag_uint_value(&tag);
			break;
		case mpack_type_float:
			return mpack_tag_float_value(&tag);
			break;
		case mpack_type_double:
			return mpack_tag_double_value(&tag);
			break;
		case mpack_type_str: {
			// NOTE: Use utf8 encoding
			String str;
			uint32_t len = mpack_tag_str_length(&tag);
			// critical check! limit length to avoid a huge allocation
			if (len >= STR_MAX_SIZE) {
				mpack_reader_flag_error(&reader, mpack_error_too_big);
				return str;
			}
			const char *buf = mpack_read_bytes_inplace(&reader, len);
			if (mpack_reader_error(&reader) == mpack_ok) {
				if (len > 0) {
					// NOTE: Use utf8 encoding
					str.parse_utf8(buf, len);
				}
			}
			return str;
		} break;
		case mpack_type_bin: {
			PackedByteArray bin_buf;
			uint32_t len = mpack_tag_bin_length(&tag);
			// critical check! limit length to avoid a huge allocation
			if (len >= BIN_MAX_SIZE) {
				mpack_reader_flag_error(&reader, mpack_error_too_big);
				return bin_buf;
			}
			const char *buf = mpack_read_bytes_inplace(&reader, len);
			if (mpack_reader_error(&reader) == mpack_ok) {
				if (bin_buf.resize(len) != OK) {
					mpack_reader_flag_error(&reader, mpack_error_memory);
					return bin_buf;
				}
				if (len > 0) {
					memcpy(bin_buf.ptrw(), buf, len);
				}
			}
			return bin_buf;
		} break;
		case mpack_type_array: {
			Array arr;
			uint32_t cnt = mpack_tag_array_count(&tag);
			arr.resize(cnt);
			for (uint32_t i = 0; i < cnt; i++) {
				arr[i] = _read_recursive(reader, depth + 1);
				if (mpack_reader_error(&reader) != mpack_ok) {
					break;
				}
			}
			return arr;
		} break;
		case mpack_type_map: {
			Dictionary map;
			uint32_t cnt = mpack_tag_map_count(&tag);
			Variant key, val;
			for (uint32_t i = 0; i < cnt; i++) {
				map = _read_recursive(reader, depth + 1);
				val = _read_recursive(reader, depth + 1);
				map[key] = val;
				if (mpack_reader_error(&reader) != mpack_ok) {
					break;
				}
			}
			return map;
		} break;
#if MPACK_EXTENSIONS
		case mpack_type_ext:
			break;
#endif
		default:
			return Variant();
	}
}

void MessagePack::_write_recursive(mpack_writer_t &writer, Variant val, int depth) {
	// critical check!
	if (depth >= MPACK_NODE_MAX_DEPTH_WITHOUT_MALLOC) {
		mpack_writer_flag_error(&writer, mpack_error_too_big);
		ERR_FAIL_COND_MSG(depth >= MPACK_NODE_MAX_DEPTH_WITHOUT_MALLOC, "Write recursive too deep.");
	}

	switch (val.get_type()) {
		case Variant::Type::NIL:
			mpack_write_nil(&writer);
			break;
		case Variant::Type::BOOL:
			mpack_write_bool(&writer, val);
			break;
		case Variant::Type::INT:
			mpack_write_int(&writer, val);
			break;
		case Variant::Type::FLOAT: {
			double f = val;
			//float range: -FLT_MAX ~ -FLT_MIN and FLT_MIN ~ FLT_MAX
			if ((f > -FLT_MAX && f < -FLT_MIN) || (f > FLT_MIN && f < FLT_MAX)) {
				// single precision float
				mpack_write_float(&writer, val);
			} else {
				// double precision float
				mpack_write_double(&writer, val);
			}
		} break;
		case Variant::Type::STRING: {
			// NOTE: Use utf8 encoding
			PackedByteArray str_buf = String(val).to_utf8_buffer();
			mpack_write_str(&writer, (const char *)str_buf.ptr(), str_buf.size());
		} break;
		case Variant::Type::PACKED_BYTE_ARRAY: {
			// NOTE: When pack bin data, it must be typed as a PackedByteArray
			// And if you want pack an array contains integer to the message pack,
			// don't use PackedByteArray, because it will be treated as a binary data buffer.
			PackedByteArray bin_buf = val;
			mpack_write_bin(&writer, (const char *)bin_buf.ptr(), bin_buf.size());
		} break;
		case Variant::Type::ARRAY: {
			// NOTE: Not typed array will be processed as a variable array to message pack.
			// But the elements in array which type is unsupported will be treated as a nil.
			Array arr = val;
			mpack_start_array(&writer, arr.size());
			for (int i = 0; i < arr.size(); i++) {
				_write_recursive(writer, arr[i], depth + 1);
			}
			mpack_finish_array(&writer);
		} break;
		case Variant::Type::PACKED_INT32_ARRAY:
		case Variant::Type::PACKED_INT64_ARRAY: {
			PackedInt64Array arr = val;
			mpack_start_array(&writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_int(&writer, arr[i]);
			}
			mpack_finish_array(&writer);
		} break;
		case Variant::Type::PACKED_FLOAT32_ARRAY: {
			PackedFloat32Array arr = val;
			mpack_start_array(&writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_float(&writer, arr[i]);
			}
			mpack_finish_array(&writer);
		} break;
		case Variant::Type::PACKED_FLOAT64_ARRAY: {
			PackedFloat64Array arr = val;
			mpack_start_array(&writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_double(&writer, arr[i]);
			}
			mpack_finish_array(&writer);
		} break;
		case Variant::Type::PACKED_STRING_ARRAY: {
			PackedStringArray arr = val;
			mpack_start_array(&writer, arr.size());
			PackedByteArray str_buf;
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				// NOTE: Use utf8 encoding
				str_buf = arr[i].to_utf8_buffer();
				mpack_write_str(&writer, (const char *)str_buf.ptr(), str_buf.size());
			}
			mpack_finish_array(&writer);
		} break;
		case Variant::Type::DICTIONARY: {
			Dictionary dict = val;
			Array keys = dict.keys();
			Array vals = dict.values();
			mpack_start_map(&writer, keys.size());
			for (int i = 0; i < keys.size(); i++) {
				// Key
				_write_recursive(writer, keys[i], depth + 1);
				// Value
				_write_recursive(writer, vals[i], depth + 1);
			}
			mpack_finish_map(&writer);
		} break;

		// TODO: support ext type
		default:
			// Unknown type
			mpack_write_nil(&writer);
			break;
	}
}

Variant MessagePack::_parse_node_recursive(mpack_node_t node, int depth) {
	// critical check!
	if (depth >= MPACK_NODE_MAX_DEPTH_WITHOUT_MALLOC) {
		mpack_tree_flag_error(node.tree, mpack_error_too_big);
		ERR_FAIL_COND_V_MSG(depth >= MPACK_NODE_MAX_DEPTH_WITHOUT_MALLOC,
				Variant(), "Parse recursive too deep.");
	}

	switch (node.data->type) {
		case mpack_type_nil:
			mpack_node_nil(node);
			return Variant();
			break;
		case mpack_type_bool:
			return mpack_node_bool(node);
			break;
		case mpack_type_int:
			return mpack_node_int(node);
			break;
		case mpack_type_uint:
			return mpack_node_uint(node);
			break;
		case mpack_type_float:
			return mpack_node_float(node);
			break;
		case mpack_type_double:
			return mpack_node_double(node);
			break;
		case mpack_type_str: {
			uint32_t len = mpack_node_strlen(node);
			String str;
			if (len > 0) {
				str.parse_utf8(mpack_node_str(node), len);
			}
			return str;
		} break;
		case mpack_type_bin: {
			uint32_t len = mpack_node_bin_size(node);
			PackedByteArray bin_buf;
			if (bin_buf.resize(len) != OK) {
				mpack_tree_flag_error(node.tree, mpack_error_too_big);
				return bin_buf;
			} else if (len > 0) {
				memcpy(bin_buf.ptrw(), mpack_node_bin_data(node), len);
			}
			return bin_buf;
		} break;
		case mpack_type_array: {
			uint32_t len = mpack_node_array_length(node);
			Array arr;
			arr.resize(len);
			for (uint32_t i = 0; i < len; i++) {
				arr[i] = _parse_node_recursive(mpack_node_array_at(node, i), depth + 1);
			}
			return arr;
		} break;
		case mpack_type_map: {
			uint32_t len = mpack_node_map_count(node);
			Dictionary map;
			Variant key, val;
			for (uint32_t i = 0; i < len; i++) {
				key = _parse_node_recursive(mpack_node_map_key_at(node, i), depth + 1);
				val = _parse_node_recursive(mpack_node_map_value_at(node, i), depth + 1);
				map[key] = val;
			}
			return map;
		} break;
#if MPACK_EXTENSIONS
		case mpack_type_ext:
			break;
#endif
		default:
			break;
	}
	return Variant();
}

Error MessagePack::_got_error_or_not(mpack_error_t err, String &_err_str) {
	switch (err) {
		case mpack_ok:
			_err_str = "";
			return OK;
		case mpack_error_io:
			_err_str = "The reader or writer failed to fill or flush, or some other file or socket error occurred.";
			return ERR_UNAVAILABLE;
		case mpack_error_invalid:
			_err_str = "The data read is not valid MessagePack.";
			return ERR_INVALID_DATA;
		case mpack_error_unsupported:
			_err_str = "The data read is not supported by this configuration of MPack. (See @ref MPACK_EXTENSIONS.)";
			return ERR_UNCONFIGURED;
		case mpack_error_type:
			_err_str = "The type or value range did not match what was expected by the caller.";
			return ERR_PARSE_ERROR;
		case mpack_error_too_big:
			_err_str = "A read or write was bigger than the maximum size allowed for that operation.";
			return ERR_OUT_OF_MEMORY;
		case mpack_error_memory:
			_err_str = "An allocation failure occurred.";
			return FAILED;
		case mpack_error_bug:
			_err_str = "The MPack API was used incorrectly. (This will always assert in debug mode.)";
			return ERR_BUG;
		case mpack_error_data:
			_err_str = "The contained data is not valid.";
			return ERR_INVALID_DATA;
		case mpack_error_eof:
			_err_str = "The reader failed to read because of file or socket EOF.";
			return ERR_FILE_EOF;
	}
	_err_str = "Unknown error.";
	return FAILED;
}

Array MessagePack::unpack(const PackedByteArray &msg_buf) {
	mpack_reader_t reader;
	const char *raw_buf = (const char *)(msg_buf.to_byte_array().ptr());
	mpack_reader_init_data(&reader, raw_buf, msg_buf.size());

	Variant val = _read_recursive(reader, 0);

	int err_idx = 0;
	if (mpack_reader_error(&reader) != mpack_ok) {
		err_idx = int(reader.end - raw_buf);
	}

	String _err_str = "";
	Error err = _got_error_or_not(mpack_reader_destroy(&reader), _err_str);

	Array result;
	if (err == OK) {
		result.resize(2);
		result[0] = err;
		result[1] = val;
	} else {
		result.resize(3);
		result[0] = err;
		result[1] = _err_str;
		result[2] = err_idx;
	}
	return result;
}

Array MessagePack::pack(const Variant &val) {
	String _err_str = "";

	char *buf;
	size_t size;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &buf, &size);

	_write_recursive(writer, val, 0);
	Error err = _got_error_or_not(mpack_writer_destroy(&writer), _err_str);

	PackedByteArray msg_buf;
	if (msg_buf.resize(size) != OK) {
		mpack_writer_flag_error(&writer, mpack_error_memory);
		err = _got_error_or_not(mpack_error_memory, _err_str);
	} else if (size > 0) {
		memcpy(msg_buf.ptrw(), buf, size);
	}

	free(buf);

	Array result;
	result.resize(2);
	result[0] = err;
	if (err == OK) {
		result[1] = msg_buf;
	} else {
		result[1] = _err_str;
	}
	return result;
}

static size_t read_stream(mpack_tree_t *tree, char *buffer, size_t count) {
	MessagePack *msgpack = (MessagePack *)mpack_tree_context(tree);
	PackedByteArray stream_buf = msgpack->_get_stream_data(count);
	if (Variant(stream_buf).get_type() == Variant::Type::NIL) {
		mpack_tree_flag_error(tree, mpack_error_io);
		return -1;
	}
	memcpy(buffer, stream_buf.ptr(), stream_buf.size());
	return stream_buf.size();
}

Error MessagePack::init_stream_reader(Callable reader, size_t msgs_max, size_t nodes_max) {
	ERR_FAIL_COND_V_MSG(reader.is_null(),
			ERR_UNAVAILABLE, String("An error occurred while call reader: ") + reader.get_method());
	err_str = "";
	remaining = 0;

	this->stream_reader = reader;
	mpack_tree_init_stream(&tree, &read_stream, this, msgs_max, nodes_max);
	return OK;
}

Error MessagePack::update_stream() {
	mpack_tree_parse(&tree);
	remaining = tree.data_length;
	if (mpack_tree_error(&tree) != mpack_ok) {
		_err_print_error(FUNCTION_STR, __FILE__, __LINE__, "Parse failed.", err_str);
		return _got_error_or_not(mpack_tree_error(&tree), err_str);
	}
	mpack_node_t root = mpack_tree_root(&tree);
	data = _parse_node_recursive(root, 0);
	return OK;
}

Error MessagePack::reset_stream(size_t msgs_max, size_t nodes_max) {
	ERR_FAIL_COND_V_MSG(stream_reader.is_null(),
			ERR_UNAVAILABLE, String("An error occurred while call reader: ") + stream_reader.get_method());
	mpack_tree_destroy(&tree);
	err_str = "";
	remaining = 0;

	mpack_tree_init_stream(&tree, &read_stream, this, msgs_max, nodes_max);
	return OK;
}

PackedByteArray MessagePack::_get_stream_data(size_t len) {
	const Variant param = len;
	const Variant *param_ptr = &param;
	Variant result;
	Callable::CallError err;
	stream_reader.callp((const Variant **)&param_ptr, 1, result, err);
	ERR_FAIL_COND_V_MSG(err.error != Callable::CallError::CALL_OK,
			PackedByteArray(), "An error occurred while call stream_reader: " + String(stream_reader.get_method()));
	if (Variant::can_convert_strict(result.get_type(), Variant::Type::PACKED_BYTE_ARRAY)) {
		return PackedByteArray(result);
	}
	ERR_FAIL_V_MSG(PackedByteArray(), String(stream_reader.get_method()) + " returned an unsupported type.");
}

#if MPACK_EXTENSIONS
Error MessagePack::register_ext(int type_id, String &type_name, Callable pack_func, Callable unpack_func) {
	return Error();
}
#endif

MessagePack::MessagePack() {
}

MessagePack::~MessagePack() {
	mpack_tree_destroy(&tree);
}

void MessagePack::_bind_methods() {
	ClassDB::bind_static_method("MessagePack", D_METHOD("unpack", "msg_buf"), &MessagePack::unpack);
	ClassDB::bind_static_method("MessagePack", D_METHOD("pack", "data"), &MessagePack::pack);

	ClassDB::bind_method(D_METHOD("init_stream_reader", "reader", "msgs_max", "nodes_max"),
			&MessagePack::init_stream_reader, DEFVAL(MSG_MAX_SIZE), DEFVAL(NODE_MAX_SIZE));
	ClassDB::bind_method(D_METHOD("unpack_stream"), &MessagePack::update_stream);
	ClassDB::bind_method(D_METHOD("get_data"), &MessagePack::get_data);
	ClassDB::bind_method(D_METHOD("get_bytes_remaining"), &MessagePack::get_bytes_remaining);
	ClassDB::bind_method(D_METHOD("get_error_message"), &MessagePack::get_error_message);
}
