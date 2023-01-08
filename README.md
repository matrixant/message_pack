# message_pack
A godot module to support MessagePack serialization protocol

> NOTE: Just in developing.

## Usage:
1. Clone/Copy the repository to `godot/modules` directory, then compile godot.
2. The `MessagePack` class will add to godot. You can use `MessagePack.encode(variant)` to encode a godot variant to `MessagePack` byte array, or use `MessagePack.decode(msg_buf)` decode the MessagePack byte array to a variant.
3. There is a simple `MessagePackRPC` class added to godot. You can use it to communicate with other peers which use messagepack rpc too. There is a simple example [MessagePack example](https://github.com/matrixant/message_pack_example).

![screenshot](https://raw.githubusercontent.com/matrixant/message_pack_example/main/screen_shot_0.png)
