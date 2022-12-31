/*************************************************************************/
/*  message_pack_rpc.h                                                       */
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

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/typed_array.h"

class MessagePackRPC : public Object {
	GDCLASS(MessagePackRPC, Object);

protected:
	static void _bind_methods();

public:
	static PackedByteArray make_request(int p_msgid, const String &p_method, const Array &p_params = Array());
	static PackedByteArray make_response(int p_msgid, const Variant &p_result, const Variant &p_error = Array());
	static PackedByteArray make_notification(const String &p_method, const Array &p_params = Array());

	Error connect_to(const String &p_address);
	Error poll();

	Variant sync_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Variant sync_callv(const String &p_method, const Array &p_params = Array());
	Error async_call(const Variant **p_args, int p_argcount, Callable::CallError &r_error);
	Error async_callv(const String &p_method, const Callable &p_return_call, const Array &p_params = Array());

	MessagePackRPC();
	~MessagePackRPC();
};

#endif // MESSAGE_PACK_RPC_H
