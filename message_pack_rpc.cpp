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

PackedByteArray MessagePackRPC::make_request(int p_msgid, const String &p_method, const Variant &p_params) {
	Array msg;
	ERR_FAIL_COND_V(msg.resize(4) != OK, PackedByteArray());
	msg[0] = 0;
	msg[1] = p_msgid;
	msg[2] = p_method;
	msg[3] = p_params;
	Array pack = MessagePack::pack(msg);
	ERR_FAIL_COND_V_MSG(int(pack[0]) != OK, PackedByteArray(), String("Some error occurred while packing request:") + String(pack[1]));
	return pack[1];
}

PackedByteArray MessagePackRPC::make_response(int p_msgid, const Variant &p_result, const Variant &p_error) {
	Array msg;
	ERR_FAIL_COND_V(msg.resize(4) != OK, PackedByteArray());
	msg[0] = 1;
	msg[1] = p_msgid;
	msg[2] = p_error;
	msg[3] = p_result;
	Array pack = MessagePack::pack(msg);
	ERR_FAIL_COND_V_MSG(int(pack[0]) != OK, PackedByteArray(), String("Some error occurred while packing response:") + String(pack[1]));
	return pack[1];
}

PackedByteArray MessagePackRPC::make_notification(const String &p_method, const Variant &p_params) {
	Array msg;
	ERR_FAIL_COND_V(msg.resize(3) != OK, PackedByteArray());
	msg[0] = 2;
	msg[1] = p_method;
	msg[2] = p_params;
	Array pack = MessagePack::pack(msg);
	ERR_FAIL_COND_V_MSG(int(pack[0]) != OK, PackedByteArray(), String("Some error occurred while packing response:") + String(pack[1]));
	return pack[1];
}

MessagePackRPC::MessagePackRPC() {
}

MessagePackRPC::~MessagePackRPC() {
}

void MessagePackRPC::_bind_methods() {
	// ClassDB::bind_static_method("MessagePackRPC", D_METHOD("unpack", "msg_buf"), &MessagePackRPC::unpack);
	// ClassDB::bind_static_method("MessagePackRPC", D_METHOD("pack", "data"), &MessagePackRPC::pack);

	ClassDB::bind_method(D_METHOD("make_request", "msg_id", "method", "params"), &MessagePackRPC::make_request);
	ClassDB::bind_method(D_METHOD("make_response", "msg_id", "result", "error"), &MessagePackRPC::make_response, DEFVAL(Variant()));
}
