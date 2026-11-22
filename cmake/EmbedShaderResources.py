import argparse
import pathlib
import struct


def read_words(path):
    data = path.read_bytes()
    if not data or len(data) % 4:
        raise ValueError(f"{path} is not a nonempty SPIR-V word stream")

    words = [word[0] for word in struct.iter_unpack("<I", data)]
    if words[0] != 0x07230203:
        raise ValueError(f"{path} has an invalid SPIR-V magic number")
    return words


def cpp_string_literal(value):
    escaped = []
    for byte in value.encode("utf-8"):
        if byte in (ord('"'), ord("\\")):
            escaped.append("\\" + chr(byte))
        elif 0x20 <= byte < 0x7F:
            escaped.append(chr(byte))
        else:
            escaped.append(f"\\{byte:03o}")
    return '"' + "".join(escaped) + '"'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--template", required=True, type=pathlib.Path)
    parser.add_argument("--output", required=True, type=pathlib.Path)
    parser.add_argument("--namespace", required=True)
    parser.add_argument("--shader", action="append", nargs=2, required=True, metavar=("KEY", "SPV"))
    arguments = parser.parse_args()

    arrays = []
    entries = []
    for index, (key, spirv_name) in enumerate(arguments.shader):
        stage = {".vert": "Vertex", ".frag": "Fragment"}.get(pathlib.PurePosixPath(key).suffix)
        if stage is None:
            parser.error(f"unsupported shader key: {key}")

        try:
            words = read_words(pathlib.Path(spirv_name))
        except (OSError, ValueError) as error:
            parser.error(str(error))
        arrays.append(
            f"    inline constexpr std::array<std::uint32_t, {len(words)}> shader{index} {{\n"
            + "\n".join(f"        0x{word:08X}U," for word in words)
            + "\n    };"
        )
        entries.append(
            "ShaderResource{.key = "
            + cpp_string_literal(key)
            + f", .stage = Stage::{stage}, .words = std::span<const std::uint32_t>{{detail::shader{index}}}}},"
        )

    template = arguments.template.read_text(encoding="utf-8")
    generated = template
    for placeholder, value in (
        ("PANDA_SHADER_NAMESPACE_PLACEHOLDER", arguments.namespace),
        ("PANDA_SHADER_COUNT_PLACEHOLDER", str(len(entries))),
        ("PANDA_SHADER_ARRAYS_PLACEHOLDER", "\n\n".join(arrays)),
        ("PANDA_SHADER_ENTRIES_PLACEHOLDER", "\n    ".join(entries) + "\n"),
    ):
        generated = generated.replace(placeholder, value)

    arguments.output.parent.mkdir(parents=True, exist_ok=True)
    with arguments.output.open("w", encoding="utf-8", newline="\n") as output:
        output.write(generated)


if __name__ == "__main__":
    main()
