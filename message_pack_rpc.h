/*************************************************************************/
/*  message_pack_rpc.h                                                   */
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

#ifndef MESSAGE_PACK_RPC_H
#define MESSAGE_PACK_RPC_H

#ifdef GDEXTENSION
#include <godot_cpp/classes/stream_peer_tcp.hpp>
#include <godot_cpp/classes/mutex.hpp>
#include <godot_cpp/core/mutex_lock.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/builtin_types.hpp>

using namespace godot;
#else
#include "core/io/stream_peer_tcp.h"
#include "core/object/ref_counted.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"
#endif

#include "message_pack.h"

#include <atomic>
#include <thread>

// Max message buf size: 8MiB, should be way more than enough.
#define _MSG_BUF_MAX_SIZE (1 << 23)
// Max message queue size
#define _MSG_QUEUE_MAX_SIZE 2048

class MessagePackRPC : public Object {
	GDCLASS(MessagePackRPC, Object);

	MessagePack msg_pack;

	Mutex mutex;
	std::thread thread;
	bool running = false;
	bool connected = false;
	Ref<StreamPeerTCP> tcp_stream;

	List<Array> msg_queue;
	PackedByteArray out_buf;
	int out_tail = 0;
	int out_head = 0;
	PackedByteArray in_buf;
	int in_tail = 0;
	int in_head = 0;

	HashMap<String, Callable> request_map;
	HashMap<String, Callable> notify_map;

	uint64_t msgid = 0;

	bool sync_started = false;
	bool sync_responded;
	int sync_msgid;
	Array sync_result;

	void _error_handle(Error p_err, const String p_err_msg);
	Error _message_handle(const Variant &p_message);
	void _write_out();
	void _read_in();
	Error _try_connect(const String &p_ip, int p_port);
	void _start_stream(int p_msgs_max = _MSG_MAX_SIZE);
	Error _try_parse_stream();
	
#ifndef GDEXTENSION
	Array _sync_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Error _async_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Error _notify(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
#endif

	static size_t _stream_reader(mpack_tree_t *p_tree, char *r_buffer, size_t p_count);

protected:
	static void _bind_methods();

public:
	enum MessageType {
		REQUEST = 0,
		RESPONSE,
		NOTIFICATION,
	};

	static PackedByteArray make_message_byte_array(const Array &p_message);
	static PackedByteArray make_request(int p_msgid, const String &p_method, const Array &p_params = Array());
	static PackedByteArray make_response(int p_msgid, const Variant &p_result, const Variant &p_error = Variant());
	static PackedByteArray make_notification(const String &p_method, const Array &p_params = Array());

	static void _thread_func(void *p_user_data);
	Error connect_to_host(const String &p_ip, int p_port, bool p_big_endian = false);
	Error takeover_connection(Ref<StreamPeerTCP> p_peer);

#if MPACK_EXTENSIONS
	void register_extension_type(int p_ext_type, const Callable &p_decoder);
#endif

	Error register_request(const String &p_method, const Callable &p_callable, bool p_rewrite = false);
	Error unregister_request(const String &p_method);
	Error register_notification(const String &p_method, const Callable &p_callable, bool p_rewrite = false);
	Error unregister_notification(const String &p_method);

	void poll();
	void close();

	void _got_error(Error p_err, const String &p_err_msg);
	void _message_received(const Variant &p_message);
	void _request_received(int p_msgid, const String &p_method, const Array &p_params);
	void _response_received(int p_msgid, const Variant &p_error, const Variant &p_result);
	void _notification_received(const String &p_method, const Array &p_params);

	inline uint64_t get_next_msgid() const { return msgid; }
	inline void set_next_msgid(int p_msgid) { msgid = p_msgid; }
	bool is_rpc_connected();
	Error _put_message(const Array &p_msg);

	Array sync_callv(const String &p_method, uint64_t p_timeout_msec = 100, const Array &p_params = Array());
	Error async_callv(const String &p_method, const Array &p_params = Array());
	Error response(uint64_t p_msgid, const Variant &p_result);
	Error response_error(uint64_t p_msgid, const Variant &p_error);
	Error notifyv(const String &p_method, const Array &p_params = Array());

	MessagePackRPC(Ref<StreamPeerTCP> p_stream = Ref<StreamPeerTCP>());
	~MessagePackRPC();
};

#ifdef GDEXTENSION
VARIANT_ENUM_CAST(MessagePackRPC, MessageType);
#else
VARIANT_ENUM_CAST(MessagePackRPC::MessageType);
#endif

#endif // MESSAGE_PACK_RPC_H
