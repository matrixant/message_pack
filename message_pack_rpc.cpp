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

#include "message_pack_rpc.h"
#include "core/os/memory.h"
#include "message_pack.h"

PackedByteArray MessagePackRPC::make_request(int p_msgid, const String &p_method, const Array &p_params) {
	Array msg;
	ERR_FAIL_COND_V(msg.resize(4) != OK, Variant());
	msg[0] = 0;
	msg[1] = p_msgid;
	msg[2] = p_method;
	msg[3] = p_params;
	Array pack = MessagePack::encode(msg);
	ERR_FAIL_COND_V_MSG(int(pack[0]) != OK, Variant(), "Some error occurred while packing request: " + String(pack[1]));
	return pack[1];
}

PackedByteArray MessagePackRPC::make_response(int p_msgid, const Variant &p_result, const Variant &p_error) {
	Array msg;
	ERR_FAIL_COND_V(msg.resize(4) != OK, Variant());
	msg[0] = 1;
	msg[1] = p_msgid;
	msg[2] = p_error;
	msg[3] = p_result;
	Array pack = MessagePack::encode(msg);
	ERR_FAIL_COND_V_MSG(int(pack[0]) != OK, Variant(), "Some error occurred while packing response: " + String(pack[1]));
	return pack[1];
}

PackedByteArray MessagePackRPC::make_notification(const String &p_method, const Array &p_params) {
	Array msg;
	ERR_FAIL_COND_V(msg.resize(3) != OK, Variant());
	msg[0] = 2;
	msg[1] = p_method;
	msg[2] = p_params;
	Array pack = MessagePack::encode(msg);
	ERR_FAIL_COND_V_MSG(int(pack[0]) != OK, Variant(), "Some error occurred while packing notification: " + String(pack[1]));
	return pack[1];
}

Variant MessagePackRPC::sync_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	return Variant();
}

Variant MessagePackRPC::sync_callv(const String &p_method, const Array &p_params) {
	return Variant();
}

Error MessagePackRPC::async_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error) {
	r_error.error = Callable::CallError::CALL_OK;

	ERR_FAIL_COND_V_MSG(p_argcount < 2, ERR_INVALID_PARAMETER, "Too few arguments. Expected at least 2.");
	ERR_FAIL_COND_V_MSG(p_args[0]->get_type() != Variant::STRING_NAME && p_args[0]->get_type() != Variant::STRING, ERR_INVALID_PARAMETER, "method must be a string.");
	ERR_FAIL_COND_V_MSG(p_args[1]->get_type() != Variant::CALLABLE, ERR_INVALID_PARAMETER, "return_call must be a callable.");

	String method = *p_args[0];
	Callable return_call = *p_args[1];

	ERR_FAIL_COND_V_MSG(method.is_empty(), ERR_INVALID_PARAMETER, "The method argument must not be empty.");
	ERR_FAIL_COND_V_MSG(!return_call.is_valid(), ERR_INVALID_PARAMETER, "Invalid Callabe.");

	return OK;
}

Error MessagePackRPC::async_callv(const String &p_method, const Callable &p_return_call, const Array &p_params) {
	return OK;
}

MessagePackRPC::MessagePackRPC() {
}

MessagePackRPC::~MessagePackRPC() {
}

void MessagePackRPC::_bind_methods() {
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_request", "msg_id", "method", "params"), &MessagePackRPC::make_request, DEFVAL(Array()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_response", "msg_id", "result", "error"), &MessagePackRPC::make_response, DEFVAL(Array()));
	ClassDB::bind_static_method("MessagePackRPC", D_METHOD("make_notification", "method", "params"), &MessagePackRPC::make_notification, DEFVAL(Array()));

	MethodInfo mi1;
	mi1.name = "sync_call";
	mi1.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "sync_call", &MessagePackRPC::sync_call, mi1);

	ClassDB::bind_method(D_METHOD("sync_callv", "method", "params"), &MessagePackRPC::sync_callv, DEFVAL(Array()));

	MethodInfo mi2;
	mi2.name = "async_call";
	mi2.arguments.push_back(PropertyInfo(Variant::STRING, "method"));
	mi2.arguments.push_back(PropertyInfo(Variant::CALLABLE, "result"));
	ClassDB::bind_vararg_method(METHOD_FLAGS_DEFAULT, "async_call", &MessagePackRPC::async_call, mi2);

	ClassDB::bind_method(D_METHOD("async_callv", "method", "return_call", "params"), &MessagePackRPC::async_callv, DEFVAL(Array()));
}
