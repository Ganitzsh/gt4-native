# Native callback bridge

`gt4::guest::NativeCallbacks` registers host C++ callbacks against original PS2 function addresses. A call can be resolved from the documented ee-gcc 2.96 vtable representation in guest memory and forwarded to the registered native function with the adjusted guest `this` address.

This is a **narrow transitional integration seam**, not a complete PS2 ABI bridge. It currently supports one 32-bit argument and a 32-bit result, with a zero vtable index field. Unbound function addresses and unsupported vtable entries fail explicitly. It cannot execute MIPS code, translate arbitrary C++ types, or dispatch unported functions.

Next step: register a genuinely matched upstream leaf function (with source revision and license provenance) and compare its native behavior against a reference.
