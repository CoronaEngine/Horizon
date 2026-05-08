from os.path import realpath, dirname


def generate(file, dim):
    entries = ["x", "y", "z", "w"][:dim]
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            str = f"[[nodiscard]] auto {x}{y}_() const {{ return eval<Vector<T, 2>>(Function::current()->swizzle(Type::of<Vector<T, 2>>(), expression(), 0x{i}{j}, 2)); }}"
            print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            for k,z in enumerate(entries):
                str = f"[[nodiscard]] auto {x}{y}{z}_() const {{ return eval<Vector<T, 3>>(Function::current()->swizzle(Type::of<Vector<T, 3>>(), expression(), 0x{i}{j}{k}, 3)); }}"
                print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            for k,z in enumerate(entries):
                for l,w in enumerate(entries):
                    str = f"[[nodiscard]] auto {x}{y}{z}{w}_() const {{ return eval<Vector<T, 4>>(Function::current()->swizzle(Type::of<Vector<T, 4>>(), expression(), 0x{i}{j}{k}{l}, 4)); }}"
                    print(str, file=file)

def dynamic_array_swizzle(file, dim):
    entries = ["x", "y", "z", "w"]
    for i,x in enumerate(entries):
        str = f"[[nodiscard]] DynamicArray<T> {x}() const {{ OC_ASSERT(size_ > {i}); return DynamicArray<T>::create(at({i})); }}"
        print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            str = f"[[nodiscard]] DynamicArray<T> {x}{y}() const {{ OC_ASSERT(size_ > {max(i,j)}); return DynamicArray<T>::create(at({i}), at({j})); }}"
            print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            for k,z in enumerate(entries):
                str = f"[[nodiscard]] DynamicArray<T> {x}{y}{z}() const {{ OC_ASSERT(size_ > {max(i,j,k)}); return DynamicArray<T>::create(at({i}), at({j}), at({k})); }}"
                print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            for k,z in enumerate(entries):
                for l,w in enumerate(entries):
                    str = f"[[nodiscard]] DynamicArray<T> {x}{y}{z}{w}() const {{ OC_ASSERT(size_ > {max(i,j,k,l)}); return DynamicArray<T>::create(at({i}), at({j}), at({k}), at({l})); }}"
                    print(str, file=file)

def array_swizzle(file, dim):
    entries = ["x", "y", "z", "w"]

    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            str = f"[[nodiscard]] auto {x}{y}() const {{ return as_vec2().{x}{y}(); }}"
            print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            for k,z in enumerate(entries):
                str = f"[[nodiscard]] auto {x}{y}{z}() const {{ return as_vec3().{x}{y}{z}(); }}"
                print(str, file=file)
    print("", file=file)
    for i,x in enumerate(entries):
        for j,y in enumerate(entries):
            for k,z in enumerate(entries):
                for l,w in enumerate(entries):
                    str = f"[[nodiscard]] auto {x}{y}{z}{w}() const {{ return as_vec4().{x}{y}{z}{w}(); }}"
                    print(str, file=file)

if __name__ == "__main__":
    base = dirname(realpath(__file__))
    # for i,k in enumerate(["a", "b"]):
    #     print(i,k)
    # for dim in range(2, 5):
    #     with open(f"{base}/swizzle_{dim}.inl.h", "w") as file:
    #         generate(file, dim)

    with open(f"{base}/dynamic_array_swizzle.inl.h", "w") as file:
        dynamic_array_swizzle(file, 4)

    with open(f"{base}/array_swizzle.inl.h", "w") as file:
        array_swizzle(file, 4)