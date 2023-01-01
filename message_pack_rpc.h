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

#include "core/io/file_access.h"
#include "core/io/stream_peer_tcp.h"
#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

#include "message_pack.h"

// Max message buf size: 8MiB, should be way more than enough.
#define _MSG_BUF_MAX_SIZE (1 << 23)
// Max message queue size
#define _MSG_QUEUE_MAX_SIZE 2048

class MessagePackRPC : public MessagePack {
	GDCLASS(MessagePackRPC, MessagePack);

	enum ChannelType {
		NONE = 0,
		PIPE,
		TCP
	};

	Mutex mutex;
	Thread thread;
	ChannelType channel = NONE;
	bool running = false;
	bool connected = false;
	uint64_t poll_interval = 1000;
	Ref<StreamPeerTCP> tcp_peer;
	Ref<FileAccess> named_pipe;

	List<Array> in_queue;
	List<Array> out_queue;
	PackedByteArray out_buf;
	int out_tail = 0;
	int out_head = 0;
	PackedByteArray in_buf;
	int in_tail = 0;
	int in_head = 0;

	uint64_t msgid = 0;

	typedef size_t (*Callback)(mpack_tree_t *p_tree, char *r_buffer, size_t p_count);

	Error _tcp_try_connect(const String &p_ip, int p_port);
	Error start_stream(Callback callback, int p_msgs_max = _MSG_MAX_SIZE);
	Error update_stream();
	void _tcp_write_out();
	void _tcp_read_in();
	void _pipe_write_out();
	void _pipe_read_in();
	static size_t _read_stream(mpack_tree_t *p_tree, char *r_buffer, size_t p_count);

protected:
	static void _bind_methods();

public:
	static PackedByteArray make_message_buf(const Array &p_message);
	static PackedByteArray make_request(int p_msgid, const String &p_method, const Array &p_params = Array());
	static PackedByteArray make_response(int p_msgid, const Variant &p_result, const Variant &p_error = Array());
	static PackedByteArray make_notification(const String &p_method, const Array &p_params = Array());

	Error connect_to(const String &p_address, bool p_big_endian = false, uint64_t p_poll_interval = 1000);
	void close();
	void poll();
	static void _thread_func(void *p_user_data);
	void _message_received(const Variant &p_message);

	bool is_connected();
	bool has_message();
	Error put_message(const Array &p_msg);
	Array get_message();

	Variant sync_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Variant sync_callv(const String &p_method, const Array &p_params = Array());
	Error async_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Error async_callv(const String &p_method, const Array &p_params = Array());

	MessagePackRPC();
	~MessagePackRPC();
};

#endif // MESSAGE_PACK_RPC_H
