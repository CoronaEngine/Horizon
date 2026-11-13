#version 450

layout(set = 3, binding = 0) uniform Scene {
    mat4 view;
    mat4 proj;
    uint instanceBufferID;
    vec4 color0;
    vec4 color1;
    vec4 color2;
    vec4 color3;
} vp;

vec4 material_color(int id)
{
    switch (id)
    {
    default: return vp.color0;
    case 0: return vp.color0;
    case 1: return vp.color1;
    case 2: return vp.color2;
    case 3: return vp.color3;
    }
}

layout(location = 0) flat in int v_materialID;

layout(location = 0) out vec4 outColor;

void main()
{
    vec4 color = material_color(v_materialID);

    // Dithered transparency for walls (alpha < 1), matching bgfx.
    if (color.a < 0.999)
    {
        ivec2 p = ivec2(gl_FragCoord.xy);
        if (((p.x + p.y) & 1) == 0)
            discard;
    }

    outColor = vec4(color.rgb, 1.0);
}
