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

Error MessagePackRPC::start_stream(Callback callback, int p_msgs_max) {
	ERR_FAIL_COND_V_MSG(callback == nullptr, ERR_INVALID_PARAMETER, "Invalid callback.");
	if (started) {
		mpack_tree_destroy(&tree);
	}
	err_msg = "";
	mpack_tree_init_stream(&tree, callback, this, p_msgs_max, _NODE_MAX_SIZE);
	started = true;

	return OK;
}

Error MessagePackRPC::update_stream() {
	if (!mpack_tree_try_parse(&tree)) {
		// if false, error or wating.
		Error err = _got_error_or_not(mpack_tree_error(&tree), err_msg);
		if (err != OK && err != ERR_SKIP) {
			// Error
			emit_signal(SNAME("got_error"), get_error_message());
		}
		ERR_FAIL_COND_V_MSG(err != OK, err, "Parse failed: " + err_msg);
		// Waiting for more data.
		err_msg = "Waiting for new data.";
		return ERR_SKIP;
	}
	// if true, got data.
	mpack_node_t root = mpack_tree_root(&tree);
	data = _parse_node_recursive(root, 0);

	ERR_FAIL_COND_V_MSG(data.get_type() != Variant::ARRAY, ERR_INVALID_DATA,
			"MessagePack rpc packet received, not an Array.");
	mutex.lock();
	in_queue.push_back(data);
	mutex.unlock();

	call_deferred(SNAME("_message_received"), data);

	return OK;
}

Error MessagePackRPC::_tcp_try_connect(const String &p_ip, int p_port) {
	const int tries = 6;
	const int waits[tries] = { 1, 10, 100, 1000, 1000, 1000 };

	tcp_peer->connect_to_host(p_ip, p_port);

	for (int i = 0; i < tries; i++) {
		tcp_peer->poll();
		if (tcp_peer->get_status() == StreamPeerTCP::STATUS_CONNECTED) {
			print_verbose("MessagePackRPC tcp peer Connected!");
			break;
		} else {
			const int ms = waits[i];
			OS::get_singleton()->delay_usec(ms * 1000);
			print_verbose("MessagePackRPC tcp peer: Connection failed with status: '" +
					String::num(tcp_peer->get_status()) + "', retrying in " + String::num(ms) + " msec.");
		}
	}

	if (tcp_peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		ERR_PRINT("MessagePackRPC: Unable to connect. Status: " + String::num(tcp_peer->get_status()) + ".");
		return FAILED;
	}
	return OK;
}

Error MessagePackRPC::connect_to(const String &p_address, bool p_big_endian, uint64_t p_poll_interval) {
	if (p_address.begins_with("pipe:")) {
		// Try to connect to a named pipe.
		String addr = p_address.trim_prefix("pipe:");
		if (!named_pipe.is_valid()) {
			print_line("Create a file access reference.");
			named_pipe = FileAccess::create(FileAccess::ACCESS_FILESYSTEM);
		}
		if (!named_pipe->file_exists(addr)) {
			named_pipe.unref();
			print_line("Named pipe " + addr + " not exist.");
			ERR_FAIL_V_MSG(ERR_FILE_NOT_FOUND, "The named pipe " + addr + " does not exist.");
		}
		named_pipe.unref();
		named_pipe = FileAccess::open(addr, FileAccess::WRITE);
		if (!named_pipe.is_valid() || !named_pipe->is_open()) {
			named_pipe.unref();
			print_line("Can't open named pipe" + addr);
			ERR_FAIL_V_MSG(ERR_FILE_CANT_OPEN, "The named pipe " + addr + " can not open.");
		}
		named_pipe->set_big_endian(p_big_endian);
		// Connect with named pipe.
		if (start_stream(_read_stream) != OK) {
			print_line("Can't connect to named pipe" + addr);
			return ERR_CANT_CONNECT;
		}
		channel = PIPE;
		print_line("Prepare connect to named pipe" + addr);
	} else if (p_address.begins_with("tcp://")) {
		// TCP address
		String addr = p_address.trim_prefix("tcp://");
		PackedStringArray sub_str = addr.split(":");
		if (!sub_str[0].is_valid_ip_address() || !sub_str[1].is_valid_int()) {
			return ERR_INVALID_PARAMETER;
		}
		String ip = sub_str[0];
		uint32_t port = sub_str[1].to_int();
		if (!tcp_peer.is_valid()) {
			tcp_peer.instantiate();
		}
		// try to connect to the specified address.
		if (_tcp_try_connect(ip, port) != OK) {
			return ERR_CANT_CONNECT;
		}
		tcp_peer->set_big_endian(p_big_endian);
		if (start_stream(_read_stream) != OK) {
			return ERR_CANT_CONNECT;
		}
		channel = TCP;
	} else {
		return ERR_INVALID_PARAMETER;
	}

	connected = true;
	running = true;
	poll_interval = p_poll_interval;
	thread.start(_thread_func, this);

	return OK;
}

void MessagePackRPC::_message_received(const Variant &p_message) {
	if (p_message.get_type() == Variant::ARRAY) {
		Array msg_arr = p_message;
		if (msg_arr[0].get_type() != Variant::INT) {
			ERR_FAIL_MSG("Invalid message received: " + p_message.to_json_string());
		}
		switch (int(msg_arr[0])) {
			case 0:
				ERR_FAIL_COND_MSG(msg_arr.size() != 4, "Invalid message received: " + p_message.to_json_string());
				emit_signal(SNAME("request_received"), msg_arr[1], msg_arr[2], msg_arr[3]);
				break;
			case 1:
				ERR_FAIL_COND_MSG(msg_arr.size() != 4, "Invalid message received: " + p_message.to_json_string());
				emit_signal(SNAME("response_received"), msg_arr[1], msg_arr[2], msg_arr[3]);
				break;
			case 2:
				ERR_FAIL_COND_MSG(msg_arr.size() != 3, "Invalid message received: " + p_message.to_json_string());
				emit_signal(SNAME("notification_received"), msg_arr[1], msg_arr[2]);
				break;
			default:
				ERR_FAIL_MSG("Invalid message received: " + p_message.to_json_string());
		}
		emit_signal(SNAME("message_received"), p_message);
	} else {
		// Invalid message
		emit_signal(SNAME("got_error"), String("Invalid message: ") + p_message.to_json_string());
	}
}

void MessagePackRPC::close() {
	running = false;
	thread.wait_to_finish();
	connected = false;
	if (tcp_peer.is_valid()) {
		tcp_peer->disconnect_from_host();
	}
	if (named_pipe.is_valid()) {
		named_pipe.unref();
	}
}

size_t MessagePackRPC::_read_stream(mpack_tree_t *p_tree, char *r_buffer, size_t p_count) {
	MessagePackRPC *rpc = (MessagePackRPC *)mpack_tree_context(p_tree);
	MutexLock lock(rpc->mutex);
	if (rpc->in_tail <= rpc->in_head) {
		return 0; // No data to read.
	}
	int bytes_read = MIN(rpc->in_tail - rpc->in_head, p_count);
	print_line("Read", bytes_read, " bytes.", __FUNCTION__);
	memcpy(r_buffer, rpc->in_buf.ptr() + rpc->in_head, bytes_read);
	rpc->in_head += bytes_read;

	return bytes_read;
}

void MessagePackRPC::_tcp_write_out() {
	while (tcp_peer->get_status() == StreamPeerTCP::STATUS_CONNECTED && tcp_peer->wait(NetSocket::POLL_TYPE_OUT) == OK) {
		uint8_t *buf = out_buf.ptrw();
		if (out_head >= out_tail) {
			if (out_queue.size() == 0) {
				break; // Nothing left to send
			}
			mutex.lock();
			Array msg = out_queue[0];
			out_queue.pop_front();
			mutex.unlock();

			PackedByteArray msg_buf = make_message_buf(msg);
			ERR_CONTINUE(msg_buf.size() <= 0 || msg_buf.size() > _MSG_BUF_MAX_SIZE);

			memcpy(out_buf.ptrw(), msg_buf.ptr(), msg_buf.size());
			out_head = 0;
			out_tail = msg_buf.size();
		}
		int sent = 0;
		tcp_peer->put_partial_data(buf + out_head, out_tail - out_head, sent);
		out_head += sent;
	}
}

void MessagePackRPC::_tcp_read_in() {
	while (tcp_peer->get_status() == StreamPeerTCP::STATUS_CONNECTED && tcp_peer->wait(NetSocket::POLL_TYPE_IN) == OK) {
		uint8_t *buf = in_buf.ptrw();
		if (in_head >= in_tail) {
			if (in_queue.size() > _MSG_QUEUE_MAX_SIZE) {
				break; // Too many messages already in queue.
			}
			if (tcp_peer->get_available_bytes() < 1) {
				break;
			}
			int read = 0;
			Error err = tcp_peer->get_partial_data(buf, _MSG_BUF_MAX_SIZE, read);
			ERR_CONTINUE(err != OK || read <= 0);
			in_head = 0;
			in_tail = read;
		}
		int available = tcp_peer->get_available_bytes();
		int read = 0;
		if (in_tail + available < _MSG_BUF_MAX_SIZE) {
			tcp_peer->get_partial_data(buf + in_tail, available, read);
		} else {
			tcp_peer->get_partial_data(buf + in_tail, _MSG_BUF_MAX_SIZE - in_tail, read);
		}
		in_tail += read;
	}
}

void MessagePackRPC::_pipe_write_out() {
	while (named_pipe.is_valid() && named_pipe->is_open()) {
		// uint8_t *buf = out_buf.ptrw();
		if (out_head >= out_tail) {
			if (out_queue.size() == 0) {
				break; // Nothing left to send
			}
			mutex.lock();
			Array msg = out_queue[0];
			out_queue.pop_front();
			mutex.unlock();

			PackedByteArray msg_buf = make_message_buf(msg);
			ERR_CONTINUE(msg_buf.size() <= 0 || msg_buf.size() > _MSG_BUF_MAX_SIZE);

			named_pipe->_store_buffer(msg_buf);
			// print_line("[PIPE] byte copy:", msg_buf.size());
			// memcpy(out_buf.ptrw(), msg_buf.ptr(), msg_buf.size());
			// out_head = 0;
			// out_tail = msg_buf.size();
		} else {
			// print_line("[PIPE] [", out_head, "~", out_tail, "]");
			// named_pipe->store_buffer(buf + out_head, out_tail - out_head);
			// out_head = out_tail;
		}
	}
}

void MessagePackRPC::_pipe_read_in() {
	while (named_pipe.is_valid() && named_pipe->is_open()) {
		print_line("[PIPE] reading:", in_tail - in_head);
		uint8_t *buf = in_buf.ptrw();
		if (in_head >= in_tail) {
			if (in_queue.size() > _MSG_QUEUE_MAX_SIZE) {
				break; // Too many messages already in queue.
			}
			if (named_pipe->get_length() < 1) {
				break;
			}
			int64_t read = named_pipe->get_buffer(buf, in_buf.size());
			ERR_CONTINUE(read <= 0);
			in_head = 0;
			in_tail = read;
		}
		int available = named_pipe->get_length();
		int read = 0;
		if (in_tail + available < in_buf.size()) {
			read = named_pipe->get_buffer(buf + in_tail, available);
		} else {
			read = named_pipe->get_buffer(buf + in_tail, in_buf.size() - in_tail);
		}
		in_tail += read;
	}
}

void MessagePackRPC::poll() {
	if (connected) {
		if (channel == PIPE) {
			_pipe_write_out();
			// _pipe_read_in();
			connected = named_pipe.is_valid() && named_pipe->is_open();
		} else if (channel == TCP) {
			_tcp_write_out();
			_tcp_read_in();
			connected = tcp_peer->get_status() == StreamPeerTCP::STATUS_CONNECTED;
		} else {
			connected = false;
		}

		if (in_tail > in_head) {
			update_stream();
		}
	}
}

void MessagePackRPC::_thread_func(void *p_user_data) {
	MessagePackRPC *rpc = (MessagePackRPC *)p_user_data;
	while (rpc->running) {
		uint64_t ticks_usec = OS::get_singleton()->get_ticks_usec();
		rpc->poll();
		ticks_usec = OS::get_singleton()->get_ticks_msec() - ticks_usec;
		if (ticks_usec < rpc->poll_interval) {
			OS::get_singleton()->delay_usec(rpc->poll_interval - ticks_usec);
		}
	}
}

bool MessagePackRPC::is_connected() {
	if (channel == PIPE) {
		connected = named_pipe.is_valid() && named_pipe->is_open();
	} else if (channel == TCP) {
		connected = tcp_peer->get_status() == StreamPeerTCP::STATUS_CONNECTED;
	} else {
		connected = false;
	}
	return connected;
}

bool MessagePackRPC::has_message() {
	return in_queue.size() > 0;
}

Error MessagePackRPC::put_message(const Array &p_msg) {
	MutexLock lock(mutex);
	if (out_queue.size() >= _MSG_QUEUE_MAX_SIZE) {
		return ERR_OUT_OF_MEMORY;
	}

	out_queue.push_back(p_msg);
	return OK;
}

Array MessagePackRPC::get_message() {
	MutexLock lock(mutex);
	ERR_FAIL_COND_V(!has_message(), Array());
	Array out = in_queue[0];
	in_queue.pop_front();
	return out;
}

Variant MessagePackRPC::sync_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	return Variant();
}

Variant MessagePackRPC::sync_callv(const String &p_method, const Array &p_params) {
	return Variant();
}

Error MessagePackRPC::async_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	if (p_argcount < 1) {
		r_error.error = Callable::CallError::CALL_ERROR_TOO_FEW_ARGUMENTS;
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Too few arguments. Expected at least 1.");
	}
	if (p_args[0]->get_type() != Variant::STRING_NAME && p_args[0]->get_type() != Variant::STRING) {
		r_error.error = Callable::CallError::CALL_ERROR_INVALID_ARGUMENT;
		r_error.argument = 0;
		r_error.expected = Variant::STRING;
		ERR_FAIL_V_MSG(ERR_INVALID_PARAMETER, "Argument 'method' must be a string.");
	}

	String method = *p_args[0];
	ERR_FAIL_COND_V_MSG(method.is_empty(), ERR_INVALID_PARAMETER, "Argument 'method' must not be empty.");

	r_error.error = Callable::CallError::CALL_OK;

	Array args;
	args.resize(p_argcount - 1);
	for (int i = 0; i < p_argcount - 1; i++) {
		args[i] = *p_args[i + 1];
	}

	return async_callv(method, args);
}

Error MessagePackRPC::async_callv(const String &p_method, const Array &p_params) {
	ERR_FAIL_COND_V_MSG(!running, FAILED, "Connect to a peer first.");
	Array msg_req;
	msg_req.resize(4);
	msg_req[0] = 0;
	msg_req[1] = msgid;
	msg_req[2] = p_method;
	msg_req[3] = p_params;
	if (put_message(msg_req) != OK) {
		return ERR_OUT_OF_MEMORY;
	}
	msgid += 1;
	return OK;
}

MessagePackRPC::MessagePackRPC() {
	in_buf.resize(_MSG_BUF_MAX_SIZE);
	out_buf.resize(_MSG_BUF_MAX_SIZE);
}

MessagePackRPC::~MessagePackRPC() {
	close();
	out_buf.clear();
	in_buf.clear();
	if (started) {
		mpack_tree_destroy(&tree);
	}
}

void MessagePackRPC::_bind_methods() {
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_message_buf", "message"), &MessagePackRPC::make_message_buf);
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_request", "msg_id", "method", "params"), &MessagePackRPC::make_request, DEFVAL(Array()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_response", "msg_id", "result", "error"), &MessagePackRPC::make_response, DEFVAL(Array()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_notification", "method", "params"), &MessagePackRPC::make_notification, DEFVAL(Array()));

	ClassDB::bind_method(D_METHOD("connect_to", "address", "big_endian", "poll_interval"), &MessagePackRPC::connect_to, DEFVAL(false), DEFVAL(10000));
	ClassDB::bind_method(D_METHOD("close"), &MessagePackRPC::close);
	ClassDB::bind_method(D_METHOD("_message_received", "message"), &MessagePackRPC::_message_received);

	MethodInfo mi1;
	mi1.name = "sync_call";
	mi1.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "sync_call", &MessagePackRPC::sync_call, mi1);

	ClassDB::bind_method(D_METHOD("sync_callv", "method", "params"), &MessagePackRPC::sync_callv, DEFVAL(Array()));

	MethodInfo mi2;
	mi2.name = "async_call";
	mi2.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "async_call", &MessagePackRPC::async_call, mi2);

	ClassDB::bind_method(D_METHOD("async_callv", "method", "params"), &MessagePackRPC::async_callv, DEFVAL(Array()));

	ADD_SIGNAL(MethodInfo("got_error", PropertyInfo(Variant::STRING, "error")));
	ADD_SIGNAL(MethodInfo("message_received", PropertyInfo(Variant::ARRAY, "message")));
	ADD_SIGNAL(MethodInfo("request_received", PropertyInfo(Variant::INT, "msgid"), PropertyInfo(Variant::STRING, "method"), PropertyInfo(Variant::ARRAY, "params")));
	ADD_SIGNAL(MethodInfo("response_received", PropertyInfo(Variant::INT, "msgid"), PropertyInfo(Variant::OBJECT, "error"), PropertyInfo(Variant::ARRAY, "result")));
	ADD_SIGNAL(MethodInfo("notification_received", PropertyInfo(Variant::STRING, "method"), PropertyInfo(Variant::ARRAY, "params")));
}
