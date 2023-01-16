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

#ifdef GDEXTENSION
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;
#else
#include "core/os/memory.h"
#endif

using namespace std::chrono;

PackedByteArray MessagePackRPC::make_message_byte_array(const Array &p_message) {
	// MessagePack message elements never less than 3 and never more than 4.
	ERR_FAIL_COND_V_MSG((p_message.size() < 3 || p_message.size() > 4), Variant(),
			"Not a valid message.");
	ERR_FAIL_COND_V_MSG((p_message[0].get_type() != Variant::Type::INT), Variant(),
			"Not a valid message.");
	switch (int(p_message[0])) {
		case REQUEST:
			// Request message: [type, msgid, method, params]
			ERR_FAIL_COND_V(p_message.size() != 4, Variant());
			break;
		case RESPONSE:
			// Response message: [type, msgid, error, result]
			ERR_FAIL_COND_V(p_message.size() != 4, Variant());
			break;
		case NOTIFICATION:
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
	msg[0] = REQUEST;
	msg[1] = p_msgid;
	msg[2] = p_method;
	msg[3] = p_params;

	return make_message_byte_array(msg);
}

PackedByteArray MessagePackRPC::make_response(int p_msgid, const Variant &p_result, const Variant &p_error) {
	Array msg;
	msg.resize(4);
	msg[0] = RESPONSE;
	msg[1] = p_msgid;
	msg[2] = p_error;
	msg[3] = p_result;

	return make_message_byte_array(msg);
}

PackedByteArray MessagePackRPC::make_notification(const String &p_method, const Array &p_params) {
	Array msg;
	msg.resize(3);
	msg[0] = NOTIFICATION;
	msg[1] = p_method;
	msg[2] = p_params;

	return make_message_byte_array(msg);
}

void MessagePackRPC::_error_handle(Error p_err, const String p_err_msg) {
	call_deferred("_got_error", p_err, p_err_msg);
}

Error MessagePackRPC::_message_handle(const Variant &p_message) {
	if (p_message.get_type() == Variant::ARRAY) {
		String _err_msg = "Invalid message received: " + String(p_message);
		Array msg_arr = p_message;
		if (msg_arr[0].get_type() != Variant::INT) {
			ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, _err_msg);
		}

		switch (int(msg_arr[0])) {
			case REQUEST: // Request [msgid, method, params]
				ERR_FAIL_COND_V_MSG(msg_arr.size() != 4, ERR_INVALID_PARAMETER, _err_msg);
				if (request_map.has(msg_arr[2])) {
					// Callable(request_map[msg_arr[2]]).call_deferred(msg_arr[1], msg_arr[2], msg_arr[3]);
					// registered request, not emit signal
					return OK;
				}
				call_deferred("_request_received", msg_arr[1], msg_arr[2], msg_arr[3]);
				break;
			case RESPONSE: // Response [msgid, error, result]
				ERR_FAIL_COND_V_MSG(msg_arr.size() != 4, ERR_INVALID_PARAMETER, _err_msg);
				if (sync_started && sync_msgid == int(msg_arr[1])) {
					// Sync request responded.
					sync_result[0] = msg_arr[2];
					sync_result[1] = msg_arr[3];
					sync_responded = true;
					// sync response not emit signal, return directly.
					return OK;
				}
				call_deferred("_response_received", msg_arr[1], msg_arr[2], msg_arr[3]);
				break;
			case NOTIFICATION: // Notification [method, params]
				ERR_FAIL_COND_V_MSG(msg_arr.size() != 3, ERR_INVALID_PARAMETER, _err_msg);
				if (notify_map.has(msg_arr[1])) {
					// Callable(notify_map[msg_arr[1]]).call_deferred(msg_arr[1], msg_arr[2]);
					// registered notification, not emit signal
					return OK;
				}
				call_deferred("_notification_received", msg_arr[1], msg_arr[2]);
				break;
			default:
				ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, _err_msg);
				break;
		}
		call_deferred("_message_received", p_message);
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
			// print_verbose("MessagePackRPC tcp peer Connected!");
			break;
		} else {
			const int ms = waits[i];
			OS::get_singleton()->delay_usec(ms * 1000);
			// print_verbose("MessagePackRPC tcp peer: Connection failed with status: '" +
			// String::num(tcp_stream->get_status()) + "', retrying in " + String::num(ms) + " msec.");
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
		time_point time_start = system_clock::now();

		rpc->poll();

		time_t time_elapsed = duration_cast<microseconds>(system_clock::now() - time_start).count();
		if (time_elapsed < poll_interval) {
			std::this_thread::sleep_for(microseconds(poll_interval - time_elapsed));
		}
	}
}

Error MessagePackRPC::connect_to_host(const String &p_ip, int p_port, bool p_big_endian) {
	// try to connect to the specified address.
	if (_try_connect(p_ip, p_port) != OK) {
		return ERR_CANT_CONNECT;
	}
	tcp_stream->set_big_endian(p_big_endian);

	close();
	_start_stream();
	connected = true;
	running = true;
	thread = std::thread(_thread_func, this);
	emit_signal("rpc_connected", tcp_stream->get_connected_host(), tcp_stream->get_connected_port());

	return OK;
}

Error MessagePackRPC::takeover_connection(Ref<StreamPeerTCP> p_peer) {
	ERR_FAIL_COND_V_MSG(!p_peer.is_valid(), ERR_INVALID_PARAMETER, "Connection invalid.");
	p_peer->poll();
	ERR_FAIL_COND_V_MSG(p_peer->get_status() != StreamPeerTCP::STATUS_CONNECTED, ERR_CONNECTION_ERROR, "Not connected.");

	close();
	tcp_stream = p_peer;
	_start_stream();
	connected = true;
	running = true;
	thread = std::thread(_thread_func, this);
	emit_signal("rpc_connected", tcp_stream->get_connected_host(), tcp_stream->get_connected_port());

	return OK;
}

#if MPACK_EXTENSIONS
void MessagePackRPC::register_extension_type(int p_ext_type, const Callable &p_decoder) {
	ERR_FAIL_COND_MSG(p_ext_type > 127, "Invalid extension type.");
	msg_pack.register_extension_type(p_ext_type, p_decoder);
}
#endif

size_t MessagePackRPC::_stream_reader(mpack_tree_t *p_tree, char *r_buffer, size_t p_count) {
	MessagePackRPC *rpc = (MessagePackRPC *)mpack_tree_context(p_tree);
	size_t bytes_left = rpc->in_tail - rpc->in_head;
	size_t read_size = MIN(p_count, bytes_left);
	const uint8_t *stream_ptr = rpc->in_buf.ptr();
	if (read_size > 0) {
		memcpy(r_buffer, stream_ptr + rpc->in_head, read_size);
		rpc->in_head += read_size;
	}
	return read_size;
}

void MessagePackRPC::_start_stream(int p_msgs_max) {
	msg_pack.start_stream_with_reader(_stream_reader, this, p_msgs_max);
}

Error MessagePackRPC::_try_parse_stream() {
	Error err = msg_pack.try_parse_stream();
	if (err == OK) {
		// if okay, got data.
		_message_handle(msg_pack.get_data());
	}

	return err;
}

void MessagePackRPC::_write_out() {
	tcp_stream->poll();
	while (tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
		uint8_t *buf = out_buf.ptrw();
		if (out_head >= out_tail) {
			if (msg_queue.size() == 0) {
				break; // Nothing left to send
			}
			mutex.lock();
			Array msg = msg_queue[0];
			msg_queue.pop_front();
			mutex.unlock();

			PackedByteArray msg_buf = make_message_byte_array(msg);
			ERR_CONTINUE(msg_buf.size() <= 0 || msg_buf.size() > _MSG_BUF_MAX_SIZE);

			memcpy(out_buf.ptrw(), msg_buf.ptr(), msg_buf.size());
			out_head = 0;
			out_tail = msg_buf.size();
		}
#ifdef GDEXTENSION
		Array result = tcp_stream->put_partial_data(out_buf.slice(out_head, out_tail));
		if (int(result[0]) == OK) {
			out_head += int(result[1]);
		}
#else
		int sent = 0;
		tcp_stream->put_partial_data(buf + out_head, out_tail - out_head, sent);
		out_head += sent;
#endif
	}
}

void MessagePackRPC::_read_in() {
	tcp_stream->poll();
	while (tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
		uint8_t *buf = in_buf.ptrw();
		if (in_head >= in_tail) {
			if (tcp_stream->get_available_bytes() < 1) {
				break;
			}
			int read = 0;
#ifdef GDEXTENSION
			Array result = tcp_stream->get_partial_data(_MSG_BUF_MAX_SIZE);
			if (int(result[0]) == OK) {
				PackedByteArray data = PackedByteArray(result[1]);
				read = data.size();
				ERR_CONTINUE(read <= 0);
				memcpy(buf, data.ptr(), read);
			} else {
				ERR_PRINT("Condition \"" _STR(err != OK) "\" is true. Continuing.");
			}
#else
			Error err = tcp_stream->get_partial_data(buf, _MSG_BUF_MAX_SIZE, read);
			ERR_CONTINUE(err != OK || read <= 0);
#endif
			in_head = 0;
			in_tail = read;
		}
		int available = tcp_stream->get_available_bytes();
		int read = 0;
#ifdef GDEXTENSION
		if (in_tail + available < _MSG_BUF_MAX_SIZE) {
			Array result = tcp_stream->get_partial_data(available);
			if (int(result[0]) == OK) {
				PackedByteArray data = PackedByteArray(result[1]);
				read = data.size();
				memcpy(buf + in_tail, data.ptr(), read);
			}
		} else {
			Array result = tcp_stream->get_partial_data(_MSG_BUF_MAX_SIZE - in_tail);
			if (int(result[0]) == OK) {
				PackedByteArray data = PackedByteArray(result[1]);
				read = data.size();
				memcpy(buf + in_tail, data.ptr(), read);
			}
		}
#else
		if (in_tail + available < _MSG_BUF_MAX_SIZE) {
			tcp_stream->get_partial_data(buf + in_tail, available, read);
		} else {
			tcp_stream->get_partial_data(buf + in_tail, _MSG_BUF_MAX_SIZE - in_tail, read);
		}
#endif
		in_tail += read;
	}
}

void MessagePackRPC::poll() {
	if (connected) {
		_write_out();
		_read_in();
		if (in_tail > in_head) {
			_try_parse_stream();
		}
		connected = tcp_stream->get_status() == StreamPeerTCP::STATUS_CONNECTED;
	}
}

void MessagePackRPC::close() {
	running = false;
	if (thread.joinable()) {
		thread.join();
	}
	connected = false;
	if (tcp_stream.is_valid()) {
		tcp_stream->disconnect_from_host();
		emit_signal("rpc_disconnected", tcp_stream->get_connected_host(), tcp_stream->get_connected_port());
	}
}

Error MessagePackRPC::register_request(const String &p_method, const Callable &p_callable, bool p_rewrite) {
	ERR_FAIL_COND_V_MSG((!p_rewrite && request_map.has(p_method)), ERR_ALREADY_EXISTS, "Request '" + p_method + "' already exist.");
	request_map[p_method] = p_callable;
	return OK;
}

Error MessagePackRPC::unregister_request(const String &p_method) {
	ERR_FAIL_COND_V_MSG(!request_map.has(p_method), ERR_DOES_NOT_EXIST, "Reqeust '" + p_method + "'does not registered.");
	request_map.erase(p_method);
	return OK;
}

Error MessagePackRPC::register_notification(const String &p_method, const Callable &p_callable, bool p_rewrite) {
	ERR_FAIL_COND_V_MSG((!p_rewrite && notify_map.has(p_method)), ERR_ALREADY_EXISTS, "Notify '" + p_method + "' already exist.");
	notify_map[p_method] = p_callable;
	return OK;
}

Error MessagePackRPC::unregister_notification(const String &p_method) {
	ERR_FAIL_COND_V_MSG(!notify_map.has(p_method), ERR_DOES_NOT_EXIST, "Notify '" + p_method + "'does not registered.");
	notify_map.erase(p_method);
	return OK;
}

void MessagePackRPC::_got_error(Error p_err, const String &p_err_msg) {
	emit_signal("got_error", p_err, p_err_msg);
}

void MessagePackRPC::_message_received(const Variant &p_message) {
	emit_signal("message_received", p_message);
}

void MessagePackRPC::_request_received(int p_msgid, const String &p_method, const Array &p_params) {
	emit_signal("request_received", p_msgid, p_method, p_params);
}

void MessagePackRPC::_response_received(int p_msgid, const Variant &p_error, const Variant &p_result) {
	emit_signal("response_received", p_msgid, p_error, p_result);
}

void MessagePackRPC::_notification_received(const String &p_method, const Array &p_params) {
	emit_signal("notification_received", p_method, p_params);
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

#ifndef GDEXTENSION
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
#endif

Array MessagePackRPC::sync_callv(const String &p_method, uint64_t p_timeout_msec, const Array &p_params) {
	ERR_FAIL_COND_V_MSG(!running, Array(), "Connect to a peer first.");
	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = REQUEST;
	msg_req[1] = msgid;
	msg_req[2] = p_method;
	msg_req[3] = p_params;

	sync_responded = false;
	sync_msgid = msgid;
	sync_started = true;
	if (_put_message(msg_req) != OK) {
		sync_started = false;
		ERR_FAIL_V_MSG(Array(), "Message queue is full.");
	}
	msgid += 1;

	// Wait until sync response is received or timeout.
	time_point time_start = system_clock::now();
	while (!sync_responded) {
		if (duration_cast<milliseconds>(system_clock::now() - time_start).count() > p_timeout_msec) {
			// Timeout
			sync_started = false;
			ERR_FAIL_V_MSG(Array(), "Sync call timeout!");
		}
		std::this_thread::sleep_for(microseconds(1000));
	}
	sync_started = false;

	return sync_result;
}

Error MessagePackRPC::async_callv(const String &p_method, const Array &p_params) {
	ERR_FAIL_COND_V_MSG(!running, ERR_UNAVAILABLE, "Connect to a peer first.");

	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = REQUEST;
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
	msg_req[0] = RESPONSE;
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
	msg_req[0] = RESPONSE;
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
	msg_req[0] = NOTIFICATION;
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
}

void MessagePackRPC::_bind_methods() {
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_message_byte_array", "message"), &MessagePackRPC::make_message_byte_array);
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_request", "msg_id", "method", "params"), &MessagePackRPC::make_request, DEFVAL(Array()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_response", "msg_id", "result", "error"), &MessagePackRPC::make_response, DEFVAL(Variant()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_notification", "method", "params"), &MessagePackRPC::make_notification, DEFVAL(Array()));

	ClassDB::bind_method(D_METHOD("connect_to_host", "ip", "port", "big_endian"), &MessagePackRPC::connect_to_host, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("takeover_connection", "tcp_connection"), &MessagePackRPC::takeover_connection);
	ClassDB::bind_method(D_METHOD("close"), &MessagePackRPC::close);

#if MPACK_EXTENSIONS
	ClassDB::bind_method(D_METHOD("register_extension_type", "type_id", "decoder"), &MessagePackRPC::register_extension_type);
#endif

	ClassDB::bind_method(D_METHOD("register_request", "method", "callable", "rewrite"), &MessagePackRPC::register_request, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("unregister_request", "method"), &MessagePackRPC::unregister_request);
	ClassDB::bind_method(D_METHOD("register_notification", "method", "callable", "rewrite"), &MessagePackRPC::register_notification, DEFVAL(false));
	ClassDB::bind_method(D_METHOD("unregister_notification", "method"), &MessagePackRPC::unregister_notification);

	ClassDB::bind_method(D_METHOD("_message_received", "message"), &MessagePackRPC::_message_received);
	ClassDB::bind_method(D_METHOD("_request_received", "msgid", "method", "params"), &MessagePackRPC::_request_received);
	ClassDB::bind_method(D_METHOD("_response_received", "msgid", "error", "result"), &MessagePackRPC::_response_received);
	ClassDB::bind_method(D_METHOD("_notification_received", "method", "params"), &MessagePackRPC::_notification_received);

#ifndef GDEXTENSION
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
#endif

	ClassDB::bind_method(D_METHOD("get_next_msgid"), &MessagePackRPC::get_next_msgid);
	ClassDB::bind_method(D_METHOD("set_next_msgid", "msgid"), &MessagePackRPC::set_next_msgid);
	ClassDB::bind_method(D_METHOD("is_rpc_connected"), &MessagePackRPC::is_rpc_connected);
	ClassDB::bind_method(D_METHOD("sync_callv", "method", "timeout_msec", "params"), &MessagePackRPC::sync_callv, DEFVAL(100), DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("async_callv", "method", "params"), &MessagePackRPC::async_callv, DEFVAL(Array()));
	ClassDB::bind_method(D_METHOD("response", "msgid", "result"), &MessagePackRPC::response);
	ClassDB::bind_method(D_METHOD("response_error", "msgid", "error"), &MessagePackRPC::response_error);
	ClassDB::bind_method(D_METHOD("notifyv", "method", "params"), &MessagePackRPC::notifyv, DEFVAL(Array()));

	ADD_SIGNAL(MethodInfo("rpc_connected", PropertyInfo(Variant::STRING, "ip"), PropertyInfo(Variant::INT, "port")));
	ADD_SIGNAL(MethodInfo("rpc_disconnected", PropertyInfo(Variant::STRING, "ip"), PropertyInfo(Variant::INT, "port")));
	ADD_SIGNAL(MethodInfo("got_error", PropertyInfo(Variant::INT, "err"), PropertyInfo(Variant::STRING, "err_msg")));
	ADD_SIGNAL(MethodInfo("message_received", PropertyInfo(Variant::ARRAY, "message")));
	ADD_SIGNAL(MethodInfo("request_received", PropertyInfo(Variant::INT, "msgid"), PropertyInfo(Variant::STRING, "method"), PropertyInfo(Variant::ARRAY, "params")));
	ADD_SIGNAL(MethodInfo("response_received", PropertyInfo(Variant::INT, "msgid"), PropertyInfo(Variant::OBJECT, "error"), PropertyInfo(Variant::ARRAY, "result")));
	ADD_SIGNAL(MethodInfo("notification_received", PropertyInfo(Variant::STRING, "method"), PropertyInfo(Variant::ARRAY, "params")));

	BIND_ENUM_CONSTANT(REQUEST);
	BIND_ENUM_CONSTANT(RESPONSE);
	BIND_ENUM_CONSTANT(NOTIFICATION);
}
