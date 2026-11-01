# Error handling example

`error_handling` creates a headless Vulkan context and deliberately requests a
zero-byte buffer. It logs the error category, message, source location and native
status when present. Only `InvalidArgument` triggers a corrected 4096-byte request;
other errors propagate to `main`, which reports them and exits with failure.
An `expect` checks that the deliberately invalid request was rejected.
Requires Vulkan 1.3 and the Khronos validation layer; no display or CUDA is needed.

Compare with [buffer allocation](../buffer_allocation/README.md): `expect` keeps
that example linear, but aborts on failure without unwinding local RAII objects.
Inspect `Result` when the application needs cleanup, recovery or a controlled exit.
`shouldBe` warns and returns a boolean; it neither unwraps a result nor makes it
safe to continue using a failed operation.

Windows:

```powershell
cmake --preset msvc-development
cmake --build --preset msvc-development --target error_handling
.\build-msvc-development\examples\error_handling\error_handling.exe
```

Linux:

```sh
cmake --preset gcc-development
cmake --build --preset gcc-development --target error_handling
./build-gcc-development-Linux/examples/error_handling/error_handling
```

A successful run logs the intentional validation error and the successful retry,
then exits with zero. It does not simulate device loss or resource exhaustion.
