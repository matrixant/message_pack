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

Variant MessagePack::_read_recursive(mpack_reader_t &p_reader, int p_depth) {
	// critical check!
	if (p_depth >= _RECURSION_MAX_DEPTH) {
		mpack_reader_flag_error(&p_reader, mpack_error_too_big);
		ERR_FAIL_COND_V_MSG(p_depth >= _RECURSION_MAX_DEPTH,
				Variant(), "Parse recursive too deep.");
	}

	mpack_tag_t tag = mpack_read_tag(&p_reader);
	if (mpack_reader_error(&p_reader) != mpack_ok) {
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
			if (len >= _STR_MAX_SIZE) {
				mpack_reader_flag_error(&p_reader, mpack_error_too_big);
				return str;
			}
			const char *buf = mpack_read_bytes_inplace(&p_reader, len);
			if (mpack_reader_error(&p_reader) == mpack_ok) {
				if (len > 0) {
					// NOTE: Use utf8 encoding
					str.parse_utf8(buf, len);
				}
			}
			mpack_done_str(&p_reader);
			return str;
		} break;
		case mpack_type_bin: {
			PackedByteArray bin_buf;
			uint32_t len = mpack_tag_bin_length(&tag);
			// critical check! limit length to avoid a huge allocation
			if (len >= _BIN_MAX_SIZE) {
				mpack_reader_flag_error(&p_reader, mpack_error_too_big);
				return bin_buf;
			}
			const char *buf = mpack_read_bytes_inplace(&p_reader, len);
			if (mpack_reader_error(&p_reader) == mpack_ok) {
				if (len > 0) {
					bin_buf.resize(len);
					memcpy(bin_buf.ptrw(), buf, len);
				}
			}
			mpack_done_bin(&p_reader);
			return bin_buf;
		} break;
		case mpack_type_array: {
			Array arr;
			uint32_t cnt = mpack_tag_array_count(&tag);
			if (cnt > 0) {
				arr.resize(cnt);
				for (uint32_t i = 0; i < cnt; i++) {
					arr[i] = _read_recursive(p_reader, p_depth + 1);
					if (mpack_reader_error(&p_reader) != mpack_ok) {
						break;
					}
				}
			}
			mpack_done_array(&p_reader);
			return arr;
		} break;
		case mpack_type_map: {
			Dictionary map;
			uint32_t cnt = mpack_tag_map_count(&tag);
			Variant key, val;
			for (uint32_t i = 0; i < cnt; i++) {
				key = _read_recursive(p_reader, p_depth + 1);
				val = _read_recursive(p_reader, p_depth + 1);
				map[key] = val;
				if (mpack_reader_error(&p_reader) != mpack_ok) {
					break;
				}
			}
			mpack_done_map(&p_reader);
			return map;
		} break;
		default:
			mpack_reader_flag_error(&p_reader, mpack_error_unsupported);
			break;
	}
	return Variant();
}

void MessagePack::_write_recursive(mpack_writer_t &p_writer, Variant p_val, int p_depth) {
	// critical check!
	if (p_depth >= _RECURSION_MAX_DEPTH) {
		mpack_writer_flag_error(&p_writer, mpack_error_too_big);
		ERR_FAIL_COND_MSG(p_depth >= _RECURSION_MAX_DEPTH, "Write recursive too deep.");
	}

	switch (p_val.get_type()) {
		case Variant::Type::NIL:
			mpack_write_nil(&p_writer);
			break;
		case Variant::Type::BOOL:
			mpack_write_bool(&p_writer, p_val);
			break;
		case Variant::Type::INT:
			mpack_write_int(&p_writer, p_val);
			break;
		case Variant::Type::FLOAT: {
			double d = p_val;
			float f = d;
			if (double(f) != d) {
				// double precision float
				mpack_write_double(&p_writer, p_val);
			} else {
				// single precision float
				mpack_write_float(&p_writer, p_val);
			}
		} break;
		case Variant::Type::STRING_NAME:
		case Variant::Type::STRING: {
			// NOTE: Use utf8 encoding
			PackedByteArray str_buf = String(p_val).to_utf8_buffer();
			mpack_write_str(&p_writer, (const char *)str_buf.ptr(), str_buf.size());
		} break;
		case Variant::Type::PACKED_BYTE_ARRAY: {
			// NOTE: When pack bin data, it must be typed as a PackedByteArray
			// And if you want pack an array contains integer to the message pack,
			// don't use PackedByteArray, because it will be treated as a binary data buffer.
			PackedByteArray bin_buf = p_val;
			mpack_write_bin(&p_writer, (const char *)bin_buf.ptr(), bin_buf.size());
		} break;
		case Variant::Type::ARRAY: {
			// NOTE: Not typed array will be processed as a variable array to message pack.
			// But the elements in array which type is unsupported will be treated as a nil.
			Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			for (int i = 0; i < arr.size(); i++) {
				_write_recursive(p_writer, arr[i], p_depth + 1);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::Type::PACKED_INT32_ARRAY:
		case Variant::Type::PACKED_INT64_ARRAY: {
			PackedInt64Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_int(&p_writer, arr[i]);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::Type::PACKED_FLOAT32_ARRAY: {
			PackedFloat32Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_float(&p_writer, arr[i]);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::Type::PACKED_FLOAT64_ARRAY: {
			PackedFloat64Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_double(&p_writer, arr[i]);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::Type::PACKED_STRING_ARRAY: {
			PackedStringArray arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			PackedByteArray str_buf;
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				// NOTE: Use utf8 encoding
				str_buf = arr[i].to_utf8_buffer();
				mpack_write_str(&p_writer, (const char *)str_buf.ptr(), str_buf.size());
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::Type::DICTIONARY: {
			Dictionary dict = p_val;
			Array keys = dict.keys();
			Array vals = dict.values();
			mpack_start_map(&p_writer, keys.size());
			for (int i = 0; i < keys.size(); i++) {
				// Key
				_write_recursive(p_writer, keys[i], p_depth + 1);
				// Value
				_write_recursive(p_writer, vals[i], p_depth + 1);
			}
			mpack_finish_map(&p_writer);
		} break;

		// TODO: support ext type
		default:
			// Unknown type
			mpack_write_nil(&p_writer);
			break;
	}
}

Variant MessagePack::_parse_node_recursive(mpack_node_t p_node, int p_depth) {
	// critical check!
	if (p_depth >= _RECURSION_MAX_DEPTH) {
		mpack_tree_flag_error(p_node.tree, mpack_error_too_big);
		ERR_FAIL_COND_V_MSG(p_depth >= _RECURSION_MAX_DEPTH,
				Variant(), "Parse recursive too deep.");
	}

	switch (p_node.data->type) {
		case mpack_type_nil:
			mpack_node_nil(p_node);
			return Variant();
			break;
		case mpack_type_bool:
			return mpack_node_bool(p_node);
			break;
		case mpack_type_int:
			return mpack_node_int(p_node);
			break;
		case mpack_type_uint:
			return mpack_node_uint(p_node);
			break;
		case mpack_type_float:
			return mpack_node_float(p_node);
			break;
		case mpack_type_double:
			return mpack_node_double(p_node);
			break;
		case mpack_type_str: {
			uint32_t len = mpack_node_strlen(p_node);
			String str;
			if (len > 0) {
				str.parse_utf8(mpack_node_str(p_node), len);
			}
			return str;
		} break;
		case mpack_type_bin: {
			uint32_t len = mpack_node_bin_size(p_node);
			PackedByteArray bin_buf;
			if (len > 0) {
				bin_buf.resize(len);
				memcpy(bin_buf.ptrw(), mpack_node_bin_data(p_node), len);
			}
			return bin_buf;
		} break;
		case mpack_type_array: {
			uint32_t len = mpack_node_array_length(p_node);
			Array arr;
			if (len > 0) {
				arr.resize(len);
				for (uint32_t i = 0; i < len; i++) {
					arr[i] = _parse_node_recursive(mpack_node_array_at(p_node, i), p_depth + 1);
				}
			}
			return arr;
		} break;
		case mpack_type_map: {
			uint32_t len = mpack_node_map_count(p_node);
			Dictionary map;
			Variant key, val;
			for (uint32_t i = 0; i < len; i++) {
				key = _parse_node_recursive(mpack_node_map_key_at(p_node, i), p_depth + 1);
				val = _parse_node_recursive(mpack_node_map_value_at(p_node, i), p_depth + 1);
				map[key] = val;
			}
			return map;
		} break;
		default:
			break;
	}
	return Variant();
}

Error MessagePack::_got_error_or_not(mpack_error_t p_err, String &r_err_str) {
	switch (p_err) {
		case mpack_ok:
			r_err_str = "";
			return OK;
		case mpack_error_io:
			r_err_str = "The reader or writer failed to fill or flush, or some other file or socket error occurred.";
			return ERR_UNAVAILABLE;
		case mpack_error_invalid:
			r_err_str = "The data read is not valid MessagePack.";
			return ERR_INVALID_DATA;
		case mpack_error_unsupported:
			r_err_str = "The data read is not supported by this configuration of MPack. (See @ref MPACK_EXTENSIONS.)";
			return ERR_UNCONFIGURED;
		case mpack_error_type:
			r_err_str = "The type or value range did not match what was expected by the caller.";
			return ERR_PARSE_ERROR;
		case mpack_error_too_big:
			r_err_str = "A read or write was bigger than the maximum size allowed for that operation.";
			return ERR_OUT_OF_MEMORY;
		case mpack_error_memory:
			r_err_str = "An allocation failure occurred.";
			return FAILED;
		case mpack_error_bug:
			r_err_str = "The MPack API was used incorrectly. (This will always assert in debug mode.)";
			return ERR_BUG;
		case mpack_error_data:
			r_err_str = "The contained data is not valid.";
			return ERR_INVALID_DATA;
		case mpack_error_eof:
			r_err_str = "The reader failed to read because of file or socket EOF.";
			return ERR_FILE_EOF;
	}
	r_err_str = "Unknown error.";
	return FAILED;
}

Array MessagePack::decode(const PackedByteArray &p_msg_buf) {
	mpack_reader_t reader;
	PackedByteArray msg_buf = p_msg_buf;
	const char *raw_ptr = (const char *)(msg_buf.ptr());
	mpack_reader_init_data(&reader, raw_ptr, p_msg_buf.size());

	Variant val = _read_recursive(reader, 0);

	int err_idx = 0;
	if (mpack_reader_error(&reader) != mpack_ok) {
		err_idx = int(reader.end - raw_ptr);
	}

	String err_str = "";
	Error err = _got_error_or_not(mpack_reader_destroy(&reader), err_str);

	Array result;
	if (err == OK) {
		result.resize(2);
		result[0] = err;
		result[1] = val;
	} else {
		result.resize(3);
		result[0] = err;
		result[1] = err_str;
		result[2] = err_idx;
	}
	return result;
}

Array MessagePack::encode(const Variant &p_val) {
	String err_str = "";

	char *buf;
	size_t size;
	mpack_writer_t writer;
	mpack_writer_init_growable(&writer, &buf, &size);

	_write_recursive(writer, p_val, 0);
	Error err = _got_error_or_not(mpack_writer_destroy(&writer), err_str);

	PackedByteArray msg_buf;
	if (size > 0) {
		msg_buf.resize(size);
		memcpy(msg_buf.ptrw(), buf, size);
	}

	free(buf);

	Array result;
	result.resize(2);
	result[0] = err;
	if (err == OK) {
		result[1] = msg_buf;
	} else {
		result[1] = err_str;
	}
	return result;
}

size_t MessagePack::_read_stream(mpack_tree_t *p_tree, char *r_buffer, size_t p_count) {
	MessagePack *msgpack = (MessagePack *)mpack_tree_context(p_tree);
	PackedByteArray stream_buf = msgpack->_get_stream_data(p_count);
	if (Variant(stream_buf).get_type() == Variant::Type::NIL) {
		return -1;
	}
	if (stream_buf.size() > 0) {
		memcpy(r_buffer, stream_buf.ptr(), stream_buf.size());
	}
	return stream_buf.size();
}

Error MessagePack::start_stream(const Callable &r_stream_reader, int p_msgs_max) {
	ERR_FAIL_COND_V_MSG(!r_stream_reader.is_valid(),
			ERR_METHOD_NOT_FOUND, "Stream reader is invalid, check and input a valid callable.");

	this->stream_reader = r_stream_reader;

	return reset_stream();
}

Error MessagePack::update_stream() {
	if (!mpack_tree_try_parse(&tree)) {
		// if false, error or wating.
		Error err = _got_error_or_not(mpack_tree_error(&tree), err_msg);
		ERR_FAIL_COND_V_MSG(err != OK, err, "Parse failed: " + err_msg);
		err_msg = "Waiting for new data.";
		return ERR_SKIP;
	}
	// if true, got data.
	mpack_node_t root = mpack_tree_root(&tree);
	data = _parse_node_recursive(root, 0);

	return OK;
}

Error MessagePack::reset_stream(int p_msgs_max) {
	ERR_FAIL_COND_V_MSG(stream_reader.is_null(),
			ERR_METHOD_NOT_FOUND, "Stream reader is invalid, call start_stream and input a valid callable.");
	if (started) {
		mpack_tree_destroy(&tree);
	}
	err_msg = "";

	mpack_tree_init_stream(&tree, &_read_stream, this, p_msgs_max, _NODE_MAX_SIZE);

	started = true;
	return OK;
}

PackedByteArray MessagePack::_get_stream_data(int p_len) {
	ERR_FAIL_COND_V_MSG(!stream_reader.is_valid(),
			Variant(), "Stream reader is invalid, call start_stream and input a valid callable.");
	const Variant param = p_len;
	const Variant *param_ptr = &param;
	Variant result;
	Callable::CallError err;
	stream_reader.callp((const Variant **)&param_ptr, 1, result, err);
	ERR_FAIL_COND_V_MSG(err.error != Callable::CallError::CALL_OK,
			Variant(), "An error occurred while call stream reader: " + String(stream_reader.get_method()));
	if (Variant::can_convert_strict(result.get_type(), Variant::Type::PACKED_BYTE_ARRAY)) {
		return PackedByteArray(result);
	}
	ERR_FAIL_V_MSG(Variant(), String(stream_reader.get_method()) + " returned an unsupported type.");
}

MessagePack::MessagePack() {
}

MessagePack::~MessagePack() {
	if (started) {
		mpack_tree_destroy(&tree);
	}
}

void MessagePack::_bind_methods() {
	ClassDB::bind_static_method("MessagePack", D_METHOD("decode", "msg_buf"), &MessagePack::decode);
	ClassDB::bind_static_method("MessagePack", D_METHOD("encode", "data"), &MessagePack::encode);

	ClassDB::bind_method(D_METHOD("start_stream", "stream_reader", "msgs_max"), &MessagePack::start_stream, DEFVAL(_MSG_MAX_SIZE));
	ClassDB::bind_method(D_METHOD("update_stream"), &MessagePack::update_stream);
	ClassDB::bind_method(D_METHOD("reset_stream"), &MessagePack::reset_stream);
	ClassDB::bind_method(D_METHOD("get_data"), &MessagePack::get_data);
	ClassDB::bind_method(D_METHOD("get_bytes_remaining"), &MessagePack::get_bytes_remaining);
	ClassDB::bind_method(D_METHOD("get_error_message"), &MessagePack::get_error_message);
}
