struct TaskParameters { uint groupCount; };
[[vk::push_constant]] ConstantBuffer<TaskParameters> task;
groupshared TaskParameters payload;

[shader("amplification")]
[numthreads(1, 1, 1)]
void main() {
    payload.groupCount = task.groupCount;
    DispatchMesh(payload.groupCount, 1, 1, payload);
}
