# message_pack
A godot module to support MessagePack serialization protocol

## Usage:
1. Clone/Copy the repository to `godot/modules` directory, then compile godot.
2. The `MessagePack` class will add to godot. You can use `MessagePack.pack(variant)` to make a MessagePack byte array, or use `MessagePack.unpack(msg_buf)` unpack the MessagePack byte array to a variant.
