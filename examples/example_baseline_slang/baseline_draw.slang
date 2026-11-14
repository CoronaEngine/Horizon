#include "../raster_batch.slang"

// Matches the GLM matrix and texture index uploaded for each draw.
struct DrawParameters {
    column_major float4x4 model;
    uint texIndex;
};

DrawParameters* getDrawParameters(uint parameterWord) {
    return (DrawParameters*)(drawRoot.parameters + parameterWord);
}
