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

#ifdef GDEXTENSION
#include <godot_cpp/classes/os.hpp>

using namespace godot;
#else
#include "core/os/memory.h"
#endif

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
			break;
	}
	mpack_reader_flag_error(&p_reader, mpack_error_unsupported);
	ERR_FAIL_V_MSG(Variant(), "The data type [" + String::num_int64(mpack_tag_type(&tag)) + "] is unsupported.");
}

void MessagePack::_write_recursive(mpack_writer_t &p_writer, Variant p_val, int p_depth) {
	// critical check!
	if (p_depth >= _RECURSION_MAX_DEPTH) {
		mpack_writer_flag_error(&p_writer, mpack_error_too_big);
		ERR_FAIL_COND_MSG(p_depth >= _RECURSION_MAX_DEPTH, "Write recursive too deep.");
	}

	switch (p_val.get_type()) {
		case Variant::NIL:
			mpack_write_nil(&p_writer);
			break;
		case Variant::BOOL:
			mpack_write_bool(&p_writer, p_val);
			break;
		case Variant::INT:
			mpack_write_int(&p_writer, p_val);
			break;
		case Variant::FLOAT: {
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
		case Variant::STRING_NAME:
		case Variant::STRING: {
			// NOTE: Use utf8 encoding
			PackedByteArray str_buf = String(p_val).to_utf8_buffer();
			mpack_write_str(&p_writer, (const char *)str_buf.ptr(), str_buf.size());
		} break;
		case Variant::PACKED_BYTE_ARRAY: {
			// NOTE: When pack bin data, it must be typed as a PackedByteArray
			// And if you want pack an array contains integer to the message pack,
			// don't use PackedByteArray, because it will be treated as a binary data buffer.
			PackedByteArray bin_buf = p_val;
			mpack_write_bin(&p_writer, (const char *)bin_buf.ptr(), bin_buf.size());
		} break;
		case Variant::ARRAY: {
			// NOTE: Not typed array will be processed as a variable array to message pack.
			// But the elements in array which type is unsupported will be treated as a nil.
			Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			for (int i = 0; i < arr.size(); i++) {
				_write_recursive(p_writer, arr[i], p_depth + 1);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY: {
			PackedInt64Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_int(&p_writer, arr[i]);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::PACKED_FLOAT32_ARRAY: {
			PackedFloat32Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_float(&p_writer, arr[i]);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::PACKED_FLOAT64_ARRAY: {
			PackedFloat64Array arr = p_val;
			mpack_start_array(&p_writer, arr.size());
			// Typed array write elememt one by one.
			for (int i = 0; i < arr.size(); i++) {
				mpack_write_double(&p_writer, arr[i]);
			}
			mpack_finish_array(&p_writer);
		} break;
		case Variant::PACKED_STRING_ARRAY: {
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
		case Variant::DICTIONARY: {
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

		default:
			// Unsupported type
			mpack_write_nil(&p_writer);
			ERR_FAIL_MSG("The data type [" + Variant::get_type_name(p_val.get_type()) + "] is unsupported.");
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
#if MPACK_EXTENSIONS
		case mpack_type_ext: {
			int8_t ext = mpack_node_exttype(p_node);
			if (ext == MPACK_EXTTYPE_TIMESTAMP) {
				mpack_timestamp_t timestamp = mpack_node_timestamp(p_node);
				Dictionary timestamp_dict;
				timestamp_dict["seconds"] = timestamp.seconds;
				timestamp_dict["nanoseconds"] = timestamp.nanoseconds;
				return timestamp_dict;
			} else if (ext_decoder.has(ext)) {
				ERR_FAIL_COND_V_MSG(!ext_decoder[ext].is_valid(), Variant(), "Invalid extension type decoder.");
				uint32_t len = mpack_node_data_len(p_node);
				PackedByteArray ext_data;
				if (len > 0) {
					ext_data.resize(len);
					memcpy(ext_data.ptrw(), mpack_node_data(p_node), len);
				}
				Array params;
				params.resize(2);
				params[0] = ext;
				params[1] = ext_data;
				return ext_decoder[ext].callv(params);
			}
			ERR_FAIL_V_MSG(Variant(), "Unsupported extension type: " + String::num_int64(ext));
		} break;
#endif

		default:
			break;
	}
	ERR_FAIL_V_MSG(Variant(), "The data type [" + String::num_int64(p_node.data->type) + "] is unsupported.");
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

	::free(buf);

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
	size_t bytes_left = msgpack->stream_tail - msgpack->stream_head;
	size_t read_size = MIN(p_count, bytes_left);
	const uint8_t *stream_ptr = msgpack->stream_data.ptr();
	if (read_size > 0) {
		memcpy(r_buffer, stream_ptr + msgpack->stream_head, read_size);
		msgpack->stream_head += read_size;
	}
	return read_size;
}

void MessagePack::start_stream_with_reader(const Callback p_reader, void *context, int p_msgs_max) {
	if (started) {
		mpack_tree_destroy(&tree);
	}
	err_msg = "";
	data = Variant();
	mpack_tree_init_stream(&tree, p_reader, context, p_msgs_max, _NODE_MAX_SIZE);
	started = true;
}

Error MessagePack::try_parse_stream() {
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

void MessagePack::start_stream(int p_msgs_max) {
	start_stream_with_reader(_read_stream, this, p_msgs_max);
}

Error MessagePack::update_stream(const PackedByteArray &p_data, int p_from, int p_to) {
	ERR_FAIL_COND_V_MSG(p_from > p_to, ERR_INVALID_PARAMETER, "Index 'to' must be greater than 'from'.");
	ERR_FAIL_COND_V_MSG(p_from >= p_data.size(), ERR_INVALID_PARAMETER, "Index from " + String::num_int64(p_from) + "out of range of data which only has " + String::num_int64(p_data.size()) + " elements.");
	stream_data = p_data;
	stream_head = p_from;
	stream_tail = MIN(p_to, p_data.size());

	return try_parse_stream();
}

#if MPACK_EXTENSIONS
void MessagePack::register_extension_type(int p_ext_type, const Callable &p_decoder) {
	ERR_FAIL_COND_MSG(p_ext_type > 127, "Invalid extension type.");
	ext_decoder[p_ext_type] = p_decoder;
}
#endif

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

#if MPACK_EXTENSIONS
	ClassDB::bind_method(D_METHOD("register_extension_type", "type_id", "decoder"), &MessagePack::register_extension_type);
#endif

	ClassDB::bind_method(D_METHOD("start_stream", "msgs_max"), &MessagePack::start_stream, DEFVAL(_MSG_MAX_SIZE));
	ClassDB::bind_method(D_METHOD("update_stream", "data", "from", "to"), &MessagePack::update_stream, DEFVAL(0), DEFVAL(INT_MAX));
	ClassDB::bind_method(D_METHOD("get_data"), &MessagePack::get_data);
	ClassDB::bind_method(D_METHOD("get_current_stream_length"), &MessagePack::get_current_stream_length);
	ClassDB::bind_method(D_METHOD("get_error_message"), &MessagePack::get_error_message);
}
