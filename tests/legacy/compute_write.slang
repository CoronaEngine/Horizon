struct Parameters {
    uint outputIndex;
    uint value;
};

[[vk::push_constant]] ConstantBuffer<Parameters> params;
[[vk::binding(0, 1)]] RWStructuredBuffer<uint> outputs[];

[shader("compute")]
[numthreads(1, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    outputs[params.outputIndex][id.x] = params.value + id.x;
}
