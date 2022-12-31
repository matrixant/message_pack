# message_pack
A godot module to support MessagePack serialization protocol

> NOTE: Just in developing.

## Usage:
1. Clone/Copy the repository to `godot/modules` directory, then compile godot.
2. The `MessagePack` class will add to godot. You can use `MessagePack.encode(variant)` to encode a godot variant to `MessagePack` byte array, or use `MessagePack.decode(msg_buf)` decode the MessagePack byte array to a variant.
