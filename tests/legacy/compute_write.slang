struct Parameters {
    uint outputIndex;
    uint value;
};

[[vk::push_constant]] ConstantBuffer<Parameters> params;
[[vk::binding(0, 1)]] RWStructuredBuffer<uint> outputs[];

public uint addIndex(uint value, uint index) {
    return value + index;
}

public void accumulateValue(inout uint total, out uint mirror) {
    total += 2;
    mirror = total;
}

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint index = id.x + 2 * id.y + 4 * id.z;
    outputs[params.outputIndex][index] = addIndex(params.value, index);
}
