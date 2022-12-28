/*************************************************************************/
/*  message_pack_rpc.cpp                                                 */
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

#include "message_pack_rpc.h"
#include "core/os/memory.h"

PackedByteArray MessagePackRPC::make_message_buf(const Array &p_message) {
	// MessagePack message elements never less than 3 and never more than 4.
	ERR_FAIL_COND_V_MSG((p_message.size() < 3 || p_message.size() > 4), Variant(),
			"Not a valid message.");
	ERR_FAIL_COND_V_MSG((p_message[0].get_type() != Variant::Type::INT), Variant(),
			"Not a valid message.");
	switch (int(p_message[0])) {
		case 0:
			// Request message: [type, msgid, method, params]
			ERR_FAIL_COND_V(p_message.size() != 4, Variant());
			break;
		case 1:
			// Response message: [type, msgid, error, result]
			ERR_FAIL_COND_V(p_message.size() != 4, Variant());
			break;
		case 2:
			// Notification message: [type, method, params]
			ERR_FAIL_COND_V(p_message.size() != 3, Variant());
			break;
	}
	Array result = MessagePack::encode(p_message);
	ERR_FAIL_COND_V_MSG(int(result[0]) != OK, Variant(),
			"Some error occurred while packing request: " + String(result[1]));
	return result[1];
}

PackedByteArray MessagePackRPC::make_request(int p_msgid, const String &p_method, const Array &p_params) {
	Array msg;
	msg.resize(4);
	msg[0] = 0;
	msg[1] = p_msgid;
	msg[2] = p_method;
	msg[3] = p_params;

	return make_message_buf(msg);
}

PackedByteArray MessagePackRPC::make_response(int p_msgid, const Variant &p_result, const Variant &p_error) {
	Array msg;
	msg.resize(4);
	msg[0] = 1;
	msg[1] = p_msgid;
	msg[2] = p_error;
	msg[3] = p_result;

	return make_message_buf(msg);
}

PackedByteArray MessagePackRPC::make_notification(const String &p_method, const Array &p_params) {
	Array msg;
	msg.resize(3);
	msg[0] = 2;
	msg[1] = p_method;
	msg[2] = p_params;

	return make_message_buf(msg);
}

void MessagePackRPC::_error_handle(Error p_err, const String p_err_msg) {
	call_deferred(SNAME("_got_error"), p_err, p_err_msg);
}

Error MessagePackRPC::_message_handle(const Variant &p_message) {
	if (p_message.get_type() == Variant::ARRAY) {
		String _err_msg = "Invalid message received: " + p_message.to_json_string();
		Array msg_arr = p_message;
		if (msg_arr[0].get_type() != Variant::INT) {
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, _err_msg);
		}

		switch (int(msg_arr[0])) {
			case 0: // Request
				ERR_FAIL_COND_V_MSG(msg_arr.size() != 4, ERR_INVALID_PARAMETER, _err_msg);
				call_deferred(SNAME("_request_received"), msg_arr[1], msg_arr[2], msg_arr[3]);
				break;
			case 1: // Response
				ERR_FAIL_COND_V_MSG(msg_arr.size() != 4, ERR_INVALID_PARAMETER, _err_msg);
				if (sync_started && sync_msgid == int(msg_arr[1])) {
					// Sync request responded.
					sync_result[0] = msg_arr[2];
					sync_result[1] = msg_arr[3];
					sync_responded = true;
					// sync response not emit signal, return directly.
					return OK;
				}
				call_deferred(SNAME("_response_received"), msg_arr[1], msg_arr[2], msg_arr[3]);
				break;
			case 2: // Notification
				ERR_FAIL_COND_V_MSG(msg_arr.size() != 3, ERR_INVALID_PARAMETER, _err_msg);
				call_deferred(SNAME("_notification_received"), msg_arr[1], msg_arr[2]);
				break;
			default:
				ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, _err_msg);
				break;
		}
		call_deferred(SNAME("_message_received"), p_message);
	} else {
		// Invalid message
		// MessagePack rpc packet received, but not an array.
	}

	return OK;
}

Error MessagePackRPC::_try_connect(const String &p_ip, int p_port) {
	const int tries = 6;
	const int waits[tries] = { 1, 10, 100, 1000, 1000, 1000 };

	tcp_stream->connect_to_host(p_ip, p_port);

	for (int i = 0; i < tries; i++) {
		tcp_stream->poll();
		if (tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
			print_verbose("MessagePackRPC tcp peer Connected!");
			break;
		} else {
			const int ms = waits[i];
			OS::get_singleton()->delay_usec(ms * 1000);
			print_verbose("MessagePackRPC tcp peer: Connection failed with status: '" +
					String::num(tcp_stream->get_status()) + "', retrying in " + String::num(ms) + " msec.");
		}
	}

	if (tcp_stream->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		ERR_PRINT("MessagePackRPC: Unable to connect. Status: " + String::num(tcp_stream->get_status()) + ".");
		return FAILED;
	}
	return OK;
}

void MessagePackRPC::_thread_func(void *p_user_data) {
	const uint64_t poll_interval = 6900;
	MessagePackRPC *rpc = (MessagePackRPC *)p_user_data;
	while (rpc->running) {
		uint64_t ticks_usec = OS::get_singleton()->get_ticks_usec();
		rpc->poll();
		ticks_usec = OS::get_singleton()->get_ticks_msec() - ticks_usec;
		if (ticks_usec < poll_interval) {
			OS::get_singleton()->delay_usec(poll_interval - ticks_usec);
		}
	}
}

Error MessagePackRPC::connect_to(const String &p_address, bool p_big_endian) {
	if (p_address.begins_with("tcp://")) {
		// TCP address
		String addr = p_address.trim_prefix("tcp://");
		PackedStringArray sub_str = addr.split(":", false);
		if (sub_str.size() != 2 || !sub_str[0].is_valid_ip_address() || !sub_str[1].is_valid_int()) {
			return ERR_INVALID_PARAMETER;
		}
		String ip = sub_str[0];
		uint32_t port = sub_str[1].to_int();
		// try to connect to the specified address.
		if (_try_connect(ip, port) != OK) {
			return ERR_CANT_CONNECT;
		}
		tcp_stream->set_big_endian(p_big_endian);
		if (_start_stream(_read_stream) != OK) {
			return ERR_BUG;
		}
	} else {
		return ERR_INVALID_PARAMETER;
	}

	connected = true;
	running = true;
	thread.start(_thread_func, this);

	return OK;
}

Error MessagePackRPC::takeover_connection(Ref<StreamPeerTCP> p_peer) {
	ERR_FAIL_COND_V_MSG(!p_peer.is_valid(), ERR_INVALID_PARAMETER, "Stream invalid.");
	p_peer->poll();
	ERR_FAIL_COND_V_MSG(p_peer->get_status() != StreamPeerTCP::STATUS_CONNECTED, ERR_CONNECTION_ERROR, "Not connected.");
	close();
	tcp_stream = p_peer;
	if (_start_stream(_read_stream) != OK) {
		return ERR_BUG;
	}

	connected = true;
	running = true;
	thread.start(_thread_func, this);

	return OK;
}

Error MessagePackRPC::_start_stream(Callback p_callback, int p_msgs_max) {
	ERR_FAIL_COND_V_MSG(p_callback == nullptr, ERR_INVALID_PARAMETER, "Invalid callback.");
	if (_started) {
		mpack_tree_destroy(&_tree);
	}
	mpack_tree_init_stream(&_tree, p_callback, this, p_msgs_max, _NODE_MAX_SIZE);
	_started = true;

	return OK;
}

Error MessagePackRPC::_update_stream() {
	String _err_msg;
	if (!mpack_tree_try_parse(&_tree)) {
		// if false, error or wating.
		Error err = _got_error_or_not(mpack_tree_error(&_tree), _err_msg);
		if (err != OK) {
			// Error
			_err_msg = "Parse failed: " + _err_msg;
			_error_handle(err, _err_msg);
			ERR_FAIL_V_MSG(err, _err_msg);
		}
		// Waiting for more data.
		return ERR_SKIP;
	}
	// if true, got data.
	mpack_node_t root = mpack_tree_root(&_tree);
	_message_handle(_parse_node_recursive(root, 0));

	return OK;
}

size_t MessagePackRPC::_read_stream(mpack_tree_t *p_tree, char *r_buffer, size_t p_count) {
	MessagePackRPC *rpc = (MessagePackRPC *)mpack_tree_context(p_tree);
	if (rpc->in_tail <= rpc->in_head) {
		return 0; // No data to read.
	}
	int bytes_read = MIN(rpc->in_tail - rpc->in_head, p_count);
	memcpy(r_buffer, rpc->in_buf.ptr() + rpc->in_head, bytes_read);
	rpc->in_head += bytes_read;

	return bytes_read;
}

void MessagePackRPC::_write_out() {
	while (tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED && tcp_stream->wait(NetSocket::POLL_TYPE_OUT) == OK) {
		uint8_t *buf = out_buf.ptrw();
		if (out_head >= out_tail) {
			if (msg_queue.size() == 0) {
				break; // Nothing left to send
			}
			mutex.lock();
			Array msg = msg_queue[0];
			msg_queue.pop_front();
			mutex.unlock();

			PackedByteArray msg_buf = make_message_buf(msg);
			ERR_CONTINUE(msg_buf.size() <= 0 || msg_buf.size() > _MSG_BUF_MAX_SIZE);

			memcpy(out_buf.ptrw(), msg_buf.ptr(), msg_buf.size());
			out_head = 0;
			out_tail = msg_buf.size();
		}
		int sent = 0;
		tcp_stream->put_partial_data(buf + out_head, out_tail - out_head, sent);
		out_head += sent;
	}
}

void MessagePackRPC::_read_in() {
	while (tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED && tcp_stream->wait(NetSocket::POLL_TYPE_IN) == OK) {
		uint8_t *buf = in_buf.ptrw();
		if (in_head >= in_tail) {
			if (tcp_stream->get_available_bytes() < 1) {
				break;
			}
			int read = 0;
			Error err = tcp_stream->get_partial_data(buf, _MSG_BUF_MAX_SIZE, read);
			ERR_CONTINUE(err != OK || read <= 0);
			in_head = 0;
			in_tail = read;
		}
		int available = tcp_stream->get_available_bytes();
		int read = 0;
		if (in_tail + available < _MSG_BUF_MAX_SIZE) {
			tcp_stream->get_partial_data(buf + in_tail, available, read);
		} else {
			tcp_stream->get_partial_data(buf + in_tail, _MSG_BUF_MAX_SIZE - in_tail, read);
		}
		in_tail += read;
	}
}

void MessagePackRPC::poll() {
	if (connected) {
		_write_out();
		_read_in();
		if (in_tail > in_head) {
			_update_stream();
		}
		connected = tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED;
	}
}

void MessagePackRPC::close() {
	running = false;
	thread.wait_to_finish();
	connected = false;
	tcp_stream->disconnect_from_host();
}

void MessagePackRPC::_got_error(Error p_err, const String &p_err_msg) {
	emit_signal(SNAME("got_error"), p_err, p_err_msg);
}

void MessagePackRPC::_message_received(const Variant &p_message) {
	emit_signal(SNAME("message_received"), p_message);
}

void MessagePackRPC::_request_received(int p_msgid, const String &p_method, const Array &p_params) {
	emit_signal(SNAME("request_received"), p_msgid, p_method, p_params);
}

void MessagePackRPC::_response_received(int p_msgid, const Variant &p_error, const Variant &p_result) {
	emit_signal(SNAME("response_received"), p_msgid, p_error, p_result);
}

void MessagePackRPC::_notification_received(const String &p_method, const Array &p_params) {
	emit_signal(SNAME("notification_received"), p_method, p_params);
}

bool MessagePackRPC::is_rpc_connected() {
	return connected;
}

Error MessagePackRPC::_put_message(const Array &p_msg) {
	MutexLock lock(mutex);
	if (msg_queue.size() >= _MSG_QUEUE_MAX_SIZE) {
		return ERR_OUT_OF_MEMORY;
	}

	msg_queue.push_back(p_msg);
	return OK;
}

Array MessagePackRPC::_sync_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (p_argcount < 2) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.argument = 1;
		ERR_FAIL_V_MSG(Array(), "Too few arguments. Expected at least 2.");
	}
	if (p_args[0]->get_type() != Variant::STRING && p_args[0]->get_type() != Variant::STRING_NAME) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 0;
		r_error.expected = Variant::STRING;
		ERR_FAIL_V_MSG(Array(), "Argument 'method' must be a string.");
	}
	if (p_args[1]->get_type() != Variant::INT) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 1;
		r_error.expected = Variant::INT;
		ERR_FAIL_V_MSG(Array(), "Argument 'timeout_msec' must be an integer.");
	}
	r_error.error = Callable::CallError::CALL_OK;

	String method = *p_args[0];
	int timeout = *p_args[1];

	Array args;
	args.resize(p_argcount - 2);
	for (int i = 0; i < p_argcount - 2; i++) {
		args[i] = *p_args[i + 2];
	}

	return sync_callv(method, timeout, args);
}

Array MessagePackRPC::sync_callv(const String &p_method, uint64_t p_timeout_msec, const Array &p_params) {
	ERR_FAIL_COND_V_MSG(!running, Array(), "Connect to a peer first.");
	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = 0;
	msg_req[1] = msgid;
	msg_req[2] = p_method;
	msg_req[3] = p_params;

	sync_msgid = msgid;
	sync_started = true;
	sync_responded = false;
	if (_put_message(msg_req) != OK) {
		sync_started = false;
		ERR_FAIL_V_MSG(Array(), "Message queue is full.");
	}
	msgid += 1;

	// Wait until sync response is received or timeout.
	uint64_t start_time = OS::get_singleton()->get_ticks_msec();
	while (!sync_responded) {
		if (OS::get_singleton()->get_ticks_msec() - start_time > p_timeout_msec) {
			// Timeout
			sync_started = false;
			ERR_FAIL_V_MSG(Array(), "Sync call timeout!");
		}
		OS::get_singleton()->delay_usec(1000);
	}
	sync_started = false;

	return sync_result;
}

Error MessagePackRPC::_async_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (p_argcount < 1) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.argument = 0;
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Too few arguments. Expected at least 1.");
	}
	if (p_args[0]->get_type() != Variant::STRING && p_args[0]->get_type() != Variant::STRING_NAME) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 0;
		r_error.expected = Variant::STRING;
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Argument 'method' must be a string.");
	}
	r_error.error = Callable::CallError::CALL_OK;

	String method = *p_args[0];

	Array args;
	args.resize(p_argcount - 1);
	for (int i = 0; i < p_argcount - 1; i++) {
		args[i] = *p_args[i + 1];
	}

	return async_callv(method, args);
}

Error MessagePackRPC::_notify(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (p_argcount < 1) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		r_error.argument = 0;
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Too few arguments. Expected at least 1.");
	}
	if (p_args[0]->get_type() != Variant::STRING && p_args[0]->get_type() != Variant::STRING_NAME) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 0;
		r_error.expected = Variant::STRING;
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Argument 'method' must be a string.");
	}
	r_error.error = Callable::CallError::CALL_OK;

	String method = *p_args[0];

	Array args;
	args.resize(p_argcount - 1);
	for (int i = 0; i < p_argcount - 1; i++) {
		args[i] = *p_args[i + 1];
	}

	return notifyv(method, args);
}

Error MessagePackRPC::async_callv(const String &p_method, const Array &p_params) {
	ERR_FAIL_COND_V_MSG(!running, ERR_UNAVAILABLE, "Connect to a peer first.");

	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = 0;
	msg_req[1] = msgid;
	msg_req[2] = p_method;
	msg_req[3] = p_params;
	ERR_FAIL_COND_V_MSG(_put_message(msg_req) != OK, ERR_OUT_OF_MEMORY, "Message queue is full.");
	msgid += 1;

	return OK;
}

Error MessagePackRPC::response(uint64_t p_msgid, const Variant &p_result) {
	ERR_FAIL_COND_V_MSG(!running, ERR_UNAVAILABLE, "Connect to a peer first.");

	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = 1;
	msg_req[1] = p_msgid;
	msg_req[2] = Variant();
	msg_req[3] = p_result;
	ERR_FAIL_COND_V_MSG(_put_message(msg_req) != OK, ERR_OUT_OF_MEMORY, "Message queue is full.");

	return OK;
}

Error MessagePackRPC::response_error(uint64_t p_msgid, const Variant &p_error) {
	ERR_FAIL_COND_V_MSG(!running, ERR_UNAVAILABLE, "Connect to a peer first.");

	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = 1;
	msg_req[1] = p_msgid;
	msg_req[2] = p_error;
	msg_req[3] = Variant();
	ERR_FAIL_COND_V_MSG(_put_message(msg_req) != OK, ERR_OUT_OF_MEMORY, "Message queue is full.");

	return OK;
}

Error MessagePackRPC::notifyv(const String &p_method, const Array &p_params) {
	ERR_FAIL_COND_V_MSG(!running, ERR_UNAVAILABLE, "Connect to a peer first.");

	Array msg_req;
	msg_req.resize(3);
	msg_req[0] = 2;
	msg_req[1] = p_method;
	msg_req[2] = p_params;
	ERR_FAIL_COND_V_MSG(_put_message(msg_req) != OK, ERR_OUT_OF_MEMORY, "Message queue is full.");

	return OK;
}

MessagePackRPC::MessagePackRPC(Ref<StreamPeerTCP> p_stream) {
	// MessagePackRPC will takes 16 MiB just because it exists...
	in_buf.resize(_MSG_BUF_MAX_SIZE);
	out_buf.resize(_MSG_BUF_MAX_SIZE);
	sync_result.resize(2);
	tcp_stream = p_stream;
	if (!tcp_stream.is_valid()) {
		tcp_stream.instantiate();
	}
}

MessagePackRPC::~MessagePackRPC() {
	close();
	out_buf.clear();
	in_buf.clear();
	sync_result.clear();
	if (_started) {
		mpack_tree_destroy(&_tree);
	}
}

void MessagePackRPC::_bind_methods() {
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_message_buf", "message"), &MessagePackRPC::make_message_buf);
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_request", "msg_id", "method", "params"), &MessagePackRPC::make_request, DEFVAL(Array()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_response", "msg_id", "result", "error"), &MessagePackRPC::make_response, DEFVAL(Variant()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_notification", "method", "params"), &MessagePackRPC::make_notification, DEFVAL(Array()));

	ClassDB::bind_method(D_METHOD("connect_to", "address", "big_endian"), &MessagePackRPC::connect_to, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("takeover_connection", "tcp_connection"), &MessagePackRPC::takeover_connection);
	ClassDB::bind_method(D_METHOD("close"), &MessagePackRPC::close);

	ClassDB::bind_method(D_METHOD("_message_received", "message"), &MessagePackRPC::_message_received);
	ClassDB::bind_method(D_METHOD("_request_received", "msgid", "method", "params"), &MessagePackRPC::_request_received);
	ClassDB::bind_method(D_METHOD("_response_received", "msgid", "error", "result"), &MessagePackRPC::_response_received);
	ClassDB::bind_method(D_METHOD("_notification_received", "method", "params"), &MessagePackRPC::_notification_received);
	{
		MethodInfo mi;
		mi.name = "sync_call";
		mi.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
		mi.arguments.push_back(PropertyInfo(Variant::INT, "timeout_msec"));
		ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "sync_call", &MessagePackRPC::_sync_call, mi, varray());
	}

	{
		MethodInfo mi;
		mi.name = "async_call";
		mi.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
		ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "async_call", &MessagePackRPC::_async_call, mi, varray());
	}

	{
		MethodInfo mi;
		mi.name = "notify";
		mi.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
		ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "notify", &MessagePackRPC::_notify, mi, varray());
	}

	ClassDB::bind_method(D_METHOD("get_next_msgid"), &MessagePackRPC::get_next_msgid);
	ClassDB::bind_method(D_METHOD("is_rpc_connected"), &MessagePackRPC::is_rpc_connected);
	ClassDB::bind_method(D_METHOD("sync_callv", "method", "timeout_msec", "params"), &MessagePackRPC::sync_callv, DEFVAL(100), DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("async_callv", "method", "params"), &MessagePackRPC::async_callv, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("response", "msgid", "result"), &MessagePackRPC::response);
	ClassDB::bind_method(D_METHOD("response_error", "msgid", "error"), &MessagePackRPC::response);
	ClassDB::bind_method(D_METHOD("notifyv", "method", "params"), &MessagePackRPC::notifyv, DEFVAL(Array()));

	ADD_SIGNAL(MethodInfo("got_error", PropertyInfo(Variant::INT, "err"), PropertyInfo(Variant::STRING, "err_msg")));
	ADD_SIGNAL(MethodInfo("message_received", PropertyInfo(Variant::ARRAY, "message")));
	ADD_SIGNAL(MethodInfo("request_received", PropertyInfo(Variant::INT, "msgid"), PropertyInfo(Variant::STRING, "method"), PropertyInfo(Variant::ARRAY, "params")));
	ADD_SIGNAL(MethodInfo("response_received", PropertyInfo(Variant::INT, "msgid"), PropertyInfo(Variant::OBJECT, "error"), PropertyInfo(Variant::ARRAY, "result")));
	ADD_SIGNAL(MethodInfo("notification_received", PropertyInfo(Variant::STRING, "method"), PropertyInfo(Variant::ARRAY, "params")));
}
