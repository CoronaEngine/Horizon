from os.path import realpath, dirname


def generate(file, dim):
    entries = ["x", "y", "z", "w"][:dim]
    color_entries = ["r", "g", "b", "a"][:dim]
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            print(f"swizzle_type<{i}, {j}>& {x}{y}() noexcept {{ return *reinterpret_cast<swizzle_type<{i}, {j}>*>(addressof(x)); }};", file=file)

    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            print(f"auto {x}{y}() const noexcept {{ return (*reinterpret_cast<const swizzle_type<{i}, {j}>*>(addressof(x))); }};", file=file)

    print("", file=file)

    for i, x in enumerate(entries):
        for j, y in enumerate(entries):
            for k, z in enumerate(entries):
                print(f"swizzle_type<{i}, {j}, {k}>& {x}{y}{z}() noexcept {{ return *reinterpret_cast<swizzle_type<{i}, {j}, {k}>*>(addressof(x)); }};",
                      file=file)
    for i, x in enumerate(entries):
        for j, y in enumerate(entries):
            for k, z in enumerate(entries):
                print(f"auto {x}{y}{z}() const noexcept {{ return (*reinterpret_cast<const swizzle_type<{i}, {j}, {k}>*>(addressof(x))); }};",
                      file=file)
    print("", file=file)

    for i, x in enumerate(entries):
        for j, y in enumerate(entries):
            for k, z in enumerate(entries):
                for n, w in enumerate(entries):
                    print(f"swizzle_type<{i}, {j}, {k}, {n}>& {x}{y}{z}{w}() noexcept {{ return *reinterpret_cast<swizzle_type<{i}, {j}, {k}, {n}>*>(addressof(x)); }};",
                          file=file)

    for i, x in enumerate(entries):
        for j, y in enumerate(entries):
            for k, z in enumerate(entries):
                for n, w in enumerate(entries):
                    print(f"auto {x}{y}{z}{w}() const noexcept {{ return (*reinterpret_cast<const swizzle_type<{i}, {j}, {k}, {n}>*>(addressof(x))); }};",
                          file=file)
    


if __name__ == "__main__":
    base = dirname(realpath(__file__))
    for dim in range(2, 5):
        with open(f"{base}/swizzle{dim}.inl.h", "w") as file:
            generate(file, dim)
